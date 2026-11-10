// SPDX-License-Identifier: MIT
// csa-align: command-line front end.
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "csa/aligner.hpp"
#include "csa/document.hpp"
#include "csa/evaluate.hpp"
#include "csa/export.hpp"

namespace {

const char* const kUsage = R"(csa-align - Gale-Church sentence aligner for bilingual corpora

Usage:
  csa-align [options] SOURCE TARGET
  csa-align [options] --pairs LIST

Inputs are UTF-8 (or UTF-16 with BOM) plain-text files, one paragraph per
line, or Word .docx files. LIST is a text file with one document pair per
line: SOURCE<TAB>TARGET (paths relative to the list file; '#' comments).

Output:
  -o, --output FILE      write to FILE (default: standard output)
  -f, --format FMT       tmx | csv | tsv | xlsx  (default: from -o extension, else tmx)
      --keep-unaligned   TMX: also write 1-0 / 0-1 units (CSV/TSV/XLSX always do)
      --csv-sep CHAR     CSV field separator (default ','; use ';' for French Excel)
      --no-bom           CSV: do not write a UTF-8 byte-order mark
      --date STAMP       TMX creation date, YYYYMMDDThhmmssZ (default: now)

Languages and segmentation:
  -s, --src-lang TAG     source language (default en)
  -t, --tgt-lang TAG     target language (default fr)
      --presegmented     inputs already have one sentence per line
      --blank-line-paragraphs
                         plain text: paragraphs are separated by blank lines
      --src-abbrev FILE  extra source abbreviations (one per line, '*' = non-blocking)
      --tgt-abbrev FILE  extra target abbreviations
      --keep-superscript keep superscript citation numbers from .docx files
      --br-as-space      .docx: manual line breaks join lines instead of splitting

Alignment model:
      --mode MODE        hier (paragraphs, then sentences; default) | flat
      --c VALUE          target/source character ratio (default: estimated)
      --s2 VALUE         variance per character (default 6.8)
      --extended-beads   also allow 3-1 and 1-3 sentence beads
      --no-anchors       pure length model (ignore shared numbers/acronyms/URLs)
      --classic          original Gale-Church scoring (implies --no-anchors):
                         unmatched sentences use the normal match model
      --anchor-weight W  cost per mismatched anchor (default 1.0)
      --review-delta Z   flag beads whose length z-score exceeds Z (default 2.5)

Diagnostics:
      --gold FILE        compare with a reference alignment and print scores
      --dump-segments    print the numbered sentences of both sides and exit
  -q, --quiet            no summary on standard error
  -h, --help             show this help
  -v, --version          show version
)";

struct Cli {
    std::vector<std::string> positional;
    std::string output, format, pairs, gold, date;
    csa::AlignerOptions align;
    csa::LoadOptions load;
    bool keep_unaligned = false, no_bom = false, dump = false, quiet = false;
    char csv_sep = ',';
};

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "csa-align: " << msg << "\n";
    std::exit(2);
}

double to_double(const std::string& opt, const std::string& v) {
    try {
        std::size_t used = 0;
        double d = std::stod(v, &used);
        if (used != v.size()) throw std::invalid_argument(v);
        return d;
    } catch (...) {
        die("invalid number for " + opt + ": " + v);
    }
}

Cli parse(int argc, char** argv) {
    Cli cli;
    auto need = [&](int& i, const std::string& opt) -> std::string {
        if (i + 1 >= argc) die("missing value for " + opt);
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        std::string val;
        // Support --opt=value.
        if (a.size() > 2 && a.compare(0, 2, "--") == 0) {
            auto eq = a.find('=');
            if (eq != std::string::npos) {
                val = a.substr(eq + 1);
                a = a.substr(0, eq);
                argv[i] = nullptr;
            }
        }
        auto value = [&](const std::string& opt) { return argv[i] == nullptr ? val : need(i, opt); };
        if (a == "-h" || a == "--help") {
            std::cout << kUsage;
            std::exit(0);
        } else if (a == "-v" || a == "--version") {
            std::cout << "csa-align " << csa::kVersion << "\n";
            std::exit(0);
        } else if (a == "-o" || a == "--output") cli.output = value(a);
        else if (a == "-f" || a == "--format") cli.format = value(a);
        else if (a == "--pairs") cli.pairs = value(a);
        else if (a == "--gold") cli.gold = value(a);
        else if (a == "--date") cli.date = value(a);
        else if (a == "-s" || a == "--src-lang") cli.align.src_lang = value(a);
        else if (a == "-t" || a == "--tgt-lang") cli.align.tgt_lang = value(a);
        else if (a == "--mode") {
            std::string m = value(a);
            if (m == "hier" || m == "hierarchical") cli.align.mode = csa::AlignMode::Hierarchical;
            else if (m == "flat") cli.align.mode = csa::AlignMode::Flat;
            else die("unknown mode: " + m);
        } else if (a == "--c") {
            std::string v = value(a);
            cli.align.c = (v == "auto") ? 0.0 : to_double(a, v);
            if (v != "auto" && cli.align.c <= 0) die("--c must be > 0");
        } else if (a == "--s2") {
            cli.align.s2 = to_double(a, value(a));
            if (cli.align.s2 <= 0) die("--s2 must be > 0");
        } else if (a == "--anchor-weight") cli.align.anchor_weight = to_double(a, value(a));
        else if (a == "--review-delta") cli.align.review_delta = to_double(a, value(a));
        else if (a == "--extended-beads") cli.align.extended_beads = true;
        else if (a == "--no-anchors") cli.align.anchors = false;
        else if (a == "--classic") {
            cli.align.classic = true;
            cli.align.anchors = false;
        }
        else if (a == "--presegmented") cli.align.presegmented = true;
        else if (a == "--src-abbrev") cli.align.src_abbrev_files.push_back(value(a));
        else if (a == "--tgt-abbrev") cli.align.tgt_abbrev_files.push_back(value(a));
        else if (a == "--blank-line-paragraphs") cli.load.blank_line_paragraphs = true;
        else if (a == "--keep-superscript") cli.load.keep_superscript_numbers = true;
        else if (a == "--br-as-space") cli.load.line_break_is_paragraph = false;
        else if (a == "--keep-unaligned") cli.keep_unaligned = true;
        else if (a == "--no-bom") cli.no_bom = true;
        else if (a == "--csv-sep") {
            std::string v = value(a);
            if (v == "\\t" || v == "tab") v = "\t";
            if (v.size() != 1) die("--csv-sep takes a single character");
            cli.csv_sep = v[0];
        } else if (a == "--dump-segments") cli.dump = true;
        else if (a == "-q" || a == "--quiet") cli.quiet = true;
        else if (!a.empty() && a[0] == '-' && a != "-") die("unknown option: " + a + " (see --help)");
        else cli.positional.push_back(a);
    }
    if (cli.pairs.empty() && cli.positional.size() != 2) die("expected SOURCE and TARGET files (see --help)");
    if (!cli.pairs.empty() && !cli.positional.empty()) die("--pairs cannot be combined with SOURCE/TARGET");
    if (!cli.gold.empty() && !cli.pairs.empty()) die("--gold works with a single document pair");
    if (cli.format.empty()) {
        std::string o = cli.output;
        auto dot = o.find_last_of('.');
        std::string ext = dot == std::string::npos ? "" : o.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        cli.format = (ext == "csv" || ext == "tsv" || ext == "xlsx" || ext == "tmx") ? ext : "tmx";
    }
    if (cli.format != "tmx" && cli.format != "csv" && cli.format != "tsv" && cli.format != "xlsx")
        die("unknown format: " + cli.format);
    if (cli.format == "xlsx" && cli.output.empty()) die("xlsx output needs -o FILE");
    return cli;
}

std::string base_name(const std::string& path) {
    auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string dir_name(const std::string& path) {
    auto p = path.find_last_of("/\\");
    return p == std::string::npos ? "" : path.substr(0, p + 1);
}

std::vector<std::pair<std::string, std::string>> read_pairs(const std::string& list) {
    std::ifstream in(list);
    if (!in) die("cannot open pair list: " + list);
    std::vector<std::pair<std::string, std::string>> out;
    std::string line;
    std::string dir = dir_name(list);
    std::size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineno == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) die(list + ":" + std::to_string(lineno) + ": expected SOURCE<TAB>TARGET");
        std::string s = line.substr(0, tab), t = line.substr(tab + 1);
        auto absolute = [](const std::string& p) {
            return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
        };
        if (!absolute(s)) s = dir + s;
        if (!absolute(t)) t = dir + t;
        out.push_back({s, t});
    }
    return out;
}

void summary(const std::string& name, const csa::AlignmentResult& r) {
    std::map<std::string, std::size_t> types;
    std::size_t review = 0;
    for (const auto& p : r.pairs) {
        types[p.type()]++;
        review += p.review ? 1 : 0;
    }
    std::cerr << name << ": " << r.src.size() << " source / " << r.tgt.size() << " target sentences ("
              << r.src_paragraphs << " / " << r.tgt_paragraphs << " paragraphs), c=" << r.c_used << "\n  beads:";
    for (const auto& t : types) std::cerr << " " << t.first << "=" << t.second;
    std::cerr << "  review=" << review << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    Cli cli = parse(argc, argv);
    std::vector<std::pair<std::string, std::string>> jobs;
    if (cli.pairs.empty()) jobs.push_back({cli.positional[0], cli.positional[1]});
    else jobs = read_pairs(cli.pairs);

    std::vector<csa::DocumentAlignment> docs;
    int failures = 0;
    for (const auto& job : jobs) {
        try {
            auto sp = csa::load_paragraphs(job.first, cli.load);
            auto tp = csa::load_paragraphs(job.second, cli.load);
            if (cli.dump) {
                auto ss = csa::segment_paragraphs(sp, cli.align.src_lang, cli.align.presegmented,
                                                  cli.align.src_abbrev_files);
                auto ts = csa::segment_paragraphs(tp, cli.align.tgt_lang, cli.align.presegmented,
                                                  cli.align.tgt_abbrev_files);
                std::cout << "side\tid\tparagraph\tlength\ttext\n";
                for (std::size_t i = 0; i < ss.size(); ++i)
                    std::cout << "src\t" << i + 1 << '\t' << ss[i].paragraph + 1 << '\t' << ss[i].length << '\t'
                              << ss[i].text << "\n";
                for (std::size_t i = 0; i < ts.size(); ++i)
                    std::cout << "tgt\t" << i + 1 << '\t' << ts[i].paragraph + 1 << '\t' << ts[i].length << '\t'
                              << ts[i].text << "\n";
                continue;
            }
            csa::DocumentAlignment d;
            d.name = base_name(job.first);
            d.result = csa::align_documents(sp, tp, cli.align);
            if (!cli.quiet) summary(d.name, d.result);
            docs.push_back(std::move(d));
        } catch (const std::exception& e) {
            std::cerr << "csa-align: " << job.first << " / " << job.second << ": " << e.what() << "\n";
            ++failures;
            if (jobs.size() == 1) return 1;
        }
    }
    if (cli.dump) return failures ? 1 : 0;

    if (!cli.gold.empty()) {
        try {
            auto gold = csa::read_gold(cli.gold);
            auto sc = csa::evaluate(docs.front().result.pairs, gold);
            std::cerr.setf(std::ios::fixed);
            std::cerr.precision(4);
            std::cerr << "gold: links P=" << sc.link_precision() << " R=" << sc.link_recall()
                      << " F1=" << sc.link_f1() << " (" << sc.correct_links << "/" << sc.test_links << " test, "
                      << sc.gold_links << " gold)\n"
                      << "      beads P=" << sc.bead_precision() << " R=" << sc.bead_recall() << " ("
                      << sc.correct_beads << " exact of " << sc.test_beads << " test, " << sc.gold_beads
                      << " gold)\n";
        } catch (const std::exception& e) {
            die(e.what());
        }
    }

    csa::ExportOptions eo;
    eo.src_lang = cli.align.src_lang;
    eo.tgt_lang = cli.align.tgt_lang;
    eo.keep_unaligned = cli.keep_unaligned;
    eo.csv_bom = !cli.no_bom;
    eo.csv_separator = cli.csv_sep;
    eo.creation_date = cli.date;

    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!cli.output.empty()) {
        file.open(cli.output, std::ios::binary);
        if (!file) die("cannot write " + cli.output);
        out = &file;
    }
    if (cli.format == "tmx") csa::write_tmx(*out, docs, eo);
    else if (cli.format == "csv") csa::write_csv(*out, docs, eo);
    else if (cli.format == "tsv") csa::write_tsv(*out, docs, eo);
    else csa::write_xlsx(*out, docs, eo);
    out->flush();
    if (!*out) die("write error");
    if (failures) std::cerr << "csa-align: " << failures << " document pair(s) failed\n";
    return failures ? 1 : 0;
}

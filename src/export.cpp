// SPDX-License-Identifier: MIT
#include "csa/export.hpp"

#include <cstdio>
#include <ctime>
#include <sstream>

#include "csa/utf8.hpp"
#include "csa/zip.hpp"

namespace csa {

namespace {

std::string fmt(double v, int decimals = 3) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string utc_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::gmtime(&now);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", t);
    return buf;
}

struct Row {
    std::string document;
    std::size_t bead;
    const AlignedPair* pair;
};

std::vector<Row> rows_of(const std::vector<DocumentAlignment>& docs) {
    std::vector<Row> rows;
    for (const auto& d : docs) {
        std::size_t k = 0;
        for (const auto& p : d.result.pairs) rows.push_back({d.name, ++k, &p});
    }
    return rows;
}

const char* const kColumns[] = {"document", "bead", "type", "src_ids", "tgt_ids",
                                "source",   "target", "cost", "delta", "review"};

void put16(std::string& s, std::uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>(v >> 8));
}
void put32(std::string& s, std::uint32_t v) {
    put16(s, static_cast<std::uint16_t>(v & 0xFFFF));
    put16(s, static_cast<std::uint16_t>(v >> 16));
}

std::string column_name(std::size_t idx) {  // 0 -> A
    std::string s;
    ++idx;
    while (idx > 0) {
        std::size_t r = (idx - 1) % 26;
        s.insert(s.begin(), static_cast<char>('A' + r));
        idx = (idx - 1) / 26;
    }
    return s;
}

std::string truncate_utf8(const std::string& s, std::size_t max_chars) {
    std::u32string u = utf8::decode(s);
    if (u.size() <= max_chars) return s;
    return utf8::encode(u.substr(0, max_chars));
}

}  // namespace

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char32_t c : utf8::decode(s)) {
        bool allowed = c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0xD7FF) ||
                       (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF);
        if (!allowed) continue;
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: utf8::append(out, c);
        }
    }
    return out;
}

std::string csv_field(const std::string& s, char sep) {
    bool quote = s.find_first_of(std::string("\"\r\n") + sep) != std::string::npos;
    if (!quote) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string format_ids(const std::vector<std::size_t>& ids) {
    std::string out;
    for (std::size_t id : ids) {
        if (!out.empty()) out.push_back('+');
        out += std::to_string(id + 1);
    }
    return out;
}

void write_tmx(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts) {
    const std::string date = opts.creation_date.empty() ? utc_timestamp() : opts.creation_date;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<tmx version=\"1.4\">\n"
        << "  <header creationtool=\"corpus-sentence-aligner\" creationtoolversion=\"" << kVersion << "\"\n"
        << "          datatype=\"plaintext\" segtype=\"sentence\" adminlang=\"en-US\"\n"
        << "          srclang=\"" << xml_escape(opts.src_lang) << "\" o-tmf=\"csa\" creationdate=\"" << date
        << "\">\n"
        << "    <prop type=\"x-aligner\">Gale-Church length-based alignment</prop>\n"
        << "  </header>\n"
        << "  <body>\n";
    std::size_t tuid = 0;
    for (const auto& d : docs) {
        std::size_t bead = 0;
        for (const auto& p : d.result.pairs) {
            ++bead;
            if (!opts.keep_unaligned && (p.src_ids.empty() || p.tgt_ids.empty())) continue;
            ++tuid;
            out << "    <tu tuid=\"" << tuid << "\" creationdate=\"" << date << "\">\n";
            if (!d.name.empty()) out << "      <prop type=\"x-document\">" << xml_escape(d.name) << "</prop>\n";
            out << "      <prop type=\"x-bead\">" << bead << "</prop>\n"
                << "      <prop type=\"x-alignment\">" << p.type() << "</prop>\n"
                << "      <prop type=\"x-src-ids\">" << format_ids(p.src_ids) << "</prop>\n"
                << "      <prop type=\"x-tgt-ids\">" << format_ids(p.tgt_ids) << "</prop>\n"
                << "      <prop type=\"x-cost\">" << fmt(p.cost) << "</prop>\n";
            if (p.review) out << "      <prop type=\"x-review\">yes</prop>\n";
            out << "      <tuv xml:lang=\"" << xml_escape(opts.src_lang) << "\"><seg>" << xml_escape(p.src_text)
                << "</seg></tuv>\n"
                << "      <tuv xml:lang=\"" << xml_escape(opts.tgt_lang) << "\"><seg>" << xml_escape(p.tgt_text)
                << "</seg></tuv>\n"
                << "    </tu>\n";
        }
    }
    out << "  </body>\n</tmx>\n";
}

void write_csv(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts) {
    const char sep = opts.csv_separator;
    if (opts.csv_bom) out << "\xEF\xBB\xBF";
    for (std::size_t c = 0; c < 10; ++c) out << (c ? std::string(1, sep) : "") << kColumns[c];
    out << "\r\n";
    for (const auto& r : rows_of(docs)) {
        const AlignedPair& p = *r.pair;
        out << csv_field(r.document, sep) << sep << r.bead << sep << p.type() << sep
            << csv_field(format_ids(p.src_ids), sep) << sep << csv_field(format_ids(p.tgt_ids), sep) << sep
            << csv_field(p.src_text, sep) << sep << csv_field(p.tgt_text, sep) << sep << fmt(p.cost) << sep
            << fmt(p.delta) << sep << (p.review ? "yes" : "") << "\r\n";
    }
}

void write_tsv(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions&) {
    auto clean = [](std::string s) {
        for (auto& c : s) {
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        }
        return s;
    };
    for (std::size_t c = 0; c < 10; ++c) out << (c ? "\t" : "") << kColumns[c];
    out << "\n";
    for (const auto& r : rows_of(docs)) {
        const AlignedPair& p = *r.pair;
        out << clean(r.document) << '\t' << r.bead << '\t' << p.type() << '\t' << format_ids(p.src_ids) << '\t'
            << format_ids(p.tgt_ids) << '\t' << clean(p.src_text) << '\t' << clean(p.tgt_text) << '\t'
            << fmt(p.cost) << '\t' << fmt(p.delta) << '\t' << (p.review ? "yes" : "") << "\n";
    }
}

void write_stored_zip(std::ostream& out, const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string data, central;
    for (const auto& e : entries) {
        const std::string& name = e.first;
        const std::string& body = e.second;
        std::uint32_t crc = crc32(body);
        auto offset = static_cast<std::uint32_t>(data.size());
        auto size = static_cast<std::uint32_t>(body.size());
        // Local file header.
        put32(data, 0x04034b50u);
        put16(data, 20);      // version needed
        put16(data, 0);       // flags
        put16(data, 0);       // method: stored
        put16(data, 0);       // time
        put16(data, 0x0021);  // date: 1980-01-01
        put32(data, crc);
        put32(data, size);
        put32(data, size);
        put16(data, static_cast<std::uint16_t>(name.size()));
        put16(data, 0);
        data += name;
        data += body;
        // Central directory header.
        put32(central, 0x02014b50u);
        put16(central, 20);  // version made by
        put16(central, 20);  // version needed
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0x0021);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<std::uint16_t>(name.size()));
        put16(central, 0);  // extra
        put16(central, 0);  // comment
        put16(central, 0);  // disk
        put16(central, 0);  // internal attrs
        put32(central, 0);  // external attrs
        put32(central, offset);
        central += name;
    }
    std::string eocd;
    put32(eocd, 0x06054b50u);
    put16(eocd, 0);
    put16(eocd, 0);
    put16(eocd, static_cast<std::uint16_t>(entries.size()));
    put16(eocd, static_cast<std::uint16_t>(entries.size()));
    put32(eocd, static_cast<std::uint32_t>(central.size()));
    put32(eocd, static_cast<std::uint32_t>(data.size()));
    put16(eocd, 0);
    out << data << central << eocd;
}

void write_xlsx(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts) {
    const char* head = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    const std::string ns_main = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
    const std::string ns_rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    auto rows = rows_of(docs);
    const std::size_t last_row = rows.size() + 1;
    const std::string last_col = column_name(9);

    std::string content_types = std::string(head) +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>";
    std::string root_rels = std::string(head) +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"" + ns_rel + "/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    std::string workbook = std::string(head) + "<workbook xmlns=\"" + ns_main + "\" xmlns:r=\"" + ns_rel + "\">"
        "<sheets><sheet name=\"Alignment\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "<definedNames><definedName name=\"_xlnm._FilterDatabase\" localSheetId=\"0\" hidden=\"1\">"
        "Alignment!$A$1:$" + last_col + "$" + std::to_string(last_row) + "</definedName></definedNames>"
        "</workbook>";
    std::string wb_rels = std::string(head) +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"" + ns_rel + "/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"" + ns_rel + "/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";
    // Styles: 0 default, 1 wrapped text, 2 header, 3 wrapped + review fill,
    // 4 number (0.00), 5 number + review fill.
    std::string styles = std::string(head) + "<styleSheet xmlns=\"" + ns_main + "\">"
        "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"4\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFFFF2CC\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFD9D9D9\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"6\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment vertical=\"top\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"3\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyFill=\"1\" applyAlignment=\"1\">"
        "<alignment vertical=\"top\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyAlignment=\"1\">"
        "<alignment vertical=\"top\"/></xf>"
        "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyFill=\"1\" "
        "applyAlignment=\"1\"><alignment vertical=\"top\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>";

    std::ostringstream sh;
    sh << head << "<worksheet xmlns=\"" << ns_main << "\" xmlns:r=\"" << ns_rel << "\">"
       << "<dimension ref=\"A1:" << last_col << last_row << "\"/>"
       << "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" "
          "state=\"frozen\"/></sheetView></sheetViews>"
       << "<sheetFormatPr defaultRowHeight=\"15\"/>"
       << "<cols>";
    const double widths[10] = {22, 7, 7, 9, 9, 60, 60, 8, 8, 8};
    for (int c = 0; c < 10; ++c)
        sh << "<col min=\"" << c + 1 << "\" max=\"" << c + 1 << "\" width=\"" << widths[c] << "\" customWidth=\"1\"/>";
    sh << "</cols><sheetData>";

    auto str_cell = [&](std::size_t col, std::size_t row, const std::string& v, int style) {
        sh << "<c r=\"" << column_name(col) << row << "\" t=\"inlineStr\" s=\"" << style
           << "\"><is><t xml:space=\"preserve\">" << xml_escape(truncate_utf8(v, 32000)) << "</t></is></c>";
    };
    auto num_cell = [&](std::size_t col, std::size_t row, const std::string& v, int style) {
        sh << "<c r=\"" << column_name(col) << row << "\" s=\"" << style << "\"><v>" << v << "</v></c>";
    };
    sh << "<row r=\"1\">";
    for (std::size_t c = 0; c < 10; ++c) str_cell(c, 1, kColumns[c], 2);
    sh << "</row>";
    std::size_t r = 1;
    for (const auto& row : rows) {
        ++r;
        const AlignedPair& p = *row.pair;
        int ts = p.review ? 3 : 1;
        int ns = p.review ? 5 : 4;
        sh << "<row r=\"" << r << "\">";
        str_cell(0, r, row.document, ts);
        num_cell(1, r, std::to_string(row.bead), ts);
        str_cell(2, r, p.type(), ts);
        str_cell(3, r, format_ids(p.src_ids), ts);
        str_cell(4, r, format_ids(p.tgt_ids), ts);
        str_cell(5, r, p.src_text, ts);
        str_cell(6, r, p.tgt_text, ts);
        num_cell(7, r, fmt(p.cost), ns);
        num_cell(8, r, fmt(p.delta), ns);
        str_cell(9, r, p.review ? "yes" : "", ts);
        sh << "</row>";
    }
    sh << "</sheetData><autoFilter ref=\"A1:" << last_col << last_row << "\"/></worksheet>";
    (void)opts;

    write_stored_zip(out, {{"[Content_Types].xml", content_types},
                           {"_rels/.rels", root_rels},
                           {"xl/workbook.xml", workbook},
                           {"xl/_rels/workbook.xml.rels", wb_rels},
                           {"xl/styles.xml", styles},
                           {"xl/worksheets/sheet1.xml", sh.str()}});
}

}  // namespace csa

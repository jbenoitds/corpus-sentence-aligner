// SPDX-License-Identifier: MIT
#include "csa/segmenter.hpp"

#include <fstream>

#include "csa/utf8.hpp"

namespace csa {

namespace {

// Abbreviations are stored lower-case and without the final period.
const char* const kEnBlocking[] = {
    "mr", "mrs", "ms", "mx", "dr", "prof", "rev", "hon", "sr", "jr", "st", "sgt", "capt", "col", "lt", "gen",
    "gov", "pres", "messrs", "mt", "ft", "vs", "v", "e.g", "i.e", "cf", "viz", "al", "approx", "appt", "apt",
    "dept", "ca", "inc", "ltd", "co", "corp", "bros", "assn", "univ", "ave", "blvd", "rd", "jan", "feb", "mar",
    "apr", "jun", "jul", "aug", "sep", "sept", "oct", "nov", "dec", "mon", "tue", "tues", "thu", "thurs", "fri",
    "ph.d", "m.d", "b.sc", "m.sc", "b.a", "m.a", "r.n", "u.s", "u.k", "u.s.a", "e.u", "n.b", "p.s", "eds",
    "et al", "resp", "incl", "esp", "dist"};
const char* const kEnNonBlocking[] = {"etc", "a.m", "p.m", "hrs", "hr", "min", "mins", "sec", "secs", "yrs",
                                      "yr", "wk", "wks", "lbs", "lb", "oz", "kg", "km", "cm", "ml", "ed"};
const char* const kEnNumeric[] = {"no", "nos", "p", "pp", "vol", "vols", "fig", "figs", "ch", "chap",
                                  "art", "eq", "eqn", "ref", "refs", "tab", "pg", "para", "q", "item"};

const char* const kFrBlocking[] = {
    "m", "mm", "mme", "mmes", "mlle", "mlles", "dr", "dre", "drs", "dres", "pr", "pre", "me", "mgr", "st",
    "ste", "sts", "stes", "c.-à-d", "c-à-d", "c.à.d", "c.-à.-d", "p.ex", "ex", "cf", "env", "tél", "tel",
    "téléc", "av", "boul", "bd", "app", "ibid", "op", "cit", "al", "janv", "févr", "fév", "avr", "juil",
    "sept", "oct", "nov", "déc", "lun", "mar", "ven", "sam", "dim", "qc", "prov", "dép", "éd", "coll", "vs",
    "ph.d", "m.d", "b.sc", "m.sc", "inc", "ltée", "cie", "resp", "suiv", "admin"};
const char* const kFrNonBlocking[] = {"etc", "h", "min", "sec", "hres", "kg", "km", "cm", "ml"};
const char* const kFrNumeric[] = {"no", "n", "nº", "n°", "p", "pp", "vol", "fig", "art", "chap", "ch",
                                  "réf", "tabl", "éq", "q", "item"};

bool is_terminator(char32_t c) {
    switch (c) {
        case '.': case '!': case '?': case 0x2026: case 0x203C: case 0x2047: case 0x2048: case 0x2049:
        case 0xFF01: case 0xFF1F: case 0x3002:
            return true;
        default:
            return false;
    }
}

bool is_closer(char32_t c) {
    switch (c) {
        case '"': case '\'': case ')': case ']': case '}': case 0xBB: case 0x201D: case 0x2019: case 0x203A:
            return true;
        default:
            return false;
    }
}

bool is_opener(char32_t c) {
    switch (c) {
        case '"': case '\'': case '(': case '[': case '{': case 0xAB: case 0x201C: case 0x2018: case 0x2039:
        case 0x201E: case 0xBF: case 0xA1: case 0x2022: case 0x25CF: case 0x25AA: case 0x25E6: case '-':
        case 0x2013: case 0x2014: case '*': case 0xB7: case 0x2192: case '#': case '$': case 0x20AC:
        case 0xA9: case 0x2713: case 0x2610: case 0x2611: case 0x25A1: case 0xF0B7:
            return true;
        default:
            return false;
    }
}

bool is_roman(const std::string& t) {
    static const char* const romans[] = {"i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x",
                                         "xi", "xii", "xiii", "xiv", "xv"};
    for (const char* r : romans) {
        if (t == r) return true;
    }
    return false;
}

bool all_digits(const std::u32string& t) {
    if (t.empty()) return false;
    for (char32_t c : t) {
        if (!utf8::is_digit(c)) return false;
    }
    return true;
}

// "U.S", "Ph.D", "J.-C", "e.g": short letter groups separated by periods.
bool is_dotted_abbreviation(const std::u32string& t) {
    if (t.find(U'.') == std::u32string::npos) return false;
    std::size_t piece = 0;
    bool has_letter = false;
    for (char32_t c : t) {
        if (c == '.') {
            if (piece > 3) return false;
            piece = 0;
        } else if (c == '-') {
            continue;
        } else if (utf8::is_alpha(c)) {
            ++piece;
            has_letter = true;
        } else {
            return false;
        }
    }
    return has_letter && piece <= 3;
}

std::u32string trim_u32(const std::u32string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && utf8::is_space(s[b])) ++b;
    while (e > b && utf8::is_space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool only_punctuation(const std::u32string& s) {
    for (char32_t c : s) {
        if (utf8::is_alpha(c) || utf8::is_digit(c)) return false;
    }
    return true;
}

}  // namespace

SentenceSplitter::SentenceSplitter(const std::string& lang) : lang_(lang) {
    std::string base = utf8::to_lower(lang.substr(0, 2));
    auto add_all = [](std::set<std::string>& dst, const char* const* b, const char* const* e) {
        for (auto p = b; p != e; ++p) dst.insert(*p);
    };
    if (base == "en") {
        add_all(blocking_, std::begin(kEnBlocking), std::end(kEnBlocking));
        add_all(nonblocking_, std::begin(kEnNonBlocking), std::end(kEnNonBlocking));
        for (const char* a : kEnNumeric) blocking_.insert(std::string("#") + a);
    } else if (base == "fr") {
        add_all(blocking_, std::begin(kFrBlocking), std::end(kFrBlocking));
        add_all(nonblocking_, std::begin(kFrNonBlocking), std::end(kFrNonBlocking));
        for (const char* a : kFrNumeric) blocking_.insert(std::string("#") + a);
    }
}

void SentenceSplitter::add_abbreviation(const std::string& abbrev, bool blocking) {
    std::string a = utf8::to_lower(utf8::normalize_segment(abbrev));
    while (!a.empty() && a.back() == '.') a.pop_back();
    if (a.empty()) return;
    if (blocking) blocking_.insert(a);
    else nonblocking_.insert(a);
}

void SentenceSplitter::load_abbreviations(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open abbreviation file: " + path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = utf8::normalize_segment(line);
        if (t.empty() || t[0] == '#') continue;
        bool nonblocking = t.back() == '*';
        if (nonblocking) t.pop_back();
        add_abbreviation(t, !nonblocking);
    }
}

std::vector<std::string> SentenceSplitter::split(const std::string& paragraph) const {
    const std::u32string u = utf8::decode(paragraph);
    const std::size_t n = u.size();
    std::vector<std::u32string> pieces;
    std::size_t start = 0;
    while (start < n && utf8::is_space(u[start])) ++start;

    for (std::size_t i = start; i < n; ++i) {
        if (!is_terminator(u[i])) continue;
        std::size_t j = i;
        while (j + 1 < n && is_terminator(u[j + 1])) ++j;
        std::size_t k = j + 1;

        // Closing quotes/brackets, including French « ... » with a space before ».
        for (;;) {
            while (k < n && is_closer(u[k])) ++k;
            std::size_t k2 = k;
            while (k2 < n && utf8::is_space(u[k2])) ++k2;
            if (k2 < n && k2 > k && (u[k2] == 0xBB || u[k2] == 0x203A)) {
                k = k2 + 1;
                continue;
            }
            break;
        }
        // Footnote/citation number glued to the end of a sentence: "insomnia.2 Next".
        if (k == j + 1 && k < n && utf8::is_digit(u[k]) && i > 0 && utf8::is_alpha(u[i - 1])) {
            std::size_t d = k;
            while (d < n && d - k < 3 && utf8::is_digit(u[d])) ++d;
            if (d >= n || utf8::is_space(u[d])) k = d;
        }
        if (k >= n) break;
        if (!utf8::is_space(u[k])) {
            i = j;
            continue;
        }
        std::size_t m = k;
        while (m < n && utf8::is_space(u[m])) ++m;
        if (m >= n) break;
        const char32_t next = u[m];

        bool split_here;
        if (utf8::is_lower(next)) {
            split_here = false;
        } else if (utf8::is_upper(next) || utf8::is_digit(next) || is_opener(next)) {
            split_here = true;
        } else {
            split_here = !(next == ',' || next == ';' || next == ':' || next == ')' || next == ']' || next == '.');
        }

        if (split_here && u[i] == '.' && j == i) {
            // Token immediately before the period.
            std::size_t ts = i;
            while (ts > start && !utf8::is_space(u[ts - 1])) --ts;
            std::u32string tok = u.substr(ts, i - ts);
            while (!tok.empty() && (is_opener(tok[0]) || tok[0] == '(')) tok.erase(0, 1);
            std::u32string lowered = tok;
            for (auto& c : lowered) c = utf8::to_lower(c);
            std::string key = utf8::encode(lowered);
            // Two-word abbreviations such as "et al."
            std::string key2;
            if (ts > start + 1) {
                std::size_t ps = ts - 1;
                while (ps > start && utf8::is_space(u[ps - 1])) --ps;
                std::size_t pt = ps;
                while (pt > start && !utf8::is_space(u[pt - 1])) --pt;
                std::u32string prev = u.substr(pt, ps - pt);
                for (auto& c : prev) c = utf8::to_lower(c);
                key2 = utf8::encode(prev) + " " + key;
            }
            bool first_token = (ts == start);
            if (key.empty()) {
                // ". Next" – keep the split.
            } else if (blocking_.count(key) || (!key2.empty() && blocking_.count(key2))) {
                split_here = false;
            } else if (blocking_.count("#" + key)) {
                split_here = !utf8::is_digit(next);
            } else if (nonblocking_.count(key)) {
                split_here = utf8::is_upper(next) || is_opener(next);
            } else if (tok.size() == 1 && utf8::is_alpha(tok[0])) {
                split_here = false;  // initials: "J. Smith", list items "a. ..."
            } else if (is_dotted_abbreviation(tok)) {
                split_here = false;
            } else if (first_token && ((all_digits(tok) && tok.size() <= 3) || is_roman(key))) {
                split_here = false;  // list numbering: "1. Introduction", "IV. Results"
            }
        }

        if (!split_here) {
            i = j;
            continue;
        }
        pieces.push_back(u.substr(start, k - start));
        start = m;
        i = m - 1;
    }
    if (start < n) pieces.push_back(u.substr(start));

    std::vector<std::string> out;
    for (auto& p : pieces) {
        std::u32string t = trim_u32(p);
        if (t.empty()) continue;
        if (!out.empty() && only_punctuation(t)) {
            out.back() += " " + utf8::encode(t);
            continue;
        }
        out.push_back(utf8::normalize_segment(utf8::encode(t)));
    }
    return out;
}

}  // namespace csa

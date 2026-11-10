// SPDX-License-Identifier: MIT
#include "csa/evaluate.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace csa {

namespace {

std::vector<std::size_t> parse_ids(const std::string& field) {
    std::vector<std::size_t> ids;
    std::string cur;
    auto flush = [&]() {
        if (cur.empty()) return;
        if (cur.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("gold: bad sentence id '" + cur + "'");
        unsigned long v = std::stoul(cur);
        if (v == 0) throw std::runtime_error("gold: sentence ids are 1-based");
        ids.push_back(static_cast<std::size_t>(v - 1));
        cur.clear();
    };
    for (char c : field) {
        if (c == '+' || c == ',' || c == ';' || c == ' ' || c == '\t' || c == '-' || c == '"' || c == '\'') flush();
        else cur.push_back(c);
    }
    flush();
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string strip_bom(std::string s) {
    if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
    return s;
}

}  // namespace

std::vector<std::vector<std::string>> parse_csv(const std::string& text, char sep) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false, any = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }
        if (c == '"' && field.empty()) {
            in_quotes = true;
            any = true;
        } else if (c == sep) {
            row.push_back(field);
            field.clear();
            any = true;
        } else if (c == '\r') {
            continue;
        } else if (c == '\n') {
            row.push_back(field);
            rows.push_back(row);
            row.clear();
            field.clear();
            any = false;
        } else {
            field.push_back(c);
            any = true;
        }
    }
    if (any || !field.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

std::vector<GoldBead> read_gold(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open gold file: " + path);
    std::string text = strip_bom(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));

    // Detect the separator from the first line.
    std::string first = text.substr(0, text.find('\n'));
    char sep = '\t';
    if (first.find('\t') == std::string::npos) {
        if (first.find(';') != std::string::npos && first.find("src_ids") != std::string::npos) sep = ';';
        else if (first.find(',') != std::string::npos && first.find("src_ids") != std::string::npos) sep = ',';
    }
    auto rows = parse_csv(text, sep);
    std::size_t src_col = 0, tgt_col = 1;
    std::size_t start = 0;
    if (!rows.empty()) {
        const auto& h = rows[0];
        auto s = std::find(h.begin(), h.end(), "src_ids");
        auto t = std::find(h.begin(), h.end(), "tgt_ids");
        if (s != h.end() && t != h.end()) {
            src_col = static_cast<std::size_t>(s - h.begin());
            tgt_col = static_cast<std::size_t>(t - h.begin());
            start = 1;
        }
    }
    std::vector<GoldBead> gold;
    for (std::size_t r = start; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.empty() || (row.size() == 1 && row[0].empty())) continue;
        if (!row[0].empty() && row[0][0] == '#') continue;
        std::string sf = src_col < row.size() ? row[src_col] : "";
        std::string tf = tgt_col < row.size() ? row[tgt_col] : "";
        GoldBead b{parse_ids(sf), parse_ids(tf)};
        if (b.first.empty() && b.second.empty()) continue;
        gold.push_back(std::move(b));
    }
    return gold;
}

EvalScores evaluate(const std::vector<AlignedPair>& test, const std::vector<GoldBead>& gold) {
    using Link = std::pair<std::size_t, std::size_t>;
    std::set<Link> gl, tl;
    std::set<GoldBead> gb, tb;
    for (const auto& b : gold) {
        gb.insert(b);
        for (auto s : b.first)
            for (auto t : b.second) gl.insert({s, t});
    }
    for (const auto& p : test) {
        GoldBead b{p.src_ids, p.tgt_ids};
        std::sort(b.first.begin(), b.first.end());
        std::sort(b.second.begin(), b.second.end());
        tb.insert(b);
        for (auto s : b.first)
            for (auto t : b.second) tl.insert({s, t});
    }
    EvalScores sc;
    sc.gold_links = gl.size();
    sc.test_links = tl.size();
    for (const auto& l : tl) sc.correct_links += gl.count(l);
    sc.gold_beads = gb.size();
    sc.test_beads = tb.size();
    for (const auto& b : tb) sc.correct_beads += gb.count(b);
    return sc;
}

}  // namespace csa

// SPDX-License-Identifier: MIT
#include <sstream>

#include "csa/evaluate.hpp"
#include "csa/export.hpp"
#include "csa/zip.hpp"
#include "minitest.hpp"

using namespace csa;

namespace {

std::vector<DocumentAlignment> tiny_docs() {
    AlignerOptions o;
    DocumentAlignment d;
    d.name = "doc \"1\".txt";
    d.result = align_documents({"Hello & welcome. Second <sentence>, here."}, {u8"Bonjour et bienvenue. Deuxième « phrase », ici."}, o);
    // Add an unaligned unit by hand.
    AlignedPair extra;
    extra.tgt_ids = {2};
    extra.tgt_text = u8"Ajout du traducteur.";
    extra.review = true;
    d.result.pairs.push_back(extra);
    return {d};
}

std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) {
        ++n;
        p += needle.size();
    }
    return n;
}

}  // namespace

TEST(export_xml_escape) {
    CHECK_EQ(xml_escape("a<b>&\"c\""), std::string("a&lt;b&gt;&amp;&quot;c&quot;"));
    CHECK_EQ(xml_escape(std::string("x\x01y\x0Bz", 5)), std::string("xyz"));
    CHECK_EQ(xml_escape(u8"été"), std::string(u8"été"));
}

TEST(export_csv_field_quoting) {
    CHECK_EQ(csv_field("plain", ','), std::string("plain"));
    CHECK_EQ(csv_field("a,b", ','), std::string("\"a,b\""));
    CHECK_EQ(csv_field("say \"hi\"", ','), std::string("\"say \"\"hi\"\"\""));
    CHECK_EQ(csv_field("a,b", ';'), std::string("a,b"));
    CHECK_EQ(csv_field("a;b", ';'), std::string("\"a;b\""));
}

TEST(export_format_ids) {
    CHECK_EQ(format_ids({}), std::string(""));
    CHECK_EQ(format_ids({0}), std::string("1"));
    CHECK_EQ(format_ids({3, 4}), std::string("4+5"));
}

TEST(export_tmx_structure) {
    auto docs = tiny_docs();
    ExportOptions eo;
    eo.creation_date = "20220101T000000Z";
    std::ostringstream os;
    write_tmx(os, docs, eo);
    std::string tmx = os.str();
    CHECK(tmx.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 0) == 0);
    CHECK(tmx.find("<tmx version=\"1.4\">") != std::string::npos);
    CHECK(tmx.find("srclang=\"en\"") != std::string::npos);
    CHECK_EQ(count(tmx, "<tu "), static_cast<std::size_t>(2));  // unaligned unit skipped
    CHECK_EQ(count(tmx, "<tuv xml:lang=\"fr\">"), static_cast<std::size_t>(2));
    CHECK(tmx.find("Hello &amp; welcome.") != std::string::npos);
    CHECK(tmx.find("&lt;sentence&gt;") != std::string::npos);
    CHECK(tmx.find("doc &quot;1&quot;.txt") != std::string::npos);
    CHECK_EQ(count(tmx, "<tu "), count(tmx, "</tu>"));

    eo.keep_unaligned = true;
    std::ostringstream os2;
    write_tmx(os2, docs, eo);
    CHECK_EQ(count(os2.str(), "<tu "), static_cast<std::size_t>(3));
    CHECK(os2.str().find("<prop type=\"x-review\">yes</prop>") != std::string::npos);
}

TEST(export_csv_roundtrip_as_gold) {
    auto docs = tiny_docs();
    ExportOptions eo;
    eo.csv_separator = ';';
    std::ostringstream os;
    write_csv(os, docs, eo);
    std::string csv = os.str();
    CHECK(csv.rfind("\xEF\xBB\xBF" "document;bead;type;src_ids;tgt_ids", 0) == 0);
    auto rows = parse_csv(csv.substr(3), ';');
    CHECK_EQ(rows.size(), static_cast<std::size_t>(4));
    CHECK_EQ(rows[1][0], std::string("doc \"1\".txt"));
    CHECK_EQ(rows[3][2], std::string("0-1"));
    CHECK_EQ(rows[3][9], std::string("yes"));
}

TEST(export_tsv_columns) {
    std::ostringstream os;
    write_tsv(os, tiny_docs(), ExportOptions{});
    std::istringstream is(os.str());
    std::string line;
    while (std::getline(is, line)) CHECK_EQ(count(line, "\t"), static_cast<std::size_t>(9));
}

TEST(export_xlsx_is_valid_zip_with_sheet) {
    std::ostringstream os;
    write_xlsx(os, tiny_docs(), ExportOptions{});
    ZipArchive z(os.str());
    CHECK(z.contains("[Content_Types].xml"));
    CHECK(z.contains("xl/workbook.xml"));
    CHECK(z.contains("xl/styles.xml"));
    std::string sheet = z.read("xl/worksheets/sheet1.xml");
    CHECK(sheet.find("Hello &amp; welcome.") != std::string::npos);
    CHECK(sheet.find(u8"Ajout du traducteur.") != std::string::npos);
    CHECK(sheet.find("<autoFilter ref=\"A1:J4\"/>") != std::string::npos);
    CHECK_EQ(count(sheet, "<row "), static_cast<std::size_t>(4));
}

TEST(parse_csv_quotes_and_newlines) {
    auto rows = parse_csv("a,\"b,c\",\"d\"\"e\"\r\n\"multi\nline\",x\n", ',');
    CHECK_EQ(rows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(rows[0][1], std::string("b,c"));
    CHECK_EQ(rows[0][2], std::string("d\"e"));
    CHECK_EQ(rows[1][0], std::string("multi\nline"));
}

TEST(evaluate_scores) {
    std::vector<GoldBead> gold = {{{0}, {0}}, {{1, 2}, {1}}, {{3}, {}}};
    std::vector<AlignedPair> test(3);
    test[0].src_ids = {0};
    test[0].tgt_ids = {0};
    test[1].src_ids = {1};
    test[1].tgt_ids = {1};
    test[2].src_ids = {2, 3};
    test[2].tgt_ids = {};
    auto sc = evaluate(test, gold);
    CHECK_EQ(sc.gold_links, static_cast<std::size_t>(3));
    CHECK_EQ(sc.test_links, static_cast<std::size_t>(2));
    CHECK_EQ(sc.correct_links, static_cast<std::size_t>(2));
    CHECK_NEAR(sc.link_precision(), 1.0, 1e-12);
    CHECK_NEAR(sc.link_recall(), 2.0 / 3.0, 1e-12);
    CHECK_EQ(sc.correct_beads, static_cast<std::size_t>(1));
}

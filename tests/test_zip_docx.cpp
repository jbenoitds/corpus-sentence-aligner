// SPDX-License-Identifier: MIT
#include <cstdint>
#include <sstream>

#include "csa/document.hpp"
#include "csa/export.hpp"
#include "csa/zip.hpp"
#include "deflate_vectors.hpp"
#include "minitest.hpp"

using namespace csa;
using V = std::vector<std::string>;

TEST(inflate_matches_zlib_vectors) {
    for (const auto& v : deflate_vectors()) {
        std::string out = inflate_raw(reinterpret_cast<const std::uint8_t*>(v.packed.data()), v.packed.size());
        if (out != v.plain) minitest::report(__FILE__, __LINE__, std::string("inflate mismatch for ") + v.name);
    }
}

TEST(inflate_rejects_garbage) {
    const std::string bad("\xff\xff\xff\xff", 4);  // block type 3 is invalid
    CHECK_THROWS(inflate_raw(reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size()));
    const std::string truncated = deflate_vectors()[2].packed.substr(0, 40);
    CHECK_THROWS(inflate_raw(reinterpret_cast<const std::uint8_t*>(truncated.data()), truncated.size()));
}

TEST(crc32_known_value) {
    CHECK_EQ(crc32("123456789"), static_cast<std::uint32_t>(0xCBF43926u));
    CHECK_EQ(crc32(""), static_cast<std::uint32_t>(0));
}

TEST(stored_zip_roundtrip) {
    std::ostringstream os;
    write_stored_zip(os, {{"a.txt", "alpha"}, {"dir/b.xml", u8"<b>été</b>"}});
    ZipArchive z(os.str());
    CHECK_EQ(z.entries().size(), static_cast<std::size_t>(2));
    CHECK(z.contains("dir/b.xml"));
    CHECK_EQ(z.read("a.txt"), std::string("alpha"));
    CHECK_EQ(z.read("dir/b.xml"), std::string(u8"<b>été</b>"));
    CHECK_THROWS(z.read("missing"));
}

TEST(zip_rejects_non_archives) {
    CHECK_THROWS(ZipArchive(std::string("not a zip file at all, just some text here")));
}

TEST(docx_fixture_paragraphs) {
    auto paras = load_paragraphs(std::string(CSA_TEST_DATA) + "/fixture.docx");
    V expected = {"Sleep & Wellbeing",
                  "About 39% report insomnia. Another study agrees.",
                  "Please carefully answer.",
                  "Name: ____",
                  "Nicole and Emilie",
                  "Team ARDS",
                  "Page 3",
                  "Did you know?",
                  "(1)",
                  "Never",
                  u8"Café <ok> été"};
    CHECK_EQ(paras.size(), expected.size());
    for (std::size_t i = 0; i < std::min(paras.size(), expected.size()); ++i) CHECK_EQ(paras[i], expected[i]);
}

TEST(docx_options) {
    LoadOptions o;
    o.keep_superscript_numbers = true;
    o.line_break_is_paragraph = false;
    auto paras = load_paragraphs(std::string(CSA_TEST_DATA) + "/fixture.docx", o);
    CHECK_EQ(paras[1], std::string("About 39% report insomnia2. Another study agrees."));
    CHECK_EQ(paras[4], std::string("Nicole and Emilie Team ARDS"));
}

TEST(xml_unescape_entities) {
    CHECK_EQ(xml_unescape("a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos; &#233;&#xE9; &bogus;"),
             std::string(u8"a & b <c> \"d\" 'e' éé &bogus;"));
}

TEST(text_paragraph_modes) {
    std::string bytes = "\xEF\xBB\xBF" "First line.\r\nSecond line.\r\n\r\nThird.\n";
    CHECK_EQ(text_paragraphs_from_bytes(bytes), (V{"First line.", "Second line.", "Third."}));
    LoadOptions o;
    o.blank_line_paragraphs = true;
    CHECK_EQ(text_paragraphs_from_bytes(bytes, o), (V{"First line. Second line.", "Third."}));
    std::string utf16("\xFF\xFE" "A\0\n\0B\0", 8);
    CHECK_EQ(text_paragraphs_from_bytes(utf16), (V{"A", "B"}));
}

TEST(unsupported_format_message) {
    CHECK_THROWS(load_paragraphs("whatever.doc"));
}

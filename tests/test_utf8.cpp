// SPDX-License-Identifier: MIT
#include "csa/utf8.hpp"
#include "minitest.hpp"

using namespace csa;

TEST(utf8_roundtrip) {
    std::string s = u8"Évitez les écrans « ce soir » — Œuvre ½ 😀";
    CHECK_EQ(utf8::encode(utf8::decode(s)), s);
}

TEST(utf8_length_counts_code_points) {
    CHECK_EQ(utf8::length(u8"été"), static_cast<std::size_t>(3));
    CHECK_EQ(utf8::length(u8"Œuf 😀"), static_cast<std::size_t>(5));
    CHECK_EQ(utf8::length(""), static_cast<std::size_t>(0));
}

TEST(utf8_invalid_bytes_become_replacement) {
    std::string bad = "a\xC3(b\xFF";
    std::u32string u = utf8::decode(bad);
    CHECK_EQ(u.size(), static_cast<std::size_t>(5));
    CHECK(u[1] == 0xFFFD);
    CHECK(u[4] == 0xFFFD);
    // Overlong encoding of '/' must be rejected.
    CHECK(utf8::decode("\xC0\xAF")[0] == 0xFFFD);
}

TEST(utf8_case_classes) {
    CHECK(utf8::is_upper(U'É'));
    CHECK(utf8::is_upper(U'Œ'));
    CHECK(utf8::is_upper(U'Ç'));
    CHECK(utf8::is_lower(U'é'));
    CHECK(utf8::is_lower(U'œ'));
    CHECK(!utf8::is_upper(U'×'));
    CHECK(utf8::to_lower(U'À') == U'à');
    CHECK_EQ(utf8::to_lower(u8"ÉTÉ Mme"), std::string(u8"été mme"));
}

TEST(utf8_normalize_segment) {
    CHECK_EQ(utf8::normalize_segment("  a \t b\r\n c  "), std::string("a b c"));
    // NBSP inside is kept (French typography), trimmed at the ends.
    CHECK_EQ(utf8::normalize_segment(u8" Quoi ? "), std::string(u8"Quoi ?"));
    // Soft hyphen and zero-width space removed.
    CHECK_EQ(utf8::normalize_segment(u8"in­for​mation"), std::string("information"));
}

TEST(utf16_conversion) {
    std::string le("\x41\x00\xE9\x00\x3D\xD8\x00\xDE", 8);  // "Aé😀"
    CHECK_EQ(utf8::from_utf16(le, false), std::string(u8"Aé😀"));
    std::string be("\x00\x41\x00\xE9", 4);
    CHECK_EQ(utf8::from_utf16(be, true), std::string(u8"Aé"));
}

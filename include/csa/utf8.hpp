// SPDX-License-Identifier: MIT
// Minimal UTF-8 utilities: decoding, encoding, and the handful of Unicode
// character classes needed for English/French sentence segmentation.
#pragma once

#include <cstddef>
#include <string>

namespace csa {
namespace utf8 {

/// Decode UTF-8 into code points. Invalid sequences become U+FFFD.
std::u32string decode(const std::string& s);

/// Encode code points as UTF-8.
std::string encode(const std::u32string& s);

/// Append one code point to a UTF-8 string.
void append(std::string& out, char32_t cp);

/// Number of code points (characters) in a UTF-8 string.
std::size_t length(const std::string& s);

/// Convert UTF-16 (little- or big-endian) bytes to UTF-8.
std::string from_utf16(const std::string& bytes, bool big_endian);

/// Normalise raw text into a clean segment:
///  - removes BOM, zero-width space and soft hyphen,
///  - turns control characters, tabs and newlines into spaces,
///  - collapses runs of ASCII whitespace into one space,
///  - trims leading/trailing whitespace (including no-break spaces).
/// No-break spaces inside the text are preserved (French typography).
std::string normalize_segment(const std::string& s);

bool is_space(char32_t c);   ///< ASCII whitespace and Unicode space separators
bool is_upper(char32_t c);   ///< Latin uppercase letters (Basic Latin, Latin-1, Extended-A, Greek)
bool is_lower(char32_t c);   ///< Latin lowercase letters
bool is_alpha(char32_t c);
bool is_digit(char32_t c);   ///< ASCII digits only

/// Lower-case a code point (Latin ranges only; others are returned unchanged).
char32_t to_lower(char32_t c);

/// Lower-case a UTF-8 string (Latin ranges only).
std::string to_lower(const std::string& s);

}  // namespace utf8
}  // namespace csa

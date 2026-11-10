// SPDX-License-Identifier: MIT
#include "csa/utf8.hpp"

namespace csa {
namespace utf8 {

std::u32string decode(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        unsigned char b = p[i];
        char32_t cp;
        int extra;
        if (b < 0x80) {
            out.push_back(b);
            ++i;
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= n || (p[i + k] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        // Reject overlong encodings, surrogates and out-of-range values.
        if (ok) {
            if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
                (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF)) {
                ok = false;
            }
        }
        if (ok) {
            out.push_back(cp);
            i += static_cast<std::size_t>(extra) + 1;
        } else {
            out.push_back(0xFFFD);
            ++i;
        }
    }
    return out;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        append(out, 0xFFFD);
    }
}

std::string encode(const std::u32string& s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t c : s) append(out, c);
    return out;
}

std::size_t length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

std::string from_utf16(const std::string& bytes, bool big_endian) {
    std::string out;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t n = bytes.size() & ~static_cast<std::size_t>(1);
    std::size_t i = 0;
    auto unit = [&](std::size_t k) -> char32_t {
        return big_endian ? static_cast<char32_t>((p[k] << 8) | p[k + 1])
                          : static_cast<char32_t>((p[k + 1] << 8) | p[k]);
    };
    while (i < n) {
        char32_t u = unit(i);
        i += 2;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i < n) {
                char32_t lo = unit(i);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    i += 2;
                    append(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                    continue;
                }
            }
            append(out, 0xFFFD);
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            append(out, 0xFFFD);
        } else {
            append(out, u);
        }
    }
    return out;
}

bool is_space(char32_t c) {
    switch (c) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
        case 0x85: case 0xA0: case 0x1680: case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

bool is_upper(char32_t c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return true;
    if (c >= 0x100 && c <= 0x137) return (c % 2) == 0;
    if (c >= 0x139 && c <= 0x148) return (c % 2) == 1;
    if (c >= 0x14A && c <= 0x177) return (c % 2) == 0;
    if (c == 0x178 || c == 0x179 || c == 0x17B || c == 0x17D) return true;
    if (c >= 0x391 && c <= 0x3A9) return true;  // Greek capitals
    if (c >= 0x410 && c <= 0x42F) return true;  // Cyrillic capitals
    return false;
}

bool is_lower(char32_t c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 0xDF && c <= 0xFF && c != 0xF7) return true;
    if (c >= 0x100 && c <= 0x137) return (c % 2) == 1;
    if (c >= 0x139 && c <= 0x148) return (c % 2) == 0;
    if (c >= 0x14A && c <= 0x177) return (c % 2) == 1;
    if (c == 0x17A || c == 0x17C || c == 0x17E || c == 0x17F) return true;
    if (c >= 0x3B1 && c <= 0x3C9) return true;
    if (c >= 0x430 && c <= 0x44F) return true;
    return false;
}

bool is_alpha(char32_t c) { return is_upper(c) || is_lower(c) || c == 0xAA || c == 0xBA; }

bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }

char32_t to_lower(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return (c % 2 == 0) ? c + 1 : c;
    if (c >= 0x139 && c <= 0x148) return (c % 2 == 1) ? c + 1 : c;
    if (c == 0x178) return 0xFF;
    if (c == 0x179 || c == 0x17B || c == 0x17D) return c + 1;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    return c;
}

std::string to_lower(const std::string& s) {
    std::u32string u = decode(s);
    for (auto& c : u) c = to_lower(c);
    return encode(u);
}

std::string normalize_segment(const std::string& s) {
    std::u32string in = decode(s);
    std::u32string out;
    out.reserve(in.size());
    bool pending_space = false;
    for (char32_t c : in) {
        if (c == 0xFEFF || c == 0x200B || c == 0x00AD || c == 0x200C || c == 0x200D) continue;
        bool ascii_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
                         c == 0x85 || c == 0x2028 || c == 0x2029);
        bool control = (c < 0x20) || (c >= 0x7F && c < 0xA0);
        if (ascii_ws || control) {
            pending_space = true;
            continue;
        }
        if (pending_space && !out.empty()) out.push_back(' ');
        pending_space = false;
        out.push_back(c);
    }
    // Trim leading and trailing Unicode spaces (including NBSP).
    std::size_t b = 0, e = out.size();
    while (b < e && is_space(out[b])) ++b;
    while (e > b && is_space(out[e - 1])) --e;
    return encode(out.substr(b, e - b));
}

}  // namespace utf8
}  // namespace csa

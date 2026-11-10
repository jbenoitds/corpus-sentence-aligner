// SPDX-License-Identifier: MIT
#include "csa/zip.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <sstream>

namespace csa {

namespace {

constexpr int kMaxBits = 15;

class BitReader {
public:
    BitReader(const std::uint8_t* d, std::size_t n) : data_(d), size_(n) {}

    unsigned bits(int need) {
        std::uint32_t val = bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ >= size_) throw ZipError("deflate: unexpected end of compressed data");
            val |= static_cast<std::uint32_t>(data_[pos_++]) << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = val >> need;
        bitcnt_ -= need;
        return val & ((1u << need) - 1u);
    }

    void align_to_byte() {
        bitbuf_ = 0;
        bitcnt_ = 0;
    }

    std::uint8_t byte() {
        if (pos_ >= size_) throw ZipError("deflate: unexpected end of stored block");
        return data_[pos_++];
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint32_t bitbuf_ = 0;
    int bitcnt_ = 0;
};

// Canonical Huffman decoding table (count of codes per length + symbols
// ordered by code), decoded one bit at a time. Simple and fast enough for
// documents of a few megabytes.
struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::vector<std::uint16_t> symbol;

    // Returns >0 if the code set is incomplete, 0 if complete; throws if over-subscribed.
    int build(const std::uint16_t* lengths, int n) {
        count.fill(0);
        symbol.assign(static_cast<std::size_t>(n), 0);
        for (int s = 0; s < n; ++s) count[lengths[s]]++;
        if (count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) throw ZipError("deflate: over-subscribed Huffman code");
        }
        std::array<std::uint16_t, kMaxBits + 1> offs{};
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = offs[len] + count[len];
        for (int s = 0; s < n; ++s) {
            if (lengths[s] != 0) symbol[offs[lengths[s]]++] = static_cast<std::uint16_t>(s);
        }
        return left;
    }

    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= static_cast<int>(br.bits(1));
            int cnt = count[len];
            if (code - cnt < first) return symbol[static_cast<std::size_t>(index + (code - first))];
            index += cnt;
            first += cnt;
            first <<= 1;
            code <<= 1;
        }
        throw ZipError("deflate: invalid Huffman code");
    }
};

const std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint16_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                     33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint16_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void inflate_codes(BitReader& br, std::string& out, const Huffman& lencode, const Huffman& distcode) {
    for (;;) {
        int sym = lencode.decode(br);
        if (sym < 256) {
            out.push_back(static_cast<char>(sym));
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) throw ZipError("deflate: invalid length symbol");
            std::size_t len = kLenBase[sym] + br.bits(kLenExtra[sym]);
            int dsym = distcode.decode(br);
            if (dsym >= 30) throw ZipError("deflate: invalid distance symbol");
            std::size_t dist = kDistBase[dsym] + br.bits(kDistExtra[dsym]);
            if (dist > out.size()) throw ZipError("deflate: distance too far back");
            std::size_t from = out.size() - dist;
            for (std::size_t k = 0; k < len; ++k) out.push_back(out[from + k]);  // may overlap
        }
    }
}

void inflate_stored(BitReader& br, std::string& out) {
    br.align_to_byte();
    unsigned len = br.byte();
    len |= static_cast<unsigned>(br.byte()) << 8;
    unsigned nlen = br.byte();
    nlen |= static_cast<unsigned>(br.byte()) << 8;
    if (len != (~nlen & 0xFFFFu)) throw ZipError("deflate: stored block length mismatch");
    for (unsigned k = 0; k < len; ++k) out.push_back(static_cast<char>(br.byte()));
}

void inflate_fixed(BitReader& br, std::string& out) {
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built) {
        std::uint16_t lengths[288];
        int s = 0;
        for (; s < 144; ++s) lengths[s] = 8;
        for (; s < 256; ++s) lengths[s] = 9;
        for (; s < 280; ++s) lengths[s] = 7;
        for (; s < 288; ++s) lengths[s] = 8;
        lencode.build(lengths, 288);
        for (s = 0; s < 30; ++s) lengths[s] = 5;
        distcode.build(lengths, 30);
        built = true;
    }
    inflate_codes(br, out, lencode, distcode);
}

void inflate_dynamic(BitReader& br, std::string& out) {
    static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = static_cast<int>(br.bits(5)) + 257;
    int ndist = static_cast<int>(br.bits(5)) + 1;
    int ncode = static_cast<int>(br.bits(4)) + 4;
    if (nlen > 286 || ndist > 30) throw ZipError("deflate: bad dynamic block counts");

    std::uint16_t lengths[320] = {0};
    for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<std::uint16_t>(br.bits(3));
    Huffman lencode, distcode;
    if (lencode.build(lengths, 19) != 0) throw ZipError("deflate: incomplete code-length code");

    int index = 0;
    while (index < nlen + ndist) {
        int sym = lencode.decode(br);
        if (sym < 16) {
            lengths[index++] = static_cast<std::uint16_t>(sym);
        } else {
            std::uint16_t len = 0;
            int rep;
            if (sym == 16) {
                if (index == 0) throw ZipError("deflate: repeat with no previous length");
                len = lengths[index - 1];
                rep = 3 + static_cast<int>(br.bits(2));
            } else if (sym == 17) {
                rep = 3 + static_cast<int>(br.bits(3));
            } else {
                rep = 11 + static_cast<int>(br.bits(7));
            }
            if (index + rep > nlen + ndist) throw ZipError("deflate: too many code lengths");
            while (rep--) lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) throw ZipError("deflate: missing end-of-block code");
    int err = lencode.build(lengths, nlen);
    if (err != 0 && nlen - lencode.count[0] != 1) throw ZipError("deflate: incomplete literal/length code");
    err = distcode.build(lengths + nlen, ndist);
    if (err != 0 && ndist - distcode.count[0] != 1) throw ZipError("deflate: incomplete distance code");
    inflate_codes(br, out, lencode, distcode);
}

std::uint16_t rd16(const std::string& d, std::size_t off) {
    if (off + 2 > d.size()) throw ZipError("zip: truncated archive");
    return static_cast<std::uint16_t>(static_cast<unsigned char>(d[off]) |
                                      (static_cast<unsigned char>(d[off + 1]) << 8));
}

std::uint32_t rd32(const std::string& d, std::size_t off) {
    return static_cast<std::uint32_t>(rd16(d, off)) | (static_cast<std::uint32_t>(rd16(d, off + 2)) << 16);
}

}  // namespace

std::string inflate_raw(const std::uint8_t* data, std::size_t size, std::size_t expected_size) {
    std::string out;
    if (expected_size) out.reserve(expected_size);
    BitReader br(data, size);
    int last;
    do {
        last = static_cast<int>(br.bits(1));
        int type = static_cast<int>(br.bits(2));
        switch (type) {
            case 0: inflate_stored(br, out); break;
            case 1: inflate_fixed(br, out); break;
            case 2: inflate_dynamic(br, out); break;
            default: throw ZipError("deflate: invalid block type");
        }
    } while (!last);
    return out;
}

std::uint32_t crc32(const std::string& data) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

ZipArchive::ZipArchive(std::string bytes) : data_(std::move(bytes)) {
    const std::string& d = data_;
    if (d.size() < 22) throw ZipError("zip: file too small to be an archive");
    // Locate the End Of Central Directory record (it may be followed by a comment).
    std::size_t eocd = std::string::npos;
    std::size_t stop = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
    for (std::size_t i = d.size() - 22 + 1; i-- > stop;) {
        if (rd32(d, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) throw ZipError("zip: end of central directory not found (not a zip/docx file?)");
    std::uint16_t count = rd16(d, eocd + 10);
    std::uint32_t cd_offset = rd32(d, eocd + 16);
    std::size_t p = cd_offset;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (rd32(d, p) != 0x02014b50u) throw ZipError("zip: bad central directory entry");
        Entry e;
        e.method = rd16(d, p + 10);
        e.crc = rd32(d, p + 16);
        e.compressed_size = rd32(d, p + 20);
        e.uncompressed_size = rd32(d, p + 24);
        std::uint16_t name_len = rd16(d, p + 28);
        std::uint16_t extra_len = rd16(d, p + 30);
        std::uint16_t comment_len = rd16(d, p + 32);
        e.local_header_offset = rd32(d, p + 42);
        if (p + 46 + name_len > d.size()) throw ZipError("zip: truncated central directory");
        e.name = d.substr(p + 46, name_len);
        entries_.push_back(std::move(e));
        p += 46u + name_len + extra_len + comment_len;
    }
}

ZipArchive ZipArchive::open_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ZipError("cannot open file: " + path);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ZipArchive(std::move(bytes));
}

bool ZipArchive::contains(const std::string& name) const {
    for (const auto& e : entries_) {
        if (e.name == name) return true;
    }
    return false;
}

std::string ZipArchive::read(const std::string& name) const {
    for (const auto& e : entries_) {
        if (e.name != name) continue;
        const std::string& d = data_;
        std::size_t lh = e.local_header_offset;
        if (rd32(d, lh) != 0x04034b50u) throw ZipError("zip: bad local header for " + name);
        std::size_t start = lh + 30u + rd16(d, lh + 26) + rd16(d, lh + 28);
        if (start + e.compressed_size > d.size()) throw ZipError("zip: entry data out of range: " + name);
        const auto* src = reinterpret_cast<const std::uint8_t*>(d.data()) + start;
        std::string out;
        if (e.method == 0) {
            out.assign(reinterpret_cast<const char*>(src), e.compressed_size);
        } else if (e.method == 8) {
            out = inflate_raw(src, e.compressed_size, e.uncompressed_size);
        } else {
            throw ZipError("zip: unsupported compression method " + std::to_string(e.method) + " for " + name);
        }
        if (crc32(out) != e.crc) throw ZipError("zip: CRC mismatch for " + name);
        return out;
    }
    throw ZipError("zip: entry not found: " + name);
}

}  // namespace csa

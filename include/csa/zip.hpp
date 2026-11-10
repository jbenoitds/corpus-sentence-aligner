// SPDX-License-Identifier: MIT
// Read-only ZIP archive access with a built-in DEFLATE (RFC 1951) decoder,
// so .docx files can be read without zlib or any other dependency.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace csa {

struct ZipError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Decompress a raw DEFLATE stream. Throws ZipError on malformed input.
/// `expected_size` is only a capacity hint.
std::string inflate_raw(const std::uint8_t* data, std::size_t size, std::size_t expected_size = 0);

/// CRC-32 (ISO-HDLC polynomial, as used by ZIP).
std::uint32_t crc32(const std::string& data);

class ZipArchive {
public:
    struct Entry {
        std::string name;
        std::uint16_t method = 0;
        std::uint32_t crc = 0;
        std::uint32_t compressed_size = 0;
        std::uint32_t uncompressed_size = 0;
        std::uint32_t local_header_offset = 0;
    };

    /// Takes ownership of the whole archive bytes.
    explicit ZipArchive(std::string bytes);

    static ZipArchive open_file(const std::string& path);

    const std::vector<Entry>& entries() const { return entries_; }
    bool contains(const std::string& name) const;

    /// Extract an entry by exact name. Verifies the CRC.
    std::string read(const std::string& name) const;

private:
    std::string data_;
    std::vector<Entry> entries_;
};

}  // namespace csa

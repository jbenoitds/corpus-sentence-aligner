// SPDX-License-Identifier: MIT
// Writers for aligned output: TMX 1.4b, CSV, TSV and a review-ready XLSX.
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "csa/aligner.hpp"

namespace csa {

constexpr const char* kVersion = "1.0.0";

struct DocumentAlignment {
    std::string name;  ///< document label (e.g. source file name)
    AlignmentResult result;
};

struct ExportOptions {
    std::string src_lang = "en";
    std::string tgt_lang = "fr";
    /// TMX only: also write units with an empty side (1-0 / 0-1).
    bool keep_unaligned = false;
    /// CSV only: write a UTF-8 byte-order mark (helps Excel detect UTF-8).
    bool csv_bom = true;
    char csv_separator = ',';
    /// TMX creation date (YYYYMMDDThhmmssZ). Empty = current UTC time.
    std::string creation_date;
};

/// Escape text for XML element content / attribute values and drop
/// characters that are not allowed in XML 1.0.
std::string xml_escape(const std::string& s);

/// Quote one CSV field (RFC 4180) if it needs quoting.
std::string csv_field(const std::string& s, char sep);

/// 1-based ids joined with '+' ("4+5"); empty string if none.
std::string format_ids(const std::vector<std::size_t>& ids);

void write_tmx(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts);
void write_csv(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts);
void write_tsv(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts);
/// Excel workbook (Office Open XML) with wrapped text, frozen header,
/// filters and highlighted review rows. Written as a stored (uncompressed) zip.
void write_xlsx(std::ostream& out, const std::vector<DocumentAlignment>& docs, const ExportOptions& opts);

/// Write a stored ZIP archive from (name, content) entries.
void write_stored_zip(std::ostream& out, const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace csa

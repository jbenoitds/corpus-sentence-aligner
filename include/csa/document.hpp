// SPDX-License-Identifier: MIT
// Loading source/target documents as lists of paragraphs.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace csa {

struct DocumentError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct LoadOptions {
    /// Plain text only: if true, paragraphs are separated by blank lines and
    /// the lines inside a paragraph are joined; if false (default), every
    /// non-empty line is a paragraph.
    bool blank_line_paragraphs = false;
    /// .docx only: keep superscript runs that look like citation/footnote
    /// numbers ("2", "1,3", "4-6"). Dropped by default.
    bool keep_superscript_numbers = false;
    /// .docx only: treat manual line breaks (<w:br/>) as paragraph breaks
    /// (default) instead of spaces.
    bool line_break_is_paragraph = true;
};

/// Load a document as a list of normalised, non-empty paragraphs.
/// Supported: UTF-8 / UTF-16 (with BOM) plain text, and .docx.
std::vector<std::string> load_paragraphs(const std::string& path, const LoadOptions& opts = {});

/// Extract paragraphs from WordprocessingML (word/document.xml content).
std::vector<std::string> docx_paragraphs_from_xml(const std::string& xml, const LoadOptions& opts = {});

/// Extract paragraphs from the bytes of a .docx file.
std::vector<std::string> docx_paragraphs_from_bytes(std::string bytes, const LoadOptions& opts = {});

/// Split decoded plain text into paragraphs (handles BOM, CRLF, UTF-16).
std::vector<std::string> text_paragraphs_from_bytes(const std::string& bytes, const LoadOptions& opts = {});

/// Decode the five predefined XML entities and numeric character references.
std::string xml_unescape(const std::string& s);

}  // namespace csa

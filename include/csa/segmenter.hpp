// SPDX-License-Identifier: MIT
// Rule-based sentence segmentation for English and French.
#pragma once

#include <set>
#include <string>
#include <vector>

namespace csa {

class SentenceSplitter {
public:
    /// `lang` is a language tag such as "en", "en-CA", "fr", "fr-CA".
    /// Languages other than English/French use a generic rule set
    /// (punctuation + capitalisation, with no abbreviation list).
    explicit SentenceSplitter(const std::string& lang);

    /// Split one paragraph into sentences. Sentences are trimmed;
    /// concatenating them with single spaces gives back the paragraph
    /// (modulo whitespace).
    std::vector<std::string> split(const std::string& paragraph) const;

    /// Add extra abbreviations (without the final period, any case).
    /// `blocking` abbreviations never end a sentence ("Dr", "M", "e.g");
    /// non-blocking ones end a sentence only when followed by an
    /// upper-case word ("etc", "a.m").
    void add_abbreviation(const std::string& abbrev, bool blocking = true);

    /// Load abbreviations from a UTF-8 file, one per line; lines starting
    /// with '#' are comments; a trailing '*' marks a non-blocking entry.
    void load_abbreviations(const std::string& path);

    const std::string& language() const { return lang_; }

private:
    std::string lang_;
    std::set<std::string> blocking_;
    std::set<std::string> nonblocking_;
};

}  // namespace csa

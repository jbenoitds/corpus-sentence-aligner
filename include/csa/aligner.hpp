// SPDX-License-Identifier: MIT
// Document-level alignment: segmentation + (optionally hierarchical)
// Gale-Church alignment + review flags.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "csa/gale_church.hpp"

namespace csa {

struct Segment {
    std::string text;
    std::size_t paragraph = 0;        ///< 0-based paragraph index
    std::size_t length = 0;           ///< length in characters (code points)
    std::vector<std::string> anchors; ///< sorted anchor tokens (numbers, acronyms, URLs)
};

struct AlignedPair {
    std::vector<std::size_t> src_ids;  ///< 0-based sentence indices
    std::vector<std::size_t> tgt_ids;
    std::string src_text;              ///< sentences joined by a space
    std::string tgt_text;
    double cost = 0.0;
    double delta = 0.0;
    bool review = false;               ///< flagged for human review

    std::string type() const { return std::to_string(src_ids.size()) + "-" + std::to_string(tgt_ids.size()); }
};

enum class AlignMode { Hierarchical, Flat };

struct AlignerOptions {
    std::string src_lang = "en";
    std::string tgt_lang = "fr";
    AlignMode mode = AlignMode::Hierarchical;
    double c = 0.0;               ///< 0 = estimate from the documents
    double s2 = 6.8;
    bool extended_beads = false;  ///< allow 3-1 / 1-3 at sentence level
    bool anchors = true;          ///< use shared numbers/acronyms/URLs as a soft cue
    double anchor_weight = 1.0;   ///< cost (nats) per mismatched anchor token
    bool presegmented = false;    ///< each paragraph is already one sentence
    double review_delta = 2.5;    ///< flag beads with |delta| above this
    /// Pure Gale-Church: score 1-0 / 0-1 beads with the normal match model
    /// (default false: unmatched units cost length / mean unit length).
    bool classic = false;
    std::vector<std::string> src_abbrev_files;
    std::vector<std::string> tgt_abbrev_files;
};

struct AlignmentResult {
    std::vector<Segment> src;
    std::vector<Segment> tgt;
    std::vector<AlignedPair> pairs;
    double c_used = 1.0;
    std::size_t src_paragraphs = 0;
    std::size_t tgt_paragraphs = 0;
};

/// Default characters-per-character ratio used when the documents are too
/// short (or too unbalanced) to estimate it reliably.
constexpr double kEnFrCharRatio = 1.15;
double default_char_ratio(const std::string& src_lang, const std::string& tgt_lang);

/// Split paragraphs into sentences (or keep them whole if presegmented).
std::vector<Segment> segment_paragraphs(const std::vector<std::string>& paragraphs, const std::string& lang,
                                        bool presegmented, const std::vector<std::string>& abbrev_files = {});

/// Anchor tokens of a text: digit runs (leading zeros stripped),
/// ASCII acronyms of 2+ capitals, and URLs / e-mail addresses.
std::vector<std::string> extract_anchors(const std::string& text);

/// Align two documents given as paragraph lists.
AlignmentResult align_documents(const std::vector<std::string>& src_paragraphs,
                                const std::vector<std::string>& tgt_paragraphs, const AlignerOptions& opts);

}  // namespace csa

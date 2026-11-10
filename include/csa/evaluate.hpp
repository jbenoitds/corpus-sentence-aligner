// SPDX-License-Identifier: MIT
// Evaluation against a reference ("gold") alignment.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "csa/aligner.hpp"

namespace csa {

/// One reference bead: 0-based sentence ids on each side (either may be empty).
using GoldBead = std::pair<std::vector<std::size_t>, std::vector<std::size_t>>;

/// Read a reference alignment. Accepted formats:
///  * this tool's CSV/TSV export (header with `src_ids` and `tgt_ids`),
///    e.g. after correcting it by hand;
///  * a plain two-column file, one bead per line: "1+2<TAB>3".
/// Ids are 1-based; a side may be empty or "-". Ids may be separated by
/// '+', ',', ';' or spaces. Lines starting with '#' are ignored.
std::vector<GoldBead> read_gold(const std::string& path);

/// Parse CSV text (RFC 4180, any single-character separator).
std::vector<std::vector<std::string>> parse_csv(const std::string& text, char sep);

struct EvalScores {
    std::size_t gold_links = 0, test_links = 0, correct_links = 0;
    std::size_t gold_beads = 0, test_beads = 0, correct_beads = 0;
    double link_precision() const { return test_links ? double(correct_links) / double(test_links) : 0.0; }
    double link_recall() const { return gold_links ? double(correct_links) / double(gold_links) : 0.0; }
    double link_f1() const {
        double p = link_precision(), r = link_recall();
        return (p + r) > 0 ? 2 * p * r / (p + r) : 0.0;
    }
    double bead_precision() const { return test_beads ? double(correct_beads) / double(test_beads) : 0.0; }
    double bead_recall() const { return gold_beads ? double(correct_beads) / double(gold_beads) : 0.0; }
};

/// Links are (source sentence, target sentence) pairs implied by beads
/// (1-0/0-1 beads contribute no links). Beads count as correct only if both
/// sides match exactly (null beads included).
EvalScores evaluate(const std::vector<AlignedPair>& test, const std::vector<GoldBead>& gold);

}  // namespace csa

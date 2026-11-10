// SPDX-License-Identifier: MIT
// Length-based alignment after Gale & Church (1993), "A Program for Aligning
// Sentences in Bilingual Corpora", Computational Linguistics 19(1):75-102.
//
// Each alignment "bead" pairs `s` consecutive source units with `t`
// consecutive target units. Its cost is
//
//     -log P(bead type) - log P(delta | match),
//     delta = (l2 - c*l1) / sqrt(s2 * l1),
//
// where l1/l2 are the summed character lengths, c the expected number of
// target characters per source character and s2 the variance per character.
// P(delta | match) = 2 * (1 - Phi(|delta|)) is the two-tailed normal
// probability. The best alignment is found by dynamic programming.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace csa {

struct BeadType {
    int src = 1;
    int tgt = 1;
    double prior = 0.89;  ///< P(bead type) as in Gale & Church table 5
};

/// The six bead types of the original paper with their published priors.
std::vector<BeadType> gale_church_bead_types();

/// Original types plus 3-1 and 1-3 (useful for paragraph-level alignment
/// and for translators who merge or split heavily).
std::vector<BeadType> extended_bead_types();

struct AlignParams {
    double c = 1.0;    ///< target chars per source char
    double s2 = 6.8;   ///< variance per character
    std::vector<BeadType> beads = gale_church_bead_types();
    /// Optional search band (in DP cells around the diagonal); 0 = unlimited.
    std::size_t band = 0;
    /// Length model for 1-0 / 0-1 beads. 0 (classic Gale-Church) scores an
    /// unmatched unit with the same normal match model as any other bead,
    /// which makes long omissions extremely expensive: an untranslated
    /// paragraph then gets forced into a bad 1-1 pair. A positive value L
    /// instead scores an unmatched unit of length l as l / L (an
    /// exponential length model with mean L, e.g. the mean unit length).
    double null_mean_length = 0.0;
};

struct Bead {
    std::size_t src_begin = 0;
    std::size_t src_count = 0;
    std::size_t tgt_begin = 0;
    std::size_t tgt_count = 0;
    double cost = 0.0;   ///< total cost of this bead (nats)
    double delta = 0.0;  ///< length z-score; |delta| > 2.5 is unusual
};

/// Two-tailed match probability term: -log(2 * (1 - Phi(|delta|))).
/// Returns the cost and stores delta in `delta_out` if non-null.
double length_cost(double l1, double l2, double c, double s2, double* delta_out = nullptr);

/// Optional extra cost added to each candidate bead, e.g. anchor mismatches.
/// Arguments: src_begin, src_count, tgt_begin, tgt_count.
class ExtraCost {
public:
    virtual ~ExtraCost() = default;
    virtual double operator()(std::size_t sb, std::size_t sc, std::size_t tb, std::size_t tc) const = 0;
};

/// Align two sequences of lengths. Returns beads in document order that
/// cover every unit exactly once.
std::vector<Bead> align_lengths(const std::vector<double>& src, const std::vector<double>& tgt,
                                const AlignParams& params, const ExtraCost* extra = nullptr);

/// String form of a bead type, e.g. "2-1".
std::string bead_label(const Bead& b);

}  // namespace csa

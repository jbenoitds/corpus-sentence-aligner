// SPDX-License-Identifier: MIT
#include "csa/gale_church.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace csa {

std::vector<BeadType> gale_church_bead_types() {
    // Table 5 of Gale & Church (1993): 1-1 .89, 1-0/0-1 .0099,
    // 2-1/1-2 .089, 2-2 .011. Following their reference implementation the
    // per-direction penalty uses the combined probability.
    return {{1, 1, 0.89}, {1, 0, 0.0099}, {0, 1, 0.0099}, {2, 1, 0.089}, {1, 2, 0.089}, {2, 2, 0.011}};
}

std::vector<BeadType> extended_bead_types() {
    auto b = gale_church_bead_types();
    b.push_back({3, 1, 0.005});
    b.push_back({1, 3, 0.005});
    return b;
}

double length_cost(double l1, double l2, double c, double s2, double* delta_out) {
    if (l1 <= 0.0 && l2 <= 0.0) {
        if (delta_out) *delta_out = 0.0;
        return 0.0;
    }
    // Symmetric form used by the reference implementation: the variance is
    // based on the mean of the source length and the c-normalised target.
    double mean = (l1 + l2 / c) / 2.0;
    double delta = (l2 - c * l1) / std::sqrt(s2 * mean);
    if (delta_out) *delta_out = delta;
    double z = std::fabs(delta);
    // 2 * (1 - Phi(z)) == erfc(z / sqrt(2)).
    double pd = std::erfc(z / std::sqrt(2.0));
    const double floor = 1e-300;
    if (pd < floor) {
        // Asymptotic tail for very large z to keep costs finite and monotone.
        const double half_log_pi_over_2 = 0.22579135264472744;  // 0.5 * ln(pi / 2)
        return z * z / 2.0 + std::log(z) + half_log_pi_over_2;
    }
    return -std::log(pd);
}

namespace {

double bead_length_cost(double l1, double l2, const BeadType& bt, const AlignParams& params, double* delta) {
    if (params.null_mean_length > 0.0 && (bt.src == 0 || bt.tgt == 0)) {
        if (delta) length_cost(l1, l2, params.c, params.s2, delta);
        double l = bt.src == 0 ? l2 / params.c : l1;  // in source-character units
        return l / params.null_mean_length;
    }
    return length_cost(l1, l2, params.c, params.s2, delta);
}

}  // namespace

std::string bead_label(const Bead& b) { return std::to_string(b.src_count) + "-" + std::to_string(b.tgt_count); }

std::vector<Bead> align_lengths(const std::vector<double>& src, const std::vector<double>& tgt,
                                const AlignParams& params, const ExtraCost* extra) {
    const std::size_t n = src.size();
    const std::size_t m = tgt.size();
    if (params.beads.empty()) throw std::invalid_argument("align_lengths: no bead types");
    if (params.beads.size() > 250) throw std::invalid_argument("align_lengths: too many bead types");
    if (params.c <= 0.0 || params.s2 <= 0.0) throw std::invalid_argument("align_lengths: c and s2 must be > 0");

    // Prefix sums for O(1) span lengths.
    std::vector<double> ps(n + 1, 0.0), pt(m + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) ps[i + 1] = ps[i] + src[i];
    for (std::size_t j = 0; j < m; ++j) pt[j + 1] = pt[j] + tgt[j];

    const double p11 = [&] {
        for (const auto& b : params.beads)
            if (b.src == 1 && b.tgt == 1) return b.prior;
        return params.beads.front().prior;
    }();
    std::vector<double> penalty;
    int max_s = 0;
    for (const auto& b : params.beads) {
        if (b.src < 0 || b.tgt < 0 || (b.src == 0 && b.tgt == 0) || b.prior <= 0.0)
            throw std::invalid_argument("align_lengths: invalid bead type");
        // Penalties relative to a 1-1 bead (Gale & Church reference code).
        penalty.push_back(-std::log(b.prior / p11));
        max_s = std::max(max_s, b.src);
    }

    const double inf = std::numeric_limits<double>::infinity();
    const std::size_t cols = m + 1;
    // Rolling cost rows (enough to look back max_s rows) and a full
    // back-pointer matrix (one byte per cell).
    const std::size_t nrows = static_cast<std::size_t>(max_s) + 1;
    std::vector<std::vector<double>> cost(nrows, std::vector<double>(cols, inf));
    std::vector<std::uint8_t> back((n + 1) * cols, 0xFF);

    auto in_band = [&](std::size_t i, std::size_t j) {
        if (params.band == 0 || n == 0 || m == 0) return true;
        double diag = static_cast<double>(i) * static_cast<double>(m) / static_cast<double>(n);
        return std::fabs(static_cast<double>(j) - diag) <= static_cast<double>(params.band);
    };

    for (std::size_t i = 0; i <= n; ++i) {
        auto& row = cost[i % nrows];
        std::fill(row.begin(), row.end(), inf);
        for (std::size_t j = 0; j <= m; ++j) {
            if (i == 0 && j == 0) {
                row[0] = 0.0;
                continue;
            }
            if (!in_band(i, j) && !(i == n && j == m)) continue;
            double best = inf;
            std::uint8_t arg = 0xFF;
            for (std::size_t k = 0; k < params.beads.size(); ++k) {
                const auto& bt = params.beads[k];
                if (static_cast<std::size_t>(bt.src) > i || static_cast<std::size_t>(bt.tgt) > j) continue;
                std::size_t pi = i - static_cast<std::size_t>(bt.src);
                std::size_t pj = j - static_cast<std::size_t>(bt.tgt);
                double prev = cost[pi % nrows][pj];
                if (prev == inf) continue;
                double l1 = ps[i] - ps[pi];
                double l2 = pt[j] - pt[pj];
                double c = prev + penalty[k] + bead_length_cost(l1, l2, bt, params, nullptr);
                if (extra) c += (*extra)(pi, static_cast<std::size_t>(bt.src), pj, static_cast<std::size_t>(bt.tgt));
                if (c < best) {
                    best = c;
                    arg = static_cast<std::uint8_t>(k);
                }
            }
            row[j] = best;
            back[i * cols + j] = arg;
        }
    }

    std::vector<Bead> beads;
    std::size_t i = n, j = m;
    while (i > 0 || j > 0) {
        std::uint8_t k = back[i * cols + j];
        if (k == 0xFF) throw std::runtime_error("align_lengths: no alignment path (band too narrow?)");
        const auto& bt = params.beads[k];
        Bead b;
        b.src_count = static_cast<std::size_t>(bt.src);
        b.tgt_count = static_cast<std::size_t>(bt.tgt);
        b.src_begin = i - b.src_count;
        b.tgt_begin = j - b.tgt_count;
        double l1 = ps[i] - ps[b.src_begin];
        double l2 = pt[j] - pt[b.tgt_begin];
        b.cost = penalty[k] + bead_length_cost(l1, l2, bt, params, &b.delta);
        if (extra) b.cost += (*extra)(b.src_begin, b.src_count, b.tgt_begin, b.tgt_count);
        beads.push_back(b);
        i = b.src_begin;
        j = b.tgt_begin;
    }
    std::reverse(beads.begin(), beads.end());
    return beads;
}

}  // namespace csa

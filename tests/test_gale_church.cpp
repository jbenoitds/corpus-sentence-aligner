// SPDX-License-Identifier: MIT
#include <random>

#include "csa/gale_church.hpp"
#include "minitest.hpp"

using namespace csa;

namespace {

void check_cover(const std::vector<Bead>& beads, std::size_t n, std::size_t m) {
    std::size_t i = 0, j = 0;
    for (const auto& b : beads) {
        CHECK_EQ(b.src_begin, i);
        CHECK_EQ(b.tgt_begin, j);
        i += b.src_count;
        j += b.tgt_count;
    }
    CHECK_EQ(i, n);
    CHECK_EQ(j, m);
}

std::vector<std::string> labels(const std::vector<Bead>& beads) {
    std::vector<std::string> out;
    for (const auto& b : beads) out.push_back(bead_label(b));
    return out;
}

}  // namespace

TEST(gc_length_cost_properties) {
    CHECK_NEAR(length_cost(100, 100, 1.0, 6.8), 0.0, 1e-12);
    CHECK_NEAR(length_cost(0, 0, 1.0, 6.8), 0.0, 1e-12);
    double small = length_cost(100, 110, 1.0, 6.8);
    double large = length_cost(100, 200, 1.0, 6.8);
    CHECK(small > 0.0);
    CHECK(large > small);
    // Huge mismatch stays finite and keeps growing.
    double huge = length_cost(5000, 10, 1.0, 6.8);
    double huger = length_cost(10000, 10, 1.0, 6.8);
    CHECK(std::isfinite(huge));
    CHECK(huger > huge);
    // c rescales the expectation.
    CHECK_NEAR(length_cost(100, 115, 1.15, 6.8), 0.0, 1e-9);
    double d = 0;
    length_cost(100, 50, 1.0, 6.8, &d);
    CHECK(d < 0.0);
}

TEST(gc_known_delta) {
    // mean = (100 + 130)/2 = 115, delta = 30 / sqrt(6.8 * 115) = 1.0727...
    double d = 0;
    double cost = length_cost(100, 130, 1.0, 6.8, &d);
    CHECK_NEAR(d, 30.0 / std::sqrt(6.8 * 115.0), 1e-12);
    CHECK_NEAR(cost, -std::log(std::erfc(d / std::sqrt(2.0))), 1e-12);
}

TEST(gc_identical_lengths_give_one_to_one) {
    std::vector<double> a = {40, 85, 12, 60, 130, 7};
    auto beads = align_lengths(a, a, AlignParams{});
    CHECK_EQ(beads.size(), a.size());
    for (const auto& b : beads) CHECK_EQ(bead_label(b), std::string("1-1"));
}

TEST(gc_detects_merge_and_split) {
    std::vector<double> src = {50, 60, 40, 120, 30};
    std::vector<double> tgt = {112, 41, 58, 64, 31};  // (50+60)->112, 40->41, 120->(58+64), 30->31
    auto beads = align_lengths(src, tgt, AlignParams{});
    check_cover(beads, src.size(), tgt.size());
    CHECK_EQ(labels(beads), (std::vector<std::string>{"2-1", "1-1", "1-2", "1-1"}));
}

TEST(gc_empty_sides) {
    auto b1 = align_lengths({}, {10, 20}, AlignParams{});
    CHECK_EQ(labels(b1), (std::vector<std::string>{"0-1", "0-1"}));
    auto b2 = align_lengths({15}, {}, AlignParams{});
    CHECK_EQ(labels(b2), (std::vector<std::string>{"1-0"}));
    CHECK(align_lengths({}, {}, AlignParams{}).empty());
}

TEST(gc_random_inputs_always_cover) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> len(1, 200), count(0, 40);
    for (int trial = 0; trial < 50; ++trial) {
        std::vector<double> a(static_cast<std::size_t>(count(rng))), b(static_cast<std::size_t>(count(rng)));
        for (auto& x : a) x = len(rng);
        for (auto& x : b) x = len(rng);
        AlignParams p;
        p.beads = extended_bead_types();
        check_cover(align_lengths(a, b, p), a.size(), b.size());
    }
}

TEST(gc_band_matches_full_search_near_diagonal) {
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> len(5, 150);
    std::vector<double> a(300), b;
    for (auto& x : a) x = len(rng);
    for (double x : a) b.push_back(x * 1.1 + (len(rng) % 7) - 3);
    AlignParams full, banded;
    banded.c = full.c = 1.1;
    banded.band = 20;
    auto f = align_lengths(a, b, full);
    auto g = align_lengths(a, b, banded);
    CHECK_EQ(labels(f), labels(g));
}

TEST(gc_extra_cost_is_applied) {
    // Two equally plausible alignments; an extra cost forbidding (0,0)-(1-1)
    // pushes the solution elsewhere.
    struct NoFirstPair : ExtraCost {
        double operator()(std::size_t sb, std::size_t sc, std::size_t tb, std::size_t tc) const override {
            return (sb == 0 && tb == 0 && sc == 1 && tc == 1) ? 100.0 : 0.0;
        }
    } extra;
    std::vector<double> a = {50, 50}, b = {100};
    auto plain = align_lengths(a, b, AlignParams{});
    CHECK_EQ(labels(plain), (std::vector<std::string>{"2-1"}));
    std::vector<double> c = {50, 50}, d = {50, 50};
    auto with = align_lengths(c, d, AlignParams{}, &extra);
    CHECK(labels(with) != (std::vector<std::string>{"1-1", "1-1"}));
}

TEST(gc_invalid_params) {
    AlignParams p;
    p.c = 0;
    CHECK_THROWS(align_lengths({1}, {1}, p));
    AlignParams q;
    q.beads.clear();
    CHECK_THROWS(align_lengths({1}, {1}, q));
}

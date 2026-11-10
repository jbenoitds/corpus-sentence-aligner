// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "csa/aligner.hpp"
#include "csa/document.hpp"
#include "csa/evaluate.hpp"
#include "csa/export.hpp"
#include "minitest.hpp"

using namespace csa;

namespace {

const std::string kData = CSA_TEST_DATA;

AlignmentResult align_sample(const AlignerOptions& o) {
    auto en = load_paragraphs(kData + "/sample_en.txt");
    auto fr = load_paragraphs(kData + "/sample_fr.txt");
    return align_documents(en, fr, o);
}

}  // namespace

TEST(anchor_extraction) {
    auto a = extract_anchors(u8"Call 514-555-0199 or write to Info@Example.org (COVID-19, BRCS) at 09:00 – I agree");
    std::vector<std::string> expected = {"#0", "#19", "#514", "#555", "#9", "#199", "BRCS", "COVID",
                                         "url:info@example.org"};
    std::sort(expected.begin(), expected.end());
    CHECK_EQ(a.size(), expected.size());
    for (std::size_t i = 0; i < std::min(a.size(), expected.size()); ++i) CHECK_EQ(a[i], expected[i]);
}

TEST(sample_segmentation_counts) {
    auto r = align_sample(AlignerOptions{});
    CHECK_EQ(r.src.size(), static_cast<std::size_t>(35));
    CHECK_EQ(r.tgt.size(), static_cast<std::size_t>(34));
    CHECK_EQ(r.src_paragraphs, static_cast<std::size_t>(21));
    CHECK_EQ(r.tgt_paragraphs, static_cast<std::size_t>(22));
    CHECK(r.c_used > 1.05 && r.c_used < 1.25);  // French runs longer than English
}

TEST(sample_alignment_quality_hierarchical) {
    auto r = align_sample(AlignerOptions{});
    auto gold = read_gold(kData + "/sample_gold.tsv");
    CHECK_EQ(gold.size(), static_cast<std::size_t>(34));
    auto sc = evaluate(r.pairs, gold);
    // Every link is found, and the added (0-1) and omitted (1-0)
    // sentences are recognised as such.
    CHECK_NEAR(sc.link_recall(), 1.0, 1e-12);
    CHECK_NEAR(sc.link_precision(), 1.0, 1e-12);
    CHECK_NEAR(sc.bead_recall(), 1.0, 1e-12);
}

TEST(sample_alignment_quality_classic) {
    // Original Gale-Church scoring: still finds every link, but absorbs the
    // two null beads into neighbouring 1-2 / 2-1 beads.
    AlignerOptions o;
    o.classic = true;
    o.anchors = false;
    auto sc = evaluate(align_sample(o).pairs, read_gold(kData + "/sample_gold.tsv"));
    CHECK_NEAR(sc.link_recall(), 1.0, 1e-12);
    CHECK(sc.link_precision() >= 0.94);
}

TEST(sample_alignment_quality_flat) {
    AlignerOptions o;
    o.mode = AlignMode::Flat;
    auto sc = evaluate(align_sample(o).pairs, read_gold(kData + "/sample_gold.tsv"));
    CHECK(sc.link_f1() >= 0.95);
}

TEST(pairs_are_ordered_and_cover_everything) {
    for (AlignMode mode : {AlignMode::Hierarchical, AlignMode::Flat}) {
        AlignerOptions o;
        o.mode = mode;
        auto r = align_sample(o);
        std::size_t s = 0, t = 0;
        for (const auto& p : r.pairs) {
            for (auto id : p.src_ids) CHECK_EQ(id, s++);
            for (auto id : p.tgt_ids) CHECK_EQ(id, t++);
            CHECK(!p.src_ids.empty() || !p.tgt_ids.empty());
        }
        CHECK_EQ(s, r.src.size());
        CHECK_EQ(t, r.tgt.size());
    }
}

TEST(review_flags_follow_rules) {
    auto r = align_sample(AlignerOptions{});
    for (const auto& p : r.pairs) {
        std::string ty = p.type();
        bool common = ty == "1-1" || ty == "2-1" || ty == "1-2";
        if (!common) CHECK(p.review);
        if (common && std::fabs(p.delta) <= 2.5) CHECK(!p.review);
    }
}

TEST(presegmented_input) {
    AlignerOptions o;
    o.presegmented = true;
    auto r = align_documents({"One. Two.", "Three."}, {"Un. Deux.", "Trois."}, o);
    CHECK_EQ(r.src.size(), static_cast<std::size_t>(2));
    CHECK_EQ(r.pairs.size(), static_cast<std::size_t>(2));
    CHECK_EQ(r.pairs[0].src_text, std::string("One. Two."));
}

TEST(empty_document_side) {
    auto r = align_documents({"Only English here."}, {}, AlignerOptions{});
    CHECK_EQ(r.pairs.size(), static_cast<std::size_t>(1));
    CHECK_EQ(r.pairs[0].type(), std::string("1-0"));
    CHECK(r.pairs[0].review);
    auto e = align_documents({}, {}, AlignerOptions{});
    CHECK(e.pairs.empty());
}

TEST(paragraph_omission_is_detected) {
    // A whole paragraph missing from the translation must not shift the rest.
    std::vector<std::string> en = {"Introduction to the study.",
                                   "This paragraph was not translated because it only applies to Ontario residents and "
                                   "describes provincial billing rules in detail.",
                                   "Thank you for your time.", "Contact us at 514-555-0199."};
    std::vector<std::string> fr = {u8"Introduction à l’étude.", u8"Merci de votre temps.",
                                   u8"Communiquez avec nous au 514-555-0199."};
    auto r = align_documents(en, fr, AlignerOptions{});
    std::vector<GoldBead> gold = {{{0}, {0}}, {{1}, {}}, {{2}, {1}}, {{3}, {2}}};
    auto sc = evaluate(r.pairs, gold);
    CHECK_NEAR(sc.bead_recall(), 1.0, 1e-12);
}

TEST(csv_export_reads_back_as_perfect_gold) {
    auto r = align_sample(AlignerOptions{});
    std::vector<DocumentAlignment> docs = {{"sample", r}};
    std::ostringstream os;
    write_csv(os, docs, ExportOptions{});
    std::string path = kData + "/../roundtrip_tmp.csv";
    {
        std::ofstream f(path, std::ios::binary);
        f << os.str();
    }
    auto gold = read_gold(path);
    std::remove(path.c_str());
    auto sc = evaluate(r.pairs, gold);
    CHECK_NEAR(sc.link_f1(), 1.0, 1e-12);
    CHECK_NEAR(sc.bead_recall(), 1.0, 1e-12);
}

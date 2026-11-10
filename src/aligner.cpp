// SPDX-License-Identifier: MIT
#include "csa/aligner.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "csa/segmenter.hpp"
#include "csa/utf8.hpp"

namespace csa {

namespace {

bool ascii_upper(char32_t c) { return c >= 'A' && c <= 'Z'; }
bool ascii_alnum(char32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// Soft cue: anchors shared by both documents should end up in the same bead.
// Tokens are interned as integers so the inner DP loop never allocates.
class AnchorCost : public ExtraCost {
public:
    AnchorCost(const std::vector<const std::vector<std::string>*>& src,
               const std::vector<const std::vector<std::string>*>& tgt, double weight)
        : weight_(weight) {
        std::map<std::string, int> ids;
        auto intern = [&](const std::vector<const std::vector<std::string>*>& side,
                          std::vector<std::vector<int>>& out, std::vector<std::size_t>& count) {
            out.resize(side.size());
            count.assign(side.size() + 1, 0);
            for (std::size_t i = 0; i < side.size(); ++i) {
                for (const auto& a : *side[i]) out[i].push_back(ids.emplace(a, static_cast<int>(ids.size())).first->second);
                std::sort(out[i].begin(), out[i].end());
                count[i + 1] = count[i] + out[i].size();
            }
        };
        intern(src, src_, src_count_);
        intern(tgt, tgt_, tgt_count_);
    }

    double operator()(std::size_t sb, std::size_t sc, std::size_t tb, std::size_t tc) const override {
        std::size_t ns = src_count_[sb + sc] - src_count_[sb];
        std::size_t nt = tgt_count_[tb + tc] - tgt_count_[tb];
        if (ns == 0 && nt == 0) return 0.0;
        if (ns == 0 || nt == 0) return weight_ * static_cast<double>(ns + nt);
        const std::vector<int>* a = &src_[sb];
        const std::vector<int>* b = &tgt_[tb];
        if (sc > 1) a = &gather(src_, sb, sc, buf_a_);
        if (tc > 1) b = &gather(tgt_, tb, tc, buf_b_);
        std::size_t i = 0, j = 0, common = 0;
        while (i < a->size() && j < b->size()) {
            if ((*a)[i] < (*b)[j]) ++i;
            else if ((*b)[j] < (*a)[i]) ++j;
            else {
                ++common;
                ++i;
                ++j;
            }
        }
        return weight_ * static_cast<double>(ns + nt - 2 * common);
    }

private:
    static const std::vector<int>& gather(const std::vector<std::vector<int>>& side, std::size_t b, std::size_t n,
                                          std::vector<int>& buf) {
        buf.clear();
        for (std::size_t k = b; k < b + n; ++k) buf.insert(buf.end(), side[k].begin(), side[k].end());
        std::sort(buf.begin(), buf.end());
        return buf;
    }

    std::vector<std::vector<int>> src_, tgt_;
    std::vector<std::size_t> src_count_, tgt_count_;
    mutable std::vector<int> buf_a_, buf_b_;  // reused scratch space
    double weight_;
};

// Keep only anchors that occur on both sides of the document pair; the rest
// carry no alignment information and would only inflate costs.
void filter_shared_anchors(std::vector<Segment>& src, std::vector<Segment>& tgt) {
    std::set<std::string> s, t;
    for (const auto& x : src) s.insert(x.anchors.begin(), x.anchors.end());
    for (const auto& x : tgt) t.insert(x.anchors.begin(), x.anchors.end());
    auto keep = [](std::vector<Segment>& segs, const std::set<std::string>& other) {
        for (auto& x : segs) {
            x.anchors.erase(std::remove_if(x.anchors.begin(), x.anchors.end(),
                                           [&](const std::string& a) { return other.count(a) == 0; }),
                            x.anchors.end());
        }
    };
    keep(src, t);
    keep(tgt, s);
}

std::string join_texts(const std::vector<Segment>& segs, const std::vector<std::size_t>& ids) {
    std::string out;
    for (std::size_t id : ids) {
        if (!out.empty()) out.push_back(' ');
        out += segs[id].text;
    }
    return out;
}

AlignParams make_params(double c, double s2, bool extended) {
    AlignParams p;
    p.c = c;
    p.s2 = s2;
    p.beads = extended ? extended_bead_types() : gale_church_bead_types();
    return p;
}

// Align a contiguous range of sentences [sb, sb+sn) x [tb, tb+tn).
void align_sentence_range(const AlignmentResult& res, std::size_t sb, std::size_t sn, std::size_t tb,
                          std::size_t tn, const AlignParams& params, const AlignerOptions& opts,
                          std::vector<AlignedPair>& out) {
    std::vector<double> sl, tl;
    std::vector<const std::vector<std::string>*> sa, ta;
    for (std::size_t k = sb; k < sb + sn; ++k) {
        sl.push_back(static_cast<double>(res.src[k].length));
        sa.push_back(&res.src[k].anchors);
    }
    for (std::size_t k = tb; k < tb + tn; ++k) {
        tl.push_back(static_cast<double>(res.tgt[k].length));
        ta.push_back(&res.tgt[k].anchors);
    }
    AnchorCost anchors(sa, ta, opts.anchor_weight);
    auto beads = align_lengths(sl, tl, params, opts.anchors ? &anchors : nullptr);
    for (const auto& b : beads) {
        AlignedPair p;
        for (std::size_t k = 0; k < b.src_count; ++k) p.src_ids.push_back(sb + b.src_begin + k);
        for (std::size_t k = 0; k < b.tgt_count; ++k) p.tgt_ids.push_back(tb + b.tgt_begin + k);
        p.cost = b.cost;
        p.delta = b.delta;
        out.push_back(std::move(p));
    }
}

}  // namespace

double default_char_ratio(const std::string& src_lang, const std::string& tgt_lang) {
    std::string s = utf8::to_lower(src_lang.substr(0, 2));
    std::string t = utf8::to_lower(tgt_lang.substr(0, 2));
    // Characters of French per character of English, measured on
    // English/French health-research documents (consent forms,
    // questionnaires, psycho-education modules).
    const double en_fr = kEnFrCharRatio;
    if (s == "en" && t == "fr") return en_fr;
    if (s == "fr" && t == "en") return 1.0 / en_fr;
    return 1.0;
}

std::vector<std::string> extract_anchors(const std::string& text) {
    std::vector<std::string> out;
    std::u32string u = utf8::decode(text);
    const std::size_t n = u.size();
    std::size_t i = 0;
    while (i < n) {
        // URLs and e-mail addresses: a whitespace-delimited token containing
        // "://", "www." or "@" followed by a dot.
        if (!utf8::is_space(u[i])) {
            std::size_t e = i;
            while (e < n && !utf8::is_space(u[e])) ++e;
            std::u32string tok = u.substr(i, e - i);
            std::string t = utf8::to_lower(utf8::encode(tok));
            bool url = t.find("://") != std::string::npos || t.compare(0, 4, "www.") == 0 ||
                       (t.find('@') != std::string::npos && t.find('.', t.find('@')) != std::string::npos);
            if (url) {
                while (!t.empty() && std::string(".,;:)]»\"'").find(t.back()) != std::string::npos) t.pop_back();
                std::size_t b = 0;
                while (b < t.size() && std::string("([«\"'<").find(t[b]) != std::string::npos) ++b;
                out.push_back("url:" + t.substr(b));
                i = e;
                continue;
            }
        }
        if (utf8::is_digit(u[i])) {
            std::size_t e = i;
            while (e < n && utf8::is_digit(u[e])) ++e;
            std::string num = utf8::encode(u.substr(i, e - i));
            std::size_t nz = num.find_first_not_of('0');
            num = (nz == std::string::npos) ? "0" : num.substr(nz);
            out.push_back("#" + num);
            i = e;
            continue;
        }
        if (ascii_upper(u[i]) && (i == 0 || !ascii_alnum(u[i - 1]))) {
            std::size_t e = i;
            bool all_upper = true;
            while (e < n && (utf8::is_alpha(u[e]))) {
                if (!ascii_upper(u[e])) all_upper = false;
                ++e;
            }
            if (all_upper && e - i >= 2) out.push_back(utf8::encode(u.substr(i, e - i)));
            i = e;
            continue;
        }
        ++i;
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Segment> segment_paragraphs(const std::vector<std::string>& paragraphs, const std::string& lang,
                                        bool presegmented, const std::vector<std::string>& abbrev_files) {
    SentenceSplitter splitter(lang);
    for (const auto& f : abbrev_files) splitter.load_abbreviations(f);
    std::vector<Segment> out;
    for (std::size_t p = 0; p < paragraphs.size(); ++p) {
        std::vector<std::string> sents;
        if (presegmented) sents.push_back(utf8::normalize_segment(paragraphs[p]));
        else sents = splitter.split(paragraphs[p]);
        for (auto& s : sents) {
            if (s.empty()) continue;
            Segment seg;
            seg.length = utf8::length(s);
            seg.anchors = extract_anchors(s);
            seg.text = std::move(s);
            seg.paragraph = p;
            out.push_back(std::move(seg));
        }
    }
    return out;
}

AlignmentResult align_documents(const std::vector<std::string>& src_paragraphs,
                                const std::vector<std::string>& tgt_paragraphs, const AlignerOptions& opts) {
    AlignmentResult res;
    res.src = segment_paragraphs(src_paragraphs, opts.src_lang, opts.presegmented, opts.src_abbrev_files);
    res.tgt = segment_paragraphs(tgt_paragraphs, opts.tgt_lang, opts.presegmented, opts.tgt_abbrev_files);
    if (opts.anchors) filter_shared_anchors(res.src, res.tgt);

    // Paragraph indices of non-empty paragraphs, in order.
    auto paragraph_spans = [](const std::vector<Segment>& segs) {
        std::vector<std::pair<std::size_t, std::size_t>> spans;  // [begin, end) in segment indices
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (i == 0 || segs[i].paragraph != segs[i - 1].paragraph) spans.push_back({i, i + 1});
            else spans.back().second = i + 1;
        }
        return spans;
    };
    auto sp = paragraph_spans(res.src);
    auto tp = paragraph_spans(res.tgt);
    res.src_paragraphs = sp.size();
    res.tgt_paragraphs = tp.size();

    const bool flat = res.src.empty() || res.tgt.empty() || opts.mode == AlignMode::Flat || opts.presegmented;

    // Top-level units: paragraphs (hierarchical) or sentences (flat).
    // Lengths include the joining spaces.
    std::vector<double> pl_s, pl_t;
    std::vector<std::vector<std::string>> pa_s, pa_t;
    if (flat) {
        for (const auto& s : res.src) {
            pl_s.push_back(static_cast<double>(s.length));
            pa_s.push_back(s.anchors);
        }
        for (const auto& t : res.tgt) {
            pl_t.push_back(static_cast<double>(t.length));
            pa_t.push_back(t.anchors);
        }
    } else {
        for (auto& s : sp) {
            double len = 0;
            std::vector<std::string> a;
            for (std::size_t k = s.first; k < s.second; ++k) {
                len += static_cast<double>(res.src[k].length) + (k > s.first ? 1.0 : 0.0);
                a.insert(a.end(), res.src[k].anchors.begin(), res.src[k].anchors.end());
            }
            pl_s.push_back(len);
            pa_s.push_back(std::move(a));
        }
        for (auto& t : tp) {
            double len = 0;
            std::vector<std::string> a;
            for (std::size_t k = t.first; k < t.second; ++k) {
                len += static_cast<double>(res.tgt[k].length) + (k > t.first ? 1.0 : 0.0);
                a.insert(a.end(), res.tgt[k].anchors.begin(), res.tgt[k].anchors.end());
            }
            pl_t.push_back(len);
            pa_t.push_back(std::move(a));
        }
    }
    std::vector<const std::vector<std::string>*> pas, pat;
    for (auto& a : pa_s) pas.push_back(&a);
    for (auto& a : pa_t) pat.push_back(&a);
    AnchorCost top_anchors(pas, pat, opts.anchor_weight);

    // Mean unit length (in source characters) for the unmatched-unit model.
    auto mean_length = [](const std::vector<double>& a, const std::vector<double>& b, double c) {
        double sum = 0;
        for (double x : a) sum += x;
        for (double x : b) sum += x / c;
        std::size_t n = a.size() + b.size();
        return n ? std::max(10.0, sum / static_cast<double>(n)) : 10.0;
    };
    auto top_params = [&](double c) {
        AlignParams p = make_params(c, opts.s2, !flat || opts.presegmented || opts.extended_beads);
        if (!opts.classic) p.null_mean_length = mean_length(pl_s, pl_t, c);
        // Very large inputs: search a band around the diagonal (wide enough
        // for the length difference plus 10% drift) to bound time and memory.
        const std::size_t n = pl_s.size(), m = pl_t.size();
        if (n * m > 4000000) {
            std::size_t diff = n > m ? n - m : m - n;
            p.band = std::max<std::size_t>(200, diff + std::max(n, m) / 10);
        }
        return p;
    };
    auto align_top = [&](double c) {
        return align_lengths(pl_s, pl_t, top_params(c), opts.anchors ? &top_anchors : nullptr);
    };

    // Estimate c robustly: start from the global length ratio, then refit
    // it on confident 1-1 units only, so that untranslated or added
    // passages do not bias it. A couple of iterations suffice.
    double c = opts.c;
    std::vector<Bead> top;
    if (c <= 0.0) {
        double ls = 0, lt = 0;
        for (double x : pl_s) ls += x;
        for (double x : pl_t) lt += x;
        const double prior = default_char_ratio(opts.src_lang, opts.tgt_lang);
        c = prior;
        if (ls > 0 && lt > 0) {
            double global = lt / ls;
            if (global >= prior / 1.3 && global <= prior * 1.3) c = global;
        }
        for (int iter = 0; iter < 4 && !pl_s.empty() && !pl_t.empty(); ++iter) {
            top = align_top(c);
            double ss = 0, tt = 0;
            std::size_t used = 0;
            for (const auto& b : top) {
                if (b.src_count != 1 || b.tgt_count != 1) continue;
                double a = pl_s[b.src_begin], t = pl_t[b.tgt_begin];
                if (a < 15 || t < 15 || std::fabs(b.delta) > 2.0) continue;
                ss += a;
                tt += t;
                ++used;
            }
            if (used < 3 || ss <= 0) break;
            double next = std::min(2.0, std::max(0.5, tt / ss));
            if (std::fabs(next - c) < 0.005 * c) {
                c = next;
                break;
            }
            c = next;
            top.clear();
        }
    }
    if (top.empty()) top = align_top(c);
    res.c_used = c;

    AlignParams sent_params = make_params(c, opts.s2, opts.extended_beads);
    if (!opts.classic) {
        std::vector<double> sl, tl;
        for (const auto& x : res.src) sl.push_back(static_cast<double>(x.length));
        for (const auto& x : res.tgt) tl.push_back(static_cast<double>(x.length));
        sent_params.null_mean_length = mean_length(sl, tl, c);
    }
    std::vector<AlignedPair> pairs;

    if (flat) {
        for (const auto& b : top) {
            AlignedPair p;
            for (std::size_t k = 0; k < b.src_count; ++k) p.src_ids.push_back(b.src_begin + k);
            for (std::size_t k = 0; k < b.tgt_count; ++k) p.tgt_ids.push_back(b.tgt_begin + k);
            p.cost = b.cost;
            p.delta = b.delta;
            pairs.push_back(std::move(p));
        }
    } else {
        const auto& pbeads = top;
        // Stage 2: sentences inside each paragraph bead.
        for (const auto& pb : pbeads) {
            std::size_t sb = pb.src_count ? sp[pb.src_begin].first : 0;
            std::size_t se = pb.src_count ? sp[pb.src_begin + pb.src_count - 1].second : 0;
            std::size_t tb = pb.tgt_count ? tp[pb.tgt_begin].first : 0;
            std::size_t te = pb.tgt_count ? tp[pb.tgt_begin + pb.tgt_count - 1].second : 0;
            if (pb.src_count == 0 || pb.tgt_count == 0) {
                for (std::size_t k = sb; k < se; ++k) {
                    AlignedPair p;
                    p.src_ids = {k};
                    p.cost = pb.cost;
                    pairs.push_back(std::move(p));
                }
                for (std::size_t k = tb; k < te; ++k) {
                    AlignedPair p;
                    p.tgt_ids = {k};
                    p.cost = pb.cost;
                    pairs.push_back(std::move(p));
                }
                continue;
            }
            align_sentence_range(res, sb, se - sb, tb, te - tb, sent_params, opts, pairs);
        }
    }

    for (auto& p : pairs) {
        p.src_text = join_texts(res.src, p.src_ids);
        p.tgt_text = join_texts(res.tgt, p.tgt_ids);
        std::size_t a = p.src_ids.size(), b = p.tgt_ids.size();
        bool common_type = (a == 1 && b == 1) || (a == 2 && b == 1) || (a == 1 && b == 2);
        p.review = !common_type || std::fabs(p.delta) > opts.review_delta;
    }
    res.pairs = std::move(pairs);
    return res;
}

}  // namespace csa

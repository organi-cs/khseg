#include <gtest/gtest.h>
#include <khseg/dictionary.hpp>
#include <khseg/segmenter.hpp>
#include <khseg/utf8.hpp>

#include <memory>
#include <string>
#include <vector>

using khseg::Algorithm;
using khseg::Dictionary;
using khseg::Options;
using khseg::Segmenter;
using khseg::TokenType;

namespace {

// Real Khmer clusters used as opaque symbols. The tests check algorithm
// behaviour for a given dictionary, not Khmer linguistics.
const std::u32string A = U"ក";               // KA
const std::u32string B = U"ខ";               // KHA
const std::u32string C = U"គ";               // KO
const std::u32string D = U"ឃ";               // KHO
const std::u32string M = U"ម";               // MO
const std::u32string X = U"ម្ពុ";  // MO COENG PO U, one cluster

std::string u8(std::u32string_view s) { return khseg::utf8::encode(s); }

std::shared_ptr<const Dictionary> dict(std::vector<std::pair<std::u32string, double>> words) {
  return std::make_shared<const Dictionary>(Dictionary::from_words(words));
}

struct Piece {
  std::string text;
  TokenType type;
  friend bool operator==(const Piece&, const Piece&) = default;
  friend std::ostream& operator<<(std::ostream& os, const Piece& p) {
    return os << "{" << p.text << ", " << khseg::to_string(p.type) << "}";
  }
};

std::vector<Piece> seg(const Segmenter& s, const std::u32string& text) {
  const std::string in = u8(text);
  std::vector<Piece> out;
  for (const auto& t : s.segment(in)) {
    out.push_back({in.substr(t.byte_begin, t.byte_end - t.byte_begin), t.type});
  }
  return out;
}

Segmenter make(std::shared_ptr<const Dictionary> d, Algorithm a, bool merge = true) {
  Options o;
  o.algorithm = a;
  o.merge_unknown = merge;
  return Segmenter(std::move(d), o);
}

const auto W = TokenType::Word;
const auto U = TokenType::Unknown;

}  // namespace

TEST(Segmenter, NoDictionaryKeepsKhmerRuns) {
  Segmenter s;
  EXPECT_EQ(seg(s, A + B + U" " + C), (std::vector<Piece>{{u8(A + B), TokenType::Khmer},
                                                           {" ", TokenType::Space},
                                                           {u8(C), TokenType::Khmer}}));
}

TEST(Segmenter, ForwardAndBackwardDisagree) {
  // Dictionary {AB, BC, A, C} on ABC: forward takes AB then C, backward
  // takes BC then A.
  auto d = dict({{A + B, 1}, {B + C, 1}, {A, 1}, {C, 1}});
  EXPECT_EQ(seg(make(d, Algorithm::Forward), A + B + C),
            (std::vector<Piece>{{u8(A + B), W}, {u8(C), W}}));
  EXPECT_EQ(seg(make(d, Algorithm::Backward), A + B + C),
            (std::vector<Piece>{{u8(A), W}, {u8(B + C), W}}));
  // Same number of pieces, no unknowns, one single-cluster word each: the
  // bidirectional tie-break prefers backward.
  EXPECT_EQ(seg(make(d, Algorithm::Bidirectional), A + B + C),
            (std::vector<Piece>{{u8(A), W}, {u8(B + C), W}}));
}

TEST(Segmenter, BidirectionalPrefersFewerPieces) {
  // Forward takes the longest word ABC and leaves D unknown. Backward takes
  // CD, then AB. Both have two pieces; backward has no unknowns.
  auto d = dict({{A + B, 1}, {C + D, 1}, {A + B + C, 1}});
  EXPECT_EQ(seg(make(d, Algorithm::Forward, false), A + B + C + D),
            (std::vector<Piece>{{u8(A + B + C), W}, {u8(D), U}}));
  EXPECT_EQ(seg(make(d, Algorithm::Backward), A + B + C + D),
            (std::vector<Piece>{{u8(A + B), W}, {u8(C + D), W}}));
  EXPECT_EQ(seg(make(d, Algorithm::Bidirectional), A + B + C + D),
            (std::vector<Piece>{{u8(A + B), W}, {u8(C + D), W}}));
}

TEST(Segmenter, MatchesMustEndOnClusterBoundary) {
  // MO alone is a word, but inside the cluster MO+COENG+PO+U it cannot match.
  auto d = dict({{M, 1}, {A, 1}});
  for (Algorithm a : {Algorithm::Forward, Algorithm::Backward, Algorithm::Bidirectional}) {
    EXPECT_EQ(seg(make(d, a), A + X), (std::vector<Piece>{{u8(A), W}, {u8(X), U}}))
        << static_cast<int>(a);
  }
}

TEST(Segmenter, UnknownClustersMerge) {
  auto d = dict({{A, 1}});
  for (Algorithm a : {Algorithm::Forward, Algorithm::Backward}) {
    EXPECT_EQ(seg(make(d, a, true), A + D + X + A),
              (std::vector<Piece>{{u8(A), W}, {u8(D + X), U}, {u8(A), W}}));
    EXPECT_EQ(seg(make(d, a, false), A + D + X + A),
              (std::vector<Piece>{{u8(A), W}, {u8(D), U}, {u8(X), U}, {u8(A), W}}));
  }
}

TEST(Segmenter, UnknownsDoNotMergeAcrossRuns) {
  auto d = dict({{A, 1}});
  EXPECT_EQ(seg(make(d, Algorithm::Forward), D + U" " + D),
            (std::vector<Piece>{{u8(D), U}, {" ", TokenType::Space}, {u8(D), U}}));
}

TEST(Segmenter, LekTooPolicy) {
  auto d = dict({{A, 1}});
  const std::u32string text = A + U"ៗ" + U" " + U"ៗ";
  Options o;
  o.algorithm = Algorithm::Forward;
  EXPECT_EQ(seg(Segmenter(d, o), text), (std::vector<Piece>{{u8(A), W},
                                                             {"\xE1\x9F\x97", TokenType::Punct},
                                                             {" ", TokenType::Space},
                                                             {"\xE1\x9F\x97", TokenType::Punct}}));
  o.lektoo = khseg::LekTooPolicy::Attach;
  // Only a lek too directly after a Khmer token attaches.
  EXPECT_EQ(seg(Segmenter(d, o), text), (std::vector<Piece>{{u8(A) + "\xE1\x9F\x97", W},
                                                             {" ", TokenType::Space},
                                                             {"\xE1\x9F\x97", TokenType::Punct}}));
}

TEST(Segmenter, WordTokensCarryEntryIds) {
  auto d = dict({{A, 1}, {B, 1}});
  const auto toks = make(d, Algorithm::Forward).segment(u8(B + A + C));
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].entry, d->find(B));
  EXPECT_EQ(toks[1].entry, d->find(A));
  EXPECT_EQ(toks[2].entry, khseg::kNoEntry);
  EXPECT_EQ(toks[2].byte_begin, 6u);
  EXPECT_EQ(toks[2].byte_end, 9u);
}

TEST(Segmenter, OrphanMarksBecomeUnknown) {
  auto d = dict({{A, 1}});
  EXPECT_EQ(seg(make(d, Algorithm::Forward), U"ា" + A),
            (std::vector<Piece>{{"\xE1\x9E\xB6", U}, {u8(A), W}}));
}

TEST(Segmenter, WordsHelper) {
  auto d = dict({{A, 1}, {B, 1}});
  EXPECT_EQ(make(d, Algorithm::Forward).words(u8(A + B) + " 12"),
            (std::vector<std::string>{u8(A), u8(B), "12"}));
}

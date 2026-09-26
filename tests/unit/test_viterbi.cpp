#include <gtest/gtest.h>
#include <khseg/dictionary.hpp>
#include <khseg/segmenter.hpp>
#include <khseg/utf8.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using khseg::Algorithm;
using khseg::Dictionary;
using khseg::Options;
using khseg::Segmenter;

namespace {

const std::u32string A = U"ក";
const std::u32string B = U"ខ";
const std::u32string C = U"គ";
const std::u32string D = U"ឃ";
const std::u32string X = U"ម្ពុ";

std::string u8(std::u32string_view s) { return khseg::utf8::encode(s); }

// Dictionary with exact costs, written as a logprob TSV.
std::shared_ptr<const Dictionary> costs(std::vector<std::pair<std::u32string, double>> entries) {
  std::string tsv = "# format: logprob\n";
  for (const auto& [w, c] : entries) tsv += u8(w) + "\t" + std::to_string(-c) + "\n";
  std::istringstream in(tsv);
  return std::make_shared<const Dictionary>(Dictionary::from_tsv(in));
}

std::vector<std::string> run(std::shared_ptr<const Dictionary> d, const std::u32string& text,
                             Options o = {}) {
  return Segmenter(std::move(d), o).words(u8(text));
}

std::vector<std::string> words(std::initializer_list<std::u32string> ws) {
  std::vector<std::string> out;
  for (const auto& w : ws) out.push_back(u8(w));
  return out;
}

}  // namespace

TEST(Viterbi, PrefersCheaperPathOverLongestMatch) {
  // AB is rare, A and B are common: two cheap words beat one expensive one.
  auto d = std::make_shared<const Dictionary>(
      Dictionary::from_words({{A + B, 1}, {A, 1000}, {B, 1000}}));
  EXPECT_EQ(run(d, A + B), words({A, B}));
  Options fmm;
  fmm.algorithm = Algorithm::Forward;
  EXPECT_EQ(run(d, A + B, fmm), words({A + B}));
}

TEST(Viterbi, PrefersFrequentCompound) {
  auto d = std::make_shared<const Dictionary>(
      Dictionary::from_words({{A + B, 1000}, {A, 10}, {B, 10}}));
  EXPECT_EQ(run(d, A + B), words({A + B}));
}

TEST(Viterbi, EqualCostPrefersFewerTokens) {
  auto d = costs({{A + B, 2.0}, {A, 1.0}, {B, 1.0}});
  EXPECT_EQ(run(d, A + B), words({A + B}));
}

TEST(Viterbi, FullTiePrefersLongerLastWord) {
  // "AB C" and "A BC" both cost 2 with 2 tokens.
  auto d = costs({{A + B, 1.0}, {C, 1.0}, {A, 1.0}, {B + C, 1.0}});
  EXPECT_EQ(run(d, A + B + C), words({A, B + C}));
}

TEST(Viterbi, UnknownSpanBetweenKnownWords) {
  auto d = costs({{A, 1.0}});
  EXPECT_EQ(run(d, A + D + X + A), words({A, D + X, A}));
  Options o;
  o.merge_unknown = false;
  EXPECT_EQ(run(d, A + D + X + A, o), words({A, D, X, A}));
}

TEST(Viterbi, UnknownCostControlsTradeOff) {
  auto d = costs({{A, 5.0}, {B, 5.0}});
  Options cheap;
  cheap.unknown_cost = 1.0;  // cheaper than any word, so everything is unknown
  EXPECT_EQ(run(d, A + B, cheap), words({A + B}));
  const auto toks = Segmenter(d, cheap).segment(u8(A + B));
  ASSERT_EQ(toks.size(), 1u);
  EXPECT_EQ(toks[0].type, khseg::TokenType::Unknown);
  EXPECT_EQ(run(d, A + B), words({A, B}));  // default: max cost + 1 = 6
}

TEST(Viterbi, DefaultUnknownCostComesFromDictionary) {
  auto d = costs({{A, 5.0}});
  EXPECT_DOUBLE_EQ(Segmenter(d).unknown_cost(), 6.0);
  Options o;
  o.unknown_cost = 3.5;
  EXPECT_DOUBLE_EQ(Segmenter(d, o).unknown_cost(), 3.5);
}

// The worked example from the README, with the same illustrative costs.
TEST(Viterbi, ReadmeWorkedExample) {
  auto d = costs({{U"ខ្ញុំ", 4.1},              // khnhom
                  {U"ស្រលាញ់", 7.9},  // srolanh
                  {U"ប្រទេស", 6.2},        // brates
                  {U"ទេ", 3.0},                                // te
                  {U"ស", 7.5},                                      // sa
                  {U"ក", 8.0},                                      // ka
                  {U"កម្ពុជា", 6.8},  // kampuchea
                  {U"ជា", 3.2}});                              // chea
  Options o;
  o.unknown_cost = 12.0;
  Segmenter seg(d, o);
  const std::u32string text =
      U"ខ្ញុំស្រលាញ់"
      U"ប្រទេសកម្ពុជា";
  khseg::Workspace ws;
  std::vector<khseg::Token> toks;
  seg.segment(u8(text), ws, toks);

  ASSERT_EQ(toks.size(), 4u);
  const std::vector<std::uint32_t> starts{0, 5, 12, 18};
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(toks[i].begin, starts[i]);
    EXPECT_EQ(toks[i].type, khseg::TokenType::Word);
  }
  // best[] at each cluster boundary, as in the README table.
  const std::vector<std::pair<std::uint32_t, double>> table{
      {0, 0.0},   {5, 4.1},   {8, 16.1},  {10, 28.1}, {12, 12.0}, {15, 24.0},
      {17, 27.0}, {18, 18.2}, {19, 26.2}, {23, 38.2}, {25, 25.0}};
  for (const auto& [pos, cost] : table) EXPECT_NEAR(ws.best[pos], cost, 1e-9) << "at " << pos;
}

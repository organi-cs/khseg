// Properties that must hold for every input and every algorithm:
//   1. tokens cover the input with no gaps or overlaps (code points and bytes)
//   2. a boundary inside Khmer text is always a cluster boundary
//   3. Word tokens spell their dictionary entry (after normalizing)
//   4. the same input gives the same output
#include <gtest/gtest.h>
#include <khseg/cluster.hpp>
#include <khseg/dictionary.hpp>
#include <khseg/normalize.hpp>
#include <khseg/segmenter.hpp>
#include <khseg/utf8.hpp>

#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using khseg::Algorithm;
using khseg::Dictionary;
using khseg::Options;
using khseg::Segmenter;
using khseg::TokenType;

namespace {

std::shared_ptr<const Dictionary> sample_dict() {
  static auto d = std::make_shared<const Dictionary>(
      Dictionary::from_tsv_file(KHSEG_SOURCE_DIR "/data/sample/dict.tsv"));
  return d;
}

std::vector<std::string> inputs() {
  std::vector<std::string> out;
  std::ifstream raw(KHSEG_SOURCE_DIR "/data/sample/raw.txt", std::ios::binary);
  for (std::string line; std::getline(raw, line);) out.push_back(line);

  out.push_back("");
  out.push_back(" ");
  out.push_back("\xE1\x9E\xB6");  // orphan vowel sign alone
  out.push_back("abc \xE1\x9E\x80\xE1\x9F\x92 12.5 \xE2\x80\x8B\xF0\x9F\x98\x80 \xE1\x9F\x97");
  out.push_back("bad \xFF\xFE utf8 \xE1\x9E");

  // Random Khmer-heavy strings, including marks in impossible positions.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> pick(0, 99);
  std::uniform_int_distribution<int> khmer(0x1780, 0x17FF);
  for (int n = 0; n < 300; ++n) {
    std::u32string s;
    const int len = pick(rng) % 40;
    for (int i = 0; i < len; ++i) {
      const int r = pick(rng);
      if (r < 80) {
        s.push_back(static_cast<char32_t>(khmer(rng)));
      } else if (r < 85) {
        s.push_back(0x17D2);
      } else if (r < 90) {
        s.push_back(U' ');
      } else if (r < 93) {
        s.push_back(0x200B);
      } else if (r < 96) {
        s.push_back(static_cast<char32_t>('a' + r % 26));
      } else {
        s.push_back(static_cast<char32_t>('0' + r % 10));
      }
    }
    out.push_back(khseg::utf8::encode(s));
  }
  return out;
}

void check(const Segmenter& seg, const std::string& in) {
  khseg::Workspace ws;
  std::vector<khseg::Token> toks;
  seg.segment(in, ws, toks);
  const std::u32string& text = ws.text;

  std::uint32_t pos = 0;
  std::uint32_t byte = 0;
  for (const auto& t : toks) {
    ASSERT_EQ(t.begin, pos) << in;
    ASSERT_EQ(t.byte_begin, byte) << in;
    ASSERT_LT(t.begin, t.end) << in;
    pos = t.end;
    byte = t.byte_end;
    if (khseg::is_khmer_token(t.type)) {
      EXPECT_TRUE(khseg::starts_cluster(text, t.begin)) << in;
      if (t.end < text.size()) {
        EXPECT_TRUE(khseg::starts_cluster(text, t.end)) << in;
      }
    }
    if (t.type == TokenType::Word) {
      ASSERT_NE(t.entry, khseg::kNoEntry);
      const auto span = std::u32string_view(text).substr(t.begin, t.end - t.begin);
      if (seg.options().normalize) {
        EXPECT_EQ(seg.dictionary()->word(t.entry), khseg::normalize(span));
      } else {
        EXPECT_EQ(seg.dictionary()->word(t.entry), span);
      }
    }
  }
  EXPECT_EQ(pos, text.size()) << in;
  EXPECT_EQ(byte, in.size()) << in;

  std::vector<khseg::Token> again;
  seg.segment(in, ws, again);
  EXPECT_EQ(toks, again);
}

class Invariants : public ::testing::TestWithParam<Algorithm> {};

}  // namespace

TEST_P(Invariants, HoldOnAllInputs) {
  for (bool merge : {true, false}) {
    for (bool norm : {true, false}) {
      Options o;
      o.algorithm = GetParam();
      o.merge_unknown = merge;
      o.normalize = norm;
      const Segmenter seg(sample_dict(), o);
      for (const auto& in : inputs()) check(seg, in);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(All, Invariants,
                         ::testing::Values(Algorithm::Viterbi, Algorithm::Forward,
                                           Algorithm::Backward, Algorithm::Bidirectional),
                         [](const auto& p) {
                           switch (p.param) {
                             case Algorithm::Viterbi: return std::string("Viterbi");
                             case Algorithm::Forward: return std::string("Forward");
                             case Algorithm::Backward: return std::string("Backward");
                             case Algorithm::Bidirectional: return std::string("Bidirectional");
                           }
                           return std::string("Unknown");
                         });

TEST(Invariants, LongLine) {
  // One megabyte-scale line with no spaces: the DP must stay linear and must
  // not recurse.
  std::string line;
  std::ifstream raw(KHSEG_SOURCE_DIR "/data/sample/raw.txt", std::ios::binary);
  std::string all;
  for (std::string l; std::getline(raw, l);) all += l;
  while (line.size() < (1u << 20)) line += all;
  const Segmenter seg(sample_dict());
  check(seg, line);
}

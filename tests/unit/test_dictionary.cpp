#include <gtest/gtest.h>
#include <khseg/dictionary.hpp>
#include <khseg/token.hpp>
#include <khseg/trie.hpp>
#include <khseg/utf8.hpp>

#include <cmath>
#include <sstream>

using khseg::Dictionary;
using khseg::LoadReport;
using khseg::RefTrie;

TEST(RefTrie, InsertFindStep) {
  RefTrie t;
  EXPECT_TRUE(t.insert(U"ab", 1));
  EXPECT_TRUE(t.insert(U"abc", 2));
  EXPECT_TRUE(t.insert(U"b", 3));
  EXPECT_FALSE(t.insert(U"", 4));
  EXPECT_EQ(t.find(U"ab"), 1u);
  EXPECT_EQ(t.find(U"abc"), 2u);
  EXPECT_EQ(t.find(U"b"), 3u);
  EXPECT_EQ(t.find(U"a"), RefTrie::kNone);
  EXPECT_EQ(t.find(U"abcd"), RefTrie::kNone);
  EXPECT_EQ(t.find(U""), RefTrie::kNone);

  const auto a = t.step(RefTrie::kRoot, U'a');
  ASSERT_NE(a, RefTrie::kNone);
  EXPECT_EQ(t.value(a), RefTrie::kNone);
  EXPECT_EQ(t.step(a, U'z'), RefTrie::kNone);

  EXPECT_TRUE(t.insert(U"ab", 9));
  EXPECT_EQ(t.find(U"ab"), 9u);
  EXPECT_EQ(t.node_count(), 5u);  // root, a, ab, abc, b
}

namespace {

Dictionary load(const std::string& tsv, LoadReport* r = nullptr) {
  std::istringstream in(tsv);
  return Dictionary::from_tsv(in, r);
}

std::string u8(std::u32string_view s) { return khseg::utf8::encode(s); }

const std::u32string kKa = U"ក";
const std::u32string kKha = U"ខ";
const std::u32string kKhmer = U"ខ្មែរ";

}  // namespace

TEST(Dictionary, CostsFromCounts) {
  const Dictionary d = load(u8(kKa) + "\t3\n" + u8(kKha) + "\t1\n");
  ASSERT_EQ(d.size(), 2u);
  EXPECT_DOUBLE_EQ(d.total_count(), 4.0);
  // alpha = 0.5: denominator 4 + 0.5 * 2 = 5
  EXPECT_NEAR(d.cost(d.find(kKa)), -std::log(3.5 / 5), 1e-12);
  EXPECT_NEAR(d.cost(d.find(kKha)), -std::log(1.5 / 5), 1e-12);
  EXPECT_NEAR(d.max_cost(), -std::log(1.5 / 5), 1e-12);
  EXPECT_NEAR(d.default_unknown_cost(), d.max_cost() + 1.0, 1e-12);
  EXPECT_EQ(d.find(kKhmer), khseg::kNoEntry);
}

TEST(Dictionary, CommentsBomCrlfAndMissingCounts) {
  LoadReport r;
  const Dictionary d = load("\xEF\xBB\xBF# a comment\r\n\r\n" + u8(kKa) + "\r\n" + u8(kKha) +
                                "\t2\r\n   \n",
                            &r);
  EXPECT_EQ(d.size(), 2u);
  EXPECT_EQ(d.count(d.find(kKa)), 0.0);
  EXPECT_EQ(d.count(d.find(kKha)), 2.0);
  EXPECT_EQ(r.lines, 5u);
  EXPECT_EQ(r.entries, 2u);
  EXPECT_EQ(r.rejected, 0u);
  EXPECT_TRUE(r.problems.empty());
}

TEST(Dictionary, DuplicatesAreSummed) {
  LoadReport r;
  const Dictionary d = load(u8(kKa) + "\t3\n" + u8(kKa) + "\t4\n" + u8(kKa) + "\n", &r);
  EXPECT_EQ(d.size(), 1u);
  EXPECT_EQ(d.count(0), 7.0);
  EXPECT_EQ(r.duplicates, 2u);
}

TEST(Dictionary, Rejections) {
  LoadReport r;
  const Dictionary d = load(std::string("abc\t1\n") +              // Latin
                                "\xE1\x9E\xB6\xE1\x9E\x80\t1\n" +  // starts with vowel sign AA
                                u8(kKa) + "\tlots\n" +             // bad number
                                u8(kKha) + "\t-2\n" +              // negative
                                "\xE1\x9E\t1\n" +                  // invalid UTF-8
                                u8(kKhmer) + "\t5\n",
                            &r);
  EXPECT_EQ(d.size(), 1u);
  EXPECT_NE(d.find(kKhmer), khseg::kNoEntry);
  EXPECT_EQ(r.rejected, 5u);
  ASSERT_EQ(r.problems.size(), 5u);
  EXPECT_EQ(r.problems[0].line, 1u);
  EXPECT_EQ(r.problems[0].message, "contains non-Khmer character U+0061");
  EXPECT_EQ(r.problems[1].message, "starts with a mark");
  EXPECT_EQ(r.problems[2].message, "bad number 'lots'");
  EXPECT_EQ(r.problems[3].message, "negative count");
  EXPECT_EQ(r.problems[4].message, "invalid UTF-8");
}

TEST(Dictionary, NonCanonicalOrderIsKeptWithWarning) {
  LoadReport r;
  // KHA, AE, COENG, MO, RO: vowel typed before the subscript.
  const Dictionary d = load(u8(U"ខែ្មរ") + "\t1\n", &r);
  EXPECT_EQ(d.size(), 1u);
  EXPECT_EQ(r.warnings, 1u);
  ASSERT_EQ(r.problems.size(), 1u);
  EXPECT_EQ(r.problems[0].message, "kept, but cluster check found out-of-order at offset 2");
}

TEST(Dictionary, LogProbFormatAndUnknownCost) {
  const Dictionary d = load("# format: logprob\n# unknown-cost: 20.5\n" + u8(kKa) + "\t-2.5\n" +
                            u8(kKha) + "\t-7\n" + u8(kKhmer) + "\n");
  EXPECT_EQ(d.value_format(), Dictionary::ValueFormat::LogProb);
  EXPECT_DOUBLE_EQ(d.cost(d.find(kKa)), 2.5);
  EXPECT_DOUBLE_EQ(d.cost(d.find(kKha)), 7.0);
  EXPECT_DOUBLE_EQ(d.cost(d.find(kKhmer)), 7.0);  // missing value: worst known cost
  EXPECT_DOUBLE_EQ(d.default_unknown_cost(), 20.5);
}

TEST(Dictionary, LogProbAboveZeroRejected) {
  LoadReport r;
  const Dictionary d = load("# format: logprob\n" + u8(kKa) + "\t0.5\n", &r);
  EXPECT_EQ(d.size(), 0u);
  EXPECT_EQ(r.rejected, 1u);
}

TEST(Dictionary, BackwardTrieHoldsReversedWords) {
  const Dictionary d = Dictionary::from_words({{kKhmer, 1.0}});
  const std::u32string rev(kKhmer.rbegin(), kKhmer.rend());
  EXPECT_EQ(d.backward().find(rev), 0u);
  EXPECT_EQ(d.max_word_length(), 5u);
}

TEST(Dictionary, MissingFileThrows) {
  EXPECT_THROW(Dictionary::from_tsv_file("no/such/file.tsv"), std::runtime_error);
}

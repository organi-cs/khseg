#include <gtest/gtest.h>
#include <khseg/dictionary.hpp>
#include <khseg/eval.hpp>

#include <string>
#include <vector>

using khseg::EvalOptions;
using khseg::SentenceScore;
using khseg::split_words;

TEST(Eval, SplitAndJoin) {
  const auto w = split_words("  ab \tc  de\r");
  ASSERT_EQ(w.size(), 3u);
  EXPECT_EQ(w[0], "ab");
  EXPECT_EQ(w[2], "de");
  EXPECT_EQ(khseg::join_words(w), "abcde");
  EXPECT_TRUE(split_words("   ").empty());
}

TEST(Eval, HandComputedScores) {
  // gold ab|c|de, pred a|bc|de: only "de" matches.
  SentenceScore s;
  ASSERT_TRUE(khseg::score_sentence(split_words("ab c de"), split_words("a bc de"), {}, s));
  EXPECT_EQ(s.words.gold, 3u);
  EXPECT_EQ(s.words.pred, 3u);
  EXPECT_EQ(s.words.correct, 1u);
  EXPECT_NEAR(s.words.f1(), 1.0 / 3.0, 1e-12);
  // Boundaries: gold {2, 3}, pred {1, 3}.
  EXPECT_EQ(s.boundaries.gold, 2u);
  EXPECT_EQ(s.boundaries.pred, 2u);
  EXPECT_EQ(s.boundaries.correct, 1u);
  EXPECT_FALSE(s.exact);
}

TEST(Eval, PerfectAndUnequalCounts) {
  SentenceScore s;
  ASSERT_TRUE(khseg::score_sentence(split_words("ab c"), split_words("ab c"), {}, s));
  EXPECT_TRUE(s.exact);
  EXPECT_DOUBLE_EQ(s.words.f1(), 1.0);

  ASSERT_TRUE(khseg::score_sentence(split_words("abcd"), split_words("a b c d"), {}, s));
  EXPECT_EQ(s.words.correct, 0u);
  EXPECT_EQ(s.words.pred, 4u);
  EXPECT_EQ(s.boundaries.gold, 0u);
  EXPECT_EQ(s.boundaries.pred, 3u);
}

TEST(Eval, TextMismatchIsRejected) {
  SentenceScore s;
  s.words.gold = 99;
  EXPECT_FALSE(khseg::score_sentence(split_words("ab c"), split_words("ab d"), {}, s));
  EXPECT_EQ(s.words.gold, 99u);
}

TEST(Eval, IgnorePunct) {
  EvalOptions o;
  o.ignore_punct = true;
  SentenceScore s;
  // KHAN (U+17D4) and "!" are punctuation; "a" is not.
  ASSERT_TRUE(khseg::score_sentence(split_words("a \xE1\x9F\x94 !"),
                                    split_words("a \xE1\x9F\x94!"), o, s));
  EXPECT_EQ(s.words.gold, 1u);
  EXPECT_EQ(s.words.pred, 1u);
  EXPECT_EQ(s.words.correct, 1u);
}

TEST(Eval, VocabularySplit) {
  const auto d = khseg::Dictionary::from_words({{U"ក", 1}});
  EvalOptions o;
  o.dictionary = &d;
  SentenceScore s;
  // gold: KA (known) | KHA KO (unknown) | 12 (not Khmer, not counted)
  // pred: KA | KHA | KO 12
  ASSERT_TRUE(khseg::score_sentence(
      split_words("\xE1\x9E\x80 \xE1\x9E\x81\xE1\x9E\x82 12"),
      split_words("\xE1\x9E\x80 \xE1\x9E\x81 \xE1\x9E\x82" "12"), o, s));
  EXPECT_EQ(s.iv_gold, 1u);
  EXPECT_EQ(s.iv_correct, 1u);
  EXPECT_EQ(s.oov_gold, 1u);
  EXPECT_EQ(s.oov_correct, 0u);
}

TEST(Eval, SummaryAndBootstrap) {
  std::vector<SentenceScore> scores;
  khseg::EvalSummary sum;
  for (int i = 0; i < 50; ++i) {
    SentenceScore s;
    khseg::score_sentence(split_words(i % 3 ? "ab c de" : "abc de"), split_words("ab c de"), {}, s);
    scores.push_back(s);
    sum.add(s);
  }
  EXPECT_EQ(sum.sentences, 50u);
  const double f1 = sum.words.f1();
  const auto ci = khseg::bootstrap_f1(scores, 500, 7);
  EXPECT_LE(ci.low, f1);
  EXPECT_GE(ci.high, f1);
  const auto again = khseg::bootstrap_f1(scores, 500, 7);
  EXPECT_EQ(ci.low, again.low);
  EXPECT_EQ(ci.high, again.high);

  // Comparing a system with itself: no difference, p = 1.
  const auto pr = khseg::paired_bootstrap(scores, scores, 200, 7);
  EXPECT_EQ(pr.delta, 0.0);
  EXPECT_EQ(pr.p_value, 1.0);
}

#pragma once

#include <khseg/export.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace khseg {

class Dictionary;

// Word-level evaluation against a gold segmentation. Gold and predicted
// lines are both "words separated by spaces". A word counts as correct when
// its [start, end) span over the unsegmented text matches a gold word exactly.

struct Counts {
  std::uint64_t gold = 0;     // gold words
  std::uint64_t pred = 0;     // predicted words
  std::uint64_t correct = 0;  // predicted words that match a gold word

  Counts& operator+=(const Counts& o) {
    gold += o.gold;
    pred += o.pred;
    correct += o.correct;
    return *this;
  }
  double precision() const { return pred ? static_cast<double>(correct) / static_cast<double>(pred) : 0.0; }
  double recall() const { return gold ? static_cast<double>(correct) / static_cast<double>(gold) : 0.0; }
  double f1() const {
    const double p = precision(), r = recall();
    return p + r > 0 ? 2 * p * r / (p + r) : 0.0;
  }
};

struct SentenceScore {
  Counts words;
  Counts boundaries;  // word boundaries strictly inside the sentence
  bool exact = false;
  // Recall split by whether the gold word is in the dictionary. Only Khmer
  // words are counted here; numbers, Latin and punctuation are left out.
  std::uint64_t iv_gold = 0, iv_correct = 0;
  std::uint64_t oov_gold = 0, oov_correct = 0;
};

struct EvalOptions {
  // Leave punctuation-only words out of both sides.
  bool ignore_punct = false;
  // Used for the in-vocabulary / out-of-vocabulary split; may be null.
  const Dictionary* dictionary = nullptr;
};

// Splits a gold or predicted line on ASCII spaces and tabs.
KHSEG_EXPORT std::vector<std::string_view> split_words(std::string_view line);

// The gold line with separators removed, i.e. the text to segment.
KHSEG_EXPORT std::string join_words(const std::vector<std::string_view>& words);

// Scores one sentence. Returns false (and leaves `out` untouched) when the
// two sides do not spell the same text, which means they cannot be compared.
KHSEG_EXPORT bool score_sentence(const std::vector<std::string_view>& gold,
                                 const std::vector<std::string_view>& pred,
                                 const EvalOptions& options, SentenceScore& out);

struct KHSEG_EXPORT EvalSummary {
  Counts words;
  Counts boundaries;
  std::uint64_t sentences = 0;
  std::uint64_t exact = 0;
  std::uint64_t skipped = 0;  // lines where gold and prediction spell different text
  std::uint64_t iv_gold = 0, iv_correct = 0, oov_gold = 0, oov_correct = 0;

  void add(const SentenceScore& s);
  double oov_rate() const;
  double oov_recall() const;
  double iv_recall() const;
  double exact_rate() const;
};

struct Interval {
  double low = 0, high = 0;
};

// Percentile bootstrap over sentences for word F1. Deterministic for a seed.
KHSEG_EXPORT Interval bootstrap_f1(const std::vector<SentenceScore>& scores, int samples,
                                   std::uint64_t seed, double confidence = 0.95);

struct PairedResult {
  double delta = 0;    // F1(a) - F1(b) on the full set
  Interval interval;   // bootstrap interval for the difference
  double p_value = 0;  // share of resamples where the sign of delta flips (or ties)
};

// Paired bootstrap comparing two systems scored on the same sentences.
KHSEG_EXPORT PairedResult paired_bootstrap(const std::vector<SentenceScore>& a,
                                           const std::vector<SentenceScore>& b, int samples,
                                           std::uint64_t seed, double confidence = 0.95);

}  // namespace khseg

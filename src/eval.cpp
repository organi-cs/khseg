#include <khseg/charclass.hpp>
#include <khseg/dictionary.hpp>
#include <khseg/eval.hpp>
#include <khseg/pretokenize.hpp>
#include <khseg/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace khseg {

namespace {

struct Span {
  std::uint32_t begin, end;
  std::string_view word;
};

std::vector<Span> spans_of(const std::vector<std::string_view>& words) {
  std::vector<Span> out;
  out.reserve(words.size());
  std::uint32_t pos = 0;
  for (auto w : words) {
    const auto end = pos + static_cast<std::uint32_t>(w.size());
    out.push_back({pos, end, w});
    pos = end;
  }
  return out;
}

bool is_punct_word(std::string_view w) {
  const std::u32string text = utf8::decode(w);
  std::vector<Token> toks;
  pretokenize(text, toks);
  return !toks.empty() && std::all_of(toks.begin(), toks.end(),
                                      [](const Token& t) { return t.type == TokenType::Punct; });
}

// Number of spans present in both sorted lists.
template <class Keep>
std::uint64_t matches(const std::vector<Span>& a, const std::vector<Span>& b, Keep keep,
                      std::vector<std::uint8_t>* matched_a = nullptr) {
  std::uint64_t n = 0;
  std::size_t j = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    while (j < b.size() && b[j].begin < a[i].begin) ++j;
    if (j < b.size() && b[j].begin == a[i].begin && b[j].end == a[i].end && keep(a[i])) {
      ++n;
      if (matched_a) (*matched_a)[i] = 1;
    }
  }
  return n;
}

}  // namespace

std::vector<std::string_view> split_words(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
    const std::size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

std::string join_words(const std::vector<std::string_view>& words) {
  std::string out;
  for (auto w : words) out.append(w);
  return out;
}

bool score_sentence(const std::vector<std::string_view>& gold,
                    const std::vector<std::string_view>& pred, const EvalOptions& options,
                    SentenceScore& out) {
  if (join_words(gold) != join_words(pred)) return false;
  const auto g = spans_of(gold);
  const auto p = spans_of(pred);

  auto keep = [&options](const Span& s) { return !options.ignore_punct || !is_punct_word(s.word); };
  SentenceScore s;
  s.words.gold = static_cast<std::uint64_t>(std::count_if(g.begin(), g.end(), keep));
  s.words.pred = static_cast<std::uint64_t>(std::count_if(p.begin(), p.end(), keep));
  std::vector<std::uint8_t> matched(g.size(), 0);
  s.words.correct = matches(g, p, keep, &matched);

  // Internal boundaries: every word end except the last.
  std::vector<std::uint32_t> gb, pb;
  for (std::size_t i = 0; i + 1 < g.size(); ++i) gb.push_back(g[i].end);
  for (std::size_t i = 0; i + 1 < p.size(); ++i) pb.push_back(p[i].end);
  s.boundaries.gold = gb.size();
  s.boundaries.pred = pb.size();
  std::vector<std::uint32_t> common;
  std::set_intersection(gb.begin(), gb.end(), pb.begin(), pb.end(), std::back_inserter(common));
  s.boundaries.correct = common.size();

  s.exact = g.size() == p.size() &&
            std::equal(g.begin(), g.end(), p.begin(),
                       [](const Span& a, const Span& b) { return a.begin == b.begin && a.end == b.end; });

  if (options.dictionary) {
    for (std::size_t i = 0; i < g.size(); ++i) {
      const std::u32string w = utf8::decode(g[i].word);
      if (w.empty() || !is_khmer_letter(classify(w.front()))) continue;
      const bool known = options.dictionary->find(w) != kNoEntry;
      (known ? s.iv_gold : s.oov_gold) += 1;
      if (matched[i]) (known ? s.iv_correct : s.oov_correct) += 1;
    }
  }
  out = s;
  return true;
}

void EvalSummary::add(const SentenceScore& s) {
  words += s.words;
  boundaries += s.boundaries;
  ++sentences;
  exact += s.exact ? 1 : 0;
  iv_gold += s.iv_gold;
  iv_correct += s.iv_correct;
  oov_gold += s.oov_gold;
  oov_correct += s.oov_correct;
}

namespace {
double ratio(std::uint64_t a, std::uint64_t b) {
  return b ? static_cast<double>(a) / static_cast<double>(b) : 0.0;
}
}  // namespace

double EvalSummary::oov_rate() const { return ratio(oov_gold, oov_gold + iv_gold); }
double EvalSummary::oov_recall() const { return ratio(oov_correct, oov_gold); }
double EvalSummary::iv_recall() const { return ratio(iv_correct, iv_gold); }
double EvalSummary::exact_rate() const { return ratio(exact, sentences); }

namespace {

// Uniform index in [0, n). std::uniform_int_distribution is implemented
// differently by each standard library, which would make published
// intervals depend on the compiler; this does not. The modulo bias is below
// n / 2^64 and does not matter here.
std::size_t draw(std::mt19937_64& rng, std::size_t n) {
  return static_cast<std::size_t>(rng() % n);
}

Interval percentile_interval(std::vector<double>& v, double confidence) {
  if (v.empty()) return {};
  std::sort(v.begin(), v.end());
  const double tail = (1.0 - confidence) / 2.0;
  auto at = [&v](double q) {
    const auto idx = static_cast<std::size_t>(std::floor(q * static_cast<double>(v.size() - 1) + 0.5));
    return v[std::min(idx, v.size() - 1)];
  };
  return {at(tail), at(1.0 - tail)};
}

}  // namespace

Interval bootstrap_f1(const std::vector<SentenceScore>& scores, int samples, std::uint64_t seed,
                      double confidence) {
  if (scores.empty() || samples <= 0) return {};
  std::mt19937_64 rng(seed);
  std::vector<double> f1s;
  f1s.reserve(static_cast<std::size_t>(samples));
  for (int s = 0; s < samples; ++s) {
    Counts c;
    for (std::size_t i = 0; i < scores.size(); ++i) c += scores[draw(rng, scores.size())].words;
    f1s.push_back(c.f1());
  }
  return percentile_interval(f1s, confidence);
}

PairedResult paired_bootstrap(const std::vector<SentenceScore>& a,
                              const std::vector<SentenceScore>& b, int samples,
                              std::uint64_t seed, double confidence) {
  PairedResult r;
  if (a.empty() || a.size() != b.size() || samples <= 0) return r;
  Counts ta, tb;
  for (std::size_t i = 0; i < a.size(); ++i) {
    ta += a[i].words;
    tb += b[i].words;
  }
  r.delta = ta.f1() - tb.f1();

  std::mt19937_64 rng(seed);
  std::vector<double> deltas;
  deltas.reserve(static_cast<std::size_t>(samples));
  int flips = 0;
  for (int s = 0; s < samples; ++s) {
    Counts ca, cb;
    for (std::size_t i = 0; i < a.size(); ++i) {
      const std::size_t k = draw(rng, a.size());
      ca += a[k].words;
      cb += b[k].words;
    }
    const double d = ca.f1() - cb.f1();
    deltas.push_back(d);
    if ((r.delta > 0 && d <= 0) || (r.delta < 0 && d >= 0) || r.delta == 0) ++flips;
  }
  r.p_value = static_cast<double>(flips) / samples;
  r.interval = percentile_interval(deltas, confidence);
  return r;
}

}  // namespace khseg

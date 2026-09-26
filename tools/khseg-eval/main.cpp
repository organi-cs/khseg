#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../common/cli_io.hpp"
#include "../common/dict_loader.hpp"

namespace {

enum Exit : int { kOk = 0, kUsage = 1, kIoError = 2, kTooManySkipped = 4 };

bool read_lines(const std::string& path, std::vector<std::string>& out) {
  std::FILE* f = khseg::cli::open_read(path);
  if (f == nullptr) return false;
  khseg::cli::LineReader reader(f);
  std::string line;
  std::string_view ending;
  bool first = true;
  while (reader.next(line, ending)) {
    if (first) khseg::cli::strip_bom(line);
    first = false;
    out.push_back(line);
  }
  const bool ok = !reader.error();
  std::fclose(f);
  return ok;
}

struct Run {
  khseg::EvalSummary summary;
  std::vector<khseg::SentenceScore> scores;  // one per gold line; skipped lines score zero
  std::vector<std::size_t> skipped_lines;
};

// Predicted lines: from a file, or produced by the segmenter from the gold text.
std::vector<std::string> predict(const std::vector<std::string>& gold, const khseg::Segmenter& seg) {
  std::vector<std::string> out;
  out.reserve(gold.size());
  khseg::Workspace ws;
  std::vector<khseg::Token> toks;
  for (const auto& line : gold) {
    const std::string raw = khseg::join_words(khseg::split_words(line));
    seg.segment(raw, ws, toks);
    std::string pred;
    khseg::write_separated(raw, toks, " ", pred);
    out.push_back(std::move(pred));
  }
  return out;
}

Run score(const std::vector<std::string>& gold, const std::vector<std::string>& pred,
          const khseg::EvalOptions& opts) {
  Run r;
  r.scores.resize(gold.size());
  for (std::size_t i = 0; i < gold.size(); ++i) {
    const auto g = khseg::split_words(gold[i]);
    const auto p = khseg::split_words(i < pred.size() ? pred[i] : std::string_view{});
    if (g.empty() && p.empty()) continue;
    if (!khseg::score_sentence(g, p, opts, r.scores[i])) {
      r.skipped_lines.push_back(i + 1);
      ++r.summary.skipped;
      continue;
    }
    r.summary.add(r.scores[i]);
  }
  return r;
}

std::string pct(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f", v * 100.0);
  return buf;
}

void dump_errors(const std::vector<std::string>& gold, const std::vector<std::string>& pred,
                 const Run& run, int limit) {
  int shown = 0;
  for (std::size_t i = 0; i < gold.size() && shown < limit; ++i) {
    const auto g = khseg::split_words(gold[i]);
    const auto p = khseg::split_words(pred[i]);
    if (run.scores[i].exact || (g.empty() && p.empty())) continue;
    // Bracket the words that have no exact counterpart on the other side.
    auto mark = [](const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
      std::vector<std::pair<std::size_t, std::size_t>> spans_b;
      std::size_t pos = 0;
      for (auto w : b) {
        spans_b.emplace_back(pos, pos + w.size());
        pos += w.size();
      }
      std::string out;
      pos = 0;
      for (auto w : a) {
        const std::pair<std::size_t, std::size_t> s{pos, pos + w.size()};
        const bool ok = std::find(spans_b.begin(), spans_b.end(), s) != spans_b.end();
        if (!out.empty()) out += ' ';
        out += ok ? std::string(w) : "[" + std::string(w) + "]";
        pos += w.size();
      }
      return out;
    };
    std::cout << "line " << i + 1 << "\n  gold: " << mark(g, p) << "\n  pred: " << mark(p, g)
              << "\n";
    ++shown;
  }
}

bool parse_sweep(const std::string& spec, std::string& key, double& from, double& to, double& step) {
  const auto eq = spec.find('=');
  if (eq == std::string::npos) return false;
  key = spec.substr(0, eq);
  return std::sscanf(spec.c_str() + eq + 1, "%lf:%lf:%lf", &from, &to, &step) == 3 && step > 0 &&
         from <= to;
}

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();
  CLI::App app{"khseg-eval: word-level precision, recall and F1 against a gold segmentation"};
  app.set_version_flag("--version", std::string("khseg-eval ") + khseg::version());

  std::string gold_path, pred_path, compare_path, dict_path, sweep, tsv_name;
  khseg::Options seg_opts;
  std::optional<double> unk_cost;
  double alpha = 0.5;
  bool no_merge = false, ignore_punct = false, verbose = false;
  int dump = 0, samples = 1000;
  std::uint64_t seed = 42;

  app.add_option("-g,--gold", gold_path, "Gold file: one sentence per line, words separated by spaces")
      ->required();
  app.add_option("-p,--pred", pred_path, "Predicted file in the same format (default: run khseg)");
  app.add_option("--compare", compare_path, "Second predicted file for a paired bootstrap test");
  app.add_option("-d,--dict", dict_path, "Dictionary TSV");
  app.add_option("-a,--algo", seg_opts.algorithm, "Segmentation algorithm")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::Algorithm>{{"viterbi", khseg::Algorithm::Viterbi},
                                                  {"fmm", khseg::Algorithm::Forward},
                                                  {"bmm", khseg::Algorithm::Backward},
                                                  {"bimm", khseg::Algorithm::Bidirectional}},
          CLI::ignore_case));
  app.add_option("--unk-cost", unk_cost, "Cost of one unknown cluster")->check(CLI::PositiveNumber);
  app.add_option("--alpha", alpha, "Smoothing constant for dictionary counts")
      ->check(CLI::PositiveNumber);
  app.add_flag("--no-merge-unknown", no_merge, "Keep unknown clusters as separate tokens");
  app.add_option("--lektoo", seg_opts.lektoo, "Handling of U+17D7")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::LekTooPolicy>{{"separate", khseg::LekTooPolicy::Separate},
                                                     {"attach", khseg::LekTooPolicy::Attach}},
          CLI::ignore_case));
  app.add_flag("--ignore-punct", ignore_punct, "Leave punctuation-only words out of the scores");
  app.add_option("--dump-errors", dump, "Print up to N sentences that differ from gold");
  app.add_option("--bootstrap", samples, "Bootstrap resamples for confidence intervals (0 = off)");
  app.add_option("--seed", seed, "Random seed for the bootstrap");
  app.add_option("--sweep", sweep, "Grid search, e.g. unk-cost=8:20:0.5 or alpha=0.1:1:0.1");
  app.add_option("--tsv", tsv_name, "Also print one tab-separated summary row labelled NAME");
  app.add_flag("-v,--verbose", verbose, "Report dictionary problems");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    const int rc = app.exit(e);
    return rc == 0 ? kOk : kUsage;
  }
  seg_opts.unknown_cost = unk_cost;
  seg_opts.merge_unknown = !no_merge;

  std::vector<std::string> gold;
  if (!read_lines(gold_path, gold)) {
    std::cerr << "khseg-eval: cannot read " << gold_path << "\n";
    return kIoError;
  }

  std::shared_ptr<const khseg::Dictionary> dict;
  const bool need_segmenter = pred_path.empty() || !sweep.empty();
  try {
    if (need_segmenter || !dict_path.empty()) {
      dict = khseg::cli::load_dictionary("khseg-eval", dict_path, verbose, alpha);
    }
  } catch (const std::exception& e) {
    std::cerr << "khseg-eval: " << e.what() << "\n";
    return kIoError;
  }
  if (need_segmenter && !dict) {
    std::cerr << "khseg-eval: no --pred file and no dictionary to segment with\n";
    return kUsage;
  }

  khseg::EvalOptions eval_opts;
  eval_opts.ignore_punct = ignore_punct;
  eval_opts.dictionary = dict.get();

  if (!sweep.empty()) {
    std::string key;
    double from = 0, to = 0, step = 0;
    if (!parse_sweep(sweep, key, from, to, step) || (key != "unk-cost" && key != "alpha")) {
      std::cerr << "khseg-eval: --sweep expects unk-cost=FROM:TO:STEP or alpha=FROM:TO:STEP\n";
      return kUsage;
    }
    std::printf("%-10s %8s %8s %8s\n", key.c_str(), "P", "R", "F1");
    double best_v = from, best_f1 = -1;
    const int steps = static_cast<int>((to - from) / step + 1e-9);
    for (int i = 0; i <= steps; ++i) {
      const double v = from + i * step;
      khseg::Options o = seg_opts;
      std::shared_ptr<const khseg::Dictionary> d = dict;
      if (key == "unk-cost") {
        o.unknown_cost = v;
      } else {
        d = khseg::cli::load_dictionary("khseg-eval", dict_path, false, v);
      }
      const Run r = score(gold, predict(gold, khseg::Segmenter(d, o)), eval_opts);
      const auto& w = r.summary.words;
      std::printf("%-10.3f %8s %8s %8s\n", v, pct(w.precision()).c_str(), pct(w.recall()).c_str(),
                  pct(w.f1()).c_str());
      if (w.f1() > best_f1) {
        best_f1 = w.f1();
        best_v = v;
      }
    }
    std::printf("best %s = %.3f (F1 %s)\n", key.c_str(), best_v, pct(best_f1).c_str());
    return kOk;
  }

  std::vector<std::string> pred;
  if (!pred_path.empty()) {
    if (!read_lines(pred_path, pred)) {
      std::cerr << "khseg-eval: cannot read " << pred_path << "\n";
      return kIoError;
    }
  } else {
    pred = predict(gold, khseg::Segmenter(dict, seg_opts));
  }
  if (pred.size() != gold.size()) {
    std::cerr << "khseg-eval: gold has " << gold.size() << " lines, prediction has " << pred.size()
              << "\n";
    return kIoError;
  }

  const Run run = score(gold, pred, eval_opts);
  const auto& s = run.summary;

  std::cout << "sentences    " << s.sentences << " (skipped " << s.skipped << ")\n";
  std::cout << "words        P " << pct(s.words.precision()) << "  R " << pct(s.words.recall())
            << "  F1 " << pct(s.words.f1());
  if (samples > 0) {
    const auto ci = khseg::bootstrap_f1(run.scores, samples, seed);
    std::cout << "  (95% CI " << pct(ci.low) << " to " << pct(ci.high) << ")";
  }
  std::cout << "\n";
  std::cout << "             gold " << s.words.gold << ", predicted " << s.words.pred << ", correct "
            << s.words.correct << "\n";
  std::cout << "boundaries   P " << pct(s.boundaries.precision()) << "  R "
            << pct(s.boundaries.recall()) << "  F1 " << pct(s.boundaries.f1()) << "\n";
  std::cout << "exact match  " << pct(s.exact_rate()) << "%\n";
  if (dict) {
    std::cout << "vocabulary   OOV rate " << pct(s.oov_rate()) << "%  OOV recall "
              << pct(s.oov_recall()) << "%  IV recall " << pct(s.iv_recall()) << "%\n";
  }

  if (!compare_path.empty()) {
    std::vector<std::string> other;
    if (!read_lines(compare_path, other) || other.size() != gold.size()) {
      std::cerr << "khseg-eval: cannot read " << compare_path << " or line count differs\n";
      return kIoError;
    }
    const Run r2 = score(gold, other, eval_opts);
    const int n = samples > 0 ? samples : 1000;
    const auto pr = khseg::paired_bootstrap(run.scores, r2.scores, n, seed);
    std::cout << "compare      F1 " << pct(r2.summary.words.f1()) << " in " << compare_path
              << "\n             difference " << pct(pr.delta) << " points (95% CI "
              << pct(pr.interval.low) << " to " << pct(pr.interval.high) << "), ";
    if (pr.p_value > 0) {
      std::cout << "p = " << pr.p_value << "\n";
    } else {
      std::cout << "p < " << 1.0 / n << "\n";
    }
  }

  if (!tsv_name.empty()) {
    // name, P, R, F1, F1 CI low, F1 CI high, boundary F1, exact match, OOV rate, OOV recall
    const auto ci = samples > 0 ? khseg::bootstrap_f1(run.scores, samples, seed) : khseg::Interval{};
    std::cout << tsv_name << '\t' << pct(s.words.precision()) << '\t' << pct(s.words.recall())
              << '\t' << pct(s.words.f1()) << '\t' << pct(ci.low) << '\t' << pct(ci.high) << '\t'
              << pct(s.boundaries.f1()) << '\t' << pct(s.exact_rate()) << '\t'
              << pct(s.oov_rate()) << '\t' << pct(s.oov_recall()) << "\n";
  }

  if (dump > 0) dump_errors(gold, pred, run, dump);

  if (!run.skipped_lines.empty()) {
    std::cerr << "khseg-eval: " << run.skipped_lines.size()
              << " lines spell different text in gold and prediction, first at line "
              << run.skipped_lines.front() << "\n";
    if (static_cast<double>(run.skipped_lines.size()) > 0.001 * static_cast<double>(gold.size())) {
      return kTooManySkipped;
    }
  }
  return kOk;
}

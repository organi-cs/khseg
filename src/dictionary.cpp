#include <khseg/charclass.hpp>
#include <khseg/cluster.hpp>
#include <khseg/dictionary.hpp>
#include <khseg/token.hpp>
#include <khseg/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <istream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace khseg {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Locale-independent number parsing. std::from_chars for double is not
// available in every standard library we build with.
std::optional<double> parse_double(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::istringstream in{std::string(s)};
  in.imbue(std::locale::classic());
  double v = 0;
  in >> v;
  if (in.fail() || !in.eof() || !std::isfinite(v)) return std::nullopt;
  return v;
}

std::string hex_cp(char32_t cp) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
  return buf;
}

// Why a word cannot go in the dictionary, or empty if it can.
std::string reject_reason(std::u32string_view w) {
  if (w.empty()) return "empty word";
  for (char32_t cp : w) {
    const CharClass c = classify(cp);
    if (!is_khmer_letter(c) && c != CharClass::Joiner) {
      return "contains non-Khmer character " + hex_cp(cp);
    }
  }
  if (!is_base(classify(w.front()))) return "starts with a mark";
  return {};
}

}  // namespace

struct Dictionary::Builder {
  Builder(DictionaryOptions opts, LoadReport* r) : options(opts), report(r) {}

  DictionaryOptions options;
  LoadReport* report;
  ValueFormat format = ValueFormat::Count;
  std::optional<double> unknown_cost;
  std::vector<std::u32string> words;
  std::vector<std::optional<double>> values;
  std::unordered_map<std::u32string, std::uint32_t> index;

  void problem(std::size_t line, std::string_view word, std::string message) {
    if (report && report->problems.size() < LoadReport::kMaxProblems) {
      report->problems.push_back({line, std::string(word), std::move(message)});
    }
  }

  void reject(std::size_t line, std::string_view word, std::string message) {
    if (report) ++report->rejected;
    problem(line, word, std::move(message));
  }

  void add(std::u32string w, std::optional<double> value, std::size_t line, std::string_view raw) {
    if (std::string why = reject_reason(w); !why.empty()) {
      reject(line, raw, std::move(why));
      return;
    }
    if (value) {
      const bool ok = format == ValueFormat::Count ? *value >= 0 : *value <= 0;
      if (!ok) {
        reject(line, raw, format == ValueFormat::Count ? "negative count" : "log probability above 0");
        return;
      }
    }
    if (auto issues = validate_clusters(w); !issues.empty()) {
      if (report) ++report->warnings;
      problem(line, raw,
              std::string("kept, but cluster check found ") + to_string(issues.front().kind) +
                  " at offset " + std::to_string(issues.front().offset));
    }

    auto [it, inserted] = index.try_emplace(w, static_cast<std::uint32_t>(words.size()));
    if (inserted) {
      words.push_back(std::move(w));
      values.push_back(value);
      return;
    }
    if (report) ++report->duplicates;
    auto& old = values[it->second];
    if (!value) return;
    if (!old) {
      old = value;
    } else if (format == ValueFormat::Count) {
      *old += *value;
    } else {
      *old = std::max(*old, *value);
    }
  }

  void directive(std::string_view body, std::size_t line) {
    body = trim(body);
    auto take = [&body](std::string_view key) {
      if (body.substr(0, key.size()) != key) return false;
      body = trim(body.substr(key.size()));
      return true;
    };
    if (take("format:")) {
      if (body == "count") {
        format = ValueFormat::Count;
      } else if (body == "logprob") {
        format = ValueFormat::LogProb;
      } else {
        problem(line, {}, "unknown format '" + std::string(body) + "', using count");
      }
    } else if (take("unknown-cost:")) {
      if (auto v = parse_double(body); v && *v > 0) {
        unknown_cost = *v;
      } else {
        problem(line, {}, "bad unknown-cost value");
      }
    }
  }

  Dictionary finish() {
    if (!(options.alpha > 0)) throw std::invalid_argument("dictionary alpha must be > 0");
    Dictionary d;
    d.format_ = format;
    d.unknown_cost_ = options.unknown_cost ? options.unknown_cost : unknown_cost;
    const std::size_t n = words.size();
    d.counts_.resize(n);
    d.costs_.resize(n);

    if (format == ValueFormat::Count) {
      double total = 0;
      for (const auto& v : values) total += v.value_or(0.0);
      const double denom = total + options.alpha * static_cast<double>(n);
      for (std::size_t i = 0; i < n; ++i) {
        d.counts_[i] = values[i].value_or(0.0);
        d.costs_[i] = -std::log((d.counts_[i] + options.alpha) / denom);
      }
      d.total_ = total;
    } else {
      double worst = 0;
      for (const auto& v : values) {
        if (v) worst = std::max(worst, -*v);
      }
      for (std::size_t i = 0; i < n; ++i) {
        d.counts_[i] = 0;
        d.costs_[i] = values[i] ? -*values[i] : worst;
      }
    }

    RefTrie fwd, bwd;
    for (std::size_t i = 0; i < n; ++i) {
      const auto id = static_cast<std::uint32_t>(i);
      d.max_cost_ = std::max(d.max_cost_, d.costs_[i]);
      d.max_len_ = std::max(d.max_len_, words[i].size());
      fwd.insert(words[i], id);
      std::u32string rev(words[i].rbegin(), words[i].rend());
      bwd.insert(rev, id);
    }
    d.forward_ = DoubleArrayTrie::build(fwd);
    d.backward_ = DoubleArrayTrie::build(bwd);
    for (const auto& w : words) {
      d.word_blob_ += w;
      d.word_offsets_.push_back(static_cast<std::uint32_t>(d.word_blob_.size()));
    }
    if (report) report->entries = n;
    return d;
  }
};

Dictionary Dictionary::from_tsv(std::istream& in, LoadReport* report, DictionaryOptions options) {
  Builder b(options, report);
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (report) ++report->lines;
    std::string_view view = line;
    if (line_no == 1 && view.substr(0, 3) == "\xEF\xBB\xBF") view.remove_prefix(3);
    view = trim(view);
    if (view.empty()) continue;
    if (view.front() == '#') {
      b.directive(view.substr(1), line_no);
      continue;
    }

    const std::size_t tab = view.find('\t');
    const std::string_view raw_word = trim(view.substr(0, tab));
    std::optional<double> value;
    if (tab != std::string_view::npos) {
      std::string_view rest = view.substr(tab + 1);
      rest = trim(rest.substr(0, rest.find('\t')));
      if (!rest.empty()) {
        value = parse_double(rest);
        if (!value) {
          b.reject(line_no, raw_word, "bad number '" + std::string(rest) + "'");
          continue;
        }
      }
    }

    std::u32string w;
    std::vector<std::uint32_t> offsets;
    try {
      utf8::decode(raw_word, w, offsets, utf8::ErrorPolicy::Throw);
    } catch (const utf8::DecodeError&) {
      b.reject(line_no, raw_word, "invalid UTF-8");
      continue;
    }
    b.add(std::move(w), value, line_no, raw_word);
  }
  if (in.bad()) throw std::runtime_error("error reading dictionary");
  return b.finish();
}

Dictionary Dictionary::from_tsv_file(const std::filesystem::path& path, LoadReport* report,
                                     DictionaryOptions options) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open dictionary " + path.string());
  return from_tsv(in, report, options);
}

Dictionary Dictionary::from_words(const std::vector<std::pair<std::u32string, double>>& words,
                                  LoadReport* report, DictionaryOptions options) {
  Builder b(options, report);
  for (const auto& [w, c] : words) b.add(w, c, 0, utf8::encode(w));
  return b.finish();
}

double Dictionary::default_unknown_cost() const noexcept {
  return unknown_cost_ ? *unknown_cost_ : max_cost_ + 1.0;
}

std::uint32_t Dictionary::find(std::u32string_view word) const noexcept {
  const std::uint32_t id = forward_.find(word);
  return id == DoubleArrayTrie::kNone ? kNoEntry : id;
}

}  // namespace khseg

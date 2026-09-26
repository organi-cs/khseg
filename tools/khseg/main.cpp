#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../common/cli_io.hpp"
#include "../common/dict_loader.hpp"

namespace {

enum class Format { Space, Zwsp, Json, Clusters };

enum Exit : int { kOk = 0, kUsage = 1, kIoError = 2, kBadUtf8 = 3 };

struct Config {
  std::vector<std::string> files;
  Format format = Format::Space;
  std::string sep = " ";
  khseg::OffsetUnit offsets = khseg::OffsetUnit::CodePoints;
  bool strict_utf8 = false;
  std::string dict_path;
  bool verbose = false;
  unsigned threads = 1;
};

struct Line {
  std::string text;
  std::string_view ending;  // points at a string literal owned by LineReader
  std::uint64_t number;     // across all inputs, for JSON output
  std::size_t file_line;    // within its file, for error messages
  bool had_bom;
};

// One worker's scratch state. Each thread needs its own.
struct Worker {
  khseg::Workspace ws;
  std::vector<khseg::Token> tokens;
  std::string out;
  std::size_t failed = 0;  // index + 1 of the first line with invalid UTF-8
  std::string error;
};

// Appends the formatted line to w.out. Returns false on invalid UTF-8 in
// strict mode.
bool format_line(const Config& cfg, const khseg::Segmenter& seg, Line& l, Worker& w) {
  if (l.had_bom && cfg.format == Format::Zwsp) w.out.append("\xEF\xBB\xBF");
  try {
    seg.segment(l.text, w.ws, w.tokens);
  } catch (const khseg::utf8::DecodeError& e) {
    w.error = e.what();
    return false;
  }
  if (w.ws.replacements > 0) {
    // Write U+FFFD instead of the invalid bytes. Offsets then refer to the
    // repaired line.
    l.text = khseg::utf8::encode(w.ws.text);
    seg.segment(l.text, w.ws, w.tokens);
  }
  switch (cfg.format) {
    case Format::Space: khseg::write_separated(l.text, w.tokens, cfg.sep, w.out); break;
    case Format::Zwsp: khseg::write_zwsp(l.text, w.tokens, w.out); break;
    case Format::Json: khseg::write_json(l.text, w.tokens, l.number, cfg.offsets, w.out); break;
    case Format::Clusters:
      khseg::write_clusters(l.text, w.ws.text, w.ws.byte_offsets, w.tokens, w.out);
      break;
  }
  // Every output line ends in a newline except in lossless zwsp mode, which
  // mirrors the input exactly.
  w.out.append(cfg.format == Format::Zwsp ? l.ending : (l.ending.empty() ? "\n" : l.ending));
  return true;
}

int run(const Config& cfg, const khseg::Segmenter& seg) {
  khseg::cli::Output out(stdout);
  std::vector<Worker> workers(cfg.threads);
  std::vector<Line> batch;
  std::uint64_t line_no = 0;

  // With one thread every line is written as soon as it is read, so khseg
  // works interactively. With more, lines are collected into batches of about
  // 1 MB, split into one contiguous slice per thread, and written back in order.
  const std::size_t batch_bytes = cfg.threads > 1 ? (1u << 20) : 0;

  auto flush_batch = [&](const std::string& name) -> int {
    if (batch.empty()) return kOk;
    const std::size_t n = batch.size();
    const std::size_t parts = std::min<std::size_t>(workers.size(), n);
    auto work = [&](std::size_t k) {
      Worker& w = workers[k];
      w.out.clear();
      w.failed = 0;
      for (std::size_t i = n * k / parts; i < n * (k + 1) / parts; ++i) {
        if (!format_line(cfg, seg, batch[i], w)) {
          w.failed = i + 1;
          return;
        }
      }
    };
    if (parts == 1) {
      work(0);
    } else {
      std::vector<std::thread> threads;
      for (std::size_t k = 1; k < parts; ++k) threads.emplace_back(work, k);
      work(0);
      for (auto& t : threads) t.join();
    }
    for (std::size_t k = 0; k < parts; ++k) {
      out.buffer().append(workers[k].out);
      if (workers[k].failed) {
        out.flush();
        const Line& bad = batch[workers[k].failed - 1];
        std::cerr << "khseg: " << name << ":" << bad.file_line << ": " << workers[k].error << "\n";
        return kBadUtf8;
      }
    }
    batch.clear();
    out.maybe_flush();
    return kOk;
  };

  auto process = [&](std::FILE* f, const std::string& name) -> int {
    khseg::cli::LineReader reader(f);
    std::size_t pending = 0;
    std::string text;
    std::string_view ending;
    while (reader.next(text, ending)) {
      const bool bom = reader.lines_read() == 1 && khseg::cli::strip_bom(text);
      pending += text.size();
      batch.push_back({std::move(text), ending, ++line_no, reader.lines_read(), bom});
      text = std::string();
      if (pending >= batch_bytes) {
        if (const int rc = flush_batch(name); rc != kOk) return rc;
        pending = 0;
      }
    }
    if (const int rc = flush_batch(name); rc != kOk) return rc;
    if (reader.error()) {
      std::cerr << "khseg: read error on " << name << "\n";
      return kIoError;
    }
    return kOk;
  };

  if (cfg.files.empty()) return process(stdin, "<stdin>");
  for (const auto& path : cfg.files) {
    std::FILE* f = khseg::cli::open_read(path);
    if (f == nullptr) {
      out.flush();
      std::cerr << "khseg: cannot open " << path << "\n";
      return kIoError;
    }
    const int rc = process(f, path);
    std::fclose(f);
    if (rc != kOk) return rc;
  }
  out.flush();
  return out.error() ? kIoError : kOk;
}

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();

  CLI::App app{"khseg: split Khmer text into words. Reads FILEs or standard input."};
  app.set_version_flag("--version", std::string("khseg ") + khseg::version());

  Config cfg;
  bool zwsp = false;
  bool json = false;
  std::string sep;

  app.add_option("files", cfg.files, "Input files (default: standard input)");
  app.add_option("-f,--format", cfg.format, "Output format")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, Format>{{"space", Format::Space},
                                        {"zwsp", Format::Zwsp},
                                        {"json", Format::Json},
                                        {"clusters", Format::Clusters}},
          CLI::ignore_case));
  app.add_flag("--zwsp", zwsp, "Same as --format zwsp");
  app.add_flag("--json", json, "Same as --format json");
  app.add_option("--sep", sep, "Token separator for the space format");
  app.add_option("--offsets", cfg.offsets, "JSON offsets in code points or bytes")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::OffsetUnit>{{"cp", khseg::OffsetUnit::CodePoints},
                                                   {"byte", khseg::OffsetUnit::Bytes}},
          CLI::ignore_case));
  app.add_flag("--strict-utf8", cfg.strict_utf8, "Fail on invalid UTF-8 instead of using U+FFFD");

  khseg::Options opts;
  std::optional<double> unk_cost;
  bool no_merge = false;
  app.add_option("-d,--dict", cfg.dict_path,
                 "Dictionary TSV (default: $KHSEG_DICT, then <exe>/../share/khseg/)");
  app.add_option("-a,--algo", opts.algorithm, "Segmentation algorithm")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::Algorithm>{{"viterbi", khseg::Algorithm::Viterbi},
                                                  {"fmm", khseg::Algorithm::Forward},
                                                  {"bmm", khseg::Algorithm::Backward},
                                                  {"bimm", khseg::Algorithm::Bidirectional}},
          CLI::ignore_case));
  app.add_option("--unk-cost", unk_cost, "Cost of one unknown cluster (default: from dictionary)")
      ->check(CLI::PositiveNumber);
  app.add_flag("--no-merge-unknown", no_merge, "Keep unknown clusters as separate tokens");
  bool no_normalize = false;
  app.add_flag("--no-normalize", no_normalize, "Do not reorder marks before dictionary lookup");
  app.add_option("--lektoo", opts.lektoo, "Handling of the repetition mark U+17D7")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::LekTooPolicy>{{"separate", khseg::LekTooPolicy::Separate},
                                                     {"attach", khseg::LekTooPolicy::Attach}},
          CLI::ignore_case));
  app.add_flag("-v,--verbose", cfg.verbose, "Report dictionary problems on stderr");
  app.add_option("-j,--threads", cfg.threads, "Worker threads (0 = one per CPU); output order is kept");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    const int rc = app.exit(e);
    return rc == 0 ? kOk : kUsage;
  }

  if (zwsp && json) {
    std::cerr << "khseg: --zwsp and --json cannot be combined\n";
    return kUsage;
  }
  if (zwsp) cfg.format = Format::Zwsp;
  if (json) cfg.format = Format::Json;
  if (!sep.empty()) cfg.sep = sep;
  if (cfg.threads == 0) cfg.threads = std::max(1u, std::thread::hardware_concurrency());

  if (cfg.strict_utf8) opts.invalid_utf8 = khseg::utf8::ErrorPolicy::Throw;
  opts.unknown_cost = unk_cost;
  opts.merge_unknown = !no_merge;
  opts.normalize = !no_normalize;

  std::shared_ptr<const khseg::Dictionary> dict;
  try {
    dict = khseg::cli::load_dictionary("khseg", cfg.dict_path, cfg.verbose);
  } catch (const std::exception& e) {
    std::cerr << "khseg: " << e.what() << '\n';
    return kIoError;
  }
  if (!dict && cfg.format != Format::Clusters) {
    std::cerr << "khseg: no dictionary found, Khmer text is not segmented (use --dict)\n";
  }
  khseg::Segmenter seg(dict, opts);

  return run(cfg, seg);
}

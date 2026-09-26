#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../common/cli_io.hpp"

#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

double peak_rss_mib() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0;
  return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
  return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
#else
  return static_cast<double>(ru.ru_maxrss) / 1024.0;
#endif
#endif
}

std::string compiler() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
  return "msvc " + std::to_string(_MSC_VER);
#else
  return "unknown";
#endif
}

struct Result {
  std::string name;
  double median_s = 0, best_s = 0;
  std::size_t tokens = 0;
};

// Segments every line `repeat` times after one warm-up pass. Input is in
// memory, so this measures the segmenter and not I/O.
Result time_segmenter(const std::string& name, const khseg::Segmenter& seg,
                      const std::vector<std::string>& lines, int repeat) {
  khseg::Workspace ws;
  std::vector<khseg::Token> toks;
  auto pass = [&] {
    std::size_t n = 0;
    for (const auto& l : lines) {
      seg.segment(l, ws, toks);
      n += toks.size();
    }
    return n;
  };
  Result r;
  r.name = name;
  r.tokens = pass();
  std::vector<double> times;
  for (int i = 0; i < repeat; ++i) {
    const auto t0 = Clock::now();
    if (pass() != r.tokens) std::abort();  // also keeps the work from being optimized away
    times.push_back(since(t0));
  }
  std::sort(times.begin(), times.end());
  r.median_s = times[times.size() / 2];
  r.best_s = times.front();
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();
  CLI::App app{"khseg-bench: segmentation throughput in MB/s of UTF-8 input"};
  app.set_version_flag("--version", std::string("khseg-bench ") + khseg::version());

  std::string dict_path, input_path, csv_path, label = "run";
  double min_mb = 50;
  int repeat = 5;
  std::vector<std::string> algos{"viterbi", "fmm", "bmm", "bimm"};
  app.add_option("-d,--dict", dict_path, "Dictionary (TSV or .khd)")->required();
  app.add_option("-i,--input", input_path, "Text to segment, one sentence or paragraph per line")
      ->required();
  app.add_option("--min-mb", min_mb, "Repeat the input until it is at least this many MB");
  app.add_option("--repeat", repeat, "Timed passes per configuration (median is reported)")
      ->check(CLI::PositiveNumber);
  app.add_option("--algo", algos, "Algorithms to time")->delimiter(',');
  app.add_option("--csv", csv_path, "Append result rows to this CSV file");
  app.add_option("--label", label, "Label for the CSV rows");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    const int rc = app.exit(e);
    return rc == 0 ? 0 : 1;
  }

  const std::map<std::string, khseg::Algorithm> by_name{{"viterbi", khseg::Algorithm::Viterbi},
                                                        {"fmm", khseg::Algorithm::Forward},
                                                        {"bmm", khseg::Algorithm::Backward},
                                                        {"bimm", khseg::Algorithm::Bidirectional}};
  for (const auto& a : algos) {
    if (!by_name.count(a)) {
      std::cerr << "khseg-bench: unknown algorithm " << a << "\n";
      return 1;
    }
  }

  // Dictionary load times. A TSV dictionary is parsed once and written to a
  // temporary .khd so that the binary paths can be timed as well. The
  // benchmark itself uses a heap copy, so the temporary file can be deleted.
  std::shared_ptr<const khseg::Dictionary> dict;
  double tsv_ms = -1, heap_ms = -1, map_verify_ms = -1, map_trust_ms = -1;
  try {
    std::string bytes;
    {
      std::ifstream in(dict_path, std::ios::binary);
      if (!in) throw std::runtime_error("cannot open " + dict_path);
      bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::path khd = dict_path;
    std::filesystem::path temp;
    if (bytes.compare(0, 8, "KHSEGDIC") != 0) {
      auto t0 = Clock::now();
      const auto d = khseg::Dictionary::from_tsv_file(dict_path);
      tsv_ms = since(t0) * 1000;
      temp = std::filesystem::temp_directory_path() / "khseg-bench-dict.khd";
      d.save_binary(temp);
      khd = temp;
      bytes = d.to_binary();
    }
    // Best of 5 for each way of loading a binary dictionary.
    auto best_ms = [](auto load) {
      double best = 1e300;
      for (int i = 0; i < 5; ++i) {
        const auto t0 = Clock::now();
        load();
        best = std::min(best, since(t0) * 1000);
      }
      return best;
    };
    heap_ms = best_ms([&] { khseg::Dictionary::from_binary(bytes); });
    map_verify_ms = best_ms([&] { khseg::Dictionary::map_file(khd, true); });
    map_trust_ms = best_ms([&] { khseg::Dictionary::map_file(khd, false); });
    dict = std::make_shared<const khseg::Dictionary>(khseg::Dictionary::from_binary(bytes));
    if (!temp.empty()) std::filesystem::remove(temp);
  } catch (const std::exception& ex) {
    std::cerr << "khseg-bench: " << ex.what() << "\n";
    return 2;
  }

  std::vector<std::string> base;
  {
    std::FILE* f = khseg::cli::open_read(input_path);
    if (f == nullptr) {
      std::cerr << "khseg-bench: cannot open " << input_path << "\n";
      return 2;
    }
    khseg::cli::LineReader reader(f);
    std::string line;
    std::string_view ending;
    while (reader.next(line, ending)) {
      if (!line.empty()) base.push_back(line);
    }
    std::fclose(f);
  }
  if (base.empty()) {
    std::cerr << "khseg-bench: input is empty\n";
    return 2;
  }
  std::vector<std::string> lines;
  std::size_t bytes = 0;
  while (static_cast<double>(bytes) < min_mb * 1e6 || lines.empty()) {
    for (const auto& l : base) {
      lines.push_back(l);
      bytes += l.size();
    }
  }
  const double mb = static_cast<double>(bytes) / 1e6;

  std::printf("compiler     %s\n", compiler().c_str());
  std::printf("dictionary   %s, %zu words\n", dict_path.c_str(), dict->size());
  if (tsv_ms >= 0) std::printf("load TSV                   %8.2f ms\n", tsv_ms);
  std::printf("load .khd into memory      %8.2f ms\n", heap_ms);
  std::printf("map .khd, checksum         %8.2f ms\n", map_verify_ms);
  std::printf("map .khd, no checksum      %8.2f ms\n", map_trust_ms);
  std::printf("input        %s, %.1f MB in %zu lines\n\n", input_path.c_str(), mb, lines.size());
  std::printf("%-22s %10s %10s %12s\n", "configuration", "MB/s", "best MB/s", "tokens/s");

  std::vector<Result> results;
  results.push_back(time_segmenter("decode+pretokenize", khseg::Segmenter(nullptr), lines, repeat));
  for (const auto& a : algos) {
    khseg::Options o;
    o.algorithm = by_name.at(a);
    results.push_back(time_segmenter(a, khseg::Segmenter(dict, o), lines, repeat));
  }
  for (const auto& r : results) {
    std::printf("%-22s %10.1f %10.1f %12.0f\n", r.name.c_str(), mb / r.median_s, mb / r.best_s,
                static_cast<double>(r.tokens) / r.median_s);
  }
  std::printf("\npeak memory  %.1f MiB\n", peak_rss_mib());

  if (!csv_path.empty()) {
    const bool fresh = !std::filesystem::exists(csv_path);
    std::ofstream csv(csv_path, std::ios::app | std::ios::binary);
    if (fresh) csv << "label,compiler,dictionary_words,input_mb,configuration,median_mb_s,best_mb_s,tokens_s\n";
    for (const auto& r : results) {
      char row[512];
      std::snprintf(row, sizeof row, "%s,\"%s\",%zu,%.1f,%s,%.2f,%.2f,%.0f\n", label.c_str(),
                    compiler().c_str(), dict->size(), mb, r.name.c_str(), mb / r.median_s,
                    mb / r.best_s, static_cast<double>(r.tokens) / r.median_s);
      csv << row;
    }
  }
  return 0;
}

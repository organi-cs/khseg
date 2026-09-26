#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "../common/cli_io.hpp"
#include "../common/dict_loader.hpp"

namespace {

enum Exit : int { kOk = 0, kUsage = 1, kIoError = 2, kProblems = 5 };

double seconds_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

int build(const std::string& in, const std::string& out, double alpha, std::optional<double> unk,
          bool verbose) {
  khseg::LoadReport report;
  khseg::DictionaryOptions opts;
  opts.alpha = alpha;
  opts.unknown_cost = unk;
  const auto t0 = std::chrono::steady_clock::now();
  const auto d = khseg::Dictionary::from_tsv_file(in, &report, opts);
  const double load = seconds_since(t0);
  khseg::cli::print_report("khseg-dict", in, report, verbose);
  d.save_binary(out);
  const auto t1 = std::chrono::steady_clock::now();
  const auto e = khseg::Dictionary::from_file(out);
  const double reload = seconds_since(t1);
  std::printf("%s: %zu words, unknown cost %.3f\n", out.c_str(), e.size(), e.default_unknown_cost());
  std::printf("TSV load %.1f ms, binary load %.1f ms\n", load * 1000, reload * 1000);
  return kOk;
}

int stats(const std::string& path) {
  const auto d = khseg::Dictionary::from_file(path);
  std::printf("storage            %s\n", d.is_mapped() ? "memory-mapped file" : "in memory");
  std::printf("words              %zu\n", d.size());
  std::printf("total count        %.0f\n", d.total_count());
  std::printf("max cost           %.3f\n", d.max_cost());
  std::printf("unknown cost       %.3f%s\n", d.default_unknown_cost(),
              d.unknown_cost_override() ? " (from file)" : " (max cost + 1)");
  std::printf("longest word       %zu code points\n", d.max_word_length());
  std::printf("trie size          %zu + %zu slots, %.1f MiB\n", d.forward().size(), d.backward().size(),
              static_cast<double>(d.forward().memory_bytes() + d.backward().memory_bytes()) /
                  (1024.0 * 1024.0));

  std::size_t single = 0, counted = 0;
  std::vector<std::uint32_t> clusters;
  std::vector<std::size_t> by_clusters(9, 0);
  std::vector<std::size_t> by_cost(8, 0);
  for (std::uint32_t i = 0; i < d.size(); ++i) {
    khseg::cluster_boundaries(d.word(i), clusters);
    const std::size_t n = clusters.size() - 1;
    ++by_clusters[std::min<std::size_t>(n, 8)];
    single += n == 1;
    counted += d.count(i) > 0;
    ++by_cost[std::min<std::size_t>(static_cast<std::size_t>(d.cost(i) / 4.0), 7)];
  }
  std::printf("with a count       %zu\n", counted);
  std::printf("one-cluster words  %zu\n", single);
  std::printf("\nclusters per word\n");
  for (std::size_t k = 1; k < by_clusters.size(); ++k) {
    std::printf("  %zu%s  %zu\n", k, k == 8 ? "+" : " ", by_clusters[k]);
  }
  std::printf("\ncost (-ln p)\n");
  for (std::size_t k = 0; k < by_cost.size(); ++k) {
    if (k == 7) {
      std::printf("  28+      %zu\n", by_cost[k]);
    } else {
      std::printf("  %2zu to %2zu %zu\n", k * 4, k * 4 + 4, by_cost[k]);
    }
  }

  std::vector<std::uint32_t> ids(d.size());
  for (std::uint32_t i = 0; i < d.size(); ++i) ids[i] = i;
  std::sort(ids.begin(), ids.end(),
            [&d](std::uint32_t a, std::uint32_t b) { return d.word(a).size() > d.word(b).size(); });
  std::printf("\nlongest entries\n");
  for (std::size_t k = 0; k < std::min<std::size_t>(5, ids.size()); ++k) {
    std::printf("  %s\n", khseg::utf8::encode(d.word(ids[k])).c_str());
  }
  return kOk;
}

int check(const std::string& path) {
  khseg::LoadReport report;
  khseg::Dictionary::from_tsv_file(path, &report);
  std::printf("%zu lines, %zu words, %zu duplicates, %zu rejected, %zu warnings\n", report.lines,
              report.entries, report.duplicates, report.rejected, report.warnings);
  for (const auto& p : report.problems) {
    std::printf("line %zu: %s: %s\n", p.line, p.word.c_str(), p.message.c_str());
  }
  if (report.problems.size() == khseg::LoadReport::kMaxProblems) {
    std::printf("(only the first %zu problems are listed)\n", khseg::LoadReport::kMaxProblems);
  }
  return report.rejected + report.warnings > 0 ? kProblems : kOk;
}

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();
  CLI::App app{"khseg-dict: build, inspect and check khseg dictionaries"};
  app.set_version_flag("--version", std::string("khseg-dict ") + khseg::version());
  app.require_subcommand(1);

  std::string in, out;
  double alpha = 0.5;
  std::optional<double> unk;
  bool verbose = false;

  auto* b = app.add_subcommand("build", "Compile a TSV dictionary to the binary .khd format");
  b->add_option("input", in, "TSV dictionary")->required();
  b->add_option("-o,--output", out, "Output .khd file")->required();
  b->add_option("--alpha", alpha, "Smoothing constant")->check(CLI::PositiveNumber);
  b->add_option("--unk-cost", unk, "Store this unknown-cluster cost in the file")
      ->check(CLI::PositiveNumber);
  b->add_flag("-v,--verbose", verbose, "List rejected entries");

  std::string stats_path;
  auto* s = app.add_subcommand("stats", "Print statistics for a TSV or .khd dictionary");
  s->add_option("dictionary", stats_path)->required();

  std::string check_path;
  auto* c = app.add_subcommand("check", "List every rejected or suspicious entry of a TSV file");
  c->add_option("dictionary", check_path)->required();

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    const int rc = app.exit(e);
    return rc == 0 ? kOk : kUsage;
  }

  try {
    if (*b) return build(in, out, alpha, unk, verbose);
    if (*s) return stats(stats_path);
    if (*c) return check(check_path);
  } catch (const std::exception& e) {
    std::cerr << "khseg-dict: " << e.what() << "\n";
    return kIoError;
  }
  return kUsage;
}

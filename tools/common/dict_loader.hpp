#pragma once

#include <khseg/dictionary.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "cli_io.hpp"

namespace khseg::cli {

// Where to look for a dictionary when --dict is not given.
inline std::filesystem::path default_dictionary_path() {
  if (const char* env = std::getenv("KHSEG_DICT"); env != nullptr && *env != '\0') return env;
  const auto dir = executable_dir();
  if (dir.empty()) return {};
  for (const char* name : {"khmer.khd", "khmer.tsv"}) {
    auto p = dir / ".." / "share" / "khseg" / name;
    if (std::filesystem::exists(p)) return p;
  }
  return {};
}

inline void print_report(const std::string& tool, const std::filesystem::path& path,
                         const LoadReport& r, bool verbose) {
  if (verbose) {
    std::cerr << tool << ": " << path.string() << ": " << r.entries << " words, " << r.duplicates
              << " duplicates, " << r.rejected << " rejected, " << r.warnings << " warnings\n";
    for (const auto& p : r.problems) {
      std::cerr << "  line " << p.line << ": " << p.word << ": " << p.message << "\n";
    }
  } else if (r.rejected > 0) {
    std::cerr << tool << ": " << path.string() << ": " << r.rejected
              << " entries rejected (use --verbose to list them)\n";
  }
}

// Loads the dictionary at `path`, or the default one when `path` is empty.
// Returns nullptr when no dictionary is configured. Throws on read errors.
inline std::shared_ptr<const Dictionary> load_dictionary(const std::string& tool,
                                                         std::filesystem::path path, bool verbose,
                                                         double alpha = 0.5) {
  if (path.empty()) path = default_dictionary_path();
  if (path.empty()) return nullptr;
  LoadReport report;
  DictionaryOptions opts;
  opts.alpha = alpha;
  auto dict = std::make_shared<const Dictionary>(Dictionary::from_file(path, &report, opts));
  print_report(tool, path, report, verbose);
  return dict;
}

}  // namespace khseg::cli

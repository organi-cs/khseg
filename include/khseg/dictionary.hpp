#pragma once

#include <khseg/export.hpp>
#include <khseg/trie.hpp>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace khseg {

// What happened while loading a dictionary. The library never prints; tools
// decide what to show.
struct LoadReport {
  struct Problem {
    std::size_t line;  // 1-based line in the TSV, 0 when not from a file
    std::string word;  // UTF-8 as found in the file
    std::string message;
  };

  std::size_t lines = 0;       // lines read, comments included
  std::size_t entries = 0;     // distinct words kept
  std::size_t duplicates = 0;  // lines whose word was already present
  std::size_t rejected = 0;    // lines dropped
  std::size_t warnings = 0;    // kept, but flagged (e.g. non-canonical mark order)
  std::vector<Problem> problems;  // first kMaxProblems rejections and warnings

  static constexpr std::size_t kMaxProblems = 1000;
};

struct DictionaryOptions {
  // Additive smoothing constant for P(w) = (c(w) + alpha) / (N + alpha * V).
  double alpha = 0.5;
  // Replaces the "# unknown-cost:" value from the file, if set.
  std::optional<double> unknown_cost;
  // Store words with their marks in canonical order (see normalize.hpp), so
  // that differently typed spellings of one word become one entry.
  bool normalize = true;
  // For binary files: check the FNV-1a checksum over the whole file. This reads
  // every page once; turn it off to load a trusted file in constant time.
  // Structural checks that keep lookups in bounds always run.
  bool verify_checksum = true;
};

// Word list with unigram costs. Build it from a TSV file:
//
//   # comment
//   # format: count          (default) or "logprob" for natural log probabilities
//   # unknown-cost: 14.5     optional, cost of one unknown cluster
//   word<TAB>count
//
// Words must be Khmer letters only (bases, marks, joiners) and must start with
// a base. A missing count means the word is known but was never counted.
// Repeated words have their counts added.
class KHSEG_EXPORT Dictionary {
 public:
  enum class ValueFormat : std::uint8_t { Count, LogProb };

  static Dictionary from_tsv(std::istream& in, LoadReport* report = nullptr,
                             DictionaryOptions options = {});
  static Dictionary from_tsv_file(const std::filesystem::path& path, LoadReport* report = nullptr,
                                  DictionaryOptions options = {});
  // Reads a TSV file or a binary .khd file, telling them apart by content.
  // A binary file is memory-mapped (see map_file). For a binary file `report`
  // only gets the entry count, and alpha and normalize are ignored because
  // the file stores finished costs and normalized words.
  static Dictionary from_file(const std::filesystem::path& path, LoadReport* report = nullptr,
                              DictionaryOptions options = {});

  // Copies `bytes` (the contents of a .khd file) into memory it owns.
  static Dictionary from_binary(std::string_view bytes, bool verify_checksum = true);

  // Maps a .khd file read-only and uses its arrays in place, without copying.
  // Loading costs a few page faults instead of reading the whole file, and
  // processes that map the same file share its pages. The mapping lives as
  // long as the Dictionary or any copy of it. While it is mapped, Windows
  // will not let the file be replaced or deleted.
  static Dictionary map_file(const std::filesystem::path& path, bool verify_checksum = true);

  // True when the arrays live in a memory-mapped file.
  bool is_mapped() const noexcept { return mapped_; }

  // Binary format: see the comment at the top of src/dictionary_io.cpp.
  // Throws std::runtime_error on failure.
  void save_binary(const std::filesystem::path& path) const;
  std::string to_binary() const;
  // Words with counts, for tests and programmatic use. Invalid words are
  // rejected the same way as in a file.
  static Dictionary from_words(const std::vector<std::pair<std::u32string, double>>& words,
                               LoadReport* report = nullptr, DictionaryOptions options = {});

  std::size_t size() const noexcept { return word_offsets_.size() - 1; }
  std::u32string_view word(std::uint32_t id) const noexcept {
    return {word_blob_.data() + word_offsets_[id], word_offsets_[id + 1] - word_offsets_[id]};
  }
  double count(std::uint32_t id) const noexcept { return counts_[id]; }
  double cost(std::uint32_t id) const noexcept { return costs_[id]; }
  double total_count() const noexcept { return total_; }
  double max_cost() const noexcept { return max_cost_; }
  ValueFormat value_format() const noexcept { return format_; }

  // Unknown-cluster cost from the file header, or max_cost() + 1.
  double default_unknown_cost() const noexcept;
  std::optional<double> unknown_cost_override() const noexcept { return unknown_cost_; }

  // Entry id of `word`, or kNoEntry.
  std::uint32_t find(std::u32string_view word) const noexcept;

  // Tries over the words and over the reversed words (for backward matching).
  const DoubleArrayTrie& forward() const noexcept { return forward_; }
  const DoubleArrayTrie& backward() const noexcept { return backward_; }
  std::size_t max_word_length() const noexcept { return max_len_; }

 private:
  struct Builder;
  static Dictionary parse_binary(const char* data, std::size_t size,
                                 std::shared_ptr<const void> owner, bool verify_checksum);

  static constexpr std::uint32_t kNoOffsets[1] = {0};

  // The arrays below point into `storage_`: either buffers owned by this
  // dictionary or a mapped file. Copies of a Dictionary share them.
  std::shared_ptr<const void> storage_;
  bool mapped_ = false;
  // All words back to back; word i is [word_offsets_[i], word_offsets_[i + 1]).
  std::span<const char32_t> word_blob_;
  std::span<const std::uint32_t> word_offsets_{kNoOffsets};
  std::span<const double> counts_;
  std::span<const double> costs_;
  double total_ = 0.0;
  double max_cost_ = 0.0;
  std::size_t max_len_ = 0;
  ValueFormat format_ = ValueFormat::Count;
  std::optional<double> unknown_cost_;
  DoubleArrayTrie forward_;
  DoubleArrayTrie backward_;
};

}  // namespace khseg

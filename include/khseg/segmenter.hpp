#pragma once

#include <khseg/export.hpp>
#include <khseg/token.hpp>
#include <khseg/utf8.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace khseg {

class Dictionary;

enum class Algorithm : std::uint8_t {
  Viterbi,        // minimum total cost over unigram word costs (default)
  Forward,        // forward maximal matching
  Backward,       // backward maximal matching
  Bidirectional,  // run both and pick with a heuristic
};

enum class LekTooPolicy : std::uint8_t {
  Separate,  // ៗ is its own Punct token
  Attach,    // ៗ directly after a Khmer word becomes part of that word
};

struct Options {
  Algorithm algorithm = Algorithm::Viterbi;
  // Cost of one unknown cluster. When unset, the dictionary's value is used.
  std::optional<double> unknown_cost;
  // Merge adjacent unknown clusters into a single Unknown token.
  bool merge_unknown = true;
  LekTooPolicy lektoo = LekTooPolicy::Separate;
  // Reorder marks inside each cluster before dictionary lookup (see
  // normalize.hpp). Token offsets always refer to the original text.
  bool normalize = true;
  utf8::ErrorPolicy invalid_utf8 = utf8::ErrorPolicy::Replace;
};

// Scratch buffers for one call to Segmenter::segment. Reusing one Workspace
// per thread avoids allocating on every call. After a call, `text` and
// `byte_offsets` hold the decoded input, which formatters can use, and
// `replacements` counts invalid UTF-8 sequences that became U+FFFD.
struct Workspace {
  std::u32string text;
  std::vector<std::uint32_t> byte_offsets;
  std::size_t replacements = 0;
  std::vector<Token> coarse;
  std::vector<std::uint32_t> clusters;
  std::vector<std::uint8_t> is_boundary;
  std::vector<double> best;  // Viterbi cost to reach each code point of the last run
  std::vector<std::uint32_t> ntokens;
  std::vector<std::uint32_t> back_from;
  std::vector<std::uint32_t> back_entry;
  std::vector<Token> scratch;
  std::u32string norm;                       // normalized copy of the current run
  std::vector<std::uint32_t> norm_clusters;  // its cluster boundaries
  std::vector<std::uint32_t> norm_to_orig;   // normalized boundary offset -> original offset
};

class KHSEG_EXPORT Segmenter {
 public:
  // With no dictionary, Khmer runs are returned whole as TokenType::Khmer.
  explicit Segmenter(std::shared_ptr<const Dictionary> dict = nullptr, Options options = {});
  ~Segmenter();
  Segmenter(const Segmenter&);
  Segmenter& operator=(const Segmenter&);
  Segmenter(Segmenter&&) noexcept;
  Segmenter& operator=(Segmenter&&) noexcept;

  std::vector<Token> segment(std::string_view utf8) const;
  void segment(std::string_view utf8, Workspace& ws, std::vector<Token>& out) const;

  // Convenience: the text of each non-space token.
  std::vector<std::string> words(std::string_view utf8) const;

  const Options& options() const noexcept { return options_; }
  const Dictionary* dictionary() const noexcept { return dict_.get(); }
  double unknown_cost() const noexcept { return unknown_cost_; }

 private:
  void segment_run(const Token& run, Workspace& ws, std::vector<Token>& out) const;

  std::shared_ptr<const Dictionary> dict_;
  Options options_;
  double unknown_cost_ = 0.0;
};

}  // namespace khseg

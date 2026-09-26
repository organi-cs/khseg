#pragma once

#include <khseg/export.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace khseg {

// Khmer Character Cluster splitting. The rules are in docs/clusters.md.
//
// The splitter is tolerant: it accepts any input and always produces a
// partition. Khmer clusters follow "Base Mark*", where a consonant or
// independent vowel right after COENG is part of the mark sequence.
// Non-Khmer code points each form a cluster of their own. Marks with no
// Khmer base before them form an "orphan" cluster.

// Writes cluster boundaries to `out` (cleared first): 0, b1, ..., text.size().
// For empty input `out` is {0}.
KHSEG_EXPORT void cluster_boundaries(std::u32string_view text, std::vector<std::uint32_t>& out);

KHSEG_EXPORT std::vector<std::u32string_view> split_clusters(std::u32string_view text);

// True when position i starts a new cluster (always true for i == 0).
KHSEG_EXPORT bool starts_cluster(std::u32string_view text, std::size_t i) noexcept;

// True when a cluster begins with a combining mark instead of a base.
KHSEG_EXPORT bool is_orphan(std::u32string_view cluster) noexcept;

enum class ClusterIssueKind : std::uint8_t {
  OrphanMark,         // cluster starts with a mark
  DanglingCoeng,      // COENG not followed by a consonant or independent vowel
  TooManySubscripts,  // more than two COENG + consonant pairs
  InherentVowel,      // U+17B4 or U+17B5 present
  DuplicateMark,      // the same mark twice in a cluster
  MultipleVowels,     // more than one dependent vowel
  OutOfOrder,         // mark appears after a mark from a later slot
};

KHSEG_EXPORT const char* to_string(ClusterIssueKind kind) noexcept;

struct ClusterIssue {
  std::uint32_t offset;  // code point offset of the offending code point
  ClusterIssueKind kind;
};

// Checks Khmer clusters in `text` against the canonical order
//   Base [Robat] (Coeng C){0,2} [Shifter] [Joiner] [DepV] (Sign | Diac)*
// Non-Khmer clusters are ignored. Offsets are relative to `text`.
KHSEG_EXPORT std::vector<ClusterIssue> validate_clusters(std::u32string_view text);

}  // namespace khseg

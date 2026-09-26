#pragma once

#include <khseg/export.hpp>

#include <string>
#include <string_view>

namespace khseg {

// Puts the marks of each Khmer cluster into the canonical order used by the
// cluster validator:
//
//   Base [Robat] (Coeng C)* [Shifter] [Joiner] [Vowel] (Sign | Diac)*
//
// Marks keep their relative order inside a slot, except that a subscript Ro
// (COENG + U+179A) goes after the other subscripts. The invisible inherent
// vowels U+17B4 and U+17B5 are removed, and a mark repeated with nothing
// between it and its copy is kept once.
//
// Clusters that start with a mark, and clusters with a COENG that has no
// consonant to subscript, are copied unchanged.
//
// Only the inside of a cluster changes, so the cluster count is the same
// before and after; the segmenter relies on that to map positions back to the
// original text. Non-Khmer text is copied unchanged.
KHSEG_EXPORT std::u32string normalize(std::u32string_view text);

// Normalizes one cluster (as returned by split_clusters) and appends it to out.
KHSEG_EXPORT void normalize_cluster(std::u32string_view cluster, std::u32string& out);

}  // namespace khseg

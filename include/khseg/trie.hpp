#pragma once

#include <khseg/export.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace khseg {

// A plain pointer-free trie over code points. Children are kept sorted and
// found by binary search. It is simple enough to trust, so it serves as the
// reference that the double-array trie is tested against.
class KHSEG_EXPORT RefTrie {
 public:
  static constexpr std::uint32_t kRoot = 0;
  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

  RefTrie();

  // Maps `key` to `value`, replacing an existing value. Returns false and
  // does nothing when `key` is empty.
  bool insert(std::u32string_view key, std::uint32_t value);

  // Child of `node` along `c`, or kNone.
  std::uint32_t step(std::uint32_t node, char32_t c) const noexcept;

  // Value stored at `node`, or kNone when no key ends there.
  std::uint32_t value(std::uint32_t node) const noexcept { return nodes_[node].value; }

  std::uint32_t find(std::u32string_view key) const noexcept;

  std::size_t node_count() const noexcept { return nodes_.size(); }

  const std::vector<std::pair<char32_t, std::uint32_t>>& children(std::uint32_t node) const {
    return nodes_[node].children;
  }

 private:
  struct Node {
    std::uint32_t value = kNone;
    std::vector<std::pair<char32_t, std::uint32_t>> children;
  };

  std::vector<Node> nodes_;
};

// Double-array trie over a compact alphabet: the Khmer block U+1780..U+17FF
// plus ZWNJ and ZWJ, 130 symbols in all. Any other code point has no
// transition, which is fine because dictionary words are Khmer only.
//
// A transition from node s on symbol c goes to t = base[s] + c and exists
// when check[t] == s. Lookups are two array reads and a compare.
//
// The arrays are held as spans plus a shared owner, so they can live in the
// trie's own buffers or in a memory-mapped file. Copies share the arrays.
class KHSEG_EXPORT DoubleArrayTrie {
 public:
  static constexpr std::uint32_t kRoot = 0;
  static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
  static constexpr std::uint32_t kAlphabet = 131;  // symbol 0 is unused

  static constexpr std::uint32_t symbol(char32_t c) noexcept {
    if (c >= 0x1780 && c <= 0x17FF) return static_cast<std::uint32_t>(c - 0x1780) + 1;
    if (c == 0x200C) return 129;
    if (c == 0x200D) return 130;
    return 0;
  }

  DoubleArrayTrie();

  // Builds from a reference trie. Node values are copied unchanged. Throws
  // std::invalid_argument if a key uses a code point outside the alphabet.
  static DoubleArrayTrie build(const RefTrie& ref);

  // Takes over raw arrays. They must be non-empty and of equal size, and
  // check[0] must be kNone; otherwise returns false and leaves the trie empty.
  // Lookups stay in bounds for any array contents, so no deeper check is needed.
  bool assign(std::vector<std::uint32_t> base, std::vector<std::uint32_t> check,
              std::vector<std::uint32_t> value);

  // Same, but uses arrays owned elsewhere; `owner` keeps them alive.
  bool assign_view(std::span<const std::uint32_t> base, std::span<const std::uint32_t> check,
                   std::span<const std::uint32_t> value, std::shared_ptr<const void> owner);

  std::uint32_t step(std::uint32_t node, char32_t c) const noexcept {
    const std::uint32_t code = symbol(c);
    if (code == 0) return kNone;
    const std::uint64_t t = static_cast<std::uint64_t>(base_[node]) + code;
    return (t < check_.size() && check_[t] == node) ? static_cast<std::uint32_t>(t) : kNone;
  }

  std::uint32_t value(std::uint32_t node) const noexcept { return value_[node]; }

  std::uint32_t find(std::u32string_view key) const noexcept;

  std::size_t size() const noexcept { return check_.size(); }
  std::size_t memory_bytes() const noexcept { return size() * 3 * sizeof(std::uint32_t); }

  std::span<const std::uint32_t> base_array() const noexcept { return base_; }
  std::span<const std::uint32_t> check_array() const noexcept { return check_; }
  std::span<const std::uint32_t> value_array() const noexcept { return value_; }

 private:
  std::shared_ptr<const void> owner_;
  std::span<const std::uint32_t> base_;
  std::span<const std::uint32_t> check_;
  std::span<const std::uint32_t> value_;
};

}  // namespace khseg

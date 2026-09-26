#pragma once

#include <khseg/export.hpp>

#include <cstdint>
#include <limits>
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

  // Calls f(node, depth) for every node, depth first, children in code
  // point order. Used to build the double-array trie.
  template <class F>
  void visit(F&& f) const {
    visit_from(kRoot, 0, f);
  }

  const std::vector<std::pair<char32_t, std::uint32_t>>& children(std::uint32_t node) const {
    return nodes_[node].children;
  }

 private:
  struct Node {
    std::uint32_t value = kNone;
    std::vector<std::pair<char32_t, std::uint32_t>> children;
  };

  template <class F>
  void visit_from(std::uint32_t node, std::size_t depth, F& f) const {
    f(node, depth);
    for (const auto& [c, child] : nodes_[node].children) visit_from(child, depth + 1, f);
  }

  std::vector<Node> nodes_;
};

}  // namespace khseg

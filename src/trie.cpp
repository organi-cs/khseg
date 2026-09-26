#include <khseg/trie.hpp>

#include <algorithm>

namespace khseg {

RefTrie::RefTrie() : nodes_(1) {}

bool RefTrie::insert(std::u32string_view key, std::uint32_t value) {
  if (key.empty()) return false;
  std::uint32_t node = kRoot;
  for (char32_t c : key) {
    auto& kids = nodes_[node].children;
    auto it = std::lower_bound(kids.begin(), kids.end(), c,
                               [](const auto& p, char32_t x) { return p.first < x; });
    if (it != kids.end() && it->first == c) {
      node = it->second;
      continue;
    }
    const auto next = static_cast<std::uint32_t>(nodes_.size());
    kids.insert(it, {c, next});
    nodes_.emplace_back();  // invalidates `kids`, which is not used again
    node = next;
  }
  nodes_[node].value = value;
  return true;
}

std::uint32_t RefTrie::step(std::uint32_t node, char32_t c) const noexcept {
  const auto& kids = nodes_[node].children;
  auto it = std::lower_bound(kids.begin(), kids.end(), c,
                             [](const auto& p, char32_t x) { return p.first < x; });
  return (it != kids.end() && it->first == c) ? it->second : kNone;
}

std::uint32_t RefTrie::find(std::u32string_view key) const noexcept {
  std::uint32_t node = kRoot;
  for (char32_t c : key) {
    node = step(node, c);
    if (node == kNone) return kNone;
  }
  return value(node);
}

}  // namespace khseg

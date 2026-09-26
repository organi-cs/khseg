#include <khseg/trie.hpp>

#include <algorithm>
#include <deque>
#include <stdexcept>

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

DoubleArrayTrie::DoubleArrayTrie() : base_(1, 0), check_(1, kNone), value_(1, kNone) {}

DoubleArrayTrie DoubleArrayTrie::build(const RefTrie& ref) {
  DoubleArrayTrie t;
  std::size_t cap = std::max<std::size_t>(256, ref.node_count() * 2);
  t.base_.assign(cap, 0);
  t.check_.assign(cap, kNone);
  t.value_.assign(cap, kNone);
  std::vector<std::uint8_t> used(cap, 0);
  used[0] = 1;
  t.value_[0] = ref.value(RefTrie::kRoot);

  auto grow = [&](std::size_t need) {
    if (need < cap) return;
    while (cap <= need) cap *= 2;
    t.base_.resize(cap, 0);
    t.check_.resize(cap, kNone);
    t.value_.resize(cap, kNone);
    used.resize(cap, 0);
  };

  // Breadth-first: (reference node, double-array slot).
  std::deque<std::pair<std::uint32_t, std::uint32_t>> queue{{RefTrie::kRoot, 0}};
  std::vector<std::uint32_t> codes;
  std::size_t first_free = 1;
  std::size_t top = 0;  // highest slot in use

  while (!queue.empty()) {
    const auto [node, slot] = queue.front();
    queue.pop_front();
    const auto& kids = ref.children(node);
    if (kids.empty()) continue;

    codes.clear();
    for (const auto& [c, child] : kids) {
      const std::uint32_t code = symbol(c);
      if (code == 0) throw std::invalid_argument("trie key contains a code point outside the alphabet");
      codes.push_back(code);
    }

    // First fit: the smallest base whose child slots are all free. Children
    // are sorted by code point, and symbol() keeps that order.
    while (first_free < cap && used[first_free]) ++first_free;
    std::size_t pos = std::max<std::size_t>(first_free, codes.front());
    std::size_t b = 0;
    for (;;) {
      b = pos - codes.front();
      grow(b + codes.back());
      bool fits = true;
      for (std::uint32_t code : codes) {
        if (used[b + code]) {
          fits = false;
          break;
        }
      }
      if (fits) break;
      do ++pos;
      while (pos < cap && used[pos]);
    }

    t.base_[slot] = static_cast<std::uint32_t>(b);
    for (std::size_t k = 0; k < kids.size(); ++k) {
      const std::size_t s = b + codes[k];
      used[s] = 1;
      t.check_[s] = slot;
      t.value_[s] = ref.value(kids[k].second);
      top = std::max(top, s);
      queue.emplace_back(kids[k].second, static_cast<std::uint32_t>(s));
    }
  }

  t.base_.resize(top + 1);
  t.check_.resize(top + 1);
  t.value_.resize(top + 1);
  return t;
}

bool DoubleArrayTrie::assign(std::vector<std::uint32_t> base, std::vector<std::uint32_t> check,
                             std::vector<std::uint32_t> value) {
  const std::size_t n = check.size();
  bool ok = n > 0 && base.size() == n && value.size() == n && check[0] == kNone;
  for (std::size_t i = 1; ok && i < n; ++i) {
    if (check[i] != kNone && (check[i] >= n || base[check[i]] >= i)) ok = false;
  }
  if (!ok) {
    *this = DoubleArrayTrie();
    return false;
  }
  base_ = std::move(base);
  check_ = std::move(check);
  value_ = std::move(value);
  return true;
}

std::uint32_t DoubleArrayTrie::find(std::u32string_view key) const noexcept {
  std::uint32_t node = kRoot;
  for (char32_t c : key) {
    node = step(node, c);
    if (node == kNone) return kNone;
  }
  return value(node);
}

}  // namespace khseg

#include <khseg/charclass.hpp>
#include <khseg/cluster.hpp>

#include <algorithm>
#include <bitset>

namespace khseg {

bool starts_cluster(std::u32string_view text, std::size_t i) noexcept {
  if (i == 0) return true;
  const CharClass c = classify(text[i]);
  const CharClass p = classify(text[i - 1]);
  const bool c_joins = is_khmer_letter(c) || c == CharClass::Joiner;
  if (p == CharClass::Coeng) return !c_joins;
  if (is_mark(c) || c == CharClass::Joiner) {
    return !(is_khmer_letter(p) || p == CharClass::Joiner);
  }
  return true;
}

void cluster_boundaries(std::u32string_view text, std::vector<std::uint32_t>& out) {
  out.clear();
  out.push_back(0);
  for (std::size_t i = 1; i < text.size(); ++i) {
    if (starts_cluster(text, i)) out.push_back(static_cast<std::uint32_t>(i));
  }
  if (!text.empty()) out.push_back(static_cast<std::uint32_t>(text.size()));
}

std::vector<std::u32string_view> split_clusters(std::u32string_view text) {
  std::vector<std::uint32_t> b;
  cluster_boundaries(text, b);
  std::vector<std::u32string_view> out;
  out.reserve(b.size());
  for (std::size_t k = 0; k + 1 < b.size(); ++k) out.push_back(text.substr(b[k], b[k + 1] - b[k]));
  return out;
}

bool is_orphan(std::u32string_view cluster) noexcept {
  if (cluster.empty()) return false;
  const CharClass c = classify(cluster.front());
  return is_mark(c) || c == CharClass::Joiner;
}

const char* to_string(ClusterIssueKind kind) noexcept {
  switch (kind) {
    case ClusterIssueKind::OrphanMark: return "orphan-mark";
    case ClusterIssueKind::DanglingCoeng: return "dangling-coeng";
    case ClusterIssueKind::TooManySubscripts: return "too-many-subscripts";
    case ClusterIssueKind::InherentVowel: return "inherent-vowel";
    case ClusterIssueKind::DuplicateMark: return "duplicate-mark";
    case ClusterIssueKind::MultipleVowels: return "multiple-vowels";
    case ClusterIssueKind::OutOfOrder: return "out-of-order";
  }
  return "unknown";
}

namespace {

// Canonical position of each mark inside a cluster.
enum Slot : int { kBase = 0, kRobat = 1, kSubscript = 2, kShifter = 3, kJoiner = 4, kVowel = 5, kSign = 6 };

void validate_one(std::u32string_view text, std::size_t begin, std::size_t end,
                  std::vector<ClusterIssue>& issues) {
  auto report = [&issues](std::size_t at, ClusterIssueKind k) {
    issues.push_back({static_cast<std::uint32_t>(at), k});
  };

  if (!is_base(classify(text[begin]))) {
    if (is_orphan(text.substr(begin, end - begin))) report(begin, ClusterIssueKind::OrphanMark);
    return;
  }

  int slot = kBase;
  int subscripts = 0;
  int vowels = 0;
  std::bitset<0x30> seen;  // marks U+17B4..U+17E3, indexed from U+17B4

  std::size_t j = begin + 1;
  while (j < end) {
    const char32_t cp = text[j];
    const CharClass c = classify(cp);
    int this_slot = slot;

    if (c == CharClass::Coeng) {
      const bool has_sub = j + 1 < end && (classify(text[j + 1]) == CharClass::Cons ||
                                           classify(text[j + 1]) == CharClass::IndV);
      if (!has_sub) {
        report(j, ClusterIssueKind::DanglingCoeng);
        ++j;
        continue;
      }
      if (++subscripts > 2) report(j, ClusterIssueKind::TooManySubscripts);
      this_slot = kSubscript;
      if (this_slot < slot) report(j, ClusterIssueKind::OutOfOrder);
      slot = std::max(slot, this_slot);
      j += 2;
      continue;
    }

    switch (c) {
      case CharClass::Inherent:
        report(j, ClusterIssueKind::InherentVowel);
        ++j;
        continue;
      case CharClass::Robat: this_slot = kRobat; break;
      case CharClass::Shifter: this_slot = kShifter; break;
      case CharClass::Joiner: this_slot = kJoiner; break;
      case CharClass::DepV: this_slot = kVowel; break;
      default: this_slot = kSign; break;
    }

    if (c != CharClass::Joiner) {
      const std::size_t bit = cp - 0x17B4;
      if (seen.test(bit)) {
        report(j, ClusterIssueKind::DuplicateMark);
        ++j;
        continue;
      }
      seen.set(bit);
    }
    if (c == CharClass::DepV && ++vowels > 1) report(j, ClusterIssueKind::MultipleVowels);
    if (this_slot < slot) report(j, ClusterIssueKind::OutOfOrder);
    slot = std::max(slot, this_slot);
    ++j;
  }
}

}  // namespace

std::vector<ClusterIssue> validate_clusters(std::u32string_view text) {
  std::vector<std::uint32_t> b;
  cluster_boundaries(text, b);
  std::vector<ClusterIssue> issues;
  for (std::size_t k = 0; k + 1 < b.size(); ++k) {
    const CharClass first = classify(text[b[k]]);
    if (is_khmer_letter(first) || first == CharClass::Joiner) validate_one(text, b[k], b[k + 1], issues);
  }
  return issues;
}

}  // namespace khseg

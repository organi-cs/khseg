#include <khseg/charclass.hpp>
#include <khseg/cluster.hpp>
#include <khseg/normalize.hpp>

#include <algorithm>
#include <vector>

namespace khseg {

namespace {

constexpr char32_t kRo = 0x179A;

// A unit is one mark, or COENG plus the letter it subscripts.
struct Unit {
  int slot;
  bool coeng_ro;
  std::u32string_view text;
};

int slot_of(CharClass c) {
  switch (c) {
    case CharClass::Robat: return 1;
    case CharClass::Coeng: return 2;
    case CharClass::Shifter: return 3;
    case CharClass::Joiner: return 4;
    case CharClass::DepV: return 5;
    default: return 6;  // Sign, Diac
  }
}

bool is_sub_letter(CharClass c) { return c == CharClass::Cons || c == CharClass::IndV; }

// True when normalize_cluster would return the cluster unchanged. Almost all
// real text is already in canonical order, so this check saves building and
// sorting the unit list.
bool already_canonical(std::u32string_view cluster) {
  if (!is_base(classify(cluster.front()))) return true;  // copied unchanged anyway
  int slot = 0;
  bool seen_ro = false;
  char32_t prev = 0;
  for (std::size_t i = 1; i < cluster.size(); ++i) {
    const char32_t cp = cluster[i];
    const CharClass c = classify(cp);
    if (c == CharClass::Inherent) return false;
    if (c == CharClass::Coeng) {
      if (i + 1 >= cluster.size() || !is_sub_letter(classify(cluster[i + 1]))) return true;
      const bool ro = cluster[i + 1] == kRo;
      if (slot > 2 || (seen_ro && !ro)) return false;
      seen_ro = seen_ro || ro;
      slot = 2;
      prev = 0;
      ++i;
      continue;
    }
    const int s = slot_of(c);
    if (s < slot || cp == prev) return false;
    slot = s;
    prev = cp;
  }
  return true;
}

}  // namespace

void normalize_cluster(std::u32string_view cluster, std::u32string& out) {
  if (cluster.empty()) return;
  if (already_canonical(cluster)) {
    out.append(cluster);
    return;
  }

  std::vector<Unit> units;
  bool leave_alone = !is_base(classify(cluster.front()));
  for (std::size_t i = 1; i < cluster.size() && !leave_alone;) {
    const CharClass c = classify(cluster[i]);
    if (c == CharClass::Inherent) {
      ++i;
      continue;
    }
    if (c == CharClass::Coeng) {
      const bool has_sub = i + 1 < cluster.size() && (classify(cluster[i + 1]) == CharClass::Cons ||
                                                      classify(cluster[i + 1]) == CharClass::IndV);
      leave_alone = !has_sub;
      if (has_sub) units.push_back({2, cluster[i + 1] == kRo, cluster.substr(i, 2)});
      i += 2;
      continue;
    }
    units.push_back({slot_of(c), false, cluster.substr(i, 1)});
    ++i;
  }

  // Orphan marks, non-Khmer clusters and clusters with a COENG that has
  // nothing to subscript are copied as they are. Moving or dropping anything
  // there could let a COENG capture the next cluster's consonant, which would
  // change the cluster count.
  if (leave_alone) {
    out.append(cluster);
    return;
  }

  std::stable_sort(units.begin(), units.end(), [](const Unit& a, const Unit& b) {
    if (a.slot != b.slot) return a.slot < b.slot;
    return !a.coeng_ro && b.coeng_ro;
  });

  out.push_back(cluster.front());
  const Unit* prev = nullptr;
  for (const Unit& u : units) {
    // Drop a single mark that directly repeats the previous one.
    if (prev && u.text.size() == 1 && prev->text == u.text) continue;
    out.append(u.text);
    prev = &u;
  }
}

std::u32string normalize(std::u32string_view text) {
  std::u32string out;
  out.reserve(text.size());
  for (auto cluster : split_clusters(text)) normalize_cluster(cluster, out);
  return out;
}

}  // namespace khseg

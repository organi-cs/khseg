#include <khseg/cluster.hpp>
#include <khseg/dictionary.hpp>
#include <khseg/pretokenize.hpp>
#include <khseg/segmenter.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace khseg {

namespace {

constexpr char32_t kLekToo = 0x17D7;

Token piece(std::uint32_t b, std::uint32_t e, TokenType type, std::uint32_t entry = kNoEntry) {
  Token t;
  t.begin = b;
  t.end = e;
  t.type = type;
  t.entry = entry;
  return t;
}

std::uint32_t next_boundary(const std::vector<std::uint8_t>& is_boundary, std::uint32_t p) {
  do ++p;
  while (!is_boundary[p]);
  return p;
}

std::uint32_t prev_boundary(const std::vector<std::uint8_t>& is_boundary, std::uint32_t p) {
  do --p;
  while (!is_boundary[p]);
  return p;
}

// Forward maximal matching: at each boundary take the longest dictionary word
// that ends on a boundary; if there is none, take one cluster as unknown.
void forward_match(std::u32string_view text, const Dictionary& dict,
                   const std::vector<std::uint8_t>& is_boundary, std::vector<Token>& out) {
  const RefTrie& trie = dict.forward();
  const auto len = static_cast<std::uint32_t>(text.size());
  std::uint32_t p = 0;
  while (p < len) {
    std::uint32_t node = RefTrie::kRoot;
    std::uint32_t best_end = 0;
    std::uint32_t best_id = kNoEntry;
    for (std::uint32_t q = p; q < len; ++q) {
      node = trie.step(node, text[q]);
      if (node == RefTrie::kNone) break;
      if (is_boundary[q + 1] && trie.value(node) != RefTrie::kNone) {
        best_end = q + 1;
        best_id = trie.value(node);
      }
    }
    if (best_id != kNoEntry) {
      out.push_back(piece(p, best_end, TokenType::Word, best_id));
      p = best_end;
    } else {
      const std::uint32_t e = next_boundary(is_boundary, p);
      out.push_back(piece(p, e, TokenType::Unknown));
      p = e;
    }
  }
}

// Backward maximal matching, the mirror image of forward_match, using the
// trie of reversed words.
void backward_match(std::u32string_view text, const Dictionary& dict,
                    const std::vector<std::uint8_t>& is_boundary, std::vector<Token>& out) {
  const RefTrie& trie = dict.backward();
  const std::size_t first = out.size();
  auto e = static_cast<std::uint32_t>(text.size());
  while (e > 0) {
    std::uint32_t node = RefTrie::kRoot;
    std::uint32_t best_start = 0;
    std::uint32_t best_id = kNoEntry;
    for (std::uint32_t q = e; q-- > 0;) {
      node = trie.step(node, text[q]);
      if (node == RefTrie::kNone) break;
      if (is_boundary[q] && trie.value(node) != RefTrie::kNone) {
        best_start = q;
        best_id = trie.value(node);
      }
    }
    if (best_id != kNoEntry) {
      out.push_back(piece(best_start, e, TokenType::Word, best_id));
      e = best_start;
    } else {
      const std::uint32_t s = prev_boundary(is_boundary, e);
      out.push_back(piece(s, e, TokenType::Unknown));
      e = s;
    }
  }
  std::reverse(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
}

// Viterbi over cluster boundaries. Edges are dictionary words that start and
// end on a boundary (cost from the dictionary) and single clusters taken as
// unknown (cost `unknown_cost`). Among paths of equal cost the one with fewer
// tokens wins; after that the first path found wins, which is the one whose
// last word is longest.
void viterbi(std::u32string_view text, const Dictionary& dict, double unknown_cost, Workspace& ws,
             std::vector<Token>& out) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  constexpr double kEps = 1e-9;
  const RefTrie& trie = dict.forward();
  const auto len = static_cast<std::uint32_t>(text.size());
  const std::vector<std::uint8_t>& is_boundary = ws.is_boundary;

  ws.best.assign(len + 1, kInf);
  ws.ntokens.assign(len + 1, 0);
  ws.back_from.assign(len + 1, 0);
  ws.back_entry.assign(len + 1, kNoEntry);
  ws.best[0] = 0.0;

  auto relax = [&ws](std::uint32_t to, std::uint32_t from, double cost, std::uint32_t entry) {
    const double c = ws.best[from] + cost;
    const std::uint32_t n = ws.ntokens[from] + 1;
    const double cur = ws.best[to];
    if (c < cur - kEps || (std::abs(c - cur) <= kEps && n < ws.ntokens[to])) {
      ws.best[to] = c;
      ws.ntokens[to] = n;
      ws.back_from[to] = from;
      ws.back_entry[to] = entry;
    }
  };

  for (std::uint32_t p = 0; p < len; ++p) {
    if (!is_boundary[p] || ws.best[p] == kInf) continue;
    std::uint32_t node = RefTrie::kRoot;
    for (std::uint32_t q = p; q < len; ++q) {
      node = trie.step(node, text[q]);
      if (node == RefTrie::kNone) break;
      const std::uint32_t id = trie.value(node);
      if (id != RefTrie::kNone && is_boundary[q + 1]) relax(q + 1, p, dict.cost(id), id);
    }
    relax(next_boundary(is_boundary, p), p, unknown_cost, kNoEntry);
  }

  const std::size_t first = out.size();
  for (std::uint32_t e = len; e > 0;) {
    const std::uint32_t b = ws.back_from[e];
    const std::uint32_t id = ws.back_entry[e];
    out.push_back(piece(b, e, id == kNoEntry ? TokenType::Unknown : TokenType::Word, id));
    e = b;
  }
  std::reverse(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
}

// Lower is better: fewer pieces, then fewer unknown clusters, then fewer
// one-cluster words.
std::tuple<std::size_t, std::size_t, std::size_t> bimm_score(
    const Token* first, const Token* last, const std::vector<std::uint8_t>& is_boundary) {
  std::size_t unknown = 0;
  std::size_t single = 0;
  for (const Token* t = first; t != last; ++t) {
    if (t->type == TokenType::Unknown) {
      ++unknown;
    } else if (next_boundary(is_boundary, t->begin) == t->end) {
      ++single;
    }
  }
  return {static_cast<std::size_t>(last - first), unknown, single};
}

}  // namespace

Segmenter::Segmenter(std::shared_ptr<const Dictionary> dict, Options options)
    : dict_(std::move(dict)), options_(options) {
  if (options_.unknown_cost) {
    unknown_cost_ = *options_.unknown_cost;
  } else if (dict_) {
    unknown_cost_ = dict_->default_unknown_cost();
  }
}

Segmenter::~Segmenter() = default;
Segmenter::Segmenter(const Segmenter&) = default;
Segmenter& Segmenter::operator=(const Segmenter&) = default;
Segmenter::Segmenter(Segmenter&&) noexcept = default;
Segmenter& Segmenter::operator=(Segmenter&&) noexcept = default;

std::vector<Token> Segmenter::segment(std::string_view utf8) const {
  Workspace ws;
  std::vector<Token> out;
  segment(utf8, ws, out);
  return out;
}

void Segmenter::segment(std::string_view utf8, Workspace& ws, std::vector<Token>& out) const {
  out.clear();
  ws.replacements = utf8::decode(utf8, ws.text, ws.byte_offsets, options_.invalid_utf8);
  pretokenize(ws.text, ws.coarse);

  for (const Token& t : ws.coarse) {
    if (t.type == TokenType::Khmer && dict_) {
      segment_run(t, ws, out);
      continue;
    }
    const bool lektoo = t.type == TokenType::Punct && t.end - t.begin == 1 &&
                        ws.text[t.begin] == kLekToo;
    if (lektoo && options_.lektoo == LekTooPolicy::Attach && !out.empty() &&
        is_khmer_token(out.back().type) && out.back().end == t.begin) {
      out.back().end = t.end;
      continue;
    }
    out.push_back(t);
  }
  for (Token& t : out) {
    t.byte_begin = ws.byte_offsets[t.begin];
    t.byte_end = ws.byte_offsets[t.end];
  }
}

void Segmenter::segment_run(const Token& run, Workspace& ws, std::vector<Token>& out) const {
  const std::u32string_view text = std::u32string_view(ws.text).substr(run.begin, run.end - run.begin);
  cluster_boundaries(text, ws.clusters);
  ws.is_boundary.assign(text.size() + 1, 0);
  for (std::uint32_t b : ws.clusters) ws.is_boundary[b] = 1;

  ws.scratch.clear();
  switch (options_.algorithm) {
    case Algorithm::Viterbi:
      viterbi(text, *dict_, unknown_cost_, ws, ws.scratch);
      break;
    case Algorithm::Forward:
      forward_match(text, *dict_, ws.is_boundary, ws.scratch);
      break;
    case Algorithm::Backward:
      backward_match(text, *dict_, ws.is_boundary, ws.scratch);
      break;
    case Algorithm::Bidirectional: {
      forward_match(text, *dict_, ws.is_boundary, ws.scratch);
      const std::size_t split = ws.scratch.size();
      backward_match(text, *dict_, ws.is_boundary, ws.scratch);
      const Token* s = ws.scratch.data();
      const auto fwd = bimm_score(s, s + split, ws.is_boundary);
      const auto bwd = bimm_score(s + split, s + ws.scratch.size(), ws.is_boundary);
      if (fwd < bwd) {
        ws.scratch.resize(split);
      } else {
        ws.scratch.erase(ws.scratch.begin(), ws.scratch.begin() + static_cast<std::ptrdiff_t>(split));
      }
      break;
    }
  }

  for (Token t : ws.scratch) {
    t.begin += run.begin;
    t.end += run.begin;
    if (options_.merge_unknown && t.type == TokenType::Unknown && !out.empty() &&
        out.back().type == TokenType::Unknown && out.back().end == t.begin) {
      out.back().end = t.end;
      continue;
    }
    out.push_back(t);
  }
}

std::vector<std::string> Segmenter::words(std::string_view utf8) const {
  std::vector<std::string> result;
  for (const Token& t : segment(utf8)) {
    if (t.type == TokenType::Space) continue;
    result.emplace_back(utf8.substr(t.byte_begin, t.byte_end - t.byte_begin));
  }
  return result;
}

}  // namespace khseg

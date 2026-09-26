#include <gtest/gtest.h>
#include <khseg/dictionary.hpp>
#include <khseg/segmenter.hpp>
#include <khseg/trie.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using khseg::Dictionary;
using khseg::DoubleArrayTrie;
using khseg::RefTrie;

namespace {

// Random Khmer strings, biased towards a small alphabet so that keys share
// prefixes and the trie has real branching.
std::u32string random_word(std::mt19937& rng, std::size_t max_len) {
  std::uniform_int_distribution<std::size_t> len(1, max_len);
  std::uniform_int_distribution<int> pick(0, 99);
  std::u32string w;
  const std::size_t n = len(rng);
  for (std::size_t i = 0; i < n; ++i) {
    const int r = pick(rng);
    if (r < 60) {
      w.push_back(static_cast<char32_t>(0x1780 + r % 12));
    } else if (r < 95) {
      w.push_back(static_cast<char32_t>(0x1780 + (r * 7) % 128));
    } else {
      w.push_back(r % 2 ? char32_t{0x200C} : char32_t{0x200D});
    }
  }
  return w;
}

// All (length, value) pairs of keys that are prefixes of `s`.
template <class Trie>
std::vector<std::pair<std::size_t, std::uint32_t>> prefixes(const Trie& t, std::u32string_view s) {
  std::vector<std::pair<std::size_t, std::uint32_t>> out;
  std::uint32_t node = Trie::kRoot;
  for (std::size_t i = 0; i < s.size(); ++i) {
    node = t.step(node, s[i]);
    if (node == Trie::kNone) break;
    if (t.value(node) != Trie::kNone) out.emplace_back(i + 1, t.value(node));
  }
  return out;
}

}  // namespace

TEST(DoubleArrayTrie, EmptyTrie) {
  const DoubleArrayTrie t = DoubleArrayTrie::build(RefTrie());
  EXPECT_EQ(t.find(U"ក"), DoubleArrayTrie::kNone);
  EXPECT_EQ(t.step(DoubleArrayTrie::kRoot, U'ក'), DoubleArrayTrie::kNone);
}

TEST(DoubleArrayTrie, RejectsOutOfAlphabetKeys) {
  RefTrie r;
  r.insert(U"a", 0);
  EXPECT_THROW(DoubleArrayTrie::build(r), std::invalid_argument);
}

TEST(DoubleArrayTrie, NonKhmerHasNoTransition) {
  RefTrie r;
  r.insert(U"ក", 5);
  const auto t = DoubleArrayTrie::build(r);
  EXPECT_EQ(t.find(U"ក"), 5u);
  EXPECT_EQ(t.step(DoubleArrayTrie::kRoot, U'a'), DoubleArrayTrie::kNone);
  EXPECT_EQ(t.step(DoubleArrayTrie::kRoot, U'ᢀ'), DoubleArrayTrie::kNone);
}

// Property test: the double-array trie answers every prefix query exactly
// like the reference trie it was built from.
TEST(DoubleArrayTrie, MatchesReferenceTrie) {
  std::mt19937 rng(2026);
  RefTrie ref;
  std::vector<std::u32string> keys;
  for (std::uint32_t i = 0; i < 5000; ++i) {
    keys.push_back(random_word(rng, 10));
    ref.insert(keys.back(), i);
  }
  const auto dat = DoubleArrayTrie::build(ref);

  for (const auto& k : keys) ASSERT_EQ(dat.find(k), ref.find(k));
  for (int i = 0; i < 100000; ++i) {
    const auto q = random_word(rng, 14);
    ASSERT_EQ(prefixes(dat, q), prefixes(ref, q)) << i;
  }
}

TEST(DoubleArrayTrie, AssignChecksShapeOnly) {
  DoubleArrayTrie t;
  EXPECT_FALSE(t.assign({0, 0}, {DoubleArrayTrie::kNone}, {0, 0}));
  EXPECT_FALSE(t.assign({0}, {0}, {0}));  // check[0] must be kNone
  // base[0] = 0 and check[1] = 0: KA (symbol 1) leads from the root to slot 1.
  EXPECT_TRUE(t.assign({0, 0}, {DoubleArrayTrie::kNone, 0}, {DoubleArrayTrie::kNone, 3}));
  EXPECT_EQ(t.find(U"ក"), 3u);
  EXPECT_EQ(t.find(U"ខ"), DoubleArrayTrie::kNone);

  // Nonsense contents are accepted, and lookups still stay inside the arrays.
  EXPECT_TRUE(t.assign({4000000000u, 7, 1}, {DoubleArrayTrie::kNone, 7, 0}, {1, 2, 3}));
  for (char32_t c = 0x1780; c <= 0x17FF; ++c) {
    const auto n = t.step(DoubleArrayTrie::kRoot, c);
    if (n != DoubleArrayTrie::kNone) {
      EXPECT_LT(n, t.size());
      t.step(n, c);
    }
  }
}

TEST(BinaryDictionary, RoundTrip) {
  const auto d = Dictionary::from_tsv_file(KHSEG_SOURCE_DIR "/data/sample/dict.tsv");
  const auto bytes = d.to_binary();
  const auto e = Dictionary::from_binary(bytes);
  ASSERT_EQ(d.size(), e.size());
  for (std::uint32_t i = 0; i < d.size(); ++i) {
    EXPECT_EQ(d.word(i), e.word(i));
    EXPECT_EQ(d.count(i), e.count(i));
    EXPECT_EQ(d.cost(i), e.cost(i));
    EXPECT_EQ(e.find(d.word(i)), i);
  }
  EXPECT_EQ(d.max_cost(), e.max_cost());
  EXPECT_EQ(d.total_count(), e.total_count());
  EXPECT_EQ(d.default_unknown_cost(), e.default_unknown_cost());
  EXPECT_EQ(e.to_binary(), bytes);
}

TEST(BinaryDictionary, KeepsUnknownCostAndSegmentsTheSame) {
  khseg::DictionaryOptions o;
  o.unknown_cost = 9.5;
  const auto d = std::make_shared<const Dictionary>(
      Dictionary::from_tsv_file(KHSEG_SOURCE_DIR "/data/sample/dict.tsv", nullptr, o));
  const auto e = std::make_shared<const Dictionary>(Dictionary::from_binary(d->to_binary()));
  EXPECT_EQ(e->default_unknown_cost(), 9.5);

  std::ifstream raw(KHSEG_SOURCE_DIR "/data/sample/raw.txt", std::ios::binary);
  const khseg::Segmenter a(d), b(e);
  for (std::string line; std::getline(raw, line);) EXPECT_EQ(a.segment(line), b.segment(line));
}

TEST(BinaryDictionary, DetectsCorruption) {
  const auto bytes = Dictionary::from_words({{U"ក", 2}, {U"ខ", 3}}).to_binary();

  std::string flipped = bytes;
  flipped[flipped.size() / 2] ^= 0x10;
  EXPECT_THROW(Dictionary::from_binary(flipped), std::runtime_error);
  EXPECT_THROW(Dictionary::from_binary(bytes.substr(0, bytes.size() - 9)), std::runtime_error);
  EXPECT_THROW(Dictionary::from_binary("not a dictionary"), std::runtime_error);

  std::string wrong_version = bytes;
  wrong_version[8] = 9;
  EXPECT_THROW(Dictionary::from_binary(wrong_version), std::runtime_error);
}

namespace {

// A .khd file in the temp directory, removed at the end of the test. Any
// mapping of it must be gone by then (Windows refuses to delete mapped files).
struct TempKhd {
  std::filesystem::path path;
  explicit TempKhd(const char* name) : path(std::filesystem::temp_directory_path() / name) {}
  ~TempKhd() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
};

Dictionary sample() { return Dictionary::from_tsv_file(KHSEG_SOURCE_DIR "/data/sample/dict.tsv"); }

}  // namespace

TEST(BinaryDictionary, FromFileDetectsFormat) {
  TempKhd khd("khseg_test_detect.khd");
  const auto d = sample();
  d.save_binary(khd.path);
  {
    khseg::LoadReport report;
    const auto e = Dictionary::from_file(khd.path, &report);
    EXPECT_TRUE(e.is_mapped());
    EXPECT_EQ(e.size(), d.size());
    EXPECT_EQ(report.entries, d.size());
  }
  const auto f = Dictionary::from_file(KHSEG_SOURCE_DIR "/data/sample/dict.tsv");
  EXPECT_FALSE(f.is_mapped());
  EXPECT_EQ(f.size(), d.size());
}

TEST(MappedDictionary, SameAsHeapCopy) {
  TempKhd khd("khseg_test_mapped.khd");
  const auto d = sample();
  d.save_binary(khd.path);
  for (bool verify : {true, false}) {
    const auto m = Dictionary::map_file(khd.path, verify);
    ASSERT_TRUE(m.is_mapped());
    ASSERT_EQ(m.size(), d.size());
    for (std::uint32_t i = 0; i < d.size(); ++i) {
      EXPECT_EQ(m.word(i), d.word(i));
      EXPECT_EQ(m.cost(i), d.cost(i));
      EXPECT_EQ(m.find(d.word(i)), i);
    }
    EXPECT_EQ(m.to_binary(), d.to_binary());
  }
}

TEST(MappedDictionary, CopiesKeepTheMappingAlive) {
  TempKhd khd("khseg_test_alive.khd");
  sample().save_binary(khd.path);
  std::ifstream raw(KHSEG_SOURCE_DIR "/data/sample/raw.txt", std::ios::binary);
  std::string line;
  std::getline(raw, line);

  std::vector<khseg::Token> expected;
  {
    auto heap = std::make_shared<const Dictionary>(sample());
    expected = khseg::Segmenter(heap).segment(line);
  }
  std::shared_ptr<const Dictionary> copy;
  {
    const Dictionary original = Dictionary::map_file(khd.path);
    copy = std::make_shared<const Dictionary>(original);
  }  // the original is gone; the copy still holds the mapping
  EXPECT_EQ(khseg::Segmenter(copy).segment(line), expected);
}

TEST(MappedDictionary, EmptyDictionary) {
  TempKhd khd("khseg_test_empty.khd");
  Dictionary::from_words({}).save_binary(khd.path);
  const auto m = Dictionary::map_file(khd.path);
  EXPECT_EQ(m.size(), 0u);
  EXPECT_EQ(m.find(U"ក"), khseg::kNoEntry);
}

TEST(MappedDictionary, Errors) {
  EXPECT_THROW(Dictionary::map_file("no/such/file.khd"), std::runtime_error);
  TempKhd khd("khseg_test_errors.khd");
  { std::ofstream(khd.path, std::ios::binary) << "not a dictionary"; }
  EXPECT_THROW(Dictionary::map_file(khd.path), std::runtime_error);
}

// Without the checksum, damage that would make lookups unsafe must still be
// caught: here a trie value that is not a valid word id.
TEST(MappedDictionary, StructuralChecksWithoutChecksum) {
  std::string bytes = sample().to_binary();
  // Section 6 is the forward trie's value array; its offset is in the table
  // that starts at byte 72 (see src/dictionary_io.cpp).
  std::uint64_t offset = 0;
  std::memcpy(&offset, bytes.data() + 72 + 6 * 16, 8);
  const std::uint32_t bad = 0x7FFFFFFF;
  std::memcpy(bytes.data() + offset, &bad, 4);
  EXPECT_THROW(Dictionary::from_binary(bytes, false), std::runtime_error);
  EXPECT_THROW(Dictionary::from_binary(bytes, true), std::runtime_error);

  // A section table entry that points past the end of the file.
  std::string past = sample().to_binary();
  const std::uint64_t huge = past.size() * 2;
  std::memcpy(past.data() + 72 + 1 * 16, &huge, 8);
  EXPECT_THROW(Dictionary::from_binary(past, false), std::runtime_error);
}

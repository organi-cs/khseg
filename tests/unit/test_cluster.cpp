#include <gtest/gtest.h>
#include <khseg/cluster.hpp>
#include <khseg/utf8.hpp>

#include <string>
#include <vector>

using khseg::ClusterIssueKind;

namespace {

std::vector<std::u32string> split(std::u32string_view s) {
  std::vector<std::u32string> out;
  for (auto v : khseg::split_clusters(s)) out.emplace_back(v);
  return out;
}

// Readable failure messages: gtest prints UTF-8 strings, not u32strings.
std::vector<std::string> utf8_list(const std::vector<std::u32string>& v) {
  std::vector<std::string> out;
  for (const auto& s : v) out.push_back(khseg::utf8::encode(s));
  return out;
}

struct SplitCase {
  const char* name;
  std::u32string text;
  std::vector<std::u32string> clusters;
};

class ClusterSplit : public ::testing::TestWithParam<SplitCase> {};

}  // namespace

TEST_P(ClusterSplit, Splits) {
  const SplitCase& c = GetParam();
  EXPECT_EQ(utf8_list(split(c.text)), utf8_list(c.clusters));
}

// Source uses escapes so that editors cannot reorder or normalize the text.
INSTANTIATE_TEST_SUITE_P(
    Cases, ClusterSplit,
    ::testing::Values(
        // khmer: KHA COENG MO AE | RO
        SplitCase{"Khmer", U"ខ្មែរ",
                  {U"ខ្មែ", U"រ"}},
        // strei: SA COENG TA COENG RO II, two subscripts in one cluster
        SplitCase{"TwoSubscripts", U"ស្ត្រី",
                  {U"ស្ត្រី"}},
        // kampuchea: KA | MO COENG PO U | CO AA
        SplitCase{"Kampuchea", U"កម្ពុជា",
                  {U"ក", U"ម្ពុ", U"ជា"}},
        // nyam: NYO MUUSIKATOAN AA NIKAHIT
        SplitCase{"ShifterVowelSign", U"ញ៉ាំ", {U"ញ៉ាំ"}},
        // thor: THO | MO ROBAT
        SplitCase{"Robat", U"ធម៌", {U"ធ", U"ម៌"}},
        SplitCase{"OrphanAtStart", U"ាក", {U"ា", U"ក"}},
        SplitCase{"OrphanRunAfterLatin", U"aាំ", {U"a", U"ាំ"}},
        SplitCase{"DanglingCoengAtEnd", U"ក្", {U"ក្"}},
        SplitCase{"CoengIndependentVowel", U"ក្ឥ", {U"ក្ឥ"}},
        SplitCase{"ZwjInside", U"ក‍ា", {U"ក‍ា"}},
        SplitCase{"ZwspSeparates", U"ក​ខ", {U"ក", U"​", U"ខ"}},
        SplitCase{"DigitsAreSingles", U"១២", {U"១", U"២"}},
        SplitCase{"Mixed", U"abកា 1", {U"a", U"b", U"កា", U" ", U"1"}},
        SplitCase{"Empty", U"", {}}),
    [](const auto& p) { return std::string(p.param.name); });

TEST(Cluster, WorkedExampleBoundaries) {
  // "I love the country of Cambodia", the README example.
  const std::u32string s = khseg::utf8::decode(
      "\xE1\x9E\x81\xE1\x9F\x92\xE1\x9E\x89\xE1\x9E\xBB\xE1\x9F\x86"              // khnhom
      "\xE1\x9E\x9F\xE1\x9F\x92\xE1\x9E\x9A\xE1\x9E\x9B\xE1\x9E\xB6\xE1\x9E\x89"  // srolanh
      "\xE1\x9F\x8B"
      "\xE1\x9E\x94\xE1\x9F\x92\xE1\x9E\x9A\xE1\x9E\x91\xE1\x9F\x81\xE1\x9E\x9F"  // brates
      "\xE1\x9E\x80\xE1\x9E\x98\xE1\x9F\x92\xE1\x9E\x96\xE1\x9E\xBB\xE1\x9E\x87"  // kampuchea
      "\xE1\x9E\xB6");
  ASSERT_EQ(s.size(), 25u);
  std::vector<std::uint32_t> b;
  khseg::cluster_boundaries(s, b);
  EXPECT_EQ(b, (std::vector<std::uint32_t>{0, 5, 8, 10, 12, 15, 17, 18, 19, 23, 25}));
  EXPECT_TRUE(khseg::validate_clusters(s).empty());
}

TEST(Cluster, EmptyBoundaries) {
  std::vector<std::uint32_t> b{7, 8};
  khseg::cluster_boundaries(U"", b);
  EXPECT_EQ(b, (std::vector<std::uint32_t>{0}));
}

TEST(Cluster, OrphanDetection) {
  EXPECT_TRUE(khseg::is_orphan(U"ា"));
  EXPECT_FALSE(khseg::is_orphan(U"កា"));
  EXPECT_FALSE(khseg::is_orphan(U"a"));
}

TEST(Cluster, PartitionInvariant) {
  // Every code point lands in exactly one cluster, whatever the input.
  std::u32string s;
  for (char32_t cp = 0x1780; cp <= 0x17FF; ++cp) s.push_back(cp);
  s += U"abc​‌‍្្";
  std::u32string joined;
  for (auto c : khseg::split_clusters(s)) {
    EXPECT_FALSE(c.empty());
    joined += c;
  }
  EXPECT_EQ(joined, s);
}

namespace {

struct IssueCase {
  const char* name;
  std::u32string text;
  std::vector<std::pair<std::uint32_t, ClusterIssueKind>> issues;
};

class ClusterValidate : public ::testing::TestWithParam<IssueCase> {};

}  // namespace

TEST_P(ClusterValidate, Reports) {
  const IssueCase& c = GetParam();
  std::vector<std::pair<std::uint32_t, ClusterIssueKind>> got;
  for (const auto& i : khseg::validate_clusters(c.text)) got.emplace_back(i.offset, i.kind);
  EXPECT_EQ(got, c.issues);
}

INSTANTIATE_TEST_SUITE_P(
    Cases, ClusterValidate,
    ::testing::Values(
        IssueCase{"WellFormed", U"ខ្មែរញ៉ាំ", {}},
        IssueCase{"NonKhmerIgnored", U"abc 123", {}},
        IssueCase{"VowelBeforeCoeng", U"ខែ្មរ",
                  {{2, ClusterIssueKind::OutOfOrder}}},
        IssueCase{"ShifterAfterSign", U"ញាំ៉",
                  {{3, ClusterIssueKind::OutOfOrder}}},
        IssueCase{"ThreeSubscripts", U"ក្ក្ក្ក",
                  {{5, ClusterIssueKind::TooManySubscripts}}},
        IssueCase{"DanglingCoeng", U"ក្", {{1, ClusterIssueKind::DanglingCoeng}}},
        IssueCase{"CoengBeforeVowel", U"ក្ា",
                  {{1, ClusterIssueKind::DanglingCoeng}}},
        IssueCase{"Inherent", U"ក឴", {{1, ClusterIssueKind::InherentVowel}}},
        IssueCase{"Duplicate", U"កាា", {{2, ClusterIssueKind::DuplicateMark}}},
        IssueCase{"TwoVowels", U"កាី", {{2, ClusterIssueKind::MultipleVowels}}},
        IssueCase{"Orphan", U"ា", {{0, ClusterIssueKind::OrphanMark}}}),
    [](const auto& p) { return std::string(p.param.name); });

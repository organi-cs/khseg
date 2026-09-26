#include <gtest/gtest.h>
#include <khseg/cluster.hpp>
#include <khseg/dictionary.hpp>
#include <khseg/normalize.hpp>
#include <khseg/segmenter.hpp>
#include <khseg/utf8.hpp>

#include <memory>
#include <random>
#include <string>

using khseg::normalize;

namespace {
std::string u8(std::u32string_view s) { return khseg::utf8::encode(s); }
}  // namespace

TEST(Normalize, VowelTypedBeforeSubscript) {
  // KHA AE COENG MO RO -> KHA COENG MO AE RO
  EXPECT_EQ(u8(normalize(U"ខែ្មរ")),
            u8(U"ខ្មែរ"));
}

TEST(Normalize, ShifterBeforeVowelAndSign) {
  // NYO AA NIKAHIT MUUSIKATOAN -> NYO MUUSIKATOAN AA NIKAHIT
  EXPECT_EQ(u8(normalize(U"ញាំ៉")), u8(U"ញ៉ាំ"));
}

TEST(Normalize, SubscriptRoGoesLast) {
  // SA COENG RO COENG TA II -> SA COENG TA COENG RO II
  EXPECT_EQ(u8(normalize(U"ស្រ្តី")),
            u8(U"ស្ត្រី"));
}

TEST(Normalize, RobatBeforeSubscripts) {
  EXPECT_EQ(u8(normalize(U"ក្ក៌")), u8(U"ក៌្ក"));
}

TEST(Normalize, DropsInherentVowelsAndDuplicates) {
  EXPECT_EQ(u8(normalize(U"ក឴ា")), u8(U"កា"));
  EXPECT_EQ(u8(normalize(U"កាា")), u8(U"កា"));
  EXPECT_EQ(u8(normalize(U"កំាំ")), u8(U"កាំ"));
}

TEST(Normalize, LeavesCanonicalAndOtherTextAlone) {
  const std::u32string ok = U"ខ្មែរ abc ១២ ា";
  EXPECT_EQ(normalize(ok), ok);
  // COENG with nothing to subscript: the cluster is copied unchanged.
  EXPECT_EQ(normalize(U"ក្៌ខ"), U"ក្៌ខ");
  EXPECT_EQ(normalize(U""), U"");
}

TEST(Normalize, PropertiesOnRandomText) {
  std::mt19937 rng(99);
  std::uniform_int_distribution<int> cp(0x1780, 0x17DD);
  std::uniform_int_distribution<int> len(0, 30);
  for (int n = 0; n < 5000; ++n) {
    std::u32string s;
    for (int i = len(rng); i > 0; --i) s.push_back(static_cast<char32_t>(cp(rng)));
    const auto once = normalize(s);
    EXPECT_EQ(khseg::split_clusters(once).size(), khseg::split_clusters(s).size()) << u8(s);
    EXPECT_EQ(normalize(once), once) << u8(s);
  }
}

TEST(Normalize, DictionaryAndSegmenterAgree) {
  // The dictionary holds the canonical spelling; text uses the misordered one.
  const std::u32string canonical = U"ខ្មែរ";
  const std::u32string typed = U"ខែ្មរ";
  auto d = std::make_shared<const khseg::Dictionary>(
      khseg::Dictionary::from_words({{typed, 3}, {U"ក", 1}}));
  EXPECT_EQ(d->size(), 2u);
  EXPECT_NE(d->find(canonical), khseg::kNoEntry);

  const std::string text = u8(U"ក" + typed + U"ក");
  khseg::Segmenter on(d);
  const auto toks = on.segment(text);
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[1].type, khseg::TokenType::Word);
  // Offsets point into the original text, not the normalized copy.
  EXPECT_EQ(toks[1].begin, 1u);
  EXPECT_EQ(toks[1].end, 6u);
  EXPECT_EQ(text.substr(toks[1].byte_begin, toks[1].byte_end - toks[1].byte_begin), u8(typed));

  khseg::Options o;
  o.normalize = false;
  const auto raw = khseg::Segmenter(d, o).segment(text);
  EXPECT_EQ(raw[1].type, khseg::TokenType::Unknown);
}

TEST(Normalize, InherentVowelShortensClusterButOffsetsHold) {
  // KA INHERENT-AQ KHA: normalized "KA KHA" is shorter; offsets must still be
  // in the original.
  auto d = std::make_shared<const khseg::Dictionary>(
      khseg::Dictionary::from_words({{U"ក", 1}, {U"ខ", 1}}));
  const std::string text = u8(U"ក឴ខ");
  const auto toks = khseg::Segmenter(d).segment(text);
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].begin, 0u);
  EXPECT_EQ(toks[0].end, 2u);
  EXPECT_EQ(toks[1].begin, 2u);
  EXPECT_EQ(toks[1].end, 3u);
  EXPECT_EQ(toks[0].type, khseg::TokenType::Word);
}

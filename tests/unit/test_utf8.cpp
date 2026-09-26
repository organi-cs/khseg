#include <gtest/gtest.h>
#include <khseg/utf8.hpp>

#include <string>

using khseg::utf8::decode;
using khseg::utf8::DecodeError;
using khseg::utf8::encode;
using khseg::utf8::ErrorPolicy;

namespace {

std::size_t count_replacements(const std::u32string& s) {
  std::size_t n = 0;
  for (char32_t c : s) n += c == khseg::utf8::kReplacement;
  return n;
}

}  // namespace

TEST(Utf8, DecodesAllLengths) {
  EXPECT_EQ(decode("A"), U"A");
  EXPECT_EQ(decode("\xC3\xA9"), U"é");
  EXPECT_EQ(decode("\xE1\x9E\x80"), U"ក");
  EXPECT_EQ(decode("\xF0\x9F\x98\x80"), U"\U0001F600");
}

TEST(Utf8, ByteOffsets) {
  std::u32string out;
  std::vector<std::uint32_t> off;
  decode("a\xE1\x9E\x80" "b", out, off);
  ASSERT_EQ(out.size(), 3u);
  EXPECT_EQ(off, (std::vector<std::uint32_t>{0, 1, 4, 5}));
}

TEST(Utf8, EmptyInput) {
  std::u32string out;
  std::vector<std::uint32_t> off;
  EXPECT_EQ(decode("", out, off), 0u);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(off, (std::vector<std::uint32_t>{0}));
}

struct BadCase {
  const char* name;
  std::string bytes;
  std::size_t replacements;
  std::size_t code_points;
};

class Utf8Invalid : public ::testing::TestWithParam<BadCase> {};

TEST_P(Utf8Invalid, MaximalSubpartReplacement) {
  const BadCase& c = GetParam();
  std::u32string out;
  std::vector<std::uint32_t> off;
  EXPECT_EQ(decode(c.bytes, out, off), c.replacements);
  EXPECT_EQ(count_replacements(out), c.replacements);
  EXPECT_EQ(out.size(), c.code_points);
  EXPECT_EQ(off.back(), c.bytes.size());
  EXPECT_THROW(decode(c.bytes, ErrorPolicy::Throw), DecodeError);
}

// Expected counts follow the "maximal subpart" practice in Unicode ch. 3.9.
INSTANTIATE_TEST_SUITE_P(
    Cases, Utf8Invalid,
    ::testing::Values(BadCase{"LoneContinuation", "\x80", 1, 1},
                      BadCase{"OverlongTwoByte", "\xC0\x80", 2, 2},
                      BadCase{"OverlongThreeByte", "\xE0\x80\x80", 3, 3},
                      BadCase{"Surrogate", "\xED\xA0\x80", 3, 3},
                      BadCase{"AboveMax", "\xF4\x90\x80\x80", 4, 4},
                      BadCase{"F5Lead", "\xF5\x80", 2, 2},
                      BadCase{"TruncatedAtEnd", "\xE1\x9E", 1, 1},
                      BadCase{"TruncatedBeforeAscii", "\xE1\x9E" "A", 1, 2},
                      BadCase{"TruncatedFourByte", "\xF0\x9F\x98" "x", 1, 2}),
    [](const auto& p) { return std::string(p.param.name); });

TEST(Utf8, ThrowReportsOffset) {
  try {
    decode("ab\xFF", ErrorPolicy::Throw);
    FAIL();
  } catch (const DecodeError& e) {
    EXPECT_EQ(e.byte_offset(), 2u);
  }
}

TEST(Utf8, RoundTrip) {
  const std::string s = "\xE1\x9E\x81\xE1\x9F\x92\xE1\x9E\x98\xE1\x9F\x82\xE1\x9E\x9A abc \xF0\x9F\x98\x80";
  EXPECT_EQ(encode(decode(s)), s);
}

TEST(Utf8, EncodeReplacesInvalidScalars) {
  EXPECT_EQ(encode(std::u32string(1, char32_t{0xD800})), "\xEF\xBF\xBD");
  EXPECT_EQ(encode(std::u32string(1, char32_t{0x110000})), "\xEF\xBF\xBD");
}

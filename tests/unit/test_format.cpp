#include <gtest/gtest.h>
#include <khseg/format.hpp>
#include <khseg/segmenter.hpp>

#include <string>
#include <tuple>
#include <vector>

using khseg::Token;
using khseg::TokenType;

namespace {

// Builds tokens over ASCII text so that code point and byte offsets agree.
std::vector<Token> toks(std::initializer_list<std::tuple<std::uint32_t, std::uint32_t, TokenType>> spec) {
  std::vector<Token> out;
  for (const auto& [b, e, t] : spec) {
    Token tok;
    tok.begin = tok.byte_begin = b;
    tok.end = tok.byte_end = e;
    tok.type = t;
    out.push_back(tok);
  }
  return out;
}

const std::string kZ = "\xE2\x80\x8B";

}  // namespace

TEST(Format, SeparatedDropsSpaces) {
  const std::string src = "ab cd.";
  auto t = toks({{0, 1, TokenType::Word}, {1, 2, TokenType::Word}, {2, 3, TokenType::Space},
                 {3, 5, TokenType::Latin}, {5, 6, TokenType::Punct}});
  std::string out;
  khseg::write_separated(src, t, " ", out);
  EXPECT_EQ(out, "a b cd .");
  out.clear();
  khseg::write_separated(src, t, "|", out);
  EXPECT_EQ(out, "a|b|cd|.");
}

TEST(Format, ZwspOnlyBetweenKhmerTokens) {
  const std::string src = "abc d1.";
  auto t = toks({{0, 1, TokenType::Word}, {1, 2, TokenType::Unknown}, {2, 3, TokenType::Word},
                 {3, 4, TokenType::Space}, {4, 5, TokenType::Word}, {5, 6, TokenType::Number},
                 {6, 7, TokenType::Punct}});
  std::string out;
  khseg::write_zwsp(src, t, out);
  EXPECT_EQ(out, "a" + kZ + "b" + kZ + "c d1.");
}

TEST(Format, ZwspIsIdempotent) {
  // Real pipeline, no dictionary: Khmer runs are single tokens, so feed text
  // that already has a ZWSP and check nothing is added next to it.
  khseg::Segmenter seg;
  const std::string once = "\xE1\x9E\x80" + kZ + "\xE1\x9E\x81";
  khseg::Workspace ws;
  std::vector<Token> t;
  seg.segment(once, ws, t);
  std::string out;
  khseg::write_zwsp(once, t, out);
  EXPECT_EQ(out, once);
}

TEST(Format, JsonEscaping) {
  std::string out;
  khseg::append_json_string(out, "a\"b\\c\n\t\x01 \xE2\x80\xA8");
  const std::string expected = std::string("\"a") + '\\' + "\"b" + '\\' + '\\' + "c" + '\\' +
                               "n" + '\\' + "t" + '\\' + "u0001 " + '\\' + "u2028\"";
  EXPECT_EQ(out, expected);
}

TEST(Format, JsonOffsets) {
  khseg::Segmenter seg;
  const std::string src = "\xE1\x9E\x80 a";  // KA, space, a
  const auto t = seg.segment(src);
  std::string cp, bytes;
  khseg::write_json(src, t, 7, khseg::OffsetUnit::CodePoints, cp);
  khseg::write_json(src, t, 7, khseg::OffsetUnit::Bytes, bytes);
  EXPECT_EQ(cp,
            "{\"line\":7,\"tokens\":["
            "{\"text\":\"\xE1\x9E\x80\",\"start\":0,\"end\":1,\"type\":\"khmer\"},"
            "{\"text\":\" \",\"start\":1,\"end\":2,\"type\":\"space\"},"
            "{\"text\":\"a\",\"start\":2,\"end\":3,\"type\":\"latin\"}]}");
  EXPECT_EQ(bytes,
            "{\"line\":7,\"tokens\":["
            "{\"text\":\"\xE1\x9E\x80\",\"start\":0,\"end\":3,\"type\":\"khmer\"},"
            "{\"text\":\" \",\"start\":3,\"end\":4,\"type\":\"space\"},"
            "{\"text\":\"a\",\"start\":4,\"end\":5,\"type\":\"latin\"}]}");
}

TEST(Format, Clusters) {
  khseg::Segmenter seg;
  // KHA COENG MO AE RO, space, 12
  const std::string src = "\xE1\x9E\x81\xE1\x9F\x92\xE1\x9E\x98\xE1\x9F\x82\xE1\x9E\x9A 12";
  khseg::Workspace ws;
  std::vector<Token> t;
  seg.segment(src, ws, t);
  std::string out;
  khseg::write_clusters(src, ws.text, ws.byte_offsets, t, out);
  EXPECT_EQ(out, "\xE1\x9E\x81\xE1\x9F\x92\xE1\x9E\x98\xE1\x9F\x82\xC2\xB7\xE1\x9E\x9A 12");
}

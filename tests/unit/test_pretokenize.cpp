#include <gtest/gtest.h>
#include <khseg/pretokenize.hpp>
#include <khseg/utf8.hpp>

#include <string>
#include <utility>
#include <vector>

using khseg::TokenType;

namespace {

using Pieces = std::vector<std::pair<std::string, TokenType>>;

Pieces run(std::u32string_view text) {
  std::vector<khseg::Token> toks;
  khseg::pretokenize(text, toks);
  Pieces out;
  for (const auto& t : toks) {
    out.emplace_back(khseg::utf8::encode(text.substr(t.begin, t.end - t.begin)), t.type);
  }
  return out;
}

std::string u8(std::u32string_view s) { return khseg::utf8::encode(s); }

}  // namespace

TEST(Pretokenize, Empty) { EXPECT_TRUE(run(U"").empty()); }

TEST(Pretokenize, KhmerRunsAndSpaces) {
  EXPECT_EQ(run(U"ខ្មែរ កា"),
            (Pieces{{u8(U"ខ្មែរ"), TokenType::Khmer},
                    {" ", TokenType::Space},
                    {u8(U"កា"), TokenType::Khmer}}));
}

TEST(Pretokenize, KhmerPunctuationIsSeparate) {
  EXPECT_EQ(run(U"ក។ខ៕"),
            (Pieces{{u8(U"ក"), TokenType::Khmer},
                    {u8(U"។"), TokenType::Punct},
                    {u8(U"ខ"), TokenType::Khmer},
                    {u8(U"៕"), TokenType::Punct}}));
}

TEST(Pretokenize, LekTooIsPunct) {
  EXPECT_EQ(run(U"កៗ"),
            (Pieces{{u8(U"ក"), TokenType::Khmer}, {u8(U"ៗ"), TokenType::Punct}}));
}

TEST(Pretokenize, Numbers) {
  EXPECT_EQ(run(U"12,000.50"), (Pieces{{"12,000.50", TokenType::Number}}));
  EXPECT_EQ(run(U"10:30"), (Pieces{{"10:30", TokenType::Number}}));
  EXPECT_EQ(run(U"១២,០០០"),
            (Pieces{{u8(U"១២,០០០"), TokenType::Number}}));
  // A separator not followed by a digit ends the number.
  EXPECT_EQ(run(U"5."), (Pieces{{"5", TokenType::Number}, {".", TokenType::Punct}}));
  EXPECT_EQ(run(U"1, 2"), (Pieces{{"1", TokenType::Number},
                                  {",", TokenType::Punct},
                                  {" ", TokenType::Space},
                                  {"2", TokenType::Number}}));
}

TEST(Pretokenize, NumberNextToKhmer) {
  EXPECT_EQ(run(U"២០២៦ឆ្នាំ"),
            (Pieces{{u8(U"២០២៦"), TokenType::Number},
                    {u8(U"ឆ្នាំ"), TokenType::Khmer}}));
}

TEST(Pretokenize, Latin) {
  EXPECT_EQ(run(U"don't COVID19 e-mail"), (Pieces{{"don't", TokenType::Latin},
                                                  {" ", TokenType::Space},
                                                  {"COVID19", TokenType::Latin},
                                                  {" ", TokenType::Space},
                                                  {"e-mail", TokenType::Latin}}));
  EXPECT_EQ(run(U"café"), (Pieces{{u8(U"café"), TokenType::Latin}}));
  EXPECT_EQ(run(U"café"), (Pieces{{u8(U"café"), TokenType::Latin}}));
  EXPECT_EQ(run(U"ok-"), (Pieces{{"ok", TokenType::Latin}, {"-", TokenType::Punct}}));
}

TEST(Pretokenize, PunctuationRuns) {
  EXPECT_EQ(run(U"...!?"), (Pieces{{"...", TokenType::Punct},
                                   {"!", TokenType::Punct},
                                   {"?", TokenType::Punct}}));
}

TEST(Pretokenize, Symbols) {
  EXPECT_EQ(run(U"៛$"),
            (Pieces{{u8(U"៛"), TokenType::Symbol}, {"$", TokenType::Symbol}}));
}

TEST(Pretokenize, ZwspRunsAreSeparateFromSpaces) {
  EXPECT_EQ(run(U"ក​​ ខ"),
            (Pieces{{u8(U"ក"), TokenType::Khmer},
                    {u8(U"​​"), TokenType::Space},
                    {" ", TokenType::Space},
                    {u8(U"ខ"), TokenType::Khmer}}));
}

TEST(Pretokenize, JoinerInsideKhmer) {
  EXPECT_EQ(run(U"ក‌ា"), (Pieces{{u8(U"ក‌ា"), TokenType::Khmer}}));
}

TEST(Pretokenize, OtherScriptsAndEmoji) {
  // Thai letters, then a family emoji built with ZWJ, then a thumbs up with a skin tone.
  EXPECT_EQ(run(U"กข\U0001F468‍\U0001F469\U0001F44D\U0001F3FD"),
            (Pieces{{u8(U"ก"), TokenType::Other},
                    {u8(U"ข"), TokenType::Other},
                    {u8(U"\U0001F468‍\U0001F469"), TokenType::Other},
                    {u8(U"\U0001F44D\U0001F3FD"), TokenType::Other}}));
}

TEST(Pretokenize, ReplacementCharIsOther) {
  EXPECT_EQ(run(U"�"), (Pieces{{u8(U"�"), TokenType::Other}}));
}

TEST(Pretokenize, CoversInputExactly) {
  const std::u32string s =
      U"abc កា្ 12.5។ ​\U0001F600ៗxា ́";
  std::vector<khseg::Token> toks;
  khseg::pretokenize(s, toks);
  std::uint32_t pos = 0;
  for (const auto& t : toks) {
    EXPECT_EQ(t.begin, pos);
    EXPECT_LT(t.begin, t.end);
    pos = t.end;
  }
  EXPECT_EQ(pos, s.size());
}

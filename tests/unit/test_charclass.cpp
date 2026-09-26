#include <gtest/gtest.h>
#include <khseg/charclass.hpp>

#include <fstream>
#include <sstream>
#include <string>

using khseg::CharClass;
using khseg::classify;

namespace {

// Which General_Category values each class may have. This catches typos in the
// range table: a class boundary that is off by one lands on a code point with
// an unexpected category.
bool category_fits(CharClass c, const std::string& gc) {
  switch (c) {
    case CharClass::Cons:
    case CharClass::IndV:
    case CharClass::LetterSym: return gc == "Lo";
    case CharClass::Inherent:
    case CharClass::Shifter:
    case CharClass::Robat:
    case CharClass::Diac:
    case CharClass::Coeng: return gc == "Mn";
    case CharClass::DepV:
    case CharClass::Sign: return gc == "Mn" || gc == "Mc";
    case CharClass::Punct: return gc == "Po";
    case CharClass::LekToo: return gc == "Lm";
    case CharClass::Currency: return gc == "Sc";
    case CharClass::Digit: return gc == "Nd";
    case CharClass::NumSym: return gc == "No" || gc == "So";
    case CharClass::Joiner:
    case CharClass::Zwsp: return gc == "Cf";
    case CharClass::Other: return false;
  }
  return false;
}

}  // namespace

TEST(CharClass, MatchesUnicodeData) {
  std::ifstream in(KHSEG_TEST_DATA_DIR "/khmer_unicodedata.txt");
  ASSERT_TRUE(in) << "missing khmer_unicodedata.txt";
  int rows = 0;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string hex, name, gc;
    std::getline(fields, hex, ';');
    std::getline(fields, name, ';');
    std::getline(fields, gc, ';');
    const auto cp = static_cast<char32_t>(std::stoul(hex, nullptr, 16));
    EXPECT_TRUE(category_fits(classify(cp), gc)) << hex << " " << name << " gc=" << gc;
    ++rows;
  }
  EXPECT_EQ(rows, 149);
}

TEST(CharClass, UnassignedAndOutsideAreOther) {
  for (char32_t cp : {0x17DEu, 0x17DFu, 0x17EAu, 0x17EFu, 0x17FAu, 0x17FFu, 0x41u, 0x0E01u}) {
    EXPECT_EQ(classify(cp), CharClass::Other) << std::hex << static_cast<unsigned>(cp);
  }
}

TEST(CharClass, SpecificPoints) {
  EXPECT_EQ(classify(0x1780), CharClass::Cons);
  EXPECT_EQ(classify(0x17A2), CharClass::Cons);
  EXPECT_EQ(classify(0x17A3), CharClass::IndV);
  EXPECT_EQ(classify(0x17D2), CharClass::Coeng);
  EXPECT_EQ(classify(0x17CC), CharClass::Robat);
  EXPECT_EQ(classify(0x17D7), CharClass::LekToo);
  EXPECT_EQ(classify(0x17DC), CharClass::LetterSym);
  EXPECT_EQ(classify(0x200B), CharClass::Zwsp);
  EXPECT_EQ(classify(0x200D), CharClass::Joiner);
}

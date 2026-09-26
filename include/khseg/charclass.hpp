#pragma once

#include <array>
#include <cstdint>

namespace khseg {

// Classes of code points that matter for Khmer clustering and pre-tokenizing.
// See docs/clusters.md for the full table and the reasons behind each choice.
enum class CharClass : std::uint8_t {
  Other,
  Cons,       // U+1780..U+17A2 consonants
  IndV,       // U+17A3..U+17B3 independent vowels
  Inherent,   // U+17B4, U+17B5 invisible inherent vowels (should not occur)
  DepV,       // U+17B6..U+17C5 dependent vowels
  Sign,       // U+17C6..U+17C8 nikahit, reahmuk, yuukaleapintu
  Shifter,    // U+17C9, U+17CA register shifters
  Robat,      // U+17CC
  Diac,       // U+17CB, U+17CD..U+17D1, U+17D3, U+17DD other marks
  Coeng,      // U+17D2
  Punct,      // U+17D4..U+17D6, U+17D8..U+17DA
  LekToo,     // U+17D7 repetition mark
  Currency,   // U+17DB riel sign
  LetterSym,  // U+17DC avakrahasanya, behaves like a base letter
  Digit,      // U+17E0..U+17E9
  NumSym,     // U+17F0..U+17F9 and U+19E0..U+19FF
  Joiner,     // U+200C ZWNJ, U+200D ZWJ
  Zwsp,       // U+200B
};

inline constexpr char32_t kCoeng = 0x17D2;
inline constexpr char32_t kZwsp = 0x200B;

namespace detail {

constexpr std::array<CharClass, 128> make_khmer_table() {
  std::array<CharClass, 128> t{};
  auto set = [&t](char32_t lo, char32_t hi, CharClass c) {
    for (char32_t cp = lo; cp <= hi; ++cp) t[cp - 0x1780] = c;
  };
  set(0x1780, 0x17A2, CharClass::Cons);
  set(0x17A3, 0x17B3, CharClass::IndV);
  set(0x17B4, 0x17B5, CharClass::Inherent);
  set(0x17B6, 0x17C5, CharClass::DepV);
  set(0x17C6, 0x17C8, CharClass::Sign);
  set(0x17C9, 0x17CA, CharClass::Shifter);
  set(0x17CB, 0x17CB, CharClass::Diac);
  set(0x17CC, 0x17CC, CharClass::Robat);
  set(0x17CD, 0x17D1, CharClass::Diac);
  set(0x17D2, 0x17D2, CharClass::Coeng);
  set(0x17D3, 0x17D3, CharClass::Diac);
  set(0x17D4, 0x17D6, CharClass::Punct);
  set(0x17D7, 0x17D7, CharClass::LekToo);
  set(0x17D8, 0x17DA, CharClass::Punct);
  set(0x17DB, 0x17DB, CharClass::Currency);
  set(0x17DC, 0x17DC, CharClass::LetterSym);
  set(0x17DD, 0x17DD, CharClass::Diac);
  set(0x17E0, 0x17E9, CharClass::Digit);
  set(0x17F0, 0x17F9, CharClass::NumSym);
  return t;
}

inline constexpr auto kKhmerTable = make_khmer_table();

}  // namespace detail

constexpr CharClass classify(char32_t cp) noexcept {
  if (cp >= 0x1780 && cp <= 0x17FF) return detail::kKhmerTable[cp - 0x1780];
  if (cp >= 0x19E0 && cp <= 0x19FF) return CharClass::NumSym;
  if (cp == 0x200B) return CharClass::Zwsp;
  if (cp == 0x200C || cp == 0x200D) return CharClass::Joiner;
  return CharClass::Other;
}

// Can start a cluster: consonant, independent vowel, avakrahasanya.
constexpr bool is_base(CharClass c) noexcept {
  return c == CharClass::Cons || c == CharClass::IndV || c == CharClass::LetterSym;
}

// Combining Khmer marks that attach to the preceding cluster.
constexpr bool is_mark(CharClass c) noexcept {
  switch (c) {
    case CharClass::Inherent:
    case CharClass::DepV:
    case CharClass::Sign:
    case CharClass::Shifter:
    case CharClass::Robat:
    case CharClass::Diac:
    case CharClass::Coeng:
      return true;
    default:
      return false;
  }
}

// Code points that make up the letter part of Khmer text (what the
// segmenter works on). Digits, punctuation and symbols are excluded.
constexpr bool is_khmer_letter(CharClass c) noexcept {
  return is_base(c) || is_mark(c);
}

}  // namespace khseg

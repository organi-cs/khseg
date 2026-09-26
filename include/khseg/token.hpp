#pragma once

#include <khseg/export.hpp>

#include <cstdint>
#include <limits>

namespace khseg {

enum class TokenType : std::uint8_t {
  Word,     // Khmer word found in the dictionary
  Unknown,  // Khmer text not in the dictionary (adjacent unknown clusters merged)
  Khmer,    // Khmer run that was not segmented (no dictionary loaded)
  Number,   // ASCII or Khmer digits, with . , : allowed between digits
  Latin,    // Latin letters and digits, with ' and - allowed between letters
  Punct,    // punctuation, including Khmer ។ ៕ ៖ and ៗ
  Symbol,   // currency, math and Khmer numeric symbols
  Space,    // whitespace run, or a run of ZERO WIDTH SPACE
  Other,    // anything else (other scripts, emoji, U+FFFD)
};

KHSEG_EXPORT const char* to_string(TokenType type) noexcept;

inline constexpr std::uint32_t kNoEntry = std::numeric_limits<std::uint32_t>::max();

// Offsets refer to the input passed to the segmenter, in code points
// (begin/end) and in UTF-8 bytes (byte_begin/byte_end). Half-open ranges.
struct Token {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
  std::uint32_t byte_begin = 0;
  std::uint32_t byte_end = 0;
  TokenType type = TokenType::Other;
  std::uint32_t entry = kNoEntry;  // dictionary entry id for Word tokens

  friend bool operator==(const Token&, const Token&) = default;
};

// Tokens that come from Khmer letters (Word, Unknown, Khmer).
constexpr bool is_khmer_token(TokenType t) noexcept {
  return t == TokenType::Word || t == TokenType::Unknown || t == TokenType::Khmer;
}

}  // namespace khseg

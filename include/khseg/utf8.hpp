#pragma once

#include <khseg/export.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace khseg::utf8 {

inline constexpr char32_t kReplacement = 0xFFFD;

enum class ErrorPolicy : std::uint8_t {
  Replace,  // each maximal ill-formed subpart becomes U+FFFD
  Throw,    // throw DecodeError at the first ill-formed byte
};

class KHSEG_EXPORT DecodeError : public std::runtime_error {
 public:
  explicit DecodeError(std::size_t byte_offset);
  std::size_t byte_offset() const noexcept { return byte_offset_; }

 private:
  std::size_t byte_offset_;
};

// Decodes `in` into `out` (cleared first). `byte_offsets` receives out.size()+1
// entries: the byte offset where each code point starts, then in.size().
// Offsets are 32-bit, so a single call handles at most 4 GiB of input.
// Returns the number of U+FFFD substitutions made.
KHSEG_EXPORT std::size_t decode(std::string_view in, std::u32string& out,
                                std::vector<std::uint32_t>& byte_offsets,
                                ErrorPolicy policy = ErrorPolicy::Replace);

KHSEG_EXPORT std::u32string decode(std::string_view in,
                                   ErrorPolicy policy = ErrorPolicy::Replace);

// Appends the UTF-8 encoding of `cp`. Surrogates and values above U+10FFFF
// are written as U+FFFD.
KHSEG_EXPORT void append(std::string& out, char32_t cp);

KHSEG_EXPORT std::string encode(std::u32string_view text);

}  // namespace khseg::utf8

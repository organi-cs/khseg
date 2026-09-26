#pragma once

#include <khseg/export.hpp>
#include <khseg/token.hpp>

#include <string_view>
#include <vector>

namespace khseg {

// Splits text into coarse tokens by character type. Khmer letters come out
// as TokenType::Khmer runs for the segmenter to split further. Only code
// point offsets are filled in; byte offsets are left at zero.
//
// Rules (see docs/DESIGN.md section 3.4):
//   Khmer   run of Khmer bases, marks and joiners
//   Number  digits (ASCII or Khmer), with . , : allowed between two digits
//   Latin   Latin letter, then letters/digits, with ' - allowed between letters
//   Punct   one punctuation code point, or a run of the same one ("...")
//   Symbol  one symbol code point
//   Space   run of whitespace; ZERO WIDTH SPACE runs are kept separate
//   Other   one code point plus any combining marks, ZWJ sequences and
//           variation selectors after it
KHSEG_EXPORT void pretokenize(std::u32string_view text, std::vector<Token>& out);

}  // namespace khseg

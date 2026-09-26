#include <khseg/charclass.hpp>
#include <khseg/pretokenize.hpp>

namespace khseg {

namespace {

enum class Kind : std::uint8_t {
  KhmerLetter,
  Joiner,
  Zwsp,
  Digit,
  LatinLetter,
  Space,
  Punct,
  Symbol,
  Combining,
  Other,
};

constexpr bool in(char32_t cp, char32_t lo, char32_t hi) noexcept { return cp >= lo && cp <= hi; }

constexpr bool is_whitespace(char32_t cp) noexcept {
  return in(cp, 0x09, 0x0D) || cp == 0x20 || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
         in(cp, 0x2000, 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
         cp == 0x3000 || cp == 0xFEFF;
}

// Combining marks, variation selectors, emoji modifiers and tag characters.
// They extend whatever non-Khmer character comes before them.
constexpr bool is_extender(char32_t cp) noexcept {
  return in(cp, 0x0300, 0x036F) || in(cp, 0x1AB0, 0x1AFF) || in(cp, 0x1DC0, 0x1DFF) ||
         in(cp, 0x20D0, 0x20FF) || in(cp, 0xFE00, 0xFE0F) || in(cp, 0xFE20, 0xFE2F) ||
         in(cp, 0x1F3FB, 0x1F3FF) || in(cp, 0xE0020, 0xE007F) || in(cp, 0xE0100, 0xE01EF);
}

constexpr bool is_latin_letter(char32_t cp) noexcept {
  return in(cp, 'A', 'Z') || in(cp, 'a', 'z') ||
         (in(cp, 0xC0, 0xFF) && cp != 0xD7 && cp != 0xF7) || in(cp, 0x0100, 0x024F) ||
         in(cp, 0x1E00, 0x1EFF);
}

constexpr bool is_ascii_punct(char32_t cp) noexcept {
  switch (cp) {
    case '!': case '"': case '#': case '%': case '&': case '\'': case '(': case ')':
    case '*': case ',': case '-': case '.': case '/': case ':': case ';': case '?':
    case '@': case '[': case '\\': case ']': case '_': case '{': case '}':
      return true;
    default:
      return false;
  }
}

constexpr bool is_ascii_symbol(char32_t cp) noexcept {
  switch (cp) {
    case '$': case '+': case '<': case '=': case '>': case '^': case '`': case '|': case '~':
      return true;
    default:
      return false;
  }
}

constexpr Kind kind_of(char32_t cp) noexcept {
  const CharClass c = classify(cp);
  if (is_khmer_letter(c)) return Kind::KhmerLetter;
  switch (c) {
    case CharClass::Joiner: return Kind::Joiner;
    case CharClass::Zwsp: return Kind::Zwsp;
    case CharClass::Digit: return Kind::Digit;
    case CharClass::Punct:
    case CharClass::LekToo: return Kind::Punct;
    case CharClass::Currency:
    case CharClass::NumSym: return Kind::Symbol;
    default: break;
  }
  if (in(cp, '0', '9')) return Kind::Digit;
  if (is_latin_letter(cp)) return Kind::LatinLetter;
  if (is_whitespace(cp)) return Kind::Space;
  if (is_ascii_punct(cp)) return Kind::Punct;
  if (is_ascii_symbol(cp)) return Kind::Symbol;
  if (in(cp, 0xA1, 0xBF)) {
    switch (cp) {
      case 0xA1: case 0xA7: case 0xAB: case 0xB6: case 0xB7: case 0xBB: case 0xBF:
        return Kind::Punct;
      default:
        return Kind::Symbol;
    }
  }
  if (in(cp, 0x2010, 0x2027) || in(cp, 0x2030, 0x205E)) return Kind::Punct;
  if (in(cp, 0x3001, 0x3003) || in(cp, 0x3008, 0x3011)) return Kind::Punct;
  if (in(cp, 0x20A0, 0x20CF) || in(cp, 0x2190, 0x22FF)) return Kind::Symbol;
  if (is_extender(cp)) return Kind::Combining;
  return Kind::Other;
}

constexpr bool is_number_separator(char32_t cp) noexcept {
  return cp == '.' || cp == ',' || cp == ':';
}

constexpr bool is_word_joiner_punct(char32_t cp) noexcept {
  return cp == '\'' || cp == 0x2019 || cp == '-';
}

}  // namespace

void pretokenize(std::u32string_view text, std::vector<Token>& out) {
  out.clear();
  const std::size_t n = text.size();
  auto kind_at = [&](std::size_t j) { return kind_of(text[j]); };

  std::size_t i = 0;
  while (i < n) {
    const Kind k = kind_at(i);
    std::size_t j = i + 1;
    TokenType type = TokenType::Other;

    switch (k) {
      case Kind::KhmerLetter:
        while (j < n && (kind_at(j) == Kind::KhmerLetter || kind_at(j) == Kind::Joiner)) ++j;
        type = TokenType::Khmer;
        break;

      case Kind::Digit:
        for (;;) {
          while (j < n && kind_at(j) == Kind::Digit) ++j;
          if (j + 1 < n && is_number_separator(text[j]) && kind_at(j + 1) == Kind::Digit) {
            j += 2;
            continue;
          }
          break;
        }
        type = TokenType::Number;
        break;

      case Kind::LatinLetter:
        for (;;) {
          while (j < n && (kind_at(j) == Kind::LatinLetter || kind_at(j) == Kind::Digit ||
                           kind_at(j) == Kind::Combining)) {
            ++j;
          }
          if (j + 1 < n && is_word_joiner_punct(text[j]) && kind_at(j + 1) == Kind::LatinLetter) {
            j += 2;
            continue;
          }
          break;
        }
        type = TokenType::Latin;
        break;

      case Kind::Space:
        while (j < n && kind_at(j) == Kind::Space) ++j;
        type = TokenType::Space;
        break;

      case Kind::Zwsp:
        while (j < n && kind_at(j) == Kind::Zwsp) ++j;
        type = TokenType::Space;
        break;

      case Kind::Punct:
        while (j < n && text[j] == text[i]) ++j;
        type = TokenType::Punct;
        break;

      case Kind::Symbol:
        type = TokenType::Symbol;
        break;

      case Kind::Joiner:
      case Kind::Combining:
      case Kind::Other:
        for (;;) {
          if (j < n && kind_at(j) == Kind::Combining) {
            ++j;
          } else if (j < n && text[j] == 0x200D) {
            ++j;
            if (j < n && kind_at(j) == Kind::Other) ++j;
          } else {
            break;
          }
        }
        type = TokenType::Other;
        break;
    }

    Token t;
    t.begin = static_cast<std::uint32_t>(i);
    t.end = static_cast<std::uint32_t>(j);
    t.type = type;
    out.push_back(t);
    i = j;
  }
}

const char* to_string(TokenType type) noexcept {
  switch (type) {
    case TokenType::Word: return "word";
    case TokenType::Unknown: return "unknown";
    case TokenType::Khmer: return "khmer";
    case TokenType::Number: return "number";
    case TokenType::Latin: return "latin";
    case TokenType::Punct: return "punct";
    case TokenType::Symbol: return "symbol";
    case TokenType::Space: return "space";
    case TokenType::Other: return "other";
  }
  return "other";
}

}  // namespace khseg

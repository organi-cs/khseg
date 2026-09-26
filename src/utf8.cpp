#include <khseg/utf8.hpp>

#include <string>

namespace khseg::utf8 {

DecodeError::DecodeError(std::size_t byte_offset)
    : std::runtime_error("invalid UTF-8 at byte " + std::to_string(byte_offset)),
      byte_offset_(byte_offset) {}

namespace {

struct Lead {
  std::uint8_t length;  // 0 means the byte cannot start a sequence
  std::uint8_t lo;      // allowed range for the second byte
  std::uint8_t hi;
};

// Second-byte ranges from Table 3-7 of the Unicode Standard. Restricting the
// second byte rules out overlong forms, surrogates and values above U+10FFFF,
// and gives the "maximal subpart" replacement behaviour for free.
constexpr Lead lead_info(std::uint8_t b) noexcept {
  if (b < 0x80) return {1, 0, 0};
  if (b < 0xC2) return {0, 0, 0};
  if (b < 0xE0) return {2, 0x80, 0xBF};
  if (b == 0xE0) return {3, 0xA0, 0xBF};
  if (b == 0xED) return {3, 0x80, 0x9F};
  if (b < 0xF0) return {3, 0x80, 0xBF};
  if (b == 0xF0) return {4, 0x90, 0xBF};
  if (b < 0xF4) return {4, 0x80, 0xBF};
  if (b == 0xF4) return {4, 0x80, 0x8F};
  return {0, 0, 0};
}

}  // namespace

std::size_t decode(std::string_view in, std::u32string& out,
                   std::vector<std::uint32_t>& byte_offsets, ErrorPolicy policy) {
  out.clear();
  byte_offsets.clear();
  out.reserve(in.size());
  byte_offsets.reserve(in.size() + 1);

  const auto* s = reinterpret_cast<const unsigned char*>(in.data());
  const std::size_t n = in.size();
  std::size_t errors = 0;
  std::size_t i = 0;

  while (i < n) {
    const std::uint8_t b0 = s[i];
    if (b0 < 0x80) {
      out.push_back(b0);
      byte_offsets.push_back(static_cast<std::uint32_t>(i));
      ++i;
      continue;
    }

    const Lead lead = lead_info(b0);
    std::size_t consumed = 1;
    bool ok = lead.length != 0;
    char32_t cp = 0;

    if (ok) {
      cp = b0 & (0xFFu >> (lead.length + 1));
      for (std::size_t k = 1; k < lead.length; ++k) {
        if (i + k >= n) {
          ok = false;
          break;
        }
        const std::uint8_t b = s[i + k];
        const std::uint8_t lo = k == 1 ? lead.lo : std::uint8_t{0x80};
        const std::uint8_t hi = k == 1 ? lead.hi : std::uint8_t{0xBF};
        if (b < lo || b > hi) {
          ok = false;
          break;
        }
        cp = (cp << 6) | (b & 0x3Fu);
        ++consumed;
      }
    }

    if (!ok) {
      if (policy == ErrorPolicy::Throw) throw DecodeError(i);
      cp = kReplacement;
      ++errors;
    }
    out.push_back(cp);
    byte_offsets.push_back(static_cast<std::uint32_t>(i));
    i += consumed;
  }

  byte_offsets.push_back(static_cast<std::uint32_t>(n));
  return errors;
}

std::u32string decode(std::string_view in, ErrorPolicy policy) {
  std::u32string out;
  std::vector<std::uint32_t> offsets;
  decode(in, out, offsets, policy);
  return out;
}

void append(std::string& out, char32_t cp) {
  if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = kReplacement;
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string encode(std::u32string_view text) {
  std::string out;
  out.reserve(text.size() * 3);
  for (char32_t cp : text) append(out, cp);
  return out;
}

}  // namespace khseg::utf8

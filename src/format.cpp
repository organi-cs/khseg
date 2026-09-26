#include <khseg/cluster.hpp>
#include <khseg/format.hpp>

#include <charconv>
#include <vector>

namespace khseg {

namespace {

std::string_view bytes_of(std::string_view src, const Token& t) {
  return src.substr(t.byte_begin, t.byte_end - t.byte_begin);
}

void append_uint(std::string& out, std::uint64_t v) {
  char buf[24];
  auto res = std::to_chars(buf, buf + sizeof buf, v);
  out.append(buf, res.ptr);
}

constexpr std::string_view kZwspUtf8 = "\xE2\x80\x8B";
constexpr std::string_view kMiddleDot = "\xC2\xB7";

}  // namespace

void write_separated(std::string_view src, std::span<const Token> tokens, std::string_view sep,
                     std::string& out) {
  bool first = true;
  for (const Token& t : tokens) {
    if (t.type == TokenType::Space) continue;
    if (!first) out.append(sep);
    out.append(bytes_of(src, t));
    first = false;
  }
}

void write_zwsp(std::string_view src, std::span<const Token> tokens, std::string& out) {
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0 && is_khmer_token(tokens[i - 1].type) && is_khmer_token(tokens[i].type)) {
      out.append(kZwspUtf8);
    }
    out.append(bytes_of(src, tokens[i]));
  }
}

void append_json_string(std::string& out, std::string_view s) {
  static constexpr char kHex[] = "0123456789abcdef";
  out.push_back('"');
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (c < 0x20) {
          out.append("\\u00");
          out.push_back(kHex[c >> 4]);
          out.push_back(kHex[c & 0xF]);
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(s[i + 2]) == 0xA9)) {
          // U+2028 and U+2029 are valid JSON but break JavaScript string literals.
          out.append(static_cast<unsigned char>(s[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029");
          i += 2;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

void write_json(std::string_view src, std::span<const Token> tokens, std::uint64_t line,
                OffsetUnit unit, std::string& out) {
  out.append("{\"line\":");
  append_uint(out, line);
  out.append(",\"tokens\":[");
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const Token& t = tokens[i];
    if (i > 0) out.push_back(',');
    out.append("{\"text\":");
    append_json_string(out, bytes_of(src, t));
    out.append(",\"start\":");
    append_uint(out, unit == OffsetUnit::Bytes ? t.byte_begin : t.begin);
    out.append(",\"end\":");
    append_uint(out, unit == OffsetUnit::Bytes ? t.byte_end : t.end);
    out.append(",\"type\":\"");
    out.append(to_string(t.type));
    out.append("\"}");
  }
  out.append("]}");
}

void write_clusters(std::string_view src, std::u32string_view text,
                    std::span<const std::uint32_t> byte_offsets, std::span<const Token> tokens,
                    std::string& out) {
  std::vector<std::uint32_t> b;
  bool first = true;
  for (const Token& t : tokens) {
    if (t.type == TokenType::Space) continue;
    if (!first) out.push_back(' ');
    first = false;
    if (!is_khmer_token(t.type)) {
      out.append(bytes_of(src, t));
      continue;
    }
    cluster_boundaries(text.substr(t.begin, t.end - t.begin), b);
    for (std::size_t k = 0; k + 1 < b.size(); ++k) {
      if (k > 0) out.append(kMiddleDot);
      const std::uint32_t from = byte_offsets[t.begin + b[k]];
      const std::uint32_t to = byte_offsets[t.begin + b[k + 1]];
      out.append(src.substr(from, to - from));
    }
  }
}

}  // namespace khseg

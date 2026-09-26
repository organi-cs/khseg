#pragma once

#include <khseg/export.hpp>
#include <khseg/token.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace khseg {

// All writers append to `out` and never add a trailing newline. `src` is the
// UTF-8 text the tokens were produced from.

// Non-space tokens joined by `sep`. Original whitespace is dropped, so this
// is lossy; it is the format gold files use.
KHSEG_EXPORT void write_separated(std::string_view src, std::span<const Token> tokens,
                                  std::string_view sep, std::string& out);

// The original text with U+200B inserted between two adjacent Khmer tokens.
// Nothing is removed, and running it on its own output changes nothing,
// because an existing ZERO WIDTH SPACE is a Space token and separates the
// tokens on either side of it.
KHSEG_EXPORT void write_zwsp(std::string_view src, std::span<const Token> tokens,
                             std::string& out);

enum class OffsetUnit : std::uint8_t { CodePoints, Bytes };

// One JSON object: {"line":N,"tokens":[{"text":..,"start":..,"end":..,"type":..}]}.
// Every token is listed, spaces included.
KHSEG_EXPORT void write_json(std::string_view src, std::span<const Token> tokens,
                             std::uint64_t line, OffsetUnit unit, std::string& out);

// Tokens separated by spaces, with the clusters of each Khmer token joined by
// U+00B7 MIDDLE DOT. Needs the decoded text and byte offsets from decoding
// `src` (Workspace::text and Workspace::byte_offsets).
KHSEG_EXPORT void write_clusters(std::string_view src, std::u32string_view text,
                                 std::span<const std::uint32_t> byte_offsets,
                                 std::span<const Token> tokens, std::string& out);

// Appends `utf8` as a quoted JSON string.
KHSEG_EXPORT void append_json_string(std::string& out, std::string_view utf8);

}  // namespace khseg

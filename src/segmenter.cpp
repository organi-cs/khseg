#include <khseg/pretokenize.hpp>
#include <khseg/segmenter.hpp>

namespace khseg {

Segmenter::Segmenter(std::shared_ptr<const Dictionary> dict, Options options)
    : dict_(std::move(dict)), options_(options) {}

Segmenter::~Segmenter() = default;
Segmenter::Segmenter(const Segmenter&) = default;
Segmenter& Segmenter::operator=(const Segmenter&) = default;
Segmenter::Segmenter(Segmenter&&) noexcept = default;
Segmenter& Segmenter::operator=(Segmenter&&) noexcept = default;

std::vector<Token> Segmenter::segment(std::string_view utf8) const {
  Workspace ws;
  std::vector<Token> out;
  segment(utf8, ws, out);
  return out;
}

void Segmenter::segment(std::string_view utf8, Workspace& ws, std::vector<Token>& out) const {
  out.clear();
  ws.replacements = utf8::decode(utf8, ws.text, ws.byte_offsets, options_.invalid_utf8);
  pretokenize(ws.text, ws.coarse);

  for (const Token& t : ws.coarse) {
    if (t.type == TokenType::Khmer && dict_) {
      segment_run(t, ws, out);
    } else {
      out.push_back(t);
    }
  }
  for (Token& t : out) {
    t.byte_begin = ws.byte_offsets[t.begin];
    t.byte_end = ws.byte_offsets[t.end];
  }
}

void Segmenter::segment_run(const Token& run, Workspace&, std::vector<Token>& out) const {
  out.push_back(run);
}

std::vector<std::string> Segmenter::words(std::string_view utf8) const {
  std::vector<std::string> result;
  for (const Token& t : segment(utf8)) {
    if (t.type == TokenType::Space) continue;
    result.emplace_back(utf8.substr(t.byte_begin, t.byte_end - t.byte_begin));
  }
  return result;
}

}  // namespace khseg

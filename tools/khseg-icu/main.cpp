// Baseline: ICU's word break iterator for Khmer, which uses ICU's own
// dictionary (khmerdict) and a maximal-matching style break engine.
//
// Segment mode writes the same space-separated format as `khseg`, so the
// output can be scored with `khseg-eval --pred`. Bench mode measures
// in-memory throughput the same way `khseg-bench` does.
//
// Only ICU's C API is used, so this also links against the MSVC-built ICU
// binaries from a MinGW build.
#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>
#include <unicode/ubrk.h>
#include <unicode/uclean.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../common/cli_io.hpp"

namespace {

// Segments of a line that are only whitespace (including ZWSP) are dropped,
// matching the space-separated output of khseg.
bool is_space_segment(std::string_view seg, khseg::Workspace& ws, std::vector<khseg::Token>& toks) {
  khseg::utf8::decode(seg, ws.text, ws.byte_offsets);
  khseg::pretokenize(ws.text, toks);
  return std::all_of(toks.begin(), toks.end(),
                     [](const khseg::Token& t) { return t.type == khseg::TokenType::Space; });
}

class IcuSegmenter {
 public:
  IcuSegmenter() {
    UErrorCode st = U_ZERO_ERROR;
    bi_ = ubrk_open(UBRK_WORD, "km", nullptr, 0, &st);
    if (U_FAILURE(st)) throw std::runtime_error(std::string("ubrk_open: ") + u_errorName(st));
  }
  ~IcuSegmenter() {
    ubrk_close(bi_);
    utext_close(ut_);
  }
  IcuSegmenter(const IcuSegmenter&) = delete;
  IcuSegmenter& operator=(const IcuSegmenter&) = delete;

  // Calls f(begin, end) with byte offsets of each segment.
  template <class F>
  void segment(std::string_view line, F&& f) {
    UErrorCode st = U_ZERO_ERROR;
    ut_ = utext_openUTF8(ut_, line.data(), static_cast<int64_t>(line.size()), &st);
    ubrk_setUText(bi_, ut_, &st);
    if (U_FAILURE(st)) throw std::runtime_error(std::string("ICU: ") + u_errorName(st));
    int32_t prev = ubrk_first(bi_);
    for (int32_t b = ubrk_next(bi_); b != UBRK_DONE; b = ubrk_next(bi_)) {
      f(static_cast<std::size_t>(prev), static_cast<std::size_t>(b));
      prev = b;
    }
  }

 private:
  UBreakIterator* bi_ = nullptr;
  UText* ut_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();
  CLI::App app{"khseg-icu: segment Khmer with ICU's word break iterator (baseline)"};
  std::string input;
  bool bench = false;
  double min_mb = 50;
  int repeat = 5;
  app.add_option("input", input, "Input file (default: standard input)");
  app.add_flag("--bench", bench, "Measure throughput instead of writing segments");
  app.add_option("--min-mb", min_mb, "Bench: repeat the input to at least this many MB");
  app.add_option("--repeat", repeat, "Bench: timed passes (median is reported)")
      ->check(CLI::PositiveNumber);
  CLI11_PARSE(app, argc, argv);

  UVersionInfo v;
  u_getVersion(v);
  char version[U_MAX_VERSION_STRING_LENGTH];
  u_versionToString(v, version);

  std::FILE* f = input.empty() ? stdin : khseg::cli::open_read(input);
  if (f == nullptr) {
    std::cerr << "khseg-icu: cannot open " << input << "\n";
    return 2;
  }
  std::vector<std::string> lines;
  {
    khseg::cli::LineReader reader(f);
    std::string line;
    std::string_view ending;
    while (reader.next(line, ending)) {
      if (reader.lines_read() == 1) khseg::cli::strip_bom(line);
      lines.push_back(line);
    }
  }
  if (f != stdin) std::fclose(f);

  try {
    IcuSegmenter icu;
    if (!bench) {
      khseg::cli::Output out(stdout);
      khseg::Workspace ws;
      std::vector<khseg::Token> toks;
      for (const auto& line : lines) {
        std::string& buf = out.buffer();
        bool first = true;
        icu.segment(line, [&](std::size_t b, std::size_t e) {
          const std::string_view seg = std::string_view(line).substr(b, e - b);
          if (is_space_segment(seg, ws, toks)) return;
          if (!first) buf.push_back(' ');
          buf.append(seg);
          first = false;
        });
        buf.push_back('\n');
        out.maybe_flush();
      }
      return 0;
    }

    std::vector<std::string> data;
    std::size_t bytes = 0;
    while (static_cast<double>(bytes) < min_mb * 1e6 && !lines.empty()) {
      for (const auto& l : lines) {
        data.push_back(l);
        bytes += l.size();
      }
    }
    std::size_t segments = 0;
    auto pass = [&] {
      std::size_t n = 0;
      for (const auto& l : data) icu.segment(l, [&n](std::size_t, std::size_t) { ++n; });
      return n;
    };
    segments = pass();
    std::vector<double> times;
    for (int i = 0; i < repeat; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      if (pass() != segments) std::abort();
      times.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(times.begin(), times.end());
    const double mb = static_cast<double>(bytes) / 1e6;
    std::printf("ICU %s, locale km, word break iterator\n", version);
    std::printf("input %.1f MB in %zu lines\n", mb, data.size());
    std::printf("median %.1f MB/s, best %.1f MB/s, %.0f segments/s\n", mb / times[times.size() / 2],
                mb / times.front(), static_cast<double>(segments) / times[times.size() / 2]);
  } catch (const std::exception& e) {
    std::cerr << "khseg-icu: " << e.what() << "\n";
    return 2;
  }
  u_cleanup();
  return 0;
}

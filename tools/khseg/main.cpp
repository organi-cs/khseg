#include <CLI/CLI.hpp>
#include <khseg/khseg.hpp>

#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../common/cli_io.hpp"
#include "../common/dict_loader.hpp"

namespace {

enum class Format { Space, Zwsp, Json, Clusters };

enum Exit : int { kOk = 0, kUsage = 1, kIoError = 2, kBadUtf8 = 3 };

struct Config {
  std::vector<std::string> files;
  Format format = Format::Space;
  std::string sep = " ";
  khseg::OffsetUnit offsets = khseg::OffsetUnit::CodePoints;
  bool strict_utf8 = false;
  std::string dict_path;
  bool verbose = false;
};

int run(const Config& cfg, const khseg::Segmenter& seg) {
  khseg::cli::Output out(stdout);
  khseg::Workspace ws;
  std::vector<khseg::Token> tokens;
  std::string line;
  std::string_view ending;
  std::uint64_t line_no = 0;

  auto process = [&](std::FILE* f, const std::string& name) -> int {
    khseg::cli::LineReader reader(f);
    bool first = true;
    while (reader.next(line, ending)) {
      ++line_no;
      std::string& buf = out.buffer();
      if (first && khseg::cli::strip_bom(line) && cfg.format == Format::Zwsp) {
        buf.append("\xEF\xBB\xBF");
      }
      first = false;
      try {
        seg.segment(line, ws, tokens);
        if (ws.replacements > 0) {
          // Write U+FFFD instead of the invalid bytes. Offsets then refer to
          // the repaired line.
          line = khseg::utf8::encode(ws.text);
          seg.segment(line, ws, tokens);
        }
      } catch (const khseg::utf8::DecodeError& e) {
        out.flush();
        std::cerr << "khseg: " << name << ":" << reader.lines_read() << ": " << e.what() << "\n";
        return kBadUtf8;
      }
      switch (cfg.format) {
        case Format::Space: khseg::write_separated(line, tokens, cfg.sep, buf); break;
        case Format::Zwsp: khseg::write_zwsp(line, tokens, buf); break;
        case Format::Json: khseg::write_json(line, tokens, line_no, cfg.offsets, buf); break;
        case Format::Clusters:
          khseg::write_clusters(line, ws.text, ws.byte_offsets, tokens, buf);
          break;
      }
      // Every output line ends in a newline except in lossless zwsp mode,
      // which mirrors the input exactly.
      buf.append(cfg.format == Format::Zwsp ? ending : (ending.empty() ? "\n" : ending));
      out.maybe_flush();
    }
    if (reader.error()) {
      std::cerr << "khseg: read error on " << name << "\n";
      return kIoError;
    }
    return kOk;
  };

  if (cfg.files.empty()) return process(stdin, "<stdin>");
  for (const auto& path : cfg.files) {
    std::FILE* f = khseg::cli::open_read(path);
    if (f == nullptr) {
      out.flush();
      std::cerr << "khseg: cannot open " << path << "\n";
      return kIoError;
    }
    const int rc = process(f, path);
    std::fclose(f);
    if (rc != kOk) return rc;
  }
  out.flush();
  return out.error() ? kIoError : kOk;
}

}  // namespace

int main(int argc, char** argv) {
  khseg::cli::setup_stdio();

  CLI::App app{"khseg: split Khmer text into words. Reads FILEs or standard input."};
  app.set_version_flag("--version", std::string("khseg ") + khseg::version());

  Config cfg;
  bool zwsp = false;
  bool json = false;
  std::string sep;

  app.add_option("files", cfg.files, "Input files (default: standard input)");
  app.add_option("-f,--format", cfg.format, "Output format")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, Format>{{"space", Format::Space},
                                        {"zwsp", Format::Zwsp},
                                        {"json", Format::Json},
                                        {"clusters", Format::Clusters}},
          CLI::ignore_case));
  app.add_flag("--zwsp", zwsp, "Same as --format zwsp");
  app.add_flag("--json", json, "Same as --format json");
  app.add_option("--sep", sep, "Token separator for the space format");
  app.add_option("--offsets", cfg.offsets, "JSON offsets in code points or bytes")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::OffsetUnit>{{"cp", khseg::OffsetUnit::CodePoints},
                                                   {"byte", khseg::OffsetUnit::Bytes}},
          CLI::ignore_case));
  app.add_flag("--strict-utf8", cfg.strict_utf8, "Fail on invalid UTF-8 instead of using U+FFFD");

  khseg::Options opts;
  std::optional<double> unk_cost;
  bool no_merge = false;
  app.add_option("-d,--dict", cfg.dict_path,
                 "Dictionary TSV (default: $KHSEG_DICT, then <exe>/../share/khseg/)");
  app.add_option("-a,--algo", opts.algorithm, "Segmentation algorithm")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::Algorithm>{{"viterbi", khseg::Algorithm::Viterbi},
                                                  {"fmm", khseg::Algorithm::Forward},
                                                  {"bmm", khseg::Algorithm::Backward},
                                                  {"bimm", khseg::Algorithm::Bidirectional}},
          CLI::ignore_case));
  app.add_option("--unk-cost", unk_cost, "Cost of one unknown cluster (default: from dictionary)")
      ->check(CLI::PositiveNumber);
  app.add_flag("--no-merge-unknown", no_merge, "Keep unknown clusters as separate tokens");
  app.add_option("--lektoo", opts.lektoo, "Handling of the repetition mark U+17D7")
      ->transform(CLI::CheckedTransformer(
          std::map<std::string, khseg::LekTooPolicy>{{"separate", khseg::LekTooPolicy::Separate},
                                                     {"attach", khseg::LekTooPolicy::Attach}},
          CLI::ignore_case));
  app.add_flag("-v,--verbose", cfg.verbose, "Report dictionary problems on stderr");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    const int rc = app.exit(e);
    return rc == 0 ? kOk : kUsage;
  }

  if (zwsp && json) {
    std::cerr << "khseg: --zwsp and --json cannot be combined\n";
    return kUsage;
  }
  if (zwsp) cfg.format = Format::Zwsp;
  if (json) cfg.format = Format::Json;
  if (!sep.empty()) cfg.sep = sep;

  if (cfg.strict_utf8) opts.invalid_utf8 = khseg::utf8::ErrorPolicy::Throw;
  opts.unknown_cost = unk_cost;
  opts.merge_unknown = !no_merge;

  std::shared_ptr<const khseg::Dictionary> dict;
  try {
    dict = khseg::cli::load_dictionary("khseg", cfg.dict_path, cfg.verbose);
  } catch (const std::exception& e) {
    std::cerr << "khseg: " << e.what() << '\n';
    return kIoError;
  }
  if (!dict && cfg.format != Format::Clusters) {
    std::cerr << "khseg: no dictionary found, Khmer text is not segmented (use --dict)\n";
  }
  khseg::Segmenter seg(dict, opts);

  return run(cfg, seg);
}

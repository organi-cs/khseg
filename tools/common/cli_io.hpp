#pragma once

// Byte-exact line I/O shared by the command line tools. On Windows the
// standard streams are switched to binary mode so that the C runtime does not
// translate CRLF or touch the UTF-8 bytes.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace khseg::cli {

inline void setup_stdio() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  if (_isatty(_fileno(stdout))) SetConsoleOutputCP(CP_UTF8);
#endif
}

inline std::FILE* open_read(const std::filesystem::path& p) {
#ifdef _WIN32
  return _wfopen(p.c_str(), L"rb");
#else
  return std::fopen(p.c_str(), "rb");
#endif
}

inline std::FILE* open_write(const std::filesystem::path& p) {
#ifdef _WIN32
  return _wfopen(p.c_str(), L"wb");
#else
  return std::fopen(p.c_str(), "wb");
#endif
}

// Reads lines from a FILE*. The returned line excludes the line ending, and
// `ending` says which one it had ("", "\n" or "\r\n") so output can mirror it.
class LineReader {
 public:
  explicit LineReader(std::FILE* f) : f_(f), buf_(1 << 16) {}

  bool next(std::string& line, std::string_view& ending) {
    line.clear();
    for (;;) {
      if (pos_ == len_) {
        if (eof_) break;
        len_ = std::fread(buf_.data(), 1, buf_.size(), f_);
        pos_ = 0;
        if (len_ == 0) {
          eof_ = true;
          break;
        }
      }
      const char* start = buf_.data() + pos_;
      const auto* nl = static_cast<const char*>(std::memchr(start, '\n', len_ - pos_));
      if (nl == nullptr) {
        line.append(start, len_ - pos_);
        pos_ = len_;
        continue;
      }
      line.append(start, static_cast<std::size_t>(nl - start));
      pos_ += static_cast<std::size_t>(nl - start) + 1;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
        ending = "\r\n";
      } else {
        ending = "\n";
      }
      ++lines_;
      return true;
    }
    ending = "";
    if (line.empty()) return false;
    if (line.back() == '\r') line.pop_back();  // lone CR at end of file
    ++lines_;
    return true;
  }

  bool error() const { return std::ferror(f_) != 0; }
  std::size_t lines_read() const { return lines_; }

 private:
  std::FILE* f_;
  std::vector<char> buf_;
  std::size_t pos_ = 0;
  std::size_t len_ = 0;
  std::size_t lines_ = 0;
  bool eof_ = false;
};

// Buffered writer on a FILE*.
class Output {
 public:
  explicit Output(std::FILE* f) : f_(f) { buf_.reserve(kFlushAt * 2); }
  ~Output() { flush(); }
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;

  std::string& buffer() { return buf_; }
  void maybe_flush() {
    if (buf_.size() >= kFlushAt) flush();
  }
  void flush() {
    if (!buf_.empty()) std::fwrite(buf_.data(), 1, buf_.size(), f_);
    buf_.clear();
    std::fflush(f_);
  }
  bool error() const { return std::ferror(f_) != 0; }

 private:
  static constexpr std::size_t kFlushAt = 1 << 16;
  std::FILE* f_;
  std::string buf_;
};

inline bool strip_bom(std::string& line) {
  if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    line.erase(0, 3);
    return true;
  }
  return false;
}

}  // namespace khseg::cli

#include "mapped_file.hpp"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace khseg::detail {

namespace {
[[noreturn]] void fail(const std::filesystem::path& path, const char* what) {
  throw std::runtime_error(std::string(what) + " " + path.string());
}
}  // namespace

#ifdef _WIN32

MappedFile::MappedFile(const std::filesystem::path& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) fail(path, "cannot open dictionary");
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size)) {
    CloseHandle(file);
    fail(path, "cannot read size of");
  }
  size_ = static_cast<std::size_t>(size.QuadPart);
  if (size_ > 0) {
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping != nullptr) {
      data_ = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
      CloseHandle(mapping);  // the view keeps the mapping alive
    }
  }
  CloseHandle(file);
  if (size_ > 0 && data_ == nullptr) fail(path, "cannot map");
}

MappedFile::~MappedFile() {
  if (data_ != nullptr) UnmapViewOfFile(data_);
}

#else

MappedFile::MappedFile(const std::filesystem::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) fail(path, "cannot open dictionary");
  struct stat st {};
  if (::fstat(fd, &st) != 0) {
    ::close(fd);
    fail(path, "cannot read size of");
  }
  size_ = static_cast<std::size_t>(st.st_size);
  if (size_ > 0) {
    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p != MAP_FAILED) data_ = static_cast<const char*>(p);
  }
  ::close(fd);  // the mapping stays valid
  if (size_ > 0 && data_ == nullptr) fail(path, "cannot map");
}

MappedFile::~MappedFile() {
  if (data_ != nullptr) ::munmap(const_cast<char*>(data_), size_);
}

#endif

}  // namespace khseg::detail

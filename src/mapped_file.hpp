#pragma once

// Read-only memory mapping of a whole file. Internal to the library.

#include <cstddef>
#include <filesystem>

namespace khseg::detail {

class MappedFile {
 public:
  // Throws std::runtime_error if the file cannot be opened or mapped.
  explicit MappedFile(const std::filesystem::path& path);
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  // Page aligned, so any array stored at an 8-byte aligned offset is aligned.
  const char* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }

 private:
  const char* data_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace khseg::detail

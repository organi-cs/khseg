// Binary dictionary format (.khd), version 2, in host byte order (a marker
// field rejects files from machines with the other order):
//
//   offset  field
//        0  char[8]  magic "KHSEGDIC"
//        8  u32      version (2)
//       12  u32      byte order mark 0x01020304
//       16  u32      value format (0 count, 1 logprob)
//       20  u32      flags (bit 0: unknown cost present)
//       24  f64      unknown cost
//       32  f64      total count
//       40  f64      max cost
//       48  u64      max word length (code points)
//       56  u64      number of words n
//       64  u64      number of sections (10)
//       72  10 x {u64 byte offset, u64 element count}, one per section:
//             0 u32[n+1] word offsets (code points)   5 u32 forward check
//             1 u32[m]   words as UTF-32              6 u32 forward value
//             2 f64[n]   counts                       7 u32 backward base
//             3 f64[n]   costs                        8 u32 backward check
//             4 u32      forward base                 9 u32 backward value
//      232  sections, each starting at a multiple of 8 bytes
//   last 8  u64 checksum: FNV-1a over the preceding bytes taken as 64-bit
//           words (a final partial word is zero padded)
//
// Every section starts 8-byte aligned, so a mapped file (page aligned) or an
// aligned heap copy can be used in place without copying the arrays.
//
// Loading always checks the header, that every section lies inside the file,
// that word offsets increase, and that trie values are valid word ids, so a
// damaged file cannot cause out-of-bounds reads. The checksum is optional.
#include <khseg/dictionary.hpp>

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "mapped_file.hpp"

namespace khseg {

namespace {

constexpr char kMagic[8] = {'K', 'H', 'S', 'E', 'G', 'D', 'I', 'C'};
constexpr std::uint32_t kVersion = 2;
constexpr std::uint32_t kByteOrder = 0x01020304;
constexpr std::size_t kSections = 10;
constexpr std::size_t kTableOffset = 72;
constexpr std::size_t kHeaderSize = kTableOffset + kSections * 16;

std::uint64_t checksum(const char* data, std::size_t size) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  std::size_t i = 0;
  for (; i + 8 <= size; i += 8) {
    std::uint64_t w;
    std::memcpy(&w, data + i, 8);
    h = (h ^ w) * 0x100000001b3ull;
  }
  if (i < size) {
    std::uint64_t w = 0;
    std::memcpy(&w, data + i, size - i);
    h = (h ^ w) * 0x100000001b3ull;
  }
  return h;
}

[[noreturn]] void corrupt() { throw std::runtime_error("binary dictionary is truncated or corrupt"); }

template <class T>
T read_at(const char* data, std::size_t offset) {
  T v;
  std::memcpy(&v, data + offset, sizeof(T));
  return v;
}

template <class T>
void write_at(std::string& out, std::size_t offset, const T& v) {
  std::memcpy(out.data() + offset, &v, sizeof(T));
}

bool has_magic(std::string_view bytes) {
  return bytes.size() >= sizeof kMagic && std::memcmp(bytes.data(), kMagic, sizeof kMagic) == 0;
}

bool file_has_magic(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open dictionary " + path.string());
  char head[sizeof kMagic] = {};
  in.read(head, sizeof head);
  return in.gcount() == sizeof head && std::memcmp(head, kMagic, sizeof kMagic) == 0;
}

}  // namespace

std::string Dictionary::to_binary() const {
  std::string out(kHeaderSize, '\0');
  std::memcpy(out.data(), kMagic, sizeof kMagic);
  write_at(out, 8, kVersion);
  write_at(out, 12, kByteOrder);
  write_at<std::uint32_t>(out, 16, format_ == ValueFormat::LogProb ? 1 : 0);
  write_at<std::uint32_t>(out, 20, unknown_cost_ ? 1 : 0);
  write_at<double>(out, 24, unknown_cost_.value_or(0.0));
  write_at(out, 32, total_);
  write_at(out, 40, max_cost_);
  write_at<std::uint64_t>(out, 48, max_len_);
  write_at<std::uint64_t>(out, 56, size());
  write_at<std::uint64_t>(out, 64, kSections);

  std::size_t k = 0;
  auto section = [&](auto span) {
    while (out.size() % 8 != 0) out.push_back('\0');
    write_at<std::uint64_t>(out, kTableOffset + k * 16, out.size());
    write_at<std::uint64_t>(out, kTableOffset + k * 16 + 8, span.size());
    out.append(reinterpret_cast<const char*>(span.data()), span.size_bytes());
    ++k;
  };
  section(word_offsets_);
  section(word_blob_);
  section(counts_);
  section(costs_);
  for (const DoubleArrayTrie* t : {&forward_, &backward_}) {
    section(t->base_array());
    section(t->check_array());
    section(t->value_array());
  }
  while (out.size() % 8 != 0) out.push_back('\0');
  const std::uint64_t sum = checksum(out.data(), out.size());
  out.append(reinterpret_cast<const char*>(&sum), sizeof sum);
  return out;
}

void Dictionary::save_binary(const std::filesystem::path& path) const {
  const std::string bytes = to_binary();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) throw std::runtime_error("error writing " + path.string());
}

Dictionary Dictionary::parse_binary(const char* data, std::size_t size,
                                    std::shared_ptr<const void> owner, bool verify_checksum) {
  if (!has_magic(std::string_view(data, size))) throw std::runtime_error("not a khseg binary dictionary");
  if (size < kHeaderSize + 8) corrupt();
  if (read_at<std::uint32_t>(data, 8) != kVersion) {
    throw std::runtime_error("binary dictionary has an unsupported version; rebuild it with khseg-dict");
  }
  if (read_at<std::uint32_t>(data, 12) != kByteOrder) {
    throw std::runtime_error("binary dictionary was written on a machine with other byte order");
  }
  const std::size_t body = size - 8;
  if (verify_checksum && checksum(data, body) != read_at<std::uint64_t>(data, body)) {
    throw std::runtime_error("binary dictionary checksum mismatch");
  }
  if (read_at<std::uint64_t>(data, 64) != kSections) corrupt();

  const auto n = read_at<std::uint64_t>(data, 56);
  if (n >= 0xFFFFFFFFull) corrupt();

  // Section k as a span of T, after checking alignment and bounds.
  auto section = [&]<class T>(std::size_t k, T*) -> std::span<const T> {
    const auto offset = read_at<std::uint64_t>(data, kTableOffset + k * 16);
    const auto count = read_at<std::uint64_t>(data, kTableOffset + k * 16 + 8);
    if (offset % 8 != 0 || offset < kHeaderSize || offset > body || count > (body - offset) / sizeof(T)) {
      corrupt();
    }
    return {reinterpret_cast<const T*>(data + offset), static_cast<std::size_t>(count)};
  };

  Dictionary d;
  d.format_ = read_at<std::uint32_t>(data, 16) == 1 ? ValueFormat::LogProb : ValueFormat::Count;
  if (read_at<std::uint32_t>(data, 20) & 1u) d.unknown_cost_ = read_at<double>(data, 24);
  d.total_ = read_at<double>(data, 32);
  d.max_cost_ = read_at<double>(data, 40);
  d.max_len_ = static_cast<std::size_t>(read_at<std::uint64_t>(data, 48));

  d.word_offsets_ = section(0, static_cast<std::uint32_t*>(nullptr));
  d.word_blob_ = section(1, static_cast<char32_t*>(nullptr));
  d.counts_ = section(2, static_cast<double*>(nullptr));
  d.costs_ = section(3, static_cast<double*>(nullptr));
  if (d.word_offsets_.size() != n + 1 || d.counts_.size() != n || d.costs_.size() != n) corrupt();
  if (d.word_offsets_.front() != 0 || d.word_offsets_.back() != d.word_blob_.size()) corrupt();
  for (std::size_t i = 0; i < n; ++i) {
    if (d.word_offsets_[i] > d.word_offsets_[i + 1]) corrupt();
  }

  auto* const u32 = static_cast<std::uint32_t*>(nullptr);
  DoubleArrayTrie* tries[2] = {&d.forward_, &d.backward_};
  for (std::size_t t = 0; t < 2; ++t) {
    const auto base = section(4 + t * 3, u32);
    const auto check = section(5 + t * 3, u32);
    const auto value = section(6 + t * 3, u32);
    if (!tries[t]->assign_view(base, check, value, owner)) corrupt();
    for (std::uint32_t v : value) {
      if (v != DoubleArrayTrie::kNone && v >= n) corrupt();
    }
  }
  d.storage_ = std::move(owner);
  return d;
}

Dictionary Dictionary::from_binary(std::string_view bytes, bool verify_checksum) {
  // Copy into 8-byte aligned memory so the arrays can be used in place.
  auto buffer = std::make_shared<std::vector<std::uint64_t>>((bytes.size() + 7) / 8);
  if (!bytes.empty()) std::memcpy(buffer->data(), bytes.data(), bytes.size());
  const char* data = reinterpret_cast<const char*>(buffer->data());
  return parse_binary(data, bytes.size(), std::move(buffer), verify_checksum);
}

Dictionary Dictionary::map_file(const std::filesystem::path& path, bool verify_checksum) {
  auto file = std::make_shared<detail::MappedFile>(path);
  const char* data = file->data();
  const std::size_t size = file->size();
  if (data == nullptr) throw std::runtime_error("dictionary file is empty: " + path.string());
  Dictionary d = parse_binary(data, size, std::move(file), verify_checksum);
  d.mapped_ = true;
  return d;
}

Dictionary Dictionary::from_file(const std::filesystem::path& path, LoadReport* report,
                                 DictionaryOptions options) {
  if (!file_has_magic(path)) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open dictionary " + path.string());
    return from_tsv(in, report, options);
  }
  Dictionary d = map_file(path, options.verify_checksum);
  if (options.unknown_cost) d.unknown_cost_ = options.unknown_cost;
  if (report) report->entries = d.size();
  return d;
}

}  // namespace khseg

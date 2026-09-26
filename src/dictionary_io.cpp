// Binary dictionary format (.khd), little endian:
//
//   char[8]  magic "KHSEGDIC"
//   u32      version (1)
//   u32      byte order mark 0x01020304, written in host order
//   u32      value format (0 count, 1 logprob)
//   u32      flags (bit 0: unknown cost present)
//   f64      unknown cost
//   f64      total count
//   f64      max cost
//   u64      max word length (code points)
//   u64      number of words n
//   u32[n+1] offsets into the word blob, in code points
//   u32[m]   the words back to back as UTF-32, m = offsets[n]
//   f64[n]   counts
//   f64[n]   costs
//   2 x trie (forward, then backward): u64 size m, u32[m] base, check, value
//   u64      FNV-1a hash of every byte before it
//
// The file is read in one piece and every read is bounds checked, so a
// truncated or corrupted file gives an error instead of a crash.
#include <khseg/dictionary.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace khseg {

namespace {

constexpr char kMagic[8] = {'K', 'H', 'S', 'E', 'G', 'D', 'I', 'C'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kByteOrder = 0x01020304;

std::uint64_t fnv1a(std::string_view bytes) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (char c : bytes) {
    h ^= static_cast<unsigned char>(c);
    h *= 0x100000001b3ull;
  }
  return h;
}

class Writer {
 public:
  template <class T>
  void put(const T& v) {
    const auto* p = reinterpret_cast<const char*>(&v);
    out_.append(p, sizeof(T));
  }
  template <class T>
  void put_array(const std::vector<T>& v) {
    if (!v.empty()) out_.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
  }
  void put_bytes(std::string_view s) { out_.append(s); }
  std::string& str() { return out_; }

 private:
  std::string out_;
};

class Reader {
 public:
  explicit Reader(std::string_view data) : data_(data) {}

  template <class T>
  T get() {
    T v;
    std::memcpy(&v, take(sizeof(T)).data(), sizeof(T));
    return v;
  }
  template <class T>
  std::vector<T> get_array(std::uint64_t n) {
    if (n > data_.size() / sizeof(T)) fail();
    std::vector<T> v(static_cast<std::size_t>(n));
    const auto bytes = take(static_cast<std::size_t>(n) * sizeof(T));
    if (n) std::memcpy(v.data(), bytes.data(), bytes.size());
    return v;
  }
  std::string_view take(std::size_t n) {
    if (n > data_.size() - pos_) fail();
    auto s = data_.substr(pos_, n);
    pos_ += n;
    return s;
  }
  std::size_t pos() const { return pos_; }

  [[noreturn]] static void fail() { throw std::runtime_error("binary dictionary is truncated or corrupt"); }

 private:
  std::string_view data_;
  std::size_t pos_ = 0;
};

void put_trie(Writer& w, const DoubleArrayTrie& t) {
  w.put<std::uint64_t>(t.size());
  w.put_array(t.base_array());
  w.put_array(t.check_array());
  w.put_array(t.value_array());
}

DoubleArrayTrie get_trie(Reader& r) {
  const auto m = r.get<std::uint64_t>();
  auto base = r.get_array<std::uint32_t>(m);
  auto check = r.get_array<std::uint32_t>(m);
  auto value = r.get_array<std::uint32_t>(m);
  DoubleArrayTrie t;
  if (!t.assign(std::move(base), std::move(check), std::move(value))) Reader::fail();
  return t;
}

bool has_magic(std::string_view bytes) {
  return bytes.size() >= sizeof kMagic && std::memcmp(bytes.data(), kMagic, sizeof kMagic) == 0;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open dictionary " + path.string());
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) throw std::runtime_error("error reading dictionary " + path.string());
  return data;
}

}  // namespace

std::string Dictionary::to_binary() const {
  Writer w;
  w.put_bytes(std::string_view(kMagic, sizeof kMagic));
  w.put(kVersion);
  w.put(kByteOrder);
  w.put<std::uint32_t>(format_ == ValueFormat::LogProb ? 1 : 0);
  w.put<std::uint32_t>(unknown_cost_ ? 1 : 0);
  w.put<double>(unknown_cost_.value_or(0.0));
  w.put(total_);
  w.put(max_cost_);
  w.put<std::uint64_t>(max_len_);
  w.put<std::uint64_t>(size());
  w.put_array(word_offsets_);
  w.put_bytes(std::string_view(reinterpret_cast<const char*>(word_blob_.data()),
                               word_blob_.size() * sizeof(char32_t)));
  w.put_array(counts_);
  w.put_array(costs_);
  put_trie(w, forward_);
  put_trie(w, backward_);
  w.put(fnv1a(w.str()));
  return std::move(w.str());
}

void Dictionary::save_binary(const std::filesystem::path& path) const {
  const std::string bytes = to_binary();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) throw std::runtime_error("error writing " + path.string());
}

Dictionary Dictionary::from_binary(std::string_view bytes) {
  if (!has_magic(bytes)) throw std::runtime_error("not a khseg binary dictionary");
  if (bytes.size() < sizeof kMagic + sizeof(std::uint64_t)) Reader::fail();
  const std::string_view body = bytes.substr(0, bytes.size() - sizeof(std::uint64_t));
  std::uint64_t stored = 0;
  std::memcpy(&stored, bytes.data() + body.size(), sizeof stored);
  if (stored != fnv1a(body)) throw std::runtime_error("binary dictionary checksum mismatch");

  Reader r(body);
  r.take(sizeof kMagic);
  if (r.get<std::uint32_t>() != kVersion) {
    throw std::runtime_error("binary dictionary has an unsupported version; rebuild it");
  }
  if (r.get<std::uint32_t>() != kByteOrder) {
    throw std::runtime_error("binary dictionary was written on a machine with other byte order");
  }

  Dictionary d;
  d.format_ = r.get<std::uint32_t>() == 1 ? ValueFormat::LogProb : ValueFormat::Count;
  const auto flags = r.get<std::uint32_t>();
  const auto unk = r.get<double>();
  if (flags & 1u) d.unknown_cost_ = unk;
  d.total_ = r.get<double>();
  d.max_cost_ = r.get<double>();
  d.max_len_ = static_cast<std::size_t>(r.get<std::uint64_t>());
  const auto n = r.get<std::uint64_t>();
  if (n >= 0xFFFFFFFFull) Reader::fail();
  d.word_offsets_ = r.get_array<std::uint32_t>(n + 1);
  if (d.word_offsets_.front() != 0) Reader::fail();
  for (std::size_t i = 0; i < n; ++i) {
    if (d.word_offsets_[i] > d.word_offsets_[i + 1]) Reader::fail();
  }
  const auto blob = r.get_array<char32_t>(d.word_offsets_.back());
  d.word_blob_.assign(blob.begin(), blob.end());
  d.counts_ = r.get_array<double>(n);
  d.costs_ = r.get_array<double>(n);
  d.forward_ = get_trie(r);
  d.backward_ = get_trie(r);
  if (r.pos() != body.size()) Reader::fail();
  for (auto v : d.forward_.value_array()) {
    if (v != DoubleArrayTrie::kNone && v >= n) Reader::fail();
  }
  for (auto v : d.backward_.value_array()) {
    if (v != DoubleArrayTrie::kNone && v >= n) Reader::fail();
  }
  return d;
}

Dictionary Dictionary::from_file(const std::filesystem::path& path, LoadReport* report,
                                 DictionaryOptions options) {
  const std::string data = read_file(path);
  if (!has_magic(data)) {
    std::istringstream in(data);
    return from_tsv(in, report, options);
  }
  Dictionary d = from_binary(data);
  if (options.unknown_cost) d.unknown_cost_ = options.unknown_cost;
  if (report) report->entries = d.size();
  return d;
}

}  // namespace khseg

#include "mpq.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "art.hpp"   // read_file

namespace pf {
namespace {

constexpr uint32_t kMagic = 0x1A51504D;   // 'MPQ\x1a'
constexpr uint32_t kFlagExists = 0x80000000;
constexpr uint32_t kFlagEncrypted = 0x00010000;
constexpr uint32_t kFlagFixKey = 0x00020000;
constexpr uint32_t kFlagSingleUnit = 0x01000000;
constexpr uint8_t kCompressionPkware = 0x08;

uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

/// The table every MPQ hash and cipher is built from, generated from a seed.
struct CryptTable {
  uint32_t v[0x500];
  CryptTable() {
    uint32_t seed = 0x00100001;
    for (uint32_t i = 0; i < 256; i++) {
      for (uint32_t k = i, n = 0; n < 5; n++, k += 0x100) {
        seed = (seed * 125 + 3) % 0x2AAAAB;
        const uint32_t a = (seed & 0xFFFF) << 16;
        seed = (seed * 125 + 3) % 0x2AAAAB;
        v[k] = a | (seed & 0xFFFF);
      }
    }
  }
};

const CryptTable& crypt() {
  static const CryptTable table;
  return table;
}

/// MPQ paths are case-insensitive and use backslashes.
std::string normalise(const std::string& name) {
  std::string out = name;
  for (char& c : out) {
    if (c == '/') c = '\\';
    out[size_t(&c - out.data())] = char(toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

uint32_t hash_string(const std::string& name, uint32_t kind) {
  const CryptTable& t = crypt();
  uint32_t seed1 = 0x7FED7FED;
  uint32_t seed2 = 0xEEEEEEEE;
  for (char raw : normalise(name)) {
    const uint32_t ch = uint8_t(raw);
    seed1 = t.v[(kind << 8) + ch] ^ (seed1 + seed2);
    seed2 = ch + seed1 + seed2 + (seed2 << 5) + 3;
  }
  return seed1;
}

void decrypt(uint8_t* data, size_t len, uint32_t key) {
  const CryptTable& t = crypt();
  uint32_t seed2 = 0xEEEEEEEE;
  for (size_t i = 0; i + 4 <= len; i += 4) {
    seed2 += t.v[0x400 + (key & 0xFF)];
    const uint32_t value = rd32(data + i) ^ (key + seed2);
    key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
    seed2 = value + seed2 + (seed2 << 5) + 3;
    data[i] = uint8_t(value);
    data[i + 1] = uint8_t(value >> 8);
    data[i + 2] = uint8_t(value >> 16);
    data[i + 3] = uint8_t(value >> 24);
  }
}

// ------------------------------------------------------------ PKWARE DCL
//
// Derived from Mark Adler's `blast.c`, in zlib's `contrib/blast/`. The four
// tables below are his verbatim, and so is the shape of the decoder: the
// canonical-code walk, the inverted bits, and the run-length encoding of the
// code lengths — that last one is his compaction rather than anything the
// format asks for, which is what makes this derivation and not coincidence.
//
// Rewritten rather than vendored, but derived is derived. blast.c is under the
// zlib licence, which asks that its origin not be misrepresented and that an
// altered version say that it is one. This is one.

/// Bit lengths, run-length encoded: high nibble is repeat-1, low nibble is the
/// length itself. Note *is*, not plus one — getting that wrong decodes every
/// stream into nonsense.
const uint8_t kLenLengths[] = {2, 35, 36, 53, 38, 23};
const uint8_t kDistLengths[] = {2, 20, 53, 230, 247, 151, 248};
const uint16_t kLenBase[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
const uint8_t kLenExtra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

struct Huffman {
  int count[16] = {};
  std::vector<int> symbol;

  Huffman(const uint8_t* rle, size_t n) {
    std::vector<int> lengths;
    for (size_t i = 0; i < n; i++) {
      const int repeat = (rle[i] >> 4) + 1;
      const int bits = rle[i] & 15;
      for (int r = 0; r < repeat; r++) lengths.push_back(bits);
    }
    for (int length : lengths) count[length]++;

    int offsets[16] = {};
    int total = 0;
    for (int length = 1; length < 16; length++) {
      offsets[length] = total;
      total += count[length];
    }
    symbol.assign(size_t(total), 0);
    for (size_t sym = 0; sym < lengths.size(); sym++) {
      symbol[size_t(offsets[lengths[sym]]++)] = int(sym);
    }
  }
};

const Huffman& length_code() {
  static const Huffman code(kLenLengths, sizeof(kLenLengths));
  return code;
}
const Huffman& distance_code() {
  static const Huffman code(kDistLengths, sizeof(kDistLengths));
  return code;
}

struct BitReader {
  const uint8_t* data;
  size_t len;
  size_t pos = 0;
  uint32_t buffer = 0;
  int bits = 0;
  bool overrun = false;

  int take(int need) {
    while (bits < need) {
      if (pos >= len) { overrun = true; return 0; }
      buffer |= uint32_t(data[pos++]) << bits;
      bits += 8;
    }
    const int value = int(buffer & ((1u << need) - 1));
    buffer >>= need;
    bits -= need;
    return value;
  }
};

/// Walk a canonical code one bit at a time. PKWARE stores its codes inverted.
int decode_symbol(BitReader& in, const Huffman& code) {
  int value = 0, first = 0, index = 0;
  for (int length = 1; length < 16; length++) {
    value |= in.take(1) ^ 1;
    if (in.overrun) return -1;
    const int n = code.count[length];
    if (value - n < first) return code.symbol[size_t(index + (value - first))];
    index += n;
    first = (first + n) << 1;
    value <<= 1;
  }
  return -1;
}

}  // namespace

std::vector<uint8_t> pkware_explode(const uint8_t* data, size_t len) {
  std::vector<uint8_t> out;
  if (len < 3) return out;
  // Literal mode 1 means Huffman-coded literals. No Warcraft II sector uses
  // it, so it is refused rather than implemented from imagination.
  if (data[0] != 0) return out;
  const int dict_bits = data[1];
  if (dict_bits < 4 || dict_bits > 6) return out;

  BitReader in{data + 2, len - 2};
  while (true) {
    const int coded = in.take(1);
    if (in.overrun) break;

    if (!coded) {
      out.push_back(uint8_t(in.take(8)));
      if (in.overrun) break;
      continue;
    }

    const int sym = decode_symbol(in, length_code());
    if (sym < 0) break;
    const int length = kLenBase[sym] + in.take(kLenExtra[sym]);
    if (in.overrun) break;
    if (length == 519) break;                 // end of stream

    const int extra = length == 2 ? 2 : dict_bits;
    const int dsym = decode_symbol(in, distance_code());
    if (dsym < 0) break;
    const size_t distance = size_t(dsym << extra) + size_t(in.take(extra)) + 1;
    if (in.overrun || distance > out.size()) break;

    for (int i = 0; i < length; i++) out.push_back(out[out.size() - distance]);
  }
  return out;
}

// ---------------------------------------------------------------- archive

MpqArchive* MpqArchive::open(const std::string& path, Status& status) {
  std::vector<uint8_t> bytes;
  if (!read_file(path, bytes)) {
    status = Status::Malformed;
    return nullptr;
  }
  return open_bytes(std::move(bytes), status);
}

MpqArchive* MpqArchive::open_bytes(std::vector<uint8_t> bytes, Status& status) {
  auto* archive = new MpqArchive();
  archive->data_ = std::move(bytes);
  if (!archive->parse(status)) {
    delete archive;
    return nullptr;
  }
  status = Status::Ok;
  return archive;
}

bool MpqArchive::parse(Status& status) {
  status = Status::Malformed;
  if (data_.size() < 32 || rd32(data_.data()) != kMagic) return false;

  const uint16_t version = rd16(&data_[12]);
  if (version != 0) return false;             // only what Warcraft II ships
  sector_size_ = 512u << rd16(&data_[14]);

  const uint32_t hash_pos = rd32(&data_[16]);
  const uint32_t block_pos = rd32(&data_[20]);
  hash_count_ = rd32(&data_[24]);
  block_count_ = rd32(&data_[28]);

  const size_t hash_bytes = size_t(hash_count_) * 16;
  const size_t block_bytes = size_t(block_count_) * 16;
  if (hash_pos + hash_bytes > data_.size()) return false;
  if (block_pos + block_bytes > data_.size()) return false;

  hash_table_.assign(data_.begin() + hash_pos, data_.begin() + hash_pos + hash_bytes);
  block_table_.assign(data_.begin() + block_pos, data_.begin() + block_pos + block_bytes);
  decrypt(hash_table_.data(), hash_table_.size(), hash_string("(hash table)", 3));
  decrypt(block_table_.data(), block_table_.size(), hash_string("(block table)", 3));

  std::vector<uint8_t> list;
  if (read("(listfile)", list)) {
    std::string current;
    for (uint8_t c : list) {
      if (c == '\r') continue;
      if (c == '\n') {
        if (!current.empty()) listfile_.push_back(current);
        current.clear();
      } else {
        current.push_back(char(c));
      }
    }
    if (!current.empty()) listfile_.push_back(current);
  }

  status = Status::Ok;
  return true;
}

int MpqArchive::find_block(const std::string& name) const {
  if (hash_count_ == 0) return -1;
  const uint32_t start = hash_string(name, 0) % hash_count_;
  const uint32_t want_a = hash_string(name, 1);
  const uint32_t want_b = hash_string(name, 2);

  for (uint32_t probe = 0; probe < hash_count_; probe++) {
    const uint32_t slot = (start + probe) % hash_count_;
    const uint8_t* entry = &hash_table_[size_t(slot) * 16];
    const int32_t block = int32_t(rd32(entry + 12));
    if (block == -1) return -1;               // never used: the name is absent
    if (rd32(entry) == want_a && rd32(entry + 4) == want_b && block >= 0) {
      return block;
    }
  }
  return -1;
}

bool MpqArchive::contains(const std::string& name) const {
  return find_block(name) >= 0;
}

bool MpqArchive::read(const std::string& name, std::vector<uint8_t>& out) const {
  out.clear();
  const int block = find_block(name);
  if (block < 0 || uint32_t(block) >= block_count_) return false;

  const uint8_t* entry = &block_table_[size_t(block) * 16];
  const uint32_t pos = rd32(entry);
  const uint32_t packed = rd32(entry + 4);
  const uint32_t size = rd32(entry + 8);
  const uint32_t flags = rd32(entry + 12);
  if (!(flags & kFlagExists) || size == 0) return false;
  if (size_t(pos) + packed > data_.size()) return false;

  uint32_t key = 0;
  const bool encrypted = (flags & kFlagEncrypted) != 0;
  if (encrypted) {
    // The key is the *file name* alone, not the path.
    const std::string norm = normalise(name);
    const size_t slash = norm.find_last_of('\\');
    key = hash_string(slash == std::string::npos ? norm : norm.substr(slash + 1), 3);
    if (flags & kFlagFixKey) key = (key + pos) ^ size;
  }

  auto explode_or_copy = [&](std::vector<uint8_t> raw, uint32_t want) {
    if (raw.size() >= want) {                 // stored: it did not compress
      raw.resize(want);
      return raw;
    }
    if (raw.empty() || raw[0] != kCompressionPkware) return std::vector<uint8_t>();
    return pkware_explode(raw.data() + 1, raw.size() - 1);
  };

  if (flags & kFlagSingleUnit) {
    std::vector<uint8_t> raw(data_.begin() + pos, data_.begin() + pos + packed);
    if (encrypted) decrypt(raw.data(), raw.size(), key);
    out = explode_or_copy(std::move(raw), size);
    return out.size() == size;
  }

  const uint32_t sectors = (size + sector_size_ - 1) / sector_size_;
  const size_t table_bytes = size_t(sectors + 1) * 4;
  if (size_t(pos) + table_bytes > data_.size()) return false;

  std::vector<uint8_t> table(data_.begin() + pos, data_.begin() + pos + table_bytes);
  if (encrypted) decrypt(table.data(), table.size(), key - 1);

  out.reserve(size);
  for (uint32_t i = 0; i < sectors; i++) {
    const uint32_t from = rd32(&table[size_t(i) * 4]);
    const uint32_t to = rd32(&table[size_t(i + 1) * 4]);
    if (to < from || size_t(pos) + to > data_.size()) return false;
    const uint32_t want = std::min(sector_size_, size - i * sector_size_);

    std::vector<uint8_t> raw(data_.begin() + pos + from, data_.begin() + pos + to);
    if (encrypted) decrypt(raw.data(), raw.size(), key + i);
    std::vector<uint8_t> chunk = explode_or_copy(std::move(raw), want);
    if (chunk.size() != want) return false;
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  out.resize(size);
  return true;
}

// ------------------------------------------------------------ data source

DataSource::~DataSource() {
  for (MpqArchive* archive : archives_) delete archive;
}

bool DataSource::add_archive(const std::string& path) {
  Status status = Status::Ok;
  MpqArchive* archive = MpqArchive::open(path, status);
  if (!archive) return false;
  archives_.push_back(archive);
  return true;
}

int DataSource::add_directory(const std::string& dir) {
  // Base first, then the patch, so the patch wins. Anything else found is added
  // after both, so a mod archive dropped into the folder overrides.
  const char* ordered[] = {"War2Dat.mpq", "War2Patch.mpq"};
  int opened = 0;
  std::vector<std::string> taken;
  for (const char* name : ordered) {
    const std::string path = dir + "/" + name;
    if (add_archive(path)) { opened++; taken.push_back(name); }
  }

  std::error_code ec;
  if (std::filesystem::is_directory(dir, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;
      std::string ext = entry.path().extension().string();
      for (char& c : ext) c = char(tolower(static_cast<unsigned char>(c)));
      if (ext != ".mpq") continue;
      const std::string leaf = entry.path().filename().string();
      if (std::find(taken.begin(), taken.end(), leaf) != taken.end()) continue;
      if (add_archive(entry.path().string())) opened++;
    }
  }
  return opened;
}

void DataSource::add_files(const std::string& dir) { dirs_.push_back(dir); }

bool DataSource::read(const std::string& name, std::vector<uint8_t>& out) const {
  // Loose files first: an unpacked copy keeps working, and a mod folder can
  // shadow the archives without repacking anything.
  std::string relative = name;
  for (char& c : relative) {
    if (c == '\\') c = '/';
  }
  for (const std::string& dir : dirs_) {
    if (read_file(dir + "/" + relative, out)) return true;
  }
  for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
    if ((*it)->read(name, out)) return true;
  }
  out.clear();
  return false;
}

}  // namespace pf

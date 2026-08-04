#include "tbl.hpp"

namespace pf {
namespace {

/// The 32 places cp1252 differs from Latin-1, as Unicode code points.
///
/// 0x80 to 0x9F are control codes in Latin-1 and printable in cp1252, and 0x92
/// is the curly apostrophe a localised table uses inside one of the hero names.
const uint16_t kHighRange[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

void append_utf8(std::string& out, uint32_t code) {
  if (code < 0x80) {
    out.push_back(char(code));
  } else if (code < 0x800) {
    out.push_back(char(0xC0 | (code >> 6)));
    out.push_back(char(0x80 | (code & 0x3F)));
  } else {
    out.push_back(char(0xE0 | (code >> 12)));
    out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(char(0x80 | (code & 0x3F)));
  }
}

}  // namespace

std::string cp1252_to_utf8(const char* bytes, size_t length) {
  std::string out;
  out.reserve(length);
  for (size_t i = 0; i < length; i++) {
    const uint8_t c = uint8_t(bytes[i]);
    if (c < 0x80) out.push_back(char(c));
    else if (c < 0xA0) append_utf8(out, kHighRange[c - 0x80]);
    else append_utf8(out, c);   // 0xA0 and up agree with Latin-1
  }
  return out;
}

bool Tbl::parse(const uint8_t* bytes, size_t length) {
  strings_.clear();
  flat_.clear();
  if (!bytes || length < 2) return false;

  const size_t count = size_t(bytes[0]) | (size_t(bytes[1]) << 8);
  // The offset table has to fit, and a table of nothing is not a table.
  if (count == 0 || 2 + count * 2 > length) return false;

  std::vector<std::string> parsed;
  parsed.reserve(count);
  for (size_t i = 0; i < count; i++) {
    const size_t at = 2 + i * 2;
    const size_t offset = size_t(bytes[at]) | (size_t(bytes[at + 1]) << 8);
    // An offset past the end is the shape a wrong file has. Refuse the whole
    // table rather than the one string, or half a table read out of something
    // that is not one becomes names nobody can account for.
    if (offset >= length) return false;
    size_t end = offset;
    while (end < length && bytes[end] != 0) end++;
    parsed.push_back(
        cp1252_to_utf8(reinterpret_cast<const char*>(bytes + offset), end - offset));
  }
  strings_ = std::move(parsed);
  // One pass now rather than a copy per lookup: a name is handed out as a
  // pointer into this, so it has to outlive the call.
  flat_.reserve(strings_.size());
  for (const std::string& one : strings_) {
    std::string line = one;
    for (char& c : line) {
      if (c == '\n' || c == '\r') c = ' ';
    }
    flat_.push_back(std::move(line));
  }
  return true;
}

const std::string& Tbl::at(int index) const {
  static const std::string kNone;
  if (index < 0 || index >= int(strings_.size())) return kNone;
  return strings_[size_t(index)];
}

const std::string& Tbl::flat(int index) const {
  static const std::string kNone;
  if (index < 0 || index >= int(flat_.size())) return kNone;
  return flat_[size_t(index)];
}

}  // namespace pf

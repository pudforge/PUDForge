#include "png.hpp"

#include <cstring>

namespace pf {
namespace {

// ------------------------------------------------------------ checksums

const uint32_t* crc_table() {
  static uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    built = true;
  }
  return table;
}

// ------------------------------------------------------------ bit output

/// Least-significant-bit-first bit writer, which is the order DEFLATE uses for
/// everything except Huffman codes — those are written most-significant first,
/// so they get their own call.
struct BitWriter {
  std::vector<uint8_t> out;
  uint32_t bits = 0;
  int count = 0;

  void put(uint32_t value, int width) {
    bits |= (value & ((1u << width) - 1)) << count;
    count += width;
    while (count >= 8) {
      out.push_back(uint8_t(bits & 0xff));
      bits >>= 8;
      count -= 8;
    }
  }

  /// A Huffman code, whose bits travel in the opposite order to everything else.
  void put_code(uint32_t code, int width) {
    for (int i = width - 1; i >= 0; i--) put((code >> i) & 1, 1);
  }

  void flush() {
    if (count > 0) out.push_back(uint8_t(bits & 0xff));
    bits = 0;
    count = 0;
  }
};

// -------------------------------------------------- fixed Huffman tables
//
// RFC 1951 §3.2.6. Literals 0-143 are eight bits, 144-255 are nine, the
// end-of-block and short lengths are seven, and the rest are eight.

void literal_code(int symbol, uint32_t& code, int& width) {
  if (symbol < 144) { code = uint32_t(0x30 + symbol); width = 8; }
  else if (symbol < 256) { code = uint32_t(0x190 + symbol - 144); width = 9; }
  else if (symbol < 280) { code = uint32_t(symbol - 256); width = 7; }
  else { code = uint32_t(0xc0 + symbol - 280); width = 8; }
}

struct Span { int symbol; int extra_bits; int base; };

/// Length codes 257-285, and the distance codes, from RFC 1951 §3.2.5.
const Span kLengths[] = {
    {257, 0, 3},   {258, 0, 4},   {259, 0, 5},   {260, 0, 6},   {261, 0, 7},
    {262, 0, 8},   {263, 0, 9},   {264, 0, 10},  {265, 1, 11},  {266, 1, 13},
    {267, 1, 15},  {268, 1, 17},  {269, 2, 19},  {270, 2, 23},  {271, 2, 27},
    {272, 2, 31},  {273, 3, 35},  {274, 3, 43},  {275, 3, 51},  {276, 3, 59},
    {277, 4, 67},  {278, 4, 83},  {279, 4, 99},  {280, 4, 115}, {281, 5, 131},
    {282, 5, 163}, {283, 5, 195}, {284, 5, 227}, {285, 0, 258},
};
const Span kDistances[] = {
    {0, 0, 1},      {1, 0, 2},      {2, 0, 3},      {3, 0, 4},
    {4, 1, 5},      {5, 1, 7},      {6, 2, 9},      {7, 2, 13},
    {8, 3, 17},     {9, 3, 25},     {10, 4, 33},    {11, 4, 49},
    {12, 5, 65},    {13, 5, 97},    {14, 6, 129},   {15, 6, 193},
    {16, 7, 257},   {17, 7, 385},   {18, 8, 513},   {19, 8, 769},
    {20, 9, 1025},  {21, 9, 1537},  {22, 10, 2049}, {23, 10, 3073},
    {24, 11, 4097}, {25, 11, 6145}, {26, 12, 8193}, {27, 12, 12289},
    {28, 13, 16385}, {29, 13, 24577},
};

template <size_t N>
const Span& span_for(const Span (&table)[N], int value) {
  size_t i = 0;
  while (i + 1 < N && table[i + 1].base <= value) i++;
  return table[i];
}

// ------------------------------------------------------------------ LZ77

constexpr int kWindow = 32768;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;
/// How far back to follow one hash chain. The cap is what keeps this linear on
/// long runs of identical pixels, which map renders are full of.
constexpr int kMaxChain = 64;

inline uint32_t hash3(const uint8_t* p) {
  return ((uint32_t(p[0]) << 10) ^ (uint32_t(p[1]) << 5) ^ uint32_t(p[2])) &
         (kHashSize - 1);
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t length, uint32_t seed) {
  const uint32_t* table = crc_table();
  uint32_t c = seed ^ 0xffffffffu;
  for (size_t i = 0; i < length; i++) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

uint32_t adler32(const uint8_t* data, size_t length) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < length; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

std::vector<uint8_t> zlib_compress(const uint8_t* data, size_t length) {
  BitWriter w;
  w.out.reserve(length / 2 + 64);
  w.out.push_back(0x78);   // CM = deflate, CINFO = 32K window
  w.out.push_back(0x01);   // FCHECK, no preset dictionary, fastest

  w.put(1, 1);   // final block
  w.put(1, 2);   // fixed Huffman codes

  std::vector<int> head(kHashSize, -1);
  std::vector<int> prev(length + 1, -1);

  size_t pos = 0;
  while (pos < length) {
    int best_len = 0, best_dist = 0;
    if (pos + kMinMatch <= length) {
      const uint32_t h = hash3(data + pos);
      int candidate = head[h];
      int chain = kMaxChain;
      const size_t limit = pos > kWindow ? pos - kWindow : 0;
      while (candidate >= 0 && size_t(candidate) >= limit && chain-- > 0) {
        const size_t max_here =
            (length - pos) < kMaxMatch ? (length - pos) : size_t(kMaxMatch);
        size_t n = 0;
        while (n < max_here && data[size_t(candidate) + n] == data[pos + n]) n++;
        if (int(n) > best_len) {
          best_len = int(n);
          best_dist = int(pos - size_t(candidate));
          if (best_len >= kMaxMatch) break;
        }
        candidate = prev[size_t(candidate)];
      }
      prev[pos] = head[h];
      head[h] = int(pos);
    }

    if (best_len >= kMinMatch) {
      const Span& len = span_for(kLengths, best_len);
      uint32_t code = 0;
      int width = 0;
      literal_code(len.symbol, code, width);
      w.put_code(code, width);
      if (len.extra_bits) w.put(uint32_t(best_len - len.base), len.extra_bits);

      const Span& dist = span_for(kDistances, best_dist);
      w.put_code(uint32_t(dist.symbol), 5);   // distance codes are 5-bit fixed
      if (dist.extra_bits) w.put(uint32_t(best_dist - dist.base), dist.extra_bits);

      // Every position inside the match still has to enter the hash chains, or
      // later matches lose the anchors they would have found.
      for (int i = 1; i < best_len; i++) {
        const size_t at = pos + size_t(i);
        if (at + kMinMatch > length) break;
        const uint32_t h2 = hash3(data + at);
        prev[at] = head[h2];
        head[h2] = int(at);
      }
      pos += size_t(best_len);
    } else {
      uint32_t code = 0;
      int width = 0;
      literal_code(int(data[pos]), code, width);
      w.put_code(code, width);
      pos++;
    }
  }

  uint32_t end_code = 0;
  int end_width = 0;
  literal_code(256, end_code, end_width);   // end of block
  w.put_code(end_code, end_width);
  w.flush();

  const uint32_t sum = adler32(data, length);
  w.out.push_back(uint8_t(sum >> 24));
  w.out.push_back(uint8_t(sum >> 16));
  w.out.push_back(uint8_t(sum >> 8));
  w.out.push_back(uint8_t(sum));
  return w.out;
}

std::vector<uint8_t> encode_png(const uint32_t* rgba, int width, int height) {
  std::vector<uint8_t> out;
  if (!rgba || width <= 0 || height <= 0) return out;

  // Filter type 1 (Sub) on every row: map renders are wide runs of one colour,
  // so subtracting the pixel to the left turns most of a row into zeros before
  // the compressor sees it.
  const size_t stride = size_t(width) * 4;
  std::vector<uint8_t> raw(size_t(height) * (stride + 1));
  for (int y = 0; y < height; y++) {
    uint8_t* row = raw.data() + size_t(y) * (stride + 1);
    row[0] = 1;
    const uint32_t* src = rgba + size_t(y) * size_t(width);
    for (int x = 0; x < width; x++) {
      for (int c = 0; c < 4; c++) {
        const uint8_t here = uint8_t((src[x] >> (8 * c)) & 0xff);
        // Sub subtracts the *unfiltered* pixel to the left, so read it from the
        // source rather than from the row already written.
        const uint8_t left = x > 0 ? uint8_t((src[x - 1] >> (8 * c)) & 0xff) : 0;
        row[1 + size_t(x) * 4 + size_t(c)] = uint8_t(here - left);
      }
    }
  }

  const std::vector<uint8_t> data = zlib_compress(raw.data(), raw.size());

  auto be32 = [](std::vector<uint8_t>& v, uint32_t n) {
    v.push_back(uint8_t(n >> 24));
    v.push_back(uint8_t(n >> 16));
    v.push_back(uint8_t(n >> 8));
    v.push_back(uint8_t(n));
  };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& payload) {
    be32(out, uint32_t(payload.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    be32(out, crc32(out.data() + start, out.size() - start));
  };

  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  out.insert(out.end(), signature, signature + 8);

  std::vector<uint8_t> ihdr;
  be32(ihdr, uint32_t(width));
  be32(ihdr, uint32_t(height));
  ihdr.push_back(8);   // bit depth
  ihdr.push_back(6);   // colour type: truecolour with alpha
  ihdr.push_back(0);   // deflate
  ihdr.push_back(0);   // adaptive filtering
  ihdr.push_back(0);   // no interlace
  chunk("IHDR", ihdr);
  chunk("IDAT", data);
  chunk("IEND", {});
  return out;
}

}  // namespace pf

// The other half of png.cpp: reading one back.
//
// The encoder has been here since the first release because any client has to
// be able to write out what it rendered. Reading was never needed until the
// Remastered artwork, which ships as PNG atlases rather than the .grp files
// the Battle.net edition keeps in its archives — so a client that wants that
// artwork needs a decoder, and putting it here rather than in the Windows
// client is what stops it being written once per platform.
//
// Self-contained for the same reason the encoder is: the core has no
// third-party dependencies. The encoder emits fixed Huffman codes only, so
// none of the inflate below could be borrowed from it — a general decoder has
// to read dynamic codes, which is most of what DEFLATE is.
//
// Correctness over speed. The decode is a one-off import, off whatever thread
// the caller likes, so this walks the Huffman codes a bit at a time rather
// than building the lookup tables a decompressor in a hot loop would want.

#include "png.hpp"

#include <cstring>

namespace pf {
namespace {

// ------------------------------------------------------------------ DEFLATE

/// Bits, least-significant first, which is the order DEFLATE packs them in.
struct BitReader {
  const uint8_t* data = nullptr;
  size_t length = 0;
  size_t at = 0;
  uint32_t bits = 0;
  int count = 0;
  bool overrun = false;

  int take(int want) {
    while (count < want) {
      if (at >= length) { overrun = true; return 0; }
      bits |= uint32_t(data[at++]) << count;
      count += 8;
    }
    const int value = int(bits & ((1u << want) - 1));
    bits >>= want;
    count -= want;
    return value;
  }

  void align() { bits = 0; count = 0; }
};

/// A canonical Huffman code, held as the counts per length and the symbols in
/// code order — which is all decoding needs and all the format states.
struct Huffman {
  static constexpr int kMaxBits = 15;
  int count[kMaxBits + 1] = {};
  std::vector<int> symbol;

  /// @param lengths code length per symbol, zero where the symbol is unused
  bool build(const std::vector<int>& lengths) {
    for (int i = 0; i <= kMaxBits; i++) count[i] = 0;
    for (int len : lengths) {
      if (len < 0 || len > kMaxBits) return false;
      count[len]++;
    }
    // All-zero is legal for the distance tree of a block that never uses one.
    if (count[0] == int(lengths.size())) { symbol.clear(); return true; }

    // Every code must be used exactly once: a tree with room left over, or one
    // that overruns, is a stream we should refuse rather than guess at.
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
      left <<= 1;
      left -= count[len];
      if (left < 0) return false;
    }

    int offsets[kMaxBits + 2] = {};
    for (int len = 1; len <= kMaxBits; len++) offsets[len + 1] = offsets[len] + count[len];
    symbol.assign(lengths.size(), 0);
    for (size_t s = 0; s < lengths.size(); s++) {
      if (lengths[s]) symbol[size_t(offsets[lengths[s]]++)] = int(s);
    }
    return true;
  }

  /// One symbol, or -1 when the bits do not name one.
  int decode(BitReader& in) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; len++) {
      code |= in.take(1);
      if (in.overrun) return -1;
      const int n = count[len];
      if (code - first < n) return symbol[size_t(index + (code - first))];
      index += n;
      first = (first + n) << 1;
      code <<= 1;
    }
    return -1;
  }
};

// Length and distance tables, straight from the format's own.
const int kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                          31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                           33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                           1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_block(BitReader& in, const Huffman& lit, const Huffman& dist,
                   std::vector<uint8_t>& out) {
  for (;;) {
    const int symbol = lit.decode(in);
    if (symbol < 0) return false;
    if (symbol < 256) {
      out.push_back(uint8_t(symbol));
      continue;
    }
    if (symbol == 256) return true;             // end of block
    const int len_index = symbol - 257;
    if (len_index >= 29) return false;
    const int length = kLenBase[len_index] + in.take(kLenExtra[len_index]);
    const int dist_symbol = dist.decode(in);
    if (dist_symbol < 0 || dist_symbol >= 30) return false;
    const size_t distance =
        size_t(kDistBase[dist_symbol]) + size_t(in.take(kDistExtra[dist_symbol]));
    if (in.overrun || distance == 0 || distance > out.size()) return false;
    // Byte at a time on purpose: the copy may overlap itself, which is how
    // DEFLATE writes a run.
    const size_t from = out.size() - distance;
    for (int i = 0; i < length; i++) out.push_back(out[from + size_t(i)]);
  }
}

bool fixed_trees(Huffman& lit, Huffman& dist) {
  std::vector<int> lengths(288);
  for (int i = 0; i < 144; i++) lengths[size_t(i)] = 8;
  for (int i = 144; i < 256; i++) lengths[size_t(i)] = 9;
  for (int i = 256; i < 280; i++) lengths[size_t(i)] = 7;
  for (int i = 280; i < 288; i++) lengths[size_t(i)] = 8;
  if (!lit.build(lengths)) return false;
  return dist.build(std::vector<int>(30, 5));
}

bool dynamic_trees(BitReader& in, Huffman& lit, Huffman& dist) {
  const int nlen = in.take(5) + 257;
  const int ndist = in.take(5) + 1;
  const int ncode = in.take(4) + 4;
  if (in.overrun || nlen > 288 || ndist > 30) return false;

  // The order the lengths of the code-length alphabet arrive in, which is the
  // format's and not a sorted one.
  static const int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                 11, 4,  12, 3, 13, 2, 14, 1, 15};
  std::vector<int> code_lengths(19, 0);
  for (int i = 0; i < ncode; i++) code_lengths[size_t(kOrder[i])] = in.take(3);
  if (in.overrun) return false;
  Huffman code_tree;
  if (!code_tree.build(code_lengths)) return false;

  std::vector<int> lengths;
  lengths.reserve(size_t(nlen + ndist));
  while (int(lengths.size()) < nlen + ndist) {
    const int symbol = code_tree.decode(in);
    if (symbol < 0) return false;
    if (symbol < 16) {
      lengths.push_back(symbol);
    } else if (symbol == 16) {
      if (lengths.empty()) return false;
      const int previous = lengths.back();
      for (int n = 3 + in.take(2); n > 0; n--) lengths.push_back(previous);
    } else if (symbol == 17) {
      for (int n = 3 + in.take(3); n > 0; n--) lengths.push_back(0);
    } else {
      for (int n = 11 + in.take(7); n > 0; n--) lengths.push_back(0);
    }
    if (in.overrun) return false;
  }
  if (int(lengths.size()) != nlen + ndist) return false;

  const std::vector<int> lit_lengths(lengths.begin(), lengths.begin() + nlen);
  const std::vector<int> dist_lengths(lengths.begin() + nlen, lengths.end());
  if (!lit.build(lit_lengths)) return false;
  return dist.build(dist_lengths);
}

// ------------------------------------------------------------------- PNG

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

/// Undo the per-row filter, in place, over rows of `stride` bytes each
/// preceded by their filter byte. `bpp` is bytes per pixel, which is what the
/// Sub and Paeth filters step back by.
bool unfilter(std::vector<uint8_t>& raw, int width, int height, int bpp) {
  const size_t stride = size_t(width) * size_t(bpp);
  if (raw.size() < (stride + 1) * size_t(height)) return false;
  std::vector<uint8_t> out(stride * size_t(height));
  const uint8_t* in = raw.data();
  for (int y = 0; y < height; y++) {
    const int filter = *in++;
    uint8_t* row = out.data() + stride * size_t(y);
    const uint8_t* prior = y ? out.data() + stride * size_t(y - 1) : nullptr;
    for (size_t x = 0; x < stride; x++) {
      const int left = x >= size_t(bpp) ? row[x - size_t(bpp)] : 0;
      const int up = prior ? prior[x] : 0;
      const int upleft = (prior && x >= size_t(bpp)) ? prior[x - size_t(bpp)] : 0;
      int value = in[x];
      switch (filter) {
        case 0: break;
        case 1: value += left; break;
        case 2: value += up; break;
        case 3: value += (left + up) / 2; break;
        case 4: value += paeth(left, up, upleft); break;
        default: return false;
      }
      row[x] = uint8_t(value);
    }
    in += stride;
  }
  raw.swap(out);
  return true;
}

}  // namespace

std::vector<uint8_t> zlib_decompress(const uint8_t* data, size_t length) {
  std::vector<uint8_t> out;
  if (!data || length < 2) return out;
  // The zlib wrapper: compression method 8, and a header that checks out as a
  // multiple of 31. A preset dictionary is not something a PNG carries.
  if ((data[0] & 0x0f) != 8) return out;
  if (((uint32_t(data[0]) << 8) | data[1]) % 31 != 0) return out;
  if (data[1] & 0x20) return out;

  BitReader in;
  in.data = data + 2;
  in.length = length - 2;

  for (;;) {
    const int last = in.take(1);
    const int kind = in.take(2);
    if (in.overrun) return {};
    if (kind == 0) {
      in.align();
      if (in.at + 4 > in.length) return {};
      const uint32_t len = uint32_t(in.data[in.at]) | (uint32_t(in.data[in.at + 1]) << 8);
      in.at += 4;                                    // length, then its complement
      if (in.at + len > in.length) return {};
      out.insert(out.end(), in.data + in.at, in.data + in.at + len);
      in.at += len;
    } else if (kind == 1 || kind == 2) {
      Huffman lit, dist;
      const bool built = kind == 1 ? fixed_trees(lit, dist) : dynamic_trees(in, lit, dist);
      if (!built || !inflate_block(in, lit, dist, out)) return {};
    } else {
      return {};
    }
    if (last) break;
  }
  return out;
}

std::vector<uint32_t> decode_png(const uint8_t* bytes, size_t length, int* width,
                                 int* height) {
  if (width) *width = 0;
  if (height) *height = 0;
  static const uint8_t kSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (!bytes || length < 8 + 25 || std::memcmp(bytes, kSignature, 8) != 0) return {};

  int w = 0, h = 0, depth = 0, colour = 0;
  bool seen_header = false;
  std::vector<uint8_t> idat;
  std::vector<uint8_t> palette;      // RGB triples
  std::vector<uint8_t> alpha;        // tRNS for a palette

  size_t at = 8;
  while (at + 8 <= length) {
    const uint32_t size = be32(bytes + at);
    const char* kind = reinterpret_cast<const char*>(bytes + at + 4);
    const uint8_t* body = bytes + at + 8;
    if (size > length || at + 12 + size > length) return {};

    if (!std::memcmp(kind, "IHDR", 4)) {
      if (size < 13) return {};
      w = int(be32(body));
      h = int(be32(body + 4));
      depth = body[8];
      colour = body[9];
      // Interlaced files are a second pass structure the atlases never use,
      // so they are refused rather than half-supported.
      if (body[12] != 0) return {};
      if (w <= 0 || h <= 0 || depth != 8) return {};
      if (colour != 0 && colour != 2 && colour != 3 && colour != 4 && colour != 6) return {};
      seen_header = true;
    } else if (!std::memcmp(kind, "PLTE", 4)) {
      palette.assign(body, body + size);
    } else if (!std::memcmp(kind, "tRNS", 4)) {
      alpha.assign(body, body + size);
    } else if (!std::memcmp(kind, "IDAT", 4)) {
      idat.insert(idat.end(), body, body + size);
    } else if (!std::memcmp(kind, "IEND", 4)) {
      break;
    }
    at += 12 + size;                               // length, type, body, CRC
  }
  if (!seen_header || idat.empty()) return {};

  static const int kChannels[7] = {1, 0, 3, 1, 2, 0, 4};
  const int channels = kChannels[colour];
  std::vector<uint8_t> raw = zlib_decompress(idat.data(), idat.size());
  if (raw.empty() || !unfilter(raw, w, h, channels)) return {};

  std::vector<uint32_t> out(size_t(w) * size_t(h), 0);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = raw.data() + size_t(y) * size_t(w) * size_t(channels);
    for (int x = 0; x < w; x++) {
      const uint8_t* p = row + size_t(x) * size_t(channels);
      uint8_t r = 0, g = 0, b = 0, a = 255;
      switch (colour) {
        case 0: r = g = b = p[0]; break;                       // grey
        case 2: r = p[0]; g = p[1]; b = p[2]; break;           // RGB
        case 4: r = g = b = p[0]; a = p[1]; break;             // grey + alpha
        case 6: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break; // RGBA
        case 3: {                                              // palette
          const size_t index = size_t(p[0]) * 3;
          if (index + 2 >= palette.size()) return {};
          r = palette[index];
          g = palette[index + 1];
          b = palette[index + 2];
          a = p[0] < alpha.size() ? alpha[p[0]] : 255;
          break;
        }
        default: return {};
      }
      // The same packing encode_png reads: red in the low byte.
      out[size_t(y) * size_t(w) + size_t(x)] =
          uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }
  }
  if (width) *width = w;
  if (height) *height = h;
  return out;
}

}  // namespace pf

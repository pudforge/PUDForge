// A PNG encoder, so any client can write out what it rendered.
//
// Turning pixels into a file can be described without saying "window", so it
// belongs here rather than being solved once per platform — macOS had
// CoreGraphics, the web client Node's zlib, and the Windows client nothing.
//
// Self-contained on purpose: the core has no third-party dependencies, and that
// is worth more than the last few percent of compression. Just enough DEFLATE
// (fixed Huffman codes over a greedy LZ77 search) that any decoder accepts the
// stream; map renders come out around a fifth of their raw size.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pf {

/// Encode RGBA pixels, 8 bits per channel, row-major from the top left.
/// Returns an empty vector when the dimensions are not positive.
std::vector<uint8_t> encode_png(const uint32_t* rgba, int width, int height);

/// DEFLATE with fixed Huffman codes, wrapped in a zlib stream. Exposed for
/// tests, which check the stream is well-formed independently of the PNG
/// container around it.
std::vector<uint8_t> zlib_compress(const uint8_t* data, size_t length);

uint32_t crc32(const uint8_t* data, size_t length, uint32_t seed = 0);
uint32_t adler32(const uint8_t* data, size_t length);

}  // namespace pf

// Reading Warcraft II's MPQ archives.
//
// Point at the install directory and the artwork comes out of `War2Dat.mpq`
// with no unpacking step, which for anyone who cannot be given Blizzard's files
// is the only way in.
//
// Written against what Warcraft II actually contains rather than the format's
// much larger surface. Measured across both shipped archives, every block:
//
//   * format version 0, 4096-byte sectors
//   * every one of 9,139 compressed sectors uses PKWARE DCL, and nothing else
//   * every one of them uses raw 8-bit literals, so no literal Huffman table
//   * one encrypted file per archive, with the fix-key flag
//
// Anything outside that is refused rather than guessed at.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pud.hpp"

namespace pf {

/// Decompress one PKWARE DCL stream. Empty on failure.
std::vector<uint8_t> pkware_explode(const uint8_t* data, size_t len);

class MpqArchive {
 public:
  /// Open an archive from disk. Null when it is not one, or not one we read.
  static MpqArchive* open(const std::string& path, Status& status);
  static MpqArchive* open_bytes(std::vector<uint8_t> bytes, Status& status);

  /// Extract one file by its archive path. Slashes either way round.
  bool read(const std::string& name, std::vector<uint8_t>& out) const;
  bool contains(const std::string& name) const;

  /// Paths from `(listfile)`, empty when the archive carries none.
  const std::vector<std::string>& files() const { return listfile_; }

 private:
  bool parse(Status& status);
  int find_block(const std::string& name) const;

  std::vector<uint8_t> data_;
  std::vector<uint8_t> hash_table_;
  std::vector<uint8_t> block_table_;
  std::vector<std::string> listfile_;
  uint32_t hash_count_ = 0;
  uint32_t block_count_ = 0;
  uint32_t sector_size_ = 0;
};

/**
 * Where game data comes from: several archives, later ones winning.
 *
 * One place decides that `War2Patch.mpq` overrides `War2Dat.mpq` and a mod
 * overrides both, so clients cannot drift apart implementing precedence.
 */
class DataSource {
 public:
  ~DataSource();

  /// Add every archive in a Warcraft II install directory, in the right order.
  /// Returns how many were opened.
  int add_directory(const std::string& dir);

  /// Add one archive by path, at the highest priority so far.
  bool add_archive(const std::string& path);

  /// A loose directory tree, searched before any archive. This is how an
  /// unpacked copy keeps working, and how a mod folder would.
  void add_files(const std::string& dir);

  bool read(const std::string& name, std::vector<uint8_t>& out) const;

  int archive_count() const { return int(archives_.size()); }

 private:
  std::vector<MpqArchive*> archives_;   ///< last added wins
  std::vector<std::string> dirs_;
};

}  // namespace pf

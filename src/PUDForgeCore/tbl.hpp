// Warcraft II's string tables.
//
// `rez/stat_txt.tbl` holds every name the game shows, in the format Blizzard
// used from Warcraft II through StarCraft — a count, that many 16-bit offsets,
// then NUL-terminated strings — and it is where a localised install keeps its
// translations. An editor that hard-codes "Footman" shows "Footman" to somebody
// running the German game, and it also disagrees with the game about names it
// only ever guessed at: the shipped table says "Elven Archer" and "Watch
// Tower" where this repository had "Archer" and "Orc Scout Tower".
//
// The strings are cp1252 and come out of here as UTF-8, which is what the C ABI
// carries.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pf {

/// Where each block of names begins in `rez/stat_txt.tbl`.
///
/// Offsets rather than a derivation, because they are not derivable: the unit
/// block is 105 long where `kUnitCount` is 110 — the game leaves the five dead
/// slots and the orc wall blank — so counting from one would put every upgrade
/// name five places out.
constexpr int kFirstUnitString = 1;
constexpr int kFirstUpgradeString = 106;
/// And the AI scripts, all 83 of them, ending at 241. The built-in list had
/// "Expansion 1" through "Expansion 51" where the game knows them as
/// "_Hum Exp. 5c (Violet)", and fifty-one numbered placeholders offer nothing.
constexpr int kFirstAiString = 159;

/// Single strings the editor has a use for, rather than whole blocks.
///
/// Only the game's refusal messages that stay true of an editor placing
/// anything. Its own "You cannot build there." is deliberately absent: it
/// covers wrong ground, blocked ground and the wrong element with one sentence,
/// where the editor can say which of the three it was.
constexpr int kOilLeftString = 417;
constexpr int kGoldLeftString = 425;
constexpr int kCannotBuildOffMapString = 441;
constexpr int kMustBuildOnCoastString = 443;
constexpr int kTooNearGoldmineString = 446;

class Tbl {
 public:
  /// Parse a table. False when the bytes are not one, in which case nothing
  /// about this object is usable — a truncated file must not half-load and then
  /// hand out garbage names.
  bool parse(const uint8_t* bytes, size_t length);

  int count() const { return int(strings_.size()); }
  /// The string at `index`, or empty when out of range. Empty is also a real
  /// answer: the table has blanks where the game names nothing.
  const std::string& at(int index) const;

  /// The same string with its newlines turned into spaces.
  ///
  /// The table is mostly command-button captions broken across the three lines
  /// a button has room for ("Upgrade\nSword\nStrength 1"); a list row has one,
  /// and the game's own wording is the point, so the breaks go rather than the
  /// string being rewritten. Kept beside the raw one because the result is
  /// handed out as a `const char*` and something has to own it.
  const std::string& flat(int index) const;

 private:
  std::vector<std::string> strings_;
  std::vector<std::string> flat_;
};

/// One cp1252 string as UTF-8. Exposed because the tables are not the only
/// thing in the archives written in it.
std::string cp1252_to_utf8(const char* bytes, size_t length);

}  // namespace pf

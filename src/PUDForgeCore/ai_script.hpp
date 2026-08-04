// Reading the AI scripts out of `rez/ai.bin`.
//
// "Human 7" and "Expansion 17" give a mapper nothing to go on, and what a
// script builds and attacks with is in the archive, so it is read rather than
// described from memory.
//
// Derived from the file at runtime, so a modded archive with a different or
// longer table is summarised as it actually is. The instruction set is the only
// hand-written part, in overrides/ai_opcodes.cpp with its provenance.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pud.hpp"

namespace pf {

class DataSource;

enum class AiOp : uint8_t { kVar, kGoto, kSleep, kDo };

struct AiOpcode {
  uint8_t code;
  AiOp op;
  uint8_t size;   ///< total instruction length, opcode byte included
};

/// How a variable should be read back to a person.
enum class AiVarKind : uint8_t {
  kFlag,        ///< on or off
  kParty,       ///< attack group size or count
  kForce,       ///< how many of a unit the script wants
  kAggression,
  kOther,
};

struct AiVariable {
  uint8_t number;
  const char* name;
  AiVarKind kind;
};

struct AiItem {
  uint8_t code;
  const char* name;
};

extern const AiOpcode kAiOpcodes[];
extern const int kAiOpcodeCount;
extern const AiVariable kAiVariables[];
extern const int kAiVariableCount;
extern const AiItem kAiResearch[];
extern const int kAiResearchCount;
extern const AiItem kAiPairedNames[];
extern const int kAiPairedNameCount;

/**
 * The script table from `rez/ai.bin`.
 *
 * The file gives no count, so the length of the offset table is worked out —
 * see `parse`.
 */
class AiScripts {
 public:
  static AiScripts* open(std::vector<uint8_t> bytes, Status& status);
  static AiScripts* open_source(const DataSource& source, Status& status);

  int count() const { return int(records_.size()); }

  /// A few lines saying what the script does. Empty when out of range.
  std::string summary(int index) const;

  /// The disassembly, one instruction per line.
  std::string listing(int index) const;

  /**
   * The attack waves, one row per line, fields separated by tabs.
   *
   * A script states a target force, sleeps while the game builds it, then
   * raises the target and sleeps again, so each sleep ends a wave.
   *
   * Columns: wave number, ticks slept after it, what it attacks by, the attack
   * group shape, and the force it wants.
   */
  std::string waves(int index) const;

 private:
  struct Record {
    int at = 0;
    int build = 0;
    int rates = 0;
  };

  bool parse();
  /// Walk the bytecode, gathering what it does. False when it will not decode.
  struct Walk {
    bool ok = false;
    int instructions = 0;
    int sleeps = 0;
    long long ticks = 0;
    bool attacks_land = false, attacks_sea = false, attacks_air = false;
    bool strategy = false;
    int aggression = 0;
    int land_size = 0, land_parties = 0;
    int sea_size = 0, sea_parties = 0;
    int air_size = 0, air_parties = 0;
    /// Highest value each force variable is ever set to.
    uint8_t force[0x100] = {};
    std::vector<uint8_t> builds;    ///< item codes in the order `do` asks for them
  };
  Walk walk(int index) const;
  std::string item_name(uint8_t code) const;
  std::vector<uint8_t> build_order(int at) const;

  std::vector<uint8_t> data_;
  std::vector<Record> records_;
};

}  // namespace pf

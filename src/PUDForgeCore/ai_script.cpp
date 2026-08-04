#include "ai_script.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "constants.hpp"
#include "mpq.hpp"

namespace pf {
namespace {

const AiOpcode* opcode_for(uint8_t code) {
  for (int i = 0; i < kAiOpcodeCount; i++) {
    if (kAiOpcodes[i].code == code) return &kAiOpcodes[i];
  }
  return nullptr;
}

const AiVariable* variable_for(uint8_t number) {
  for (int i = 0; i < kAiVariableCount; i++) {
    if (kAiVariables[i].number == number) return &kAiVariables[i];
  }
  return nullptr;
}

/// Item codes 0xFF and 0x00 are the list's own punctuation, not things to make.
constexpr uint8_t kItemEndOfPhase = 0xff;
constexpr uint8_t kItemNothing = 0x00;
/// Below this an item code is a unit id; at or above it, research.
constexpr uint8_t kFirstResearchItem = 0x80;

void append(std::string& out, const std::string& line) {
  if (!out.empty()) out += '\n';
  out += line;
}

std::string join(const std::vector<std::string>& parts, const char* sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

}  // namespace

AiScripts* AiScripts::open(std::vector<uint8_t> bytes, Status& status) {
  AiScripts* scripts = new AiScripts();
  scripts->data_ = std::move(bytes);
  if (!scripts->parse()) {
    delete scripts;
    status = Status::Malformed;
    return nullptr;
  }
  status = Status::Ok;
  return scripts;
}

AiScripts* AiScripts::open_source(const DataSource& source, Status& status) {
  std::vector<uint8_t> bytes;
  if (!source.read("rez\\ai.bin", bytes)) {
    status = Status::Malformed;
    return nullptr;
  }
  return open(std::move(bytes), status);
}

bool AiScripts::parse() {
  const size_t size = data_.size();
  if (size < 16) return false;
  auto u16 = [&](size_t at) -> int {
    return int(data_[at]) | (int(data_[at + 1]) << 8);
  };

  // The file records no count, so the offset table is bounded by the first
  // offset it contains and each entry read shrinks the ceiling. Slots past the
  // real end hold script data, which read as a pair of pointers lands outside
  // the file almost at once.
  int ceiling = int(size);
  for (int i = 0; i * 2 + 1 < ceiling; i++) {
    const int at = u16(size_t(i) * 2);
    if (at < 0 || at + 4 > int(size)) break;
    ceiling = std::min(ceiling, at);
    if (i * 2 + 2 > ceiling) break;

    Record record;
    record.at = at;
    record.build = u16(size_t(at));
    record.rates = u16(size_t(at) + 2);
    if (record.build + 1 >= int(size) || record.rates + 1 >= int(size)) break;

    records_.push_back(record);
    if (!walk(int(records_.size()) - 1).ok) {
      records_.pop_back();
      break;
    }
  }
  return !records_.empty();
}

AiScripts::Walk AiScripts::walk(int index) const {
  Walk out;
  if (index < 0 || index >= int(records_.size())) return out;
  const size_t size = data_.size();
  auto u16 = [&](size_t at) -> int {
    return int(data_[at]) | (int(data_[at + 1]) << 8);
  };

  std::set<int> seen;
  int pc = records_[size_t(index)].at + 4;
  // A script ends by jumping somewhere it has already been — its idle loop.
  // Bounded anyway, so a malformed file cannot spin here.
  for (int steps = 0; steps < 20000; steps++) {
    if (pc < 0 || pc + 1 >= int(size)) return out;
    if (seen.count(pc)) { out.ok = true; return out; }
    seen.insert(pc);

    const AiOpcode* op = opcode_for(data_[size_t(pc)]);
    if (!op) return out;
    if (pc + op->size > int(size)) return out;
    out.instructions++;

    switch (op->op) {
      case AiOp::kVar: {
        const uint8_t number = data_[size_t(pc) + 1];
        const uint8_t value = data_[size_t(pc) + 2];
        const AiVariable* var = variable_for(number);
        if (!var) return out;
        switch (var->kind) {
          case AiVarKind::kFlag:
            if (number == 0x09) out.attacks_land |= value != 0;
            if (number == 0x0a) out.attacks_sea |= value != 0;
            if (number == 0x0b) out.attacks_air |= value != 0;
            if (number == 0x0c) out.strategy |= value != 0;
            break;
          case AiVarKind::kAggression:
            out.aggression = std::max(out.aggression, int(value));
            break;
          case AiVarKind::kParty:
            if (number == 0x0d) out.land_size = std::max(out.land_size, int(value));
            if (number == 0x0e) out.land_parties = std::max(out.land_parties, int(value));
            if (number == 0x0f) out.sea_size = std::max(out.sea_size, int(value));
            if (number == 0x10) out.sea_parties = std::max(out.sea_parties, int(value));
            if (number == 0x11) out.air_size = std::max(out.air_size, int(value));
            if (number == 0x12) out.air_parties = std::max(out.air_parties, int(value));
            break;
          case AiVarKind::kForce:
            // The peak, not the final value: a script counts down as it spends,
            // so the largest number it ever asks for is the force it wants.
            out.force[number] = std::max(out.force[number], value);
            break;
          case AiVarKind::kOther:
            break;
        }
        break;
      }
      case AiOp::kDo: {
        const uint8_t item = data_[size_t(pc) + 1];
        if (item != kItemNothing && item != kItemEndOfPhase) out.builds.push_back(item);
        break;
      }
      case AiOp::kSleep:
        out.sleeps++;
        out.ticks += int64_t(u16(size_t(pc) + 1)) | (int64_t(u16(size_t(pc) + 3)) << 16);
        break;
      case AiOp::kGoto:
        pc = u16(size_t(pc) + 1);
        continue;
    }
    pc += op->size;
  }
  out.ok = true;
  return out;
}

std::string AiScripts::item_name(uint8_t code) const {
  for (int i = 0; i < kAiPairedNameCount; i++) {
    if (kAiPairedNames[i].code == code) return kAiPairedNames[i].name;
  }
  if (code >= kFirstResearchItem) {
    for (int i = 0; i < kAiResearchCount; i++) {
      if (kAiResearch[i].code == code) return kAiResearch[i].name;
    }
  } else if (code < kUnitCount && kUnits[code].name) {
    std::string name = kUnits[code].name;
    for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    return name;
  }
  char buf[24];
  std::snprintf(buf, sizeof(buf), "item $%02x", code);
  return buf;
}

std::vector<uint8_t> AiScripts::build_order(int at) const {
  std::vector<uint8_t> out;
  for (size_t o = size_t(at); o < data_.size() && out.size() < 64; o++) {
    // One 0xFF closes a phase; two in a row close the list.
    if (data_[o] == kItemEndOfPhase) {
      if (o + 1 < data_.size() && data_[o + 1] == kItemEndOfPhase) break;
      break;   // the first phase is the opening, which is what a reader wants
    }
    if (data_[o] != kItemNothing) out.push_back(data_[o]);
  }
  return out;
}

std::string AiScripts::summary(int index) const {
  if (index < 0 || index >= int(records_.size())) return "";
  const Walk w = walk(index);
  if (!w.ok) return "Uses an instruction this decoder does not know.";

  // Lists, not prose: the disassembly below says what the script does, and this
  // saves reading 400 instructions to find what it fields and puts up first.
  std::string out;

  std::vector<std::string> fronts;
  if (w.attacks_land) fronts.push_back("land");
  if (w.attacks_sea) fronts.push_back("sea");
  if (w.attacks_air) fronts.push_back("air");
  append(out, "Attacks by: " + (fronts.empty() ? "nothing" : join(fronts, ", ")));

  std::vector<std::string> parties;
  auto party = [&](const char* what, int size, int count) {
    // A front with no party size is not a front, whatever the flags say.
    if (size <= 0) return;
    const int groups = count > 0 ? count : 1;
    parties.push_back(std::string(what) + " " + std::to_string(groups)
                      + " x " + std::to_string(size));
  };
  party("land", w.land_size, w.land_parties);
  party("sea", w.sea_size, w.sea_parties);
  party("air", w.air_size, w.air_parties);
  if (!parties.empty()) append(out, "Attack groups: " + join(parties, ", "));

  // The force it asks for, biggest first. The peak rather than the final value:
  // a script counts down as it spends, so the largest ask is the target.
  std::vector<std::pair<int, std::string>> force;
  for (int i = 0; i < kAiVariableCount; i++) {
    if (kAiVariables[i].kind != AiVarKind::kForce) continue;
    const int want = w.force[kAiVariables[i].number];
    if (want > 0) force.emplace_back(want, kAiVariables[i].name);
  }
  std::sort(force.begin(), force.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  if (!force.empty()) {
    std::vector<std::string> parts;
    for (const auto& entry : force) {
      parts.push_back(std::to_string(entry.first) + " " + entry.second);
    }
    append(out, "Units: " + join(parts, ", "));
  }

  // Everything it ever asks to make, in build-list order. The first phase only:
  // later phases are the same base rebuilt, which would bury what is worth
  // reading.
  return out;
}

std::string AiScripts::waves(int index) const {
  if (index < 0 || index >= int(records_.size())) return "";
  const size_t size = data_.size();
  auto u16 = [&](size_t at) -> int {
    return int(data_[at]) | (int(data_[at + 1]) << 8);
  };

  // Running state, exactly as the script leaves it. A sleep closes a wave,
  // because that is the script saying "now go and build all that".
  bool land = false, sea = false, air = false;
  int party[6] = {};   // land size, land count, sea size, sea count, air size, air count
  uint8_t force[0x100] = {};

  std::string out;
  int wave = 0;
  std::set<int> seen;
  int pc = records_[size_t(index)].at + 4;
  for (int steps = 0; steps < 20000; steps++) {
    if (pc < 0 || pc + 1 >= int(size) || seen.count(pc)) break;
    seen.insert(pc);
    const AiOpcode* op = opcode_for(data_[size_t(pc)]);
    if (!op) break;

    if (op->op == AiOp::kVar) {
      const uint8_t number = data_[size_t(pc) + 1];
      const uint8_t value = data_[size_t(pc) + 2];
      if (number == 0x09) land = value != 0;
      if (number == 0x0a) sea = value != 0;
      if (number == 0x0b) air = value != 0;
      if (number >= 0x0d && number <= 0x12) party[number - 0x0d] = value;
      const AiVariable* var = variable_for(number);
      if (var && var->kind == AiVarKind::kForce) force[number] = value;
    } else if (op->op == AiOp::kSleep) {
      const long long ticks = int64_t(u16(size_t(pc) + 1))
                              | (int64_t(u16(size_t(pc) + 3)) << 16);
      // A wave with no force and no attack is the script idling, not a wave.
      std::vector<std::pair<int, std::string>> want;
      for (int i = 0; i < kAiVariableCount; i++) {
        if (kAiVariables[i].kind != AiVarKind::kForce) continue;
        const int n = force[kAiVariables[i].number];
        if (n > 0) want.emplace_back(n, kAiVariables[i].name);
      }
      if (!want.empty() || land || sea || air) {
        std::sort(want.begin(), want.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::string> units;
        for (const auto& entry : want) {
          units.push_back(std::to_string(entry.first) + " " + entry.second);
        }

        std::vector<std::string> fronts;
        if (land) fronts.push_back("land");
        if (sea) fronts.push_back("sea");
        if (air) fronts.push_back("air");

        std::vector<std::string> groups;
        auto group = [&](const char* what, int size_at, int count_at) {
          if (party[size_at] <= 0) return;
          const int count = party[count_at] > 0 ? party[count_at] : 1;
          groups.push_back(std::string(what) + " " + std::to_string(count)
                           + " x " + std::to_string(party[size_at]));
        };
        group("land", 0, 1);
        group("sea", 2, 3);
        group("air", 4, 5);

        append(out, std::to_string(++wave) + "\t" + std::to_string(ticks) + "\t"
                    + (fronts.empty() ? "holds" : join(fronts, ", ")) + "\t"
                    + join(groups, ", ") + "\t" + join(units, ", "));
      }
    } else if (op->op == AiOp::kGoto) {
      pc = u16(size_t(pc) + 1);
      continue;
    }
    pc += op->size;
  }
  return out;
}

std::string AiScripts::listing(int index) const {
  if (index < 0 || index >= int(records_.size())) return "";
  const size_t size = data_.size();
  auto u16 = [&](size_t at) -> int {
    return int(data_[at]) | (int(data_[at + 1]) << 8);
  };

  std::string out;
  std::set<int> seen;
  int pc = records_[size_t(index)].at + 4;
  for (int steps = 0; steps < 20000; steps++) {
    if (pc < 0 || pc + 1 >= int(size) || seen.count(pc)) break;
    seen.insert(pc);
    const AiOpcode* op = opcode_for(data_[size_t(pc)]);
    if (!op) {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%6d  ??? $%02x", pc, data_[size_t(pc)]);
      append(out, buf);
      break;
    }
    char buf[160];
    switch (op->op) {
      case AiOp::kVar: {
        const AiVariable* var = variable_for(data_[size_t(pc) + 1]);
        std::snprintf(buf, sizeof(buf), "%6d  %s = %d", pc,
                      var ? var->name : "?", data_[size_t(pc) + 2]);
        break;
      }
      case AiOp::kDo:
        std::snprintf(buf, sizeof(buf), "%6d  make %s", pc,
                      item_name(data_[size_t(pc) + 1]).c_str());
        break;
      case AiOp::kSleep:
        std::snprintf(buf, sizeof(buf), "%6d  sleep %lld ticks", pc,
                      (long long)(int64_t(u16(size_t(pc) + 1))
                                  | (int64_t(u16(size_t(pc) + 3)) << 16)));
        break;
      case AiOp::kGoto:
        std::snprintf(buf, sizeof(buf), "%6d  goto %d", pc, u16(size_t(pc) + 1));
        break;
    }
    append(out, buf);
    if (op->op == AiOp::kGoto) { pc = u16(size_t(pc) + 1); continue; }
    pc += op->size;
  }
  return out;
}

}  // namespace pf

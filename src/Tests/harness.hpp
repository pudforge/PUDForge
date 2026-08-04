// Test harness: assertions, fixtures, and self-registration.
//
// The suite is split across one translation unit per subject area. That is not
// organisational tidiness — it is the build. As a single 3,200-line file the
// tests took 11 s to compile against 4 s to run, so editing a test cost three
// times as much as running the whole suite. Split, each file compiles in about
// two seconds, only the edited one rebuilds, and `-j` overlaps the rest.
//
// Tests register themselves, so adding one means editing a single file. The
// group comes from `TEST_GROUP` at the top of each file, which is also what
// `--group` filters on and what CTest registers as a parallel job.
//
// Tests that need the shipped maps or the game artwork skip cleanly when the
// data is absent, so a fresh clone without Git LFS still gets a real run.

#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "pudforge/pudforge.h"

#include "art.hpp"
#include "constants.hpp"
#include "mpq.hpp"
#include "pud.hpp"
#include "terrain.hpp"

namespace pft {

// ------------------------------------------------------------- assertions

extern int g_checks;
extern int g_failures;
extern int g_tests;
extern int g_skipped;
extern const char* g_current;

void fail(const char* file, int line, const std::string& what);

#define CHECK(cond)                                                       \
  do {                                                                    \
    ::pft::g_checks++;                                                    \
    if (!(cond)) ::pft::fail(__FILE__, __LINE__, "expected: " #cond);     \
  } while (0)

#define CHECK_EQ(a, b)                                                    \
  do {                                                                    \
    ::pft::g_checks++;                                                     \
    auto va_ = (a);                                                       \
    auto vb_ = (b);                                                       \
    if (!(va_ == vb_)) {                                                  \
      ::pft::fail(__FILE__, __LINE__,                                     \
           std::string(#a " == " #b "  (") + std::to_string(va_) +        \
               " vs " + std::to_string(vb_) + ")");                       \
    }                                                                     \
  } while (0)

// ----------------------------------------------------------- registration

struct TestCase {
  const char* group;
  const char* name;
  void (*fn)();
};

/// Every registered test. A function-local static, so registration from any
/// translation unit is safe regardless of initialisation order.
std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* group, const char* name, void (*fn)());
};

/// Declares which group the tests in this file belong to.
#define TEST_GROUP(g) namespace { const char* const kTestGroup = g; }

/// Defines a test and registers it. Body follows, as before.
#define TEST(name)                                                        \
  static void test_body_##name();                                         \
  static const ::pft::Registrar reg_##name(kTestGroup, #name,             \
                                           test_body_##name);             \
  static void test_body_##name()

// --------------------------------------------------------------- fixtures

extern std::string g_corpus_dir;
/// Repository root, so tests can reach the archives and their unpacked copy.
extern std::string g_root;
extern std::vector<std::string> g_corpus;
/// True when the corpus is Blizzard's shipped maps rather than the two
/// checked-in fixtures. Some invariants hold only for the former: a synthetic
/// fixture may legitimately have no start locations.
extern bool g_corpus_is_shipped;

/**
 * The maps Blizzard shipped, as a subset of the corpus.
 *
 * The corpus tree holds two different populations: the archives' own maps and
 * about a thousand community ones. They are not interchangeable evidence.
 * Blizzard's maps were made by the tool that defined the format, so "every
 * shipped map validates cleanly" is a real claim about the format. Community
 * maps were made by hand with editors that break rules on purpose — a castle on
 * the map edge, a peasant on a cliff — so the same assertion says nothing about
 * us and everything about them.
 *
 * What both populations must satisfy is parsing and byte-exact round-tripping.
 * Those assertions run over everything; the cleanliness ones run over this.
 */
extern std::vector<std::string> g_shipped;
extern std::string g_bgs_dir;
extern std::string g_unit_dir;

bool is_shipped(const std::string& path);
bool have_corpus();
bool have_art();
void skip(const char* why);

/// True when a path holds a Git LFS pointer rather than the real file.
///
/// A checkout without LFS leaves small text stand-ins in place of the corpus
/// and artwork. They are not absent, so an existence check passes and every
/// parse then fails — which is exactly how CI first went red. Treat a pointer
/// as missing data.
bool is_lfs_pointer(const std::string& path);
bool file_exists(const std::string& path);
/// Collect real .pud paths under a directory, recursively.
void collect_puds(const std::string& dir, std::vector<std::string>& out);

// ------------------------------------------------- shared test utilities

/**
 * Locate a section's payload inside a serialized PUD.
 *
 * The core does not expose `OILM` — nothing in the editor has a use for it —
 * so the trigger tests read it back out of the bytes, which is also the only
 * way to prove what we actually write to disk. Shared because the synthetic
 * and corpus halves of that check live in different groups.
 */
const uint8_t* find_section(const std::vector<uint8_t>& pud, const char* tag,
                            size_t* out_len);

/// Put a map through the edits a user makes in a session.
void exercise_editing(pf_map* map);

/**
 * Deal `count` tests out to `shards` runners and return shard `shard`'s share.
 *
 * Round-robin, not contiguous blocks. Cost is wildly uneven between tests — one
 * corpus test outweighs a whole group — and tests run in sorted order, so a
 * contiguous split would hand one shard every expensive neighbour.
 *
 * A function rather than a loop inside the runner because CTest registers the
 * shards separately: if they ever stopped tiling the group exactly, tests would
 * silently go unrun. `shards_tile_the_test_list` checks they do.
 */
std::vector<size_t> shard_indices(size_t count, int shard, int shards);

}  // namespace pft

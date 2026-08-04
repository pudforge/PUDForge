// Test harness definitions and the runner. See harness.hpp.

#include "harness.hpp"

#include <cstdlib>

namespace pft {

int g_checks = 0;
int g_failures = 0;
int g_tests = 0;
int g_skipped = 0;
const char* g_current = "";

void fail(const char* file, int line, const std::string& what) {
  g_failures++;
  std::printf("  FAIL %s:%d\n    %s\n", file, line, what.c_str());
}

std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

Registrar::Registrar(const char* group, const char* name, void (*fn)()) {
  registry().push_back({group, name, fn});
}

std::string g_corpus_dir;
std::string g_root;
std::vector<std::string> g_corpus;
bool g_corpus_is_shipped = false;
std::vector<std::string> g_shipped;
std::string g_bgs_dir;
std::string g_unit_dir;

bool is_shipped(const std::string& path) {
  // Every path the harness holds is generic — forward slashes on Windows too,
  // see collect_puds — so one spelling is enough.
  return path.find("/mpq/") != std::string::npos;
}

bool have_corpus() { return !g_corpus.empty(); }
bool have_art() { return !g_bgs_dir.empty(); }

void skip(const char* why) {
  g_skipped++;
  std::printf("skip %s  (%s)\n", g_current, why);
}

bool is_lfs_pointer(const std::string& path) {
  std::FILE* f = pf::open_file(path, "rb");
  if (!f) return false;
  char head[48] = {};
  const size_t got = std::fread(head, 1, sizeof(head) - 1, f);
  std::fclose(f);
  return got > 20 && std::strncmp(head, "version https://git-lfs", 23) == 0;
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(std::filesystem::u8path(path), ec);
}

void collect_puds(const std::string& dir, std::vector<std::string>& out) {
  std::error_code ec;
  const std::filesystem::path root = std::filesystem::u8path(dir);
  if (!std::filesystem::is_directory(root, ec)) return;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end;
       it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    // generic_u8string, not string. On MSVC `path::string()` converts to the
    // ANSI code page and *throws* when the name will not fit it — reference/war2_ref/
    // holds maps named in Cyrillic, and nothing here catches, so the whole
    // binary went terminate -> abort before printing a line. u8string cannot
    // fail, and generic_ gives forward slashes so is_shipped's "/mpq/" and
    // every path comparison in the tests mean the same thing on every
    // platform. pf::open_file is what makes such a path openable again.
    std::string ext = it->path().extension().u8string();
    for (char& c : ext) c = char(tolower(static_cast<unsigned char>(c)));
    if (ext != ".pud") continue;
    const std::string path = it->path().generic_u8string();
    if (is_lfs_pointer(path)) continue;
    out.push_back(path);
  }
}

const uint8_t* find_section(const std::vector<uint8_t>& pud, const char* tag,
                            size_t* out_len) {
  size_t p = 0;
  while (p + 8 <= pud.size()) {
    const size_t len = size_t(pud[p + 4]) | (size_t(pud[p + 5]) << 8) |
                       (size_t(pud[p + 6]) << 16) | (size_t(pud[p + 7]) << 24);
    if (std::memcmp(&pud[p], tag, 4) == 0) {
      if (p + 8 + len > pud.size()) return nullptr;
      *out_len = len;
      return &pud[p + 8];
    }
    p += 8 + len;
  }
  return nullptr;
}

/// Put a map through the edits a user makes in a session.
void exercise_editing(pf_map* map) {
  const int w = pf_map_width(map);
  const int h = pf_map_height(map);
  for (int i = 0; i < 12 && 10 + i < w; i++) {
    pf_map_paint_terrain(map, 10 + i, 10, PF_TERRAIN_WATER_LIGHT, 3);
  }
  pf_map_paint_terrain_raw(map, 4, 4, PF_TERRAIN_FOREST, 1);
  pf_map_paint_wall(map, 6, 6, 1, 1);
  pf_map_rebuild_regions(map);
  const int added = pf_map_add_unit(map, w / 2, h / 2, 0, 0, 0);
  if (added >= 0) pf_map_remove_unit(map, added);
  pf_map_set_description(map, "edited");
}

std::vector<size_t> shard_indices(size_t count, int shard, int shards) {
  std::vector<size_t> out;
  if (shards < 1 || shard < 0 || shard >= shards) return out;
  for (size_t i = size_t(shard); i < count; i += size_t(shards)) out.push_back(i);
  return out;
}


namespace {

void discover(const std::string& root) {
  g_root = root;
  g_corpus_dir = root + "/reference/war2_ref";
  collect_puds(g_corpus_dir, g_corpus);
  // Fall back to the checked-in fixtures. They are two maps rather than 529,
  // but they are real ones, and they are small enough for CI to fetch — which
  // is the difference between the round-trip guarantee being verified on every
  // push and only on machines that happen to hold the full corpus.
  g_corpus_is_shipped = !g_corpus.empty();
  if (g_corpus.empty()) collect_puds(root + "/test/fixtures", g_corpus);
  for (const std::string& path : g_corpus) {
    if (is_shipped(path) || !g_corpus_is_shipped) g_shipped.push_back(path);
  }
  std::sort(g_shipped.begin(), g_shipped.end());
  // readdir order is filesystem-dependent; sort so sampled subsets are stable.
  std::sort(g_corpus.begin(), g_corpus.end());

  const std::string bgs = root + "/reference/war2_ref/mpq/War2Dat/art/bgs/";
  if (file_exists(bgs + "forest/forest.cv4") && !is_lfs_pointer(bgs + "forest/forest.cv4")) {
    // An unfetched Git LFS pointer is a ~130-byte text file, not the payload.
    std::vector<uint8_t> probe;
    if (pf::read_file(bgs + "forest/forest.cv4", probe) &&
        probe.size() == size_t(pf::kCv4GroupCount) * pf::kCv4GroupBytes) {
      g_bgs_dir = bgs;
    }
  }
  const std::string units = root + "/reference/war2_ref/mpq/War2Dat/art/unit/";
  if (file_exists(units + "human/thall.grp") && !is_lfs_pointer(units + "human/thall.grp"))
    g_unit_dir = units;
}

void usage() {
  std::printf(
      "usage: pf_tests [root] [options]\n"
      "  --group NAME    run one group (repeatable)\n"
      "  --filter TEXT   run tests whose name contains TEXT\n"
      "  --shard I/N     run only shard I of N of the matched tests\n"
      "  --list          list tests and exit\n"
      "  --groups        list group names and exit\n"
      "  --quiet         only report failures and the summary\n"
      "\n"
      "root defaults to \"..\" and locates the map corpus and artwork.\n");
}

}  // namespace
}  // namespace pft

int main(int argc, char** argv) {
  using namespace pft;

  std::string root;
  std::vector<std::string> groups;
  std::string filter;
  int shard = 0, shards = 1;
  bool list = false, list_groups = false, quiet = false;

  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--group" && i + 1 < argc) groups.push_back(argv[++i]);
    else if (a == "--filter" && i + 1 < argc) filter = argv[++i];
    else if (a == "--shard" && i + 1 < argc) {
      const std::string spec = argv[++i];
      const size_t slash = spec.find('/');
      if (slash == std::string::npos) { usage(); return 2; }
      shard = std::atoi(spec.substr(0, slash).c_str());
      shards = std::atoi(spec.substr(slash + 1).c_str());
      if (shards < 1 || shard < 0 || shard >= shards) { usage(); return 2; }
    }
    else if (a == "--list") list = true;
    else if (a == "--groups") list_groups = true;
    else if (a == "--quiet") quiet = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (!a.empty() && a[0] == '-') { usage(); return 2; }
    else if (root.empty()) root = a;
    else { usage(); return 2; }
  }
  if (root.empty()) root = "..";

  // Registration order across translation units is unspecified, so sort for a
  // deterministic run order and stable output.
  auto& cases = registry();
  std::sort(cases.begin(), cases.end(), [](const TestCase& a, const TestCase& b) {
    const int g = std::strcmp(a.group, b.group);
    return g != 0 ? g < 0 : std::strcmp(a.name, b.name) < 0;
  });

  if (list_groups) {
    std::string last;
    for (const TestCase& c : cases) {
      if (c.group != last) { std::printf("%s\n", c.group); last = c.group; }
    }
    return 0;
  }
  if (list) {
    for (const TestCase& c : cases) std::printf("%-8s %s\n", c.group, c.name);
    return 0;
  }

  const auto wanted = [&](const TestCase& c) {
    if (!groups.empty() &&
        std::find(groups.begin(), groups.end(), c.group) == groups.end()) return false;
    if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) return false;
    return true;
  };

  std::vector<const TestCase*> matched;
  for (const TestCase& c : cases) if (wanted(c)) matched.push_back(&c);
  if (matched.empty()) {
    std::printf("no tests matched\n");
    return 2;
  }

  std::vector<const TestCase*> selected;
  for (size_t i : shard_indices(matched.size(), shard, shards)) {
    selected.push_back(matched[i]);
  }

  discover(root);
  if (!quiet) {
    std::printf("pudforge core tests  (%d maps, %d of them shipped, artwork %s)\n",
                int(g_corpus.size()), int(g_shipped.size()),
                g_bgs_dir.empty() ? "absent" : "present");
    if (selected.size() != cases.size()) {
      std::printf("running %d of %d tests", int(selected.size()), int(cases.size()));
      if (shards > 1) std::printf("  (shard %d of %d)", shard, shards);
      std::printf("\n");
    }
    std::printf("\n");
  }

  for (const TestCase* c : selected) {
    g_current = c->name;
    g_tests++;
    const int before = g_failures;
    c->fn();
    if (g_failures != before) std::printf("FAIL %s\n", c->name);
    else if (!quiet) std::printf("ok   %s\n", c->name);
  }

  std::printf("\n%d tests, %d checks, %d skipped, %d failures\n",
              g_tests, g_checks, g_skipped, g_failures);
  return g_failures ? 1 : 0;
}

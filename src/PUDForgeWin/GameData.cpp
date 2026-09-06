#include "GameData.hpp"

#include "Strings.hpp"
#include "strings.h"

#include <shlobj.h>

#include <cstdlib>   // free, for the bytes pf_data_source_read hands back
#include <vector>

namespace pfwin {
namespace {

constexpr wchar_t kRegKey[] = L"Software\\PUDForge";
constexpr wchar_t kRegValue[] = L"GamePath";

/// Where the installers put it. Tried in order when nothing is remembered, so
/// the common case never sees a dialog at all.
const wchar_t* kGuesses[] = {
    L"C:\\Program Files (x86)\\War2Combat",
    L"C:\\War2Combat",
    L"C:\\Program Files (x86)\\Warcraft II Remastered",
    L"C:\\Program Files\\Warcraft II Remastered",
    L"C:\\Program Files (x86)\\Warcraft II BNE",
    L"C:\\Program Files\\Warcraft II BNE",
    L"C:\\Program Files (x86)\\GOG Galaxy\\Games\\Warcraft II BNE",
};

/// Where the data sits relative to an installation. The empty one first: the
/// BNE layout keeps War2Dat.mpq in the root, and only Remastered buries it.
const wchar_t* kInside[] = {
    L"",
    L"\\x86\\Data",     // Warcraft II Remastered
    L"\\Data",
    L"\\data",
    L"\\War2Dat",       // an unpacked archive, which is what --render uses
};

/// Under Uninstall, which is the one place both products record where they went.
/// Matched on the display name because the key names are a GUID and an installer
/// string respectively, and neither is worth hard-coding.
const wchar_t* kUninstallRoots[] = {
    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
};

std::wstring RegString(HKEY root, const wchar_t* path, const wchar_t* value) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
  wchar_t buffer[MAX_PATH] = {};
  DWORD size = sizeof(buffer), type = 0;
  const LSTATUS status = RegQueryValueExW(key, value, nullptr, &type,
                                          reinterpret_cast<BYTE*>(buffer), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
  return buffer;
}

/// Trailing separators, which the War2Combat uninstall entry has and the
/// Remastered one does not. Left on, they turn every path built from one into a
/// double backslash.
std::wstring Trimmed(std::wstring path) {
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  return path;
}

/// Case-insensitively, because a display name is written by whoever wrote the
/// installer. CharLowerW rather than towlower per character: it is the one the
/// user's locale agrees with.
bool Contains(std::wstring haystack, std::wstring needle) {
  if (haystack.empty() || needle.empty()) return false;
  CharLowerW(haystack.data());
  CharLowerW(needle.data());
  return haystack.find(needle) != std::wstring::npos;
}

/// Whether the game's data can be read out of exactly this folder.
///
/// Deliberately not GameData::Adopt: adopting installs the folder's string table
/// into the core, and a *probe* that did that would swap the running client's
/// names for a candidate's and then free them on the way out, leaving the core
/// with none. Nothing here outlives the call.
bool HasDataAt(const std::wstring& folder) {
  if (folder.empty()) return false;
  pf_data_source* source = pf_data_source_create();
  if (!source) return false;
  const std::string utf8 = ToUtf8(folder);
  pf_data_source_add_files(source, utf8.c_str());
  pf_data_source_add_directory(source, utf8.c_str());
  pf_tileset_art* probe = pf_tileset_art_open_source(source, 0, nullptr);
  const bool ok = probe != nullptr;
  if (probe) pf_tileset_art_free(probe);
  pf_data_source_free(source);
  return ok;
}

/// The folder inside an installation that the data is actually in, or empty.
std::wstring FindDataIn(const std::wstring& root) {
  if (root.empty()) return {};
  const std::wstring base = Trimmed(root);
  for (const wchar_t* inside : kInside) {
    if (HasDataAt(base + inside)) return base + inside;
  }
  return {};
}

std::wstring ReadRegistry() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return {};
  }
  wchar_t buffer[MAX_PATH] = {};
  DWORD size = sizeof(buffer);
  DWORD type = 0;
  const LSTATUS status = RegQueryValueExW(key, kRegValue, nullptr, &type,
                                          reinterpret_cast<BYTE*>(buffer), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_SZ) return {};
  return buffer;
}

/// PUDForge.ini beside the exe, `[Game] Path=`.
///
/// Neither product ships an INI naming its own install — War2Combat's two are
/// the graphics wrapper's settings and Remastered has none — so this is ours: a
/// copy on a stick, a shared build, or an install script can say where the game
/// is without touching anybody's registry. Read before the registry so that a
/// file somebody put there on purpose beats a guess.
std::wstring ReadIni() {
  wchar_t module[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return {};
  std::wstring ini = module;
  const size_t slash = ini.find_last_of(L'\\');
  if (slash == std::wstring::npos) return {};
  ini.resize(slash);
  ini += L"\\PUDForge.ini";
  if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return {};

  wchar_t path[MAX_PATH] = {};
  GetPrivateProfileStringW(L"Game", L"Path", L"", path, MAX_PATH, ini.c_str());
  return Trimmed(path);
}

/// Everything the installers left behind, without opening any of it.
void RegistryCandidates(std::vector<GameInstall>& out) {
  // The BNE key points at the map editor rather than the install, so its parent
  // is what we want. Written by War2Combat's installer as well as Blizzard's.
  const std::wstring editor = RegString(
      HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\Warcraft II BNE", L"War2CD");
  if (!editor.empty()) {
    const std::wstring trimmed = Trimmed(editor);
    const size_t up = trimmed.find_last_of(L'\\');
    if (up != std::wstring::npos) out.push_back({L"Warcraft II BNE", trimmed.substr(0, up)});
  }

  for (const wchar_t* root : kUninstallRoots) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &key) != ERROR_SUCCESS) {
      continue;
    }
    for (DWORD i = 0;; i++) {
      wchar_t name[256] = {};
      DWORD length = 256;
      if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) !=
          ERROR_SUCCESS) {
        break;
      }
      const std::wstring sub = std::wstring(root) + L"\\" + name;
      const std::wstring shown = RegString(HKEY_LOCAL_MACHINE, sub.c_str(), L"DisplayName");
      if (!Contains(shown, L"warcraft ii") && !Contains(shown, L"war2")) continue;
      const std::wstring where =
          Trimmed(RegString(HKEY_LOCAL_MACHINE, sub.c_str(), L"InstallLocation"));
      if (!where.empty()) out.push_back({shown, where});
    }
    RegCloseKey(key);
  }
}

}  // namespace

std::vector<GameInstall> FindGameInstalls() {
  std::vector<GameInstall> candidates;
  if (const std::wstring ini = ReadIni(); !ini.empty()) {
    candidates.push_back({L"PUDForge.ini", ini});
  }
  RegistryCandidates(candidates);
  for (const wchar_t* guess : kGuesses) candidates.push_back({L"", guess});

  // Verified and de-duplicated, because the same install reaches this list from
  // three directions: an uninstall key, the BNE key's parent, and a guess.
  std::vector<GameInstall> found;
  for (const GameInstall& candidate : candidates) {
    const std::wstring data = FindDataIn(candidate.path);
    if (data.empty()) continue;
    bool already = false;
    for (const GameInstall& kept : found) {
      already = already || Contains(kept.path, data) || Contains(data, kept.path);
    }
    if (already) continue;
    // Named after the folder it resolved to, not after whatever recorded it:
    // the BNE registry key is written by War2Combat's installer as well as
    // Blizzard's, and an uninstall entry reads "War2Combat full english version
    // version 4.6.1". The path is the thing that is actually true.
    std::wstring label = Contains(data, L"remastered")   ? L"Warcraft II Remastered"
                         : Contains(data, L"war2combat") ? L"War2Combat"
                         : !candidate.label.empty()      ? candidate.label
                                                         : L"Warcraft II";
    found.push_back({label, data});
  }
  return found;
}

std::string ToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                                    nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n,
                      nullptr, nullptr);
  return out;
}

std::wstring FromUtf8(const std::string& text) {
  if (text.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n);
  return out;
}

GameData::~GameData() {
  // The strings first: the core is holding a pointer to them, and freeing one
  // uninstalls it.
  if (strings_) pf_strings_free(strings_);
  if (source_) pf_data_source_free(source_);
#ifdef PF_ENABLE_HD_ART
  if (hd_cache_) pf_hd_cache_free(hd_cache_);
#endif
}

void GameData::UseGameStrings() {
  pf_strings* opened = source_ ? pf_strings_open_source(source_, nullptr) : nullptr;
  // Only swap once the new table is in hand: a folder that turns out to have no
  // stat_txt.tbl should leave the names that were working alone.
  if (!opened) return;
  pf_use_strings(opened);
  if (strings_) pf_strings_free(strings_);
  strings_ = opened;
}

bool GameData::Adopt(const std::wstring& folder) {
  if (folder.empty()) return false;
  pf_data_source* source = pf_data_source_create();
  if (!source) return false;

  const std::string utf8 = ToUtf8(folder);
  // Loose files beat archives, and a patch archive beats the base one — the
  // precedence is the core's, so both clients resolve a file the same way.
  //
  // Both calls matter. add_files serves an *unpacked* copy of the data; without
  // it a folder holding loose files verified as empty and every path in Restore
  // silently failed.
  pf_data_source_add_files(source, utf8.c_str());
  pf_data_source_add_directory(source, utf8.c_str());

  // Prove it before adopting it, so a wrong folder is caught here rather than as
  // a map that silently draws in flat colours.
  pf_tileset_art* probe = pf_tileset_art_open_source(source, 0, nullptr);
  if (!probe) {
    pf_data_source_free(source);
    return false;
  }
  pf_tileset_art_free(probe);

  if (source_) pf_data_source_free(source_);
  source_ = source;
  folder_ = folder;
  // The game's own names, so a localised install is described in its own words
  // and the handful this repository guessed at come out right.
  UseGameStrings();
  return true;
}

void GameData::Remember() const {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(key, kRegValue, 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(folder_.c_str()),
                 DWORD((folder_.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
}

bool GameData::AdoptTree(const std::wstring& root) {
  return Adopt(FindDataIn(root));
}

bool GameData::Choose(const std::wstring& folder) {
  if (!AdoptTree(folder)) return false;
  Remember();
  return true;
}

bool GameData::Restore() {
  // What was chosen last time, and nothing else on a second run: the choice
  // between two installed products is the person's, so a remembered folder that
  // has since gone should ask again rather than quietly switch to the other one.
  if (AdoptTree(ReadRegistry())) return true;
  if (Search()) return true;
  // Not remembered. A guess is not a choice, and writing one down would make
  // the next start look like a returning user's — so the setup dialog, which
  // asks exactly once, would never appear. The loop is cheap enough to re-run:
  // it stops at the first folder that opens a tileset.
  for (const wchar_t* guess : kGuesses) {
    if (AdoptTree(guess)) return true;
  }
  return false;
}

/// Near the exe, then up the directory tree — the same walk the macOS client
/// does. This is what lets the exe be dropped straight into the game folder or
/// sit next to an unpacked `War2Dat`.
///
/// It used to probe `reference\war2_ref\mpq\War2Dat` as well, which meant a
/// build run out of a checkout read this repository's own copy of the data. That
/// is a copy no user has, so the one configuration nobody could test was the one
/// every developer ran.
///
/// Deliberately not remembered: it is positional, so if the exe moves the search
/// just runs again, whereas a stale registry path would win over the new place.
bool GameData::Search() {
  wchar_t module[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return false;
  std::wstring dir = module;
  const size_t base = dir.find_last_of(L'\\');
  if (base == std::wstring::npos) return false;
  dir.resize(base);

  // Six levels covers any sane layout without probing a whole drive; each
  // candidate is verified by Adopt, so a false hit cannot get through.
  for (int depth = 0; depth < 6; depth++) {
    if (Adopt(dir)) return true;
    if (Adopt(dir + L"\\War2Dat")) return true;
    const size_t up = dir.find_last_of(L'\\');
    if (up == std::wstring::npos || up < 2) break;  // stop at the drive root
    dir.resize(up);
  }
  return false;
}

bool GameData::Ask(HWND owner, bool* picked) {
  if (picked) *picked = false;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return false;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dialog->SetTitle(Str(IDS_FIND_GAME_TITLE).c_str());

  bool ok = false;
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        if (picked) *picked = true;
        ok = Choose(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return ok;
}

pf_tileset_art* GameData::OpenTileset(int tileset) {
  return source_ ? pf_tileset_art_open_source(source_, tileset, nullptr) : nullptr;
}

std::vector<uint8_t> GameData::ReadFile(const std::string& path) const {
  if (!source_ || path.empty()) return {};
  size_t length = 0;
  uint8_t* bytes = pf_data_source_read(source_, path.c_str(), &length);
  if (!bytes) return {};
  std::vector<uint8_t> out(bytes, bytes + length);
  free(bytes);   // the ABI hands back memory the caller frees
  return out;
}

pf_sprite_set* GameData::OpenSprites(const pf_map* map, const pf_tileset_art* art) {
  if (!source_ || !map || !art) return nullptr;
  pf_sprite_set* set = pf_sprite_set_create();
  if (!set) return nullptr;

  const int tileset = pf_map_tileset(map);
  for (int i = 0, n = pf_map_unit_count(map); i < n; i++) {
    pf_unit unit = {};
    if (pf_map_unit(map, i, &unit) != PF_OK) continue;
    const int type = unit.type, owner = unit.owner;
    if (pf_sprite_set_has(set, type, owner)) continue;
    // Through the one loader, which is also where the Remastered artwork is
    // chosen: this used to open sprites itself and would have missed it.
    if (pf_sprite* sprite = OpenUnitSprite(type, tileset, owner)) {
      pf_sprite_set_add(set, type, owner, sprite);
    }
  }
  return set;
}

int GameData::AddMissingSprites(const pf_map* map, pf_sprite_set* set) {
  if (!source_ || !map || !set) return 0;
  const int tileset = pf_map_tileset(map);
  int added = 0;
  for (int i = 0, n = pf_map_unit_count(map); i < n; i++) {
    pf_unit unit = {};
    if (pf_map_unit(map, i, &unit) != PF_OK) continue;
    if (pf_sprite_set_has(set, unit.type, unit.owner)) continue;
    pf_sprite* sprite = OpenUnitSprite(unit.type, tileset, unit.owner);
    if (sprite && pf_sprite_set_add(set, unit.type, unit.owner, sprite) == PF_OK) {
      added++;
    }
  }
  return added;
}

#ifdef PF_ENABLE_HD_ART

// ------------------------------------------------- the Remastered artwork
//
// The host's half of the import: find the files, read their bytes, keep the
// result. What the bytes mean is the core's, through pf_hd_cache_*.

namespace {

/// Every sidecar matching a pattern in a directory, with its sheet beside it.
std::vector<std::wstring> SidecarsIn(const std::wstring& dir, const wchar_t* pattern) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW found = {};
  HANDLE search = FindFirstFileW((dir + L"\\" + pattern).c_str(), &found);
  if (search == INVALID_HANDLE_VALUE) return out;
  do {
    if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    out.push_back(dir + L"\\" + found.cFileName);
  } while (FindNextFileW(search, &found));
  FindClose(search);
  return out;
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart > 512ll * 1024 * 1024) {
    CloseHandle(file);
    return false;
  }
  out.resize(size_t(size.QuadPart));
  DWORD got = 0;
  const bool ok = ReadFile(file, out.data(), DWORD(out.size()), &got, nullptr) &&
                  got == out.size();
  CloseHandle(file);
  if (!ok) out.clear();
  return ok;
}

bool WriteWholeFile(const std::wstring& path, const uint8_t* bytes, size_t length) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD wrote = 0;
  const bool ok = WriteFile(file, bytes, DWORD(length), &wrote, nullptr) &&
                  wrote == length;
  CloseHandle(file);
  return ok;
}

}  // namespace

std::wstring GameData::HdCachePath(int tile_px) const {
  wchar_t* base = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) != S_OK) return {};
  std::wstring dir = base;
  CoTaskMemFree(base);
  dir += L"\\PUDForge";
  CreateDirectoryW(dir.c_str(), nullptr);   // already there is the usual answer
  return dir + L"\\hd-" + std::to_wstring(tile_px) + L".cache";
}

std::wstring GameData::HdStamp(const std::wstring& hd_dir) const {
  // The biggest sheet answers for the set: a patch that changes the artwork
  // changes this, and one that does not leaves the cache alone.
  WIN32_FILE_ATTRIBUTE_DATA about = {};
  const std::wstring probe = hd_dir + L"\\unit\\units_human_common_sprites.png";
  if (!GetFileAttributesExW(probe.c_str(), GetFileExInfoStandard, &about)) return {};
  wchar_t stamp[64] = {};
  swprintf(stamp, 64, L"%08x%08x-%08x%08x", about.nFileSizeHigh, about.nFileSizeLow,
           about.ftLastWriteTime.dwHighDateTime, about.ftLastWriteTime.dwLowDateTime);
  return stamp;
}

pf_hd_cache* GameData::BuildHdCache(const HdRequest& request) {
  // The kept one first, which is the whole point of having written it.
  std::vector<uint8_t> held;
  if (!request.cache_path.empty() && ReadWholeFile(request.cache_path, held)) {
    if (pf_hd_cache* loaded = pf_hd_cache_load(held.data(), held.size(), request.tile_px,
                                               request.stamp.c_str(), nullptr)) {
      return loaded;
    }
  }

  const std::wstring& hd_dir = request.hd_dir;
  const std::vector<std::wstring> sidecars =
      SidecarsIn(hd_dir + L"\\unit", L"*_sprites.json");
  if (sidecars.empty()) return nullptr;

  pf_hd_cache* cache = pf_hd_cache_create(request.tile_px, request.stamp.c_str());
  if (!cache) return nullptr;
  for (const std::wstring& sidecar : sidecars) {
    std::wstring sheet = sidecar;
    sheet.replace(sheet.size() - 5, 5, L".png");     // .json -> .png
    std::vector<uint8_t> json, png;
    if (!ReadWholeFile(sidecar, json)) continue;
    // Ask before reading the sheet. Only five of the forty-two hold anything
    // an editor draws, and a sheet is a quarter of a second and 235 MB.
    if (pf_hd_cache_add_atlas(cache, ToUtf8(sidecar).c_str(),
                              reinterpret_cast<const char*>(json.data()), json.size(),
                              nullptr, 0) <= 0) {
      continue;
    }
    if (!ReadWholeFile(sheet, png)) continue;
    pf_hd_cache_add_atlas(cache, ToUtf8(sidecar).c_str(),
                          reinterpret_cast<const char*>(json.data()), json.size(),
                          png.data(), png.size());
  }

  // The team-colour masks, after the sprites, because they are matched to the
  // sprites already taken. Without them every unit is the colour the artwork
  // was rendered in.
  for (const std::wstring& sidecar : SidecarsIn(hd_dir + L"\\unit", L"*_masks.json")) {
    std::wstring sheet = sidecar;
    sheet.replace(sheet.size() - 5, 5, L".png");
    std::vector<uint8_t> json, png;
    if (!ReadWholeFile(sidecar, json)) continue;
    if (pf_hd_cache_add_masks(cache, ToUtf8(sidecar).c_str(),
                              reinterpret_cast<const char*>(json.data()), json.size(),
                              nullptr, 0) <= 0) {
      continue;
    }
    if (!ReadWholeFile(sheet, png)) continue;
    pf_hd_cache_add_masks(cache, ToUtf8(sidecar).c_str(),
                          reinterpret_cast<const char*>(json.data()), json.size(),
                          png.data(), png.size());
  }

  // The command-button icons. One sheet holds all four tilesets, keyed by
  // tileset name, and its mask sheet beside it carries the team colour. Fitted
  // to the classic sheet's frames because every panel that draws an icon is
  // laid out around that size.
  if (request.icon_w > 0 && request.icon_h > 0) {
    const std::wstring face = hd_dir + L"\\HUD\\Portrait-face";
    const std::wstring team = hd_dir + L"\\HUD\\Portrait-mask";
    std::vector<uint8_t> face_json, face_png, team_json, team_png;
    const bool have_face =
        ReadWholeFile(face + L".json", face_json) && ReadWholeFile(face + L".png", face_png);
    const bool have_team =
        ReadWholeFile(team + L".json", team_json) && ReadWholeFile(team + L".png", team_png);
    if (have_face) {
      for (int tileset = PF_TILESET_FOREST; tileset <= PF_TILESET_SWAMP; tileset++) {
        pf_hd_cache_add_portraits(cache, tileset,
                                  reinterpret_cast<const char*>(face_json.data()),
                                  face_json.size(), face_png.data(), face_png.size(),
                                  request.icon_w, request.icon_h);
        if (!have_team) continue;
        pf_hd_cache_add_portrait_masks(cache, tileset,
                                       reinterpret_cast<const char*>(team_json.data()),
                                       team_json.size(), team_png.data(),
                                       team_png.size(), request.icon_w, request.icon_h);
      }
    }
  }

  // The terrain sheets, one per tileset. Named for the artwork rather than for
  // the tileset ids, and the last two do not pair the way their names suggest:
  // bgs_Swamp is the wasteland and bgs_XSwamp is the swamp, which is what the
  // megatile counts say.
  struct TerrainSheet { const wchar_t* file; int tileset; };
  static const TerrainSheet kTerrain[] = {
      {L"bgs_Forest", PF_TILESET_FOREST},
      {L"bgs_Ice", PF_TILESET_WINTER},
      {L"bgs_Swamp", PF_TILESET_WASTELAND},
      {L"bgs_XSwamp", PF_TILESET_SWAMP},
  };
  for (const TerrainSheet& set : kTerrain) {
    const std::wstring base = hd_dir + L"\\bgs\\" + set.file;
    std::vector<uint8_t> json, png;
    if (!ReadWholeFile(base + L".json", json)) continue;
    if (pf_hd_cache_add_terrain(cache, set.tileset,
                                reinterpret_cast<const char*>(json.data()), json.size(),
                                nullptr, 0) <= 0) {
      continue;
    }
    if (!ReadWholeFile(base + L".png", png)) continue;
    pf_hd_cache_add_terrain(cache, set.tileset,
                            reinterpret_cast<const char*>(json.data()), json.size(),
                            png.data(), png.size());
  }

  if (pf_hd_cache_sprite_count(cache) <= 0) {
    pf_hd_cache_free(cache);
    return nullptr;
  }

  // Kept for next time. A cache that cannot be written is not a failure: the
  // artwork is in memory and the import simply happens again next run.
  size_t length = 0;
  if (uint8_t* bytes = pf_hd_cache_save(cache, &length)) {
    if (!request.cache_path.empty()) WriteWholeFile(request.cache_path, bytes, length);
    pf_buffer_free(bytes);
  }
  return cache;
}

bool GameData::ApplyHdTerrain(pf_tileset_art* art, int tileset) {
  if (!hd_ready_ || !hd_cache_ || !art) return false;
  return pf_tileset_art_use_hd(art, hd_cache_, tileset) > 0;
}

bool GameData::UseHdArt(int tile_px) {
  // Already holding exactly this. LoadArtwork runs on every map open and the
  // import is seconds long, so asking twice has to be free.
  if (hd_ready_ && hd_tile_px_ == tile_px) return true;

  if (hd_cache_) { pf_hd_cache_free(hd_cache_); hd_cache_ = nullptr; }
  hd_ready_ = false;
  hd_tile_px_ = 0;
  hd_seconds_ = 0;
  if (tile_px <= 0) return false;

  HdRequest request;
  if (!PrepareHdRequest(tile_px, request)) return false;
  const DWORD started = GetTickCount();
  pf_hd_cache* built = BuildHdCache(request);
  hd_seconds_ = double(GetTickCount() - started) / 1000.0;
  AdoptHdCache(built, request.tile_px);
  return hd_ready_;
}

bool GameData::PrepareHdRequest(int tile_px, HdRequest& out) {
  out = HdRequest();
  if (tile_px <= 0 || folder_.empty()) return false;
  // The size asked for. The renderer composes at whatever tile size it is
  // given, so this is now a real choice rather than a wish.
  out.tile_px = tile_px;

  // Beside the data the rest of the artwork comes from, wherever that turned
  // out to be — only Remastered has this at all.
  for (const wchar_t* inside : kInside) {
    const std::wstring candidate = folder_ + inside + L"\\Art\\hd";
    if (GetFileAttributesW((candidate + L"\\unit").c_str()) != INVALID_FILE_ATTRIBUTES) {
      out.hd_dir = candidate;
      break;
    }
  }
  if (out.hd_dir.empty()) return false;
  out.cache_path = HdCachePath(out.tile_px);
  out.stamp = ToUtf8(HdStamp(out.hd_dir));

  // The icon size, read here rather than in the builder: it comes out of the
  // archives this object owns, and a worker thread must not touch those.
  char path[128] = {};
  if (source_ && pf_portrait_path(PF_TILESET_FOREST, path, sizeof(path)) > 0) {
    size_t length = 0;
    if (uint8_t* bytes = pf_data_source_read(source_, path, &length)) {
      if (pf_sprite* classic = pf_sprite_open_memory(bytes, length, nullptr)) {
        out.icon_w = pf_sprite_width(classic);
        out.icon_h = pf_sprite_height(classic);
        pf_sprite_free(classic);
      }
      pf_buffer_free(bytes);
    }
  }
  return true;
}

void GameData::AdoptHdCache(pf_hd_cache* cache, int tile_px) {
  if (hd_cache_ && hd_cache_ != cache) pf_hd_cache_free(hd_cache_);
  hd_cache_ = cache;
  hd_ready_ = cache != nullptr && pf_hd_cache_sprite_count(cache) > 0;
  hd_tile_px_ = hd_ready_ ? tile_px : 0;
}

#endif   // PF_ENABLE_HD_ART

pf_ai_scripts* GameData::OpenAiScripts() {
  if (!source_) return nullptr;
  return pf_ai_scripts_open_source(source_, nullptr);
}

pf_sprite* GameData::OpenPortraits(int tileset, int owner) {
#ifdef PF_ENABLE_HD_ART
  // The Remastered icons when they are in use. `HUD/Portrait-face` holds all
  // four tilesets keyed by name, and a sheet is built per owner because these
  // carry their colour in the pixels rather than in a palette.
  if (hd_ready_ && hd_cache_) {
    if (pf_sprite* hd = pf_hd_cache_portraits(hd_cache_, tileset, owner, nullptr)) {
      return hd;
    }
  }
#endif
  if (!source_) return nullptr;
  char path[128] = {};
  if (pf_portrait_path(tileset, path, sizeof(path)) <= 0) return nullptr;
  size_t length = 0;
  uint8_t* bytes = pf_data_source_read(source_, path, &length);
  if (!bytes) return nullptr;
  pf_sprite* sheet = pf_sprite_open_memory(bytes, length, nullptr);
  pf_buffer_free(bytes);
  return sheet;
}

pf_sprite* GameData::OpenUnitSprite(int unit_id, int tileset, int owner) {
#ifdef PF_ENABLE_HD_ART
  // The Remastered artwork first when it is in use. A unit it has nothing for
  // falls through to the game's own sprite rather than drawing nothing, which
  // is why the Circle of Power still appears.
  if (hd_ready_ && hd_cache_) {
    if (pf_sprite* hd = pf_hd_cache_sprite(hd_cache_, unit_id, owner, nullptr)) {
      return hd;
    }
  }
#endif
  if (!source_) return nullptr;
  pf_sprite* sprite = pf_sprite_open_source(source_, unit_id, tileset, nullptr);
  if (!sprite && tileset != 0) {
    sprite = pf_sprite_open_source(source_, unit_id, 0, nullptr);
  }
  return sprite;
}

}  // namespace pfwin

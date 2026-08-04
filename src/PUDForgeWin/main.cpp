// PUDForge for Windows.
//
// A window, a message loop, the canvas, the docks and the status bar. What the
// editor knows is in pudforge.lib behind a plain C ABI, what the *user* is
// doing is in pfwin::Editor, which is tested off Windows, and this file is
// presentation and input.
//
// The rule, inherited from the other clients: if a behaviour can be described
// without saying "window", it is already in the core — 222 functions that agree
// with 1378 shipped maps.

#include <windows.h>
#include <windowsx.h>

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "Capture.hpp"
#include "Dialogs.hpp"
#include "Docks.hpp"
#include "Editor.hpp"
#include "GameData.hpp"
#include "Form.hpp"
#include "Icons.hpp"
#include "Log.hpp"
#include "Radial.hpp"
#include "Tools.hpp"
#include "Host.hpp"
#include "MapWindow.hpp"
#include "Minimap.hpp"
#include "Sounds.hpp"
#include "PaletteGrid.hpp"
#include "Strings.hpp"
#include "Toolbar.hpp"
#include "UiIcons.hpp"
#include "pudforge/pudforge.h"
#include "resource.h"
#include "strings.h"
#include "version.h"

namespace {

using namespace pfwin;

constexpr wchar_t kAppClass[] = L"PUDForgeMain";
/// The name alone, for the message boxes that are the application talking
/// rather than a window describing itself. A version number in "Save changes?"
/// is noise; in the title bar it is how you tell two builds apart.
constexpr wchar_t kAppTitle[] = L"PUDForge";
/// The title bar before a map has been adopted, so the very first frame drawn
/// already says which build this is.
constexpr wchar_t kAppTitleVersioned[] = L"PUDForge " PF_APP_VERSION_WSTR;
constexpr wchar_t kRecentKey[] = L"Software\\PUDForge\\Recent";
/// Where the window was and how it was set up, kept between runs.
///
/// An editor that forgets the size you gave it is one you have to set up again
/// every morning. None of this is about a map, so none of it is the core's.
constexpr wchar_t kSettingsKey[] = L"Software\\PUDForge\\Settings";

/// The window's own geometry, as one blob. Kept whole rather than as four
/// numbers because a half-restored rectangle is worse than none.
void SavePlacement(HWND hwnd) {
  WINDOWPLACEMENT wp = {};
  wp.length = sizeof(wp);
  if (!GetWindowPlacement(hwnd, &wp)) return;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(key, L"Placement", 0, REG_BINARY,
                 reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
  RegCloseKey(key);
}

/// Put the window back where it was, if that is still somewhere on a screen —
/// a monitor unplugged since would otherwise open it offscreen.
///
/// Sets the rectangle without showing anything, so a session that ended
/// maximised does not open restored and snap.
/// @return whether the last run ended maximised
bool RestorePlacement(HWND hwnd) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  WINDOWPLACEMENT wp = {};
  DWORD size = sizeof(wp), type = 0;
  const bool ok = RegQueryValueExW(key, L"Placement", nullptr, &type,
                                   reinterpret_cast<BYTE*>(&wp), &size) ==
                      ERROR_SUCCESS &&
                  type == REG_BINARY && size == sizeof(wp) &&
                  wp.length == sizeof(wp);
  RegCloseKey(key);
  if (!ok) return false;
  RECT where = wp.rcNormalPosition;
  if (where.right <= where.left || where.bottom <= where.top) return false;
  HMONITOR monitor = MonitorFromRect(&where, MONITOR_DEFAULTTONULL);
  if (!monitor) return false;   // nothing there now: take the default instead
  const bool maximized = wp.showCmd == SW_SHOWMAXIMIZED;
  wp.showCmd = SW_HIDE;
  SetWindowPlacement(hwnd, &wp);
  return maximized;
}

int LoadSetting(const wchar_t* name, int fallback) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return fallback;
  }
  DWORD value = 0, size = sizeof(value), type = 0;
  const bool ok = RegQueryValueExW(key, name, nullptr, &type,
                                   reinterpret_cast<BYTE*>(&value), &size) ==
                      ERROR_SUCCESS &&
                  type == REG_DWORD;
  RegCloseKey(key);
  return ok ? int(value) : fallback;
}

void SaveSetting(const wchar_t* name, int value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const DWORD stored = DWORD(value);
  RegSetValueExW(key, name, 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&stored), sizeof(stored));
  RegCloseKey(key);
}

/// Dock widths in DIPs, scaled by the window's DPI at layout time. Starting
/// points now: both are draggable, and what they are dragged to is kept.
constexpr int kTerrainDockDip = 200;
constexpr int kUnitsDockDip = 220;
/// The gap between a dock and the canvas, which the main window owns and which
/// is what there is to grab.
///
/// The gap *is* the target: either side of it is a child window. Six DIPs is
/// what Windows' own splitters are, and it reads as a seam rather than a bar.
constexpr int kSplitDip = 6;
/// A dock narrower than this cannot show a palette cell and a label, and one
/// wider than this is a dock that has eaten the map.
constexpr int kDockMinDip = 130;
constexpr int kDockMaxDip = 420;

int Scaled(HWND hwnd, int dip) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return MulDiv(dip, int(dpi ? dpi : 96), 96);
}

// ------------------------------------------------------------ recent files

std::vector<std::wstring> LoadRecent() {
  std::vector<std::wstring> out;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRecentKey, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return out;
  }
  for (int i = 0; i <= IDM_FILE_RECENT_LAST - IDM_FILE_RECENT_FIRST; i++) {
    wchar_t name[16];
    wsprintfW(name, L"File%d", i);
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(buffer), &size) == ERROR_SUCCESS &&
        type == REG_SZ && buffer[0]) {
      out.push_back(buffer);
    }
  }
  RegCloseKey(key);
  return out;
}

void SaveRecent(const std::vector<std::wstring>& list) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRecentKey, 0, nullptr, 0, KEY_WRITE,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  for (int i = 0; i <= IDM_FILE_RECENT_LAST - IDM_FILE_RECENT_FIRST; i++) {
    wchar_t name[16];
    wsprintfW(name, L"File%d", i);
    if (i < int(list.size())) {
      RegSetValueExW(key, name, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(list[size_t(i)].c_str()),
                     DWORD((list[size_t(i)].size() + 1) * sizeof(wchar_t)));
    } else {
      RegDeleteValueW(key, name);
    }
  }
  RegCloseKey(key);
}

/// The submenu holding a command, anywhere in the tree.
///
/// A position — `GetSubMenu(menu, 4)` — is right until somebody adds an item
/// above it and then quietly fills the wrong menu.
HMENU FindPopupContaining(HMENU menu, UINT id) {
  if (!menu) return nullptr;
  const int count = GetMenuItemCount(menu);
  for (int i = 0; i < count; i++) {
    if (HMENU sub = GetSubMenu(menu, i)) {
      if (HMENU found = FindPopupContaining(sub, id)) return found;
    } else if (GetMenuItemID(menu, UINT(i)) == id) {
      return menu;
    }
  }
  return nullptr;
}

// ------------------------------------------------------------ file dialogs

std::wstring AskOpenPath(HWND owner) {
  std::wstring result;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return result;
  }
  // The patterns are not text and must not be translated; the labels are.
  const std::wstring maps = Str(IDS_FILTER_MAPS), all = Str(IDS_FILTER_ALL);
  const COMDLG_FILTERSPEC kFilter[] = {{maps.c_str(), L"*.pud"},
                                       {all.c_str(), L"*.*"}};
  dialog->SetFileTypes(2, kFilter);
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

std::wstring AskSavePath(HWND owner, const std::wstring& current) {
  std::wstring result;
  IFileSaveDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return result;
  }
  const std::wstring maps = Str(IDS_FILTER_MAPS);
  const COMDLG_FILTERSPEC kFilter[] = {{maps.c_str(), L"*.pud"}};
  dialog->SetFileTypes(1, kFilter);
  dialog->SetDefaultExtension(L"pud");
  if (!current.empty()) {
    const size_t slash = current.find_last_of(L"\\/");
    dialog->SetFileName(slash == std::wstring::npos
                            ? current.c_str() : current.c_str() + slash + 1);
  }
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return result;
}

/// Put the embedded user guide in front of the reader.
///
// -------------------------------------------------------------------- app

/// Which cell of the status bar is which.
///
/// Named rather than numbered at the six places that set text: the order is a
/// layout decision that has changed once and will change again, and a bare
/// `SB_SETTEXT, 5` at the far end of the file is how a cell says the wrong
/// thing. PartStatus sizes them in this same order.
enum StatusCell {
  kCellHover = 0,     ///< "128, 128  Shore" — first, because it is read most
  kCellTool,          ///< "Painting"
  kCellHint,          ///< the chords that tool answers to
  kCellMessage,       ///< takes the slack; the one that can be a sentence
  kCellSelection,     ///< "12 units in 20 x 20"
  kCellZoom,
  kCellSize,
  kCellCount,
};

struct App : Host {
  HWND main = nullptr;
  /// The Recent Maps popup, looked up once. See RefreshRecentMenu.
  HMENU recent_menu = nullptr;
  HINSTANCE instance = nullptr;
  MapWindow canvas;
  Editor editor{nullptr};
  GameData game;
  /// Command-button icons, shared by the unit palette, the property sheets
  /// and the quick pick — one rasterisation each, however many ask.
  IconCache icons;
  /// The editor's own button artwork, loaded from the exe's own resources
  /// rather than a Warcraft II folder that may not be there. Nothing to do with
  /// `icons` above, which is the *game's*.
  UiIcons ui_icons;
  TerrainPanel terrain_panel;
  UnitsPanel units_panel;
  Minimap minimap;
  Toolbar toolbar;
  SoundPlayer sounds;
  HWND status = nullptr;
  pf_tileset_art* art = nullptr;
  pf_sprite_set* sprites = nullptr;
  std::wstring path;
  std::vector<std::wstring> recent = LoadRecent();
  /// Which artwork a unit shows: 0 its command-button icon, 1 its own sprite.
  /// The client's, not the editor's — it changes nothing about the map.
  int unit_art = LoadSetting(L"UnitArt", 0);
  bool vary_facing = LoadSetting(L"VaryFacing", 1) != 0;
  // Off unless asked for: an editor that talks back is a preference, not a
  // default, and it needs the game to hand to say anything at all.
  bool unit_sounds = LoadSetting(L"UnitSounds", 0) != 0;
  /// The furniture, hideable the way the web client's is. The canvas takes
  /// whatever is left, so hiding a dock is a real gain in room to work.
  bool show_docks = LoadSetting(L"ShowDocks", 1) != 0;
  bool show_toolbar = LoadSetting(L"ShowToolbar", 1) != 0;
  /// Dock widths in DIPs, as dragged. Stored unscaled so moving the window to
  /// a different monitor keeps the dock the same size in inches.
  int left_dip = LoadSetting(L"LeftDock", kTerrainDockDip);
  int right_dip = LoadSetting(L"RightDock", kUnitsDockDip);

  /// Which side of the canvas each dock lives on. Terrain left is what a
  /// right-handed mapper working a palette with the left hand wants; a
  /// left-hander wants the mirror, and there is no way to know which.
  bool terrain_right = LoadSetting(L"TerrainOnRight", 0) != 0;
  bool units_right = LoadSetting(L"UnitsOnRight", 1) != 0;
  /// Left by default, above the terrain palette: the minimap is where you look
  /// to decide where to paint next, so it belongs beside the thing you paint
  /// with rather than across the window from it.
  bool minimap_right = LoadSetting(L"MinimapOnRight", 0) != 0;

  bool DockIsRight(Dock which) const override {
    switch (which) {
      case Dock::kTerrain: return terrain_right;
      case Dock::kUnits: return units_right;
      case Dock::kMinimap: return minimap_right;
    }
    return false;
  }

  void OnDockSide(Dock which, bool right) override {
    bool* side = which == Dock::kTerrain ? &terrain_right
               : which == Dock::kUnits ? &units_right
                                       : &minimap_right;
    if (*side == right) return;
    *side = right;
    // A side that has just been emptied gives its width to the canvas, and one
    // that has just been filled needs a width to give, so both are laid out.
    Layout();
    // A side that went from two docks to one changes height without changing
    // width, and the palettes size their cells from the width — so ask for the
    // repaint rather than relying on WM_SIZE.
    InvalidateRect(main, nullptr, TRUE);
  }
  /// Which splitter is being dragged: -1 none, 0 left, 1 right.
  int dragging_split = -1;

  /// Whether the pointer is over the status bar's message cell, which is the
  /// one that opens the log.
  bool OverStatusMessage() const {
    if (!status) return false;
    POINT at{};
    GetCursorPos(&at);
    ScreenToClient(status, &at);
    RECT part{};
    if (!SendMessageW(status, SB_GETRECT, kCellMessage,
                      reinterpret_cast<LPARAM>(&part))) {
      return false;
    }
    return PtInRect(&part, at) != FALSE;
  }

  static int ClampedDock(int dip) {
    return std::max(kDockMinDip, std::min(dip, kDockMaxDip));
  }

  /// Which splitter, if any, a point in client coordinates is over.
  /// @return 0 left, 1 right, -1 neither
  int SplitAt(int x, int y) const {
    if (!show_docks) return -1;
    RECT rc;
    GetClientRect(main, &rc);
    RECT bar;
    GetWindowRect(status, &bar);
    const int height = rc.bottom - (show_status ? bar.bottom - bar.top : 0);
    // The seams start under the toolbar, or a press on the strip beside the
    // last button would grab a splitter that is not there.
    if (y < toolbar.Height() || y >= height) return -1;
    const int split = Scaled(main, kSplitDip);
    // A side with nothing docked to it has no width and no seam — there is
    // nothing there to drag the edge of.
    if (SideHasSomething(false)) {
      const int left_w = Scaled(main, ClampedDock(left_dip));
      if (x >= left_w && x < left_w + split) return 0;
    }
    if (SideHasSomething(true)) {
      const int right_w = Scaled(main, ClampedDock(right_dip));
      const int edge = rc.right - right_w - split;
      if (x >= edge && x < edge + split) return 1;
    }
    return -1;
  }

  /// Move a splitter to where the pointer is. Widths are kept in DIPs so the
  /// dock stays the same size in inches if the window changes monitor.
  void DragSplit(int which, int x) {
    RECT rc;
    GetClientRect(main, &rc);
    const UINT dpi = GetDpiForWindow(main);
    const int scale = int(dpi ? dpi : 96);
    const int split = Scaled(main, kSplitDip);
    const int want = which == 0 ? x : rc.right - x - split;
    const int dip = MulDiv(std::max(0, want), 96, scale);
    int& target = which == 0 ? left_dip : right_dip;
    if (ClampedDock(dip) == ClampedDock(target)) return;
    target = ClampedDock(dip);
    Layout();
  }

  /// Paint the seams the splitters live in. Everything else on the main
  /// window is covered by a child, so this is the only thing it draws.
  void PaintSplits(HDC dc) {
    if (!show_docks) return;
    RECT rc;
    GetClientRect(main, &rc);
    RECT bar;
    GetWindowRect(status, &bar);
    const int height = rc.bottom - (show_status ? bar.bottom - bar.top : 0);
    const int top = toolbar.Height();
    const int split = Scaled(main, kSplitDip);
    HBRUSH face = GetSysColorBrush(COLOR_BTNFACE);
    if (SideHasSomething(false)) {
      const int left_w = Scaled(main, ClampedDock(left_dip));
      RECT one{left_w, top, left_w + split, height};
      FillRect(dc, &one, face);
    }
    if (SideHasSomething(true)) {
      const int right_w = Scaled(main, ClampedDock(right_dip));
      RECT one{rc.right - right_w - split, top, rc.right - right_w, height};
      FillRect(dc, &one, face);
    }
  }
  bool show_status = LoadSetting(L"ShowStatus", 1) != 0;
  bool show_minimap = LoadSetting(L"ShowMinimap", 1) != 0;

  /// Put back what the last run was set up with — the furniture, the view
  /// toggles, and how the brush was left. Not the map: which file to open is
  /// the command line's business, and reopening one silently is a surprise.
  void RestoreSettings() {
    editor.show_grid = LoadSetting(L"Grid", 0) != 0;
    // Floored at one tile, so a run always opens on a brush that covers a tile
    // even if the last one ended on the corner rung. The corner brush is a
    // deliberate reach for something smaller than the grid, and inheriting it
    // silently from a previous session means the first stroke of a new map is a
    // quarter the size the pointer looks like it is.
    editor.brush_size = std::max(1, LoadSetting(L"BrushSize", 1));
    editor.brush_shape = LoadSetting(L"BrushShape", PF_BRUSH_SQUARE);
    editor.mix_shades = LoadSetting(L"MixShades", 0) != 0;
    // Which drawing of a terrain the brush lays, kept for the same reason the
    // shape and the size are: it is a setting about how you paint, not about
    // any one map.
    editor.paint_dark = LoadSetting(L"PaintDark", 0) != 0;
    editor.SetVariationPolicy(LoadSetting(L"Variation", PF_VARIATION_PLAIN));
    // A preference about the palette rather than about a map, so it is kept
    // with the rest of them. Off by default: see Editor::show_all_races.
    editor.show_all_races = LoadSetting(L"ShowAllRaces", 0) != 0;
    icons.SetPreferSprites(unit_art != 0);
    canvas.SetWaterAnimated(LoadSetting(L"Water", 1) != 0);
    terrain_panel.SetColumns(LoadSetting(L"TerrainColumns", 0));
    units_panel.SetColumns(LoadSetting(L"UnitColumns", 0));
  }

  void SaveSettings() {
    // A reset deletes the whole key; writing the running window's settings back
    // out on the way to the desktop would undo it silently, and the person
    // would find the same layout waiting for them next time.
    if (SettingsWereReset()) return;
    SaveSetting(L"ShowDocks", show_docks);
    SaveSetting(L"ShowToolbar", show_toolbar);
    SaveSetting(L"LeftDock", left_dip);
    SaveSetting(L"RightDock", right_dip);
    SaveSetting(L"ShowStatus", show_status);
    SaveSetting(L"ShowMinimap", show_minimap);
    SaveSetting(L"UnitArt", unit_art);
    SaveSetting(L"VaryFacing", vary_facing);
    SaveSetting(L"UnitSounds", unit_sounds);
    SaveSetting(L"Grid", editor.show_grid);
    SaveSetting(L"BrushSize", editor.brush_size);
    SaveSetting(L"BrushShape", editor.brush_shape);
    SaveSetting(L"MixShades", editor.mix_shades);
    SaveSetting(L"PaintDark", editor.paint_dark);
    SaveSetting(L"Variation", editor.variation_policy());
    SaveSetting(L"ShowAllRaces", editor.show_all_races);
    SaveSetting(L"Water", canvas.water_animated());
    SaveSetting(L"TerrainColumns", terrain_panel.columns());
    SaveSetting(L"UnitColumns", units_panel.columns());
    SaveSetting(L"TerrainOnRight", terrain_right);
    SaveSetting(L"UnitsOnRight", units_right);
    SaveSetting(L"MinimapOnRight", minimap_right);
  }
  /// Where the context menu was opened, for the items that act on a tile.
  POINT context_tile_{};

  // ---- Host --------------------------------------------------------------
  void OnMapEdited() override {
    // A new unit type may have no sprite yet; load it before the repaint.
    if (canvas.map() && sprites) game.AddMissingSprites(canvas.map(), sprites);
    // A stroke says which tiles it touched, so the canvas can recompose those
    // rather than the whole visible region. Anything that did not say — undo, a
    // property sheet, a resize — gets the full recompose.
    const TileRect& touched = editor.touched();
    if (!touched.empty()) {
      canvas.MarkTilesChanged(touched.x, touched.y, touched.w, touched.h);
      editor.ClearTouched();
    }
    canvas.Invalidate();
    minimap.MarkMapChanged();
    RefreshTitle();
    RefreshStatusCells();
    RefreshCommands();
  }

  void OnUnitSound(int unit_type, int kind) override {
    sounds.Play(unit_type, pf_sound_kind(kind));
  }

  void OnMapStroke() override {
    // MarkMapChanged rather than Invalidate: the minimap composes from the map,
    // so the tiles the brush just wrote are only in the picture if the picture
    // is built again. See Minimap::Invalidate for why a posted WM_PAINT never
    // arrives until the button is up.
    minimap.MarkMapChanged();
  }

  void OnViewChanged() override {
    RefreshStatusCells();
    RefreshViewportBox();
    toolbar.ShowZoom(canvas.view().zoom);
  }

  void OnEditorChanged() override {
    canvas.Invalidate();
    terrain_panel.Refresh();
    units_panel.Refresh();
    RefreshSelectionCell();
    RefreshCommands();
    RefreshHint();
  }

  /// Which tool is in hand, and the line of keyboard help that goes with it.
  ///
  /// Two cells rather than one sentence: the tool is a noun you glance at, and
  /// the hints are chords you read once and then stop seeing. Every hint is
  /// something useful that nobody discovers — shift for the other shade, alt
  /// and the wheel for the brush size — and they were in the F1 list, which is
  /// where you look for a key you already suspect exists.
  void ToolAndHint(UINT& tool, UINT& hint) const {
    if (editor.pasting()) { tool = IDS_TOOL_PASTING; hint = IDS_HINT_PASTE; return; }
    if (editor.mode() == Mode::kTerrain) {
      if (editor.tool() == Tool::kRect) {
        tool = IDS_TOOL_RECT;
        hint = IDS_HINT_TERRAIN_RECT;
        return;
      }
      // The bucket is a brush *shape*, not a tool of its own, so it shared the
      // brush's line — which offers a straight stroke it cannot draw and a size
      // its own slider greys out.
      if (editor.brush_shape == Editor::kShapeFill) {
        tool = IDS_TOOL_FILLING;
        hint = IDS_HINT_TERRAIN_FILL;
        return;
      }
      tool = IDS_TOOL_PAINTING;
      hint = IDS_HINT_TERRAIN_PAINT;
      return;
    }
    const bool place = editor.tool() == Tool::kPlace;
    tool = place ? IDS_TOOL_PLACING : IDS_TOOL_PICKING;
    hint = place ? IDS_HINT_UNIT_PLACE : IDS_HINT_UNIT_SELECT;
  }

  /// Put the tool and its tips in their two cells, at the left of the bar.
  ///
  /// Their own cells rather than the message: a hint is true for as long as the
  /// tool is in hand, and a message is news.
  void RefreshHint() {
    UINT tool = 0, hint = 0;
    ToolAndHint(tool, hint);
    if (hint == shown_hint_) return;
    shown_hint_ = hint;
    SendMessageW(status, SB_SETTEXT, kCellTool,
                 reinterpret_cast<LPARAM>(Str(tool).c_str()));
    SendMessageW(status, SB_SETTEXT, kCellHint,
                 reinterpret_cast<LPARAM>(Str(hint).c_str()));
  }

  /// Grey on the strip what the menus grey when they drop.
  ///
  /// The conditions are `OnMenuOpening`'s, deliberately: a toolbar button that
  /// disagrees with the menu item it raises is a toolbar button that is wrong.
  void RefreshCommands() {
    const bool copyable =
        !editor.terrain_selection().empty() || editor.HasSelection();
    toolbar.SetEnabled(IDM_EDIT_UNDO, editor.CanUndo());
    toolbar.SetEnabled(IDM_EDIT_REDO, editor.CanRedo());
    toolbar.SetEnabled(IDM_EDIT_DELETE, editor.HasSelection());
    toolbar.SetEnabled(IDM_EDIT_COPY, copyable);
    toolbar.SetEnabled(IDM_EDIT_CUT, copyable);
    toolbar.SetEnabled(IDM_EDIT_PASTE, editor.HasClipboard());
    // The two tools the strip carries stay pushed in while they are in hand.
    // They came off the docks, where a radio button showed this for free.
    toolbar.SetChecked(IDM_TOOL_TERRAIN_SELECT, editor.tool() == Tool::kRect);
    toolbar.SetChecked(IDM_TOOL_UNIT_SELECT, editor.tool() == Tool::kSelect);
  }

  void OnStatus(const std::wstring& text, bool warn) override {
    Say(text, text, warn);
  }

  /// Say one thing in the status bar and keep another in the log.
  ///
  /// A status line is one line wide and a path eats it, but "which of the four
  /// copies did I just open" is a question the log has to answer.
  void Say(const std::wstring& brief, const std::wstring& full, bool warn) {
    const std::wstring line = warn ? L"⚠ " + brief : brief;
    SendMessageW(status, SB_SETTEXT, kCellMessage,
                 reinterpret_cast<LPARAM>(line.c_str()));
    // A status bar shows a part's tip only when the text is too long to fit, so
    // this is set every time rather than once.
    SendMessageW(status, SB_SETTIPTEXT, kCellMessage,
                 reinterpret_cast<LPARAM>(Str(IDS_STATUS_OPENS_LOG).c_str()));
    // The status bar holds one line and then loses it, and the ones worth
    // reading twice are exactly the ones that scroll away.
    Log::The().Add(full, warn);
  }

  void OnBulkEdit(BulkEdit which) override {
    std::wstring note;
    const bool changed =
        which == BulkEdit::kReplace
            ? ShowReplaceTerrain(main, instance, editor, art, note)
            : ShowDecorate(main, instance, editor, art, note);
    // MarkMapChanged before OnMapEdited: the patch the canvas may be holding is
    // from something else, and these two run several passes over ground the
    // last stroke never touched.
    if (changed) { canvas.MarkMapChanged(); OnMapEdited(); }
    // "Nothing to replace" is a refusal rather than a result, hence the warning
    // colour. A cancelled sheet says nothing at all.
    if (!note.empty()) OnStatus(note, !changed);
  }

  void OnScrollTo(int tx, int ty) override {
    canvas.CentreOn(tx, ty);
    // Painted inside the gesture, for the reason Minimap::Invalidate gives:
    // WM_PAINT is the lowest-priority message there is, so the map used to lag
    // a whole drag behind the box.
    UpdateWindow(canvas.hwnd());
  }

  void OnOpenMapSheet(int tab, int row) override { OpenMapSheets(tab, row); }

  void OnInspectUnit(int index) override {
    std::wstring note;
    int properties_for = -1;
    if (ShowUnitInspector(main, instance, editor, &icons, index, note,
                          &properties_for)) {
      OnMapEdited();
    }
    if (!note.empty()) OnStatus(note, false);
    // After the edit is applied, so the sheet opens over a map that already
    // holds it. The row is the unit id, the same way the units dock's own jump
    // into this page works.
    if (properties_for >= 0) OpenMapSheets(kMapSheetUnits, properties_for);
  }

  /// What can be done here, at the pointer.
  ///
  /// Built from the menu bar rather than written out a second time, so a
  /// context menu cannot disagree with the menus it shadows.
  void OnContextMenu(int tx, int ty, POINT screen) override {
    HMENU popup = CreatePopupMenu();
    if (!popup) return;
    const bool has_units = editor.HasSelection();
    const bool has_rect = !editor.terrain_selection().empty();

    auto item = [&](int id, UINT text, bool enabled) {
      AppendMenuW(popup, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), UINT(id),
                  Str(text).c_str());
    };
    auto named = [&](int id, UINT text, const std::wstring& what) {
      AppendMenuW(popup, MF_STRING, UINT(id), Format(text, what.c_str()).c_str());
    };

    // A unit under the pointer decides what leads. Over bare terrain there is
    // nothing to name, and the terrain actions are not offered here — Ctrl+left
    // is the eyedropper and the view goes where the minimap is clicked.
    const int under = editor.SelectAt(tx, ty, false);
    std::wstring type_name;
    if (under >= 0) {
      OnEditorChanged();
      pf_unit u{};
      if (pf_map_unit(canvas.map(), under, &u) == PF_OK) {
        const char* name = pf_unit_name(u.type);
        if (name) type_name = FromUtf8(name);
      }
      item(IDM_CONTEXT_INSPECT, IDS_CONTEXT_UNIT_PROPS, true);
      item(IDM_EDIT_DELETE, IDS_CONTEXT_DELETE, true);
      if (!type_name.empty()) {
        named(IDM_CONTEXT_SAME_TYPE, IDS_CONTEXT_SAME_TYPE, type_name);
        named(IDM_EDIT_DUPLICATE, IDS_CONTEXT_DUPLICATE, type_name);
      }
      AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    }
    item(IDM_EDIT_COPY, IDS_CONTEXT_COPY, has_units || has_rect);
    item(IDM_EDIT_CUT, IDS_CONTEXT_CUT, has_units || has_rect);
    item(IDM_EDIT_PASTE, IDS_CONTEXT_PASTE, editor.HasClipboard());
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    item(IDM_EDIT_FILL, IDS_CONTEXT_FILL, has_rect);
    item(IDM_MAP_STATS, IDS_CONTEXT_STATS, canvas.map() != nullptr);

    context_tile_ = {tx, ty};
    TrackPopupMenu(popup, TPM_RIGHTBUTTON, screen.x, screen.y, 0, main, nullptr);
    DestroyMenu(popup);
  }

  void OnHoverTile(int tx, int ty) override {
    wchar_t text[64] = L"";
    if (tx >= 0 && canvas.map()) {
      // The tile, and what stands on it - the same cell the web client keeps.
      const int terrain =
          pf_tile_dominant_terrain(uint16_t(pf_map_tile_at(canvas.map(), tx, ty)));
      const char* name = pf_terrain_name(terrain, pf_map_tileset(canvas.map()));
      const std::wstring line = Format(IDS_HOVER_TILE, tx, ty,
                                       name ? FromUtf8(name).c_str() : L"");
      wcsncpy(text, line.c_str(), 63);
    }
    SendMessageW(status, SB_SETTEXT, kCellHover, reinterpret_cast<LPARAM>(text));
  }

  // ---- pieces ------------------------------------------------------------
  void RefreshTitle() {
    std::wstring name = path.empty() ? Str(IDS_UNTITLED) : path;
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    SetWindowTextW(main,
                   Format(editor.Dirty() ? IDS_TITLE_DIRTY : IDS_TITLE,
                          name.c_str(), PF_APP_VERSION_WSTR).c_str());
  }

  void RefreshStatusCells() {
    if (!canvas.map()) return;
    wchar_t text[64];
    const std::wstring zoom = Format(IDS_ZOOM_PERCENT, canvas.view().zoom);
    wcsncpy(text, zoom.c_str(), 63);
    SendMessageW(status, SB_SETTEXT, kCellZoom, reinterpret_cast<LPARAM>(text));
    const std::wstring size = Format(IDS_MAP_SIZE, pf_map_width(canvas.map()),
                                     pf_map_height(canvas.map()));
    SendMessageW(status, SB_SETTEXT, kCellSize, reinterpret_cast<LPARAM>(size.c_str()));
    RefreshSelectionCell();
  }

  /// What is selected, in the cell between the pointer and the zoom readout.
  ///
  /// How *many* matters as much as how big once units are involved. Empty when
  /// nothing is selected, rather than "0 units".
  void RefreshSelectionCell() {
    std::wstring text;
    const TileRect rect = editor.terrain_selection();
    const int units = int(editor.selected().size());
    if (units > 0) {
      const TileRect box = editor.SelectionBounds();
      text = Format(Plural(units, IDS_SELECTED_ONE, IDS_SELECTED_MANY), units,
                    box.w, box.h);
    } else if (!rect.empty()) {
      // A region's area is not its box's: "24 tiles in 8 x 6" says both what
      // will be painted and how far it reaches.
      text = editor.TerrainSelectionIsRect()
                 ? Format(IDS_SELECTED_TILES, rect.w, rect.h)
                 : Format(IDS_SELECTED_REGION, editor.terrain_selected_count(),
                          rect.w, rect.h);
    }
    SendMessageW(status, SB_SETTEXT, kCellSelection,
                 reinterpret_cast<LPARAM>(text.c_str()));
  }

  void RefreshViewportBox() {
    int x0 = 0, y0 = 0, cols = 0, rows = 0;
    pf::view_region(canvas.view(), x0, y0, cols, rows);
    minimap.SetViewport(x0, y0, cols, rows);
  }

  void RefreshRecentMenu() {
    // Found by the id the template put inside the popup rather than by its
    // position in the File menu, and cached: after the first refill the item
    // that answered is gone, replaced by the first map.
    if (!recent_menu) {
      recent_menu = FindPopupContaining(GetMenu(main), IDM_FILE_RECENT_FIRST);
    }
    HMENU menu = recent_menu;
    if (!menu) return;
    while (GetMenuItemCount(menu) > 0) DeleteMenu(menu, 0, MF_BYPOSITION);
    if (recent.empty()) {
      // Something rather than nothing: an empty popup opens as a small blank
      // box, which reads as a broken menu instead of an empty list.
      AppendMenuW(menu, MF_GRAYED, IDM_FILE_RECENT_FIRST,
                  Str(IDS_RECENT_EMPTY).c_str());
      return;
    }
    for (int i = 0; i < int(recent.size()); i++) {
      std::wstring name = recent[size_t(i)];
      const size_t slash = name.find_last_of(L"\\/");
      if (slash != std::wstring::npos) name = name.substr(slash + 1);
      name = Format(IDS_RECENT_ITEM, i + 1, name.c_str());
      AppendMenuW(menu, MF_STRING, UINT(IDM_FILE_RECENT_FIRST + i),
                  name.c_str());
    }
  }

  void AddRecent(const std::wstring& file) {
    if (SettingsWereReset()) return;   // a reset records nothing after it
    for (auto it = recent.begin(); it != recent.end();) {
      if (*it == file) it = recent.erase(it);
      else ++it;
    }
    recent.insert(recent.begin(), file);
    if (recent.size() > 10) recent.resize(10);
    SaveRecent(recent);
    RefreshRecentMenu();
  }

  void LoadArtwork() {
    if (!canvas.map()) return;
    if (art) pf_tileset_art_free(art);
    const int tileset = pf_map_tileset(canvas.map());
    art = game.OpenTileset(tileset);

    if (sprites) pf_sprite_set_free(sprites);
    sprites = art ? game.OpenSprites(canvas.map(), art) : nullptr;

    icons.Open(&game, art, tileset);
    // The clips it cached came out of whatever folder was in force, and this is
    // the moment that can have changed.
    sounds.Open(&game);
    sounds.SetEnabled(unit_sounds);

    canvas.SetArtwork(art, sprites);
    terrain_panel.SetArtwork(art, tileset);
    units_panel.SetArtwork(&icons, art, tileset);
    minimap.SetMap(canvas.map(), art);
  }

  void AdoptMap(pf_map* map, const std::wstring& file) {
    path = file;
    canvas.SetMap(map);
    editor.SetMap(map);
    // A freshly opened file has nothing to undo back to.
    if (map) pf_map_clear_history(map);
    LoadArtwork();
    RefreshTitle();
    RefreshStatusCells();
    RefreshViewportBox();
    // A map that has just been opened has nothing to undo and nothing selected,
    // and this is not one of the paths that goes through OnMapEdited.
    RefreshCommands();
    if (file.empty()) {
      OnStatus(Str(IDS_NEW_MAP), false);
    } else {
      Say(Format(IDS_OPENED, NameOf(file).c_str()),
          Format(IDS_OPENED, file.c_str()), false);
    }
  }

  bool OpenMap(const std::wstring& file) {
    pf_status status_code = PF_OK;
    pf_map* map = pf_map_open_file(ToUtf8(file).c_str(), &status_code);
    if (!map) {
      const std::wstring why = FromUtf8(pf_status_message(status_code));
      MessageBoxW(main, Format(IDS_CANNOT_OPEN, file.c_str(), why.c_str()).c_str(),
                  kAppTitle,
                  MB_OK | MB_ICONERROR);
      return false;
    }
    AdoptMap(map, file);
    AddRecent(file);
    return true;
  }

  void NewMap() {
    if (!ConfirmDiscard()) return;
    // Ask, rather than making a 64x64 forest map and leaving the user to change
    // both: changing the tileset afterwards repaints every tile.
    const int tileset = canvas.map() ? pf_map_tileset(canvas.map()) : PF_TILESET_FOREST;
    if (pf_map* made = ShowNewMap(main, instance, tileset)) AdoptMap(made, L"");
  }

  /// Open the tabbed map window on a tab, and deal with whatever it wrote.
  ///
  /// One place rather than six, because what has to happen afterwards is the
  /// union of what any page might have done: a tileset change repaints every
  /// tile, a `UDTA` edit changes what may stand where. Doing the lot is a few
  /// milliseconds and cannot be wrong.
  void OpenMapSheets(int tab, int row = -1) {
    if (!canvas.map()) return;
    std::wstring note;
    MapSheetsOutcome outcome;
    if (ShowMapSheets(main, instance, canvas.map(), &icons, &game, &ui_icons,
                      tab, row, note,
                      &outcome)) {
      // A player whose race changed takes their units with them. This was a
      // dialog of its own able to disagree with the player page about `SIDE`,
      // which is what makes a swapped base unbuildable rather than merely odd.
      // Its own undo step per player, so one press takes it back.
      for (int player : outcome.races_changed) {
        const Editor::BulkResult swapped =
            editor.SwitchPlayerRace(player, pf_map_race(canvas.map(), player));
        if (swapped.changed == 0 && swapped.kept == 0) continue;
        const char* who = pf_player_name(player);
        note = Format(IDS_RACE_FOLLOWED, who ? FromUtf8(who).c_str() : L"",
                      Str(pf_map_race(canvas.map(), player) == PF_RACE_ORC
                              ? IDS_RACE_ORC_NAME
                              : IDS_RACE_HUMAN_NAME).c_str(),
                      swapped.changed, swapped.kept);
      }
      LoadArtwork();
      editor.MarkMapChanged();
      canvas.MarkMapChanged();
      minimap.MarkMapChanged();
      // A resize replaces the grid rather than a value in it, so both
      // selections are rectangles of a grid that no longer exists and the views
      // have to be told the *shape* changed.
      if (outcome.resized) {
        editor.ClearSelection();
        editor.ClearTerrainSelection();
        canvas.MapSizeChanged();
        minimap.MapSizeChanged();
      }
      OnMapEdited();
      if (outcome.resized) OnEditorChanged();
    }
    // How many units the new grid had no room for decides whether this is a
    // warning; nothing else in the window can lose anything.
    if (!note.empty()) OnStatus(note, outcome.dropped_units > 0);
  }

  void GenerateMap() {
    if (!ConfirmDiscard()) return;
    const int tileset = canvas.map() ? pf_map_tileset(canvas.map()) : 0;
    if (pf_map* made = ShowGenerate(main, instance, tileset)) {
      AdoptMap(made, L"");
      // Generation happens before any artwork is attached, so the generator
      // chose from all sixteen variations of every group and 81% of the map
      // came out as blank megatiles. AdoptMap has just attached the tileset, so
      // re-choose every tile against what can actually be drawn.
      if (canvas.map()) {
        pf_map_refit(canvas.map());
        pf_map_clear_history(canvas.map());
        editor.MarkMapChanged();
        canvas.MarkMapChanged();
        minimap.MarkMapChanged();
      }
      OnStatus(Str(IDS_GENERATED), false);
    }
  }

  /// How many start locations the map carries.
  int StartLocationCount() const {
    if (!canvas.map()) return 0;
    int n = 0;
    for (int i = 0; i < pf_map_unit_count(canvas.map()); i++) {
      pf_unit unit{};
      if (pf_map_unit(canvas.map(), i, &unit) != PF_OK) continue;
      if (pf_unit_in_group(unit.type, PF_GROUP_START_LOCATIONS)) n++;
    }
    return n;
  }

  /// Offer to place the missing start locations before a save.
  ///
  /// A map with units on it and fewer than two places to start from is one the
  /// game will not begin a match on, and nothing about opening it says so.
  /// Saving is the moment it stops being a work in progress.
  ///
  /// Asked rather than done: a campaign map really can have one start location.
  void OfferStartLocations() {
    if (!canvas.map()) return;
    const int units = pf_map_unit_count(canvas.map());
    const int starts = StartLocationCount();
    if (units == 0 || starts >= 2) return;
    if (MessageBoxW(main, Format(IDS_STARTS_MISSING, units, starts).c_str(),
                    Str(IDS_STARTS_MISSING_TITLE).c_str(),
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
      return;
    }
    RunPlaceStarts();
  }

  bool Save(bool ask_path) {
    if (!canvas.map()) return false;
    // Before the file dialog, so answering yes still lands in one file rather
    // than leaving the map dirty again straight after.
    OfferStartLocations();
    std::wstring to = path;
    if (ask_path || to.empty()) {
      to = AskSavePath(main, path);
      if (to.empty()) return false;
    }
    const pf_status status_code =
        pf_map_save_file(canvas.map(), ToUtf8(to).c_str());
    if (status_code != PF_OK) {
      MessageBoxW(main, FromUtf8(pf_status_message(status_code)).c_str(),
                  kAppTitle, MB_OK | MB_ICONERROR);
      return false;
    }
    path = to;
    editor.MarkClean();
    AddRecent(to);
    RefreshTitle();
    Say(Format(IDS_SAVED, NameOf(to).c_str()), Format(IDS_SAVED, to.c_str()),
        false);
    return true;
  }

  /// True when it is fine to drop the current map - saved, discarded, or clean.
  bool ConfirmDiscard() {
    if (!editor.Dirty()) return true;
    const int answer =
        MessageBoxW(main, Str(IDS_SAVE_CHANGES).c_str(), kAppTitle,
                    MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) return Save(false);
    return true;
  }

  // ------------------------------------------------------------- commands
  void RunValidate() {
    if (!canvas.map()) return;
    const int total = pf_map_validate(canvas.map(), nullptr, 0);
    if (total == 0) {
      MessageBoxW(main, Str(IDS_CHECK_NONE).c_str(), Str(IDS_CHECK_TITLE).c_str(),
                  MB_OK | MB_ICONINFORMATION);
      return;
    }
    // static_cast, not size_t(total): the functional cast is a most vexing
    // parse on every compiler, and the .data() two lines down stops compiling.
    std::vector<pf_issue> issues(static_cast<size_t>(total));
    pf_map_validate(canvas.map(), issues.data(), total);
    std::wstring text;
    for (const pf_issue& issue : issues) {
      text += Str(issue.severity == PF_SEVERITY_ERROR    ? IDS_SEVERITY_ERROR
                  : issue.severity == PF_SEVERITY_WARNING ? IDS_SEVERITY_WARNING
                                                          : IDS_SEVERITY_NOTE);
      text += FromUtf8(issue.message);
      text += L"\n";
    }
    MessageBoxW(main, text.c_str(), Str(IDS_CHECK_TITLE).c_str(),
                MB_OK | MB_ICONWARNING);
  }

  void RunPlaceStarts() {
    if (!canvas.map()) return;
    pf_map_checkpoint(canvas.map());
    const int placed = pf_map_place_start_locations(canvas.map());
    // An operation that did nothing must not consume an undo step.
    if (placed <= 0) pf_map_undo(canvas.map());
    else canvas.MarkMapChanged();
    OnStatus(Format(Plural(placed, IDS_STARTS_PLACED_ONE,
                              IDS_STARTS_PLACED_MANY), placed), false);
    OnMapEdited();
  }

  void RunRandomizeShades() {
    if (!canvas.map()) return;
    pf_map_checkpoint(canvas.map());
    const TileRect& box = editor.terrain_selection();
    const int changed = pf_map_randomize_shades(
        canvas.map(), box.x, box.y, box.w, box.h, GetTickCount());
    if (changed <= 0) pf_map_undo(canvas.map());
    else canvas.MarkMapChanged();
    OnMapEdited();
  }

  /// Say how many units a select-by-player found, naming the player. Silence
  /// reads as "nothing happened" whichever way it went, and zero is the answer
  /// worth being explicit about.
  void ReportSelection(int found, int owner) {
    const char* name = pf_player_name(owner);
    const std::wstring who = name ? FromUtf8(name) : Str(IDS_THAT_PLAYER);
    OnStatus(found == 0
                 ? Format(IDS_OWNS_NOTHING, who.c_str())
                 : Format(Plural(found, IDS_OWNED_BY_ONE, IDS_OWNED_BY_MANY),
                          found, who.c_str()),
             found == 0);
  }

  void GiveSelectionTo(int owner) {
    const char* name = pf_player_name(owner);
    const std::wstring who = name ? FromUtf8(name) : Str(IDS_THAT_PLAYER);
    if (editor.tool() == Tool::kPlace || !editor.HasSelection()) {
      // With the placement tool in hand, or with nothing selected, the same key
      // means "who is the next unit for" — the reading the units panel already
      // shows. Placing is the stronger signal of the two: somebody aiming the
      // next unit is saying which player it is for, not giving away whatever
      // they last clicked. With the filter on this also swaps the armed unit to
      // that player's race, which is the point of asking by player at all.
      editor.placing_owner = owner;
      OnStatus(Format(IDS_PLACING_FOR, who.c_str()), false);
      OnEditorChanged();
      return;
    }
    const int n = int(editor.selected().size());
    if (!editor.SetSelectedOwner(owner)) return;
    OnStatus(Format(Plural(n, IDS_GIVEN_TO_ONE, IDS_GIVEN_TO_MANY), n, who.c_str()),
             false);
    OnMapEdited();
  }

  /// Fill the two per-player submenus from the core's own names, so a colour
  /// is spelled out once rather than sixteen times in the .rc.
  void FillPlayerMenus() {
    // The eight playable slots. The other eight never have a start location, so
    // offering them would be eight entries that can only say "no start
    // location".
    if (HMENU starts = FindPopupContaining(GetMenu(main), IDM_VIEW_GOTO_START_FIRST)) {
      DeleteMenu(starts, IDM_VIEW_GOTO_START_FIRST, MF_BYCOMMAND);
      for (int i = 0; i < 8; i++) {
        const char* name = pf_player_name(i);
        AppendMenuW(starts, MF_STRING, UINT(IDM_VIEW_GOTO_START_FIRST + i),
                    (name ? FromUtf8(name) : Format(IDS_PLAYER_N, i + 1)).c_str());
      }
    }

    const UINT firsts[] = {IDM_SELECT_OWNER_FIRST, IDM_EDIT_OWNER_FIRST};
    for (UINT first : firsts) {
      HMENU popup = FindPopupContaining(GetMenu(main), first);
      if (!popup) continue;
      DeleteMenu(popup, first, MF_BYCOMMAND);
      // The command id carries the slot, so skipping the seven the game reads
      // nothing from leaves every other entry where it was.
      for (int i = 0; i < 16; i++) {
        if (!pf_player_is_supported(i)) continue;
        const char* name = pf_player_name(i);
        std::wstring label =
            name ? FromUtf8(name) : Format(IDS_PLAYER_N, i + 1);
        if (first == IDM_EDIT_OWNER_FIRST && i < 8) {
          label += L"\t" + std::to_wstring(i + 1);
        }
        AppendMenuW(popup, MF_STRING, first + UINT(i), label.c_str());
      }
    }
  }

  /// Fill the Units menu: every type the editor will place, by race and kind.
  ///
  /// Grouped by pf_unit_race and pf_unit_category rather than by this menu's
  /// own opinion, so it cannot come to disagree with the palette beside it
  /// about where a Dragon lives. Ours is only the order of the groups, which is
  /// PUDDraft's. The opt-in dozen are left out, as they are in the palette.
  void FillUnitsMenu() {
    HMENU units = FindPopupContaining(GetMenu(main), IDM_UNITS_FIRST);
    if (!units) return;
    DeleteMenu(units, IDM_UNITS_FIRST, MF_BYCOMMAND);

    // One submenu, or nothing at all when no unit belongs in it. An empty
    // submenu is a word you can point at that does nothing.
    auto group = [&](const std::wstring& heading, auto belongs) {
      HMENU sub = CreatePopupMenu();
      if (!sub) return;
      for (int id = 0; id < PF_UNIT_COUNT; id++) {
        if (pf_unit_is_unused(id) || pf_unit_never_offered(id) ||
            pf_unit_needs_opt_in(id)) {
          continue;
        }
        if (!belongs(pf_unit_race(id), pf_unit_category(id))) continue;
        const char* name = pf_unit_name(id);
        AppendMenuW(sub, MF_STRING, UINT(IDM_UNITS_FIRST + id),
                    (name ? FromUtf8(name) : std::to_wstring(id)).c_str());
      }
      if (GetMenuItemCount(sub) == 0) { DestroyMenu(sub); return; }
      AppendMenuW(units, MF_POPUP, reinterpret_cast<UINT_PTR>(sub),
                  heading.c_str());
    };

    // The menu's own wording, not the palette's: a palette heading is shouted
    // and a menu item is Title Case, and these are the same groups said in the
    // two different voices Strings.rc describes.
    static const struct { UINT name; char race; } kRaces[] = {
        {IDS_MENU_RACE_HUMAN, 'h'}, {IDS_MENU_RACE_ORC, 'o'}};
    static const struct { UINT name; int category; } kKinds[] = {
        {IDS_MENU_KIND_LAND, PF_CATEGORY_LAND},
        {IDS_MENU_KIND_AIR, PF_CATEGORY_AIR},
        {IDS_MENU_KIND_WATER, PF_CATEGORY_WATER},
        {IDS_MENU_KIND_BUILDINGS, PF_CATEGORY_BUILDING},
        {IDS_MENU_KIND_HEROES, PF_CATEGORY_HERO}};
    for (const auto& race : kRaces) {
      for (const auto& kind : kKinds) {
        group(Format(IDS_GROUP_HEADING, Str(race.name).c_str(),
                     Str(kind.name).c_str()),
              [&](char r, int c) {
                return r == race.race && c == kind.category;
              });
      }
    }
    AppendMenuW(units, MF_SEPARATOR, 0, nullptr);
    // Critters, mines, oil, the buildings you rescue.
    group(Str(IDS_MENU_GROUP_NEUTRAL), [](char r, int) { return r == 'n'; });
    // The start locations carry a race but are markers, so the runs above
    // deliberately did not take them.
    group(Str(IDS_MENU_GROUP_MARKERS),
          [](char r, int c) { return r != 'n' && c == PF_CATEGORY_SPECIAL; });
  }

  /// Show or hide the docks, the minimap and the status bar, then relayout.
  /// The canvas takes whatever is left, so hiding one is a real gain in room
  /// to work rather than a cosmetic change.
  void ShowFurniture() {
    const int docks = show_docks ? SW_SHOW : SW_HIDE;
    ShowWindow(terrain_panel.hwnd(), docks);
    ShowWindow(units_panel.hwnd(), docks);
    ShowWindow(minimap.hwnd(), show_docks && show_minimap ? SW_SHOW : SW_HIDE);
    ShowWindow(status, show_status ? SW_SHOW : SW_HIDE);
    toolbar.Show(show_toolbar);
    Layout();
  }

  /// A path's last component, for messages. A full path in a status line
  /// pushes out everything worth reading.
  static std::wstring NameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
  }

  // Importing and exporting a section moved to the property page that shows
  // that section (Dialogs.cpp, ImportSection / ExportSection). A .un file is
  // the Units page's business and nothing else's, and the Tools item never
  // said which of the three it was about to replace.

  /// Whether a player has a start location on the map.
  bool HasStartLocation(int player) const {
    if (!canvas.map()) return false;
    for (int i = 0; i < pf_map_unit_count(canvas.map()); i++) {
      pf_unit unit{};
      if (pf_map_unit(canvas.map(), i, &unit) != PF_OK) continue;
      if (unit.owner == player &&
          pf_unit_in_group(unit.type, PF_GROUP_START_LOCATIONS)) {
        return true;
      }
    }
    return false;
  }

  /// Scroll to a player's start location, and say where it is.
  ///
  /// A start location is a unit like any other in the format, so this is a
  /// search rather than a lookup — and a player may simply not have one.
  void GoToStart(int player) {
    if (!canvas.map()) return;
    for (int i = 0; i < pf_map_unit_count(canvas.map()); i++) {
      pf_unit unit{};
      if (pf_map_unit(canvas.map(), i, &unit) != PF_OK) continue;
      if (unit.owner != player ||
          !pf_unit_in_group(unit.type, PF_GROUP_START_LOCATIONS)) {
        continue;
      }
      canvas.CentreOn(unit.x, unit.y);
      const char* name = pf_player_name(player);
      OnStatus(Format(IDS_WENT_TO_START, name ? FromUtf8(name).c_str() : L"",
                      unit.x, unit.y), false);
      return;
    }
    const char* name = pf_player_name(player);
    OnStatus(Format(IDS_NO_START_FOR, name ? FromUtf8(name).c_str() : L""), true);
  }

  bool OnCommand(int id) {
    switch (id) {
      case IDM_FILE_NEW: NewMap(); return true;
      case IDM_FILE_GENERATE: GenerateMap(); return true;
      case IDM_FILE_ARCHIVE: {
        if (!ConfirmDiscard()) return true;
        std::wstring name, note;
        if (pf_map* made = ShowOpenFromArchive(main, instance, game, name, note)) {
          // A map read out of an archive has no file of its own, so Save must
          // ask where. AdoptMap with an empty path is exactly that.
          AdoptMap(made, L"");
          SetWindowTextW(
              main,
              Format(IDS_TITLE, name.c_str(), PF_APP_VERSION_WSTR).c_str());
        }
        if (!note.empty()) OnStatus(note, false);
        return true;
      }
      case IDM_FILE_OPEN: {
        if (!ConfirmDiscard()) return true;
        const std::wstring file = AskOpenPath(main);
        if (!file.empty()) OpenMap(file);
        return true;
      }
      case IDM_FILE_SAVE: Save(false); return true;
      case IDM_FILE_SAVE_AS: Save(true); return true;
      case IDM_FILE_EXIT: PostMessageW(main, WM_CLOSE, 0, 0); return true;

      case IDM_EDIT_UNDO:
        if (editor.Undo()) { canvas.MarkMapChanged(); OnMapEdited(); }
        return true;
      case IDM_EDIT_REDO:
        if (editor.Redo()) { canvas.MarkMapChanged(); OnMapEdited(); }
        return true;
      case IDM_EDIT_DELETE:
        if (editor.DeleteSelected()) OnMapEdited();
        return true;

      case IDM_EDIT_COPY:
      case IDM_EDIT_COPY_TERRAIN:
      case IDM_EDIT_COPY_UNITS:
      case IDM_EDIT_CUT: {
        const bool cutting = id == IDM_EDIT_CUT;
        // A copy takes terrain or units, never both. Plain Copy asks the mode,
        // because which half of the editor you are in is the same answer as
        // which half of the map you meant; cut is units by definition.
        const Editor::Grab what = id == IDM_EDIT_COPY_TERRAIN ? Editor::Grab::kTerrain
                                : id == IDM_EDIT_COPY_UNITS   ? Editor::Grab::kUnits
                                                              : Editor::Grab::kByMode;
        const int captured = cutting ? editor.Cut() : editor.Copy(what);
        if (captured < 0) {
          OnStatus(FromUtf8(editor.last_refusal), true);
          return true;
        }
        const TileRect frag = editor.ClipboardBounds();
        // What was taken decides what to say: a terrain fragment has no units
        // to count, and a unit fragment covers no ground.
        if (cutting) {
          OnStatus(Format(Plural(captured, IDS_CUT_ONE, IDS_CUT_MANY), captured), false);
          OnMapEdited();
          return true;
        }
        OnStatus(editor.ClipboardHasTerrain()
                     ? Format(IDS_COPIED_TERRAIN, frag.w, frag.h)
                     : Format(Plural(captured, IDS_COPIED_ONE, IDS_COPIED_MANY),
                              captured),
                 false);
        OnEditorChanged();
        return true;
      }

      case IDM_EDIT_PASTE:
        if (!editor.HasClipboard()) {
          OnStatus(Str(IDS_NOTHING_COPIED), true);
          return true;
        }
        // Arming rather than pasting: only the pointer knows where the fragment
        // lands, so the canvas shows it under the cursor until a click drops it.
        editor.BeginPaste();
        OnStatus(Str(IDS_PASTE_ARMED), false);
        OnEditorChanged();
        return true;

      case IDM_EDIT_PASTE_FLIP:
      case IDM_EDIT_PASTE_MIRROR:
      case IDM_EDIT_PASTE_ROTATE: {
        const bool turned = id == IDM_EDIT_PASTE_FLIP   ? editor.FlipClipboard()
                          : id == IDM_EDIT_PASTE_MIRROR ? editor.MirrorClipboard()
                                                        : editor.RotateClipboard(1);
        if (!turned) {
          OnStatus(Str(IDS_NOTHING_COPIED), true);
          return true;
        }
        // Turning is free until the paste lands, so show the new shape and
        // leave the paste armed.
        const TileRect frag = editor.ClipboardBounds();
        OnStatus(Format(IDS_FRAGMENT_SIZE, frag.w, frag.h), false);
        OnEditorChanged();
        return true;
      }

      case IDM_OPT_FIT_PASTE:
        editor.fit_pasted_edges = !editor.fit_pasted_edges;
        return true;

      case IDM_VIEW_MODE_TERRAIN:
      case IDM_VIEW_MODE_UNITS:
        editor.SetMode(id == IDM_VIEW_MODE_TERRAIN ? Mode::kTerrain : Mode::kUnit);
        OnEditorChanged();
        return true;

      case IDM_VIEW_GRID:
        editor.show_grid = !editor.show_grid;
        canvas.MarkMapChanged();
        return true;

      case IDM_VIEW_ZOOM_FIT: canvas.Fit(); return true;
      case IDM_VIEW_ZOOM_IN: canvas.ZoomStep(1); return true;
      case IDM_VIEW_ZOOM_OUT: canvas.ZoomStep(-1); return true;
      case IDM_VIEW_ZOOM_50: canvas.ZoomTo(50); return true;
      case IDM_VIEW_ZOOM_100: canvas.ZoomTo(100); return true;
      case IDM_VIEW_ZOOM_200: canvas.ZoomTo(200); return true;
      case IDM_VIEW_ZOOM_400: canvas.ZoomTo(400); return true;

      case IDM_MAP_PLACE_STARTS: RunPlaceStarts(); return true;
      case IDM_MAP_RANDOM_SHADES: RunRandomizeShades(); return true;
      case IDM_MAP_VALIDATE: RunValidate(); return true;


      // The two select tools are toggles: pressing the one already in hand puts
      // it down, which is what a pushed-in button being pressed again means
      // everywhere else.
      case IDM_TOOL_TERRAIN_SELECT:
      case IDM_TOOL_UNIT_SELECT: {
        const Tool want =
            id == IDM_TOOL_TERRAIN_SELECT ? Tool::kRect : Tool::kSelect;
        if (editor.tool() == want) {
          // Back to what the mode draws with, so the strip is never left with
          // nothing pushed in and no tool a click on the map obeys.
          editor.SetTool(want == Tool::kRect ? Tool::kPaint : Tool::kPlace);
        } else {
          editor.SetTool(want);
        }
        OnEditorChanged();
        return true;
      }

      case IDM_SELECT_ALL:
        editor.SetMode(Mode::kUnit);
        editor.SelectAll();
        OnEditorChanged();
        return true;
      case IDM_SELECT_NONE:
        // Esc is this command's accelerator, and whatever tool is in hand is
        // the loudest thing it can be asked to put down.
        if (canvas.LeaveActiveTool()) return true;
        // Esc clears whichever selection exists, unit first.
        if (editor.HasSelection()) editor.ClearSelection();
        else editor.ClearTerrainSelection();
        OnEditorChanged();
        return true;
      case IDM_SELECT_INVERT:
        editor.InvertSelection();
        OnEditorChanged();
        return true;
      case IDM_SELECT_SAME_TYPE:
        editor.SelectSameType();
        OnEditorChanged();
        return true;
      case IDM_SELECT_SAME_OWNER:
        editor.SelectSameOwner();
        OnEditorChanged();
        return true;

      case IDM_EDIT_FILL: {
        const int painted = editor.FillTerrainSelection();
        OnStatus(painted > 0 ? Format(IDS_TILES_FILLED, painted)
                             : Str(IDS_FILL_NEEDS_RECT),
                 painted == 0);
        OnEditorChanged();
        return true;
      }

      case IDM_VIEW_WATER:
        canvas.SetWaterAnimated(!canvas.water_animated());
        return true;

      case IDM_EDIT_BRUSH_SMALLER:
      case IDM_EDIT_BRUSH_BIGGER: {
        if (editor.mode() != Mode::kTerrain) return true;
        const int size = editor.StepBrushSize(id == IDM_EDIT_BRUSH_BIGGER ? 1 : -1);
        if (size < 0) return true;
        OnEditorChanged();
        OnStatus(size == PF_BRUSH_SIZE_CORNER ? Str(IDS_BRUSH_SIZED_CORNER)
                                              : Format(IDS_BRUSH_SIZED, size, size),
                 false);
        return true;
      }

      case IDM_VIEW_TERRAIN_RING: {
        POINT at{};
        GetCursorPos(&at);
        const int tileset = canvas.map() ? pf_map_tileset(canvas.map()) : 0;
        const int brush = ShowTerrainRing(main, instance, art, tileset,
                                          editor.brush_index, at);
        if (brush < 0) return true;
        // Through SetBrush, so a dark wedge comes back as its terrain with the
        // shade switch thrown rather than as a brush the palette hides.
        editor.SetBrush(brush);
        // Picking what to paint with means painting, the same way picking a
        // unit means placing one.
        editor.SetTool(Tool::kPaint);
        OnEditorChanged();
        OnStatus(FromUtf8(editor.BrushName()), false);
        return true;
      }

      case IDM_VIEW_QUICK_PICK: {
        // Led by the chosen player's race, so an empty query lists the units in
        // the order the dock has them rather than a second order of its own.
        const int race = canvas.map()
                             ? pf_map_race(canvas.map(), editor.placing_owner)
                             : int(PF_RACE_HUMAN);
        const int unit = ShowQuickPick(main, instance, &icons,
                                       race == PF_RACE_ORC ? 'o' : 'h');
        if (unit < 0) return true;
        // Choosing a unit means placing one, so the tool follows rather than
        // leaving the choice armed behind whatever mode was on screen.
        editor.placing_type = unit;
        editor.SetTool(Tool::kPlace);
        OnEditorChanged();
        const char* name = pf_unit_name(unit);
        OnStatus(name ? FromUtf8(name) : L"", false);
        return true;
      }

      // The six sheets about the map are one window with six tabs. Each menu
      // item opens the window on "its" tab, so the menu says what it always did.
      case IDM_MAP_PROPERTIES: OpenMapSheets(kMapSheetMap); return true;
      case IDM_MAP_PLAYERS: OpenMapSheets(kMapSheetPlayers); return true;
      case IDM_MAP_UNIT_DATA: OpenMapSheets(kMapSheetUnits); return true;
      case IDM_MAP_UPGRADES: OpenMapSheets(kMapSheetUpgrades); return true;
      case IDM_MAP_RESTRICTIONS: OpenMapSheets(kMapSheetRestrictions); return true;
      case IDM_MAP_STATS: OpenMapSheets(kMapSheetStatistics); return true;

      case IDM_TOOLS_GAME_FOLDER:
        // Not required here: there is already a folder in use, so closing this
        // means keeping it rather than leaving the editor with nothing.
        if (ShowGameSetup(main, instance, game, false)) {
          LoadArtwork();
          // A different folder can be a different language, and this menu is the
          // one place names are written down rather than asked for each time.
          FillUnitsMenu();
          units_panel.RebuildPalette();
          OnEditorChanged();
        }
        return true;

      case IDM_TOOLS_OPTIONS: {
        bool reset = false;
        const bool changed = ShowOptions(main, instance, editor, &unit_art,
                                         &vary_facing, &unit_sounds, &reset);
        // The running window keeps the layout it has — moving everything under
        // somebody mid-edit is worse than waiting for the restart the dialog
        // told them about. What must not happen is writing it all back out on
        // the way out. The recent list is the exception: it is a menu, so an
        // empty one is the only honest thing to show once it has been forgotten.
        if (reset) {
          recent.clear();
          RefreshRecentMenu();
        }
        if (changed) {
          sounds.SetEnabled(unit_sounds);
          // Both display options change what a unit *looks like* rather than
          // anything about the map, so nothing here marks it dirty.
          icons.SetPreferSprites(unit_art == 1);
          // The palette holds no icons of its own — it reads the cache — but it
          // does need telling that what the cache answers has changed.
          units_panel.SetArtwork(&icons, art, pf_map_tileset(canvas.map()));
          canvas.SetVaryFacing(vary_facing);
          canvas.MarkMapChanged();
          OnEditorChanged();
        }
        return true;
      }

      case IDM_TOOLS_CONVERT: {
        std::wstring note;
        const bool changed = ShowConvertUnits(main, instance, editor, &icons, note);
        if (changed) OnMapEdited();
        if (!note.empty()) OnStatus(note, !changed);
        return true;
      }

      case IDM_TOOLS_EXPORT_PNG: {
        std::wstring note;
        const bool wrote = ShowExportPng(main, instance, editor, art, sprites, note);
        if (!note.empty()) OnStatus(note, !wrote);
        return true;
      }

      case IDM_TOOLS_MOVEMENT: {
        std::wstring note;
        if (ShowMovement(main, instance, editor, note)) {
          canvas.MarkMapChanged();
          OnMapEdited();
        }
        if (!note.empty()) OnStatus(note, false);
        return true;
      }

      case IDM_TOOLS_AI_SCRIPTS:
        ShowAiScripts(main, instance, game);
        return true;

      case IDM_TOOLS_LOG: ShowLog(main, instance); return true;

      case IDM_EDIT_DUPLICATE: {
        // The context menu selects the unit it was opened over before it draws
        // itself, so "the unit named in the item" and "the unit Ctrl+D means"
        // are the same unit either way. The tool follows the choice, because
        // the mode may well have been terrain when the menu was raised.
        if (editor.PickUnitTypeOf(editor.SelectedUnit()) >= 0) {
          editor.SetMode(Mode::kUnit);
          editor.SetTool(Tool::kPlace);
          const char* name = pf_unit_name(editor.placing_type);
          if (name) OnStatus(FromUtf8(name), false);
        }
        OnEditorChanged();
        return true;
      }

      case IDM_CONTEXT_SAME_TYPE:
        editor.SelectSameType();
        OnEditorChanged();
        return true;

      case IDM_CONTEXT_INSPECT: {
        const int index = editor.SelectedUnit();
        if (index >= 0) { OnInspectUnit(index); return true; }
        // Enter reaches this from anywhere on the canvas, where the context
        // menu only offered it over a unit, so say why nothing opened.
        OnStatus(Str(editor.HasSelection() ? IDS_INSPECT_ONE_AT_A_TIME
                                           : IDS_INSPECT_NEEDS_UNIT),
                 true);
        return true;
      }

      case IDM_VIEW_ZOOM_SELECTION: {
        // The unit selection first, then the terrain rectangle: both are "what
        // I am working on", and only one of them is usually set.
        TileRect box = editor.SelectionBounds();
        if (box.empty()) box = editor.terrain_selection();
        if (box.empty()) { OnStatus(Str(IDS_NO_SELECTION_TO_ZOOM), true); return true; }
        canvas.ZoomToTiles(box.x, box.y, box.w, box.h);
        return true;
      }

      case IDM_VIEW_REACH:
        canvas.SetShowReach(!canvas.show_reach());
        return true;

      case IDM_VIEW_TOOLBAR:
        show_toolbar = !show_toolbar;
        ShowFurniture();
        return true;
      case IDM_VIEW_TOOLBARS:
        show_docks = !show_docks;
        ShowFurniture();
        return true;
      case IDM_VIEW_STATUSBAR:
        show_status = !show_status;
        ShowFurniture();
        return true;
      case IDM_VIEW_MINIMAP:
        show_minimap = !show_minimap;
        ShowFurniture();
        return true;

      case IDM_HELP_GUIDE:
        if (!OpenUserGuide(main)) OnStatus(Str(IDS_GUIDE_FAILED), true);
        return true;

      case IDM_HELP_ABOUT: {
        MessageBoxW(main, Format(IDS_ABOUT_BODY, PF_APP_VERSION_WSTR).c_str(),
                    Str(IDS_ABOUT_TITLE).c_str(), MB_OK);
        return true;
      }

      default:
        if (id >= IDM_FILE_RECENT_FIRST && id <= IDM_FILE_RECENT_LAST) {
          const int index = id - IDM_FILE_RECENT_FIRST;
          if (index < int(recent.size()) && ConfirmDiscard()) {
            OpenMap(recent[size_t(index)]);
          }
          return true;
        }
        if (id >= IDM_SELECT_KIND_FIRST && id <= IDM_SELECT_KIND_LAST) {
          editor.SetMode(Mode::kUnit);
          editor.SelectGroup(id - IDM_SELECT_KIND_FIRST, false);
          OnEditorChanged();
          return true;
        }
        if (id >= IDM_SELECT_OWNER_FIRST && id <= IDM_SELECT_OWNER_LAST) {
          editor.SetMode(Mode::kUnit);
          const int owner = id - IDM_SELECT_OWNER_FIRST;
          const int n = editor.SelectOwner(owner, false);
          ReportSelection(n, owner);
          OnEditorChanged();
          return true;
        }
        if (id >= IDM_EDIT_OWNER_FIRST && id <= IDM_EDIT_OWNER_LAST) {
          GiveSelectionTo(id - IDM_EDIT_OWNER_FIRST);
          return true;
        }
        if (id >= IDM_VIEW_GOTO_START_FIRST && id <= IDM_VIEW_GOTO_START_LAST) {
          GoToStart(id - IDM_VIEW_GOTO_START_FIRST);
          return true;
        }
        if (id >= IDM_UNITS_FIRST && id <= IDM_UNITS_LAST) {
          // The item is the unit, so choosing one arms the tool and the next
          // click on the map says where — which is what the old Add-a-unit box
          // made you type in.
          editor.placing_type = id - IDM_UNITS_FIRST;
          editor.SetMode(Mode::kUnit);
          editor.SetTool(Tool::kPlace);
          OnEditorChanged();
          const char* name = pf_unit_name(editor.placing_type);
          if (name) OnStatus(FromUtf8(name), false);
          return true;
        }
        return false;
    }
  }

  /// Ticks and enables, asked for the moment a menu drops.
  void OnMenuOpening() {
    HMENU menu = GetMenu(main);
    auto check = [&](int id, bool on) {
      CheckMenuItem(menu, UINT(id), on ? MF_CHECKED : MF_UNCHECKED);
    };
    auto enable = [&](int id, bool on) {
      EnableMenuItem(menu, UINT(id), on ? MF_ENABLED : MF_GRAYED);
    };

    // The strip shadows six of these, and this is the moment the two are side
    // by side on screen and any disagreement would show.
    RefreshCommands();

    enable(IDM_EDIT_UNDO, editor.CanUndo());
    enable(IDM_EDIT_REDO, editor.CanRedo());
    enable(IDM_EDIT_DELETE, editor.HasSelection());
    // One unit, not a selection: duplicating means "another one of this", and
    // with three different things selected there is no this.
    enable(IDM_EDIT_DUPLICATE, editor.SelectedUnit() >= 0);
    // Copy needs something to copy from; paste needs something copied.
    const bool copyable = !editor.terrain_selection().empty() ||
                          editor.HasSelection();
    enable(IDM_EDIT_COPY, copyable);
    enable(IDM_EDIT_CUT, copyable);
    enable(IDM_EDIT_PASTE, editor.HasClipboard());
    // Turning needs an armed paste, not just a full clipboard: the fragment
    // under the pointer is the only thing that shows which way round it is.
    enable(IDM_EDIT_PASTE_FLIP, editor.pasting());
    enable(IDM_EDIT_PASTE_MIRROR, editor.pasting());
    enable(IDM_EDIT_PASTE_ROTATE, editor.pasting());
    enable(IDM_EDIT_FILL, !editor.terrain_selection().empty());
    // Enabled on a map rather than on a selection, because these do something
    // either way: with units selected they hand those over, and with nothing
    // selected they say who the next unit placed is for.
    for (int i = 0; i < 16; i++) {
      enable(IDM_EDIT_OWNER_FIRST + i, canvas.map() != nullptr);
    }

    check(IDM_VIEW_MODE_TERRAIN, editor.mode() == Mode::kTerrain);
    check(IDM_VIEW_MODE_UNITS, editor.mode() == Mode::kUnit);
    // The two select tools are toggles and the strip shows them pushed in, so
    // their menu items have to say the same thing.
    check(IDM_TOOL_TERRAIN_SELECT, editor.tool() == Tool::kRect);
    check(IDM_TOOL_UNIT_SELECT, editor.tool() == Tool::kSelect);
    for (int i = 0; i < 5; i++) {
      check(IDM_VIEW_UNITS_ALL + i, editor.unit_filter == i);
    }
    for (int i = 0; i < 4; i++) {
      check(IDM_VIEW_LAYER_ART + i, editor.overlay == i);
    }
    check(IDM_VIEW_GRID, editor.show_grid);

    check(IDM_VIEW_WATER, canvas.water_animated());
    check(IDM_VIEW_REACH, canvas.show_reach());
    check(IDM_VIEW_TOOLBAR, show_toolbar);
    check(IDM_VIEW_TOOLBARS, show_docks);
    check(IDM_VIEW_STATUSBAR, show_status);
    check(IDM_VIEW_MINIMAP, show_minimap);
    enable(IDM_VIEW_MINIMAP, show_docks);
    enable(IDM_VIEW_ZOOM_SELECTION,
           editor.HasSelection() || !editor.terrain_selection().empty());
    // The rest of the options are in the Options dialog. This one stays a menu
    // item because it is a property of the paste about to happen, and the Edit
    // menu is where you already are when you think about it.
    check(IDM_OPT_FIT_PASTE, editor.fit_pasted_edges);

    // Only a player with a start location can be gone to.
    for (int i = 0; i < 8; i++) {
      enable(IDM_VIEW_GOTO_START_FIRST + i, HasStartLocation(i));
    }

    enable(IDM_MAP_PROPERTIES, canvas.map() != nullptr);
    enable(IDM_MAP_PLAYERS, canvas.map() != nullptr);
    enable(IDM_MAP_UNIT_DATA, canvas.map() != nullptr);
    enable(IDM_MAP_UPGRADES, canvas.map() != nullptr);
    // Not greyed when the section is absent: the sheet says so, which is more
    // use than a menu item that cannot be clicked and will not say why.
    enable(IDM_MAP_RESTRICTIONS, canvas.map() != nullptr);
  }

  /// The View menu's radio groups act on pick; they need their own handler
  /// because the id runs are contiguous with state.
  bool OnViewRadio(int id) {
    if (id >= IDM_VIEW_UNITS_ALL && id <= IDM_VIEW_UNITS_BUILDINGS) {
      editor.unit_filter = id - IDM_VIEW_UNITS_ALL;
      canvas.MarkMapChanged();
      return true;
    }
    if (id >= IDM_VIEW_LAYER_ART && id <= IDM_VIEW_LAYER_TILES) {
      editor.overlay = id - IDM_VIEW_LAYER_ART;
      canvas.MarkMapChanged();
      return true;
    }
    return false;
  }

  /// Whether anything is docked to a side, so an empty one can collapse.
  bool SideHasSomething(bool right) const {
    return terrain_right == right || units_right == right ||
           (show_minimap && minimap_right == right);
  }

  /// Place whatever is on one side, top to bottom, into a column.
  ///
  /// The minimap is always at the top: it is the one thing here that is a
  /// picture rather than a set of controls, and it wants to be glanceable.
  ///
  /// The two panels split what is left evenly rather than each taking what it
  /// asks for, because only one of them can answer: the terrain panel has a
  /// natural height and a hundred and ten units scroll at any.
  void StackSide(HDWP& defer, UINT flags, bool right, int x, int top, int width,
                 int height) {
    if (width <= 0) return;
    const int bottom = top + height;
    int y = top;
    if (show_minimap && minimap_right == right) {
      // Square by default, since the map it draws usually is. Sharing a side
      // with both panels it gives way to a third of the column, or the two
      // palettes below it end up with a few rows each.
      int mini_h = width;
      if (SideCount(right) >= 2) mini_h = std::min(mini_h, height / 3);
      mini_h = std::min(mini_h, height);
      defer = DeferWindowPos(defer, minimap.hwnd(), nullptr, x, y, width,
                             mini_h, flags);
      y += mini_h;
    }
    const int panels = (terrain_right == right ? 1 : 0) + (units_right == right ? 1 : 0);
    if (panels == 0) return;
    const int each = std::max(0, (bottom - y) / panels);
    if (terrain_right == right) {
      // The last one takes the remainder, so an odd number of pixels does not
      // show up as a gap at the bottom of the column.
      const int h = units_right == right ? each : bottom - y;
      defer = DeferWindowPos(defer, terrain_panel.hwnd(), nullptr, x, y, width,
                             h, flags);
      y += h;
    }
    if (units_right == right) {
      defer = DeferWindowPos(defer, units_panel.hwnd(), nullptr, x, y, width,
                             std::max(0, bottom - y), flags);
    }
  }

  /// How many of the two panels are on a side.
  int SideCount(bool right) const {
    return (terrain_right == right ? 1 : 0) + (units_right == right ? 1 : 0);
  }

  void Layout() {
    RECT rc;
    GetClientRect(main, &rc);
    // The status bar sizes itself; ask it to, then take what it left.
    //
    // Only when its own geometry can have moved, which is the window's width
    // and its DPI. Dragging a dock's seam calls Layout on every mouse move
    // without either changing, and re-parting the bar there repainted five
    // cells for a result identical to what was on screen.
    const UINT dpi = GetDpiForWindow(main);
    if (status && (rc.right != status_width_ || dpi != status_dpi_)) {
      status_width_ = rc.right;
      status_dpi_ = dpi;
      SendMessageW(status, WM_SIZE, 0, 0);
      RECT status_rc;
      GetWindowRect(status, &status_rc);
      status_height_ = status_rc.bottom - status_rc.top;
      PartStatus(rc.right);
    }
    const int status_h = show_status ? status_height_ : 0;
    // The strip is carved off the top the way the status bar is off the bottom.
    // Clamped at zero because a window dragged shorter than its own furniture
    // would hand the canvas a negative height.
    const int toolbar_h = toolbar.Height();
    const int height = std::max(0, int(rc.bottom) - status_h - toolbar_h);

    // A hidden dock gives its width to the canvas rather than leaving a hole,
    // and so does a side with nothing on it.
    const int split = Scaled(main, kSplitDip);
    const bool left_used = show_docks && SideHasSomething(false);
    const bool right_used = show_docks && SideHasSomething(true);
    const int left_w = left_used ? Scaled(main, ClampedDock(left_dip)) : 0;
    const int right_w = right_used ? Scaled(main, ClampedDock(right_dip)) : 0;
    const int left_gap = left_used ? split : 0;
    const int right_gap = right_used ? split : 0;

    // All six in one pass. Moved one at a time, each MoveWindow repaints before
    // the next has been placed, so a seam drag showed three states per mouse
    // move; deferred, the frame changes once.
    if (HDWP defer = BeginDeferWindowPos(6)) {
      const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
      toolbar.Place(defer, flags, rc.right);
      if (show_docks) {
        StackSide(defer, flags, false, 0, toolbar_h, left_w, height);
        StackSide(defer, flags, true, rc.right - right_w, toolbar_h, right_w,
                  height);
      }
      // The gaps either side of the canvas belong to the main window, which is
      // what makes them grabbable and what draws them as a seam.
      defer = DeferWindowPos(
          defer, canvas.hwnd(), nullptr, left_w + left_gap, toolbar_h,
          std::max(0, int(rc.right) - left_w - right_w - left_gap - right_gap),
          height, flags);
      EndDeferWindowPos(defer);
    }
  }

  /// Status parts: a left group, a right group, and the slack between them.
  ///
  /// The pointer's tile leads, because it is the cell read most often and the
  /// far left is where the eye returns; the tool and its tips follow it, and
  /// the three readouts are pinned right.
  ///
  /// The slack goes to the message, which sits between the two groups. That is
  /// the cell whose content is a sentence rather than a number, and putting it
  /// after the fixed left group means the space grows away from the tips
  /// instead of pushing them into the middle of the bar.
  ///
  /// On a narrow window everything gives way together: fixed cells took half of
  /// a 700-pixel bar and left the sentence clipped mid-word.
  void PartStatus(int width) {
    // Wide enough for the game's own refusals, which are whole sentences where
    // the editor's are fragments. At 220 they clipped mid-word, and a refusal
    // you cannot read is the one message on the bar that had to be readable.
    int hover = Scaled(main, 118);      // "128, 128  Shore, mostly land"
    int tool = Scaled(main, 110);       // "Selecting tiles"
    int hint = Scaled(main, 340);       // "Shift to draw straight  Alt for..."
    int message = Scaled(main, 300);    // "Opened All You Need BNE.pud"
    int right[3] = {Scaled(main, 128),  // "12 units in 20 x 20"
                    Scaled(main, 56),   // "400%"
                    Scaled(main, 80)};  // "128 x 128"

    int total = hover + tool + hint + message;
    for (int one : right) total += one;
    const int most = width * 15 / 16;
    if (total > most && total > 0) {
      hover = hover * most / total;
      tool = tool * most / total;
      hint = hint * most / total;
      message = message * most / total;
      for (int& one : right) one = one * most / total;
    }

    // widths[i] is part i's *right* edge. The left group runs forward from
    // zero; the right group is placed by working back from the far edge; the
    // message stretches to meet it, and never shrinks below what it asked for.
    int widths[kCellCount];
    widths[kCellHover] = hover;
    widths[kCellTool] = widths[kCellHover] + tool;
    widths[kCellHint] = widths[kCellTool] + hint;
    // Backwards from the far edge, so each readout keeps its own width and the
    // rounding lands in the slack rather than on a number.
    int edge = width;
    widths[kCellSize] = edge;       edge -= right[2];
    widths[kCellZoom] = edge;       edge -= right[1];
    widths[kCellSelection] = edge;  edge -= right[0];
    widths[kCellMessage] = std::max(widths[kCellHint] + message, edge);
    SendMessageW(status, SB_SETPARTS, kCellCount, reinterpret_cast<LPARAM>(widths));
    // Re-parting a status bar empties every cell, so whatever they were saying
    // has to be said again.
    shown_hint_ = 0;
    RefreshStatusCells();
    RefreshHint();
  }

  /// What the status bar was last sized and parted for. Its geometry depends
  /// on these two and on nothing else, so anything else that calls Layout can
  /// leave it alone.
  int status_width_ = -1;
  UINT status_dpi_ = 0;
  int status_height_ = 0;
  /// Which tip the hint cell is showing, so it is only written when the tool
  /// actually changes. OnEditorChanged runs after every edit.
  UINT shown_hint_ = 0;
};

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE: {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      break;
    }

    case WM_SIZE:
      if (app) app->Layout();
      return 0;

    // The main window shows through only in the two seams beside the canvas;
    // everything else it owns is covered by a child.
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      if (app) app->PaintSplits(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }

    // The message cell of the status bar opens the log, so it points.
    case WM_SETCURSOR:
      if (app && app->status &&
          reinterpret_cast<HWND>(wparam) == app->status &&
          app->OverStatusMessage()) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      if (app && LOWORD(lparam) == HTCLIENT) {
        POINT at{};
        GetCursorPos(&at);
        ScreenToClient(hwnd, &at);
        if (app->dragging_split >= 0 || app->SplitAt(at.x, at.y) >= 0) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
          return TRUE;
        }
      }
      break;

    case WM_LBUTTONDOWN:
      if (app) {
        const int which = app->SplitAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (which >= 0) {
          app->dragging_split = which;
          SetCapture(hwnd);
          return 0;
        }
      }
      break;

    case WM_MOUSEMOVE:
      if (app && app->dragging_split >= 0) {
        app->DragSplit(app->dragging_split, GET_X_LPARAM(lparam));
        return 0;
      }
      break;

    case WM_LBUTTONUP:
      if (app && app->dragging_split >= 0) {
        app->dragging_split = -1;
        ReleaseCapture();
        return 0;
      }
      break;

    // A double-click on a seam puts that dock back where it started, which is
    // the way out of having dragged one to a width that hides its contents.
    case WM_LBUTTONDBLCLK:
      if (app) {
        const int which = app->SplitAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (which == 0) { app->left_dip = kTerrainDockDip; app->Layout(); return 0; }
        if (which == 1) { app->right_dip = kUnitsDockDip; app->Layout(); return 0; }
      }
      break;

    case WM_DPICHANGED: {
      auto* rect = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                   rect->right - rect->left, rect->bottom - rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }

    case WM_COMMAND:
      if (!app) break;
      // The zoom dropdown is a control rather than a command: it arrives with
      // a notification code and the control's handle, which is how it is told
      // apart from the buttons beside it.
      if (const int zoom = app->toolbar.ZoomChosen(wparam, lparam)) {
        app->canvas.ZoomTo(zoom);
        // Otherwise the keyboard is left in the dropdown, where the arrows
        // change the zoom again and every other key does nothing.
        SetFocus(app->canvas.hwnd());
        return 0;
      }
      if (app->OnCommand(LOWORD(wparam)) || app->OnViewRadio(LOWORD(wparam))) {
        return 0;
      }
      break;

    case WM_INITMENUPOPUP:
      if (app) app->OnMenuOpening();
      return 0;

    case WM_DROPFILES: {
      if (!app) break;
      auto drop = reinterpret_cast<HDROP>(wparam);
      wchar_t path[MAX_PATH] = {};
      if (DragQueryFileW(drop, 0, path, MAX_PATH) && app->ConfirmDiscard()) {
        app->OpenMap(path);
      }
      DragFinish(drop);
      return 0;
    }

    case WM_CLOSE:
      if (app && !app->ConfirmDiscard()) return 0;
      DestroyWindow(hwnd);
      return 0;

    // A status bar tells its parent when it is clicked. The message cell is
    // the one that scrolls away, so clicking it opens the log that kept it.
    case WM_NOTIFY: {
      auto* head = reinterpret_cast<NMHDR*>(lparam);
      // The strip's tooltip asks its parent for the text, which is here.
      if (app && head && head->code == TTN_GETDISPINFOW &&
          app->toolbar.TooltipWanted(lparam)) {
        return 0;
      }
      if (app && head && head->hwndFrom == app->status && head->code == NM_CLICK) {
        auto* mouse = reinterpret_cast<NMMOUSE*>(lparam);
        if (mouse->dwItemSpec == kCellMessage) {
          ShowLog(hwnd, app->instance);
          return 0;
        }
      }
      break;
    }

    case WM_DESTROY:
      // Here rather than in WM_CLOSE: the window can also be destroyed by the
      // session ending, and a setting only kept when you shut down the polite
      // way is a setting that looks unreliable. The HWND is still valid here.
      if (!SettingsWereReset()) SavePlacement(hwnd);
      if (app) app->SaveSettings();
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
  // A headless capture runs and exits before anything is created — no window,
  // no message loop, no window server — which is what makes map rendering
  // checkable from a terminal, over SSH and in CI.
  {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && WantsCapture(argc, argv)) {
      const int code = RunCapture(argc, argv);
      LocalFree(argv);
      return code;
    }
    if (argv) LocalFree(argv);
  }

  // Per-monitor v2, or the map is blurry on every laptop sold in eight years.
  // The manifest already says so; this is for a debugger-launched build that
  // side-steps it.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  INITCOMMONCONTROLSEX icc = {sizeof(icc),
                              ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES |
                                  ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);

  App app;
  app.instance = instance;

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  // CS_DBLCLKS so a double-click on a dock's seam arrives as one: without it
  // the second click is another WM_LBUTTONDOWN and the dock never resets.
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = MainProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = kAppClass;
  wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  if (!RegisterClassExW(&wc)) return 1;
  if (!MapWindow::Register(instance)) return 1;
  if (!PaletteGrid::Register(instance)) return 1;
  if (!Form::Register(instance)) return 1;
  if (!TerrainPanel::Register(instance)) return 1;
  if (!UnitsPanel::Register(instance)) return 1;
  if (!Minimap::Register(instance)) return 1;
  if (!RegisterTerrainRing(instance)) return 1;

  // WS_CLIPCHILDREN: everything here except the two seams is covered by a
  // child, and without it the class background brush repaints the whole client
  // area before those children draw again. Dragging a seam did that on every
  // mouse move.
  app.main = CreateWindowExW(WS_EX_ACCEPTFILES, kAppClass, kAppTitleVersioned,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             1280, 860, nullptr, nullptr, instance, &app);
  if (!app.main) return 1;

  // Cut up before the strip and the docks are built, because both ask for their
  // drawings as they create their buttons. Sixteen pixels scaled by the window's
  // DPI, so at 200% each sheet pixel becomes exactly four rather than mush. A
  // sheet that fails to load is not fatal — every button has a caption.
  app.ui_icons.Load(instance, MulDiv(16, int(GetDpiForWindow(app.main)), 96));

  CreateWindowExW(0, MapWindow::kClassName, nullptr, WS_CHILD | WS_VISIBLE, 0,
                  0, 0, 0, app.main, nullptr, instance, &app.canvas);
  app.canvas.SetEditor(&app.editor, &app);
  app.canvas.SetIcons(&app.icons);
  app.terrain_panel.Create(app.main, instance, &app.editor, &app);
  app.units_panel.Create(app.main, instance, &app.editor, &app);
  // After Create, because the buttons have to exist to be decorated.
  app.terrain_panel.SetUiIcons(&app.ui_icons);
  app.units_panel.SetUiIcons(&app.ui_icons);
  app.minimap.Create(app.main, instance, IDC_MINIMAP, &app);
  app.status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                               WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0,
                               0, app.main,
                               reinterpret_cast<HMENU>(INT_PTR(IDC_STATUS_BAR)),
                               instance, nullptr);
  app.toolbar.Create(app.main, instance, &app.ui_icons);
  // The strip is created visible, so a run that last hid it has to be told.
  app.toolbar.Show(app.show_toolbar);
  app.RefreshRecentMenu();
  app.FillPlayerMenus();

  // Where Warcraft II is. The game is a requirement, not a preference. Asked on the first run even
  // when Restore found something — two products can be installed at once and
  // they carry different artwork, so a new install confirms which one, with the
  // found folder already selected — and asked again on any later run where
  // nothing could be restored, because that is an install that has moved or
  // gone rather than a question already answered.
  //
  // Closed rather than answered, or answered with a folder holding no game, the
  // client closes. Running on flat colours was supportable and was supported;
  // what it was not was testable — every artwork path had a second behaviour
  // underneath it that nobody ran, so bugs lived there.
  const bool first_run = LoadSetting(L"SetupSeen", 0) == 0;
  app.game.Restore();
  if (first_run || !app.game.ready()) {
    // On the answer and not on `ready()`: Restore ends in guesses, so by the
    // time the question is asked it has usually adopted something already, and
    // testing what it found would make closing the dialog close a dialog and
    // nothing else — which is what Skip did, drawing the artwork it had just
    // declined.
    if (!ShowGameSetup(app.main, instance, app.game, true)) {
      // Silently: the note under the list said what closing the window does, and
      // a dialog explaining the action just taken is one to dismiss twice.
      CoUninitialize();
      return 0;
    }
  }
  // Written only once there is a folder in hand: a run that ended at the dialog
  // answered nothing, and must ask again rather than start silently next time.
  SaveSetting(L"SetupSeen", 1);

  // After the game folder, not before: adopting it installs the game's own
  // string table, and this menu writes a hundred unit names into a resource once
  // and never looks at them again. Built first, it was the one place still
  // saying "Archer" while everything else said "Elven Archer".
  app.FillUnitsMenu();

  // A path on the command line, so the exe can be a file association and so
  // `PUDForge.exe map.pud` is the first thing that ever works.
  int argc = 0;
  bool opened = false;
  if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
    if (argc > 1) opened = app.OpenMap(argv[1]);
    LocalFree(argv);
  }
  if (!opened) {
    pf_status status_code = PF_OK;
    app.AdoptMap(pf_map_create(64, 64, PF_TILESET_FOREST, &status_code), L"");
  }

  // The size and the settings the last run was left with. Restored after the
  // map is open so the toggles land on a window that has something in it.
  app.RestoreSettings();
  app.terrain_panel.Refresh();
  app.units_panel.Refresh();
  app.Layout();
  const bool maximized = RestorePlacement(app.main);
  ShowWindow(app.main, maximized ? SW_SHOWMAXIMIZED : show);
  UpdateWindow(app.main);

  HACCEL accel = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (accel && TranslateAcceleratorW(app.main, accel, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (app.sprites) pf_sprite_set_free(app.sprites);
  if (app.art) pf_tileset_art_free(app.art);
  CoUninitialize();
  return int(msg.wParam);
}

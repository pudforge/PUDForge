#include "Tools.hpp"

#include <commctrl.h>
#include <shellapi.h>   // ShellExecuteW, for handing the guide to a browser
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Dialogs.hpp"
#include "GameData.hpp"
#include "Strings.hpp"
#include "resource.h"
#include "strings.h"

namespace pfwin {
namespace {

// ------------------------------------------------------------- shared bits

/// Fill a combo with every brush terrain, and remember which id each row is.
/// Terrains rather than tiles: the bulk edits work in terrain classes, which is
/// what makes "replace water with grass" a sentence and not a tile hunt.
void FillTerrainCombo(HWND combo, int tileset, std::vector<int>& terrains,
                      int selected) {
  terrains.clear();
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (int i = 0; i < pf_brush_count(); i++) {
    const int terrain = pf_brush_terrain(i);
    terrains.push_back(terrain);
    const char* name = pf_terrain_name(terrain, tileset);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name ? FromUtf8(name).c_str() : L"?"));
  }
  int at = 0;
  for (int i = 0; i < int(terrains.size()); i++) {
    if (terrains[size_t(i)] == selected) at = i;
  }
  SendMessageW(combo, CB_SETCURSEL, WPARAM(at), 0);
}

/// One 32x32 tile of a terrain, for the owner-drawn terrain combos.
Icon TerrainIcon(const pf_tileset_art* art, int terrain, int tileset) {
  const int tile = pf_solid_tile(terrain, 0);
  const int mega = (art && tile >= 0)
                       ? pf_tileset_art_megatile_for(art, uint16_t(tile)) : -1;
  if (mega < 0 || pf_tileset_art_is_blank(art, mega)) {
    return FlatIcon(pf_terrain_flat_colour(terrain, tileset));
  }
  Icon icon;
  icon.w = icon.h = 32;
  icon.fill = true;
  icon.px.resize(32 * 32);
  pf_tileset_art_draw(art, mega, icon.px.data(), 32);
  return icon;
}

/// Fill a combo with the units a map may hold, and remember the ids. Grouped the
/// way the palette groups them — race, then kind — so scanning the list is the
/// same act as scanning the palette.
void FillUnitCombo(HWND combo, std::vector<int>& ids, int selected) {
  ids.clear();
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (int id = 0; id < PF_UNIT_COUNT; id++) {
    if (pf_unit_is_unused(id) || pf_unit_never_offered(id)) continue;
    ids.push_back(id);
    const char* name = pf_unit_name(id);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name ? FromUtf8(name).c_str() : L"?"));
  }
  int at = 0;
  for (int i = 0; i < int(ids.size()); i++) {
    if (ids[size_t(i)] == selected) at = i;
  }
  SendMessageW(combo, CB_SETCURSEL, WPARAM(at), 0);
}

/// The slots the game plays, so `selected` is a slot and the rows are not.
void FillPlayerCombo(HWND combo, int selected) {
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (int i : PlayerSlots()) {
    const char* name = pf_player_name(i);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name ? FromUtf8(name).c_str() : L"?"));
  }
  SendMessageW(combo, CB_SETCURSEL, WPARAM(RowForSlot(selected)), 0);
}

int ComboIndex(HWND dialog, int control) {
  return int(SendDlgItemMessageW(dialog, control, CB_GETCURSEL, 0, 0));
}

/// The scope every bulk edit shares, as a phrase for its note.
std::wstring ScopeWord(const Editor& editor) {
  return Str(editor.terrain_selection().empty() ? IDS_SCOPE_MAP
                                                : IDS_SCOPE_SELECTION);
}

// ------------------------------------------------------- replace terrain

struct ReplaceSheet {
  Editor* editor = nullptr;
  const pf_tileset_art* art = nullptr;
  int tileset = 0;
  std::vector<int> terrains;
  std::map<int, Icon> icons;      ///< by terrain id, made once per sheet
  int from = -1, to = -1;

  const Icon* IconFor(int row) {
    if (row < 0 || row >= int(terrains.size())) return nullptr;
    const int terrain = terrains[size_t(row)];
    auto found = icons.find(terrain);
    if (found == icons.end()) {
      found = icons.emplace(terrain, TerrainIcon(art, terrain, tileset)).first;
    }
    return &found->second;
  }
};

void RefreshReplaceNote(HWND dialog, ReplaceSheet& sheet) {
  const int from_row = ComboIndex(dialog, IDC_REPLACE_FROM);
  const int to_row = ComboIndex(dialog, IDC_REPLACE_TO);
  if (from_row < 0 || to_row < 0) return;
  sheet.from = sheet.terrains[size_t(from_row)];
  sheet.to = sheet.terrains[size_t(to_row)];

  // Say what it would touch before it touches anything. A whole-map repaint is
  // not a small change and the count is the only warning worth giving.
  if (sheet.from == sheet.to) {
    SetDlgItemTextW(dialog, IDC_REPLACE_NOTE, Str(IDS_PICK_TWO_TERRAINS).c_str());
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    return;
  }
  const int n = sheet.editor->CountTerrain(sheet.from);
  const char* name = pf_terrain_name(sheet.from, sheet.tileset);
  const std::wstring what = name ? FromUtf8(name) : L"";
  const std::wstring scope = ScopeWord(*sheet.editor);
  // Two calls rather than one with a positional format: the C runtime's wide
  // printf has no %1$ form, so a string that needs its arguments in a different
  // order needs its own argument list.
  SetDlgItemTextW(dialog, IDC_REPLACE_NOTE,
                  (n ? Format(IDS_REPLACE_WOULD_TOUCH, n, what.c_str(), scope.c_str())
                     : Format(IDS_REPLACE_FOUND_NONE, what.c_str(), scope.c_str()))
                      .c_str());
  EnableWindow(GetDlgItem(dialog, IDOK), n > 0);
}

INT_PTR CALLBACK ReplaceProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<ReplaceSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (!sheet || !draw ||
          (draw->CtlID != IDC_REPLACE_FROM && draw->CtlID != IDC_REPLACE_TO)) {
        return FALSE;
      }
      return DrawIconRow(lparam, draw->itemID == UINT(-1)
                                     ? nullptr
                                     : sheet->IconFor(int(draw->itemID)));
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<ReplaceSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      FillTerrainCombo(GetDlgItem(dialog, IDC_REPLACE_FROM), sheet->tileset,
                       sheet->terrains, sheet->editor->TerrainOfBrush());
      std::vector<int> unused;
      FillTerrainCombo(GetDlgItem(dialog, IDC_REPLACE_TO), sheet->tileset, unused,
                       sheet->editor->TerrainOfBrush());
      RefreshReplaceNote(dialog, *sheet);
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if ((id == IDC_REPLACE_FROM || id == IDC_REPLACE_TO) && code == CBN_SELCHANGE) {
        RefreshReplaceNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, id); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ------------------------------------------------------------- decorate

struct DecorateSheet {
  Editor* editor = nullptr;
  const pf_tileset_art* art = nullptr;
  int tileset = 0;
  std::vector<int> terrains;
  std::map<int, Icon> icons;
  int terrain = -1;
  int density = 8;                ///< per cent

  const Icon* IconFor(int row) {
    if (row < 0 || row >= int(terrains.size())) return nullptr;
    const int t = terrains[size_t(row)];
    auto found = icons.find(t);
    if (found == icons.end()) found = icons.emplace(t, TerrainIcon(art, t, tileset)).first;
    return &found->second;
  }
};

void RefreshDecorateNote(HWND dialog, DecorateSheet& sheet) {
  SetDlgItemTextW(dialog, IDC_DECORATE_VALUE,
                  Format(IDS_PER_CENT, sheet.density).c_str());
  SetDlgItemTextW(dialog, IDC_DECORATE_NOTE,
                  Format(IDS_DECORATE_SCOPE, ScopeWord(*sheet.editor).c_str()).c_str());
}

INT_PTR CALLBACK DecorateProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<DecorateSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (!sheet || !draw || draw->CtlID != IDC_DECORATE_TERRAIN) return FALSE;
      return DrawIconRow(lparam, draw->itemID == UINT(-1)
                                     ? nullptr
                                     : sheet->IconFor(int(draw->itemID)));
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<DecorateSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      FillTerrainCombo(GetDlgItem(dialog, IDC_DECORATE_TERRAIN), sheet->tileset,
                       sheet->terrains, sheet->editor->TerrainOfBrush());
      HWND slider = GetDlgItem(dialog, IDC_DECORATE_DENSITY);
      SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 40));
      SendMessageW(slider, TBM_SETPOS, TRUE, sheet->density);
      RefreshDecorateNote(dialog, *sheet);
      return TRUE;
    }

    case WM_HSCROLL: {
      if (!sheet) return FALSE;
      sheet->density =
          int(SendDlgItemMessageW(dialog, IDC_DECORATE_DENSITY, TBM_GETPOS, 0, 0));
      RefreshDecorateNote(dialog, *sheet);
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDOK) {
        const int row = ComboIndex(dialog, IDC_DECORATE_TERRAIN);
        if (row >= 0) sheet->terrain = sheet->terrains[size_t(row)];
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// -------------------------------------------------------- convert units

struct ConvertSheet {
  Editor* editor = nullptr;
  IconCache* icons = nullptr;
  std::vector<int> ids;
  int from = -1, to = -1;
  /// Editor::ReplaceUnitType's scope. Off unless the box is ticked.
  bool selected_only = false;

  const Icon* IconFor(int row) {
    if (!icons || row < 0 || row >= int(ids.size())) return nullptr;
    return &icons->Unit(ids[size_t(row)]);
  }
};


void RefreshConvertNote(HWND dialog, ConvertSheet& sheet) {
  const int from_row = ComboIndex(dialog, IDC_CONVERT_FROM);
  const int to_row = ComboIndex(dialog, IDC_CONVERT_TO);
  if (from_row < 0 || to_row < 0) return;
  sheet.from = sheet.ids[size_t(from_row)];
  sheet.to = sheet.ids[size_t(to_row)];
  if (sheet.from == sheet.to) {
    SetDlgItemTextW(dialog, IDC_CONVERT_NOTE, Str(IDS_PICK_TWO_TYPES).c_str());
    EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
    return;
  }
  // The count is the editor's, and so is the conversion, so the two cannot
  // disagree about what the tick box put in scope.
  const int n = sheet.editor->CountUnitsOfType(sheet.from, sheet.selected_only);
  const char* name = pf_unit_name(sheet.from);
  const UINT said = sheet.selected_only
                        ? Plural(n, IDS_CONVERT_SEL_ONE, IDS_CONVERT_SEL_MANY)
                        : Plural(n, IDS_CONVERT_WOULD_ONE, IDS_CONVERT_WOULD_MANY);
  SetDlgItemTextW(dialog, IDC_CONVERT_NOTE,
                  Format(said, n, name ? FromUtf8(name).c_str() : L"").c_str());
  EnableWindow(GetDlgItem(dialog, IDOK), n > 0);
}

INT_PTR CALLBACK ConvertProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<ConvertSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (!sheet || !draw ||
          (draw->CtlID != IDC_CONVERT_FROM && draw->CtlID != IDC_CONVERT_TO)) {
        return FALSE;
      }
      return DrawIconRow(lparam, draw->itemID == UINT(-1)
                                     ? nullptr
                                     : sheet->IconFor(int(draw->itemID)));
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<ConvertSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      FillUnitCombo(GetDlgItem(dialog, IDC_CONVERT_FROM), sheet->ids,
                    sheet->editor->placing_type);
      std::vector<int> unused;
      FillUnitCombo(GetDlgItem(dialog, IDC_CONVERT_TO), unused,
                    sheet->editor->placing_type);
      // Nothing selected, nothing to narrow to: the box would be a way to
      // arm Convert and have it do nothing.
      EnableWindow(GetDlgItem(dialog, IDC_CONVERT_SELECTED),
                   sheet->editor->HasSelection());
      RefreshConvertNote(dialog, *sheet);
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if ((id == IDC_CONVERT_FROM || id == IDC_CONVERT_TO) && code == CBN_SELCHANGE) {
        RefreshConvertNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDC_CONVERT_SELECTED) {
        sheet->selected_only =
            IsDlgButtonChecked(dialog, IDC_CONVERT_SELECTED) == BST_CHECKED;
        RefreshConvertNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, id); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ---------------------------------------------------------- map statistics

struct StatsSheet {
  const pf_map* map = nullptr;
};

void FillStats(HWND dialog, const pf_map* map) {
  HWND list = GetDlgItem(dialog, IDC_STATS_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  int tab = 120;
  SendMessageW(list, LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tab));
  auto add = [&](const std::wstring& text) {
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
  };
  auto row = [&](const std::wstring& label, const std::wstring& value) {
    add(Format(IDS_STAT_ROW, label.c_str(), value.c_str()));
  };

  const int w = pf_map_width(map), h = pf_map_height(map);
  add(Str(IDS_STAT_HEAD_MAP));
  row(Str(IDS_FIELD_SIZE), Format(IDS_SIZE_IN_TILES, w, h));
  static const UINT kTilesets[] = {IDS_TILESET_FOREST, IDS_TILESET_WINTER,
                                   IDS_TILESET_WASTELAND, IDS_TILESET_SWAMP};
  const int tileset = pf_map_tileset(map);
  row(Str(IDS_FIELD_TILESET),
      tileset >= 0 && tileset < 4 ? Str(kTilesets[tileset]) : Str(IDS_UNKNOWN));

  // Corners, not tiles: a tile is four of them and can hold two terrains, so
  // counting tiles would round every coastline to whichever side won.
  std::map<int, int> corners;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t q[4] = {};
      pf_tile_quadrants(uint16_t(pf_map_tile_at(map, x, y)), q);
      for (uint8_t c : q) corners[int(c)]++;
    }
  }
  const double total = double(w) * double(h) * 4.0;
  add(L"");
  add(Str(IDS_STAT_HEAD_TERRAIN));
  for (int terrain = 0; terrain <= PF_TERRAIN_UNKNOWN; terrain++) {
    const auto found = corners.find(terrain);
    if (found == corners.end() || found->second == 0) continue;
    const char* name = pf_terrain_name(terrain, tileset);
    wchar_t share[32];
    swprintf(share, 32, L"%.1f%%", 100.0 * double(found->second) / total);
    row(name ? FromUtf8(name) : Str(IDS_UNKNOWN), share);
  }

  std::map<int, int> by_owner;
  int64_t gold = 0, oil = 0;
  const int units = pf_map_unit_count(map);
  for (int i = 0; i < units; i++) {
    pf_unit unit{};
    if (pf_map_unit(map, i, &unit) != PF_OK) continue;
    by_owner[unit.owner]++;
    // Which resource, and how much of it, are both the core's answer: the
    // multiplier is a fact about the format and the gold/oil split a fact about
    // the unit.
    const int resource = pf_unit_resource(unit.type);
    if (resource == PF_RESOURCE_GOLD) gold += pf_resource_amount(unit.value);
    else if (resource == PF_RESOURCE_OIL) oil += pf_resource_amount(unit.value);
  }
  add(L"");
  add(Str(IDS_STAT_HEAD_UNITS));
  row(Str(IDS_STAT_TOTAL), std::to_wstring(units));
  for (const auto& entry : by_owner) {
    const char* name = pf_player_name(entry.first);
    row(name ? FromUtf8(name) : Str(IDS_UNKNOWN), std::to_wstring(entry.second));
  }
  if (gold || oil) {
    add(L"");
    add(Str(IDS_STAT_HEAD_RESOURCES));
    if (gold) row(Str(IDS_STAT_GOLD), std::to_wstring(gold));
    if (oil) row(Str(IDS_STAT_OIL), std::to_wstring(oil));
  }
}

}  // namespace

INT_PTR CALLBACK StatsPageProc(HWND page, UINT message, WPARAM, LPARAM lparam) {
  // A page of the map window rather than a dialog of its own, so it takes the
  // map straight as its parameter — there is nothing to edit and so nothing to
  // keep a sheet struct for.
  if (message == WM_INITDIALOG) {
    FillStats(page, reinterpret_cast<const pf_map*>(lparam));
    return TRUE;
  }
  return FALSE;
}

namespace {

// ------------------------------------------------------------- export PNG

struct PngSheet {
  Editor* editor = nullptr;
  bool units = true, grid = false, only_selection = false;
  int scale = 1;
};

void RefreshPngNote(HWND dialog, PngSheet& sheet) {
  const pf_map* map = sheet.editor->map();
  const TileRect sel = sheet.editor->terrain_selection();
  const bool use_sel = sheet.only_selection && !sel.empty();
  const int cols = use_sel ? sel.w : pf_map_width(map);
  const int rows = use_sel ? sel.h : pf_map_height(map);
  SetDlgItemTextW(dialog, IDC_PNG_NOTE,
                  Format(IDS_PNG_SIZE, cols * 32 * sheet.scale,
                         rows * 32 * sheet.scale).c_str());
  // Only offer the rectangle when there is one to offer.
  EnableWindow(GetDlgItem(dialog, IDC_PNG_SELECTION), !sel.empty());
}

INT_PTR CALLBACK PngProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<PngSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<PngSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      CheckDlgButton(dialog, IDC_PNG_UNITS, sheet->units ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_PNG_GRID, sheet->grid ? BST_CHECKED : BST_UNCHECKED);
      HWND scale = GetDlgItem(dialog, IDC_PNG_SCALE);
      for (int i = 1; i <= 4; i++) {
        SendMessageW(scale, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Format(IDS_PNG_SCALE_X, i).c_str()));
      }
      SendMessageW(scale, CB_SETCURSEL, 0, 0);
      RefreshPngNote(dialog, *sheet);
      return TRUE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if (id == IDC_PNG_UNITS || id == IDC_PNG_GRID || id == IDC_PNG_SELECTION) {
        sheet->units = IsDlgButtonChecked(dialog, IDC_PNG_UNITS) == BST_CHECKED;
        sheet->grid = IsDlgButtonChecked(dialog, IDC_PNG_GRID) == BST_CHECKED;
        sheet->only_selection =
            IsDlgButtonChecked(dialog, IDC_PNG_SELECTION) == BST_CHECKED;
        RefreshPngNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDC_PNG_SCALE && code == CBN_SELCHANGE) {
        sheet->scale = ComboIndex(dialog, IDC_PNG_SCALE) + 1;
        RefreshPngNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, id); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ---------------------------------------------------------------- options

struct OptionSheet {
  Editor* editor = nullptr;
  int* unit_art = nullptr;
  bool* vary_facing = nullptr;
  bool* unit_sounds = nullptr;
  bool* check_updates = nullptr;
  bool reset = false;
};

bool g_settings_reset = false;

/// Everything this client has ever written about itself, which is one key with
/// the settings, the window placement, the recent maps and the game folder
/// under it. Deleting the tree rather than the values one at a time, so a
/// setting added later cannot be the one thing a reset forgets to forget.
void ForgetEverything() {
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\PUDForge");
  g_settings_reset = true;
}

INT_PTR CALLBACK OptionsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<OptionSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<OptionSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      const Editor& ed = *sheet->editor;
      auto set = [&](int id, bool on) {
        CheckDlgButton(dialog, id, on ? BST_CHECKED : BST_UNCHECKED);
      };
      set(IDC_OPT_FIT_EDGES, ed.auto_fit_edges);
      set(IDC_OPT_FIT_PASTE, ed.fit_pasted_edges);
      set(IDC_OPT_KEEP_STRANDED, ed.keep_stranded_units);
      set(IDC_OPT_ILLEGAL, ed.allow_illegal_placement());
      set(IDC_OPT_STACKED, ed.allow_stacked_units());
      set(IDC_OPT_EDGE, ed.allow_edge_placement());
      set(IDC_OPT_MARK_SPECIAL, ed.mark_special_units);
      set(IDC_OPT_ALL_RACES, ed.show_all_races);
      set(IDC_OPT_UNUSED_UNITS, ed.offer_unused_units);
      set(IDC_OPT_FACING, sheet->vary_facing && *sheet->vary_facing);
      set(IDC_OPT_SOUNDS, sheet->unit_sounds && *sheet->unit_sounds);
      set(IDC_OPT_UPDATES, sheet->check_updates && *sheet->check_updates);
      HWND art = GetDlgItem(dialog, IDC_OPT_UNIT_ART);
      SendMessageW(art, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(Str(IDS_ART_PORTRAIT).c_str()));
      SendMessageW(art, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(Str(IDS_ART_SPRITE).c_str()));
      SendMessageW(art, CB_SETCURSEL, WPARAM(sheet->unit_art ? *sheet->unit_art : 0), 0);
      return TRUE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDOK) {
        Editor& ed = *sheet->editor;
        auto get = [&](int control) {
          return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
        };
        ed.auto_fit_edges = get(IDC_OPT_FIT_EDGES);
        ed.fit_pasted_edges = get(IDC_OPT_FIT_PASTE);
        ed.keep_stranded_units = get(IDC_OPT_KEEP_STRANDED);
        ed.SetAllowIllegalPlacement(get(IDC_OPT_ILLEGAL));
        ed.SetAllowStackedUnits(get(IDC_OPT_STACKED));
        ed.SetAllowEdgePlacement(get(IDC_OPT_EDGE));
        ed.mark_special_units = get(IDC_OPT_MARK_SPECIAL);
        ed.show_all_races = get(IDC_OPT_ALL_RACES);
        ed.offer_unused_units = get(IDC_OPT_UNUSED_UNITS);
        ed.ApplyPlacementOption();
        if (sheet->vary_facing) *sheet->vary_facing = get(IDC_OPT_FACING);
        if (sheet->unit_sounds) *sheet->unit_sounds = get(IDC_OPT_SOUNDS);
        if (sheet->check_updates) *sheet->check_updates = get(IDC_OPT_UPDATES);
        if (sheet->unit_art) {
          *sheet->unit_art = std::max(0, ComboIndex(dialog, IDC_OPT_UNIT_ART));
        }
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (id == IDC_OPT_RESET) {
        // Defaulting to No, and warning-coloured: this is the one button here
        // that throws work away, and it is next to ordinary preferences.
        if (MessageBoxW(dialog, Str(IDS_RESET_CONFIRM).c_str(),
                        Str(IDS_RESET_TITLE).c_str(),
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
          return TRUE;
        }
        ForgetEverything();
        sheet->reset = true;
        MessageBoxW(dialog, Str(IDS_RESET_DONE).c_str(), Str(IDS_RESET_TITLE).c_str(),
                    MB_OK | MB_ICONINFORMATION);
        // Closed rather than left open: OK would write the very settings that
        // were just deleted back out, and Cancel would look like it undid it.
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// --------------------------------------------------------------- new map

struct NewSheet {
  int tileset = 0;
  int size = 64;
};

const int kNewSizes[] = {32, 64, 96, 128};

INT_PTR CALLBACK NewMapProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<NewSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<NewSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      static const UINT kTilesets[] = {IDS_TILESET_FOREST, IDS_TILESET_WINTER,
                                       IDS_TILESET_WASTELAND, IDS_TILESET_SWAMP};
      HWND set = GetDlgItem(dialog, IDC_NEW_TILESET);
      for (UINT name : kTilesets) {
        SendMessageW(set, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Str(name).c_str()));
      }
      SendMessageW(set, CB_SETCURSEL, WPARAM(sheet->tileset), 0);
      HWND size = GetDlgItem(dialog, IDC_NEW_SIZE);
      for (int i = 0; i < 4; i++) {
        SendMessageW(size, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(
                         Format(IDS_SIZE_IN_TILES, kNewSizes[i], kNewSizes[i]).c_str()));
        if (kNewSizes[i] == sheet->size) SendMessageW(size, CB_SETCURSEL, WPARAM(i), 0);
      }
      return TRUE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDOK) {
        sheet->tileset = std::max(0, ComboIndex(dialog, IDC_NEW_TILESET));
        const int row = std::max(0, ComboIndex(dialog, IDC_NEW_SIZE));
        sheet->size = kNewSizes[row < 4 ? row : 1];
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// -------------------------------------------------------- unit inspector

struct InspectSheet {
  Editor* editor = nullptr;
  IconCache* icons = nullptr;
  int index = -1;
  pf_unit unit{};
  bool amount = false;   ///< value is a resource amount, not the rescue flag
};

/// Read the two editable fields back off the controls.
///
/// Shared by OK and by the properties button, so leaving for another dialog is
/// not a way of quietly discarding an owner change made just before pressing it.
void ReadInspector(HWND dialog, InspectSheet& sheet) {
  // Both narrowings are explicit because the fields are: an owner is one of
  // sixteen slots and the value is the format's own 16 bits.
  sheet.unit.owner =
      uint8_t(std::min(PF_PLAYER_COUNT - 1,
                       std::max(0, SlotForRow(ComboIndex(dialog,
                                                        IDC_INSPECT_OWNER)))));
  if (!sheet.amount) {
    sheet.unit.value =
        IsDlgButtonChecked(dialog, IDC_INSPECT_ACTIVE) == BST_CHECKED ? 1 : 0;
    return;
  }
  BOOL ok = FALSE;
  const UINT typed = GetDlgItemInt(dialog, IDC_INSPECT_VALUE, &ok, FALSE);
  if (!ok) return;
  // A resource is shown and typed as gold or oil, the way the game shows it and
  // the way the statistics page counts it. The field holds that amount / 2,500,
  // so this is the only place the two meet — and it is the core that divides,
  // because the step is the format's rule and not this dialog's.
  sheet.unit.value = sheet.amount ? uint16_t(pf_resource_value(int64_t(typed)))
                                  : uint16_t(std::min<UINT>(typed, 0xFFFFu));
}

INT_PTR CALLBACK InspectProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<InspectSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (!sheet || !draw || draw->CtlID != IDC_INSPECT_ICON) return FALSE;
      FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_BTNFACE));
      if (sheet->icons) BlitIcon(draw->hDC, draw->rcItem, sheet->icons->Unit(sheet->unit.type), 1);
      return TRUE;
    }
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<InspectSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      const char* name = pf_unit_name(sheet->unit.type);
      SetWindowTextW(dialog, name ? FromUtf8(name).c_str() : L"");
      SetDlgItemTextW(dialog, IDC_INSPECT_TYPE,
                      name ? FromUtf8(name).c_str() : Str(IDS_UNKNOWN).c_str());
      SetDlgItemTextW(dialog, IDC_INSPECT_POS,
                      Format(IDS_INSPECT_AT, sheet->unit.x, sheet->unit.y).c_str());
      FillPlayerCombo(GetDlgItem(dialog, IDC_INSPECT_OWNER), sheet->unit.owner);
      // The one number the format keeps per unit means two different things, so
      // the label says which: an amount on a resource, active or passive on
      // everything else. Editing one as the other quietly wipes the oil.
      sheet->amount = pf_unit_value_is_amount(sheet->unit.type) != 0;
      // The game names it per resource — "Gold Left:", "Oil Left:" — which is
      // both more specific and localised. It only answers with a game folder to
      // hand, so the plain label stays behind it.
      char resource_label[64] = {};
      pf_resource_label(pf_unit_resource(sheet->unit.type), resource_label,
                        int(sizeof(resource_label)));
      const std::wstring label =
          sheet->amount && resource_label[0]
              ? FromUtf8(resource_label)
              : Str(sheet->amount ? IDS_INSPECT_AMOUNT : IDS_INSPECT_FLAG);
      SetDlgItemTextW(dialog, IDC_INSPECT_VALUE_LBL, label.c_str());
      // In gold, not in the sixteen the file holds. "Gold Left: 16" against a
      // mine the statistics page counts as 40,000 is the same number twice in
      // two units, and nothing on screen said which this was.
      SetDlgItemInt(dialog, IDC_INSPECT_VALUE,
                    UINT(sheet->amount ? pf_resource_amount(sheet->unit.value)
                                       : sheet->unit.value),
                    FALSE);
      // A resource types an amount; everything else picks one of two states, so
      // only one of the two controls is ever up. A spin box for a thing with
      // two states is a spin box you have to think in.
      ShowWindow(GetDlgItem(dialog, IDC_INSPECT_VALUE),
                 sheet->amount ? SW_SHOW : SW_HIDE);
      ShowWindow(GetDlgItem(dialog, IDC_INSPECT_ACTIVE),
                 sheet->amount ? SW_HIDE : SW_SHOW);
      ShowWindow(GetDlgItem(dialog, IDC_INSPECT_PASSIVE),
                 sheet->amount ? SW_HIDE : SW_SHOW);
      if (!sheet->amount) {
        // Anything that is not 0 reads as passive rather than as nothing: the
        // field is the format's 16 bits and a map may hold any of them, and a
        // dialog that showed neither button would lose the value on OK.
        CheckRadioButton(dialog, IDC_INSPECT_ACTIVE, IDC_INSPECT_PASSIVE,
                         sheet->unit.value ? IDC_INSPECT_ACTIVE
                                           : IDC_INSPECT_PASSIVE);
      }
      // The step is the reason a typed number can come back changed, and the
      // default is what a mine dropped on the map starts with — both are things
      // only this line says anywhere in the client.
      SetDlgItemTextW(dialog, IDC_INSPECT_NOTE,
                      sheet->amount
                          ? Format(IDS_INSPECT_STEP,
                                   pf_resource_amount(
                                       pf_unit_default_value(sheet->unit.type)))
                                .c_str()
                          : Str(IDS_INSPECT_FLAG_NOTE).c_str());
      // Named after the unit, the same way the units dock names it, so it is
      // clear the button is about every Footman rather than about this one.
      SetDlgItemTextW(
          dialog, IDC_INSPECT_PROPS,
          Format(IDS_UNIT_PROPS_FOR,
                 name ? FromUtf8(name).c_str() : Str(IDS_UNKNOWN).c_str())
              .c_str());
      return TRUE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDOK) {
        ReadInspector(dialog, *sheet);
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (id == IDC_INSPECT_PROPS) {
        // Ends with its own code rather than IDOK: the caller has to know it was
        // asked to go somewhere as well as to save.
        ReadInspector(dialog, *sheet);
        EndDialog(dialog, IDC_INSPECT_PROPS);
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ------------------------------------------------------------- movement

struct MoveSheet {
  Editor* editor = nullptr;
  bool changed = false;
};

void RefreshMovement(HWND dialog, MoveSheet& sheet) {
  SetDlgItemTextW(dialog, IDC_MOVE_ABOUT, Str(IDS_MOVE_ABOUT).c_str());
  SetDlgItemTextW(dialog, IDC_MOVE_LEGEND, Str(IDS_MOVE_LEGEND).c_str());
  SetDlgItemTextW(dialog, IDC_MOVE_HINT, Str(IDS_MOVE_HINT).c_str());
  HWND list = GetDlgItem(dialog, IDC_MOVE_CLASSES);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  int tab = 150;
  SendMessageW(list, LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tab));
  // The named classes and their bits. The hex matters: the overlay draws these
  // values and a mapper reading it needs the key beside it.
  for (int i = 0; i < pf_movement_class_count(); i++) {
    const char* name = pf_movement_class_name(i);
    wchar_t hex[16];
    swprintf(hex, 16, L"%04X", unsigned(pf_movement_class_value(i)) & 0xFFFFu);
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(
                     Format(IDS_STAT_ROW, name ? FromUtf8(name).c_str() : L"?", hex)
                         .c_str()));
  }
  const int n = sheet.editor->MovementOverrides();
  SetDlgItemTextW(dialog, IDC_MOVE_COUNT,
                  (n ? Format(Plural(n, IDS_MOVE_OVERRIDDEN_ONE, IDS_MOVE_OVERRIDDEN_MANY),
                              n, ScopeWord(*sheet.editor).c_str())
                     : Str(IDS_MOVE_MATCHES)).c_str());
  EnableWindow(GetDlgItem(dialog, IDC_MOVE_RESET), n > 0);
}

INT_PTR CALLBACK MovementProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<MoveSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG:
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<MoveSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      RefreshMovement(dialog, *sheet);
      SetFocus(GetDlgItem(dialog, IDOK));
      return FALSE;
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDC_MOVE_RESET) {
        if (sheet->editor->ResetMovement() > 0) sheet->changed = true;
        RefreshMovement(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, IDOK); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ----------------------------------------------------------- AI scripts

struct AiSheet {
  pf_ai_scripts* scripts = nullptr;     ///< borrowed; the caller frees
  std::vector<int> values;              ///< the AI value each row stands for
  /// Which script to open on. A PUD stores an AI *number*, so the player page
  /// hands the number over and this lands on it rather than on script 0.
  int start = 0;
};

/// The core hands back the full length so a caller can size a buffer and ask
/// again; this is that, once.
std::wstring AiText(int (*fn)(const pf_ai_scripts*, int, char*, int),
                    const pf_ai_scripts* scripts, int index) {
  const int need = fn(scripts, index, nullptr, 0);
  if (need <= 0) return L"";
  std::vector<char> buffer(size_t(need) + 1);
  fn(scripts, index, buffer.data(), int(buffer.size()));
  // Edit controls want CRLF; the core writes bare newlines.
  std::wstring wide = FromUtf8(buffer.data());
  std::wstring out;
  out.reserve(wide.size() + wide.size() / 8);
  for (wchar_t c : wide) {
    if (c == L'\n') out += L'\r';
    out += c;
  }
  return out;
}

void RefreshAi(HWND dialog, AiSheet& sheet) {
  const int row = int(SendDlgItemMessageW(dialog, IDC_AI_LIST, LB_GETCURSEL, 0, 0));
  if (row < 0 || row >= int(sheet.values.size())) return;
  const int index = sheet.values[size_t(row)];
  SetDlgItemTextW(dialog, IDC_AI_SUMMARY,
                  AiText(&pf_ai_script_summary, sheet.scripts, index).c_str());
  SetDlgItemTextW(dialog, IDC_AI_LISTING,
                  AiText(&pf_ai_script_listing, sheet.scripts, index).c_str());
}

INT_PTR CALLBACK AiProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<AiSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<AiSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      HWND list = GetDlgItem(dialog, IDC_AI_LIST);
      const int count = pf_ai_scripts_count(sheet->scripts);
      for (int i = 0; i < count; i++) {
        sheet->values.push_back(i);
        const char* name = pf_ai_name(i);
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(
                         Format(IDS_AI_ROW, i,
                                name ? FromUtf8(name).c_str() : L"").c_str()));
      }
      const int at = sheet->start >= 0 && sheet->start < count ? sheet->start : 0;
      SendMessageW(list, LB_SETCURSEL, WPARAM(at), 0);
      // Scrolled to as well as selected: a highlighted row two hundred entries
      // down an unscrolled list is a highlighted row nobody can see.
      SendMessageW(list, LB_SETTOPINDEX, WPARAM(at), 0);
      RefreshAi(dialog, *sheet);
      return TRUE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if (id == IDC_AI_LIST && code == LBN_SELCHANGE) {
        RefreshAi(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, IDOK); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// --------------------------------------------------------- the archive

struct ArchiveSheet {
  pf_mpq* mpq = nullptr;            ///< borrowed; the caller frees
  std::vector<std::string> maps;    ///< every .pud the archive holds
  std::vector<int> rows;            ///< indices into `maps`, after filtering
  int chosen = -1;                  ///< index into `maps`
};

/// ASCII lowercase. Archive entry names are paths the game wrote, so a
/// locale-aware fold would be the wrong tool.
std::string Lowered(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return out;
}

/// Case-insensitive "does this end in .pud".
bool IsMapName(const std::string& name) {
  return name.size() >= 4 && Lowered(name.substr(name.size() - 4)) == ".pud";
}

void RefreshArchive(HWND dialog, ArchiveSheet& sheet) {
  wchar_t typed[128] = {};
  GetDlgItemTextW(dialog, IDC_ARCHIVE_FIND, typed, 128);
  const std::string needle = Lowered(ToUtf8(typed));

  HWND list = GetDlgItem(dialog, IDC_ARCHIVE_LIST);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  sheet.rows.clear();
  for (int i = 0; i < int(sheet.maps.size()); i++) {
    if (!needle.empty()) {
      if (Lowered(sheet.maps[size_t(i)]).find(needle) == std::string::npos) continue;
    }
    sheet.rows.push_back(i);
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(FromUtf8(sheet.maps[size_t(i)]).c_str()));
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  if (!sheet.rows.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
  SetDlgItemTextW(dialog, IDC_ARCHIVE_NOTE,
                  Format(Plural(int(sheet.rows.size()), IDS_ARCHIVE_COUNT_ONE,
                                IDS_ARCHIVE_COUNT_MANY),
                         int(sheet.rows.size())).c_str());
  EnableWindow(GetDlgItem(dialog, IDOK), !sheet.rows.empty());
}

INT_PTR CALLBACK ArchiveProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<ArchiveSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG:
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<ArchiveSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      SendDlgItemMessageW(dialog, IDC_ARCHIVE_FIND, EM_SETCUEBANNER, TRUE,
                          reinterpret_cast<LPARAM>(Str(IDS_ARCHIVE_FIND).c_str()));
      RefreshArchive(dialog, *sheet);
      return TRUE;
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if (id == IDC_ARCHIVE_FIND && code == EN_CHANGE) {
        RefreshArchive(dialog, *sheet);
        return TRUE;
      }
      if (id == IDC_ARCHIVE_LIST && code == LBN_DBLCLK) {
        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        return TRUE;
      }
      if (id == IDOK) {
        const int row =
            int(SendDlgItemMessageW(dialog, IDC_ARCHIVE_LIST, LB_GETCURSEL, 0, 0));
        if (row >= 0 && row < int(sheet->rows.size())) {
          sheet->chosen = sheet->rows[size_t(row)];
          EndDialog(dialog, IDOK);
        }
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

}  // namespace

// ----------------------------------------------------- first-run game setup

struct SetupSheet {
  GameData* game = nullptr;
  std::vector<GameInstall> found;
  /// The row to open on: the folder in use, or the first if none is.
  int selected = 0;
  /// Closing this ends the run rather than keeping what is already in use.
  bool required = false;
  /// Made for the heading and the icon well, and freed with the dialog. Both
  /// are sized from the window, so neither can be a shared resource.
  HFONT title_font = nullptr;
  HICON icon = nullptr;
};

/// The button that declines, which only one of the two callers wants.
///
/// From the menu it is Cancel and means keep the folder in use. At startup
/// declining ends the run, and a button for that is one the title bar already
/// has — so there is none, and OK slides right to close the gap. Escape and the
/// close box still answer IDCANCEL with no cancel button in sight, which is what
/// the caller reads.
void SetupWayOut(HWND dialog, bool required) {
  HWND cancel = GetDlgItem(dialog, IDCANCEL);
  if (!required) {
    SetDlgItemTextW(dialog, IDCANCEL, Str(IDS_SETUP_CANCEL).c_str());
    return;
  }
  HWND ok = GetDlgItem(dialog, IDOK);
  if (!cancel || !ok) return;
  RECT taken = {}, mine = {};
  GetWindowRect(cancel, &taken);
  GetWindowRect(ok, &mine);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&taken), 2);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&mine), 2);
  // Right edges aligned rather than left, so OK keeps its own width and the row
  // still ends on the margin the rest of the dialog is laid out to.
  SetWindowPos(ok, nullptr, taken.right - (mine.right - mine.left), mine.top, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER);
  ShowWindow(cancel, SW_HIDE);
}

/// The app icon at 32x32, DPI-scaled, into the well at the top left.
///
/// Loaded at a size rather than taken from the window: the caption's icon is
/// whatever the small-icon cache holds, and a 16 stretched to 32 is exactly the
/// blur a welcome screen should not open with.
void SetupDressing(HWND dialog, SetupSheet* sheet) {
  const HMODULE self = GetModuleHandleW(nullptr);
  const int px = MulDiv(32, int(GetDpiForWindow(dialog)), 96);
  sheet->icon = HICON(LoadImageW(self, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, px,
                                 px, LR_DEFAULTCOLOR));
  if (sheet->icon) {
    SendDlgItemMessageW(dialog, IDC_SETUP_ICON, STM_SETICON,
                        reinterpret_cast<WPARAM>(sheet->icon), 0);
  }

  // Grown from the dialog's own font rather than named: that way it is the
  // shell's face at the shell's size, already scaled by the DPI the dialog was
  // laid out at, and a heading is the one thing here that must not be 8pt.
  LOGFONTW lf = {};
  HFONT base = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
  if (!base || !GetObjectW(base, sizeof(lf), &lf)) return;
  lf.lfHeight = MulDiv(lf.lfHeight, 14, 8);
  lf.lfWeight = FW_SEMIBOLD;
  sheet->title_font = CreateFontIndirectW(&lf);
  if (sheet->title_font) {
    SendDlgItemMessageW(dialog, IDC_SETUP_TITLE, WM_SETFONT,
                        reinterpret_cast<WPARAM>(sheet->title_font), TRUE);
  }
}

/// Adopt what the list has selected, or say why not. Shared by OK and by a
/// double-click on a row, which must mean the same thing.
bool SetupTake(HWND dialog, SetupSheet* sheet) {
  const int row = int(SendDlgItemMessageW(dialog, IDC_SETUP_LIST, LB_GETCURSEL, 0, 0));
  if (row < 0 || row >= int(sheet->found.size())) return false;
  if (sheet->game->Choose(sheet->found[size_t(row)].path)) {
    EndDialog(dialog, IDOK);
    return true;
  }
  // Verified when the list was built, so this is a folder that has gone away
  // since — a removable drive, or an uninstall between then and now.
  MessageBoxW(dialog, Str(IDS_SETUP_BAD_FOLDER).c_str(),
              Str(IDS_FIND_GAME_TITLE).c_str(), MB_OK | MB_ICONWARNING);
  return false;
}

INT_PTR CALLBACK SetupProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  SetupSheet* sheet = reinterpret_cast<SetupSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(lparam));
      sheet = reinterpret_cast<SetupSheet*>(lparam);
      CentreOnScreen(dialog);
      SetupDressing(dialog, sheet);
      // Neither the greeting nor the way out is the same in the two places this
      // opens from: at startup it is the first thing a new install shows and
      // declining ends the run, while from the menu it is a list somebody asked
      // for and declining keeps the folder already in use.
      const std::wstring title =
          Str(sheet->required ? IDS_SETUP_TITLE_WELCOME : IDS_SETUP_TITLE_FIND);
      SetWindowTextW(dialog, title.c_str());
      SetDlgItemTextW(dialog, IDC_SETUP_TITLE, title.c_str());
      SetupWayOut(dialog, sheet->required);
      SetDlgItemTextW(dialog, IDC_SETUP_NOTE,
                      Str(sheet->required ? IDS_SETUP_NOTE_REQUIRED
                                          : IDS_SETUP_NOTE).c_str());
      SetDlgItemTextW(dialog, IDC_SETUP_GUIDE, Str(IDS_SETUP_GUIDE).c_str());
      SetDlgItemTextW(dialog, IDC_SETUP_HEAD,
                      Str(sheet->found.empty() ? IDS_SETUP_HEAD_NONE
                                               : IDS_SETUP_HEAD_FOUND).c_str());
      HWND list = GetDlgItem(dialog, IDC_SETUP_LIST);
      for (const GameInstall& install : sheet->found) {
        const std::wstring row = Format(IDS_SETUP_ROW, install.label.c_str(),
                                        install.path.c_str());
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
      }
      SendMessageW(list, LB_SETCURSEL, WPARAM(sheet->selected), 0);
      // Nothing to take means nothing to press: Browse is the only way on.
      if (sheet->found.empty()) {
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        SetFocus(GetDlgItem(dialog, IDC_SETUP_BROWSE));
        return FALSE;
      }
      return TRUE;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id == IDC_SETUP_LIST && HIWORD(wparam) == LBN_DBLCLK) {
        SetupTake(dialog, sheet);
        return TRUE;
      }
      if (id == IDOK) { SetupTake(dialog, sheet); return TRUE; }
      if (id == IDC_SETUP_GUIDE) {
        // A message box rather than a status line, because there is no status
        // bar behind this: at startup the main window has not been shown yet.
        if (!OpenUserGuide(dialog)) {
          MessageBoxW(dialog, Str(IDS_GUIDE_FAILED).c_str(),
                      Str(IDS_FIND_GAME_TITLE).c_str(), MB_OK | MB_ICONWARNING);
        }
        return TRUE;
      }
      if (id == IDC_SETUP_BROWSE) {
        // Ask remembers on its own, so a browsed folder needs nothing further.
        bool picked = false;
        if (sheet->game->Ask(dialog, &picked)) {
          EndDialog(dialog, IDOK);
        } else if (picked) {
          MessageBoxW(dialog, Str(IDS_SETUP_BAD_FOLDER).c_str(),
                      Str(IDS_FIND_GAME_TITLE).c_str(), MB_OK | MB_ICONWARNING);
        }
        return TRUE;
      }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ============================================================ entry points

pf_map* ShowOpenFromArchive(HWND owner, HINSTANCE instance, GameData& game,
                            std::wstring& name, std::wstring& note) {
  // Start where the game is, because that is where the interesting archive is:
  // the campaign maps live inside War2Dat.mpq and nowhere else.
  wchar_t path[MAX_PATH] = L"War2Dat.mpq";
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  const std::wstring filter = Str(IDS_FILTER_ARCHIVES) + L'\0' + L"*.mpq" + L'\0' +
                              Str(IDS_FILTER_ALL) + L'\0' + L"*.*" + L'\0';
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  const std::wstring folder = game.folder();
  if (!folder.empty()) ofn.lpstrInitialDir = folder.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&ofn)) return nullptr;

  // The whole archive into memory: the core's reader wants bytes, and even
  // War2Dat is only a few tens of megabytes.
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { note = Format(IDS_CANNOT_OPEN, path); return nullptr; }
  LARGE_INTEGER size{};
  std::vector<uint8_t> bytes;
  bool read_ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
                 size.QuadPart < (1LL << 31);
  if (read_ok) {
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    read_ok = ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) &&
              read == bytes.size();
  }
  CloseHandle(file);
  if (!read_ok) { note = Format(IDS_CANNOT_OPEN, path); return nullptr; }

  pf_mpq* mpq = pf_mpq_open_memory(bytes.data(), bytes.size(), nullptr);
  if (!mpq) { note = Str(IDS_ARCHIVE_UNREADABLE); return nullptr; }

  ArchiveSheet sheet;
  sheet.mpq = mpq;
  for (int i = 0, n = pf_mpq_file_count(mpq); i < n; i++) {
    const char* entry = pf_mpq_file_name(mpq, i);
    if (entry && IsMapName(entry)) sheet.maps.push_back(entry);
  }
  if (sheet.maps.empty()) {
    pf_mpq_free(mpq);
    note = Str(IDS_ARCHIVE_NO_MAPS);
    return nullptr;
  }

  pf_map* map = nullptr;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ARCHIVE), owner, ArchiveProc,
                      LPARAM(&sheet)) == IDOK &&
      sheet.chosen >= 0) {
    const std::string& entry = sheet.maps[size_t(sheet.chosen)];
    size_t length = 0;
    uint8_t* payload = pf_mpq_read(mpq, entry.c_str(), &length);
    if (payload) {
      pf_status status = PF_OK;
      map = pf_map_open(payload, length, &status);
      pf_buffer_free(payload);
    }
    // The name inside the archive, not a path: there is no file on disk, so Save
    // has to ask where, which is what an empty path means to the shell.
    const size_t slash = entry.find_last_of("\\/");
    name = FromUtf8(slash == std::string::npos ? entry : entry.substr(slash + 1));
    note = map ? Format(IDS_ARCHIVE_OPENED, name.c_str())
               : Format(IDS_CANNOT_OPEN, name.c_str());
  }
  pf_mpq_free(mpq);
  return map;
}

bool ShowMovement(HWND owner, HINSTANCE instance, Editor& editor, std::wstring& note) {
  if (!editor.map()) return false;
  MoveSheet sheet;
  sheet.editor = &editor;
  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MOVEMENT), owner, MovementProc,
                  LPARAM(&sheet));
  if (sheet.changed) note = Str(IDS_MOVE_RESET_DONE);
  return sheet.changed;
}

std::wstring AiScriptText(const pf_ai_scripts* scripts, int value) {
  if (!scripts) return Str(IDS_AI_NEEDS_GAME);
  // The summary and not the disassembly: what it attacks with, in what groups,
  // and the force it fields. The four hundred instructions under that are what
  // the button beside this opens a window for.
  return AiText(&pf_ai_script_summary, scripts, value);
}

void ShowAiScripts(HWND owner, HINSTANCE instance, GameData& game, int start) {
  pf_ai_scripts* scripts = game.OpenAiScripts();
  if (!scripts) {
    MessageBoxW(owner, Str(IDS_AI_NEEDS_GAME).c_str(), Str(IDS_FIND_GAME_TITLE).c_str(),
                MB_OK | MB_ICONINFORMATION);
    return;
  }
  AiSheet sheet;
  sheet.scripts = scripts;
  sheet.start = start;
  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_AI_SCRIPTS), owner, AiProc,
                  LPARAM(&sheet));
  pf_ai_scripts_free(scripts);
}

bool ShowReplaceTerrain(HWND owner, HINSTANCE instance, Editor& editor,
                        const pf_tileset_art* art, std::wstring& note) {
  if (!editor.map()) return false;
  ReplaceSheet sheet;
  sheet.editor = &editor;
  sheet.art = art;
  sheet.tileset = pf_map_tileset(editor.map());
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_REPLACE_TERRAIN), owner,
                      ReplaceProc, LPARAM(&sheet)) != IDOK) {
    return false;
  }
  const int n = editor.ReplaceTerrain(sheet.from, sheet.to);
  const char* from = pf_terrain_name(sheet.from, sheet.tileset);
  const char* to = pf_terrain_name(sheet.to, sheet.tileset);
  note = n ? Format(IDS_REPLACED_TILES, n, from ? FromUtf8(from).c_str() : L"",
                    to ? FromUtf8(to).c_str() : L"")
           : Str(IDS_REPLACED_NOTHING);
  return n > 0;
}

bool ShowDecorate(HWND owner, HINSTANCE instance, Editor& editor,
                  const pf_tileset_art* art, std::wstring& note) {
  if (!editor.map()) return false;
  DecorateSheet sheet;
  sheet.editor = &editor;
  sheet.art = art;
  sheet.tileset = pf_map_tileset(editor.map());
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_DECORATE), owner,
                      DecorateProc, LPARAM(&sheet)) != IDOK) {
    return false;
  }
  // A fresh seed per press, so pressing again gives a different scatter — but
  // one that can be reproduced, since the seed is what the result depends on.
  const uint32_t seed = uint32_t(GetTickCount()) | 1u;
  const Editor::BulkResult result =
      editor.DecorateTerrain(sheet.terrain, sheet.density / 100.0, seed);
  const char* name = pf_terrain_name(sheet.terrain, sheet.tileset);
  const std::wstring what = name ? FromUtf8(name) : L"";
  if (!result.changed) {
    note = Str(IDS_DECORATED_NOTHING);
  } else if (result.removed) {
    // Said here rather than left to the count of tiles: scattering trees over a
    // base deletes it, and that has to be visible while the undo step is still
    // the one at the top.
    note = Format(Plural(result.removed, IDS_DECORATED_REMOVED_ONE,
                         IDS_DECORATED_REMOVED_MANY),
                  result.changed, what.c_str(), result.removed);
  } else {
    note = Format(IDS_DECORATED, result.changed, what.c_str());
  }
  return result.changed > 0;
}

bool ShowConvertUnits(HWND owner, HINSTANCE instance, Editor& editor,
                      IconCache* icons, std::wstring& note) {
  if (!editor.map()) return false;
  ConvertSheet sheet;
  sheet.editor = &editor;
  sheet.icons = icons;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONVERT_UNITS), owner,
                      ConvertProc, LPARAM(&sheet)) != IDOK) {
    return false;
  }
  const Editor::BulkResult result =
      editor.ReplaceUnitType(sheet.from, sheet.to, sheet.selected_only);
  const char* from = pf_unit_name(sheet.from);
  note = result.changed
             ? Format(IDS_CONVERTED, result.changed,
                      from ? FromUtf8(from).c_str() : L"", result.skipped)
             : Str(IDS_CONVERTED_NOTHING);
  return result.changed > 0;
}

bool SettingsWereReset() { return g_settings_reset; }

/// The guide is one HTML file in the exe's own resources, so a copy that has
/// been moved off the machine it was built on still documents itself — no
/// install, no network, nothing to go missing.
///
/// Written to %TEMP% and handed to whatever the user reads HTML with, rather
/// than shown in a control of our own. A browser already has find, zoom,
/// print and a back button, and the alternative on Windows is either the
/// deprecated MSHTML control or a WebView2 runtime the user may not have.
///
/// Rewritten every time rather than kept: it costs twenty kilobytes, and a
/// stale copy from a previous version is exactly the kind of thing nobody
/// thinks to suspect.
bool OpenUserGuide(HWND owner) {
  const HMODULE self = GetModuleHandleW(nullptr);
  HRSRC found = FindResourceW(self, MAKEINTRESOURCEW(IDR_USER_GUIDE), RT_RCDATA);
  if (!found) return false;
  const DWORD size = SizeofResource(self, found);
  HGLOBAL loaded = LoadResource(self, found);
  if (!loaded || size == 0) return false;
  const void* bytes = LockResource(loaded);
  if (!bytes) return false;

  wchar_t dir[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, dir)) return false;
  std::wstring path = std::wstring(dir) + L"PUDForge-user-guide.html";

  // Shared for reading: the browser may still have the previous copy open, and
  // failing to replace it would show yesterday's page with no sign of why.
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, bytes, size, &written, nullptr);
  CloseHandle(file);
  if (!ok || written != size) return false;

  // Anything at or below 32 is one of ShellExecute's error codes, not a handle.
  const HINSTANCE result =
      ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

bool ShowGameSetup(HWND owner, HINSTANCE instance, GameData& game, bool required) {
  SetupSheet sheet;
  sheet.game = &game;
  sheet.required = required;
  // Before the dialog, not inside WM_INITDIALOG: this reads a hundred uninstall
  // keys and opens a tileset out of every candidate, and a window that paints
  // after a visible pause looks broken rather than thorough.
  sheet.found = FindGameInstalls();
  // The folder in use belongs in the list even when the search cannot see it:
  // Restore also looks beside the exe and up from it, which FindGameInstalls
  // does not, so a copy dropped into the game folder used to be shown "none
  // found" over a window already drawing that game's artwork.
  bool listed = false;
  for (const GameInstall& install : sheet.found) {
    listed = listed || install.path == game.folder();
  }
  if (!listed && game.ready()) {
    sheet.found.insert(sheet.found.begin(), {Str(IDS_SETUP_IN_USE), game.folder()});
  }
  // Opened on the folder in use when there is one, so that reopening this from
  // the menu and pressing OK confirms the current choice rather than silently
  // switching to whichever install happens to be listed first. Otherwise the
  // first, which is the best-evidence one: an INI somebody wrote on purpose,
  // then what an installer recorded, then a guess.
  for (int i = 0; i < int(sheet.found.size()); i++) {
    if (sheet.found[size_t(i)].path == game.folder()) { sheet.selected = i; break; }
  }
  const INT_PTR closed = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_GAME_SETUP),
                                         owner, SetupProc, LPARAM(&sheet));
  // Out here rather than on WM_DESTROY: the sheet outlives the window, and the
  // dialog is gone by now, so neither object can still be selected into it.
  if (sheet.title_font) DeleteObject(sheet.title_font);
  if (sheet.icon) DestroyIcon(sheet.icon);
  return closed == IDOK;
}

bool ShowOptions(HWND owner, HINSTANCE instance, Editor& editor, int* unit_art,
                 bool* vary_facing, bool* unit_sounds, bool* check_updates,
                 bool* reset) {
  OptionSheet sheet;
  sheet.editor = &editor;
  sheet.unit_art = unit_art;
  sheet.vary_facing = vary_facing;
  sheet.unit_sounds = unit_sounds;
  sheet.check_updates = check_updates;
  const bool ok = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_OPTIONS), owner,
                                  OptionsProc, LPARAM(&sheet)) == IDOK;
  if (reset) *reset = sheet.reset;
  return ok;
}

pf_map* ShowNewMap(HWND owner, HINSTANCE instance, int tileset) {
  NewSheet sheet;
  sheet.tileset = tileset;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_NEW_MAP), owner, NewMapProc,
                      LPARAM(&sheet)) != IDOK) {
    return nullptr;
  }
  return pf_map_create(sheet.size, sheet.size, sheet.tileset, nullptr);
}

bool ShowUnitInspector(HWND owner, HINSTANCE instance, Editor& editor,
                       IconCache* icons, int index, std::wstring& note,
                       int* open_properties_for) {
  if (open_properties_for) *open_properties_for = -1;
  if (!editor.map() || index < 0) return false;
  InspectSheet sheet;
  sheet.editor = &editor;
  sheet.icons = icons;
  sheet.index = index;
  if (pf_map_unit(editor.map(), index, &sheet.unit) != PF_OK) return false;
  const pf_unit before = sheet.unit;

  const INT_PTR closed = DialogBoxParamW(
      instance, MAKEINTRESOURCEW(IDD_UNIT_INSPECTOR), owner, InspectProc,
      LPARAM(&sheet));
  if (closed != IDOK && closed != IDC_INSPECT_PROPS) return false;
  // Asked for before the early return below: the sheet is wanted whether or not
  // the two fields here were touched on the way past.
  if (closed == IDC_INSPECT_PROPS && open_properties_for) {
    *open_properties_for = sheet.unit.type;
  }
  if (sheet.unit.owner == before.owner && sheet.unit.value == before.value) {
    // Not worth saying when the user is on their way to another window; the
    // sheet opening over the top of it is the answer to what just happened.
    if (closed == IDOK) note = Str(IDS_NOTHING_CHANGED);
    return false;
  }
  // Through the editor rather than the map: giving a unit to a player of the
  // other race can change what the unit *is*, and that rule lives in one place.
  const int now = editor.SetUnitOwnerAndValue(index, sheet.unit.owner,
                                              sheet.unit.value);
  pf_unit after = sheet.unit;
  if (now >= 0) pf_map_unit(editor.map(), now, &after);
  // The name the unit answers to now, which a conversion has changed.
  const char* name = pf_unit_name(after.type);
  note = Format(IDS_UNIT_UPDATED, name ? FromUtf8(name).c_str() : L"");
  return true;
}

bool ShowExportPng(HWND owner, HINSTANCE instance, Editor& editor,
                   const pf_tileset_art* art, pf_sprite_set* sprites,
                   std::wstring& note) {
  pf_map* map = editor.map();
  if (!map) return false;
  PngSheet sheet;
  sheet.editor = &editor;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_EXPORT_PNG), owner, PngProc,
                      LPARAM(&sheet)) != IDOK) {
    return false;
  }

  const TileRect sel = editor.terrain_selection();
  const bool use_sel = sheet.only_selection && !sel.empty();
  pf_render_options options = {};
  options.x0 = use_sel ? sel.x : 0;
  options.y0 = use_sel ? sel.y : 0;
  options.cols = use_sel ? sel.w : pf_map_width(map);
  options.rows = use_sel ? sel.h : pf_map_height(map);
  options.art = art;
  options.sprites = sheet.units ? sprites : nullptr;
  options.overlay = PF_OVERLAY_NONE;
  options.grid = sheet.grid ? 1 : 0;
  options.placeholders = 1;
  // Every unit facing the same way, so two exports of one map can be compared.
  options.vary_facing = 0;

  const int needed = pf_map_compose_region(map, &options, nullptr, 0);
  if (needed <= 0) { note = Str(IDS_PNG_FAILED); return false; }
  std::vector<uint32_t> pixels(static_cast<size_t>(needed));
  if (pf_map_compose_region(map, &options, pixels.data(), pixels.size()) != needed) {
    note = Str(IDS_PNG_FAILED);
    return false;
  }

  const int pw = options.cols * 32, ph = options.rows * 32;
  std::vector<uint32_t> scaled;
  const uint32_t* out = pixels.data();
  int ow = pw, oh = ph;
  if (sheet.scale > 1) {
    // Nearest-neighbour, by whole pixels: the artwork is 32x32 pixel art and
    // smoothing it would be a lie about the map.
    ow = pw * sheet.scale;
    oh = ph * sheet.scale;
    scaled.resize(size_t(ow) * size_t(oh));
    for (int y = 0; y < oh; y++) {
      const uint32_t* in = pixels.data() + size_t(y / sheet.scale) * size_t(pw);
      uint32_t* row = scaled.data() + size_t(y) * size_t(ow);
      for (int x = 0; x < ow; x++) row[x] = in[x / sheet.scale];
    }
    out = scaled.data();
  }

  wchar_t path[MAX_PATH] = L"map.png";
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  const std::wstring filter = Str(IDS_FILTER_PNG) + L'\0' + L"*.png" + L'\0' +
                              Str(IDS_FILTER_ALL) + L'\0' + L"*.*" + L'\0';
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&ofn)) return false;

  size_t length = 0;
  uint8_t* png = pf_png_encode(out, ow, oh, &length);
  if (!png) { note = Str(IDS_PNG_FAILED); return false; }
  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  bool ok = file != INVALID_HANDLE_VALUE;
  if (ok) {
    DWORD written = 0;
    ok = WriteFile(file, png, DWORD(length), &written, nullptr) && written == length;
    CloseHandle(file);
  }
  pf_buffer_free(png);
  note = ok ? Format(IDS_PNG_WROTE, path, ow, oh) : Str(IDS_PNG_FAILED);
  return ok;
}

}  // namespace pfwin

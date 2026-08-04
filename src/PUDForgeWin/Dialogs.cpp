// The modal property sheets: map properties and players.
//
// Both are .rc templates driven from here, and both edit the map through the
// same C ABI the canvas uses, so nothing about the map is decided in this file.
// What is decided here is when to checkpoint and what to say when a value is
// refused.
//
// Cancel means cancel: values are read out of the map on open, held in a local
// struct, and written back in one checkpointed batch on OK. Editing live and
// undoing on Cancel would look the same until the user pressed Ctrl+Z and lost
// an edit they made before opening it.

#include "Dialogs.hpp"

#include <commctrl.h>
#include <windowsx.h>   // GET_X_LPARAM, for the lists' context menus

#include <algorithm>
#include <cstdio>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "Blit.hpp"
// The quick pick is a search over the units dock's own list, so it takes that
// list's order rather than inventing a second one. Included here rather than in
// the header: Docks.cpp includes this file, and the two meeting in the headers
// would be a cycle.
#include "Docks.hpp"
#include "Form.hpp"
#include "GameData.hpp"
#include "Icons.hpp"
#include "PaletteGrid.hpp"
#include "Strings.hpp"
#include "Tools.hpp"   // StatsPageProc, the statistics tab's page
#include "resource.h"
#include "strings.h"

namespace pfwin {

// The slot list is the core's answer, cached: pf_player_is_supported is a table
// lookup but this is asked once per drawn row.
const std::vector<int>& PlayerSlots() {
  static const std::vector<int> slots = [] {
    std::vector<int> out;
    for (int i = 0; i < 16; i++) {
      if (pf_player_is_supported(i)) out.push_back(i);
    }
    return out;
  }();
  return slots;
}

int SlotForRow(int row) {
  const std::vector<int>& slots = PlayerSlots();
  if (row < 0 || row >= int(slots.size())) return -1;
  return slots[size_t(row)];
}

int RowForSlot(int slot) {
  const std::vector<int>& slots = PlayerSlots();
  for (size_t i = 0; i < slots.size(); i++) {
    if (slots[i] == slot) return int(i);
  }
  return 0;
}

void CentreOnScreen(HWND window) {
  // Children stay where their parent put them: the pages of the tabbed map
  // window share this handler, and a page that centred itself would leap out of
  // its own tab.
  if (!window || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return;
  RECT frame;
  GetWindowRect(window, &frame);
  const int w = frame.right - frame.left, h = frame.bottom - frame.top;
  HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) return;
  const RECT& work = info.rcWork;
  const int x = work.left + ((work.right - work.left) - w) / 2;
  const int y = work.top + ((work.bottom - work.top) - h) / 2;
  SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

namespace {

const UINT kTilesets[] = {IDS_TILESET_FOREST, IDS_TILESET_WINTER,
                          IDS_TILESET_WASTELAND, IDS_TILESET_SWAMP};

/// `OWNR` values, in the order PUDDraft listed them.
struct OwnerChoice { int value; UINT label; };
const OwnerChoice kOwners[] = {
    {PF_OWNER_NOBODY, IDS_OWNER_NOBODY},
    {PF_OWNER_HUMAN, IDS_OWNER_HUMAN},
    {PF_OWNER_COMPUTER, IDS_OWNER_COMPUTER},
    {PF_OWNER_PASSIVE_COMPUTER, IDS_OWNER_PASSIVE},
    {PF_OWNER_RESCUE_PASSIVE, IDS_OWNER_RESCUE_PASSIVE},
    {PF_OWNER_RESCUE_ACTIVE, IDS_OWNER_RESCUE_ACTIVE},
};
const UINT kRaces[] = {IDS_RACE_HUMAN_NAME, IDS_RACE_ORC_NAME, IDS_RACE_NEUTRAL};

/// A PUD is square and one of these four.
const int kMapSizes[] = {32, 64, 96, 128};

// --------------------------------------------------------- master and detail
//
// Four sheets share one shape: a list of subjects down the left that never
// moves, and everything about the selected one as labelled controls on the
// right. A wide table shows how the rows compare, which is a question a mapper
// asks rarely; "what does this unit cost" is the one they ask constantly, and a
// table answers it by making them count columns.
//
// The detail scrolls vertically and only vertically: a form that scrolls
// sideways is a form whose labels have wandered off the screen.

/// Put a form where the template's placeholder is, and take the placeholder
/// away. The layout stays in the .rc, where the rest of the sheet's layout is,
/// rather than being arithmetic in here.
HWND PlaceForm(Form& form, HWND dialog, int placeholder, int control_id) {
  HWND slot = GetDlgItem(dialog, placeholder);
  RECT rect{};
  GetWindowRect(slot, &rect);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
  DestroyWindow(slot);
  HWND hwnd = form.Create(dialog, GetModuleHandleW(nullptr), control_id);
  MoveWindow(hwnd, rect.left, rect.top, rect.right - rect.left,
             rect.bottom - rect.top, TRUE);
  return hwnd;
}

/// A field's name as a label a person reads: `buildTime` becomes "Build Time".
///
/// The translation is the core's, because it is a fact about the format and
/// three front-ends spelling the same field three ways is the drift the
/// layering rule exists to stop.
std::wstring FieldLabel(const char* name) {
  if (!name) return L"?";
  char label[128] = {};
  if (pf_field_label(name, label, int(sizeof(label))) <= 0) return FromUtf8(name);
  return FromUtf8(label);
}

/// Fill an owner-drawn listbox and put the selection on a row.
void FillList(HWND list, const std::vector<std::wstring>& rows, int selected) {
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (const std::wstring& row : rows) {
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
  }
  SendMessageW(list, LB_SETCURSEL, WPARAM(selected), 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

/// Which row of an owner-drawn list a right-click landed on.
///
/// `screen` is WM_CONTEXTMENU's own coordinates, including the (-1, -1) the
/// keyboard menu key sends — there the row meant is the selected one. Returns
/// -1 when the click missed the rows.
int ListRowAt(HWND list, POINT screen) {
  if (screen.x == -1 && screen.y == -1) {
    return int(SendMessageW(list, LB_GETCURSEL, 0, 0));
  }
  POINT client = screen;
  ScreenToClient(list, &client);
  const DWORD hit = DWORD(SendMessageW(list, LB_ITEMFROMPOINT, 0,
                                       MAKELPARAM(client.x, client.y)));
  // The high word is set when the point is outside the client area or past the
  // last item; the index it hands back then is the nearest row, not a hit.
  if (HIWORD(hit) != 0) return -1;
  return int(LOWORD(hit));
}

/// Where a list's context menu should appear, given WM_CONTEXTMENU's point.
POINT ListMenuAt(HWND list, POINT screen, int row) {
  if (screen.x != -1 || screen.y != -1) return screen;
  // The keyboard menu key: over the row it is about, so the menu names
  // something the pointer is nowhere near but the caret is on.
  RECT item{};
  if (row >= 0 && SendMessageW(list, LB_GETITEMRECT, WPARAM(row),
                               reinterpret_cast<LPARAM>(&item))) {
    POINT at{item.left + (item.right - item.left) / 2,
             item.top + (item.bottom - item.top) / 2};
    ClientToScreen(list, &at);
    return at;
  }
  RECT rc{};
  GetClientRect(list, &rc);
  POINT at{rc.right / 2, rc.bottom / 2};
  ClientToScreen(list, &at);
  return at;
}

/// Offer to put a whole list row back to the game's own table.
///
/// The form beside the list already offers this a field at a time, which is the
/// right grain when you know which field you changed and the wrong one when what
/// you know is that this unit should not have been touched at all. Only offered
/// where there is something to put back.
/// @return whether the user asked for it
bool OfferRowReset(HWND owner, HWND list, POINT screen, int row,
                   bool changed, const std::wstring& name) {
  if (row < 0 || !changed) return false;
  HMENU menu = CreatePopupMenu();
  if (!menu) return false;
  AppendMenuW(menu, MF_STRING, 1, Format(IDS_ROW_RESET, name.c_str()).c_str());
  const POINT at = ListMenuAt(list, screen, row);
  const int picked = int(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        at.x, at.y, 0, owner, nullptr));
  DestroyMenu(menu);
  return picked == 1;
}

/// A player has no artwork, so its colour is its icon — the same colour the
/// canvas draws its units in, which is how a mapper already knows the slots
/// apart.
std::array<Icon, 16> PlayerIcons() {
  std::array<Icon, 16> out;
  for (int i = 0; i < 16; i++) out[i] = FlatIcon(pf_player_color(i));
  return out;
}

/// The listed slots, named, with the race each one plays.
///
/// A slot is a colour *and* a side, and picking the wrong one is how a base ends
/// up unbuildable. `races` is the sheet's working copy while it is open, so the
/// list follows an edit rather than showing what the map said on open.
std::vector<std::wstring> PlayerRows(const int* races) {
  static const UINT kRaceNames[] = {IDS_RACE_HUMAN_NAME, IDS_RACE_ORC_NAME,
                                    IDS_RACE_NEUTRAL};
  std::vector<std::wstring> out;
  for (int i : PlayerSlots()) {
    const char* name = pf_player_name(i);
    std::wstring row = name ? FromUtf8(name) : Format(IDS_PLAYER_N, i + 1);
    const int race = races ? races[i] : PF_RACE_NEUTRAL;
    const std::wstring race_name = Str(kRaceNames[race >= 0 && race < 3 ? race : 2]);
    // The core names a slot "Player 1 (Red)"; the race goes inside those
    // brackets, because both answer the same question about the slot. Appended
    // when there are no brackets to slip into.
    if (!row.empty() && row.back() == L')') {
      row.insert(row.size() - 1, Format(IDS_PLAYER_RACE_SUFFIX, race_name.c_str()));
    } else if (row != race_name) {
      // The neutral slot is called "Neutral" and plays the neutral race, and
      // "Neutral Neutral" says it once too often.
      row += L" " + race_name;
    }
    out.push_back(std::move(row));
  }
  return out;
}

/// The choices a column of owners offers, in the order PUDDraft listed them.
std::vector<std::wstring> OwnerChoices() {
  std::vector<std::wstring> out;
  for (const OwnerChoice& choice : kOwners) out.push_back(Str(choice.label));
  return out;
}

std::vector<std::wstring> RaceChoices() {
  std::vector<std::wstring> out;
  for (UINT name : kRaces) out.push_back(Str(name));
  return out;
}

/// Every AI script the game has, numbered. The number is what a PUD stores,
/// so it leads.
std::vector<std::wstring> AiChoices() {
  std::vector<std::wstring> out;
  for (int i = 0; i < pf_ai_name_count(); i++) {
    const char* name = pf_ai_name(i);
    out.push_back(Format(IDS_AI_ROW, i, name ? FromUtf8(name).c_str() : L""));
  }
  return out;
}

// ------------------------------------------------------- lists with icons
//
// A name is the slowest way to recognise a unit, so the command-button icon is
// drawn beside it. The lists stay stock listboxes filled with LB_ADDSTRING:
// LBS_OWNERDRAWFIXED with LBS_HASSTRINGS changes how a row is painted and
// nothing else, so selection, keyboard navigation, multi-select and LB_GETTEXT
// all keep working.

/// Which `UGRD` field holds the command-button frame. Looked up by name once,
/// because the section's layout is the core's business and not a number to be
/// written down twice here.
int UpgradeIconField() {
  static const int found = [] {
    for (int f = 0; f < pf_ugrd_field_count(); f++) {
      const char* name = pf_ugrd_field_name(f);
      if (name && std::string(name) == "icon") return f;
    }
    return -1;
  }();
  return found;
}

int ComboIndexForOwner(int owner) {
  for (int i = 0; i < int(std::size(kOwners)); i++) {
    if (kOwners[i].value == owner) return i;
  }
  return 0;
}

void FillCombo(HWND dialog, int control, const UINT* items, int count,
               int selected) {
  HWND combo = GetDlgItem(dialog, control);
  SendMessageW(combo, CB_RESETCONTENT, 0, 0);
  for (int i = 0; i < count; i++) {
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(Str(items[i]).c_str()));
  }
  SendMessageW(combo, CB_SETCURSEL, WPARAM(selected), 0);
}

int ReadNumber(HWND dialog, int control) {
  BOOL translated = FALSE;
  const UINT value = GetDlgItemInt(dialog, control, &translated, FALSE);
  return translated ? int(value) : 0;
}

// ----------------------------------------------------------- component files
//
// Raw section payloads with no header of any kind: a `.un` is literally the
// bytes of a `UDTA` section. Length is the only thing that identifies one,
// which is why a wrong file is refused rather than written into a map.
//
// They sit on the page that shows the section they carry, where "which section"
// is answered by which page you are on — as two Tools items they did not say.
//
// Unlike everything else on these pages, importing takes effect at once and
// takes its own undo step: making it cancellable would mean keeping a second
// copy of the entire table for the length of the window.

/// The extension a component file of each kind is known by.
const wchar_t* ComponentExtension(int component) {
  switch (component) {
    case PF_COMPONENT_UDTA: return L"un";
    case PF_COMPONENT_UGRD: return L"up";
    case PF_COMPONENT_ALOW: return L"alo";
    default: return L"bin";
  }
}

bool ReadWholeFile(const wchar_t* path, std::vector<uint8_t>& out) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
            size.QuadPart < (1 << 24);
  if (ok) {
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = out.empty() ||
         (ReadFile(file, out.data(), DWORD(out.size()), &read, nullptr) &&
          read == out.size());
  }
  CloseHandle(file);
  return ok;
}

std::wstring NameOfPath(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

/// Read a section out of a component file into the map.
/// @param expect which section this page is about, so a `.up` dropped on the
///        units page is turned down rather than quietly replacing `UGRD`
/// @return whether the map changed
bool ImportSection(HWND page, pf_map* map, int expect) {
  if (!map) return false;
  wchar_t path[MAX_PATH] = {};
  const std::wstring pattern = std::wstring(L"*.") + ComponentExtension(expect);
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = page;
  const std::wstring filter = Str(IDS_FILTER_COMPONENTS) + L'\0' + pattern +
                              L'\0' + Str(IDS_FILTER_ALL) + L'\0' + L"*.*" + L'\0';
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&ofn)) return false;

  std::vector<uint8_t> bytes;
  if (!ReadWholeFile(path, bytes) || pf_component_kind(bytes.size()) != expect) {
    MessageBoxW(page,
                Format(IDS_COMPONENT_REFUSED, NameOfPath(path).c_str(),
                       int(bytes.size())).c_str(),
                Str(IDS_SECTION_IMPORT).c_str(), MB_OK | MB_ICONWARNING);
    return false;
  }
  pf_map_checkpoint(map);
  const bool ok =
      pf_map_import_component(map, bytes.data(), bytes.size()) == PF_OK;
  if (!ok) pf_map_undo(map);
  MessageBoxW(page,
              ok ? Format(IDS_COMPONENT_IMPORTED, NameOfPath(path).c_str()).c_str()
                 : Format(IDS_COMPONENT_REFUSED, NameOfPath(path).c_str(),
                          int(bytes.size())).c_str(),
              Str(IDS_SECTION_IMPORT).c_str(),
              MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
  return ok;
}

/// Write a section out to a component file.
void ExportSection(HWND page, pf_map* map, int component) {
  if (!map) return;
  size_t length = 0;
  uint8_t* bytes = pf_map_export_component(map, component, &length);
  if (!bytes) {
    MessageBoxW(page, Str(IDS_COMPONENT_ABSENT).c_str(),
                Str(IDS_SECTION_EXPORT).c_str(), MB_OK | MB_ICONINFORMATION);
    return;
  }
  const wchar_t* ext = ComponentExtension(component);
  wchar_t path[MAX_PATH] = {};
  wcscpy(path, L"section.");
  wcscat(path, ext);
  const std::wstring pattern = std::wstring(L"*.") + ext;
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = page;
  const std::wstring filter = Str(IDS_FILTER_COMPONENTS) + L'\0' + pattern +
                              L'\0' + Str(IDS_FILTER_ALL) + L'\0' + L"*.*" + L'\0';
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = ext;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameW(&ofn)) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
      DWORD written = 0;
      ok = WriteFile(file, bytes, DWORD(length), &written, nullptr) &&
           written == length;
      CloseHandle(file);
    }
    MessageBoxW(page,
                ok ? Format(IDS_COMPONENT_EXPORTED, NameOfPath(path).c_str()).c_str()
                   : Format(IDS_CANNOT_OPEN, path, L"").c_str(),
                Str(IDS_SECTION_EXPORT).c_str(),
                MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
  }
  pf_buffer_free(bytes);
}

// ------------------------------------------------------------ map properties
//
// The description, the tileset — and the size, which is to say resizing. It was
// a modal of its own until the six sheets became one window, and then it was the
// only thing still opening on its own for a question this page already answered:
// it showed the size and could not change it.
//
// ApplyResize is not just another Apply*Sheet because it changes the *grid*
// rather than a value in it, so the caller has to throw both selections away and
// tell the views the shape changed. `MapSheetsOutcome` is how it says so.

struct MapSheet {
  pf_map* map = nullptr;
  std::wstring description;
  int tileset = 0;
  bool changed = false;

  /// The size the page is offering, and where the old map lands in the new
  /// grid — 0/1/2 per axis. Centre by default, which is what a symmetric melee
  /// map wants.
  int size = 64;
  int anchor_x = 1, anchor_y = 1;
  /// What ApplyResize did, for the caller. Here rather than returned because a
  /// resize the core refuses changed nothing and still has something to say.
  int dropped_units = 0;
  bool resize_failed = false;
};

/// Can the `DESC` field hold this one character?
///
/// Asked of the core, because the answer is a fact about the file format. A lone
/// surrogate half is not valid UTF-8 and so is turned down, which is right: no
/// character outside the BMP is storable.
bool DescriptionHolds(wchar_t ch) {
  return pf_map_description_bytes(ToUtf8(std::wstring(1, ch)).c_str()) > 0;
}

/// Where the old map's top-left corner lands in the new grid.
void ResizeOffset(const MapSheet& sheet, int& ox, int& oy) {
  const int slack = sheet.size - pf_map_width(sheet.map);
  const int slack_y = sheet.size - pf_map_height(sheet.map);
  // Rounded, so an odd slack lands one tile past centre rather than short.
  ox = (slack * sheet.anchor_x + 1) / 2;
  oy = (slack_y * sheet.anchor_y + 1) / 2;
  if (sheet.anchor_x == 0) ox = 0;
  if (sheet.anchor_y == 0) oy = 0;
}

/// How many units would no longer fit whole after the resize.
int UnitsLostTo(const MapSheet& sheet) {
  int ox = 0, oy = 0;
  ResizeOffset(sheet, ox, oy);
  int lost = 0;
  const int count = pf_map_unit_count(sheet.map);
  for (int i = 0; i < count; i++) {
    pf_unit u{};
    if (pf_map_unit(sheet.map, i, &u) != PF_OK) continue;
    int fw = 1, fh = 1;
    pf_map_unit_footprint(sheet.map, u.type, &fw, &fh);
    const int x = int(u.x) + ox, y = int(u.y) + oy;
    if (x < 0 || y < 0 || x + fw > sheet.size || y + fh > sheet.size) lost++;
  }
  return lost;
}

/// What resizing would do, in words.
///
/// Cropping throws work away and there is no way to see that coming from a
/// number in a dropdown, so it is said before rather than reported after.
void RefreshResizeNote(HWND dialog, const MapSheet& sheet) {
  const int from = pf_map_width(sheet.map);
  std::wstring line;
  if (sheet.size == from) {
    line = Str(IDS_RESIZE_SAME);
  } else if (sheet.size > from) {
    line = Format(IDS_RESIZE_PADS, sheet.size - from);
  } else {
    const int lost = UnitsLostTo(sheet);
    line = lost == 0
        ? Format(IDS_RESIZE_CROPS_SAFE, from - sheet.size)
        : Format(Plural(lost, IDS_RESIZE_CROPS_ONE, IDS_RESIZE_CROPS_MANY),
                 from - sheet.size, lost);
  }
  SetDlgItemTextW(dialog, IDC_RESIZE_NOTE, line.c_str());

  // War2mod keeps its trigger bytecode in OILM, which is not a per-tile grid.
  // Resizing remaps it tile by tile and scrambles it, so say so while there is
  // still a Cancel button.
  const int oil = pf_map_oil_map_used(sheet.map);
  SetDlgItemTextW(dialog, IDC_RESIZE_WARNING,
                  oil > 0 ? Str(IDS_RESIZE_OILM_WARNING).c_str() : L"");

  // This page shares the window's OK with five others, so what goes dead when
  // the size is unchanged is the anchor — the only control that means anything
  // once there is nothing to anchor.
  for (int i = 0; i < 9; i++) {
    EnableWindow(GetDlgItem(dialog, IDC_RESIZE_ANCHOR + i), sheet.size != from);
  }
}

void RefreshDescriptionRoom(HWND dialog) {
  wchar_t text[256] = {};
  GetDlgItemTextW(dialog, IDC_MAP_DESCRIPTION, text, 256);
  const int used = pf_map_description_bytes(ToUtf8(text).c_str());
  const int most = pf_map_description_max();
  // Bytes, which for a storable character is also characters: DESC is a fixed
  // 31 bytes of code page 437, so counting UTF-16 units would say "31 of 31"
  // while the file kept half the text. -1 means a path that does not go through
  // WM_CHAR let something unstorable in.
  SetDlgItemTextW(dialog, IDC_MAP_DESC_LEFT,
                  used < 0 ? Str(IDS_DESC_UNSTORABLE).c_str()
                           : Format(IDS_DESC_ROOM, used, most).c_str());
}

/// Keeps out of the box what the file cannot keep.
///
/// EM_LIMITTEXT counts UTF-16 units, which is the wrong unit twice over: an
/// accented character costs one byte in the file, and a character with no byte
/// at all costs the whole edit. Once nothing unstorable can be typed the two
/// counts agree.
LRESULT CALLBACK DescriptionProc(HWND hwnd, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
  HWND dialog = reinterpret_cast<HWND>(ref);
  switch (message) {
    case WM_CHAR:
      // Backspace, Ctrl+V and the rest arrive here as control characters and
      // belong to the edit control, not to us.
      if (wparam >= L' ' && !DescriptionHolds(wchar_t(wparam))) {
        MessageBeep(MB_OK);
        SetDlgItemTextW(dialog, IDC_MAP_DESC_LEFT,
                        Str(IDS_DESC_REFUSED_CHAR).c_str());
        return 0;
      }
      break;

    case WM_PASTE: {
      // Pasting is how most unstorable text would arrive, so it is filtered
      // rather than refused: dropping two characters a person did not realise
      // were there beats losing the sentence they meant to paste. A clipboard
      // we cannot open is a paste that does not happen.
      if (!OpenClipboard(hwnd)) return 0;
      std::wstring kept;
      bool dropped = false;
      if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
          for (const wchar_t* p = text; *p; p++) {
            if (*p >= L' ' && DescriptionHolds(*p)) kept.push_back(*p);
            else dropped = true;
          }
          GlobalUnlock(handle);
        }
      }
      CloseClipboard();

      // EM_REPLACESEL is not bound by EM_LIMITTEXT, so the room the paste has is
      // worked out here: what the field holds, less what is in the box, plus
      // whatever the paste is about to replace.
      DWORD from = 0, to = 0;
      SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&from),
                   reinterpret_cast<LPARAM>(&to));
      const int room = pf_map_description_max() -
                       GetWindowTextLengthW(hwnd) + int(to - from);
      if (int(kept.size()) > room) kept.resize(size_t(room < 0 ? 0 : room));
      // Nothing to give means nothing taken away: a paste of unstorable text
      // must not delete the selection it landed on.
      if (!kept.empty()) {
        SendMessageW(hwnd, EM_REPLACESEL, TRUE,
                     reinterpret_cast<LPARAM>(kept.c_str()));
      }
      if (dropped) {
        MessageBeep(MB_OK);
        SetDlgItemTextW(dialog, IDC_MAP_DESC_LEFT,
                        Str(IDS_DESC_REFUSED_CHAR).c_str());
      }
      return 0;
    }
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

INT_PTR CALLBACK MapProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  MapSheet* sheet = reinterpret_cast<MapSheet*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<MapSheet*>(lparam);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, LONG_PTR(sheet));
      SendDlgItemMessageW(dialog, IDC_MAP_DESCRIPTION, EM_LIMITTEXT,
                          WPARAM(pf_map_description_max()), 0);
      SetWindowSubclass(GetDlgItem(dialog, IDC_MAP_DESCRIPTION), DescriptionProc,
                        1, reinterpret_cast<DWORD_PTR>(dialog));
      // EM_LIMITTEXT does not apply to text put in with WM_SETTEXT, which is
      // what shows the one shipped map whose description fills all 32 bytes in
      // full instead of one character short.
      SetDlgItemTextW(dialog, IDC_MAP_DESCRIPTION, sheet->description.c_str());
      FillCombo(dialog, IDC_MAP_TILESET, kTilesets, 4, sheet->tileset);
      SetDlgItemTextW(dialog, IDC_MAP_SIZE,
                      Format(IDS_MAP_SIZE_NOW, pf_map_width(sheet->map),
                             pf_map_height(sheet->map)).c_str());

      HWND sizes = GetDlgItem(dialog, IDC_RESIZE_SIZE);
      for (int i = 0; i < int(std::size(kMapSizes)); i++) {
        const std::wstring label = Format(IDS_GEN_SIZE, kMapSizes[i], kMapSizes[i]);
        SendMessageW(sizes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (kMapSizes[i] == sheet->size) SendMessageW(sizes, CB_SETCURSEL, WPARAM(i), 0);
      }
      for (int i = 0; i < 9; i++) {
        CheckDlgButton(dialog, IDC_RESIZE_ANCHOR + i,
                       (i % 3 == sheet->anchor_x && i / 3 == sheet->anchor_y)
                           ? BST_CHECKED : BST_UNCHECKED);
      }
      RefreshResizeNote(dialog, *sheet);
      RefreshDescriptionRoom(dialog);
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDC_MAP_DESCRIPTION && HIWORD(wparam) == EN_CHANGE) {
        RefreshDescriptionRoom(dialog);
        return TRUE;
      }
      // The size and the anchor write through as they change, unlike the two
      // controls above them: what the page says the resize *would* do has to
      // follow both, and there is nothing to harvest later.
      if (id == IDC_RESIZE_SIZE && HIWORD(wparam) == CBN_SELCHANGE) {
        const int at = int(SendDlgItemMessageW(dialog, IDC_RESIZE_SIZE,
                                               CB_GETCURSEL, 0, 0));
        if (at >= 0 && at < int(std::size(kMapSizes))) sheet->size = kMapSizes[at];
        RefreshResizeNote(dialog, *sheet);
        return TRUE;
      }
      if (id >= IDC_RESIZE_ANCHOR && id < IDC_RESIZE_ANCHOR + 9) {
        const int at = id - IDC_RESIZE_ANCHOR;
        sheet->anchor_x = at % 3;
        sheet->anchor_y = at / 3;
        RefreshResizeNote(dialog, *sheet);
        return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

/// Read the two controls back into the sheet.
///
/// A page has no OK of its own — the window it lives in does — so this is called
/// when that window is closing. Neither the description nor the tileset is worth
/// harvesting on every keystroke.
void HarvestMapPage(HWND page, MapSheet& sheet) {
  if (!page) return;
  wchar_t text[256] = {};
  GetDlgItemTextW(page, IDC_MAP_DESCRIPTION, text, 256);
  const int tileset =
      int(SendDlgItemMessageW(page, IDC_MAP_TILESET, CB_GETCURSEL, 0, 0));
  sheet.changed = sheet.description != text || sheet.tileset != tileset;
  sheet.description = text;
  sheet.tileset = tileset;
}

// ------------------------------------------------------------------ players

struct PlayerRow {
  int owner = PF_OWNER_NOBODY;
  int race = PF_RACE_HUMAN;
  int gold = 0, lumber = 0, oil = 0;
  int ai = 0;
};

struct PlayerSheet {
  pf_map* map = nullptr;
  /// Where the game is, for the button that opens the AI listing. Borrowed and
  /// allowed to be null: the client runs without an install, and then the button
  /// says so rather than being missing.
  GameData* game = nullptr;
  /// The client's own button artwork, for the AI button below the list.
  /// Borrowed and allowed to be null, on the same terms as `game`.
  const UiIcons* ui = nullptr;
  HINSTANCE instance = nullptr;
  PlayerRow rows[16];
  Form form;
  std::array<Icon, 16> icons = PlayerIcons();
  int at = 0;              ///< the slot the list is on
  bool changed = false;

  /// The page itself, so the form's callbacks can reach the list beside them.
  HWND page = nullptr;
  /// The races as the list is currently showing them, so a change made on the
  /// form can be spotted and the list refilled.
  int shown_races[16] = {};
  /// The sixteen races out of the working copy, for PlayerRows.
  void RacesInto(int* out) const {
    for (int i = 0; i < 16; i++) out[i] = rows[i].race;
  }

  /// The game's script table, opened once and kept while the page is up.
  ///
  /// Reading it is a walk over `rez/ai.bin`, and the note under the dropdown
  /// asks for a fresh disassembly every time the slot or the script changes —
  /// which is every click on the list.
  pf_ai_scripts* scripts = nullptr;
  bool tried_scripts = false;

  const pf_ai_scripts* Scripts() {
    if (!tried_scripts) {
      tried_scripts = true;
      if (game) scripts = game->OpenAiScripts();
    }
    return scripts;
  }

  ~PlayerSheet() { if (scripts) pf_ai_scripts_free(scripts); }
};

/// Refill the list, but only when a race on it has actually moved.
///
/// Refilling costs the selection and the scroll, and the race is the only thing
/// on the form that the list shows.
void RefreshPlayerList(PlayerSheet* sheet) {
  if (!sheet || !sheet->page) return;
  int now[16] = {};
  sheet->RacesInto(now);
  bool same = true;
  for (int i = 0; i < 16; i++) {
    if (now[i] != sheet->shown_races[i]) same = false;
    sheet->shown_races[i] = now[i];
  }
  if (same) return;
  FillList(GetDlgItem(sheet->page, IDC_PLAYER_LIST), PlayerRows(now),
           RowForSlot(sheet->at));
}

/// How many units each slot owns, so a person can tell a live slot from a dead
/// one without closing the sheet and looking at the map.
int UnitsOwnedBy(pf_map* map, int player) {
  int count = 0;
  for (int i = 0, n = pf_map_unit_count(map); i < n; i++) {
    pf_unit unit{};
    if (pf_map_unit(map, i, &unit) == PF_OK && unit.owner == player) count++;
  }
  return count;
}

/// The fields of a slot, in the order a mapper reads one: who plays it, what
/// they are, what they start with, and what runs them.
enum PlayerField {
  kPlayerOwner, kPlayerRace, kPlayerGold, kPlayerLumber, kPlayerOil, kPlayerAi,
  /// Not a field of the slot: what the script the row above names actually
  /// does. Read-only, and written by nothing.
  kPlayerAiText
};

void BuildPlayerForm(PlayerSheet* sheet) {
  std::vector<Form::Row> rows;
  rows.push_back({kPlayerOwner, Str(IDS_COL_CONTROLLED_BY), Form::Kind::kChoice,
                  OwnerChoices()});
  rows.push_back({kPlayerRace, Str(IDS_COL_RACE), Form::Kind::kChoice, RaceChoices()});
  // The format stores these as 16-bit words times 10, so the ceiling is the
  // format's rather than a number picked here.
  rows.push_back({kPlayerGold, Str(IDS_COL_GOLD), Form::Kind::kNumber, {}, 0, 655350});
  rows.push_back({kPlayerLumber, Str(IDS_COL_LUMBER), Form::Kind::kNumber, {}, 0, 655350});
  rows.push_back({kPlayerOil, Str(IDS_COL_OIL), Form::Kind::kNumber, {}, 0, 655350});
  // The AI row carries the button that opens the game's own listing, beside the
  // dropdown it is about: a PUD stores an AI script *number*, so "12 Orc 8" says
  // nothing about what this player will do. It used to be a captioned button at
  // the foot of the page, a long way from the field it explained.
  Form::Row ai{kPlayerAi, Str(IDS_COL_AI), Form::Kind::kChoice, AiChoices()};
  // The button opens the browser on the script the dropdown is showing, which
  // is the whole reason it sits on this row rather than at the foot of the page.
  ai.side_icon = kIconProperties;
  ai.side_tip = Str(IDS_TIP_VIEW_AI);
  rows.push_back(std::move(ai));
  // What that number means, under the dropdown it belongs to. It was a button
  // to a window of its own, which is a long way to go to answer "and what does
  // that one do" while comparing two of them.
  Form::Row what{kPlayerAiText, L"", Form::Kind::kNote};
  what.note_rows = 5;
  rows.push_back(std::move(what));

  sheet->form.read = [sheet](int id) -> int64_t {
    const PlayerRow& player = sheet->rows[sheet->at];
    switch (id) {
      case kPlayerOwner: return ComboIndexForOwner(player.owner);
      case kPlayerRace: return std::max(0, std::min(player.race, 2));
      case kPlayerGold: return player.gold;
      case kPlayerLumber: return player.lumber;
      case kPlayerOil: return player.oil;
      case kPlayerAi: return player.ai;
      default: return 0;
    }
  };
  sheet->form.write = [sheet](int id, int64_t value) {
    PlayerRow& player = sheet->rows[sheet->at];
    switch (id) {
      case kPlayerOwner:
        if (value >= 0 && value < int64_t(std::size(kOwners))) {
          player.owner = kOwners[value].value;
        }
        return true;
      case kPlayerRace:
        if (value >= 0 && value <= 2) player.race = int(value);
        // The list names each slot's race, so it has to follow the change rather
        // than showing what the map said when the window opened.
        RefreshPlayerList(sheet);
        return true;
      case kPlayerGold: player.gold = int(value); return true;
      case kPlayerLumber: player.lumber = int(value); return true;
      case kPlayerOil: player.oil = int(value); return true;
      case kPlayerAi:
        if (value >= 0) player.ai = int(value);
        return true;
      default: return false;
    }
  };
  sheet->form.activate_side = [sheet](int id) {
    if (id != kPlayerAi || !sheet->game || !sheet->page) return;
    // On the number the dropdown is showing rather than at the top of the
    // listing: the note above says what it does in two lines, and this is where
    // somebody goes when two lines are not enough.
    ShowAiScripts(sheet->page, sheet->instance, *sheet->game,
                  sheet->rows[sheet->at].ai);
  };
  // The note's text, asked for whenever the form reloads — which is every
  // change of slot and every change of script.
  sheet->form.text = [sheet](int id) -> std::wstring {
    if (id != kPlayerAiText) return L"";
    return AiScriptText(sheet->Scripts(), sheet->rows[sheet->at].ai);
  };
  sheet->form.SetRows(std::move(rows));
}

/// Show a slot. The count of units it owns goes under the list, so a person
/// can tell a live slot from a dead one without closing the sheet.
void ShowPlayer(HWND dialog, PlayerSheet* sheet, int player) {
  sheet->at = pf_player_is_supported(player) ? player : SlotForRow(0);
  SetDlgItemTextW(dialog, IDC_PLAYER_NOTE,
                  Format(Plural(UnitsOwnedBy(sheet->map, sheet->at),
                                IDS_PLAYER_OWNS_ONE, IDS_PLAYER_OWNS_MANY),
                         UnitsOwnedBy(sheet->map, sheet->at)).c_str());
  sheet->form.Reload();
}

INT_PTR CALLBACK PlayersProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  PlayerSheet* sheet =
      reinterpret_cast<PlayerSheet*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      if (!sheet || wparam != IDC_PLAYER_LIST) return FALSE;
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      const int at = SlotForRow(int(item->itemID));
      return DrawIconRow(lparam, at >= 0 ? &sheet->icons[at] : nullptr);
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<PlayerSheet*>(lparam);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, LONG_PTR(sheet));
      sheet->page = dialog;
      // Beside the caption rather than instead of it: "Show this AI script…"
      // says what the drawing cannot. Before BuildPlayerForm, which is where the
      // rows and their companion buttons are made.
      sheet->form.SetUiIcons(sheet->ui);
      PlaceForm(sheet->form, dialog, IDC_PLAYER_FORM_SLOT, IDC_PLAYER_FORM);
      BuildPlayerForm(sheet);
      sheet->RacesInto(sheet->shown_races);
      // On the slot the caller asked for: opening the sheet on player 1 when you
      // pressed the button beside player 5 is a click and a hunt for nothing.
      FillList(GetDlgItem(dialog, IDC_PLAYER_LIST), PlayerRows(sheet->shown_races),
               RowForSlot(sheet->at));
      ShowPlayer(dialog, sheet, sheet->at);
      SetFocus(GetDlgItem(dialog, IDC_PLAYER_LIST));
      return FALSE;   // focus set by hand, above
    }

    case WM_COMMAND:
      if (!sheet) return FALSE;
      if (LOWORD(wparam) == IDC_PLAYER_LIST && HIWORD(wparam) == LBN_SELCHANGE) {
        ShowPlayer(dialog, sheet,
                   SlotForRow(int(SendDlgItemMessageW(dialog, IDC_PLAYER_LIST,
                                                      LB_GETCURSEL, 0, 0))));
        return TRUE;
      }
      // The AI listing is not raised from here any more: it is the AI row's
      // own companion button, and the form answers it.
      return FALSE;
  }
  return FALSE;
}

// Nothing to harvest when this page closes: Form::write puts every control
// straight into sheet->rows as it changes, which is also what makes switching
// slots and back not lose a typed number.

// ---------------------------------------------------------------- unit data

/// One editable cell of `UDTA`: a field, and which of its components.
struct UnitCell {
  int field = 0;
  int component = 0;
};

/// The `UDTA` sheet's working copy.
///
/// Edits are held in a map keyed by (field, unit, component) rather than a full
/// copy of the table, because the interesting fact about a map's `UDTA` is which
/// handful of values it changed — and holding only those is what makes "put the
/// game's own value back" a deletion rather than a lookup.
struct UnitSheet {
  pf_map* map = nullptr;
  IconCache* icons = nullptr;      ///< borrowed; null just leaves rows blank
  Form form;
  std::vector<UnitCell> cells;     ///< every per-unit (field, component)
  int at = 0;                      ///< the unit the list is on
  /// How many rows the form was last built for. Nearly every field covers all
  /// 110 units, but `secondMouseButton` covers 58, so the form's *shape* changes
  /// with the unit; rebuilding only when it does keeps the scroll position.
  int shape = -1;
  std::map<std::array<int, 3>, int64_t> edits;
  bool defaults = false;           ///< `useDefaultData`, a map-level flag
  bool defaults_before = false;

  /// What the game will read for this field.
  ///
  /// A map need not carry `UDTA` at all and most do not, so asking the map for a
  /// field it has no section for answers -1 — which is not what the game will
  /// read. 110 rows of -1 says the map is broken when it is merely ordinary.
  int64_t Stored(int field, int unit_id, int component) const {
    if (pf_map_has_unit_data(map)) {
      return pf_map_unit_field(map, field, unit_id, component);
    }
    return pf_udta_default_field(field, unit_id, component);
  }
  int64_t Value(int field, int unit_id, int component) const {
    const auto found = edits.find({field, unit_id, component});
    if (found != edits.end()) return found->second;
    return Stored(field, unit_id, component);
  }
  void Set(int field, int unit_id, int component, int64_t value) {
    if (Stored(field, unit_id, component) == value) {
      edits.erase({field, unit_id, component});
    } else {
      edits[{field, unit_id, component}] = value;
    }
  }

  /// Whether anything about this unit is not what the game would read.
  ///
  /// The same question the form asks row by row, asked about the whole unit, so
  /// the list can say which of a hundred and ten rows are worth opening. Not
  /// "was it edited in this window": a map that arrived with a changed Footman
  /// has a changed Footman before anybody touches it.
  bool Changed(int unit_id) const {
    for (const UnitCell& cell : cells) {
      if (unit_id >= pf_udta_field_units(cell.field)) continue;
      if (Value(cell.field, unit_id, cell.component) !=
          pf_udta_default_field(cell.field, unit_id, cell.component)) {
        return true;
      }
    }
    return false;
  }

  /// Put every field of one unit back to the game's own table.
  /// @return whether anything moved
  bool Reset(int unit_id) {
    bool any = false;
    for (const UnitCell& cell : cells) {
      if (unit_id >= pf_udta_field_units(cell.field)) continue;
      const int64_t want =
          pf_udta_default_field(cell.field, unit_id, cell.component);
      if (Value(cell.field, unit_id, cell.component) == want) continue;
      Set(cell.field, unit_id, cell.component, want);
      any = true;
    }
    return any;
  }
};

/// Every per-unit field of `UDTA`, with one entry each for the two-component
/// ones. Which fields exist and what they mean is the core's to say; this only
/// turns each into the kind of editor it wants.
std::vector<UnitCell> UnitColumns() {
  std::vector<UnitCell> out;
  for (int field = 0; field < pf_udta_field_count(); field++) {
    if (pf_udta_field_units(field) <= 0) continue;   // not per-unit: skip
    const int components = std::max(1, pf_udta_field_components(field));
    for (int component = 0; component < components; component++) {
      out.push_back({field, component});
    }
  }
  return out;
}

// IDD_BITS and its proc are gone with the button that raised them: a flags
// field is thirty ticks on this page now, not a hex number that opened a second
// dialog to tick them in.

/// Row ids for the flag ticks, which are not cells but bits of one.
///
/// A form row is named by a single int and a tick is two numbers — which cell,
/// and which of its named bits. Packed rather than giving the form a second
/// identifier, because every other sheet's rows really are one number. The base
/// is comfortably past `cells.size()`, which the format fixes at forty-odd.
constexpr int kFlagRowBase = 1000;
constexpr int kFlagRowStride = 64;   ///< a flags field names at most 32 bits
int FlagRowId(int cell, int option) {
  return kFlagRowBase + cell * kFlagRowStride + option;
}
bool IsFlagRow(int id) { return id >= kFlagRowBase; }
int FlagRowCell(int id) { return (id - kFlagRowBase) / kFlagRowStride; }
int FlagRowOption(int id) { return (id - kFlagRowBase) % kFlagRowStride; }

/// The rows the form shows for one unit: every field that covers it, labelled
/// and given the kind of control it deserves.
///
/// The flags fields are the exception to "one field, one row": each named bit is
/// its own tick, where a mask used to be a hex number that opened a second
/// dialog. They go below the numbered fields rather than in amongst them,
/// because thirty-odd flags interleaved with a dozen numbers buries the numbers.
std::vector<Form::Row> UnitFormRows(const UnitSheet& sheet, int unit) {
  std::vector<Form::Row> rows;
  std::vector<Form::Row> flags;
  for (size_t i = 0; i < sheet.cells.size(); i++) {
    const UnitCell& at = sheet.cells[i];
    if (unit >= pf_udta_field_units(at.field)) continue;   // the field stops short
    Form::Row row;
    row.id = int(i);
    row.label = FieldLabel(pf_udta_field_name(at.field));
    if (pf_udta_field_components(at.field) > 1) {
      row.label += Str(at.component == 0 ? IDS_FIELD_WIDTH : IDS_FIELD_HEIGHT);
    }
    switch (pf_udta_field_kind(at.field)) {
      case PF_UDTA_BOOL:
        row.kind = Form::Kind::kCheck;
        break;
      case PF_UDTA_ENUM:
        row.kind = Form::Kind::kChoice;
        for (int o = 0; o < pf_udta_field_option_count(at.field); o++) {
          int value = 0;
          const char* option = pf_udta_field_option(at.field, o, &value);
          row.choices.push_back(option ? FromUtf8(option) : L"?");
        }
        break;
      case PF_UDTA_FLAGS: {
        // One tick per named bit, held back to the end of the form. Bits the
        // field does not name are left alone and unshown: they are somebody's
        // data, and the flag editor never wrote them either.
        for (int o = 0; o < pf_udta_field_option_count(at.field); o++) {
          int mask = 0;
          const char* option = pf_udta_field_option(at.field, o, &mask);
          Form::Row tick;
          tick.id = FlagRowId(int(i), o);
          tick.label = option ? FromUtf8(option) : L"?";
          tick.kind = Form::Kind::kFlag;
          flags.push_back(std::move(tick));
        }
        continue;   // the field itself gets no row of its own
      }
      default: {
        row.kind = Form::Kind::kNumber;
        // The field's own width is the ceiling: writing 70000 into a two-byte
        // field is not an edit, it is corruption.
        const int bytes = pf_udta_field_width(at.field);
        row.low = 0;
        row.high = bytes >= 4 ? 0x7FFFFFFF : (int64_t(1) << (8 * bytes)) - 1;
        break;
      }
    }
    rows.push_back(std::move(row));
  }
  for (Form::Row& tick : flags) rows.push_back(std::move(tick));
  return rows;
}

void BuildUnitForm(HWND, UnitSheet* sheet) {
  sheet->cells = UnitColumns();

  /// The bit a flag row stands for, as a mask.
  const auto mask_of = [sheet](int id, int& cell) {
    cell = FlagRowCell(id);
    int mask = 0;
    pf_udta_field_option(sheet->cells[size_t(cell)].field, FlagRowOption(id), &mask);
    return int64_t(uint32_t(mask));
  };

  sheet->form.read = [sheet, mask_of](int id) -> int64_t {
    if (IsFlagRow(id)) {
      int cell = 0;
      const int64_t mask = mask_of(id, cell);
      const UnitCell& at = sheet->cells[size_t(cell)];
      return (sheet->Value(at.field, sheet->at, at.component) & mask) ? 1 : 0;
    }
    const UnitCell& at = sheet->cells[size_t(id)];
    const int64_t value = sheet->Value(at.field, sheet->at, at.component);
    if (pf_udta_field_kind(at.field) != PF_UDTA_ENUM) return value;
    // A choice row wants the index of the option, not the option's value.
    for (int o = 0; o < pf_udta_field_option_count(at.field); o++) {
      int option = 0;
      pf_udta_field_option(at.field, o, &option);
      if (option == value) return o;
    }
    return 0;
  };

  sheet->form.write = [sheet, mask_of](int id, int64_t value) {
    if (IsFlagRow(id)) {
      int cell = 0;
      const int64_t mask = mask_of(id, cell);
      const UnitCell& at = sheet->cells[size_t(cell)];
      int64_t held = sheet->Value(at.field, sheet->at, at.component);
      // Only this bit moves. Whatever else the mask carried — including bits the
      // field does not name — is somebody's data and stays.
      held = value ? (held | mask) : (held & ~mask);
      sheet->Set(at.field, sheet->at, at.component, held);
      return true;
    }
    const UnitCell& at = sheet->cells[size_t(id)];
    if (pf_udta_field_kind(at.field) == PF_UDTA_ENUM) {
      int option = 0;
      if (value >= 0) pf_udta_field_option(at.field, int(value), &option);
      value = option;
    }
    sheet->Set(at.field, sheet->at, at.component, value);
    return true;
  };

  sheet->form.changed = [sheet, mask_of](int id) {
    if (IsFlagRow(id)) {
      int cell = 0;
      const int64_t mask = mask_of(id, cell);
      const UnitCell& at = sheet->cells[size_t(cell)];
      return (sheet->Value(at.field, sheet->at, at.component) & mask) !=
             (pf_udta_default_field(at.field, sheet->at, at.component) & mask);
    }
    const UnitCell& at = sheet->cells[size_t(id)];
    return sheet->Value(at.field, sheet->at, at.component) !=
           pf_udta_default_field(at.field, sheet->at, at.component);
  };

  // One row back to the game's number. "Put it back" used to mean clearing every
  // edit on the page, which is not what somebody who overshot one field wants —
  // and a flags row is one bit of one field, so only that bit moves.
  sheet->form.reset = [sheet, mask_of](int id) {
    if (IsFlagRow(id)) {
      int cell = 0;
      const int64_t mask = mask_of(id, cell);
      const UnitCell& at = sheet->cells[size_t(cell)];
      const int64_t held = sheet->Value(at.field, sheet->at, at.component);
      const int64_t want =
          (held & ~mask) |
          (pf_udta_default_field(at.field, sheet->at, at.component) & mask);
      if (want == held) return false;
      sheet->Set(at.field, sheet->at, at.component, want);
      return true;
    }
    const UnitCell& at = sheet->cells[size_t(id)];
    sheet->Set(at.field, sheet->at, at.component,
               pf_udta_default_field(at.field, sheet->at, at.component));
    return true;
  };
}

/// The units, named, with the five dead slots marked as such.
std::vector<std::wstring> UnitRows() {
  std::vector<std::wstring> out;
  for (int i = 0; i < PF_UNIT_COUNT; i++) {
    const char* name = pf_unit_name(i);
    std::wstring row = name ? FromUtf8(name) : L"?";
    if (pf_unit_is_unused(i)) row += Str(IDS_UNUSED_UNIT);
    out.push_back(std::move(row));
  }
  return out;
}

void ShowUnit(HWND dialog, UnitSheet* sheet, int unit) {
  sheet->at = std::max(0, std::min(unit, PF_UNIT_COUNT - 1));
  const char* name = pf_unit_name(sheet->at);
  SetDlgItemTextW(dialog, IDC_UDTA_SUBJECT, name ? FromUtf8(name).c_str() : L"");
  std::vector<Form::Row> rows = UnitFormRows(*sheet, sheet->at);
  if (int(rows.size()) == sheet->shape) {
    sheet->form.Reload();     // same shape: keep the scroll where it was
    return;
  }
  sheet->shape = int(rows.size());
  sheet->form.SetRows(std::move(rows));
}

INT_PTR CALLBACK UnitDataProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  UnitSheet* sheet = reinterpret_cast<UnitSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      if (!sheet || wparam != IDC_UDTA_UNITS) return FALSE;
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      const int at = int(item->itemID);
      const bool known = at >= 0 && at < PF_UNIT_COUNT;
      return DrawIconRow(lparam,
                         sheet->icons && known ? &sheet->icons->Unit(at) : nullptr,
                         known && sheet->Changed(at));
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<UnitSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      PlaceForm(sheet->form, dialog, IDC_UDTA_FORM_SLOT, IDC_UDTA_FORM);
      BuildUnitForm(dialog, sheet);
      // Opened on the unit the caller asked for: right-clicking a Footman in the
      // palette and landing on "Peasant" is a click followed by a hunt. The list
      // row is the unit id, because UnitRows walks the ids in order.
      const int start = std::max(0, std::min(sheet->at, PF_UNIT_COUNT - 1));
      FillList(GetDlgItem(dialog, IDC_UDTA_UNITS), UnitRows(), start);
      ShowUnit(dialog, sheet, start);
      CheckDlgButton(dialog, IDC_UDTA_DEFAULTS,
                     sheet->defaults ? BST_CHECKED : BST_UNCHECKED);
      SetFocus(GetDlgItem(dialog, IDC_UDTA_UNITS));
      return FALSE;   // focus set by hand, above
    }
    case WM_CONTEXTMENU: {
      HWND list = GetDlgItem(dialog, IDC_UDTA_UNITS);
      if (!sheet || reinterpret_cast<HWND>(wparam) != list) return FALSE;
      POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int at = ListRowAt(list, screen);
      if (at < 0 || at >= PF_UNIT_COUNT) return TRUE;
      const char* name = pf_unit_name(at);
      const std::wstring label = name ? FromUtf8(name) : L"";
      if (!OfferRowReset(dialog, list, screen, at, sheet->Changed(at), label)) {
        return TRUE;
      }
      if (sheet->Reset(at)) {
        // The row's colour and its star both come off, and the form is showing
        // the values that just moved if it is on this unit.
        InvalidateRect(list, nullptr, TRUE);
        if (sheet->at == at) sheet->form.Reload();
      }
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDC_UDTA_UNITS && HIWORD(wparam) == LBN_SELCHANGE) {
        ShowUnit(dialog, sheet,
                 int(SendDlgItemMessageW(dialog, IDC_UDTA_UNITS, LB_GETCURSEL, 0, 0)));
        return TRUE;
      }
      if (id == IDC_UDTA_DEFAULTS) {
        sheet->defaults = IsDlgButtonChecked(dialog, IDC_UDTA_DEFAULTS) == BST_CHECKED;
        return TRUE;
      }
      if (id == IDC_UDTA_IMPORT) {
        if (ImportSection(dialog, sheet->map, PF_COMPONENT_UDTA)) {
          // Every pending edit was a difference from the table that has just
          // been replaced, so keeping them would write values back over the
          // section the user asked for.
          sheet->edits.clear();
          sheet->shape = -1;
          ShowUnit(dialog, sheet, sheet->at);
        }
        return TRUE;
      }
      if (id == IDC_UDTA_EXPORT) {
        ExportSection(dialog, sheet->map, PF_COMPONENT_UDTA);
        return TRUE;
      }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ---------------------------------------------------------------- generator

/// Three octaves: a broad one deciding where the land is, and two finer ones
/// roughening the coast. The first one's seed is what "Another" changes.
///
/// The octave scales run in thousandths of a feature per tile. 5 is a single
/// continent filling the map and 400 is gravel; the useful band is narrow at
/// the broad end, which is why the land octave defaults near the floor.
constexpr int kOctaveLeast = 5;
constexpr int kOctaveMost = 400;

struct GenSheet {
  int size = 64;
  int tileset = 0;
  /// Percentages, because a trackbar is an integer and a share is not. Water and
  /// coast are shares of the whole map; forest and rock are shares of the *land*,
  /// so raising the water does not squeeze the land out from under them.
  int water = 26, coast = 11, forest = 35, rock = 14;
  /// The three octaves' scales, in thousandths of a feature per tile — a
  /// trackbar carries an integer and a scale is not one. Broadest first: the
  /// land octave decides where the continents are, and the two above it
  /// roughen what the water meets.
  int land = 30, coastline = 75, detail = 180;
  int clearings = 4, radius = 8;
  int seed = 12345;
  bool mines = true, starts = true;
  pf_map* preview = nullptr;
  int placed_mines = 0, placed_starts = 0;
  /// False until the controls have been filled. Setting an edit control's text
  /// raises EN_CHANGE, so without this the act of showing the defaults reads the
  /// boxes that have not been filled yet and adopts their zeros.
  bool ready = false;

  ~GenSheet() { if (preview) pf_map_free(preview); }

  pf_generate_params Params() const {
    pf_generate_params p = {};
    p.width = size;
    p.height = size;
    p.tileset = tileset;
    p.water = float(water) / 100.0f;
    p.coast = float(coast) / 100.0f;
    p.forest = float(forest) / 100.0f;
    p.rock = float(rock) / 100.0f;
    p.detail_seed = uint32_t(seed) ^ 0x5bd1u;
    p.detail_scale = 0.10f;
    p.clearings = clearings;
    p.clearing_radius = radius;
    return p;
  }
};

/// Rebuild the preview, and place on it whatever the checkboxes ask for, so the
/// preview shows the map Create would give rather than a different one.
void Regenerate(GenSheet& sheet) {
  if (sheet.preview) { pf_map_free(sheet.preview); sheet.preview = nullptr; }
  sheet.placed_mines = sheet.placed_starts = 0;

  // Weights stay fixed and only the scales move: the weights are what make an
  // octave a *detail* of the one below it rather than a second landmass, and a
  // reader with six knobs cannot tell which of the two is doing what.
  const pf_noise_layer layers[] = {
      {float(sheet.land) / 1000.0f, uint32_t(sheet.seed), 1.0f},
      {float(sheet.coastline) / 1000.0f, uint32_t(sheet.seed) * 2654435761u, 0.30f},
      {float(sheet.detail) / 1000.0f, uint32_t(sheet.seed) * 40503u + 7u, 0.10f},
  };
  const pf_generate_params params = sheet.Params();
  pf_status status = PF_OK;
  sheet.preview = pf_map_generate(&params, layers, 3, &status);
  if (!sheet.preview) return;

  if (sheet.mines) {
    sheet.placed_mines =
        pf_map_place_gold_mines(sheet.preview, std::max(2, sheet.clearings));
  }
  if (sheet.starts) {
    // A start location belongs to a player, and a player with no slot has
    // nowhere to stand: give the clearings' worth of slots to a human first.
    for (int p = 0; p < sheet.clearings && p < 16; p++) {
      pf_map_set_owner(sheet.preview, p, PF_OWNER_HUMAN);
    }
    sheet.placed_starts = pf_map_place_start_locations(sheet.preview);
  }
}

/// Say what was *short*, not what worked: a map with nowhere for a fourth base
/// should say so while there is still a slider to move.
void RefreshGenNote(HWND dialog, const GenSheet& sheet) {
  std::wstring line;
  if (!sheet.preview) {
    line = Str(IDS_GEN_NO_MAP);
  } else {
    const int wanted_mines = std::max(2, sheet.clearings);
    std::wstring shorts;
    if (sheet.mines && sheet.placed_mines < wanted_mines) {
      shorts = Format(IDS_GEN_SHORT_MINES, sheet.placed_mines, wanted_mines);
    }
    if (sheet.starts && sheet.placed_starts < sheet.clearings) {
      if (!shorts.empty()) shorts += Str(IDS_GEN_SHORT_JOIN);
      shorts += Format(IDS_GEN_SHORT_STARTS, sheet.placed_starts, sheet.clearings);
    }
    line = shorts.empty()
        ? Format(IDS_GEN_SIZE, sheet.size, sheet.size)
        : Format(IDS_GEN_SIZE_SHORT, sheet.size, sheet.size, shorts.c_str());
  }
  SetDlgItemTextW(dialog, IDC_GEN_NOTE, line.c_str());

  const struct { int id; int value; UINT what; } shares[] = {
      {IDC_GEN_WATER_VALUE, sheet.water, IDS_GEN_OF_THE_MAP},
      {IDC_GEN_COAST_VALUE, sheet.coast, IDS_GEN_OF_THE_MAP},
      {IDC_GEN_FOREST_VALUE, sheet.forest, IDS_GEN_OF_THE_LAND},
      {IDC_GEN_ROCK_VALUE, sheet.rock, IDS_GEN_OF_THE_LAND},
  };
  for (const auto& s : shares) {
    SetDlgItemTextW(dialog, s.id,
                    Format(IDS_GEN_SHARE, s.value, Str(s.what).c_str()).c_str());
  }

  // The octaves read as how wide a feature is rather than as the scale itself:
  // "0.030 features per tile" is the number the core wants and tells a mapper
  // nothing, where "about 33 tiles across" is the thing they are looking at.
  const struct { int id; int value; } octaves[] = {
      {IDC_GEN_LAND_VALUE, sheet.land},
      {IDC_GEN_COASTLINE_VALUE, sheet.coastline},
      {IDC_GEN_DETAIL_VALUE, sheet.detail},
  };
  for (const auto& o : octaves) {
    SetDlgItemTextW(dialog, o.id,
                    Format(IDS_GEN_FEATURE_WIDTH, std::max(1, 1000 / o.value)).c_str());
  }
  InvalidateRect(GetDlgItem(dialog, IDC_GEN_PREVIEW), nullptr, FALSE);
}

/// Draw the preview at one pixel per tile, scaled up to fill its box.
void DrawGenPreview(GenSheet& sheet, const DRAWITEMSTRUCT& item) {
  const RECT& rect = item.rcItem;
  if (!sheet.preview) {
    FillRect(item.hDC, &rect, GetSysColorBrush(COLOR_APPWORKSPACE));
    return;
  }
  const int w = pf_map_width(sheet.preview), h = pf_map_height(sheet.preview);
  std::vector<uint32_t> pixels(size_t(w) * size_t(h));
  // No artwork: flat terrain colours, which is all a thumbnail can show and is
  // also what makes this instant enough to redraw on every slider tick.
  if (pf_map_compose_minimap(sheet.preview, nullptr, pixels.data(), pixels.size()) <= 0) {
    FillRect(item.hDC, &rect, GetSysColorBrush(COLOR_APPWORKSPACE));
    return;
  }
  BlitRgba(item.hDC, rect.left, rect.top, rect.right - rect.left,
           rect.bottom - rect.top, w, h, pixels.data());
}

INT_PTR CALLBACK GenerateProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  GenSheet* sheet = reinterpret_cast<GenSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<GenSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      HWND combo = GetDlgItem(dialog, IDC_GEN_SIZE);
      for (int i = 0; i < int(std::size(kMapSizes)); i++) {
        const std::wstring label = Format(IDS_GEN_SIZE, kMapSizes[i], kMapSizes[i]);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (kMapSizes[i] == sheet->size) SendMessageW(combo, CB_SETCURSEL, WPARAM(i), 0);
      }
      FillCombo(dialog, IDC_GEN_TILESET, kTilesets, 4, sheet->tileset);

      const struct { int id; int value; } bars[] = {
          {IDC_GEN_WATER, sheet->water}, {IDC_GEN_COAST, sheet->coast},
          {IDC_GEN_FOREST, sheet->forest}, {IDC_GEN_ROCK, sheet->rock},
      };
      for (const auto& b : bars) {
        SendDlgItemMessageW(dialog, b.id, TBM_SETRANGE, TRUE, MAKELPARAM(0, 80));
        SendDlgItemMessageW(dialog, b.id, TBM_SETPOS, TRUE, LPARAM(b.value));
      }
      // Thousandths of a feature per tile. The floor is not zero: an octave at
      // scale 0 is a flat field, which is not a broader shape but no shape at
      // all, and the map comes back a single rectangle of one terrain.
      const struct { int id; int value; } octaves[] = {
          {IDC_GEN_LAND, sheet->land},
          {IDC_GEN_COASTLINE, sheet->coastline},
          {IDC_GEN_DETAIL, sheet->detail},
      };
      for (const auto& o : octaves) {
        SendDlgItemMessageW(dialog, o.id, TBM_SETRANGE, TRUE,
                            MAKELPARAM(kOctaveLeast, kOctaveMost));
        SendDlgItemMessageW(dialog, o.id, TBM_SETPOS, TRUE, LPARAM(o.value));
      }
      SetDlgItemInt(dialog, IDC_GEN_CLEARINGS, UINT(sheet->clearings), FALSE);
      SetDlgItemInt(dialog, IDC_GEN_RADIUS, UINT(sheet->radius), FALSE);
      SetDlgItemInt(dialog, IDC_GEN_SEED, UINT(sheet->seed), FALSE);
      CheckDlgButton(dialog, IDC_GEN_MINES, sheet->mines ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(dialog, IDC_GEN_STARTS, sheet->starts ? BST_CHECKED : BST_UNCHECKED);
      sheet->ready = true;
      Regenerate(*sheet);
      RefreshGenNote(dialog, *sheet);
      return TRUE;
    }

    case WM_DRAWITEM: {
      if (!sheet || wparam != IDC_GEN_PREVIEW) return FALSE;
      DrawGenPreview(*sheet, *reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    }

    case WM_HSCROLL: {
      if (!sheet) return FALSE;
      const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
      const int value = int(SendMessageW(reinterpret_cast<HWND>(lparam), TBM_GETPOS, 0, 0));
      if (id == IDC_GEN_WATER) sheet->water = value;
      else if (id == IDC_GEN_COAST) sheet->coast = value;
      else if (id == IDC_GEN_FOREST) sheet->forest = value;
      else if (id == IDC_GEN_ROCK) sheet->rock = value;
      else if (id == IDC_GEN_LAND) sheet->land = value;
      else if (id == IDC_GEN_COASTLINE) sheet->coastline = value;
      else if (id == IDC_GEN_DETAIL) sheet->detail = value;
      else return FALSE;
      Regenerate(*sheet);
      RefreshGenNote(dialog, *sheet);
      return TRUE;
    }

    case WM_COMMAND: {
      if (!sheet || !sheet->ready) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      bool again = false;
      if (id == IDC_GEN_SIZE && code == CBN_SELCHANGE) {
        const int at = int(SendDlgItemMessageW(dialog, IDC_GEN_SIZE, CB_GETCURSEL, 0, 0));
        if (at >= 0) sheet->size = kMapSizes[at];
        again = true;
      } else if (id == IDC_GEN_TILESET && code == CBN_SELCHANGE) {
        sheet->tileset = int(SendDlgItemMessageW(dialog, IDC_GEN_TILESET, CB_GETCURSEL, 0, 0));
        again = true;
      } else if ((id == IDC_GEN_CLEARINGS || id == IDC_GEN_RADIUS ||
                  id == IDC_GEN_SEED) && code == EN_CHANGE) {
        sheet->clearings = std::max(0, std::min(16, ReadNumber(dialog, IDC_GEN_CLEARINGS)));
        sheet->radius = std::max(1, std::min(32, ReadNumber(dialog, IDC_GEN_RADIUS)));
        sheet->seed = ReadNumber(dialog, IDC_GEN_SEED);
        again = true;
      } else if (id == IDC_GEN_MINES || id == IDC_GEN_STARTS) {
        sheet->mines = IsDlgButtonChecked(dialog, IDC_GEN_MINES) == BST_CHECKED;
        sheet->starts = IsDlgButtonChecked(dialog, IDC_GEN_STARTS) == BST_CHECKED;
        again = true;
      } else if (id == IDC_GEN_RANDOMIZE) {
        // Every number the shape is made of, plus the tileset, and none of the
        // two checkboxes: those say what to put *on* the map once it exists, and
        // a button that silently turned start locations off would be taking a
        // decision the person had already made. The tileset is not a decision in
        // the same way — nothing downstream depends on which one it is, and a
        // roll that only ever hands back forest is a roll that shows a quarter
        // of what the generator can make.
        //
        // The size is left alone for the opposite reason: it is the one control
        // here somebody usually picks on purpose, and rerolling it throws away
        // a 128x128 they chose in order to give them a 32x32 they did not.
        //
        // Ranges are narrower than the controls allow. A slider can be dragged
        // to 80% rock because a person doing it means it; a roll that lands
        // there hands back a map with nowhere to build and reads as broken.
        uint32_t r = uint32_t(GetTickCount()) ^ 0x9E3779B9u;
        const auto roll = [&r](int least, int most) {
          r = r * 1664525u + 1013904223u;
          return least + int((r >> 8) % uint32_t(most - least + 1));
        };
        sheet->water = roll(10, 55);
        sheet->coast = roll(4, 20);
        sheet->forest = roll(10, 45);
        sheet->rock = roll(2, 22);
        sheet->land = roll(15, 60);
        sheet->coastline = roll(50, 140);
        sheet->detail = roll(120, 300);
        sheet->clearings = roll(2, 8);
        sheet->seed = roll(1, 999999);
        sheet->tileset = roll(PF_TILESET_FOREST, PF_TILESET_SWAMP);
        // Straight into the controls, which are what the next edit reads back.
        const struct { int id; int value; } moved[] = {
            {IDC_GEN_WATER, sheet->water},         {IDC_GEN_COAST, sheet->coast},
            {IDC_GEN_FOREST, sheet->forest},       {IDC_GEN_ROCK, sheet->rock},
            {IDC_GEN_LAND, sheet->land},           {IDC_GEN_COASTLINE, sheet->coastline},
            {IDC_GEN_DETAIL, sheet->detail},
        };
        for (const auto& m : moved) {
          SendDlgItemMessageW(dialog, m.id, TBM_SETPOS, TRUE, LPARAM(m.value));
        }
        // Outside the `ready` guard below because CB_SETCURSEL is silent: unlike
        // the edit boxes, setting a combo's selection raises no CBN_SELCHANGE,
        // so this cannot cause the second regeneration that guard exists for.
        SendDlgItemMessageW(dialog, IDC_GEN_TILESET, CB_SETCURSEL,
                            WPARAM(sheet->tileset), 0);
        // ready is dropped over the two SetDlgItemInt calls: each raises
        // EN_CHANGE, which would re-read the boxes and regenerate twice more
        // for a result this already has.
        sheet->ready = false;
        SetDlgItemInt(dialog, IDC_GEN_CLEARINGS, UINT(sheet->clearings), FALSE);
        SetDlgItemInt(dialog, IDC_GEN_SEED, UINT(sheet->seed), FALSE);
        sheet->ready = true;
        again = true;
      } else if (id == IDC_GEN_ANOTHER) {
        // A different map from the same settings. Deterministic given a seed, so
        // "the one I liked" is reachable again by typing its number.
        sheet->seed = (sheet->seed * 1103515245 + 12345) & 0x7fffffff;
        SetDlgItemInt(dialog, IDC_GEN_SEED, UINT(sheet->seed), FALSE);
        return TRUE;   // the EN_CHANGE that causes does the regenerating
      } else if (id == IDOK) {
        EndDialog(dialog, sheet->preview ? IDOK : IDCANCEL);
        return TRUE;
      } else if (id == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      if (again) {
        Regenerate(*sheet);
        RefreshGenNote(dialog, *sheet);
        return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

// ------------------------------------------------------------- upgrade data

/// The `UGRD` sheet. Same shape as the unit sheet and a good deal simpler:
/// seven per-upgrade fields, all plain numbers, no components and no options.
struct UpgradeSheet {
  pf_map* map = nullptr;
  IconCache* icons = nullptr;      ///< borrowed
  Form form;
  std::vector<int> fields;         ///< the per-upgrade fields, in order
  int at = 0;                      ///< the upgrade the list is on
  /// How many rows the form was last built for. See UnitSheet::shape.
  int shape = -1;
  std::map<std::array<int, 2>, int64_t> edits;
  bool defaults = false;
  bool defaults_before = false;

  /// What the game will read: the map's own table where it carries one, and
  /// the game's where it does not. See UnitSheet::Stored.
  int64_t Stored(int f, int u) const {
    if (pf_map_has_upgrade_data(map)) return pf_map_upgrade_field(map, f, u);
    return pf_ugrd_default_field(f, u);
  }
  int64_t Value(int f, int u) const {
    const auto found = edits.find({f, u});
    if (found != edits.end()) return found->second;
    return Stored(f, u);
  }
  void Set(int f, int u, int64_t value) {
    if (Stored(f, u) == value) edits.erase({f, u});
    else edits[{f, u}] = value;
  }

  /// Whether anything about this upgrade is not what the game would read.
  /// See UnitSheet::Changed.
  bool Changed(int up) const {
    for (int f : fields) {
      if (up >= pf_ugrd_field_entries(f)) continue;
      if (Value(f, up) != pf_ugrd_default_field(f, up)) return true;
    }
    return false;
  }

  /// Put every field of one upgrade back to the game's own table.
  /// @return whether anything moved
  bool Reset(int up) {
    bool any = false;
    for (int f : fields) {
      if (up >= pf_ugrd_field_entries(f)) continue;
      const int64_t want = pf_ugrd_default_field(f, up);
      if (Value(f, up) == want) continue;
      Set(f, up, want);
      any = true;
    }
    return any;
  }
};

/// How many upgrades the sheet lists: the longest field decides, since a
/// shorter one simply has no row for the ones past its end.
int UpgradeCount() {
  int most = 0;
  for (int f = 0; f < pf_ugrd_field_count(); f++) {
    most = std::max(most, pf_ugrd_field_entries(f));
  }
  return most;
}

void BuildUpgradeForm(HWND dialog, UpgradeSheet* sheet) {
  sheet->fields.clear();
  for (int f = 0; f < pf_ugrd_field_count(); f++) {
    if (pf_ugrd_field_entries(f) <= 0) continue;   // not per-upgrade: the flag
    sheet->fields.push_back(f);
  }

  sheet->form.read = [sheet](int id) {
    return sheet->Value(sheet->fields[size_t(id)], sheet->at);
  };
  sheet->form.write = [sheet, dialog](int id, int64_t value) {
    const int field = sheet->fields[size_t(id)];
    sheet->Set(field, sheet->at, value);
    // The list draws each upgrade with the frame this very field names, so
    // changing it has to be visible in the list too.
    if (field == UpgradeIconField()) {
      InvalidateRect(GetDlgItem(dialog, IDC_UGRD_LIST), nullptr, TRUE);
    }
    return true;
  };
  sheet->form.changed = [sheet](int id) {
    const int field = sheet->fields[size_t(id)];
    return sheet->Value(field, sheet->at) != pf_ugrd_default_field(field, sheet->at);
  };
  sheet->form.reset = [sheet, dialog](int id) {
    const int field = sheet->fields[size_t(id)];
    sheet->Set(field, sheet->at, pf_ugrd_default_field(field, sheet->at));
    // The icon field decides how the list draws this upgrade, so putting it
    // back has to reach the list too.
    if (field == UpgradeIconField()) {
      InvalidateRect(GetDlgItem(dialog, IDC_UGRD_LIST), nullptr, TRUE);
    }
    return true;
  };
}

/// The rows for one upgrade: every field that covers it. All plain numbers —
/// `UGRD` has no options and no components.
std::vector<Form::Row> UpgradeFormRows(const UpgradeSheet& sheet, int upgrade) {
  std::vector<Form::Row> rows;
  for (size_t i = 0; i < sheet.fields.size(); i++) {
    const int field = sheet.fields[i];
    if (upgrade >= pf_ugrd_field_entries(field)) continue;
    Form::Row row;
    row.id = int(i);
    row.label = FieldLabel(pf_ugrd_field_name(field));
    row.kind = Form::Kind::kNumber;
    const int bytes = pf_ugrd_field_width(field);
    row.low = 0;
    row.high = bytes >= 4 ? 0x7FFFFFFF : (int64_t(1) << (8 * bytes)) - 1;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<std::wstring> UpgradeRows() {
  std::vector<std::wstring> out;
  for (int i = 0, n = UpgradeCount(); i < n; i++) {
    const char* name = pf_upgrade_name(i);
    out.push_back(name ? FromUtf8(name) : L"?");
  }
  return out;
}

void ShowUpgrade(HWND dialog, UpgradeSheet* sheet, int upgrade) {
  sheet->at = std::max(0, std::min(upgrade, UpgradeCount() - 1));
  const char* name = pf_upgrade_name(sheet->at);
  SetDlgItemTextW(dialog, IDC_UGRD_SUBJECT, name ? FromUtf8(name).c_str() : L"");
  std::vector<Form::Row> rows = UpgradeFormRows(*sheet, sheet->at);
  if (int(rows.size()) == sheet->shape) {
    sheet->form.Reload();     // same shape: keep the scroll where it was
    return;
  }
  sheet->shape = int(rows.size());
  sheet->form.SetRows(std::move(rows));
}

INT_PTR CALLBACK UpgradeProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<UpgradeSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      if (!sheet || wparam != IDC_UGRD_LIST) return FALSE;
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      const int at = int(item->itemID);
      // An upgrade's artwork is a frame number out of `UGRD` rather than
      // anything the unit table knows, so the icon comes from the map's own
      // value where it carries one — and follows an edit to it.
      const Icon* icon = nullptr;
      if (sheet->icons && at >= 0 && UpgradeIconField() >= 0) {
        icon = &sheet->icons->Frame(int(sheet->Value(UpgradeIconField(), at)));
      }
      return DrawIconRow(lparam, icon,
                         at >= 0 && at < UpgradeCount() && sheet->Changed(at));
    }

    case WM_CONTEXTMENU: {
      HWND list = GetDlgItem(dialog, IDC_UGRD_LIST);
      if (!sheet || reinterpret_cast<HWND>(wparam) != list) return FALSE;
      POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const int at = ListRowAt(list, screen);
      if (at < 0 || at >= UpgradeCount()) return TRUE;
      const char* name = pf_upgrade_name(at);
      const std::wstring label = name ? FromUtf8(name) : L"";
      if (!OfferRowReset(dialog, list, screen, at, sheet->Changed(at), label)) {
        return TRUE;
      }
      if (sheet->Reset(at)) {
        // The icon field is one of the ones that may have moved, so the row's
        // artwork is redrawn along with its colour and its star.
        InvalidateRect(list, nullptr, TRUE);
        if (sheet->at == at) sheet->form.Reload();
      }
      return TRUE;
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<UpgradeSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      PlaceForm(sheet->form, dialog, IDC_UGRD_FORM_SLOT, IDC_UGRD_FORM);
      BuildUpgradeForm(dialog, sheet);
      FillList(GetDlgItem(dialog, IDC_UGRD_LIST), UpgradeRows(), 0);
      ShowUpgrade(dialog, sheet, 0);
      CheckDlgButton(dialog, IDC_UGRD_DEFAULTS,
                     sheet->defaults ? BST_CHECKED : BST_UNCHECKED);
      SetFocus(GetDlgItem(dialog, IDC_UGRD_LIST));
      return FALSE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDC_UGRD_LIST && HIWORD(wparam) == LBN_SELCHANGE) {
        ShowUpgrade(dialog, sheet,
                    int(SendDlgItemMessageW(dialog, IDC_UGRD_LIST, LB_GETCURSEL, 0, 0)));
        return TRUE;
      }
      if (id == IDC_UGRD_DEFAULTS) {
        sheet->defaults = IsDlgButtonChecked(dialog, IDC_UGRD_DEFAULTS) == BST_CHECKED;
        return TRUE;
      }
      if (id == IDC_UGRD_IMPORT) {
        if (ImportSection(dialog, sheet->map, PF_COMPONENT_UGRD)) {
          sheet->edits.clear();     // see the units page: they described the old table
          sheet->shape = -1;
          ShowUpgrade(dialog, sheet, sheet->at);
        }
        return TRUE;
      }
      if (id == IDC_UGRD_EXPORT) {
        ExportSection(dialog, sheet->map, PF_COMPONENT_UGRD);
        return TRUE;
      }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ------------------------------------------------------------- restrictions

/// `ALOW`: six blocks of 32 bits per player.
///
/// A player on the left, that player's thirty-two ticks on the right. The
/// question a mapper opens this to answer is "what can *this* player do",
/// which is one slot's worth of bits — and a player is a thing with a colour,
/// so the list needs no other icon.
struct AllowSheet {
  pf_map* map = nullptr;
  IconCache* icons = nullptr;      ///< borrowed
  Form form;
  std::array<Icon, 16> player_icons = PlayerIcons();
  int block = 0;
  int at = 0;                      ///< the player the list is on
  int64_t bits[6][16] = {};
  int64_t before[6][16] = {};
  /// Whether the map carries an `ALOW` section at all.
  ///
  /// Most do not, and the page used to grey every control and say so — a tab
  /// whose whole content was the news that it did nothing. `bits` is seeded with
  /// what the game will actually read, which for a map with no section is the
  /// unrestricted table, and the first tick changed is what creates it.
  bool present = false;
  /// The Use-the-game-defaults tick: ticked means the saved map should carry no
  /// section. Its own state rather than `!present`, because it is what the user
  /// is asking for and `present` is what the map currently is.
  bool defaults = false;
  /// Which of the six blocks the dropdown offers, in order. The row picked is an
  /// index into this rather than a block number: see AllowBlocksToShow.
  std::vector<int> shown;
};

/// Every bit of a block, for the All button.
constexpr int64_t kAllowEverything = int64_t(0xFFFFFFFFu);

/// What the game reads for a map with no `ALOW`, per block.
///
/// The same values `pf_map_add_restrictions` seeds a new section with, which is
/// what makes adding one on the first edit a change the map cannot feel. Asked
/// of the core rather than written down here: two of the six blocks are not
/// "allowed" flags and seeding them all-ones would show a lie and then save it.
int64_t AllowDefault(int block) {
  const int64_t value = pf_alow_default(block);
  return value < 0 ? kAllowEverything : value;
}

/// True when every block and player holds the unrestricted value.
bool AllowIsDefault(const AllowSheet& sheet) {
  for (int b = 0; b < 6; b++) {
    for (int p = 0; p < 16; p++) {
      if (sheet.bits[b][p] != AllowDefault(b)) return false;
    }
  }
  return true;
}

/// Put every block and player back to the unrestricted table.
void AllowSetDefault(AllowSheet& sheet) {
  for (int b = 0; b < 6; b++) {
    for (int p = 0; p < 16; p++) sheet.bits[b][p] = AllowDefault(b);
  }
}

/// Clear the defaults tick once the table stops matching it.
///
/// Only ever cleared here, never set: an all-default section is something a
/// person may deliberately want written, so re-ticking stays theirs to do.
void SyncAllowDefaults(HWND dialog, AllowSheet& sheet) {
  if (!sheet.defaults || AllowIsDefault(sheet)) return;
  sheet.defaults = false;
  CheckDlgButton(dialog, IDC_ALOW_DEFAULTS, BST_UNCHECKED);
}

/// Which blocks the dropdown offers.
///
/// Not all six. `spellsResearching` and `upgradesResearching` say what a player
/// is *part-way through* researching at the first tick of the game, and nothing
/// uses them: of the 40 corpus maps carrying an `ALOW` at all, 39 hold zero in
/// both, and the one exception is "-= Wall Knights =-", whose six blocks are all
/// 0xFFFFFFFF — an editor writing all-ones for "unrestricted", which in these
/// two blocks means every player starts mid-research on everything. That is the
/// bug `overrides/alow_defaults.cpp` exists to avoid, not a feature to offer.
///
/// Hidden rather than removed, and only when the map agrees they are unused: a
/// map that does carry something here must stay editable, or the page would show
/// a map it cannot fully describe. The values are read and written either way —
/// this is what the dropdown lists, not what the section holds.
std::vector<int> AllowBlocksToShow(const AllowSheet& sheet) {
  std::vector<int> shown;
  for (int b = 0; b < 6; b++) {
    const bool researching = b == 3 || b == 5;
    bool used = false;
    for (int p = 0; p < 16 && researching; p++) {
      used = used || sheet.bits[b][p] != AllowDefault(b);
    }
    if (!researching || used) shown.push_back(b);
  }
  return shown;
}

/// The thirty-two ticks of one block, named.
///
/// Blizzard left gaps in these tables. A row saying "bit 17" is honest about a
/// bit that exists and has no known meaning; leaving it out would make the ones
/// after it unreachable.
std::vector<Form::Row> AllowFormRows(int block) {
  std::vector<Form::Row> rows;
  for (int bit = 0; bit < 32; bit++) {
    Form::Row row;
    row.id = bit;
    const char* name = pf_alow_bit_name(block, bit);
    row.label = name ? FromUtf8(name) : Format(IDS_ALOW_BIT_UNKNOWN, bit);
    row.kind = Form::Kind::kCheck;
    rows.push_back(std::move(row));
  }
  return rows;
}

/// How many of the shown player's thirty-two bits are set.
int AllowCount(const AllowSheet& sheet) {
  int on = 0;
  for (int bit = 0; bit < 32; bit++) {
    on += int((sheet.bits[sheet.block][sheet.at] >> bit) & 1);
  }
  return on;
}

void RefreshAllowNote(HWND dialog, const AllowSheet& sheet) {
  // The count and nothing else. Whether the map carries an `ALOW` section is not
  // the mapper's problem: the page shows what the game will read, and editing a
  // tick is what gives the map a section.
  SetDlgItemTextW(dialog, IDC_ALOW_NOTE,
                  Format(IDS_ALOW_COUNT, AllowCount(sheet), 32).c_str());
}

/// What a tick means in this block. Only three of the six say "allowed".
UINT AllowMeaning(int block) {
  if (block == 1) return IDS_ALOW_MEANS_KNOWN;
  if (block == 3 || block == 5) return IDS_ALOW_MEANS_PARTIAL;
  return IDS_ALOW_MEANS_ALLOWED;
}

/// Show one player's ticks for the current block.
void ShowAllowPlayer(HWND dialog, AllowSheet* sheet, int player) {
  sheet->at = pf_player_is_supported(player) ? player : SlotForRow(0);
  SetDlgItemTextW(dialog, IDC_ALOW_MEANING,
                  Str(AllowMeaning(sheet->block)).c_str());
  const char* name = pf_player_name(sheet->at);
  SetDlgItemTextW(dialog, IDC_ALOW_SUBJECT,
                  Format(IDS_BITS_TITLE,
                         name ? FromUtf8(name).c_str()
                              : Format(IDS_PLAYER_N, sheet->at + 1).c_str(),
                         FieldLabel(pf_alow_block_name(sheet->block)).c_str()).c_str());
  sheet->form.Reload();
  RefreshAllowNote(dialog, *sheet);
}

INT_PTR CALLBACK AllowProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<AllowSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      if (!sheet || wparam != IDC_ALOW_PLAYER) return FALSE;
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      const int at = SlotForRow(int(item->itemID));
      return DrawIconRow(lparam, at >= 0 ? &sheet->player_icons[at] : nullptr);
    }

    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<AllowSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      HWND blocks = GetDlgItem(dialog, IDC_ALOW_BLOCK);
      sheet->shown = AllowBlocksToShow(*sheet);
      for (int b : sheet->shown) {
        // A block is named the way the section names it — `unitsAllowed` — and
        // the same rule that turns a field name into a label turns this one into
        // "Units Allowed".
        SendMessageW(blocks, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(FieldLabel(pf_alow_block_name(b)).c_str()));
      }
      SendMessageW(blocks, CB_SETCURSEL, 0, 0);
      sheet->block = sheet->shown.empty() ? 0 : sheet->shown[0];

      CheckDlgButton(dialog, IDC_ALOW_DEFAULTS,
                     sheet->defaults ? BST_CHECKED : BST_UNCHECKED);

      PlaceForm(sheet->form, dialog, IDC_ALOW_FORM_SLOT, IDC_ALOW_FORM);
      sheet->form.read = [sheet](int bit) -> int64_t {
        return (sheet->bits[sheet->block][sheet->at] >> bit) & 1;
      };
      sheet->form.write = [sheet, dialog](int bit, int64_t value) {
        int64_t& mask = sheet->bits[sheet->block][sheet->at];
        if (value) mask |= int64_t(1) << bit;
        else mask &= ~(int64_t(1) << bit);
        SyncAllowDefaults(dialog, *sheet);
        RefreshAllowNote(dialog, *sheet);
        return true;
      };
      sheet->form.changed = [sheet](int bit) {
        const int64_t mask = int64_t(1) << bit;
        return (sheet->bits[sheet->block][sheet->at] & mask) !=
               (sheet->before[sheet->block][sheet->at] & mask);
      };
      sheet->form.reset = [sheet, dialog](int bit) {
        // Back to what the map had, which for a map with no section is
        // "allowed" — the same value the page opened on.
        const int64_t mask = int64_t(1) << bit;
        int64_t& held = sheet->bits[sheet->block][sheet->at];
        const int64_t want =
            (held & ~mask) | (sheet->before[sheet->block][sheet->at] & mask);
        if (want == held) return false;
        held = want;
        SyncAllowDefaults(dialog, *sheet);
        RefreshAllowNote(dialog, *sheet);
        return true;
      };
      sheet->form.SetRows(AllowFormRows(sheet->block));

      // The races come straight from the map here: this page does not edit them,
      // so there is no working copy to prefer.
      int races[16] = {};
      for (int i = 0; i < 16; i++) races[i] = pf_map_race(sheet->map, i);
      FillList(GetDlgItem(dialog, IDC_ALOW_PLAYER), PlayerRows(races), 0);
      ShowAllowPlayer(dialog, sheet, 0);
      SetFocus(GetDlgItem(dialog, IDC_ALOW_PLAYER));
      return FALSE;
    }
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if (id == IDC_ALOW_PLAYER && code == LBN_SELCHANGE) {
        ShowAllowPlayer(dialog, sheet,
                        SlotForRow(int(SendDlgItemMessageW(dialog, IDC_ALOW_PLAYER,
                                                           LB_GETCURSEL, 0, 0))));
        return TRUE;
      }
      if (id == IDC_ALOW_BLOCK && code == CBN_SELCHANGE) {
        // A different block is thirty-two different bits with different names,
        // so the form is rebuilt rather than merely re-read. Through `shown`,
        // because the row picked is not the block number once any is hidden.
        const int row = int(SendDlgItemMessageW(dialog, IDC_ALOW_BLOCK, CB_GETCURSEL, 0, 0));
        if (row < 0 || row >= int(sheet->shown.size())) return TRUE;
        sheet->block = sheet->shown[size_t(row)];
        sheet->form.SetRows(AllowFormRows(sheet->block));
        ShowAllowPlayer(dialog, sheet, sheet->at);
        return TRUE;
      }
      if (id == IDC_ALOW_DEFAULTS) {
        sheet->defaults = IsDlgButtonChecked(dialog, IDC_ALOW_DEFAULTS) == BST_CHECKED;
        // Ticking it is also the reset: the ticks have to show what the map
        // will play as, and with no section that is the unrestricted table.
        if (sheet->defaults) {
          AllowSetDefault(*sheet);
          sheet->form.Reload();
          RefreshAllowNote(dialog, *sheet);
        }
        return TRUE;
      }
      if (id == IDC_ALOW_ALL || id == IDC_ALOW_NONE) {
        // The shown player, because the form shows one player: a button that
        // quietly set all sixteen would be a surprise from up here.
        sheet->bits[sheet->block][sheet->at] =
            id == IDC_ALOW_ALL ? kAllowEverything : 0;
        sheet->form.Reload();
        SyncAllowDefaults(dialog, *sheet);
        RefreshAllowNote(dialog, *sheet);
        return TRUE;
      }
      if (id == IDC_ALOW_IMPORT) {
        if (ImportSection(dialog, sheet->map, PF_COMPONENT_ALOW)) {
          // The whole table has been replaced, so what was on screen described a
          // section that no longer exists. Re-read it, `before` included, or OK
          // would write the old ticks back over the imported ones.
          sheet->present = pf_map_has_restrictions(sheet->map) != 0;
          for (int b = 0; b < 6; b++) {
            for (int p = 0; p < 16; p++) {
              sheet->bits[b][p] =
                  sheet->present ? pf_map_allow(sheet->map, b, p) : AllowDefault(b);
              sheet->before[b][p] = sheet->bits[b][p];
            }
          }
          // An imported section may use the researching blocks where the map
          // did not, so which blocks the dropdown offers is decided again.
          const std::vector<int> want = AllowBlocksToShow(*sheet);
          if (want != sheet->shown) {
            sheet->shown = want;
            HWND blocks = GetDlgItem(dialog, IDC_ALOW_BLOCK);
            SendMessageW(blocks, CB_RESETCONTENT, 0, 0);
            for (int b : sheet->shown) {
              SendMessageW(blocks, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(
                               FieldLabel(pf_alow_block_name(b)).c_str()));
            }
            // Stay on the block being looked at when it survived, so an import
            // does not silently move the page to a different table.
            const auto at = std::find(sheet->shown.begin(), sheet->shown.end(),
                                      sheet->block);
            const int row = at == sheet->shown.end()
                                ? 0
                                : int(at - sheet->shown.begin());
            SendMessageW(blocks, CB_SETCURSEL, WPARAM(row), 0);
            sheet->block = sheet->shown.empty() ? 0 : sheet->shown[size_t(row)];
            sheet->form.SetRows(AllowFormRows(sheet->block));
          }
          ShowAllowPlayer(dialog, sheet, sheet->at);
        }
        return TRUE;
      }
      if (id == IDC_ALOW_EXPORT) {
        ExportSection(dialog, sheet->map, PF_COMPONENT_ALOW);
        return TRUE;
      }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

// ---------------------------------------------------------------- tile picker

/// One tile the artwork can draw, rasterised on demand.
///
/// A tileset holds on the order of a thousand of these and a picker shows a
/// screenful at a time, so drawing them all up front would spend six megabytes
/// and a visible pause on cells nobody scrolls to.
struct TileSheet {
  const pf_tileset_art* art = nullptr;
  int tileset = 0;
  int chosen = -1;
  HINSTANCE instance = nullptr;
  PaletteGrid grid;
  std::vector<int> tiles;                          ///< tile values, in grid order
  std::map<int, std::vector<uint32_t>> raster;     ///< tile value -> 32x32 RGBA

  /// Every tile value the artwork can draw, bucketed by the terrain it mostly
  /// shows. A thousand unlabelled squares is a wall, and the terrain a tile
  /// belongs to is the one thing a person scanning for it already knows.
  void Gather();
  const uint32_t* Pixels(int tile);
};

void TileSheet::Gather() {
  // Tile values are (group << 4) | variation, and groups run to 0x9f — the same
  // walk the web client makes.
  std::vector<int> by_terrain[PF_TERRAIN_UNKNOWN + 1];
  for (int group = 0; group <= 0x9f; group++) {
    for (int variation = 0; variation < 16; variation++) {
      const int tile = (group << 4) | variation;
      const int megatile = pf_tileset_art_megatile_for(art, uint16_t(tile));
      // Having a megatile is not the same as being drawable: the variations a
      // tileset does not use are a valid id whose every pixel is nothing.
      if (megatile < 0 || pf_tileset_art_is_blank(art, megatile)) continue;
      int terrain = pf_tile_dominant_terrain(uint16_t(tile));
      if (terrain < 0 || terrain > PF_TERRAIN_UNKNOWN) terrain = PF_TERRAIN_UNKNOWN;
      by_terrain[terrain].push_back(tile);
    }
  }

  std::vector<PaletteGrid::Entry> entries;
  for (int terrain = 0; terrain <= PF_TERRAIN_UNKNOWN; terrain++) {
    if (by_terrain[terrain].empty()) continue;
    PaletteGrid::Entry heading;
    const char* name = pf_terrain_name(terrain, tileset);
    heading.heading = name ? FromUtf8(name) : Str(IDS_TERRAIN_OTHER);
    entries.push_back(std::move(heading));
    for (int tile : by_terrain[terrain]) {
      PaletteGrid::Entry cell;
      cell.id = tile;
      cell.label = Format(IDS_TILE_HEX, unsigned(tile));
      entries.push_back(std::move(cell));
      tiles.push_back(tile);
    }
  }
  grid.SetEntries(std::move(entries));
}

const uint32_t* TileSheet::Pixels(int tile) {
  auto found = raster.find(tile);
  if (found == raster.end()) {
    const int megatile = pf_tileset_art_megatile_for(art, uint16_t(tile));
    if (megatile < 0) return nullptr;
    std::vector<uint32_t> px(32 * 32);
    pf_tileset_art_draw(art, megatile, px.data(), 32);
    found = raster.emplace(tile, std::move(px)).first;
  }
  return found->second.data();
}

/// The chosen tile's value, under the grid, so a pick can be checked against a
/// tile id read out of a map somewhere else.
void RefreshTileNote(HWND dialog, const TileSheet& sheet) {
  std::wstring line;
  if (sheet.chosen < 0) {
    line = Format(IDS_TILES_PICK_ONE, int(sheet.tiles.size()));
  } else {
    const char* name =
        pf_terrain_name(pf_tile_dominant_terrain(uint16_t(sheet.chosen)),
                        sheet.tileset);
    line = Format(IDS_TILE_MOSTLY, unsigned(sheet.chosen),
                  name ? FromUtf8(name).c_str() : Str(IDS_UNKNOWN).c_str());
  }
  SetDlgItemTextW(dialog, IDC_TILES_NOTE, line.c_str());
  EnableWindow(GetDlgItem(dialog, IDOK), sheet.chosen >= 0);
}

/// Put the grid where the template's placeholder is, then take the placeholder
/// away. Laying it out in the .rc keeps the geometry in one place even though a
/// PaletteGrid cannot be a template control.
void PlaceGridOverPlaceholder(HWND dialog, TileSheet* sheet) {
  HWND placeholder = GetDlgItem(dialog, IDC_TILES_GRID);
  RECT rect = {};
  GetWindowRect(placeholder, &rect);
  MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
  ShowWindow(placeholder, SW_HIDE);

  HWND grid = sheet->grid.Create(dialog, sheet->instance, IDC_TILES_GRID + 1);
  MoveWindow(grid, rect.left, rect.top, rect.right - rect.left,
             rect.bottom - rect.top, TRUE);
}

INT_PTR CALLBACK TilesProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  TileSheet* sheet = reinterpret_cast<TileSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      sheet = reinterpret_cast<TileSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));

      sheet->grid.draw_icon = [sheet](HDC dc, const RECT& rect, int tile) {
        const uint32_t* px = sheet->Pixels(tile);
        if (!px) return;
        const int size = std::min(rect.right - rect.left, rect.bottom - rect.top) - 4;
        if (size <= 0) return;
        BlitRgba(dc, rect.left + (rect.right - rect.left - size) / 2,
                 rect.top + (rect.bottom - rect.top - size) / 2, size, size, 32,
                 32, px);
      };
      // A single click picks; a pick is not a commit, so the value can be read
      // before OK and a wrong one corrected without reopening.
      sheet->grid.on_pick = [sheet, dialog](int tile) {
        sheet->chosen = tile;
        RefreshTileNote(dialog, *sheet);
      };
      PlaceGridOverPlaceholder(dialog, sheet);
      sheet->grid.SetSelected(sheet->chosen);
      RefreshTileNote(dialog, *sheet);
      return TRUE;
    }
    case WM_COMMAND:
      if (!sheet) return FALSE;
      if (LOWORD(wparam) == IDOK && sheet->chosen >= 0) {
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      return FALSE;
  }
  return FALSE;
}

// ------------------------------------------------- the six map sheets, tabbed
//
// Map properties, players, unit data, upgrades, restrictions and statistics are
// the same question asked of different sections of one file, and a mapper
// setting a map up moves between them constantly. Six modal dialogs made that
// six open-edit-close cycles; one window with six tabs makes it one.
//
// Structurally: the tab strip and the buttons, and six child dialogs stacked in
// the strip's display rectangle with one shown. Each page keeps the proc, the
// sheet struct and the harvesting it had — what moved is *when* the work is
// applied, which is now this window's OK.
//
// Every one of these sheets already edited a local struct and wrote nothing
// until accepted, which is what makes one OK honest.

struct SheetsWindow {
  pf_map* map = nullptr;
  IconCache* icons = nullptr;
  HINSTANCE instance = nullptr;
  int start_tab = 0;
  HWND pages[kMapSheetCount] = {};

  MapSheet map_sheet;
  PlayerSheet player_sheet;
  UnitSheet unit_sheet;
  UpgradeSheet upgrade_sheet;
  AllowSheet allow_sheet;
};

void ShowPage(SheetsWindow& win, int which) {
  for (int i = 0; i < kMapSheetCount; i++) {
    if (!win.pages[i]) continue;
    ShowWindow(win.pages[i], i == which ? SW_SHOW : SW_HIDE);
  }
}

INT_PTR CALLBACK SheetsProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* win = reinterpret_cast<SheetsWindow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));

  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      win = reinterpret_cast<SheetsWindow*>(lparam);
      SetWindowLongPtrW(dialog, GWLP_USERDATA, LONG_PTR(win));

      HWND tabs = GetDlgItem(dialog, IDC_SHEET_TABS);
      static const UINT kLabels[kMapSheetCount] = {
          IDS_TAB_MAP, IDS_TAB_PLAYERS, IDS_TAB_UNITS,
          IDS_TAB_UPGRADES, IDS_TAB_RESTRICTIONS, IDS_TAB_STATISTICS};
      std::wstring held[kMapSheetCount];
      for (int i = 0; i < kMapSheetCount; i++) {
        held[i] = Str(kLabels[i]);
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(held[i].c_str());
        TabCtrl_InsertItem(tabs, i, &item);
      }

      // Where a page goes: the tab control's rectangle less its strip, which is
      // what TabCtrl_AdjustRect works out — the strip's height depends on the
      // theme and the font.
      RECT page_rect;
      GetWindowRect(tabs, &page_rect);
      MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&page_rect), 2);
      TabCtrl_AdjustRect(tabs, FALSE, &page_rect);

      struct { int id; DLGPROC proc; LPARAM param; } build[kMapSheetCount] = {
          {IDD_MAP_PROPERTIES, MapProc, LPARAM(&win->map_sheet)},
          {IDD_PLAYERS, PlayersProc, LPARAM(&win->player_sheet)},
          {IDD_UNIT_DATA, UnitDataProc, LPARAM(&win->unit_sheet)},
          {IDD_UPGRADES, UpgradeProc, LPARAM(&win->upgrade_sheet)},
          {IDD_RESTRICTIONS, AllowProc, LPARAM(&win->allow_sheet)},
          {IDD_MAP_STATS, StatsPageProc, LPARAM(win->map)},
      };
      for (int i = 0; i < kMapSheetCount; i++) {
        win->pages[i] = CreateDialogParamW(
            win->instance, MAKEINTRESOURCEW(build[i].id), dialog,
            build[i].proc, build[i].param);
        if (!win->pages[i]) continue;
        SetWindowPos(win->pages[i], HWND_TOP, page_rect.left, page_rect.top,
                     page_rect.right - page_rect.left,
                     page_rect.bottom - page_rect.top, SWP_NOACTIVATE);
        ShowWindow(win->pages[i], SW_HIDE);
      }

      const int start = std::max(0, std::min(win->start_tab, kMapSheetCount - 1));
      TabCtrl_SetCurSel(tabs, start);
      ShowPage(*win, start);
      return TRUE;
    }

    case WM_NOTIFY: {
      if (!win) return FALSE;
      auto* head = reinterpret_cast<NMHDR*>(lparam);
      if (head && head->idFrom == IDC_SHEET_TABS && head->code == TCN_SELCHANGE) {
        ShowPage(*win, TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SHEET_TABS)));
        return TRUE;
      }
      return FALSE;
    }

    case WM_COMMAND:
      if (!win) return FALSE;
      if (LOWORD(wparam) == IDOK) {
        // The one page that keeps its values in controls rather than writing
        // them through as they change.
        HarvestMapPage(win->pages[kMapSheetMap], win->map_sheet);
        EndDialog(dialog, IDOK);
        return TRUE;
      }
      if (LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
      }
      return FALSE;
  }
  return FALSE;
}

/// Write one sheet's changes to the map, each as its own undo step.
///
/// Its own rather than one for the window: "undo the unit data I just changed"
/// and "undo the players I just changed" are two different things a person
/// means. Each of these is the code the sheet applied when it was a dialog of
/// its own, which is why they read as six unrelated paragraphs — they are six
/// unrelated sections of the file.
bool ApplyMapSheet(MapSheet& sheet, pf_map* map, std::wstring& note) {
  if (!sheet.changed) return false;
  pf_map_checkpoint(map);
  // The page turns unstorable characters down as they are typed, so this only
  // fails by a route that got round that — and then the old description is kept
  // rather than a mangled one written.
  const bool named =
      pf_map_set_description(map, ToUtf8(sheet.description).c_str()) == PF_OK;
  const int before = pf_map_tileset(map);
  if (sheet.tileset != before) {
    pf_map_set_tileset(map, sheet.tileset);
    note = Str(IDS_TILESET_CHANGED);
  } else {
    note = Str(IDS_MAP_PROPS_UPDATED);
  }
  if (!named) note = Str(IDS_DESC_NOT_STORED);
  return true;
}

/// The Map page's other half, and its own undo step.
///
/// Its own rather than sharing the description's: undoing a resize and undoing a
/// renaming are not one thought, and a resize is the largest single thing this
/// window can do to a map.
bool ApplyResize(MapSheet& sheet, pf_map* map, std::wstring& note) {
  sheet.dropped_units = 0;
  sheet.resize_failed = false;
  if (sheet.size == pf_map_width(map)) return false;

  int ox = 0, oy = 0;
  ResizeOffset(sheet, ox, oy);
  pf_map_checkpoint(map);
  pf_status status = PF_OK;
  const int dropped = pf_map_resize(map, sheet.size, sheet.size, ox, oy, &status);
  if (dropped < 0) {
    // An operation that did nothing must not consume an undo step.
    pf_map_undo(map);
    sheet.resize_failed = true;
    return false;
  }
  sheet.dropped_units = dropped;
  note = dropped == 0
      ? Format(IDS_RESIZED, sheet.size, sheet.size)
      : Format(Plural(dropped, IDS_RESIZED_DROPPED_ONE, IDS_RESIZED_DROPPED_MANY),
               sheet.size, sheet.size, dropped);
  return true;
}

bool ApplyPlayerSheet(PlayerSheet& sheet, pf_map* map, std::wstring& note,
                      std::vector<int>* races_changed) {
  int touched = 0;
  pf_map_checkpoint(map);
  for (int i = 0; i < 16; i++) {
    const PlayerRow& row = sheet.rows[i];
    bool any = false;
    if (pf_map_owner(map, i) != row.owner) {
      pf_map_set_owner(map, i, row.owner);
      any = true;
    }
    if (pf_map_race(map, i) != row.race) {
      pf_map_set_race(map, i, row.race);
      // Named for the caller: `SIDE` and the units standing on the map have to
      // agree, or a swapped base is unbuildable rather than merely odd, and
      // swapping the units is a bulk edit that belongs to the editor.
      if (races_changed) races_changed->push_back(i);
      any = true;
    }
    if (pf_map_start_gold(map, i) != row.gold ||
        pf_map_start_lumber(map, i) != row.lumber ||
        pf_map_start_oil(map, i) != row.oil) {
      pf_map_set_start_resources(map, i, row.gold, row.lumber, row.oil);
      any = true;
    }
    if (pf_map_ai(map, i) != row.ai) {
      pf_map_set_ai(map, i, row.ai);
      any = true;
    }
    if (any) touched++;
  }
  if (touched == 0) {
    // Nothing moved, so the checkpoint is an undo step that would undo nothing.
    // Take it back off the stack rather than leaving it to be pressed through.
    pf_map_undo(map);
    return false;
  }
  note = Format(Plural(touched, IDS_PLAYERS_UPDATED_ONE, IDS_PLAYERS_UPDATED_MANY),
                touched);
  return true;
}

bool ApplyUnitSheet(UnitSheet& sheet, pf_map* map, std::wstring& note) {
  if (sheet.edits.empty() && sheet.defaults == sheet.defaults_before) return false;
  pf_map_checkpoint(map);
  // A map with no section of its own gets one, seeded from the game's table —
  // which is what it was already using, so the map plays the same until one of
  // these edits lands.
  if (!sheet.edits.empty() && !pf_map_has_unit_data(map)) pf_map_add_unit_data(map);
  for (const auto& [key, value] : sheet.edits) {
    pf_map_set_unit_field(map, key[0], key[1], key[2], value);
  }
  if (sheet.defaults != sheet.defaults_before) {
    if (!pf_map_has_unit_data(map)) pf_map_add_unit_data(map);
    pf_map_set_unit_field(map, 0, 0, 0, sheet.defaults ? 1 : 0);
  }
  note = Format(Plural(int(sheet.edits.size()), IDS_UNIT_PROPS_ONE,
                       IDS_UNIT_PROPS_MANY),
                int(sheet.edits.size()));
  return true;
}

bool ApplyUpgradeSheet(UpgradeSheet& sheet, pf_map* map, std::wstring& note) {
  if (sheet.edits.empty() && sheet.defaults == sheet.defaults_before) return false;
  pf_map_checkpoint(map);
  if (!sheet.edits.empty() && !pf_map_has_upgrade_data(map)) {
    pf_map_add_upgrade_data(map);
  }
  for (const auto& [key, value] : sheet.edits) {
    pf_map_set_upgrade_field(map, key[0], key[1], value);
  }
  if (sheet.defaults != sheet.defaults_before) {
    if (!pf_map_has_upgrade_data(map)) pf_map_add_upgrade_data(map);
    pf_map_set_upgrade_field(map, 0, 0, sheet.defaults ? 1 : 0);
  }
  note = Format(Plural(int(sheet.edits.size()), IDS_UPGRADE_PROPS_ONE,
                       IDS_UPGRADE_PROPS_MANY),
                int(sheet.edits.size()));
  return true;
}

bool ApplyAllowSheet(AllowSheet& sheet, pf_map* map, std::wstring& note) {
  // Nothing moved: no checkpoint, no section, no undo step. A map with no `ALOW`
  // must not be given one by the act of looking at the page, which is the
  // promise that lets the page be editable at all.
  const bool has = pf_map_has_restrictions(map) != 0;
  const bool want = !sheet.defaults;

  bool any = false;
  for (int b = 0; b < 6 && !any; b++) {
    for (int p = 0; p < 16; p++) {
      if (sheet.bits[b][p] != sheet.before[b][p]) { any = true; break; }
    }
  }
  if (!any && has == want) return false;

  pf_map_checkpoint(map);
  if (!want) {
    // Asked for the game's defaults, which is the absence of the section rather
    // than a section holding the unrestricted table.
    pf_map_clear_restrictions(map);
    note = Str(IDS_ALOW_CLEARED);
    return true;
  }
  // The section is created here, seeded with the unrestricted table — exactly
  // how a map with no section already behaves, so the map plays the same until
  // the ticks below land.
  if (!has) pf_map_add_restrictions(map);
  int touched = 0;
  for (int b = 0; b < 6; b++) {
    for (int p = 0; p < 16; p++) {
      // Against the map rather than against `before`: the section may have just
      // been created, and what it now holds is what these ticks differ from.
      if (pf_map_allow(map, b, p) == sheet.bits[b][p]) continue;
      if (pf_map_set_allow(map, b, p, sheet.bits[b][p]) == PF_OK) touched++;
    }
  }
  if (touched == 0 && has) {
    pf_map_undo(map);
    return false;
  }
  note = touched ? Format(Plural(touched, IDS_ALOW_CHANGED_ONE,
                                 IDS_ALOW_CHANGED_MANY), touched)
                 : Str(IDS_ALOW_ADDED);
  return true;
}

}  // namespace

pf_map* ShowGenerate(HWND owner, HINSTANCE instance, int tileset) {
  GenSheet sheet;
  sheet.tileset = tileset;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_GENERATE), owner,
                      GenerateProc, LPARAM(&sheet)) != IDOK) {
    return nullptr;
  }
  // The preview *is* the map. Hand it over and stop owning it, rather than
  // generating a second one that might not be the one on screen.
  pf_map* map = sheet.preview;
  sheet.preview = nullptr;
  return map;
}

bool ShowMapSheets(HWND owner, HINSTANCE instance, pf_map* map, IconCache* icons,
                   GameData* game, const UiIcons* ui, int tab, int row,
                   std::wstring& note, MapSheetsOutcome* outcome) {
  if (outcome) *outcome = MapSheetsOutcome{};
  if (!map) return false;

  SheetsWindow win;
  win.map = map;
  win.icons = icons;
  win.instance = instance;
  win.start_tab = tab;
  win.player_sheet.game = game;
  win.player_sheet.ui = ui;
  win.player_sheet.instance = instance;
  // Which row a page opens on, for the two pages with a list long enough for it
  // to matter. A row is only meant for the page that was asked for — player 3
  // and unit 3 are different rows of different lists.
  win.player_sheet.at =
      tab == kMapSheetPlayers && row >= 0 && row < 16 ? row : 0;
  win.unit_sheet.at =
      tab == kMapSheetUnits && row >= 0 && row < PF_UNIT_COUNT ? row : 0;

  // Every page's values are read out of the map here, before anything is shown,
  // and nothing goes back until OK. Collecting it is what makes Cancel cancel
  // all six.
  win.map_sheet.map = map;
  const char* description = pf_map_description(map);
  win.map_sheet.description = description ? FromUtf8(description) : L"";
  win.map_sheet.tileset = pf_map_tileset(map);
  win.map_sheet.size = pf_map_width(map);

  win.player_sheet.map = map;
  for (int i = 0; i < 16; i++) {
    win.player_sheet.rows[i].owner = pf_map_owner(map, i);
    win.player_sheet.rows[i].race = pf_map_race(map, i);
    win.player_sheet.rows[i].gold = pf_map_start_gold(map, i);
    win.player_sheet.rows[i].lumber = pf_map_start_lumber(map, i);
    win.player_sheet.rows[i].oil = pf_map_start_oil(map, i);
    win.player_sheet.rows[i].ai = pf_map_ai(map, i);
  }

  win.unit_sheet.map = map;
  win.unit_sheet.icons = icons;
  // Field 0 is not per-unit: it is the map saying "ignore all of this and use
  // the game's own table", which is worth a checkbox of its own.
  win.unit_sheet.defaults =
      pf_map_has_unit_data(map) && pf_map_unit_field(map, 0, 0, 0) != 0;
  win.unit_sheet.defaults_before = win.unit_sheet.defaults;

  win.upgrade_sheet.map = map;
  win.upgrade_sheet.icons = icons;
  win.upgrade_sheet.defaults =
      pf_map_has_upgrade_data(map) && pf_map_upgrade_field(map, 0, 0) != 0;
  win.upgrade_sheet.defaults_before = win.upgrade_sheet.defaults;

  win.allow_sheet.map = map;
  win.allow_sheet.icons = icons;
  // What the game will read, not what the section says: with no section the core
  // reads -1, and sixteen rows of every bit unset is the opposite of the truth.
  win.allow_sheet.present = pf_map_has_restrictions(map) != 0;
  for (int b = 0; b < 6; b++) {
    for (int p = 0; p < 16; p++) {
      win.allow_sheet.bits[b][p] =
          win.allow_sheet.present ? pf_map_allow(map, b, p) : AllowDefault(b);
      win.allow_sheet.before[b][p] = win.allow_sheet.bits[b][p];
    }
  }
  // A map with no section is using the game's defaults, which is exactly what
  // the tick says. Nothing to infer beyond that.
  win.allow_sheet.defaults = !win.allow_sheet.present;

  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAP_SHEETS), owner,
                      SheetsProc, LPARAM(&win)) != IDOK) {
    return false;
  }

  // Each page in turn. Several may have changed — that is the point of the
  // window — so the notes are collected and the last one wins the status bar,
  // with a count when there is more than one. The log keeps them all.
  std::wstring one;
  int changed = 0;
  std::vector<int> races_changed;
  auto ran = [&](bool did) {
    if (!did) return;
    changed++;
    note = one;
  };
  ran(ApplyMapSheet(win.map_sheet, map, one));
  // Applied like any other page, but the one whose result the caller needs
  // beyond "the map changed": afterwards it is a new grid, and both selections
  // and both views are addressed in the old one.
  const bool resized = ApplyResize(win.map_sheet, map, one);
  ran(resized);
  ran(ApplyPlayerSheet(win.player_sheet, map, one, &races_changed));
  ran(ApplyUnitSheet(win.unit_sheet, map, one));
  ran(ApplyUpgradeSheet(win.upgrade_sheet, map, one));
  // Unconditional now: the page decides for itself whether anything moved, and
  // creating the section on the first tick is part of what it does.
  ran(ApplyAllowSheet(win.allow_sheet, map, one));

  if (outcome) {
    outcome->resized = resized;
    outcome->dropped_units = win.map_sheet.dropped_units;
    outcome->races_changed = std::move(races_changed);
  }

  if (changed == 0) note = Str(IDS_SHEETS_NONE_CHANGED);
  if (changed > 1) note = Format(IDS_SHEETS_MANY_CHANGED, changed);
  // Louder than either, whatever else went through: a refused resize is the one
  // thing that was asked for and did not happen.
  if (win.map_sheet.resize_failed) note = Str(IDS_RESIZE_FAILED);
  return changed > 0;
}

namespace {

// ------------------------------------------------------------- quick pick
//
// Type a few letters, get the unit. The palette is where browsing happens; this
// deserves its own window rather than a mode the palette drops into, which meant
// losing your place in the grid every time you searched.
//
// The core ranks the matches (pf_unit_name_filter), so this client and the web
// one agree about what "dk" means.

struct QuickSheet {
  IconCache* icons = nullptr;
  int chosen = -1;
  /// Unit ids currently listed, in rank order. Parallel to the listbox.
  std::vector<int> rows;
  /// Which race's sections lead, matching the grid this shadows.
  char lead_race = 'h';
};

/// The 40 best matches, the way the web client caps it: past that the list is
/// not a shortlist any more and typing one more letter is quicker than
/// scrolling.
/// The 40 best matches, the way the web client caps it: past that the list is
/// not a shortlist any more and typing one more letter is quicker than
/// scrolling.
constexpr int kQuickRows = 40;

void RefreshQuickRows(HWND dialog, QuickSheet& sheet) {
  wchar_t typed[128] = {};
  GetDlgItemTextW(dialog, IDC_QUICK_SEARCH, typed, 128);

  sheet.rows = UnitsInPaletteOrder(sheet.lead_race);
  const int kept = pf_unit_name_filter(ToUtf8(typed).c_str(), sheet.rows.data(),
                                       int(sheet.rows.size()));
  sheet.rows.resize(size_t(std::max(0, std::min(kept, kQuickRows))));

  HWND list = GetDlgItem(dialog, IDC_QUICK_LIST);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (int id : sheet.rows) {
    // Name, then the heading it sits under in the grid, from the grid's own
    // answer — two units can read alike and the group tells them apart.
    const char* name = pf_unit_name(id);
    std::wstring label = name ? FromUtf8(name) : L"?";
    label += L"    " + UnitGroupHeading(id);
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
  }
  if (sheet.rows.empty()) {
    // Say so rather than showing an empty box, which reads as "still thinking".
    // Nothing selects it: TakeQuickPick checks the row against `rows`, which
    // this line is deliberately not in.
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(Str(IDS_NOTHING_MATCHES).c_str()));
  }
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  if (!sheet.rows.empty()) SendMessageW(list, LB_SETCURSEL, 0, 0);
}

/// Take whatever the list has highlighted and finish.
///
/// Reached through WM_COMMAND/IDOK rather than called directly from the mouse
/// path: a click on the list arrives while the listbox still holds the mouse
/// capture, and ending a dialog mid-capture leaves the button-up to land on
/// whatever is behind it — which put a brush stroke on the map every time a unit
/// was picked with the mouse.
void TakeQuickPick(HWND dialog, QuickSheet& sheet) {
  const int at = int(SendDlgItemMessageW(dialog, IDC_QUICK_LIST, LB_GETCURSEL, 0, 0));
  if (at < 0 || at >= int(sheet.rows.size())) return;
  sheet.chosen = sheet.rows[size_t(at)];
  EndDialog(dialog, IDOK);
}

/// The search box's arrows drive the list rather than the caret.
///
/// Without this the only way to reach the second match would be to leave the
/// box, and the whole interaction is meant to be type, arrow, Enter.
LRESULT CALLBACK QuickSearchProc(HWND hwnd, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
  HWND dialog = reinterpret_cast<HWND>(ref);
  // The arrows, and nothing else. The dialog manager would otherwise take them
  // to move between controls. Enter and Escape stay with it, which routes them
  // to IDOK and IDCANCEL.
  if (message == WM_GETDLGCODE) return DLGC_WANTARROWS | DLGC_WANTCHARS;
  // Enter takes the highlighted row. The dialog manager would do this for a
  // default pushbutton, and this box deliberately has no buttons at all.
  if (message == WM_KEYDOWN && (wparam == VK_UP || wparam == VK_DOWN ||
                                wparam == VK_PRIOR || wparam == VK_NEXT)) {
    HWND list = GetDlgItem(dialog, IDC_QUICK_LIST);
    const int count = int(SendMessageW(list, LB_GETCOUNT, 0, 0));
    if (count > 0) {
      const int at = int(SendMessageW(list, LB_GETCURSEL, 0, 0));
      const int step = wparam == VK_UP ? -1 : wparam == VK_DOWN ? 1
                     : wparam == VK_PRIOR ? -10 : 10;
      // Wraps, so holding Down runs round the shortlist rather than sticking.
      int next = (at < 0 ? 0 : at) + step;
      next = ((next % count) + count) % count;
      SendMessageW(list, LB_SETCURSEL, WPARAM(next), 0);
    }
    return 0;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

/// A click anywhere on a row takes it, including on the row already under the
/// cursor — LBN_SELCHANGE alone would ignore that, and "click the top match"
/// is the most natural thing to do with a shortlist. On button-up, so the
/// listbox has released its capture before the dialog ends.
LRESULT CALLBACK QuickListProc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
  if (message == WM_LBUTTONUP) {
    const LRESULT r = DefSubclassProc(hwnd, message, wparam, lparam);
    PostMessageW(reinterpret_cast<HWND>(ref), WM_COMMAND,
                 MAKEWPARAM(IDOK, BN_CLICKED), 0);
    return r;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

INT_PTR CALLBACK QuickPickProc(HWND dialog, UINT message, WPARAM wparam,
                               LPARAM lparam) {
  QuickSheet* sheet = reinterpret_cast<QuickSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    // The shared list height, which is a portrait's own 38 pixels: this box had
    // its own taller measure until every list here grew to match it.
    case WM_MEASUREITEM:
      return MeasureIconRow(dialog, lparam);

    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
      if (!draw || draw->CtlID != IDC_QUICK_LIST) return FALSE;
      const Icon* icon = nullptr;
      if (sheet && sheet->icons && draw->itemID != UINT(-1) &&
          draw->itemID < sheet->rows.size()) {
        icon = &sheet->icons->Unit(sheet->rows[draw->itemID]);
      }
      return DrawIconRow(lparam, icon);
    }

    // Centred, like every other window here.
    //
    // It used to open under the pointer, on the reasoning that a box appearing
    // where the hand already is costs no travel. What that gives is a window
    // that lands somewhere different every time — and this one has a text box
    // you type into, so the eye has to find it first.
    case WM_INITDIALOG: {
      sheet = reinterpret_cast<QuickSheet*>(lparam);
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));

      CentreOnScreen(dialog);

      SendDlgItemMessageW(dialog, IDC_QUICK_SEARCH, EM_SETCUEBANNER, TRUE,
                          reinterpret_cast<LPARAM>(Str(IDS_FIND_A_UNIT).c_str()));
      SetWindowSubclass(GetDlgItem(dialog, IDC_QUICK_SEARCH), QuickSearchProc, 1,
                        reinterpret_cast<DWORD_PTR>(dialog));
      SetWindowSubclass(GetDlgItem(dialog, IDC_QUICK_LIST), QuickListProc, 1,
                        reinterpret_cast<DWORD_PTR>(dialog));
      RefreshQuickRows(dialog, *sheet);
      SetFocus(GetDlgItem(dialog, IDC_QUICK_SEARCH));
      return FALSE;   // focus set by hand, above
    }

    case WM_ACTIVATE:
      // Looking elsewhere dismisses it; there is no close button to miss. Only
      // when nothing has been chosen: ending the dialog is itself what takes its
      // activation away, so without the guard every successful pick was followed
      // by a second EndDialog that overwrote IDOK with IDCANCEL.
      if (LOWORD(wparam) == WA_INACTIVE && sheet && sheet->chosen < 0) {
        EndDialog(dialog, IDCANCEL);
      }
      return TRUE;

    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam), code = HIWORD(wparam);
      if (id == IDC_QUICK_SEARCH && code == EN_CHANGE) {
        RefreshQuickRows(dialog, *sheet);
        return TRUE;
      }
      if (id == IDOK) { TakeQuickPick(dialog, *sheet); return TRUE; }
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      return FALSE;
    }

    default:
      return FALSE;
  }
}

}  // namespace

int ShowQuickPick(HWND owner, HINSTANCE instance, IconCache* icons,
                  char lead_race) {
  QuickSheet sheet;
  sheet.icons = icons;
  sheet.lead_race = lead_race;
  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_QUICK_PICK), owner,
                      QuickPickProc, LPARAM(&sheet)) != IDOK) {
    return -1;
  }
  return sheet.chosen;
}

int ShowTilePicker(HWND owner, HINSTANCE instance, const pf_tileset_art* art,
                   int tileset, int current) {
  if (!art) return -1;
  TileSheet sheet;
  sheet.art = art;
  sheet.tileset = tileset;
  sheet.chosen = current;
  sheet.instance = instance;
  sheet.Gather();
  if (sheet.tiles.empty()) return -1;

  if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_TILE_PICKER), owner,
                      TilesProc, LPARAM(&sheet)) != IDOK) {
    return -1;
  }
  return sheet.chosen;
}

}  // namespace pfwin

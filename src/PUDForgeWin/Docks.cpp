#include "Docks.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <string>

#include "Dialogs.hpp"
#include "GameData.hpp"
#include "Icons.hpp"
#include "Strings.hpp"
#include "resource.h"
#include "strings.h"

namespace pfwin {
namespace {

/// The movement palette's last cell, which puts a tile back to what its terrain
/// implies. One past the classes, the way Custom is one past the terrains.
int MovementResetCell() { return pf_movement_class_count(); }

constexpr wchar_t kTerrainClass[] = L"PUDForgeTerrainPanel";
constexpr wchar_t kUnitsClass[] = L"PUDForgeUnitsPanel";

/// Row geometry at 96 DPI, scaled by the window's DPI.
constexpr int kRowBase = 26;
constexpr int kPadBase = 6;
constexpr int kLabelBase = 44;

int Scaled(HWND hwnd, int base) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return MulDiv(base, int(dpi ? dpi : 96), 96);
}

void SetFont(HWND hwnd) {
  SendMessageW(hwnd, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

HWND MakeButton(HWND parent, HINSTANCE instance, const wchar_t* text, int id,
                DWORD style) {
  HWND hwnd = CreateWindowExW(
      0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
      reinterpret_cast<HMENU>(INT_PTR(id)), instance, nullptr);
  SetFont(hwnd);
  return hwnd;
}

HWND MakeLabel(HWND parent, HINSTANCE instance, const wchar_t* text) {
  // SS_CENTERIMAGE centres the text down the label's own height, so a row's
  // label lines up with the middle of its buttons instead of being nudged down
  // by a hand-picked number of pixels that only holds at one font size.
  HWND hwnd = CreateWindowExW(0, L"STATIC", text,
                              WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 0,
                              0, parent, nullptr, instance, nullptr);
  SetFont(hwnd);
  return hwnd;
}

}  // namespace

namespace {

/// Draw these items with a bullet rather than a tick.
///
/// Every sizing item is one answer to one question, and a tick beside each reads
/// as that many independent switches that happen to be off.
void MarkRadio(HMENU menu, int first, int count) {
  MENUITEMINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_FTYPE;
  info.fType = MFT_RADIOCHECK;   // MFT_STRING is 0, so this keeps the text
  for (int i = 0; i < count; i++) {
    SetMenuItemInfoW(menu, UINT(first + i), TRUE, &info);
  }
}

}  // namespace

int AskDockMenu(HWND owner, POINT at, int pinned, bool on_right,
                const wchar_t* extra) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return kDockMenuDismissed;
  // What the pointer is over comes before what the panel is: the right button
  // was pressed on a thing, and the panel's own settings are the fallback.
  int position = 0;
  if (extra && *extra) {
    AppendMenuW(menu, MF_STRING, UINT(IDM_DOCK_EXTRA), extra);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    position = 2;
  }
  // Then sizing: it is the setting reached most often, and the side is a thing
  // you set once. A dock with no palette passes a mode that is neither, which is
  // how the minimap gets the same menu without a nonsense half.
  const bool has_columns = pinned > 0 || pinned == kPaletteFitPanel ||
                           pinned == kPaletteScaleTiles;
  if (has_columns) {
    // One question — which of the count and the tile size the panel's width may
    // move — and the two answers to it. The three hand-picked counts that used
    // to sit below a rule said nothing the panel's own width does not, and five
    // items made a sizing choice look like a settings page.
    AppendMenuW(menu, MF_STRING | (pinned == kPaletteFitPanel ? MF_CHECKED : 0),
                UINT(IDM_COLUMNS_AUTO), Str(IDS_COLUMNS_AUTO).c_str());
    AppendMenuW(menu,
                MF_STRING | (pinned == kPaletteScaleTiles ? MF_CHECKED : 0),
                UINT(IDM_COLUMNS_SCALE), Str(IDS_COLUMNS_SCALE).c_str());
    // By position, which is why this sits against the appends that decide it,
    // and why `position` is counted rather than assumed.
    MarkRadio(menu, position, 2);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  // The side it is already on is greyed rather than hidden: a menu whose items
  // move about depending on state is one you have to read every time.
  AppendMenuW(menu, MF_STRING | (on_right ? MF_ENABLED : MF_GRAYED | MF_CHECKED),
              UINT(IDM_DOCK_LEFT), Str(IDS_DOCK_LEFT).c_str());
  AppendMenuW(menu, MF_STRING | (on_right ? MF_GRAYED | MF_CHECKED : MF_ENABLED),
              UINT(IDM_DOCK_RIGHT), Str(IDS_DOCK_RIGHT).c_str());

  const int picked = int(TrackPopupMenu(
      menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, at.x, at.y, 0,
      owner, nullptr));
  DestroyMenu(menu);
  if (picked == IDM_COLUMNS_AUTO) return kPaletteFitPanel;
  if (picked == IDM_COLUMNS_SCALE) return kPaletteScaleTiles;
  if (picked == IDM_DOCK_LEFT) return kDockMenuLeft;
  if (picked == IDM_DOCK_RIGHT) return kDockMenuRight;
  if (picked == IDM_DOCK_EXTRA) return kDockMenuExtra;
  return kDockMenuDismissed;
}

namespace {

/// The kinds a race's units are split into, in the order they are shown.
const struct { UINT name; int category; } kKinds[] = {
    {IDS_KIND_LAND, PF_CATEGORY_LAND},
    {IDS_KIND_AIR, PF_CATEGORY_AIR},
    {IDS_KIND_WATER, PF_CATEGORY_WATER},
    {IDS_KIND_BUILDINGS, PF_CATEGORY_BUILDING},
    {IDS_KIND_HEROES, PF_CATEGORY_HERO},
};

}  // namespace

std::wstring UnitGroupHeading(int id) {
  const char race = pf_unit_race(id);
  const int kind = pf_unit_category(id);
  // The start locations carry a race but are markers, so they are their own
  // group rather than the tail of their side's.
  if (kind == PF_CATEGORY_SPECIAL && race != 'n') return Str(IDS_GROUP_MARKERS);
  if (race == 'n') return Str(IDS_GROUP_NEUTRAL);
  for (const auto& one : kKinds) {
    if (one.category != kind) continue;
    return Format(IDS_GROUP_HEADING,
                  Str(race == 'o' ? IDS_RACE_ORC : IDS_RACE_HUMAN).c_str(),
                  Str(one.name).c_str());
  }
  return Str(IDS_GROUP_NEUTRAL);
}

std::vector<int> UnitsInPaletteOrder(char lead, bool with_unused) {
  std::vector<int> ids;
  ids.reserve(PF_UNIT_COUNT);
  const char first = lead == 'o' ? 'o' : 'h';
  const char second = first == 'h' ? 'o' : 'h';
  for (char race : {first, second}) {
    for (const auto& kind : kKinds) {
      for (int id = 0; id < PF_UNIT_COUNT; id++) {
        if (!Editor::ListsUnit(id, with_unused) || pf_unit_race(id) != race ||
            pf_unit_category(id) != kind.category) {
          continue;
        }
        ids.push_back(id);
      }
    }
  }
  // Everything neutral: critters, mines, oil, the buildings you rescue.
  for (int id = 0; id < PF_UNIT_COUNT; id++) {
    if (Editor::ListsUnit(id, with_unused) && pf_unit_race(id) == 'n') {
      ids.push_back(id);
    }
  }
  // Then the markers, which the runs above deliberately did not take.
  for (int id = 0; id < PF_UNIT_COUNT; id++) {
    if (Editor::ListsUnit(id, with_unused) && pf_unit_race(id) != 'n' &&
        pf_unit_category(id) == PF_CATEGORY_SPECIAL) {
      ids.push_back(id);
    }
  }
  return ids;
}

// ============================================================ TerrainPanel

bool TerrainPanel::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &TerrainPanel::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = kTerrainClass;
  return RegisterClassExW(&wc) != 0;
}

HWND TerrainPanel::Create(HWND parent, HINSTANCE instance, Editor* editor,
                          Host* host) {
  instance_ = instance;
  editor_ = editor;
  host_ = host;
  // WS_CLIPCHILDREN: the class has a background brush, so without it every
  // resize fills the whole panel grey and *then* the palette redraws over it.
  // Dragging the seam did that on every mouse move.
  return CreateWindowExW(0, kTerrainClass, nullptr,
                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0,
                         0, 0, 0, parent, nullptr, instance, this);
}

LRESULT CALLBACK TerrainPanel::Proc(HWND hwnd, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
  TerrainPanel* self =
      reinterpret_cast<TerrainPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<TerrainPanel*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT TerrainPanel::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      Build();
      return 0;
    case WM_SIZE:
      Layout();
      return 0;
      // The palette raises this too, but the panel is mostly not palette — the
      // rows below it and the margin around it are what somebody grabs at when
      // they mean "this whole thing".
    case WM_CONTEXTMENU: {
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (at.x == -1 && at.y == -1) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        at = {rc.right / 2, rc.bottom / 2};
        ClientToScreen(hwnd_, &at);
      }
      PickColumns(at);
      return 0;
    }
    case WM_COMMAND:
      OnCommand(LOWORD(wparam));
      return 0;
    case WM_HSCROLL:
      // The one slider. Steps by index over pf_brush_size, not by value.
      if (reinterpret_cast<HWND>(lparam) == size_slider_) {
        const int index = int(SendMessageW(size_slider_, TBM_GETPOS, 0, 0));
        editor_->brush_size = pf_brush_size(index);
        Refresh();
        if (host_) host_->OnEditorChanged();
      }
      return 0;
    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}

void TerrainPanel::Build() {
  // No tool buttons at all. Clicking a cell is what "paint with this" means, the
  // same way clicking a unit means placing one; selecting a rectangle is on the
  // strip; and the eyedropper is Ctrl and the left button, which the status bar
  // says out loud. A button for a chord the pointer already performs is a button
  // that has to be kept in step with it.
  palette_.Create(hwnd_, instance_, IDC_TERRAIN_PALETTE);
  // Three or four across, whichever the dock's width is nearer. A brush is a
  // texture rather than a portrait, and a texture wants to be big enough to
  // recognise the pattern in.
  palette_.SetColumns(3, 5, false);
  palette_.on_context = [this](POINT at, int) { PickColumns(at); };
  palette_.draw_icon = [this](HDC dc, const RECT& rect, int brush) {
    DrawBrushIcon(dc, rect, brush);
  };
  palette_.on_pick = [this](int brush) {
    if (editor_->mode() == Mode::kMovement) {
      editor_->movement_from_terrain = brush == MovementResetCell();
      if (!editor_->movement_from_terrain) {
        editor_->movement_value = pf_movement_class_value(brush);
      }
      editor_->SetTool(Tool::kWalkable);
      palette_.SetSelected(brush);
      Refresh();
      if (host_) host_->OnEditorChanged();
      return;
    }
    const int previous = editor_->brush_index;
    editor_->SetBrush(brush);
    // Always painting, not "painting unless a rectangle was being dragged":
    // with no Paint button left, choosing a terrain is the only way back to the
    // brush.
    editor_->SetTool(Tool::kPaint);
    if (editor_->BrushIsCustom() && !PickCustomTile() && editor_->custom_tile < 0) {
      // Cancelled with nothing chosen: the custom brush would paint nothing, so
      // leave the user on the brush they had rather than on a dead one.
      editor_->brush_index = previous;
      palette_.SetSelected(previous);
    }
    if (host_) {
      host_->OnEditorChanged();
      host_->OnStatus(FromUtf8(editor_->BrushName()), false);
    }
  };

  // Fill and Clear were here, appearing and disappearing with the rectangle;
  // both are on the canvas's right-click menu now, where the rectangle is, and a
  // row that comes and goes moved everything under it.

  // Under the palette: a 38-pixel cell of pixel art does not say "Dark Mud", and
  // the tooltip only says it while the pointer is on that cell.
  brush_name_ = MakeLabel(hwnd_, instance_, L"");
  SetWindowLongPtrW(brush_name_, GWL_STYLE,
                    GetWindowLongPtrW(brush_name_, GWL_STYLE) | SS_CENTER |
                        SS_ENDELLIPSIS);

  // Created in the order they are laid out, because tab order follows creation
  // order. Each row opens a WS_GROUP so the arrow keys stay inside the row the
  // eye is on: without one on the slider and on Mirror, both joined the radio
  // group above them and an arrow key walked out of its row.
  labels_[1] = MakeLabel(hwnd_, instance_, Str(IDS_ROW_SHAPE).c_str());
  const wchar_t* shape[] = {L"■", L"●", L"⁂", L"▨"};
  for (int i = 0; i < 4; i++) {
    shape_[i] = MakeButton(hwnd_, instance_, shape[i], IDC_SHAPE_FIRST + i,
                           BS_AUTORADIOBUTTON | BS_PUSHLIKE |
                               (i == 0 ? WS_GROUP : 0));
  }

  labels_[2] = MakeLabel(hwnd_, instance_, Str(IDS_ROW_SIZE).c_str());
  size_slider_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_GROUP | TBS_HORZ | TBS_AUTOTICKS,
                                 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(INT_PTR(IDC_BRUSH_SIZE)),
                                 instance_, nullptr);
  SendMessageW(size_slider_, TBM_SETRANGE, TRUE,
               MAKELPARAM(0, pf_brush_size_count() - 1));
  size_value_ = MakeLabel(hwnd_, instance_, L"1");

  labels_[0] = MakeLabel(hwnd_, instance_, Str(IDS_ROW_DETAIL).c_str());
  const UINT detail[] = {IDS_DETAIL_PLAIN, IDS_DETAIL_MIXED, IDS_DETAIL_DETAIL};
  for (int i = 0; i < 3; i++) {
    detail_[i] = MakeButton(hwnd_, instance_, Str(detail[i]).c_str(), IDC_DETAIL_FIRST + i,
                            BS_AUTORADIOBUTTON | BS_PUSHLIKE |
                                (i == 0 ? WS_GROUP : 0));
  }

  // Three radios, not two and a tick: which drawing of the terrain a stroke lays
  // is one question with three answers, and Mix is the third. A checkbox beside
  // two radios said a stroke could be dark *and* mixed, which never meant
  // anything.
  labels_[4] = MakeLabel(hwnd_, instance_, Str(IDS_ROW_SHADE).c_str());
  const UINT shades[] = {IDS_SHADE_LIGHT, IDS_SHADE_DARK, IDS_MIX_SHADES_SHORT};
  for (int i = 0; i < 3; i++) {
    shade_[i] = MakeButton(hwnd_, instance_, Str(shades[i]).c_str(),
                           IDC_SHADE_FIRST + i,
                           BS_AUTORADIOBUTTON | BS_PUSHLIKE |
                               (i == 0 ? WS_GROUP : 0));
  }


  labels_[3] = MakeLabel(hwnd_, instance_, Str(IDS_ROW_MIRROR).c_str());
  // Independent toggles after the first: the axes combine, "none" clears.
  const wchar_t* mirror[] = {L"·", L"↔", L"↕", L"⤢", L"⤡"};
  for (int i = 0; i < 5; i++) {
    mirror_[i] = MakeButton(hwnd_, instance_, mirror[i], IDC_MIRROR_FIRST + i,
                            BS_AUTOCHECKBOX | BS_PUSHLIKE |
                                (i == 0 ? WS_GROUP : 0));
  }

  // The two bulk terrain edits, at the foot of the panel. As Tools menu items
  // they put the whole-map terrain work a menu away from the brushes that do the
  // same job a tile at a time. Created last because tab order follows creation.
  bulk_[0] = MakeButton(hwnd_, instance_, Str(IDS_BULK_REPLACE).c_str(),
                        IDC_TERRAIN_REPLACE, WS_GROUP);
  bulk_[1] = MakeButton(hwnd_, instance_, Str(IDS_BULK_DECORATE).c_str(),
                        IDC_TERRAIN_DECORATE, 0);

  // Last of all, at the foot: which of the two the panel is painting in.
  const UINT modes[] = {IDS_PANEL_MODE_TERRAIN, IDS_PANEL_MODE_MOVEMENT};
  for (int i = 0; i < 2; i++) {
    mode_[i] = MakeButton(hwnd_, instance_, Str(modes[i]).c_str(),
                          IDC_PANEL_MODE_FIRST + i,
                          BS_AUTORADIOBUTTON | BS_PUSHLIKE |
                              (i == 0 ? WS_GROUP : 0));
  }
  Explain(mode_[0], IDS_TIP_PANEL_TERRAIN);
  Explain(mode_[1], IDS_TIP_PANEL_MOVEMENT);

  const UINT tips[] = {
      IDS_TIP_DETAIL_PLAIN, IDS_TIP_DETAIL_MIXED, IDS_TIP_DETAIL_DETAIL,
      IDS_TIP_SHAPE_SQUARE, IDS_TIP_SHAPE_CIRCLE, IDS_TIP_SHAPE_SCATTER,
      IDS_TIP_SHAPE_FILL,
      IDS_TIP_MIRROR_NONE, IDS_TIP_MIRROR_LR, IDS_TIP_MIRROR_TB,
      IDS_TIP_MIRROR_SWNE, IDS_TIP_MIRROR_NWSE,
      IDS_TIP_SHADE_LIGHT, IDS_TIP_SHADE_DARK, IDS_TIP_SHADE};
  HWND explained[] = {detail_[0], detail_[1], detail_[2],
                      shape_[0], shape_[1], shape_[2], shape_[3],
                      mirror_[0], mirror_[1], mirror_[2], mirror_[3], mirror_[4],
                      shade_[0], shade_[1], shade_[2]};
  for (size_t i = 0; i < std::size(explained); i++) Explain(explained[i], tips[i]);
  Explain(size_slider_, IDS_TIP_SIZE);
  // Two words on a button cannot say what the edit covers, and what it covers is
  // the thing worth knowing before pressing it.
  Explain(bulk_[0], IDS_TIP_BULK_REPLACE);
  Explain(bulk_[1], IDS_TIP_BULK_DECORATE);

  RebuildPalette();
  Refresh();
}

void TerrainPanel::SetUiIcons(const UiIcons* icons) {
  if (!icons) return;
  // The rows in the order the sheet is drawn in, which is the order they are on
  // screen. A cell nobody has drawn yet leaves its glyph alone, so this can be
  // called against a half-filled sheet and the panel still reads.
  struct Row { HWND* buttons; int count; int first; };
  const Row rows[] = {
      {detail_, 3, kIconDetailPlain},
      {shape_, 4, kIconShapeSquare},
      {mirror_, 5, kIconMirrorNone},
      {shade_, 3, kIconShadeLight},
  };
  for (const Row& row : rows) {
    for (int i = 0; i < row.count; i++) icons->Decorate(row.buttons[i], row.first + i);
  }
}

void TerrainPanel::Explain(HWND control, UINT text) {
  ExplainWith(control, Str(text));
}

void TerrainPanel::ExplainWith(HWND control, const std::wstring& text) {
  if (!control) return;
  if (!tip_) {
    tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                           hwnd_, nullptr, instance_, nullptr);
    if (!tip_) return;
    // Long enough to be read: the default clips at about 200 pixels and these
    // are sentences.
    SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, 320);
  }
  tip_texts_.push_back(text);
  TOOLINFOW info = {};
  info.cbSize = sizeof(info);
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = hwnd_;
  info.uId = reinterpret_cast<UINT_PTR>(control);
  info.lpszText = const_cast<wchar_t*>(tip_texts_.back().c_str());
  SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

void TerrainPanel::PickColumns(POINT screen) {
  const bool right = host_ && host_->DockIsRight(Host::Dock::kTerrain);
  const int picked = AskDockMenu(hwnd_, screen, palette_.column_count(), right);
  if (picked == kDockMenuDismissed) return;
  if (picked == kDockMenuLeft || picked == kDockMenuRight) {
    if (host_) host_->OnDockSide(Host::Dock::kTerrain, picked == kDockMenuRight);
    return;
  }
  palette_.SetColumnCount(picked);
  Layout();
  InvalidateRect(hwnd_, nullptr, TRUE);
  if (host_) host_->OnEditorChanged();
}

void TerrainPanel::SetArtwork(const pf_tileset_art* art, int tileset) {
  art_ = art;
  tileset_ = tileset;
  icons_.clear();
  RebuildPalette();
}

int TerrainPanel::SelectedMovementCell() const {
  if (!editor_) return 0;
  if (editor_->movement_from_terrain) return MovementResetCell();
  const int cls = editor_->MovementClassIndex();
  // A value the bits have taken somewhere no class names lights no cell: the
  // palette would otherwise say the brush lays something it does not.
  return cls >= 0 ? cls : -1;
}

void TerrainPanel::RebuildMovementPalette() {
  // Flat colours, and the overlay's own: a cell and the tiles it paints are
  // then the same colour, which is the whole of what has to be learned.
  // Indexed by cell id, which is unchanged by where a cell is shown.
  icons_.assign(size_t(pf_movement_class_count()) + 1, Icon{});
  for (int i = 0; i < pf_movement_class_count(); i++) {
    icons_[size_t(i)] = FlatIcon(pf_movement_colour(pf_movement_class_value(i)));
  }

  // The way back comes first: not a class but the absence of one, and the cell
  // reached for most often. An override comes off a tile at a time here, where
  // Reset Movement takes the lot.
  std::vector<PaletteGrid::Entry> entries;
  PaletteGrid::Entry back;
  back.id = MovementResetCell();
  back.label = Str(IDS_MOVE_FROM_TERRAIN);
  entries.push_back(std::move(back));

  for (int i = 0; i < pf_movement_class_count(); i++) {
    // Named but not offered: a value a real map holds still has to read as
    // something under the pointer, without being a cell to paint more of.
    if (!pf_movement_class_offered(i)) continue;
    PaletteGrid::Entry entry;
    entry.id = i;
    const char* name = pf_movement_class_name(i);
    entry.label = name ? FromUtf8(name) : L"";
    entries.push_back(std::move(entry));
  }

  palette_.SetEntries(std::move(entries));
  palette_.SetSelected(SelectedMovementCell());
}

void TerrainPanel::RebuildPalette() {
  if (editor_ && editor_->mode() == Mode::kMovement) {
    RebuildMovementPalette();
    return;
  }
  // Brush icons: the solid tile the brush paints, or the flat colour when the
  // artwork is absent - the same fallback the renderer uses.
  icons_.assign(size_t(pf_brush_count()) + 1, Icon{});
  for (int i = 0; i < pf_brush_count(); i++) {
    const int terrain = pf_brush_terrain(i);
    const int tile = pf_solid_tile(terrain, 0);
    const int mega = (art_ && tile >= 0)
        ? pf_tileset_art_megatile_for(art_, uint16_t(tile)) : -1;
    if (mega >= 0 && !pf_tileset_art_is_blank(art_, mega)) {
      Icon icon;
      icon.w = icon.h = 32;
      icon.fill = true;   // a square tile in a square cell: no margin needed
      icon.px.resize(32 * 32);
      pf_tileset_art_draw(art_, mega, icon.px.data(), 32);
      icons_[size_t(i)] = std::move(icon);
    } else {
      icons_[size_t(i)] = FlatIcon(pf_terrain_flat_colour(terrain, tileset_));
    }
  }

  // The custom cell draws the tile it paints once one has been picked. Until
  // then DrawBrushIcon falls through to the ellipsis that means "choose".
  const int custom_tile = editor_ ? editor_->custom_tile : -1;
  const int custom_mega =
      (art_ && custom_tile >= 0)
          ? pf_tileset_art_megatile_for(art_, uint16_t(custom_tile))
          : -1;
  if (custom_mega >= 0 && !pf_tileset_art_is_blank(art_, custom_mega)) {
    Icon icon;
    icon.w = icon.h = 32;
    icon.fill = true;
    icon.px.resize(32 * 32);
    pf_tileset_art_draw(art_, custom_mega, icon.px.data(), 32);
    icons_[size_t(pf_brush_count())] = std::move(icon);
  }

  // One cell per terrain, not per drawing: the dark member of each pair is
  // reached through the Light/Dark switch below. Ten cells for seven terrains,
  // with the pairs adjacent and separately named, read as ten terrains.
  std::vector<PaletteGrid::Entry> entries;
  for (int i = 0; i < pf_brush_count(); i++) {
    if (pf_brush_shade(i) < 0) continue;
    PaletteGrid::Entry entry;
    entry.id = i;
    const char* name = pf_terrain_name(pf_brush_terrain(i), tileset_);
    entry.label = name ? FromUtf8(name) : L"";
    entries.push_back(std::move(entry));
  }
  // The last cell: one specific tile, chosen from the whole tileset. Without it
  // a good third of the artwork is unreachable — PUDDraft called it Custom.
  PaletteGrid::Entry custom;
  custom.id = pf_brush_count();
  if (editor_ && editor_->custom_tile >= 0) {
    custom.label = Format(IDS_CUSTOM_TILE, unsigned(editor_->custom_tile));
  } else {
    custom.label = Str(IDS_PICK_A_TILE);
  }
  entries.push_back(std::move(custom));
  palette_.SetEntries(std::move(entries));
  palette_.SetSelected(editor_ ? editor_->brush_index : 0);
}

bool TerrainPanel::PickCustomTile() {
  if (!art_) {
    if (host_) {
      host_->OnStatus(Str(IDS_NO_ARTWORK_TO_PICK), true);
    }
    return false;
  }
  const int tile = ShowTilePicker(GetAncestor(hwnd_, GA_ROOT), instance_, art_,
                                  tileset_, editor_->custom_tile);
  if (tile < 0) return false;
  editor_->custom_tile = tile;
  // The custom cell should now show the tile it paints rather than an ellipsis:
  // "which tile did I pick" is a question the palette can answer for free.
  RebuildPalette();
  return true;
}

/// A movement cell: the class colour, with its name written over it.
///
/// Colour alone was not enough once there were eleven of them — the two shore
/// mixes and the two walls are four blues and two greys, and nobody learns
/// which is which by looking. The name goes on the cell rather than in a
/// tooltip, so choosing does not mean hovering each one in turn.
void DrawMovementCell(HDC dc, const RECT& rect, const std::wstring& name,
                      uint32_t colour) {
  const COLORREF back = RGB((colour >> 16) & 0xff, (colour >> 8) & 0xff, colour & 0xff);
  HBRUSH fill = CreateSolidBrush(back);
  FillRect(dc, &rect, fill);
  DeleteObject(fill);

  // Black on the light half, white on the dark: the eleven run from a near
  // black to a pale green, and one ink cannot be read on both.
  const int luma = (((colour >> 16) & 0xff) * 299 + ((colour >> 8) & 0xff) * 587 +
                    (colour & 0xff) * 114) / 1000;
  SetTextColor(dc, luma > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255));
  SetBkMode(dc, TRANSPARENT);
  RECT text = rect;
  InflateRect(&text, -2, -2);
  // DT_VCENTER only works on one line, and these wrap to three. Measure what
  // the wrapped text needs, then move the box down by half of what is left
  // over: a name that wraps and one that does not then sit at the same height.
  const UINT format = DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
  RECT measured = text;
  DrawTextW(dc, name.c_str(), -1, &measured, format | DT_CALCRECT);
  const int slack = (text.bottom - text.top) - (measured.bottom - measured.top);
  if (slack > 0) text.top += slack / 2;
  DrawTextW(dc, name.c_str(), -1, &text, format);
}

void TerrainPanel::DrawBrushIcon(HDC dc, const RECT& rect, int brush) {
  if (editor_ && editor_->mode() == Mode::kMovement) {
    if (brush == MovementResetCell()) {
      DrawMovementCell(dc, rect, Str(IDS_MOVE_FROM_TERRAIN), 0xf0f0f0);
    } else {
      const char* name = pf_movement_class_name(brush);
      DrawMovementCell(dc, rect, name ? FromUtf8(name) : L"?",
                       pf_movement_colour(pf_movement_class_value(brush)));
    }
    return;
  }
  // The cell shows the drawing the switch has chosen, so a palette on Dark is a
  // picture of what the next stroke lays.
  if (editor_ && editor_->mode() != Mode::kMovement && editor_->DarkWanted() &&
      brush < pf_brush_count()) {
    const int terrain = pf_brush_terrain(brush);
    const int twin = pf_terrain_other_shade(terrain);
    if (twin != terrain) {
      for (int i = 0; i < pf_brush_count(); i++) {
        if (pf_brush_terrain(i) == twin) { brush = i; break; }
      }
    }
  }
  if (brush < int(icons_.size()) && !icons_[size_t(brush)].empty()) {
    // The cell, edge to edge. The cell is sized from the dock's width, so the
    // tile follows it — the point of holding the column count still rather than
    // the cell.
    BlitIcon(dc, rect, icons_[size_t(brush)]);
    return;
  }
  // The custom cell, and anything with nothing to draw: a label.
  SetBkMode(dc, TRANSPARENT);
  RECT text = rect;
  DrawTextW(dc,
            (editor_ && editor_->mode() == Mode::kMovement) ? L"↺"
            : brush >= pf_brush_count()                     ? L"…"
                                                            : L"?",
            -1, &text,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void TerrainPanel::Layout() {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int pad = Scaled(hwnd_, kPadBase);
  const int row = Scaled(hwnd_, kRowBase);
  const int label_w = Scaled(hwnd_, kLabelBase);
  const int width = rc.right - pad * 2;
  if (width <= 0) return;

  // Every control moves in one pass. Twenty MoveWindow calls are twenty
  // repaints, and a seam drag runs this on every mouse move.
  HDWP defer = BeginDeferWindowPos(28);
  const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
  auto place = [&](HWND control, int x, int y, int w, int h) {
    if (defer) defer = DeferWindowPos(defer, control, nullptr, x, y, w, h, flags);
  };

  // The palette at the top, five settings rows and the bulk edits below it, and
  // the preview between. Nothing on this panel comes and goes any more.
  const int top = pad;
  // The palette takes what its rows need and everything below follows, rather
  // than being pinned to the bottom: with a fixed column count the palette's
  // height comes from the width, so pinning would leave a gap in the middle that
  // moves as the dock is dragged.
  const int name_h = Scaled(hwnd_, 16);
  // Six rows below the palette, not five. The palette gives up the height, so a
  // narrow dock loses cells rather than pushing Mirror and Shade off the bottom.
  //
  // Movement mode shows four of them: no Detail, and no bulk edits. The rows it
  // does without are height the palette gets, which is where the two extra
  // values and the way back went.
  const bool movement = editor_ && editor_->mode() == Mode::kMovement;
  const int most = rc.bottom - pad * 2 - row * (movement ? 4 : 6) - name_h - top;
  const int palette_h = std::max(0, std::min(most, palette_.HeightFor(width)));
  place(palette_.hwnd(), pad, top, width, palette_h);
  place(brush_name_, pad, top + palette_h + Scaled(hwnd_, 3), width, name_h);

  int y = top + palette_h + name_h + pad * 2;

  auto place_row = [&](HWND label, HWND* cells, int count) {
    place(label, pad, y, label_w, row - 2);
    const int cell_w = (width - label_w) / count;
    for (int i = 0; i < count; i++) {
      place(cells[i], pad + label_w + cell_w * i, y, cell_w, row - 2);
    }
    y += row;
  };

  // A row that means nothing in this mode is not shown greyed: a control you
  // cannot press is a question you have to work out the answer to. Detail is
  // about which drawing of a terrain a stroke lays, the bucket floods what the
  // terrain says, and the two bulk edits are terrain edits — none of them has
  // anything to say about a layer that is not drawn.
  const int shapes = movement ? 3 : 4;
  ShowWindow(shape_[3], movement ? SW_HIDE : SW_SHOW);
  ShowWindow(labels_[0], movement ? SW_HIDE : SW_SHOW);
  for (HWND control : detail_) ShowWindow(control, movement ? SW_HIDE : SW_SHOW);
  for (HWND control : bulk_) ShowWindow(control, movement ? SW_HIDE : SW_SHOW);

  // The brush first — its shape and its size, which is what the pointer is about
  // to do — then which drawing of the terrain it lays.
  place_row(labels_[1], shape_, shapes);

  place(labels_[2], pad, y, label_w, row - 2);
  const int value_w = Scaled(hwnd_, 24);
  place(size_slider_, pad + label_w, y, width - label_w - value_w, row - 2);
  place(size_value_, pad + width - value_w, y, value_w, row - 2);
  y += row;

  if (!movement) place_row(labels_[0], detail_, 3);
  if (!movement) place_row(labels_[4], shade_, 3);
  place_row(labels_[3], mirror_, 5);

  // Full width and no label column, the way Fill and Clear are: these two carry
  // their own names, and a label column would leave "Decorate…" with nowhere to
  // put its letters on a narrow dock.
  const int bulk_w = width / 2;
  for (int i = 0; i < 2; i++) {
    place(bulk_[i], pad + bulk_w * i, y, bulk_w, row - 2);
  }
  if (!movement) y += row;

  // The mode switch under everything, in both modes, so it is in the same place
  // whichever one you are in.
  for (int i = 0; i < 2; i++) {
    place(mode_[i], pad + bulk_w * i, y, bulk_w, row - 2);
  }

  y += row;

  if (defer) EndDeferWindowPos(defer);
}

void TerrainPanel::OnCommand(int id) {
  if (id >= IDC_SHADE_FIRST && id < IDC_SHADE_FIRST + 3) {
    // Mix is not a shade of its own to paint with, so it leaves paint_dark where
    // it was: switching Mix off puts the stroke back on the drawing it was
    // laying, rather than always on Light.
    editor_->mix_shades = id == IDC_SHADE_FIRST + 2;
    if (!editor_->mix_shades) editor_->paint_dark = id == IDC_SHADE_FIRST + 1;
    ArmTheBrush();
    Refresh();
    // The palette's cells draw the chosen shade, so they are now all wrong.
    if (palette_.hwnd()) InvalidateRect(palette_.hwnd(), nullptr, TRUE);
  } else if (id >= IDC_PANEL_MODE_FIRST && id <= IDC_PANEL_MODE_FIRST + 1) {
    // Through the host, not editor_->SetMode: entering movement mode turns the
    // overlay on and leaving puts back what was there, and that is the
    // application's to remember. The menu item does the same work.
    if (host_) {
      host_->OnPanelMode(id == IDC_PANEL_MODE_FIRST ? Mode::kTerrain
                                                    : Mode::kMovement);
    }
    return;
  }

  if (id == IDC_TERRAIN_REPLACE || id == IDC_TERRAIN_DECORATE) {
    // The sheet and everything that follows it belong to the application: the
    // panel cannot recompose the canvas or the minimap, and the menu item does
    // the same work.
    if (host_) {
      host_->OnBulkEdit(id == IDC_TERRAIN_REPLACE ? Host::BulkEdit::kReplace
                                                  : Host::BulkEdit::kDecorate);
    }
    return;
  }
  if (id >= IDC_DETAIL_FIRST && id < IDC_DETAIL_FIRST + 3) {
    // Screen order Plain / Mixed / Detail is policy 1 / 0 / 2.
    static const int kPolicy[] = {PF_VARIATION_PLAIN, PF_VARIATION_ANY,
                                  PF_VARIATION_DECORATED};
    editor_->SetVariationPolicy(kPolicy[id - IDC_DETAIL_FIRST]);
  } else if (id >= IDC_SHAPE_FIRST && id < IDC_SHAPE_FIRST + 4) {
    const int i = id - IDC_SHAPE_FIRST;
    editor_->brush_shape = i == 3 ? Editor::kShapeFill : i;
  } else if (id >= IDC_MIRROR_FIRST && id < IDC_MIRROR_FIRST + 5) {
    static const int kFlags[] = {PF_MIRROR_NONE, PF_MIRROR_LEFT_RIGHT,
                                 PF_MIRROR_TOP_BOTTOM, PF_MIRROR_DIAG_SW_NE,
                                 PF_MIRROR_DIAG_NW_SE};
    editor_->ToggleMirror(kFlags[id - IDC_MIRROR_FIRST]);
  } else {
    return;
  }
  ArmTheBrush();
  Refresh();
  if (host_) host_->OnEditorChanged();
}

void TerrainPanel::ArmTheBrush() {
  // Every row of this panel describes what a stroke lays. Setting one while the
  // select tool is in hand used to change a brush the next click would not use —
  // you pressed Circle, dragged, and got a rectangle.
  //
  // Which brush, though, is the mode's business. The shape, the size and the
  // mirrors are shared, so reaching for one in movement mode used to arm the
  // terrain brush and take the person out of the mode they were working in.
  if (!editor_) return;
  const Tool want = editor_->mode() == Mode::kMovement ? Tool::kWalkable
                                                       : Tool::kPaint;
  if (editor_->tool() == want) return;
  editor_->SetTool(want);
  if (host_) host_->OnEditorChanged();
}

void TerrainPanel::Refresh() {
  if (!editor_) return;
  // The palette is a different list in movement mode, so a mode change is a
  // rebuild rather than a repaint.
  if (palette_mode_ != editor_->mode()) {
    palette_mode_ = editor_->mode();
    RebuildPalette();
    // Rows come and go with the mode, and the palette takes the height they
    // give up, so the whole panel is laid out again rather than repainted.
    Layout();
  }
  if (editor_->mode() == Mode::kMovement) {
    palette_.SetSelected(SelectedMovementCell());
  } else {
    palette_.SetSelected(editor_->brush_index);
  }

  // DarkWanted rather than paint_dark: holding shift borrows the other shade,
  // and the switch has to show what the next stroke would lay or the borrowing
  // is invisible. Mix has no other shade to borrow.
  const bool mix = editor_->mix_shades;
  const bool dark = editor_->DarkWanted();
  Button_SetCheck(shade_[0], !mix && !dark);
  Button_SetCheck(shade_[1], !mix && dark);
  Button_SetCheck(shade_[2], mix);

  const int policy = editor_->variation_policy();
  Button_SetCheck(detail_[0], policy == PF_VARIATION_PLAIN);
  Button_SetCheck(detail_[1], policy == PF_VARIATION_ANY);
  Button_SetCheck(detail_[2], policy == PF_VARIATION_DECORATED);

  const int shape = editor_->brush_shape;
  for (int i = 0; i < 4; i++) {
    Button_SetCheck(shape_[i], (i == 3 && shape == Editor::kShapeFill) ||
                                   (i < 3 && shape == i));
  }
  // Brush size means nothing for Fill, so the slider says so.
  EnableWindow(size_slider_, shape != Editor::kShapeFill);

  int size_index = 0;
  for (int i = 0; i < pf_brush_size_count(); i++) {
    if (pf_brush_size(i) == editor_->brush_size) size_index = i;
  }
  SendMessageW(size_slider_, TBM_SETPOS, TRUE, size_index);
  // The bottom rung is a corner rather than a count of tiles, and "0" reads as
  // a brush that paints nothing. A half sign says "smaller than one" without
  // claiming a number of tiles it does not lay.
  SetWindowTextW(size_value_, editor_->BrushIsCorner()
                                  ? L"½"
                                  : std::to_wstring(editor_->brush_size).c_str());

  // In movement mode the palette is a different list, so the name under it is
  // a different name — and the rows that choose a *drawing* of a terrain have
  // nothing to say about a layer that is not drawn at all.
  const bool movement = editor_->mode() == Mode::kMovement;
  if (movement) {
    // The name and the word it stands for. Once bits can be turned on by hand
    // the value is the thing being painted, and it is often one no class has a
    // name for — "0x0101" is then the only honest answer.
    std::wstring shown;
    if (editor_->movement_from_terrain) {
      shown = Str(IDS_MOVE_FROM_TERRAIN);
    } else {
      const int cls = editor_->MovementClassIndex();
      const char* name = cls >= 0 ? pf_movement_class_name(cls) : nullptr;
      shown = Format(IDS_MOVE_VALUE,
                     name ? FromUtf8(name).c_str() : Str(IDS_MOVE_UNNAMED).c_str(),
                     unsigned(editor_->movement_value));
    }
    SetWindowTextW(brush_name_, shown.c_str());
  } else {
    SetWindowTextW(brush_name_, FromUtf8(editor_->BrushName()).c_str());
  }
  // A shade is a drawing of a terrain, which a layer that is not drawn has
  // none of; the bits are the movement value taken apart.
  for (HWND control : shade_) ShowWindow(control, movement ? SW_HIDE : SW_SHOW);
  ShowWindow(labels_[4], movement ? SW_HIDE : SW_SHOW);
  Button_SetCheck(mode_[0], !movement);
  Button_SetCheck(mode_[1], movement);

  // The bucket is hidden in movement mode rather than greyed, so a brush left
  // on Fill has to be moved off it or the next click would do nothing.
  if (movement && editor_->brush_shape == Editor::kShapeFill) {
    editor_->brush_shape = PF_BRUSH_SQUARE;
    Button_SetCheck(shape_[3], FALSE);
    Button_SetCheck(shape_[0], TRUE);
  }

  Button_SetCheck(mirror_[0], editor_->mirrors == PF_MIRROR_NONE);
  Button_SetCheck(mirror_[1], (editor_->mirrors & PF_MIRROR_LEFT_RIGHT) != 0);
  Button_SetCheck(mirror_[2], (editor_->mirrors & PF_MIRROR_TOP_BOTTOM) != 0);
  Button_SetCheck(mirror_[3], (editor_->mirrors & PF_MIRROR_DIAG_SW_NE) != 0);
  Button_SetCheck(mirror_[4], (editor_->mirrors & PF_MIRROR_DIAG_NW_SE) != 0);
}

// ============================================================== UnitsPanel

bool UnitsPanel::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &UnitsPanel::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = kUnitsClass;
  return RegisterClassExW(&wc) != 0;
}

HWND UnitsPanel::Create(HWND parent, HINSTANCE instance, Editor* editor,
                        Host* host) {
  instance_ = instance;
  editor_ = editor;
  host_ = host;
  // WS_CLIPCHILDREN for the reason the terrain panel has it: the background
  // erase would otherwise repaint the whole panel under the palette.
  return CreateWindowExW(0, kUnitsClass, nullptr,
                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0,
                         0, 0, parent, nullptr, instance, this);
}

LRESULT CALLBACK UnitsPanel::Proc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
  UnitsPanel* self =
      reinterpret_cast<UnitsPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<UnitsPanel*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT UnitsPanel::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      Build();
      return 0;
    case WM_SIZE:
      Layout();
      return 0;
    case WM_CONTEXTMENU: {
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (at.x == -1 && at.y == -1) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        at = {rc.right / 2, rc.bottom / 2};
        ClientToScreen(hwnd_, &at);
      }
      PickColumns(at);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id == IDC_UNIT_OWNER && HIWORD(wparam) == CBN_SELCHANGE) {
        editor_->placing_owner =
            SlotForRow(int(SendMessageW(owner_combo_, CB_GETCURSEL, 0, 0)));
        if (host_) host_->OnEditorChanged();
      } else if (id == IDC_UNIT_PLAYER_PROPS) {
        // On the player the dropdown is showing, not on the first one.
        if (host_) host_->OnOpenMapSheet(kMapSheetPlayers, editor_->placing_owner);
      }
      return 0;
    }
    default:
      return DefWindowProcW(hwnd_, message, wparam, lparam);
  }
}

void UnitsPanel::Build() {
  // No Place button, and no Select one either: clicking a cell is what "place
  // this" means, and selecting is on the strip along the top. A Place button
  // only ever repeated what the last palette click already said, and could
  // disagree with it.
  //
  // The player, and beside it the sheet where the rest of that player lives.
  owner_combo_ = CreateWindowExW(
      0, L"COMBOBOX", nullptr,
      WS_CHILD | WS_VISIBLE | WS_GROUP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0,
      0, hwnd_, reinterpret_cast<HMENU>(INT_PTR(IDC_UNIT_OWNER)), instance_,
      nullptr);
  SetFont(owner_combo_);
  RefreshOwnerCombo();
  owner_props_ = MakeButton(hwnd_, instance_, L"…", IDC_UNIT_PLAYER_PROPS, 0);

  palette_.Create(hwnd_, instance_, IDC_UNIT_PALETTE);
  // The same three or four across as the terrain dock. At four across a portrait
  // is finally big enough to tell a Knight from a Paladin without reading the
  // tooltip.
  palette_.SetColumns(3, 5, true);
  palette_.on_context = [this](POINT at, int unit) { PickColumns(at, unit); };
  palette_.draw_icon = [this](HDC dc, const RECT& rect, int unit) {
    DrawUnitIcon(dc, rect, unit);
  };
  palette_.on_pick = [this](int unit) {
    editor_->placing_type = unit;
    editor_->SetTool(Tool::kPlace);
    if (host_) {
      host_->OnEditorChanged();
      const char* name = pf_unit_name(unit);
      host_->OnStatus(name ? FromUtf8(name) : L"", false);
    }
  };
  RebuildPalette();
}

void UnitsPanel::RefreshOwnerCombo() {
  if (!owner_combo_) return;
  const pf_map* map = editor_ ? editor_->map() : nullptr;
  // Only when something moved: refilling a combo box closes its dropdown, and
  // Refresh runs after every edit.
  bool same = true;
  for (int i : PlayerSlots()) {
    const int race = map ? pf_map_race(map, i) : PF_RACE_NEUTRAL;
    if (shown_races_[i] != race) { same = false; }
    shown_races_[i] = race;
  }
  if (same && owner_combo_filled_) return;
  owner_combo_filled_ = true;

  const int keep = int(SendMessageW(owner_combo_, CB_GETCURSEL, 0, 0));
  SendMessageW(owner_combo_, CB_RESETCONTENT, 0, 0);
  static const UINT kRaceNames[] = {IDS_RACE_HUMAN_NAME, IDS_RACE_ORC_NAME,
                                    IDS_RACE_NEUTRAL};
  // The slots the game supports, so a unit cannot be armed to an owner no map
  // can carry. Rows are positions in that list, not player numbers.
  for (int i : PlayerSlots()) {
    const char* name = pf_player_name(i);
    std::wstring label = name ? FromUtf8(name) : Format(IDS_PLAYER_N, i + 1);
    const int race = shown_races_[i];
    const std::wstring race_name =
        Str(kRaceNames[race >= 0 && race < 3 ? race : 2]);
    // The core names a slot "Player 1 (Red)"; the race belongs inside those
    // brackets, because both answer the same question about the slot. Appended
    // instead when the name has no brackets to slip into.
    if (!label.empty() && label.back() == L')') {
      label.insert(label.size() - 1, Format(IDS_PLAYER_RACE_SUFFIX, race_name.c_str()));
    } else if (label.find(race_name) == std::wstring::npos) {
      label += L" " + race_name;
    }
    SendMessageW(owner_combo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label.c_str()));
  }
  SendMessageW(owner_combo_, CB_SETCURSEL, WPARAM(keep < 0 ? 0 : keep), 0);
}

void UnitsPanel::PickColumns(POINT screen, int unit) {
  const bool right = host_ && host_->DockIsRight(Host::Dock::kUnits);
  // The unit is named in the item, because the pointer is the only thing that
  // says which cell this is about and it is about to be over a menu instead.
  std::wstring named;
  if (unit >= 0) {
    const char* name = pf_unit_name(unit);
    if (name) named = Format(IDS_UNIT_PROPS_FOR, FromUtf8(name).c_str());
  }
  const int picked = AskDockMenu(hwnd_, screen, palette_.column_count(), right,
                                 named.empty() ? nullptr : named.c_str());
  if (picked == kDockMenuDismissed) return;
  if (picked == kDockMenuExtra) {
    // The list row is the unit id: the sheet walks the ids in order.
    if (host_) host_->OnOpenMapSheet(kMapSheetUnits, unit);
    return;
  }
  if (picked == kDockMenuLeft || picked == kDockMenuRight) {
    if (host_) host_->OnDockSide(Host::Dock::kUnits, picked == kDockMenuRight);
    return;
  }
  palette_.SetColumnCount(picked);
  if (host_) host_->OnEditorChanged();
}

void UnitsPanel::SetUiIcons(const UiIcons* icons) {
  if (icons) icons->Decorate(owner_props_, kIconProperties);
}

void UnitsPanel::SetArtwork(IconCache* icons, const pf_tileset_art* art,
                            int tileset) {
  icons_ = icons;
  art_ = art;
  tileset_ = tileset;
  if (palette_.hwnd()) InvalidateRect(palette_.hwnd(), nullptr, TRUE);
}

void UnitsPanel::RebuildPalette() {
  // Grouped the way the web palette is: race in caps over its kinds, in
  // UnitsInPaletteOrder's order, which the quick pick walks too. Headings come
  // out of the run rather than being looped over, so the sections and the order
  // cannot disagree about where one ends.
  std::vector<PaletteGrid::Entry> entries;
  std::wstring open;
  for (int id : UnitsInPaletteOrder(LeadingRace(),
                                    editor_ && editor_->offer_unused_units)) {
    // The chosen player's race, unless the option says otherwise. Whole sections
    // vanish rather than being greyed: an orc palette with the human half greyed
    // is the same scrolling for half the use, and the headings would still be
    // there naming nothing.
    if (editor_ && !editor_->OffersUnit(id)) continue;
    const std::wstring heading = UnitGroupHeading(id);
    if (heading != open) {
      open = heading;
      PaletteGrid::Entry head;
      head.heading = heading;
      entries.push_back(std::move(head));
    }
    PaletteGrid::Entry entry;
    entry.id = id;
    const char* name = pf_unit_name(id);
    entry.label = name ? FromUtf8(name) : L"";
    entries.push_back(std::move(entry));
  }
  palette_.SetEntries(std::move(entries));
  palette_.SetSelected(editor_ ? editor_->placing_type : -1);
}

char UnitsPanel::LeadingRace() const {
  // The chosen player's own race leads.
  //
  // With the filter on, the only thing of the other side the palette shows is
  // its heroes, and in fixed human-then-orc order those came out *first* on an
  // orc palette. Led by the chosen race they fall at the end, beside the neutral
  // units they behave like.
  //
  // Only while filtering: with every race shown both sides are there in full,
  // and reshuffling six headings on every dropdown change is worse.
  if (!editor_ || editor_->show_all_races || !editor_->map()) return 0;
  return pf_map_race(editor_->map(), editor_->placing_owner) == PF_RACE_ORC ? 'o'
                                                                            : 'h';
}

void UnitsPanel::DrawUnitIcon(HDC dc, const RECT& rect, int unit) {
  if (icons_) {
    const Icon& icon = icons_->Unit(unit, PaletteOwner(unit));
    // Portraits fill the cell edge to edge; the fourteen sprite fallbacks keep
    // their margin, since a cropped sprite loses the silhouette.
    if (!icon.empty()) { BlitIcon(dc, rect, icon); return; }
  }
  // No artwork: the unit's initials, which is at least tellable-apart.
  const char* name = pf_unit_name(unit);
  std::wstring text = name ? FromUtf8(name).substr(0, 2) : L"?";
  SetBkMode(dc, TRANSPARENT);
  RECT box = rect;
  DrawTextW(dc, text.c_str(), -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void UnitsPanel::Layout() {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int pad = Scaled(hwnd_, kPadBase);
  const int row = Scaled(hwnd_, kRowBase);
  const int width = rc.right - pad * 2;
  if (width <= 0) return;
  // One pass, for the reason TerrainPanel::Layout has it: MoveWindow repaints
  // each control before the next has moved, and four controls arriving one at a
  // time during a seam drag is the shimmer.
  HDWP defer = BeginDeferWindowPos(4);
  const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
  auto place = [&](HWND control, int x, int y, int w, int h) {
    if (defer) defer = DeferWindowPos(defer, control, nullptr, x, y, w, h, flags);
  };

  // The player over the palette, with the way to that player's settings beside
  // it. Square, so the ellipsis reads as "there is more of this here" rather
  // than as a control with a name of its own.
  const int props_w = row;
  place(owner_combo_, pad, pad, width - props_w, row);
  place(owner_props_, pad + width - props_w, pad, props_w, row - 2);
  const int top = pad + row + pad;
  // Clamped, unlike the MoveWindow this replaced: a failed DeferWindowPos loses
  // every window already in the chain, so a panel squeezed shorter than its own
  // two rows would place nothing at all.
  place(palette_.hwnd(), pad, top, width, std::max(0, int(rc.bottom) - top - pad));

  if (defer) EndDeferWindowPos(defer);
}

int UnitsPanel::PaletteOwner(int unit) const {
  // The owner this unit would actually be placed as. Scenery lands on the
  // neutral slot whoever is chosen, so a red gold mine in the palette would be a
  // promise the map will not keep.
  const int forced = pf_unit_default_owner(unit);
  if (forced >= 0) return forced;
  return editor_ ? editor_->placing_owner : kNeutralOwner;
}

void UnitsPanel::Refresh() {
  if (!editor_) return;
  // A player's race can be changed on the player sheet while this dropdown is
  // showing the old one, so the labels are re-derived rather than filled once.
  RefreshOwnerCombo();
  SendMessageW(owner_combo_, CB_SETCURSEL,
               WPARAM(RowForSlot(editor_->placing_owner)), 0);

  // Which cells the palette holds depends on the chosen player's race and on the
  // option, so both are watched. Rebuilding throws the cells away, so it happens
  // only when one has really moved — Refresh runs after every edit.
  const pf_map* map = editor_->map();
  const int race =
      map ? pf_map_race(map, editor_->placing_owner) : int(PF_RACE_NEUTRAL);
  if (race != palette_race_ || editor_->show_all_races != palette_all_races_ ||
      editor_->offer_unused_units != palette_unused_) {
    palette_race_ = race;
    palette_all_races_ = editor_->show_all_races;
    palette_unused_ = editor_->offer_unused_units;
    // Before the rebuild: the armed unit may no longer be one this palette has a
    // cell for, and the cell to show as selected is the one it became.
    editor_->RetargetPlacingType();
    RebuildPalette();
  }

  palette_.SetSelected(editor_->tool() == Tool::kPlace ? editor_->placing_type
                                                       : -1);
  // The cells are drawn in the chosen player's colours, so a change of player
  // is a repaint. Only on a change: Refresh runs after every edit.
  if (editor_->placing_owner != shown_owner_) {
    shown_owner_ = editor_->placing_owner;
    if (palette_.hwnd()) InvalidateRect(palette_.hwnd(), nullptr, TRUE);
  }
}

}  // namespace pfwin

#include "Toolbar.hpp"

#include <commctrl.h>

#include <algorithm>
#include <string>

#include "Strings.hpp"
#include "pudforge/pudforge.h"
#include "resource.h"
#include "strings.h"

namespace pfwin {
namespace {

/// Strip geometry at 96 DPI, scaled by the window's own DPI.
constexpr int kPadDip = 2;
/// A floor under the button height, so a strip of short captions is still a
/// comfortable target rather than a hairline.
constexpr int kRowDip = 22;
/// Room between the last button and the dropdown, and the dropdown's width.
/// Wide enough for "800%" and the arrow with the caption font at its largest.
constexpr int kGapDip = 8;
constexpr int kZoomDip = 76;

int Scaled(HWND hwnd, int dip) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return MulDiv(dip, int(dpi ? dpi : 96), 96);
}

/// The order the buttons sit in. A zero id is a separator.
///
/// Copy sits before cut, where the Edit menu has them the other way round: cut
/// and delete are the two buttons that throw work away, and either side of a
/// separator is a better place for a misaimed click than adjacent.
struct Command {
  int id;
  /// Its cell in the icon sheet. Shown when that cell has been drawn; when it
  /// has not, the button falls back to the caption below.
  int icon;
  UINT caption;
  /// The sentence the tooltip shows, which is where the keyboard shortcut is
  /// written: a caption is one word and has no room for "Ctrl+Z".
  UINT tip;
  /// Whether it stays pushed in while it is the tool in hand. The two select
  /// tools do; every other button here is an action that happens and is over.
  bool toggle = false;
};

const Command kCommands[] = {
    {IDM_FILE_NEW, kIconNew, IDS_TB_NEW, IDS_TBTIP_NEW},
    {IDM_FILE_OPEN, kIconOpen, IDS_TB_OPEN, IDS_TBTIP_OPEN},
    {IDM_FILE_SAVE, kIconSave, IDS_TB_SAVE, IDS_TBTIP_SAVE},
    {0, -1, 0, 0},
    {IDM_EDIT_UNDO, kIconUndo, IDS_TB_UNDO, IDS_TBTIP_UNDO},
    {IDM_EDIT_REDO, kIconRedo, IDS_TB_REDO, IDS_TBTIP_REDO},
    {IDM_EDIT_DELETE, kIconDelete, IDS_TB_DELETE, IDS_TBTIP_DELETE},
    {0, -1, 0, 0},
    {IDM_EDIT_COPY, kIconCopy, IDS_TB_COPY, IDS_TBTIP_COPY},
    {IDM_EDIT_CUT, kIconCut, IDS_TB_CUT, IDS_TBTIP_CUT},
    {IDM_EDIT_PASTE, kIconPaste, IDS_TB_PASTE, IDS_TBTIP_PASTE},
    {0, -1, 0, 0},
    // The two select tools, off the docks and onto the strip: a dock says *what*
    // to draw with, and selecting is not drawing with anything. Side by side,
    // because which half of the editor you are selecting in is the whole
    // difference between them.
    {IDM_TOOL_TERRAIN_SELECT, kIconSelectTerrain, IDS_TB_SELECT_TERRAIN,
     IDS_TBTIP_SELECT_TERRAIN, true},
    {IDM_TOOL_UNIT_SELECT, kIconSelectUnits, IDS_TB_SELECT_UNITS,
     IDS_TBTIP_SELECT_UNITS, true},
};

}  // namespace

HWND Toolbar::Create(HWND parent, HINSTANCE instance, const UiIcons* icons) {
  icons_ = icons;
  bar_ = CreateWindowExW(
      0, TOOLBARCLASSNAMEW, nullptr,
      // NOPARENTALIGN and NORESIZE because the application places this: left to
      // itself a toolbar snaps to the top of its parent and takes the full
      // width, which is impossible to reason about beside a status bar doing the
      // same at the other end. TBSTYLE_TOOLTIPS makes the strip ask its parent
      // for text through TTN_GETDISPINFO, which is where the shortcut is said.
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST |
          TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(INT_PTR(IDC_TOOLBAR)),
      instance, nullptr);
  if (!bar_) return nullptr;
  SendMessageW(bar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
  SendMessageW(bar_, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  // The strip was captions before there was any artwork, and still is wherever a
  // cell of the sheet has not been drawn.
  const bool drawn = icons_ && icons_->ready();
  if (drawn) {
    SendMessageW(bar_, TB_SETIMAGELIST, 0, LPARAM(icons_->list()));
    SendMessageW(bar_, TB_SETBITMAPSIZE, 0,
                 MAKELPARAM(icons_->size(), icons_->size()));
  } else {
    // Nothing reserves room for an image, or every caption is pushed right by
    // the blank space one would take.
    SendMessageW(bar_, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
  }
  SendMessageW(bar_, TB_SETMAXTEXTROWS, 1, 0);
  // Room either side of a caption. The default padding is drawn around an icon
  // that is not there, so word buttons ended up crowded and the strip read as
  // one long sentence. Eight pixels a side is what the shell's own text toolbars
  // use — more than a picture wants, which needs only enough not to touch.
  SendMessageW(bar_, TB_SETPADDING, 0,
               MAKELPARAM(Scaled(parent, drawn ? 8 : 16), 0));

  std::vector<TBBUTTON> buttons;
  buttons.reserve(sizeof(kCommands) / sizeof(kCommands[0]));
  for (const Command& one : kCommands) {
    TBBUTTON button = {};
    if (one.id == 0) {
      button.fsStyle = BTNS_SEP;
      buttons.push_back(button);
      continue;
    }
    const bool has_icon = drawn && icons_->Has(one.icon);
    button.iBitmap = has_icon ? one.icon : I_IMAGENONE;
    button.idCommand = one.id;
    button.fsState = TBSTATE_ENABLED;
    // BTNS_CHECK rather than BTNS_BUTTON for the tools, so the strip can show
    // which is in hand. Not BTNS_CHECKGROUP: neither tool being in hand is a
    // real state, and a check group insists one of its members is always down.
    button.fsStyle = BTNS_AUTOSIZE | (one.toggle ? BTNS_CHECK : BTNS_BUTTON);
    if (has_icon) {
      // A picture and its own word beside it would make eleven buttons wider
      // than the window; the tooltip already says the word and the shortcut.
      button.iString = -1;
    } else {
      // TB_ADDSTRING wants a double-null-terminated list and copies what it is
      // given, which is what lets the caption be a temporary from the table.
      std::wstring caption = Str(one.caption);
      caption.push_back(L'\0');
      button.iString =
          SendMessageW(bar_, TB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(caption.c_str()));
    }
    buttons.push_back(button);
  }
  SendMessageW(bar_, TB_ADDBUTTONS, WPARAM(buttons.size()),
               reinterpret_cast<LPARAM>(buttons.data()));

  // Created after the toolbar, so it sits above it in the z-order and the
  // strip's WS_CLIPSIBLINGS keeps the toolbar's background off it.
  zoom_ = CreateWindowExW(
      0, L"COMBOBOX", nullptr,
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS | WS_TABSTOP |
          CBS_DROPDOWNLIST,
      0, 0, 0, 0, parent, reinterpret_cast<HMENU>(INT_PTR(IDC_TOOLBAR_ZOOM)),
      instance, nullptr);
  if (zoom_) {
    SendMessageW(zoom_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
    // The ladder is the core's, and the same one the wheel and the +/- keys step
    // along. Writing the percentages out here would be a second copy that could
    // disagree with the one the keyboard uses.
    const int count = pf_zoom_level_count();
    rungs_.reserve(size_t(count > 0 ? count : 0));
    for (int i = 0; i < count; i++) {
      const int percent = pf_zoom_level(i);
      rungs_.push_back(percent);
      SendMessageW(
          zoom_, CB_ADDSTRING, 0,
          reinterpret_cast<LPARAM>(Format(IDS_ZOOM_PERCENT, percent).c_str()));
    }
  }
  return bar_;
}

int Toolbar::Height() const {
  if (!bar_ || !showing_) return 0;
  const DWORD size = DWORD(SendMessageW(bar_, TB_GETBUTTONSIZE, 0, 0));
  return std::max(int(HIWORD(size)), Scaled(bar_, kRowDip)) +
         2 * Scaled(bar_, kPadDip);
}

void Toolbar::Place(HDWP& defer, UINT flags, int width) {
  if (!bar_ || !showing_) return;
  const int height = Height();
  defer = DeferWindowPos(defer, bar_, nullptr, 0, 0, width, height, flags);
  if (!zoom_) return;
  // Where the buttons end, rather than a width written down here: the captions
  // are translated and a German toolbar is a third wider than an English one.
  RECT last = {};
  const int index = int(SendMessageW(bar_, TB_BUTTONCOUNT, 0, 0)) - 1;
  if (index >= 0) {
    SendMessageW(bar_, TB_GETITEMRECT, WPARAM(index),
                 reinterpret_cast<LPARAM>(&last));
  }
  const int pad = Scaled(bar_, kPadDip);
  defer = DeferWindowPos(defer, zoom_, nullptr,
                         last.right + Scaled(bar_, kGapDip), pad,
                         Scaled(bar_, kZoomDip), std::max(0, height - 2 * pad),
                         flags);
}

void Toolbar::Show(bool on) {
  showing_ = on;
  if (bar_) ShowWindow(bar_, on ? SW_SHOW : SW_HIDE);
  if (zoom_) ShowWindow(zoom_, on ? SW_SHOW : SW_HIDE);
}

void Toolbar::SetEnabled(int command, bool on) {
  if (!bar_) return;
  SendMessageW(bar_, TB_ENABLEBUTTON, WPARAM(command), MAKELPARAM(on, 0));
}

void Toolbar::SetChecked(int command, bool on) {
  if (!bar_) return;
  SendMessageW(bar_, TB_CHECKBUTTON, WPARAM(command), MAKELPARAM(on, 0));
}

bool Toolbar::TooltipWanted(LPARAM lparam) const {
  auto* ask = reinterpret_cast<NMTTDISPINFOW*>(lparam);
  if (!ask || ask->hdr.code != TTN_GETDISPINFOW) return false;
  // idFrom is the button's command id, because that is what the strip stores.
  for (const Command& one : kCommands) {
    if (one.id == 0 || UINT_PTR(one.id) != ask->hdr.idFrom) continue;
    // The string table's own buffer, which outlives the call: the tooltip reads
    // lpszText after this returns, so a local would be gone by then.
    static std::wstring held;
    held = Str(one.tip);
    ask->lpszText = const_cast<wchar_t*>(held.c_str());
    return true;
  }
  return false;
}

void Toolbar::ShowZoom(int percent) {
  if (!zoom_ || percent == shown_zoom_) return;
  shown_zoom_ = percent;
  // An off-ladder entry describes one view of one map, so the previous one goes
  // before another is added or the list grows a rung at a time.
  if (odd_rung_) {
    SendMessageW(zoom_, CB_DELETESTRING, WPARAM(rungs_.size()), 0);
    odd_rung_ = false;
  }
  int index = -1;
  for (size_t i = 0; i < rungs_.size(); i++) {
    if (rungs_[i] == percent) index = int(i);
  }
  if (index < 0) {
    // The index it went in at rather than the one it should have: a refused add
    // would otherwise leave the flag set, and the next call would delete a rung
    // that is really there.
    const LRESULT added = SendMessageW(
        zoom_, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(Format(IDS_ZOOM_PERCENT, percent).c_str()));
    if (added < 0) return;
    odd_rung_ = true;
    index = int(added);
  }
  SendMessageW(zoom_, CB_SETCURSEL, WPARAM(index), 0);
}

int Toolbar::ZoomChosen(WPARAM wparam, LPARAM lparam) const {
  if (!zoom_ || reinterpret_cast<HWND>(lparam) != zoom_ ||
      HIWORD(wparam) != CBN_SELCHANGE) {
    return 0;
  }
  const int selected = int(SendMessageW(zoom_, CB_GETCURSEL, 0, 0));
  if (selected < 0) return 0;
  return selected < int(rungs_.size()) ? rungs_[size_t(selected)] : shown_zoom_;
}

}  // namespace pfwin

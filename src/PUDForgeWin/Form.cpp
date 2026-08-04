#include "Form.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

#include "Icons.hpp"   // kChangedInk, which the lists beside a form share
#include "Strings.hpp"
#include "strings.h"

namespace pfwin {
namespace {

/// Row geometry at 96 DPI.
constexpr int kRowBase = 24;
constexpr int kGapBase = 4;
constexpr int kPadBase = 8;
constexpr int kLabelBase = 150;
constexpr int kMarkBase = 12;
/// How wide a value control gets when it has no use for more.
constexpr int kValueBase = 150;
/// Control ids start here, one per row, so WM_COMMAND says which row spoke.
constexpr int kFirstControl = 100;
/// And the companion buttons here, far enough above that no form could have
/// enough rows for the two ranges to meet: the widest is a unit's thirty-one.
constexpr int kFirstSide = 1000;

}  // namespace

const wchar_t* Form::kClassName = L"PUDForgeForm";

bool Form::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &Form::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0;
}

HWND Form::Create(HWND parent, HINSTANCE instance, int control_id) {
  instance_ = instance;
  // WS_VSCROLL only: a form that scrolls sideways is a form whose labels have
  // wandered off the screen.
  return CreateWindowExW(WS_EX_CONTROLPARENT, kClassName, nullptr,
                         WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                         0, 0, 0, 0, parent,
                         reinterpret_cast<HMENU>(INT_PTR(control_id)), instance,
                         this);
}

int Form::Scaled(int base) const {
  const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
  return MulDiv(base, int(dpi ? dpi : 96), 96);
}

int Form::IndexOfControl(int control_id) const {
  const int index = control_id - kFirstControl;
  return index >= 0 && index < int(rows_.size()) ? index : -1;
}

void Form::Explain(HWND control, const std::wstring& tip) {
  if (!control || tip.empty()) return;
  if (!tip_) {
    tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                           hwnd_, nullptr, instance_, nullptr);
    if (!tip_) return;
    SendMessageW(tip_, TTM_SETMAXTIPWIDTH, 0, 320);
  }
  tip_texts_.push_back(tip);
  TOOLINFOW info = {};
  info.cbSize = sizeof(info);
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = hwnd_;
  info.uId = reinterpret_cast<UINT_PTR>(control);
  info.lpszText = const_cast<wchar_t*>(tip_texts_.back().c_str());
  SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

int Form::IndexOfSide(int control_id) const {
  const int index = control_id - kFirstSide;
  return index >= 0 && index < int(rows_.size()) ? index : -1;
}

void Form::Clear() {
  for (Live& live : rows_) {
    if (live.label) DestroyWindow(live.label);
    if (live.control) DestroyWindow(live.control);
    if (live.side) DestroyWindow(live.side);
    if (live.mark) DestroyWindow(live.mark);
  }
  rows_.clear();
  // The tooltip's tools named windows that have just been destroyed, so it goes
  // with them rather than being cleaned out tool by tool.
  if (tip_) { DestroyWindow(tip_); tip_ = nullptr; }
  scroll_ = 0;
}

void Form::SetRows(std::vector<Row> rows) {
  if (!hwnd_) return;
  Clear();
  if (!font_) font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

  rows_.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); i++) {
    Live live;
    live.row = std::move(rows[i]);
    const int id = kFirstControl + int(i);

    live.mark = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    live.label = CreateWindowExW(0, L"STATIC", live.row.label.c_str(),
                                 WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
                                 0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);

    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    const wchar_t* klass = L"EDIT";
    DWORD ex = 0;
    switch (live.row.kind) {
      case Kind::kNumber:
        klass = L"EDIT";
        style |= ES_AUTOHSCROLL | ES_NUMBER;
        ex = WS_EX_CLIENTEDGE;
        break;
      case Kind::kText:
        klass = L"EDIT";
        style |= ES_AUTOHSCROLL;
        ex = WS_EX_CLIENTEDGE;
        break;
      case Kind::kChoice:
        klass = L"COMBOBOX";
        style |= CBS_DROPDOWNLIST | WS_VSCROLL;
        break;
      case Kind::kCheck:
        klass = L"BUTTON";
        style |= BS_AUTOCHECKBOX;
        break;
      case Kind::kFlag:
        klass = L"BUTTON";
        // BS_MULTILINE, because two of these share a line and a bit's name can
        // be four words long; clipped it would read as a different bit.
        style |= BS_AUTOCHECKBOX | BS_MULTILINE;
        break;
      case Kind::kButton:
        klass = L"BUTTON";
        style |= BS_PUSHBUTTON;
        break;
      case Kind::kNote:
        klass = L"EDIT";
        // Read-only rather than disabled: a disabled edit is grey and cannot be
        // selected, and the first thing anybody wants to do with a line of it
        // is copy it out. No border and no scrollbar — it is sized to hold what
        // it says, and a box around a caption reads as a field to fill in.
        style |= ES_MULTILINE | ES_READONLY;
        break;
    }
    // A flag carries its own caption; every other kind gets its text from the
    // label column beside it.
    live.control = CreateWindowExW(
        ex, klass,
        live.row.kind == Kind::kFlag ? live.row.label.c_str() : L"", style, 0, 0,
        0, 0, hwnd_, reinterpret_cast<HMENU>(INT_PTR(id)), instance_, nullptr);
    if (live.row.kind == Kind::kChoice) {
      for (const std::wstring& choice : live.row.choices) {
        SendMessageW(live.control, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(choice.c_str()));
      }
    }
    // The companion button, when the row asked for one. An ellipsis where the
    // sheet has no drawing for it — the same fallback the docks use.
    if (live.row.side_icon >= 0) {
      live.side = CreateWindowExW(
          0, L"BUTTON", L"…", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
          0, 0, 0, 0, hwnd_,
          reinterpret_cast<HMENU>(INT_PTR(kFirstSide + int(i))), instance_,
          nullptr);
      if (icons_) icons_->Decorate(live.side, live.row.side_icon);
      Explain(live.side, live.row.side_tip);
    }

    for (HWND child : {live.mark, live.label, live.control, live.side}) {
      if (child) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    rows_.push_back(std::move(live));
  }
  Layout();
  Reload();
}

void Form::Layout() {
  if (!hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int pad = Scaled(kPadBase);
  const int gap = Scaled(kGapBase);
  const int row_h = Scaled(kRowBase);
  const int mark_w = Scaled(kMarkBase);
  const int label_w = Scaled(kLabelBase);
  const int control_x = pad + mark_w + label_w + gap;
  const int room = std::max(Scaled(60), int(rc.right) - control_x - pad);
  // A number is at most six digits wide and a drop-down is as wide as its
  // longest choice; stretching either to the edge only makes the eye travel.
  // Free text gets the room because it can use it.
  const int narrow = std::min(room, Scaled(kValueBase));

  int y = pad;
  // Positioned in one pass so the whole form does not flicker its way down the
  // screen on every scroll.
  HDWP defer = BeginDeferWindowPos(int(rows_.size()) * 3);
  for (size_t i = 0; i < rows_.size(); i++) {
    Live& live = rows_[i];
    live.y = y;
    const int at = y - scroll_;

    // A note is prose across the full width: no label beside it, no mark, and
    // it starts where the labels start rather than where the values do.
    if (live.row.kind == Kind::kNote) {
      const int note_h = row_h * std::max(1, live.row.note_rows);
      if (IsWindowVisible(live.label)) ShowWindow(live.label, SW_HIDE);
      if (IsWindowVisible(live.mark)) ShowWindow(live.mark, SW_HIDE);
      live.x0 = live.x1 = 0;
      defer = DeferWindowPos(defer, live.control, nullptr, pad + mark_w, at,
                             std::max(Scaled(80), int(rc.right) - pad * 2 - mark_w),
                             note_h, SWP_NOZORDER);
      y += note_h + gap;
      continue;
    }

    // Flags go two to a line, and take the label column as well as the value
    // one because the caption is on the checkbox itself.
    if (live.row.kind == Kind::kFlag) {
      const bool paired =
          i + 1 < rows_.size() && rows_[i + 1].row.kind == Kind::kFlag;
      const int full = std::max(Scaled(80), int(rc.right) - pad * 2);
      const int column = paired ? full / 2 : full;
      auto place_flag = [&](Live& one, int x) {
        one.x0 = x;
        one.x1 = x + column;
        defer = DeferWindowPos(defer, one.mark, nullptr, x, at + Scaled(4),
                               mark_w, row_h - Scaled(4), SWP_NOZORDER);
        // Hidden rather than sized to nothing: a zero-width static still answers
        // hit tests and would eat clicks aimed at the checkbox.
        if (IsWindowVisible(one.label)) ShowWindow(one.label, SW_HIDE);
        defer = DeferWindowPos(defer, one.control, nullptr, x + mark_w, at,
                               std::max(Scaled(40), column - mark_w - gap),
                               row_h - Scaled(4), SWP_NOZORDER);
      };
      place_flag(live, pad);
      if (paired) {
        rows_[i + 1].y = y;
        place_flag(rows_[i + 1], pad + column);
        i++;
      }
      y += row_h + gap;
      continue;
    }

    // A combo's height is the closed height plus the list it drops; every other
    // control is the row's height.
    const int height = live.row.kind == Kind::kChoice ? row_h + Scaled(200)
                                                      : row_h - Scaled(4);
    int width = narrow;
    if (live.row.kind == Kind::kText) width = room;
    if (live.row.kind == Kind::kCheck) width = row_h - Scaled(4);
    // The whole width, so a right-click anywhere on the line finds this row.
    live.x0 = live.x1 = 0;
    // Shown again, in case this row was a flag last time: SetRows only rebuilds
    // when the number of rows changes, and two subjects can have the same count
    // with a different mix.
    if (!IsWindowVisible(live.label)) ShowWindow(live.label, SW_SHOW);
    defer = DeferWindowPos(defer, live.mark, nullptr, pad, at + Scaled(4), mark_w,
                           row_h - Scaled(4), SWP_NOZORDER);
    defer = DeferWindowPos(defer, live.label, nullptr, pad + mark_w, at + Scaled(5),
                           label_w, row_h - Scaled(4), SWP_NOZORDER);
    // A companion button takes its square off the end of the control, so the
    // pair together occupy exactly what the control alone used to.
    const int side_w = live.side ? row_h - Scaled(4) : 0;
    if (live.side) width = std::max(Scaled(40), width - side_w - gap);
    defer = DeferWindowPos(defer, live.control, nullptr, control_x, at, width,
                           height, SWP_NOZORDER);
    if (live.side) {
      defer = DeferWindowPos(defer, live.side, nullptr, control_x + width + gap,
                             at, side_w, row_h - Scaled(4), SWP_NOZORDER);
    }
    y += row_h + gap;
  }
  EndDeferWindowPos(defer);
  content_h_ = y + pad;
  SetScroll();
}

void Form::SetScroll() {
  if (!hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int page = rc.bottom;
  scroll_ = std::max(0, std::min(scroll_, content_h_ - page));

  SCROLLINFO si = {};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = std::max(0, content_h_ - 1);
  si.nPage = UINT(page);
  si.nPos = scroll_;
  SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void Form::ScrollTo(int y) {
  const int was = scroll_;
  scroll_ = y;
  SetScroll();
  if (scroll_ == was) return;
  Layout();
  // Moving the children does not repaint what they were sitting on, so a
  // scrolled form otherwise keeps a ghost of every label it has passed.
  // WS_CLIPCHILDREN keeps this erase off the controls, so it costs no flicker.
  InvalidateRect(hwnd_, nullptr, TRUE);
}

void Form::Reload() {
  filling_ = true;
  for (Live& live : rows_) {
    const int64_t value = read ? read(live.row.id) : 0;
    switch (live.row.kind) {
      case Kind::kNumber:
        SetWindowTextW(live.control, std::to_wstring(value).c_str());
        break;
      case Kind::kText:
        SetWindowTextW(live.control,
                       text ? text(live.row.id).c_str() : L"");
        break;
      case Kind::kChoice:
        SendMessageW(live.control, CB_SETCURSEL, WPARAM(value), 0);
        break;
      case Kind::kCheck:
      case Kind::kFlag:
        Button_SetCheck(live.control, value ? BST_CHECKED : BST_UNCHECKED);
        break;
      case Kind::kButton:
      case Kind::kNote:
        SetWindowTextW(live.control, text ? text(live.row.id).c_str() : L"");
        break;
    }
    MarkChanged(live);
  }
  filling_ = false;
}

/// Show whether a row differs from the game's default.
///
/// A solid bar down the left in the accent colour, and the label in the same
/// colour. Both are repainted rather than only set, because a static keeps its
/// old pixels when only its colour changes.
void Form::MarkChanged(Live& live) {
  const bool differs = changed && changed(live.row.id);
  SetWindowTextW(live.mark, differs ? L"▌" : L"");
  InvalidateRect(live.mark, nullptr, TRUE);
  if (live.label) InvalidateRect(live.label, nullptr, TRUE);
}

void Form::Harvest(int index) {
  if (filling_ || index < 0 || index >= int(rows_.size()) || !write) return;
  Live& live = rows_[size_t(index)];
  int64_t value = 0;
  switch (live.row.kind) {
    case Kind::kNumber: {
      wchar_t typed[64] = {};
      GetWindowTextW(live.control, typed, 64);
      value = _wtoi64(typed);
      if (live.row.low != live.row.high) {
        value = std::max(live.row.low, std::min(value, live.row.high));
      }
      break;
    }
    case Kind::kChoice:
      value = int64_t(SendMessageW(live.control, CB_GETCURSEL, 0, 0));
      break;
    case Kind::kCheck:
    case Kind::kFlag:
      value = Button_GetCheck(live.control) == BST_CHECKED ? 1 : 0;
      break;
    default:
      return;
  }
  if (!write(live.row.id, value)) {
    // Refused: put back what the owner still holds rather than leaving a value
    // on screen that is not in the map.
    filling_ = true;
    if (live.row.kind == Kind::kNumber) {
      SetWindowTextW(live.control, std::to_wstring(read ? read(live.row.id) : 0).c_str());
    }
    filling_ = false;
    return;
  }
  MarkChanged(live);

  // A note describes another row rather than holding a value of its own, so it
  // is stale the moment that row moves. Re-asked here because nothing else
  // will: Reload runs when the *subject* changes, and this is a change within
  // one — picking a different AI script for the same player.
  if (text) {
    filling_ = true;
    for (Live& other : rows_) {
      if (other.row.kind != Kind::kNote) continue;
      SetWindowTextW(other.control, text(other.row.id).c_str());
    }
    filling_ = false;
  }
}

int Form::RowAt(int x, int y) const {
  const int row_h = Scaled(kRowBase);
  const int at = y + scroll_;
  for (size_t i = 0; i < rows_.size(); i++) {
    const Live& live = rows_[i];
    if (at < live.y || at >= live.y + row_h) continue;
    // Flags share a line two at a time, so the line alone does not say which one
    // was clicked; everything else spans the form and x cannot decide.
    if (live.x1 > live.x0 && (x < live.x0 || x >= live.x1)) continue;
    return int(i);
  }
  return -1;
}

void Form::OfferReset(int index, POINT screen) {
  if (!reset || index < 0 || index >= int(rows_.size())) return;
  Live& live = rows_[size_t(index)];
  // Only a row that has something to put back. A menu whose one item does
  // nothing is a menu that answers a question you did not ask.
  if (!changed || !changed(live.row.id)) return;
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  // The row is named in the item, because the pointer is the only thing that
  // says which row this is about and it is about to be over a menu instead.
  const std::wstring label = live.row.label.empty()
                                 ? Str(IDS_FORM_RESET_ONE)
                                 : Format(IDS_FORM_RESET, live.row.label.c_str());
  AppendMenuW(menu, MF_STRING, 1, label.c_str());
  const int picked = int(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                        screen.x, screen.y, 0, hwnd_, nullptr));
  DestroyMenu(menu);
  if (picked != 1) return;
  if (!reset(live.row.id)) return;
  filling_ = true;
  const int64_t value = read ? read(live.row.id) : 0;
  switch (live.row.kind) {
    case Kind::kNumber:
      SetWindowTextW(live.control, std::to_wstring(value).c_str());
      break;
    case Kind::kChoice:
      SendMessageW(live.control, CB_SETCURSEL, WPARAM(value), 0);
      break;
    case Kind::kCheck:
    case Kind::kFlag:
      Button_SetCheck(live.control, value ? BST_CHECKED : BST_UNCHECKED);
      break;
    default:
      SetWindowTextW(live.control, text ? text(live.row.id).c_str() : L"");
      break;
  }
  filling_ = false;
  MarkChanged(live);
}

LRESULT CALLBACK Form::Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  Form* self = reinterpret_cast<Form*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<Form*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT Form::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
    case WM_DPICHANGED_AFTERPARENT:
      Layout();
      return 0;

    case WM_VSCROLL: {
      RECT rc;
      GetClientRect(hwnd_, &rc);
      const int line = Scaled(kRowBase);
      int y = scroll_;
      switch (LOWORD(wparam)) {
        case SB_LINEUP: y -= line; break;
        case SB_LINEDOWN: y += line; break;
        case SB_PAGEUP: y -= rc.bottom; break;
        case SB_PAGEDOWN: y += rc.bottom; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
          SCROLLINFO si = {};
          si.cbSize = sizeof(si);
          si.fMask = SIF_TRACKPOS;
          GetScrollInfo(hwnd_, SB_VERT, &si);
          y = si.nTrackPos;
          break;
        }
        default: return 0;
      }
      ScrollTo(y);
      return 0;
    }

    case WM_MOUSEWHEEL: {
      const int lines = -GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3;
      ScrollTo(scroll_ + lines * Scaled(kRowBase));
      return 0;
    }

    // A control the user has tabbed to may be scrolled off; bring it back
    // rather than letting the focus go somewhere invisible.
    case WM_COMMAND: {
      // A companion button first: it shares the message with the row's own
      // control and only the id tells them apart.
      const int side = IndexOfSide(LOWORD(wparam));
      if (side >= 0) {
        if (HIWORD(wparam) == BN_CLICKED && activate_side) {
          activate_side(rows_[size_t(side)].row.id);
        }
        return 0;
      }
      const int index = IndexOfControl(LOWORD(wparam));
      if (index < 0) break;
      const int code = HIWORD(wparam);
      Live& live = rows_[size_t(index)];
      if (code == EN_SETFOCUS || code == CBN_SETFOCUS || code == BN_SETFOCUS) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const int row_h = Scaled(kRowBase);
        if (live.y < scroll_) ScrollTo(live.y);
        else if (live.y + row_h > scroll_ + rc.bottom) {
          ScrollTo(live.y + row_h - rc.bottom);
        }
        return 0;
      }
      if (live.row.kind == Kind::kButton && code == BN_CLICKED) {
        if (activate && activate(live.row.id)) {
          filling_ = true;
          SetWindowTextW(live.control, text ? text(live.row.id).c_str() : L"");
          filling_ = false;
          const bool differs = changed && changed(live.row.id);
          SetWindowTextW(live.mark, differs ? L"•" : L"");
        }
        return 0;
      }
      if ((live.row.kind == Kind::kNumber && code == EN_CHANGE) ||
          (live.row.kind == Kind::kText && code == EN_CHANGE) ||
          (live.row.kind == Kind::kChoice && code == CBN_SELCHANGE) ||
          (live.row.kind == Kind::kCheck && code == BN_CLICKED) ||
          (live.row.kind == Kind::kFlag && code == BN_CLICKED)) {
        Harvest(index);
        return 0;
      }
      break;
    }

    case WM_CTLCOLORSTATIC: {
      // The labels and the marks sit on the form's own face, so they need its
      // colour rather than the white a static assumes. A row this map changed
      // gets the accent ink too, which is what makes "not the game's number"
      // readable across a page rather than one mark at a time.
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetBkMode(dc, TRANSPARENT);
      const HWND which = reinterpret_cast<HWND>(lparam);
      for (const Live& live : rows_) {
        if (which != live.label && which != live.mark) continue;
        if (changed && changed(live.row.id)) SetTextColor(dc, kChangedInk);
        break;
      }
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    // Right-click a row to put it back to the game's default, over the whole row
    // width. The edit controls keep their own menu, which is what somebody
    // right-clicking *inside a text box* is asking for.
    case WM_CONTEXTMENU: {
      POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      POINT client = screen;
      if (screen.x == -1 && screen.y == -1) return 0;   // the Menu key: no row
      ScreenToClient(hwnd_, &client);
      OfferReset(RowAt(client.x, client.y), screen);
      return 0;
    }

    case WM_DESTROY:
      Clear();
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace pfwin

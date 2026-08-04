#include "PaletteGrid.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

namespace pfwin {
namespace {

/// Cell geometry at 96 DPI; scaled by the window's own DPI.
///
/// 38 rather than a round 40 because that is the height of a Warcraft II
/// command-button icon, so a 38-pixel cell draws one pixel for pixel at 100 %.
/// Any other number resamples pixel art by a non-integer factor, which at this
/// size shows as rows of doubled pixels.
constexpr int kCellBase = 38;
/// The cell size a column range aims at: twice the artwork's own 32 pixels.
constexpr int kPreferredBase = 64;
constexpr int kGapBase = 2;
constexpr int kHeadingBase = 20;
constexpr int kPadBase = 4;
/// How much of the selected cell the mat takes on every side: the accent band
/// plus the one-DIP keyline against the artwork.
constexpr int kMatBase = 4;
/// The hover outline. Heavier than the resting one, because the resting one is a
/// divider and this one is an answer to the pointer.
constexpr int kHoverBase = 2;

int Scaled(HWND hwnd, int base) {
  const UINT dpi = GetDpiForWindow(hwnd);
  return MulDiv(base, int(dpi ? dpi : 96), 96);
}

/// How a row comes out: how many cells, and how wide each is.
struct RowFit {
  int per_row;
  int cell;
};

/// The whole sizing rule, in pixels, with nothing about windows in it.
///
/// A free function rather than part of `Arrange` so the three modes can be read
/// against each other: they are three answers to one question, which is which of
/// the count and the cell size the width is allowed to move.
RowFit FitRow(int usable, int gap, int base, int preferred, int fewest,
              int most, int mode) {
  // Tiles at their own size, and however many of them the width holds — what the
  // tile picker has always done, since a thousand tiles stretched four across is
  // not a picker. What is left over is margin: the price of never resampling
  // pixel art by a fraction.
  if (fewest <= 0 || mode == kPaletteScaleTiles) {
    const int per_row = std::max(1, (usable + gap) / (base + gap));
    return {per_row, std::min(base, (usable - gap * (per_row - 1)) / per_row)};
  }

  // Otherwise hold the column count and give the cells the whole width between
  // them. The preference is twice the artwork's own 32 pixels: small enough that
  // a narrow dock still gets the fewest columns, large enough that a wide dock
  // reaches the most before the cells do.
  int per_row = mode;
  if (mode <= 0) {
    const int wanted = (usable + preferred / 2) / std::max(1, preferred);
    per_row = std::max(fewest, std::min(wanted, most));
  }
  return {per_row, std::max(1, (usable - gap * (per_row - 1)) / per_row)};
}

}  // namespace

const wchar_t* PaletteGrid::kClassName = L"PUDForgePalette";

bool PaletteGrid::Register(HINSTANCE instance) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &PaletteGrid::Proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.lpszClassName = kClassName;
  return RegisterClassExW(&wc) != 0;
}

HWND PaletteGrid::Create(HWND parent, HINSTANCE instance, int control_id) {
  return CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, nullptr,
                         WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0,
                         parent, reinterpret_cast<HMENU>(INT_PTR(control_id)),
                         instance, this);
}

void PaletteGrid::SetEntries(std::vector<Entry> entries) {
  entries_ = std::move(entries);
  scroll_ = 0;
  hover_ = -1;
  Relayout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PaletteGrid::SetSelected(int id) {
  selected_ = id;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK PaletteGrid::Proc(HWND hwnd, UINT message, WPARAM wparam,
                                   LPARAM lparam) {
  PaletteGrid* self =
      reinterpret_cast<PaletteGrid*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<PaletteGrid*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT PaletteGrid::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      OnPaint();
      return 0;

    case WM_SIZE:
    case WM_DPICHANGED_AFTERPARENT:
      Relayout();
      InvalidateRect(hwnd_, nullptr, TRUE);
      return 0;

    case WM_VSCROLL:
      OnVScroll(LOWORD(wparam));
      return 0;

    case WM_MOUSEWHEEL: {
      const int lines = -GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 3;
      scroll_ = std::max(0, scroll_ + lines * Scaled(hwnd_, 16));
      SetScroll();
      InvalidateRect(hwnd_, nullptr, TRUE);
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (!tracking_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);
        tracking_ = true;
      }
      const int hit = HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (hit != hover_) {
        hover_ = hit;
        UpdateTooltip(hit);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      if (tip_) {
        // Relay the move, or the tooltip never decides it is time to appear.
        MSG relay = {hwnd_, message, wparam, lparam, 0, {}};
        SendMessageW(tip_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relay));
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_ = false;
      if (hover_ != -1) {
        hover_ = -1;
        UpdateTooltip(-1);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;

    case WM_DESTROY:
      if (tip_) DestroyWindow(tip_);
      tip_ = nullptr;
      return 0;

    case WM_CONTEXTMENU: {
      if (!on_context) break;
      POINT at{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      // A keyboard menu key arrives as (-1, -1); put it over the selected cell
      // and let it mean that cell, the only entry a keyboard user can be said to
      // be pointing at.
      int id = -1;
      if (at.x == -1 && at.y == -1) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        at = {rc.right / 2, rc.bottom / 2};
        ClientToScreen(hwnd_, &at);
        id = selected_;
      } else {
        POINT client = at;
        ScreenToClient(hwnd_, &client);
        const int hit = HitTest(int(client.x), int(client.y));
        // A heading is not an entry; it carries no id worth naming.
        if (hit >= 0 && entries_[size_t(hit)].heading.empty()) {
          id = entries_[size_t(hit)].id;
        }
      }
      on_context(at, id);
      return 0;
    }

    case WM_LBUTTONDOWN: {
      const int hit = HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (hit >= 0 && entries_[size_t(hit)].heading.empty()) {
        selected_ = entries_[size_t(hit)].id;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (on_pick) on_pick(selected_);
      }
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;   // WM_PAINT fills everything it draws over

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void PaletteGrid::EnsureTooltip() {
  if (tip_) return;
  tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                         WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0,
                         hwnd_, nullptr,
                         reinterpret_cast<HINSTANCE>(
                             GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
                         nullptr);
  if (!tip_) return;
  // One tool covering the whole grid; the text is set per cell on hover.
  TOOLINFOW info = {};
  info.cbSize = sizeof(info);
  info.uFlags = TTF_SUBCLASS;
  info.hwnd = hwnd_;
  info.uId = 1;
  info.lpszText = const_cast<wchar_t*>(L"");
  GetClientRect(hwnd_, &info.rect);
  SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
  // Long enough to read a unit name without being in the way of a fast hand.
  SendMessageW(tip_, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(400, 0));
  SendMessageW(tip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELPARAM(8000, 0));
}

void PaletteGrid::UpdateTooltip(int hit) {
  const bool has_text = hit >= 0 && hit < int(entries_.size()) &&
                        entries_[size_t(hit)].heading.empty() &&
                        !entries_[size_t(hit)].label.empty();
  if (!has_text) {
    if (tip_) SendMessageW(tip_, TTM_ACTIVATE, FALSE, 0);
    return;
  }
  EnsureTooltip();
  if (!tip_) return;
  TOOLINFOW info = {};
  info.cbSize = sizeof(info);
  info.hwnd = hwnd_;
  info.uId = 1;
  info.lpszText = const_cast<wchar_t*>(entries_[size_t(hit)].label.c_str());
  SendMessageW(tip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
  SendMessageW(tip_, TTM_ACTIVATE, TRUE, 0);
  // The tool's rectangle follows the cell, so the tip appears beside what it
  // describes rather than wherever the pointer last rested.
  TOOLINFOW area = info;
  area.rect = slots_[size_t(hit)].rect;
  OffsetRect(&area.rect, 0, -scroll_);
  SendMessageW(tip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&area));
}

void PaletteGrid::SetColumns(int fewest, int most, bool scrolls) {
  if (fewest_columns_ == fewest && most_columns_ == most && scrolls_ == scrolls) {
    return;
  }
  fewest_columns_ = fewest;
  most_columns_ = most;
  scrolls_ = scrolls;
  if (hwnd_ && !scrolls) {
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style & ~WS_VSCROLL);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    scrollbar_ = false;
  }
  Relayout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PaletteGrid::Arrange(int width, int& per_row, int& cell) const {
  const int pad = Scaled(hwnd_, kPadBase);
  const int usable = std::max(1, width - pad * 2);
  const RowFit fit =
      FitRow(usable, Scaled(hwnd_, kGapBase), Scaled(hwnd_, kCellBase),
             Scaled(hwnd_, kPreferredBase), fewest_columns_, most_columns_,
             pinned_columns_);
  per_row = fit.per_row;
  cell = fit.cell;
  columns_now_ = per_row;
}

void PaletteGrid::SetColumnCount(int columns) {
  // A pinned count is what the menu used to be able to ask for, and a settings
  // file written by that build still holds one. It was the same rule as fitting
  // the panel with the count answered by hand, so that is where it lands.
  if (columns > 0) columns = kPaletteFitPanel;
  if (pinned_columns_ == columns) return;
  pinned_columns_ = columns;
  Relayout();
  if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

int PaletteGrid::HeightFor(int width) const {
  if (!hwnd_ || entries_.empty()) return 0;
  const int pad = Scaled(hwnd_, kPadBase);
  const int gap = Scaled(hwnd_, kGapBase);
  const int heading_h = Scaled(hwnd_, kHeadingBase);
  int per_row = 1, cell = 1;
  Arrange(width, per_row, cell);

  int y = pad, column = 0;
  for (const Entry& entry : entries_) {
    if (!entry.heading.empty()) {
      if (column) { y += cell + gap; column = 0; }
      y += heading_h;
      continue;
    }
    if (column == per_row) { column = 0; y += cell + gap; }
    column++;
  }
  if (column) y += cell + gap;
  return y + pad;
}

void PaletteGrid::Relayout() {
  slots_.assign(entries_.size(), Slot{});
  content_height_ = 0;
  if (!hwnd_) return;

  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int pad = Scaled(hwnd_, kPadBase);
  const int gap = Scaled(hwnd_, kGapBase);
  const int heading_h = Scaled(hwnd_, kHeadingBase);

  // No discovery: a grid that scrolls keeps its scrollbar and lays out inside
  // what is left, and a grid that does not never has one — either way the width
  // the cells are sized from does not move once the grid has been given one.
  if (scrolls_ != scrollbar_) {
    scrollbar_ = scrolls_;
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    SetWindowLongPtrW(hwnd_, GWL_STYLE,
                      scrolls_ ? (style | WS_VSCROLL) : (style & ~WS_VSCROLL));
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    GetClientRect(hwnd_, &rc);
  }

  const int width = rc.right;
  if (width <= pad * 2) return;
  int per_row = 1, cell = 1;
  Arrange(width, per_row, cell);
  const int usable = width - pad * 2;

  // Where the width the cells did not use goes, and the two modes want opposite
  // answers. Holding a count, the cells cover the row exactly and the remainder
  // goes a pixel at a time into the leftmost ones. Holding the tile size, a cell
  // one pixel wider is the artwork resampled — the thing the mode exists to
  // avoid — so the remainder is split between the two edges.
  const int total = usable - gap * (per_row - 1);
  const int spare = total - cell * per_row;
  const int over = ScalingTiles() ? 0 : spare;
  const int left = pad + (ScalingTiles() ? spare / 2 : 0);
  auto edge = [&](int column) {
    return left + column * (cell + gap) + std::min(column, over);
  };

  int y = pad;
  int column = 0;
  for (size_t i = 0; i < entries_.size(); i++) {
    if (!entries_[i].heading.empty()) {
      if (column) { y += cell + gap; column = 0; }
      // A heading starts where the cells it heads start, not where the grid
      // does; when the two differ it is the cells the eye lines it up with.
      slots_[i].rect = {left, y, width - left, y + heading_h};
      y += heading_h;
      continue;
    }
    if (column == per_row) { column = 0; y += cell + gap; }
    slots_[i].rect = {edge(column), y, edge(column + 1) - gap, y + cell};
    column++;
  }
  if (column) y += cell + gap;
  content_height_ = y + pad;
  SetScroll();
}

void PaletteGrid::SetScroll() {
  if (!scrollbar_) return;   // no scrollbar to describe
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int page = rc.bottom;
  scroll_ = std::max(0, std::min(scroll_, content_height_ - page));
  SCROLLINFO si = {};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = std::max(0, content_height_ - 1);
  si.nPage = UINT(page);
  si.nPos = scroll_;
  SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void PaletteGrid::OnVScroll(int code) {
  RECT rc;
  GetClientRect(hwnd_, &rc);
  const int line = Scaled(hwnd_, 16);
  switch (code) {
    case SB_LINEUP: scroll_ -= line; break;
    case SB_LINEDOWN: scroll_ += line; break;
    case SB_PAGEUP: scroll_ -= rc.bottom; break;
    case SB_PAGEDOWN: scroll_ += rc.bottom; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
      SCROLLINFO si = {};
      si.cbSize = sizeof(si);
      si.fMask = SIF_TRACKPOS;
      GetScrollInfo(hwnd_, SB_VERT, &si);
      scroll_ = si.nTrackPos;
      break;
    }
    default: return;
  }
  SetScroll();
  InvalidateRect(hwnd_, nullptr, TRUE);
}

int PaletteGrid::HitTest(int x, int y) const {
  const POINT pt = {x, y + scroll_};
  for (size_t i = 0; i < slots_.size(); i++) {
    if (PtInRect(&slots_[i].rect, pt)) return int(i);
  }
  return -1;
}

void PaletteGrid::OnPaint() {
  PAINTSTRUCT ps;
  HDC window_dc = BeginPaint(hwnd_, &ps);

  // Everything goes to an off-screen bitmap and reaches the screen in one blit.
  // Painted straight to the window, a repaint is a white fill followed by a
  // hundred and ten icons arriving one at a time — a clear flash whenever the
  // whole grid redraws, as it does on every mouse move during a seam drag.
  const int w = ps.rcPaint.right - ps.rcPaint.left;
  const int h = ps.rcPaint.bottom - ps.rcPaint.top;
  HDC dc = window_dc;
  HDC mem = nullptr;
  HBITMAP buffer = nullptr;
  HGDIOBJ old_bitmap = nullptr;
  if (w > 0 && h > 0) {
    mem = CreateCompatibleDC(window_dc);
    buffer = mem ? CreateCompatibleBitmap(window_dc, w, h) : nullptr;
    if (mem && buffer) {
      old_bitmap = SelectObject(mem, buffer);
      // The buffer's origin is the invalid rectangle's, so every coordinate
      // below stays in client space and none of the drawing knows about this.
      SetViewportOrgEx(mem, -ps.rcPaint.left, -ps.rcPaint.top, nullptr);
      dc = mem;
    } else {
      if (buffer) DeleteObject(buffer);
      if (mem) DeleteDC(mem);
      mem = nullptr;
      buffer = nullptr;
    }
  }

  FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));

  SetBkMode(dc, TRANSPARENT);
  HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  HGDIOBJ old_font = SelectObject(dc, font);

  // Once, not per cell: a hundred and ten of them go past here on every repaint,
  // and the DPI does not change between the first and the last.
  const int mat_most = Scaled(hwnd_, kMatBase);
  const int hover_thick = Scaled(hwnd_, kHoverBase);

  for (size_t i = 0; i < entries_.size(); i++) {
    RECT rect = slots_[i].rect;
    OffsetRect(&rect, 0, -scroll_);
    if (rect.bottom < ps.rcPaint.top || rect.top > ps.rcPaint.bottom) continue;

    if (!entries_[i].heading.empty()) {
      SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
      DrawTextW(dc, entries_[i].heading.c_str(), -1, &rect,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
      continue;
    }

    // Selection is a mat rather than a heavier outline, and hover is neutral
    // rather than accented: two weights of the same colour differ by one pixel,
    // which across a grid of pixel art is not a difference anybody can see. The
    // selected cell is filled with the accent and the icon handed a smaller
    // rectangle, so a band of colour goes all the way round the artwork without
    // covering any of it.
    const bool chosen = entries_[i].id == selected_;
    RECT art = rect;
    int mat = 0;
    if (chosen) {
      // Never more than a quarter of the cell, or a palette dragged narrow would
      // be a column of coloured squares with no artwork left in them.
      const int side = std::min(rect.right - rect.left, rect.bottom - rect.top);
      mat = std::max(1, std::min(mat_most, side / 4));
      FillRect(dc, &rect, GetSysColorBrush(COLOR_HIGHLIGHT));
      InflateRect(&art, -mat, -mat);
      // The icon keeps the window's own background under it: the cells with no
      // artwork draw text, and black on the accent is not a colour pair the
      // theme promises anything about.
      FillRect(dc, &art, GetSysColorBrush(COLOR_WINDOW));
    }

    if (draw_icon) draw_icon(dc, art, entries_[i].id);

    if (chosen) {
      // A keyline between the band and the artwork, in the colour the theme
      // guarantees contrasts with the band. Without it a dark blue brush and a
      // dark blue accent meet with no edge, and the mat stops reading as one.
      if (mat >= 2) {
        RECT keyline = rect;
        InflateRect(&keyline, -(mat - 1), -(mat - 1));
        FrameRect(dc, &keyline, GetSysColorBrush(COLOR_HIGHLIGHTTEXT));
      }
    } else if (int(i) == hover_) {
      HBRUSH ink = GetSysColorBrush(COLOR_WINDOWTEXT);
      RECT edge = rect;
      for (int t = 0;
           t < hover_thick && edge.left < edge.right && edge.top < edge.bottom;
           t++) {
        FrameRect(dc, &edge, ink);
        InflateRect(&edge, -1, -1);
      }
    } else {
      FrameRect(dc, &rect, GetSysColorBrush(COLOR_BTNSHADOW));
    }
  }

  SelectObject(dc, old_font);
  if (mem) {
    SetViewportOrgEx(mem, 0, 0, nullptr);
    BitBlt(window_dc, ps.rcPaint.left, ps.rcPaint.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(buffer);
    DeleteDC(mem);
  }
  EndPaint(hwnd_, &ps);
}

}  // namespace pfwin

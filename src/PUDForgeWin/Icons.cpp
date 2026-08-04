#include "Icons.hpp"

#include <algorithm>
#include <string>

#include "Blit.hpp"
#include "GameData.hpp"
#include "Strings.hpp"
#include "strings.h"

namespace pfwin {
namespace {

/// The middle square of a frame, as its own icon.
///
/// Done here rather than at draw time so the cropping is paid once per frame,
/// and so BlitIcon never has to reason about source rectangles of a top-down
/// DIB — which StretchDIBits documents inconsistently and different GDI versions
/// have disagreed about.
Icon MiddleSquare(int w, int h, const std::vector<uint32_t>& px) {
  Icon icon;
  const int side = std::min(w, h);
  const int x0 = (w - side) / 2, y0 = (h - side) / 2;
  icon.w = icon.h = side;
  icon.fill = true;
  icon.px.resize(size_t(side) * size_t(side));
  for (int y = 0; y < side; y++) {
    const uint32_t* src = px.data() + size_t(y0 + y) * size_t(w) + size_t(x0);
    std::copy(src, src + side, icon.px.begin() + size_t(y) * size_t(side));
  }
  return icon;
}

/// Rasterise one frame of a sprite into a plain RGBA buffer.
bool Rasterise(pf_sprite* sprite, int frame, const pf_tileset_art* art, int owner,
               int& w, int& h, std::vector<uint32_t>& px) {
  w = pf_sprite_width(sprite);
  h = pf_sprite_height(sprite);
  if (w <= 0 || h <= 0) return false;
  px.assign(size_t(w) * size_t(h), 0);
  return pf_sprite_draw(sprite, frame, owner, art, px.data()) == PF_OK;
}

/// How the units cache is keyed. One entry per unit per owner, so switching
/// player back and forth does not re-rasterise what was already made.
int UnitKey(int unit_id, int owner) { return unit_id * 32 + owner; }

}  // namespace

void BlitIcon(HDC dc, const RECT& rect, const Icon& icon, int inset) {
  if (icon.empty()) return;
  const int margin = icon.fill ? 0 : inset;
  const int cw = rect.right - rect.left - margin * 2;
  const int ch = rect.bottom - rect.top - margin * 2;
  if (cw <= 0 || ch <= 0) return;
  int dw = cw, dh = ch;
  if (icon.fill) {
    // Already square, and the cell is meant to be: cover it, keeping the aspect,
    // so a cell that is not quite square crops rather than letterboxes.
    if (icon.w * ch < icon.h * cw) dh = std::max(1, icon.h * cw / icon.w);
    else dw = std::max(1, icon.w * ch / icon.h);
  } else if (icon.w * ch > icon.h * cw) {
    dh = std::max(1, icon.h * cw / icon.w);
  } else {
    dw = std::max(1, icon.w * ch / icon.h);
  }
  const int dx = rect.left + margin + (cw - dw) / 2;
  const int dy = rect.top + margin + (ch - dh) / 2;
  if (icon.fill) {
    // Covering means drawing outside the cell; keep it inside. Intersect and
    // restore rather than select and clear: this DC belongs to the caller's
    // control and already carries a clip to the update region, so dropping it
    // lets the rest of the row — and every row after it — paint over a
    // partially scrolled item instead of being cut off at the edge.
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    BlitRgba(dc, dx, dy, dw, dh, icon.w, icon.h, icon.px.data());
    if (saved) RestoreDC(dc, saved);
    return;
  }
  BlitRgba(dc, dx, dy, dw, dh, icon.w, icon.h, icon.px.data());
}

Icon FlatIcon(uint32_t rgb) {
  Icon icon;
  icon.w = icon.h = 32;
  icon.fill = true;
  // The core hands 0x00RRGGBB; icon pixels travel in its RGBA packing
  // (0xAABBGGRR - see Blit.hpp), so the channels move over.
  const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  icon.px.assign(32 * 32, 0xFF000000u | (b << 16) | (g << 8) | r);
  return icon;
}

int IconRowHeight(HWND reference) {
  const UINT dpi = reference ? GetDpiForWindow(reference) : 96;
  return MulDiv(kListRowBase, int(dpi ? dpi : 96), 96);
}

INT_PTR MeasureIconRow(HWND dialog, LPARAM lparam) {
  auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
  if (!measure) return FALSE;
  if (measure->CtlType != ODT_LISTBOX && measure->CtlType != ODT_COMBOBOX) {
    return FALSE;
  }
  const UINT dpi = GetDpiForWindow(dialog);
  const int base =
      measure->CtlType == ODT_COMBOBOX ? kIconRowBase : kListRowBase;
  measure->itemHeight = UINT(MulDiv(base, int(dpi ? dpi : 96), 96));
  return TRUE;
}

INT_PTR DrawIconRow(LPARAM lparam, const Icon* icon, bool changed) {
  auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
  if (!draw) return FALSE;
  if (draw->CtlType != ODT_LISTBOX && draw->CtlType != ODT_COMBOBOX) return FALSE;
  if (draw->itemID == UINT(-1)) {
    // An empty list, or one that lost its focus rectangle. Nothing to draw
    // but the caret.
    if (draw->itemAction & ODA_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
    return TRUE;
  }
  HDC dc = draw->hDC;
  const bool selected = (draw->itemState & ODS_SELECTED) != 0;
  FillRect(dc, &draw->rcItem,
           GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));

  RECT cell = draw->rcItem;
  const int side = cell.bottom - cell.top;
  cell.right = cell.left + side;
  if (icon && !icon->empty()) BlitIcon(dc, cell, *icon, 1);

  const bool combo = draw->CtlType == ODT_COMBOBOX;
  const UINT get_text = combo ? CB_GETLBTEXT : LB_GETTEXT;
  const UINT get_len = combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN;
  const int length =
      int(SendMessageW(draw->hwndItem, get_len, draw->itemID, 0));
  std::wstring text(size_t(length < 0 ? 0 : length) + 1, L'\0');
  if (length > 0) {
    SendMessageW(draw->hwndItem, get_text, draw->itemID,
                 reinterpret_cast<LPARAM>(&text[0]));
  }
  RECT label = draw->rcItem;
  label.left += side + 4;
  SetBkMode(dc, TRANSPARENT);
  // Selected wins over changed: the highlight brush is already under the text
  // and the accent blue does not have to contrast with it. Unselected, the
  // changed rows take the accent — the same blue Form uses for the same fact.
  SetTextColor(dc, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                            : changed ? kChangedInk
                                      : GetSysColor(COLOR_WINDOWTEXT));
  if (changed) {
    // Right-aligned in its own column rather than prefixed to the name: a
    // prefix pushes every changed row's text out of line with the rest, and
    // the eye reads a list of names down their left edge.
    const std::wstring star = Str(IDS_CHANGED_MARK);
    RECT mark = draw->rcItem;
    mark.right -= 4;
    DrawTextW(dc, star.c_str(), -1, &mark,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SIZE size{};
    GetTextExtentPoint32W(dc, star.c_str(), int(star.size()), &size);
    label.right = mark.right - size.cx - 4;
  }
  DrawTextW(dc, text.c_str(), -1, &label,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (draw->itemState & ODS_FOCUS) DrawFocusRect(dc, &draw->rcItem);
  return TRUE;
}

void IconCache::Open(GameData* game, const pf_tileset_art* art, int tileset) {
  // Unconditionally, even for what looks like the same tileset: `art` is
  // freed and reopened around this call, so a pointer comparison can compare
  // a fresh allocation against a stale one that happened to land at the same
  // address. Rasterising afresh costs a few milliseconds at map load.
  Close();
  game_ = game;
  art_ = art;
  tileset_ = tileset;
  if (game_ && art_) sheet_ = game_->OpenPortraits(tileset);
}

void IconCache::Close() {
  if (sheet_) pf_sprite_free(sheet_);
  sheet_ = nullptr;
  frames_.clear();
  units_.clear();
  sprites_.clear();
  tileset_ = -1;
}

const Icon& IconCache::Frame(int frame) {
  if (frame < 0) return Nothing();
  auto found = frames_.find(frame);
  if (found != frames_.end()) return found->second;
  Icon icon;
  if (sheet_ && frame < pf_sprite_frame_count(sheet_)) {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    if (Rasterise(sheet_, frame, art_, kNeutralOwner, w, h, px)) {
      icon = MiddleSquare(w, h, px);
    }
  }
  return frames_.emplace(frame, std::move(icon)).first->second;
}

void IconCache::SetPreferSprites(bool on) {
  if (prefer_sprites_ == on) return;
  prefer_sprites_ = on;
  units_.clear();   // the frames stay: an upgrade's icon means the same either way
}

const Icon& IconCache::Unit(int unit_id, int owner) {
  if (unit_id < 0) return Nothing();
  if (owner < 0 || owner >= 32) owner = kNeutralOwner;
  const int key = UnitKey(unit_id, owner);
  auto found = units_.find(key);
  if (found != units_.end()) return found->second;

  const int frame = prefer_sprites_ ? -1 : pf_unit_icon(unit_id);
  if (frame >= 0) {
    // Neutral goes through the frame cache, which the upgrade and restriction
    // lists also read: those index the sheet by frame and want it uncoloured,
    // so the two uses really are the same rasterisation.
    if (owner == kNeutralOwner) {
      const Icon& icon = Frame(frame);
      if (!icon.empty()) return units_.emplace(key, icon).first->second;
    } else if (sheet_ && frame < pf_sprite_frame_count(sheet_)) {
      int w = 0, h = 0;
      std::vector<uint32_t> px;
      if (Rasterise(sheet_, frame, art_, owner, w, h, px)) {
        return units_.emplace(key, MiddleSquare(w, h, px)).first->second;
      }
    }
  }
  // The fourteen with no button icon fall back to their own sprite, fitted
  // rather than cropped — and so does everything, when sprites are preferred.
  return Sprite(unit_id, owner);
}

const Icon& IconCache::Sprite(int unit_id, int owner) {
  if (unit_id < 0) return Nothing();
  if (owner < 0 || owner >= 32) owner = kNeutralOwner;
  const int key = UnitKey(unit_id, owner);
  auto found = sprites_.find(key);
  if (found != sprites_.end()) return found->second;

  // Frame 0: the south-facing idle for a mobile unit, the completed state for
  // a building. Decoding the .grp is the expensive part, so an empty result is
  // cached too - a unit whose artwork is missing must not be retried on every
  // repaint of the ghost that wanted it.
  Icon icon;
  if (game_ && art_) {
    if (pf_sprite* sprite = game_->OpenUnitSprite(unit_id, tileset_)) {
      int w = 0, h = 0;
      std::vector<uint32_t> px;
      if (Rasterise(sprite, 0, art_, owner, w, h, px)) {
        icon.w = w;
        icon.h = h;
        icon.px = std::move(px);
      }
      pf_sprite_free(sprite);
    }
  }
  return sprites_.emplace(key, std::move(icon)).first->second;
}

}  // namespace pfwin

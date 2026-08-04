// A scrolling column of labelled controls.
//
// The other half of a master–detail sheet: a list on the left says *which*
// thing, and this says everything about it. One row per field, scrolling
// vertically and only vertically, because a form that scrolls sideways is a
// form whose labels have wandered off the screen.
//
// Real child controls rather than a drawn grid: a unit has thirty-one fields,
// which is thirty-one windows, which is nothing — and every control then
// behaves as a Windows control should, tab order and IME and accessibility
// included, without any of it being reimplemented.
//
// The form knows nothing about maps. The owner says what rows exist and what
// each one reads; the form owns layout, scroll and the plumbing.

#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "UiIcons.hpp"

namespace pfwin {

class Form {
 public:
  enum class Kind {
    kNumber,   ///< digits, clamped to [low, high]
    kText,
    kChoice,   ///< a drop-down over `choices`
    kCheck,
    /// A button whose caption is the value. For things the form cannot edit
    /// itself — a bitmask — where pressing it is the owner's business.
    kButton,
    /// A checkbox carrying its own caption, laid out two to a line.
    ///
    /// What a flags field's named bits are shown as. A mask used to be a hex
    /// number on a button that opened a second dialog — two windows to answer
    /// "can this unit attack ground", which the tick itself answers. Two to a
    /// line because a unit has up to thirty of them, and one to a line is a
    /// scroll where a grid is a glance.
    kFlag,
    /// A read-only block of prose across the full width, with no label.
    ///
    /// For what a value *means* when the value alone does not say: an AI script
    /// is stored as a number, and what it does belongs under the dropdown that
    /// names it. Read-only because it describes rather than edits, and bare —
    /// no label column, no border, no scrollbar — because it is a caption on
    /// the row above rather than a field of its own.
    kNote,
  };

  struct Row {
    int id = 0;                          ///< the owner's name for this row
    std::wstring label;
    Kind kind = Kind::kNumber;
    std::vector<std::wstring> choices;   ///< kChoice only
    int64_t low = 0, high = 0;           ///< kNumber only; equal is unbounded
    /// A small square button immediately after the control: a `UiIcon`, or -1
    /// for a row that wants none.
    ///
    /// For the one thing a value cannot say about itself: an AI script is stored
    /// as a *number*, so "12 Orc 8" says nothing about what that player will do
    /// and the answer is in the game's own listing.
    ///
    /// The form owns it rather than the page so that it scrolls with its row. A
    /// button parked on the page would end up beside a different field.
    int side_icon = -1;
    /// What that button is for. A picture with no words needs a tooltip.
    std::wstring side_tip;
    /// How tall a `kNote` is, in rows. Ignored by every other kind.
    int note_rows = 8;
  };

  static const wchar_t* kClassName;
  static bool Register(HINSTANCE instance);

  HWND Create(HWND parent, HINSTANCE instance, int control_id);
  HWND hwnd() const { return hwnd_; }

  /// Build the controls. Called once per shape of subject, not once per
  /// subject: changing which unit is shown is Reload, which is much cheaper.
  void SetRows(std::vector<Row> rows);

  /// Re-read every row's value out of the owner and put it on screen.
  void Reload();

  /// Artwork for the rows' companion buttons. Set before SetRows; a row whose
  /// icon has not been drawn falls back to an ellipsis, the way the units dock
  /// looked before there was a sheet. Borrowed and may be null.
  void SetUiIcons(const UiIcons* icons) { icons_ = icons; }

  /// What a row currently holds. Numbers and choices and ticks all arrive as
  /// integers, because that is what every one of them is in the file.
  std::function<int64_t(int id)> read;
  /// The caption of a kButton row, and the text of a kText row.
  std::function<std::wstring(int id)> text;
  /// A new value. False refuses it and the control is put back.
  std::function<bool(int id, int64_t value)> write;
  /// A kButton row was pressed. True if anything changed.
  std::function<bool(int id)> activate;
  /// A row's companion button was pressed. It opens something rather than
  /// editing the row, so unlike `activate` it has nothing to report back.
  std::function<void(int id)> activate_side;
  /// Whether to mark this row as differing from the game's default.
  std::function<bool(int id)> changed;
  /// Put one row back to the game's default. Right-clicking a row offers it,
  /// and a form whose owner leaves this null does not offer it at all.
  /// @return whether anything moved
  std::function<bool(int id)> reset;

 private:
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  struct Live {
    Row row;
    HWND label = nullptr;
    HWND control = nullptr;
    /// The optional companion button, to the right of the control.
    HWND side = nullptr;
    /// The bar down the left of a row this map changed. A bar rather than the
    /// dot it used to be: a dot is a mark you have to look for, and what a
    /// mapper wants off a page of thirty numbers is to see at a glance which
    /// of them are not the game's.
    HWND mark = nullptr;
    int y = 0;              ///< top, in content coordinates
    /// Where the row starts and ends across the form. Only the flag rows use
    /// it: they go two to a line, so which of the pair a right-click landed on
    /// is a question about x as well as y.
    int x0 = 0, x1 = 0;
  };

  void Layout();
  /// Show whether a row differs from the game's default: the bar, and the
  /// label's colour.
  void MarkChanged(Live& live);
  /// Which row a point in client coordinates falls in, or -1.
  int RowAt(int x, int y) const;
  /// Offer to put a row back to the game's default, at a screen point.
  void OfferReset(int index, POINT screen);
  void SetScroll();
  void ScrollTo(int y);
  void Clear();
  /// Take what a control now says and give it to the owner.
  void Harvest(int index);
  int Scaled(int base) const;
  int IndexOfControl(int control_id) const;
  /// Which row's companion button an id names, or -1.
  int IndexOfSide(int control_id) const;
  /// Give a companion button its tooltip. One tooltip window for the form,
  /// the way the terrain dock keeps one for its rows of glyphs.
  void Explain(HWND control, const std::wstring& tip);

  HWND hwnd_ = nullptr;
  HINSTANCE instance_ = nullptr;
  HFONT font_ = nullptr;
  /// Borrowed; outlived by nothing here.
  const UiIcons* icons_ = nullptr;
  /// One tooltip window for the companion buttons, made on first need.
  HWND tip_ = nullptr;
  /// The tooltip texts, kept alive because the control stores a pointer and
  /// reads it back when the pointer settles. A deque rather than a vector, and
  /// rather than pointing into the rows themselves: a vector's elements move
  /// when it grows, and a short string moved takes its characters with it.
  std::deque<std::wstring> tip_texts_;
  std::vector<Live> rows_;
  int scroll_ = 0;
  int content_h_ = 0;
  /// True while Reload is writing values in. Setting an edit control's text
  /// raises EN_CHANGE exactly as typing does, so without this the act of
  /// showing a value is indistinguishable from the user changing it — and
  /// switching between units quietly writes each one's values into the next.
  bool filling_ = false;
};

}  // namespace pfwin

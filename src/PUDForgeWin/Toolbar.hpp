// The strip of commands across the top of the window.
//
// Every button raises the very WM_COMMAND its menu item does, so the handling,
// the accelerators and the greying rules are the ones that already exist and
// there is no second answer to "can this be done now". What the strip adds is
// the zoom dropdown, which the menu only has as four fixed percentages.
//
// A button whose cell in the UiIcons sheet has been drawn shows the picture
// instead of its caption; one whose cell is blank keeps the word, so the strip
// is never half a row of blank squares.

#pragma once

#include <windows.h>

#include <vector>

#include "UiIcons.hpp"

namespace pfwin {

class Toolbar {
 public:
  /// Both windows are children of `parent` rather than the strip owning the
  /// dropdown: a combo box notifies its parent, and the window that acts on a
  /// zoom is the application.
  ///
  /// `icons` is borrowed and may be null, which is the same as a sheet with
  /// nothing drawn in it.
  HWND Create(HWND parent, HINSTANCE instance, const UiIcons* icons = nullptr);
  HWND hwnd() const { return bar_; }

  /// How much of the client area the strip wants, in pixels. Zero when it is
  /// hidden, so the caller subtracts it either way and asks nothing.
  int Height() const;

  /// Place the strip across the top of a client area `width` wide.
  void Place(HDWP& defer, UINT flags, int width);

  void Show(bool on);

  /// Grey what cannot be done. The menu bar works this out when a menu drops; a
  /// toolbar is on screen the whole time, so it has to be told.
  void SetEnabled(int command, bool on);

  /// Push a toggle button in or let it out. A tool button that does not show
  /// whether it is the tool in hand is a button you have to try to find out.
  void SetChecked(int command, bool on);

  /// Show a zoom that was reached some other way — the wheel, the bracket keys,
  /// Fit.
  ///
  /// A fit may land between two rungs on a map too big for the ladder's floor,
  /// so a percentage the list does not hold is added to it rather than snapped
  /// to a neighbour the view is not at.
  void ShowZoom(int percent);

  /// The zoom just chosen from the dropdown, or 0 for a WM_COMMAND that was
  /// not the dropdown's.
  int ZoomChosen(WPARAM wparam, LPARAM lparam) const;

  /// Answer the strip's tooltip when it asks its parent for text.
  ///
  /// A toolbar with TBSTYLE_TOOLTIPS owns a tooltip window and sends the
  /// *parent* a TTN_GETDISPINFO for each button, which is why the main window's
  /// WM_NOTIFY calls this.
  /// @return whether this notification was one of ours and has been filled in
  bool TooltipWanted(LPARAM lparam) const;

 private:
  HWND bar_ = nullptr;
  HWND zoom_ = nullptr;
  /// Borrowed from the application, which outlives this.
  const UiIcons* icons_ = nullptr;
  bool showing_ = true;
  /// The ladder, in the dropdown's own order, so a selection index becomes a
  /// percentage without reading the text back and parsing it.
  std::vector<int> rungs_;
  /// Whether the list carries an off-ladder entry for the zoom it is showing.
  bool odd_rung_ = false;
  int shown_zoom_ = 0;
};

}  // namespace pfwin

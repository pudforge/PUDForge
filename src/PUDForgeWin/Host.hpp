// How the child controls talk back to the application window.
//
// The canvas, the docks and the minimap all edit the same Editor and map;
// what they cannot do is reach each other. The main window implements this
// and fans the news out: a paint stroke invalidates the minimap, a palette
// pick changes the canvas ghost, an undo re-enables a menu item.

#pragma once

#include <windows.h>

#include <string>

#include "Editor.hpp"   // Mode, for the panel's own switch between them

namespace pfwin {

struct Host {
  virtual ~Host() = default;

  /// The map's content changed: recompose views, refresh undo/redo and the
  /// dirty marker in the title.
  virtual void OnMapEdited() = 0;

  /// The map is changing inside a gesture, and will go on changing until the
  /// button comes up.
  ///
  /// A terrain stroke is one undo step and only called OnMapEdited at
  /// button-up, which left the minimap a stroke behind for as long as the
  /// stroke lasted. This is the part of OnMapEdited that has to keep up with
  /// the pointer; the title, the undo state and the status cells wait.
  virtual void OnMapStroke() = 0;

  /// The scroll or zoom moved: refresh the minimap's viewport rectangle and
  /// the zoom readout.
  virtual void OnViewChanged() = 0;

  /// Editor state changed without touching the map - mode, tool, brush,
  /// placing unit - so the views that show state need a repaint.
  virtual void OnEditorChanged() = 0;

  /// One line for the status bar. `warn` is for refusals, which the web
  /// client shows in the warning colour rather than silently swallowing.
  virtual void OnStatus(const std::wstring& text, bool warn) = 0;

  /// Something happened to a unit that the game would have made a noise about:
  /// one appeared, one was pointed at, one went away. `kind` is a
  /// `pf_sound_kind`.
  ///
  /// A notification and never a decision: no caller may depend on it, and the
  /// editor behaves identically with the sound off, which it is unless asked.
  virtual void OnUnitSound(int unit_type, int kind) = 0;

  /// Scroll the map so this tile is centred - the minimap's click.
  virtual void OnScrollTo(int tx, int ty) = 0;

  /// The pointer moved to a tile, or off the map (-1, -1) - the status bar's
  /// coordinate cell.
  virtual void OnHoverTile(int tx, int ty) = 0;

  /// A unit was double-clicked: open it. The canvas knows *which* unit,
  /// because it did the hit test; what to show is the application's.
  virtual void OnInspectUnit(int index) = 0;

  /// The right button came up over a tile without a drag — ask what can be
  /// done here. Screen coordinates, because a menu wants them.
  virtual void OnContextMenu(int tx, int ty, POINT screen) = 0;

  /// Which dock is being talked about. Not an index into anything — the
  /// application holds the three as named members, and a number that had to
  /// agree with an array would quietly stop agreeing.
  enum class Dock { kTerrain, kUnits, kMinimap };

  /// A dock asks to be moved to the left (`right` false) or right side.
  ///
  /// The dock raises the menu because that is where the right-click landed,
  /// but only the application knows where the other two are, and the whole
  /// layout is its business.
  virtual void OnDockSide(Dock which, bool right) = 0;

  /// Which side a dock is on now, so its menu can tick the one in force.
  virtual bool DockIsRight(Dock which) const = 0;

  /// Which whole-map terrain edit is being asked for.
  enum class BulkEdit { kReplace, kDecorate };

  /// Open one of the bulk terrain sheets and deal with what it changed.
  ///
  /// The terrain panel carries the buttons because these are terrain edits;
  /// what follows one is the application's, and is exactly what the Tools menu
  /// items do. They call this too, so the button and the menu item cannot come
  /// to mean different things.
  virtual void OnBulkEdit(BulkEdit which) = 0;

  /// Switch the panel between painting terrain and painting movement.
  ///
  /// Through the host rather than straight into the editor, because entering
  /// movement mode turns its overlay on and leaving puts back whatever was
  /// showing — and that is the application's to remember. The View menu items
  /// call the same code, so the switch and the menu cannot drift apart.
  virtual void OnPanelMode(Mode mode) = 0;

  /// Open the tabbed map window on a page, from a dock.
  ///
  /// The units dock offers this beside its player dropdown: picking who the
  /// next unit is for and setting up who that player *is* are one train of
  /// thought, and the menu bar is a long way from the dropdown. The page
  /// number is a `MapSheetTab`, which the dock does not need to know the shape
  /// of — it names the one page it means. `row` is which row of that page to
  /// open on, or -1 for the first: opening on player 1 when the button beside
  /// player 5 was pressed is a click and then a hunt for nothing.
  virtual void OnOpenMapSheet(int tab, int row) = 0;
};

}  // namespace pfwin

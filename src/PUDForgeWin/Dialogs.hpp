// The modal property sheets.
//
// Each opens a template from PUDForge.rc, edits a copy of the map's values
// while it is up, and writes them back in one checkpointed batch on OK — so
// Cancel really cancels and one undo step covers a whole sheet.
//
// The six that are about the map share one tabbed window; see ShowMapSheets.
// Their templates are child dialogs with no caption and no buttons, which is
// why they cannot be opened on their own.
//
// Nothing about the map is decided here: every value goes through the same C
// ABI the canvas uses.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Icons.hpp"
#include "UiIcons.hpp"
#include "pudforge/pudforge.h"

namespace pfwin {

class GameData;

/// Put a window in the middle of the screen it is on.
///
/// The screen rather than the owner, which is what DS_CENTER would give: the
/// main window is often maximised or shoved against an edge, and a sheet
/// centred on it lands anywhere but the middle.
///
/// A no-op for child windows, so the pages of the tabbed map window can call it
/// through the same shared handler without leaping out of their tab.
void CentreOnScreen(HWND window);

/// What the map window did that the caller has to act on beyond "it changed".
///
/// Resizing replaces the grid rather than a value in it, so both selections are
/// rectangles of a grid that no longer exists and the views have to be told the
/// *shape* changed. `dropped_units` is how many units no longer fitted, so the
/// caller can decide how loudly to say so without reading a message that stops
/// being readable the moment it is translated.
struct MapSheetsOutcome {
  bool resized = false;
  int dropped_units = 0;
  /// Players whose race the sheet changed, so the caller can make their units
  /// follow. The swap is the caller's because it is a bulk edit; doing it in a
  /// Tools dialog of its own let the two disagree about `SIDE`.
  std::vector<int> races_changed;
};

/// The six sheets that are *about the map*, as tabs of one window.
///
/// Description, tileset and size; the 16 player slots; the `UDTA` table; the
/// `UGRD` upgrade table; `ALOW`; and what the map is made of. One window
/// because they are one job — setting a map up — and six modal dialogs made
/// that six open-edit-close cycles.
///
/// The `UDTA` and `UGRD` pages are built by walking the core's field tables
/// rather than laid out by hand: 31 fields of four kinds is too many to spell
/// out twice, and the core already knows each one's name, width and options.
///
/// Every page edits a copy and writes nothing until OK, so Cancel cancels all
/// six; each page that changed becomes its own undo step.
///
/// @param tab which page to open on, one of the kMapSheet* constants
/// @param row which row of that page to start on, or -1 for the first. Only the
///        players page reads it, so the units dock can open on the player its
///        dropdown is showing.
/// @param game where the game is installed, for the player page's button that
///        opens the AI script listing on the number it is showing; may be null
/// @param ui the client's own button artwork, for the players page's AI button;
///        may be null, and then that button is the caption it has always been
/// @param outcome what a resize did, and which players changed race; may be null
/// @return whether the map changed; `note` is the line for the status bar
bool ShowMapSheets(HWND owner, HINSTANCE instance, pf_map* map, IconCache* icons,
                   GameData* game, const UiIcons* ui, int tab, int row,
                   std::wstring& note, MapSheetsOutcome* outcome);

enum MapSheetTab {
  kMapSheetMap = 0,
  kMapSheetPlayers,
  kMapSheetUnits,
  kMapSheetUpgrades,
  kMapSheetRestrictions,
  kMapSheetStatistics,
  kMapSheetCount
};

/// Build a map from layered noise, with a preview.
///
/// The numbers mean very little written down and a great deal shown, so every
/// control redraws a one-pixel-per-tile preview of exactly the map that
/// pressing Create would hand back.
/// @return the new map, owned by the caller, or null if cancelled
pf_map* ShowGenerate(HWND owner, HINSTANCE instance, int tileset);

/// Find a unit by typing.
///
/// Not a filter over the unit palette, which is what it used to be: the palette
/// is where browsing happens, and losing your place in the grid every time you
/// searched was the cost of sharing it.
///
/// `lead_race` is which race's sections come first, so an empty query lists the
/// units in the same order as the grid this shadows.
/// @return the chosen unit id, or -1 when dismissed
/// `with_unused` is Editor::offer_unused_units, so a search finds exactly what
/// the grid offers rather than a set of its own.
int ShowQuickPick(HWND owner, HINSTANCE instance, IconCache* icons,
                  char lead_race, bool with_unused);

/// Every tile the artwork can draw, grouped by the terrain it mostly shows.
/// The corner model will not produce roughly a third of a tileset on its own,
/// and this is how those are reached.
/// @param current the tile to open on, or -1 for none
/// @return the chosen tile value, or -1 when cancelled
int ShowTilePicker(HWND owner, HINSTANCE instance, const pf_tileset_art* art,
                   int tileset, int current);

/// The player slots worth offering, in order: the eight playable ones, then
/// neutral. The seven between are storage the game reads nothing from and no
/// corpus map writes to, so nothing lists them — but the tables behind them
/// stay sixteen wide, and a map carrying something there keeps it.
const std::vector<int>& PlayerSlots();
/// Which slot a list row stands for, or -1. Rows are not slots any more.
int SlotForRow(int row);
/// Which row shows a slot, or 0 for one that is not listed.
int RowForSlot(int slot);

}  // namespace pfwin

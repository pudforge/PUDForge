// The whole-map edits, and the sheets that are about the map rather than in it.
//
// PUDDraft's Tools menu and the web client's, kept apart from Dialogs.cpp
// because they have a different shape: the property sheets edit a table of
// values, these ask two questions and then change a great deal at once. All of
// them say how much they are about to touch first, and all are one undo step —
// a bulk edit you cannot take back in one go is one nobody presses twice.
//
// Everything they do lives in pfwin::Editor, which is Win32-free and tested;
// this file is the Win32 half only.

#pragma once

#include <windows.h>

#include <string>

#include "Editor.hpp"
#include "GameData.hpp"
#include "Icons.hpp"
#include "pudforge/pudforge.h"

namespace pfwin {

/// Repaint one terrain as another, across the terrain rectangle or the map.
/// @return whether the map changed
bool ShowReplaceTerrain(HWND owner, HINSTANCE instance, Editor& editor,
                        const pf_tileset_art* art, std::wstring& note);

/// Scatter a terrain across the scope as isolated tiles.
/// @return whether the map changed
bool ShowDecorate(HWND owner, HINSTANCE instance, Editor& editor,
                  const pf_tileset_art* art, std::wstring& note);

/// Turn every unit of one type into another.
/// @return whether the map changed
bool ShowConvertUnits(HWND owner, HINSTANCE instance, Editor& editor,
                      IconCache* icons, std::wstring& note);

/// What the map is made of: size, terrain shares, units per player, resources.
///
/// A page of the tabbed map window rather than a dialog of its own, so this is
/// the page's proc and `lParam` on WM_INITDIALOG is the `const pf_map*` to
/// describe. Nothing here edits anything.
INT_PTR CALLBACK StatsPageProc(HWND page, UINT message, WPARAM wparam, LPARAM lparam);

/// Render the map to a PNG, with the options the web client offers.
/// @return whether a file was written; `note` says what happened either way
bool ShowExportPng(HWND owner, HINSTANCE instance, Editor& editor,
                   const pf_tileset_art* art, pf_sprite_set* sprites,
                   std::wstring& note);

/// The editing and placement options, which used to be checkable menu items.
/// `unit_art`, `vary_facing`, `unit_sounds` and `check_updates` belong to the
/// client rather than the editor, so they travel separately.
///
/// `reset` comes back true when the person asked, and confirmed, that every
/// remembered setting be forgotten. The caller has to stop saving on exit, which
/// only it can do.
/// @return whether anything changed
bool ShowOptions(HWND owner, HINSTANCE instance, Editor& editor, int* unit_art,
                 bool* vary_facing, bool* unit_sounds, bool* check_updates,
                 bool* reset);

/// Whether the client has been reset this session, so nothing it remembers is
/// written back out.
///
/// Latched where the deletion happens rather than passed back from the dialog:
/// the reset runs inside a modal loop, and a window closed while that loop is
/// still up — the session ending, or somebody impatient — would otherwise save
/// the very settings that were just deleted. Measured that way round first.
bool SettingsWereReset();

/// Which installed copy of Warcraft II to read the artwork out of: the first
/// thing a new install asks, and the same dialog behind Tools > Game Folder.
/// @return whether a folder was adopted
/// @param required the editor has no game to fall back on, so closing this ends
///        the run: there is no Cancel button and the note says what closing does.
bool ShowGameSetup(HWND owner, HINSTANCE instance, GameData& game, bool required);

/// The guide, in whatever the user reads HTML with.
///
/// Here rather than beside the Help menu that was its only caller, because the
/// welcome screen offers it too: somebody deciding what this program is has more
/// use for it than somebody who has been running it for a month.
/// @return whether it opened
bool OpenUserGuide(HWND owner);

/// Tileset and size for a new map.
/// @return the new map, owned by the caller, or null if cancelled
pf_map* ShowNewMap(HWND owner, HINSTANCE instance, int tileset);

/// What the game reads out of `SQM `, and how much of this map disagrees with
/// its own terrain. @return whether the map changed
bool ShowMovement(HWND owner, HINSTANCE instance, Editor& editor, std::wstring& note);

/// The game's AI script table, disassembled. Read-only: a PUD stores the script
/// number, not the script, so this answers "what does AI 12 do". `start` is the
/// script to open on, so the answer is on screen rather than two hundred rows
/// down.
void ShowAiScripts(HWND owner, HINSTANCE instance, GameData& game, int start = 0);

/// What one AI script does, as a block of text: the summary, then the
/// disassembly under it.
///
/// Exported so the player page can show it under its own dropdown rather than
/// sending the reader to another window. One formatter, so the two views cannot
/// come to describe the same script differently.
/// @param scripts may be null, which is a machine with no game data to hand
std::wstring AiScriptText(const pf_ai_scripts* scripts, int value);

/// Open a map out of an archive: the campaign maps ship inside War2Dat.mpq
/// and are not files on disk, so this is the only way to reach them.
/// @return the chosen map, owned by the caller, or null. `name` receives what
///         it was called inside the archive, for the title bar.
pf_map* ShowOpenFromArchive(HWND owner, HINSTANCE instance, GameData& game,
                            std::wstring& name, std::wstring& note);

/// One unit up close: owner, and the single number the format keeps per unit.
///
/// `open_properties_for` receives the unit *type* when the user left through
/// the "… Unit Properties" button, or -1. This dialog is about one unit on the
/// map and the sheet it hands over to is about every unit of that type, so the
/// two cannot be the same window — but going from the first to the second is
/// the commonest next thing, and hunting for it in the menu bar is a poor
/// answer. Any edits made here are saved on the way out either way.
/// @return whether the unit changed
bool ShowUnitInspector(HWND owner, HINSTANCE instance, Editor& editor,
                       IconCache* icons, int index, std::wstring& note,
                       int* open_properties_for = nullptr);

}  // namespace pfwin

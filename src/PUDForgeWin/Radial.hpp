// The terrain ring: the brushes arranged around the pointer.
//
// There are ten of them and they never change, so they can all be on screen at
// once and picked by direction — faster than reading, and it becomes muscle
// memory. The palette in the dock keeps existing beside it: the ring is for the
// hand that already knows what it wants.
//
// Space opens it under the pointer. Held, it is spring-loaded: move and let go.
// Tapped, it stays up to be clicked at leisure. Which one a person means is not
// knowable in advance, and the ring can tell by how long the key was down.
//
// The wheel does not size the brush here, though the ring is the one moment it
// is not spoken for by scrolling: sizing is a thing you do constantly and the
// ring is a thing you open occasionally. Alt and the wheel do it over the canvas.

#pragma once

#include <windows.h>

#include "pudforge/pudforge.h"

namespace pfwin {

/// Put the ring up and run it until something is chosen or it is dismissed.
///
/// Modal, with a message loop of its own, like a menu: while it is up the
/// pointer means only "which wedge", and letting the map see any of that would
/// paint terrain behind it.
/// @param at       where to centre it, in screen pixels
/// @param current  the brush to open on
/// @param art      tiles the wedges are filled with; null falls back to the
///                 flat terrain colours
/// @return the chosen brush index, or -1 when dismissed
int ShowTerrainRing(HWND owner, HINSTANCE instance, const pf_tileset_art* art,
                    int tileset, int current, POINT at);

/// Registers the ring's window class. Call once, with the others.
bool RegisterTerrainRing(HINSTANCE instance);

}  // namespace pfwin

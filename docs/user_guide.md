# PUDForge user guide

PUDForge is a map editor for Warcraft II. It reads and writes `.pud` files for
both the Battle.net edition and Warcraft II Remastered.

This guide describes the Windows editor: the window, the tools, the map
properties, and the keyboard.

## Requirements

An installed copy of Warcraft II. Either Warcraft II Battle.net Edition or Warcraft II Remastered works.

The editor reads the tilesets, sprites and portraits from that installation and
draws maps with them.

The installation is required. PUDForge reads its terrain, units, names and
sounds from it, and closes with a message if it cannot find one.

## Choosing the installation

On first start, PUDForge lists the installations it found and asks which to
use. Select one and choose **OK**. The choice is stored and the question is not
asked again, unless that installation later moves or is removed.

If the list is empty, or the copy is somewhere unusual, choose **Browse** and
select the folder the game is installed in. Select the top of the installation,
not the folder the data files happen to sit in; PUDForge searches inside it.

Closing the window closes PUDForge without starting it.

To change the installation later, use **Tools ▸ Select Warcraft II Data…**, which
reopens the same list.

### Setting the path from a file

A `PUDForge.ini` file next to `PUDForge.exe` takes precedence over both the
stored choice and the search:

```ini
[Game]
Path=D:\Games\Warcraft II
```

Use this to configure a machine you cannot install to, or several machines at
once.

### Where PUDForge looks

In order: `PUDForge.ini`, the installation paths recorded in the registry, the
folder holding `PUDForge.exe` and its parent folders, then the default
installation folders. Placing `PUDForge.exe` inside the Warcraft II folder is
therefore sufficient on its own.

Data is read from `War2Dat.mpq` where the edition provides one, and from loose
files where it does not. There is no unpacking step in either case.

## The window

| Area | Contents |
|---|---|
| Menu bar and toolbar | Commands, and the zoom control at the right of the toolbar |
| Terrain panel | The terrain palette and brush settings |
| Map | The editing surface |
| Minimap | The whole map, and the visible region marked on it |
| Units panel | The player selector and the unit palette |
| Status bar | Seven cells of state, described below |

Every element except the map can be hidden from the **View** menu. The map
takes any space that is freed.

The dividers between the panels and the map can be dragged. Double-clicking a
divider restores that panel's default width. Right-clicking a panel offers to
move it to the other side of the window.

### The status bar

| Cell | Contents |
|---|---|
| Message | The most recent message. Click the cell to open the log. |
| Tool | The active tool: *Painting*, *Selecting tiles*, *Placing units* |
| Hints | The modifier keys the active tool accepts |
| Pointer | The tile under the pointer and its terrain |
| Selection | The number of units selected, or the tile count and bounding box |
| Zoom | The current zoom percentage |
| Size | The map dimensions in tiles |

The message cell holds one line and is overwritten by the next message.
Messages worth keeping, such as the result of a bulk edit or the number of
units a paste could not place, are also written to the log
(**Tools ▸ Log…**, or Ctrl+L).

## Modes

The editor has two modes: terrain and units. **T** selects terrain mode and
**U** selects unit mode. The panels follow the mode.

A mode always has an active tool. Selecting a terrain arms the paint tool with
it; selecting a unit arms the placement tool with it.

## Terrain mode

The terrain panel holds a palette of terrains above five rows of brush
settings.

| Setting | Values |
|---|---|
| Palette | One cell per terrain. The final cell is *Custom*: a single tile chosen from the whole tileset. |
| Detail | *Plain*, *Mixed* or *Detail*. How much of the painted ground carries rocks and flowers: none, about three tiles in ten, or all of it. Terrain the tileset draws only one way ignores the setting. Default *Plain*. |
| Shape | Square, round, spray or fill. Spray continues to apply while the button is held. |
| Size | ½, then 1 to 21 tiles. The whole sizes are odd, so the brush has a centre tile. ½ is the rung below one tile: it lays a single corner of the terrain grid, a quarter of what the 1 tile brush lays, and is the smallest mark a map can hold. It aims at the nearest corner rather than the tile under the pointer, and ignores the Shape setting — a single corner has nothing to round off or scatter. Walls and the custom tile have no corner to sit on, so they fall back to one tile. |
| Mirror | None, left to right, top to bottom, or either diagonal. Mirrors combine. Diagonals require a square map. |
| Shade | *Light*, *Dark* or *Mix*. *Mix* varies the two shades while painting. |

Ground, water and coast each exist in a light and a dark version. The **Shade**
row selects which version the brush lays. Holding **Alt** paints with the other
shade for as long as the key is held.

Painting adjusts the surrounding terrain so that edges resolve. Painting water
into open ground produces the shoreline tiles automatically, because those are
the tiles that express the corners produced by the stroke.

| Action | Result |
|---|---|
| Drag | Paint along the pointer |
| Shift+drag | Constrain the stroke to a straight horizontal or vertical line |
| `[` and `]`, or Alt+wheel | Change the brush size |
| Shift+click with the fill shape | Fill every tile of that terrain on the map, not only the contiguous region |

**Replace…** and **Decorate…** at the foot of the panel apply one operation to
an area: repainting one terrain as another, or scattering a terrain through it.
Both act on the selected tiles if there is a tile selection, and on the whole
map otherwise. Both report how many tiles they will change before changing
them.

## Unit mode

The units panel holds a player selector above a palette of the units that
player can own.

The player selector lists the eight playable slots and the neutral slot. The
game does not support the remaining slots, so they are not offered.

By default the palette shows the selected player's race together with units
that belong to no race, such as neutral units and heroes. Assigning a unit to a
player of the other race converts it to that race's equivalent. To list every
race and disable conversion, use **Tools ▸ Options…**.

The **…** button beside the player selector opens that player's settings: race,
controller, starting resources and AI script.

| Action | Result |
|---|---|
| Click | Place one unit |
| Drag | Place a row of units as a single undo step |
| Ctrl+click | Adopt the type and owner of the unit under the pointer |
| Q | Find a unit by typing part of its name |
| Double-click | Open the unit under the pointer |
| Enter | Open properties for the selected unit |
| 1 to 8 | Assign the selection to that player |
| Ctrl+D | Place another of the selected unit |

A unit covers the tiles the game gives it, and the editor keeps that area
clear. Most units cover one tile, buildings cover two to four, and ships and
flying units cover four in a two by two square.

The editor refuses placements the game would refuse: a ship on land, a ship in
water too small for it, a town hall too close to a gold mine, a unit on the map
edge. The status bar names the unit and the reason. Each refusal can be
disabled in **Tools ▸ Options…**.

Existing maps that break these rules load unchanged. On the first sight of such
a map the editor says how many units are in a position the game cannot use, and
offers to remove them; see **Checking a map**. If you decline, the map stays as
it is.

## Movement mode

**View ▸ Mode ▸ Movement** (M), or the **Movement** button at the foot of the
terrain panel, paints the layer under the artwork: what may
walk, swim, sail or fly over each tile. The terrain palette becomes the eight
movement values the game uses, in the colours the movement overlay draws them,
and the overlay comes on while you are in the mode.

Click or drag to lay the selected value over any tile, whatever is drawn there.
The brush size, shape and mirrors are the ones the terrain brush uses, and so
are its keys: **[** and **]** or **Alt** and the wheel size it, and **Ctrl** and
the left button adopt the value already on a tile — which is how you find out
what a map somebody else wrote says, and then paint more of it. The
bucket is not offered: it would follow the terrain, and this layer is the one
that disagrees with the terrain on purpose.

The palette carries fourteen values. Eight are the ones Blizzard's own maps
use. **Land and water** stops nothing, so a walker and a ship may both be on
it, and **No walking or flying** stops both, which is the only barrier nothing
crosses. The rest stop one thing at a time: ground no flier may cross, water no
platform may be built on, and so on.

The last cell of the palette, **Match the terrain**, is not a value but the
absence of one. Painting with it puts each tile back to what its terrain
implies, which is how a single override comes off; **Tools ▸ Tile Movement
Data** does the same to the selection or the whole map at once.

A value you paint here survives a terrain edit over the same tile, and a save
and a reload. Nothing in the format records which tiles you meant, and nothing
needs to: a value that disagrees with the tile under it is the answer.

## Selections

Terrain and units have separate selections and are edited independently.

**Tiles** (Ctrl+T). Drag a rectangle. **Shift+drag** adds a rectangle,
**Alt+drag** subtracts one. The status bar reports the tile count and the
bounding box separately.

**Units** (Ctrl+U). Click a unit, or drag a band around several.
**Shift+click** adds to the selection. Dragging the selection moves it. A move
is all or nothing: if any unit cannot occupy its destination, no unit moves.
During such a drag the units follow the pointer in red and white rectangles
mark where they would land.

The **Select** menu selects in bulk: all units, none, the inverse, the same
type as the selection, the same player as the selection, everything of one
kind, or everything belonging to one player.

## Copy and paste

A copy holds terrain or units, never both. **Copy** (Ctrl+C) uses the current
mode to decide which. **Copy the Terrain** and **Copy the Units** state it
explicitly. **Cut** applies to units only.

Paste is armed rather than immediate. After Ctrl+V the fragment follows the
pointer and a click places it. Nothing is written to the map until that click,
and the right button cancels.

| Key | Effect on the armed fragment |
|---|---|
| R | Rotate a quarter turn |
| F | Flip left to right |
| M | Mirror top to bottom |

A fragment stores corner terrains rather than tile values, which is what allows
it to be rotated without the artwork facing the wrong way. A selection with
holes keeps its holes, and the map shows through where the paste will not
write.

**Edit ▸ Blend a Paste into Its Surroundings** controls whether the seam is
refitted into the terrain it lands on. Disable it to move a fragment unchanged;
enable it to graft a fragment into a coastline.

## Map properties

The map properties window has six tabs. **Ctrl+Shift+P Y U G N I** opens a
specific tab directly.

| Tab | Contents |
|---|---|
| Map | Description, tileset and size, including resizing |
| Players | Race, controller, starting resources and AI script, per slot |
| Units | Every statistic of every unit type (`UDTA`) |
| Upgrades | Costs, research times and icons (`UGRD`) |
| Restrictions | What each player may build, cast or research (`ALOW`) |
| Statistics | A summary of the map's contents |

Two rules apply to all six tabs:

- Each tab edits a copy and writes nothing until **OK**. Each tab that changed
  becomes its own undo step, so undoing a change to the players does not undo a
  change made to the unit data first.
- A value that differs from the game default is marked in colour, and its list
  row carries a star. Right-clicking a row restores it. This works on a single
  field or on a whole unit.

The Units, Upgrades and Restrictions tabs each carry a **Use the game default**
checkbox. Ticking it removes the map's own copy of that section, so the map uses
the game's table. Clearing it gives the map an editable copy to change.

The Units and Upgrades tabs import and export raw section files (`.un`, `.up`),
the PUDDraft plugin format. The buttons are on the tab that shows the section
they carry.

## Navigation

| Input | Result |
|---|---|
| Wheel | Zoom about the pointer |
| Shift+wheel | Scroll horizontally |
| Middle button | Hold and push to scroll continuously in that direction |
| 0 | Fit the map to the window |
| + and - | Step the zoom ladder |
| Arrow keys | Scroll one tile |
| G | Toggle the tile grid |

The grid draws a heavier line every eight tiles for use as a ruler.

Clicking the minimap jumps to that point; dragging scrolls continuously and the
map follows during the drag.

**View ▸ Layer** replaces the artwork with the values the file stores:
movement, regions or raw tile ids. The movement layer marks tiles whose stored
value disagrees with the value their terrain implies.

**View ▸ Sight and Range of the Selected Unit** draws the sight and attack ranges of
the selected units as circles around their footprints.

## Checking a map

**Map ▸ Check Map** reports conditions that make a map unplayable or unusual:
players with no start location, units on terrain they cannot occupy, buildings
too close to a gold mine, and start locations with no gold within reach. Errors
sort before warnings.

Conditions that are merely unusual are reported as measurements rather than
faults. An uneven gold split is reported as a number, because the shipped maps
range from even to heavily lopsided.

The editor makes part of this check when you open a map, and asks once whether
to remove the units it finds: units outside the map, units on terrain they
cannot occupy, and units on top of each other. It removes them as one step,
which **Edit ▸ Undo** reverses. The answer is No until you change it.

It asks only about the rules you keep. If **Allow units on top of each other**
is on, it does not ask about units on top of each other.

Some units share tiles because the game intends it, and the editor never
reports those: a start location below a town hall, a ship on an oil patch, and
a ground unit on a Circle of Power. Nothing may share a gold mine.

**Map ▸ Place Start Locations** adds the missing start locations on open ground,
away from each other and near gold. The editor offers this on save if a map has
units but fewer than two start locations.

## Generating a map

**File ▸ Generate a Map…** (Ctrl+G) builds a map from layered noise. Set the
proportions of water, coast, forest and rock, the number of clearings to carve
for bases, and a seed. Every control redraws a preview of the map that
**Create** would produce.

The seed makes a result reproducible: the same values produce the same map.

## Tools

| Command | Function |
|---|---|
| Convert Units… | Replace units of one type with another, across the map or within the selection |
| AI Scripts… | The AI script table the map's players refer to |
| Export to a PNG… | Render the map to an image file |
| Log… (Ctrl+L) | Every message the editor has produced this session |
| Select Warcraft II Data… | Change the game installation |
| Options… | Editor settings |

## Options

**Tools ▸ Options…** contains three groups.

**Terrain.** Whether painting adjusts the surrounding terrain, whether a paste
blends into its surroundings, and whether units the terrain no longer supports
are kept rather than removed.

**Unit placement.** Three overrides: allow illegal positions, allow units on
top of each other, and allow units on the map edge. A fourth setting marks
special units on the map. All are off by default.

**Unit display.** Unit artwork as command-button icons or as sprites, varied
facing, the game's unit sounds, whether the palette lists every race or only
the selected player's, and whether it offers the unused and special units.

The unused and special units are the ones an editor normally holds back: the
slots the game has no unit for, the corpses and rubble the game makes while it
runs, and the two campaign workers. Leave this off unless you need them. Five
of the slots have no unit behind them, and a map that places one stops the
game. Walls stay out of the palette either way, because walls are terrain here.

Everything in this window is kept between sessions.

## Resetting the editor

**Tools ▸ Options… ▸ Reset Everything** discards everything PUDForge has
stored: the options, the window size and layout, the view settings, the list of
recent maps, and the game installation path. The command lists what will be
discarded and asks for confirmation.

No map file is affected and the open map stays open. The defaults apply from
the next start, and the editor asks again which installation to use.

## Keyboard reference

Every key the editor accepts. **F1** opens this guide.

### File

| Key | Function |
|---|---|
| Ctrl+N | New map |
| Ctrl+O | Open a map |
| Ctrl+S | Save |
| Ctrl+G | Generate a map |

### Editing

| Key | Function |
|---|---|
| Ctrl+Z, Ctrl+Y | Undo, redo |
| Ctrl+X, Ctrl+C, Ctrl+V | Cut, copy, arm a paste |
| R, F, M | Rotate, flip or mirror the fragment being pasted |
| Ctrl+D | Place another of the selected unit |
| Delete | Delete the selected units |
| Enter | Unit properties, for the one unit selected |
| 1 to 8 | Give the selection to that player |
| Ctrl+Shift+P Y U G N I | The six map property tabs: map, players, units, upgrades, restrictions, statistics |
| Esc | Release the tool, then clear the selection |

### Choosing what to draw

| Key | Function |
|---|---|
| Space | Terrain ring: hold and release, or tap then click |
| Q | Find a unit by typing |
| T, U | Terrain mode, unit mode |
| Ctrl+T | Drag a rectangle of terrain |
| Ctrl+U | Click or band-select units |

### The brush

| Key | Function |
|---|---|
| `[`, `]`, Alt+wheel | Brush size, one step at a time |
| Alt, held | Paint with the other shade |
| Shift+drag | Draw a straight line, across or down |
| Shift+click | Filling: fill every tile of that terrain, not just this region |

### Looking around

| Key | Function |
|---|---|
| 0, +, - | Fit the map, zoom in, zoom out |
| G | The tile grid |
| Arrow keys | Scroll a tile at a time |
| Wheel | Zoom about the pointer |
| Middle button | Hold and push to scroll |
| Ctrl+L | Log |

### The mouse

| Key | Function |
|---|---|
| Shift+click | Add to the selection. Over terrain, Alt+drag to exclude |
| Ctrl+click | Pick the terrain or unit type under the pointer |
| Double-click | Open the unit under the pointer |
| Right-click | Context menu |

The terrain ring is the one gesture with no menu equivalent. Pressing Space
displays ten brushes around the pointer, selected by direction. Held, it is
spring loaded: move the pointer and release. Tapped, it stays open until
clicked.

## Command line

`PUDForge.exe` renders a map to a PNG and exits, for screenshots and scripting:

```powershell
PUDForge.exe --render map.pud --out shot.png
PUDForge.exe --render map.pud --out shot.png --tiles 0,0,64,64 --scale 2 --grid
PUDForge.exe --tilesheet 0 --out forest.png
PUDForge.exe --version
```

Rendering uses the same code the canvas paints with, so the output matches the
editor. `--data <folder>` overrides the game installation for that run.

PUDForge is a windowed application, so a shell does not wait for it and does not
receive its exit code. Run it with `Start-Process -Wait` from PowerShell if the
exit code is needed.

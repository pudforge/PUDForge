# Changelog

What changed in each release, newest first. CI reads the section for the
version in `version.h` and posts it as the GitHub release notes. What you write
here is what people read.

Write it in ASD-STE100 (Simplified Technical English), as a short list of what
is different for a user. One bullet for each change, under **Fixed**, **New**
or **Changed** — a person reads a release to find out whether a fault they hit
is gone, and that is a different question from what is new.

**Terse is the house style.** One line for one change, and nothing around it.
Boring is correct here: this is a list of what moved, not an account of it.

- One idea in each sentence. No more than 25 words, and usually far fewer.
- The active voice, the present tense, and the same word for the same thing.
- No metaphors, no jargon, and no words that only the code explains.
- Give the change and nothing else. Not the reason, not what it was like
  before, not why it matters, not what else it touches. All of that is in the
  commit message, which is where it belongs.
- Leave out the conditions, the defaults and the exceptions. Most people want
  to know what is different, not when it applies. The user guide has the rest.
- Nothing unrelated to what shipped.

So:

    - Report an Issue, on the Help menu.

and not:

    - Help, Report an Issue sends a report to the PUDForge issue list. You can
      include the message log with it, and the reply gives you a link to follow.

A section is for a *release*, not for a commit. `version.h` gets a patch bump
on every commit, but only the version on `master` when CI runs gets a tag. A
section therefore covers all the commits after the previous tag.
`scripts/prep-release.ps1` collects those commits as a start. See
`docs/releasing.md`.

## 0.1.73

**Changed**

- The release notes no longer carry the text about virus warnings.

## 0.1.72

**New**

- You can update PUDForge from the Help menu.

## 0.1.71

**New**

- Check for Updates, on the Help menu. It downloads and installs a new release
  and offers a restart.
- PUDForge checks for a new release once a day when it starts. Options has the
  switch.

## 0.1.70

**New**

- Generate a Map has a Mirror row. The map, its gold mines and its start
  locations reflect across the axes you choose.

**Changed**

- The Generate a Map preview is square and larger.

## 0.1.69

**Changed**

- Report an Issue is not in this release.

## 0.1.67

**New**

- Report an Issue, on the Help menu. It can include the message log, and gives
  you a link to your report.

## 0.1.66

**New**

- Report an Issue, on the Help menu. It can include the message log.

## 0.1.65

**Fixed**

- The movement layer goes off when you leave movement mode. It stayed on the
  map until you set View, Layer, None.

## 0.1.64

**Fixed**

- A unit that you place is active. PUDForge made every unit passive.

**Changed**

- Unit properties give the active and passive states two buttons, in place of
  a number.

## 0.1.63

**New**

- Map validation reports units that have no hit points, and says how to put
  them back. A map made before the expansion gives the expansion units no
  statistics.

## 0.1.62

**Fixed**

- The unit and upgrade pages show the values Warcraft II uses when a map is set
  to use the default tables. Expansion heroes showed zero for every statistic.
- A change to a unit or an upgrade clears the "use the game default table"
  tick. The change had no effect in the game while the tick stayed on.

**New**

- A Reset All button on the unit and upgrade pages puts the whole table back to
  the values Warcraft II uses.

## 0.1.61

**Fixed**

- The Daemon and the Eye of Kilrogg follow the rules for air units. They
  occupy 2 x 2 tiles and go on every other tile.

## 0.1.60

**New**

- A movement editing mode, at View, Mode, Movement. It paints what may cross
  each tile, whatever the tile looks like: ground units over water, no flying
  over open ground, or one tile that a ship and a ground unit share.
- The brush is the terrain brush, with the same sizes and keys. Ctrl and the
  left button take the value under the pointer.

**Fixed**

- Oil patches and oil wells go on every other tile, as the game places them.

## 0.1.59

**New**

- A movement editing mode, at View, Mode, Movement. It paints what may cross
  each tile, whatever the tile looks like: ground units over water, no flying
  over open ground, or one tile that a ship and a ground unit share.
- The brush is the terrain brush, with the same sizes and keys. Ctrl and the
  left button take the value under the pointer.

**Fixed**

- Oil patches and oil wells go on every other tile, as the game places them.

## 0.1.40

**Fixed**

- Placement rules for water and air units.

## 0.1.37

**Fixed**

- Computer players use transports again.
- PUDForge writes correct region data for tiles on the waterline.
- Saving a map rebuilds its region data.

## 0.1.35

**Fixed**


- Ships and flying units occupy 2 x 2 tiles, the area Warcraft II gives them.
  The editor gave them one tile, and let you put a ship in a space that is too
  small.
- The editor keeps the unit placement and terrain options after you close it.
  Each session started with the default values.
- The Units menu shows each group one time after you change the Warcraft II
  folder. It showed every group two times.

**New**

- When you open a map, the editor finds the units that the game cannot place
  and offers to remove them.
- Convert Units can change only the units that you select.
- An option adds the unused and special units to the palette and the menus.
  The option is off, because some of these units can stop the game.

**Changed**

- The Generate a Map window is larger, with a much bigger preview.

## 0.1.26

- The first public release.

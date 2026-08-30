# Changelog

What changed in each release, newest first. CI reads the section for the
version in `version.h` and posts it as the GitHub release notes. What you write
here is what people read.

Write it in ASD-STE100 (Simplified Technical English), as a short list of what
is different for a user. One bullet for each change, under **Fixed**, **New**
or **Changed** — a person reads a release to find out whether a fault they hit
is gone, and that is a different question from what is new.

- One idea in each sentence. No more than 25 words.
- The active voice, the present tense, and the same word for the same thing.
- No metaphors, no jargon, and no words that only the code explains.
- Give the change and not the reason. What was wrong, why it was wrong and how
  it was found belong in the commit message.
- Leave out the conditions, the defaults and the exceptions. Most people want
  to know what is different, not when it applies. The user guide has the rest.

A section is for a *release*, not for a commit. `version.h` gets a patch bump
on every commit, but only the version on `master` when CI runs gets a tag. A
section therefore covers all the commits after the previous tag.
`scripts/prep-release.ps1` collects those commits as a start. See
`docs/releasing.md`.

## 0.1.44

**New**

- A movement mode. The terrain palette becomes the movement values, and you
  paint them over any tile whatever is drawn there.
- Two more movement values: Bridge, which stops nothing, and Space, which stops
  everything.
- A Flying row beside the brush. It adds the bit that stops flying units to
  whatever you paint.
- The palette's last cell puts a tile back to what its terrain implies.

**Changed**

- Movement you paint by hand survives a terrain edit over the same tile. The
  editor rewrote it from the terrain.

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

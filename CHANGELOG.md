# Changelog

What changed in each release, newest first. CI reads the section for the
version in `version.h` and posts it as the GitHub release notes. What you write
here is what people read.

Write it in ASD-STE100 (Simplified Technical English), as a short list of what
is different for a user. Keep it condensed: one bullet for each change, and no
headings.

- One idea in each sentence. No more than 25 words.
- The active voice, the present tense, and the same word for the same thing.
- No metaphors, no jargon, and no words that only the code explains.
- Say what a person can now do, or which problem is corrected.

A section is for a *release*, not for a commit. `version.h` gets a patch bump
on every commit, but only the version on `master` when CI runs gets a tag. A
section therefore covers all the commits after the previous tag.
`scripts/prep-release.ps1` collects those commits as a start. See
`docs/releasing.md`.

## 0.1.31

- Ships and flying units now occupy 2 x 2 tiles, the area Warcraft II gives
  them. The editor gave them one tile, and let you put a ship in water that is
  too small.
- When you open a map, the editor counts the units that the game cannot place
  and offers to remove them. The default answer is No.
- The editor asks only about the rules your options keep. It accepts a start
  location below a town hall, a ship on an oil patch, and a ground unit on a
  Circle of Power. It does not accept a unit on a gold mine.
- The editor keeps the unit placement and terrain options after you close it.
  Each session started with the default values.
- Convert Units has a new option, **Only the selected units**. The option is
  off, and is unavailable if you select no units.
- A new option, **Offer unused and special units**, adds the unused units, the
  corpses, the rubble and the campaign workers to the palette and the menus.
  The option is off, because five of these units can stop the game.
- The Generate a Map window is larger. Its preview is 2.5 times as wide and as
  high.

## 0.1.26

- The first public release.

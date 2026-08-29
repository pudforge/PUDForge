# Changelog

What changed in each release, newest first. CI reads the section for the
version in `version.h` and posts it as the GitHub release notes. What you write
here is what people read.

Write it in ASD-STE100 (Simplified Technical English), as short bullet points
that tell a user what is different:

- One idea in each sentence. No more than 25 words.
- The active voice, the present tense, and the same word for the same thing.
- No metaphors, no jargon, and no words that only the code explains.
- Say what a person can now do, or which problem is corrected.

A section is for a *release*, not for a commit. `version.h` gets a patch bump
on every commit, but only the version on `master` when CI runs gets a tag. A
section therefore covers all the commits after the previous tag.
`scripts/prep-release.ps1` collects those commits as a start. See
`docs/releasing.md`.

## 0.1.30

**Units**

- Ships and flying units now occupy 2 x 2 tiles. This is the area that
  Warcraft II gives them. The editor gave them one tile.
- The editor now refuses to put a ship in water that is too small for it. It
  also refuses to put two ships in the same place.
- The map check finds ships and flying units in positions that the game cannot
  use.
- The Convert Units window has a new option: **Only the selected units**. The
  option is off. The editor makes the option unavailable if you select no
  units.

**Maps**

- When you open a map, the editor counts the units that the game cannot place.
  It shows the count and asks if you want to remove them. The answer is No
  until you change it.
- The editor asks only about the rules that your options keep. If you permit
  units on top of each other, the editor does not ask about them.
- The editor does not count these correct positions as a fault: a start
  location below a town hall, a ship on an oil patch, and a ground unit on a
  Circle of Power.
- The editor still counts a unit on a gold mine as a fault. No unit can stand
  on a gold mine.

**Options**

- The editor now keeps these options after you close it: **Allow placing units
  at illegal positions**, **Allow units on top of each other**, **Allow units
  on the map edge**, **Mark special units**, **Adjust surrounding terrain when
  editing**, **Adjust edges when pasting**, and **Keep units the terrain no
  longer supports**. The editor did not keep them, and each session started
  with the default values.
- A new option, **Offer unused and special units**, adds the unused units, the
  corpses, the rubble and the two campaign workers to the unit palette, the
  Units menu and the quick pick. The option is off. Five of these units can
  stop the game.

**Windows**

- The Generate a Map window is larger. The preview is 2.5 times as wide and as
  high, and it is easier to read while you move the sliders.

## 0.1.26

- The first public release.

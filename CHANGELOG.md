# Changelog

What changed in each released version, newest first. This is the source for
the GitHub release notes: CI reads the section matching `version.h` and posts
it verbatim, so what is written here is what people read.

`version.h` gets a patch bump on every commit, but only the version sitting on
`master` when CI runs is tagged and released. A section here is therefore keyed
to a *release* and covers every commit since the previous tag, not one commit
each. `scripts/prep-release.ps1` collects those commits as a starting point;
the words are still a person's job. See `docs/releasing.md`.

## 0.1.28

**Ships and flying units are 2x2.** They were one tile, because that is what
`UDTA`'s `unitSize` field says about every mobile unit in the game's own
defaults — which would fit a battleship in a one-tile pond. The game plainly
disagrees, and its data says so twice: `boxSize` gives all ten ships and all
six fliers 63–71 px against 31–42 for infantry, and every ship and flier in the
shipped maps sits on an even tile, which is the signature of a 2x2 unit
anchored at its top-left corner. Placing, overlap and validation all follow the
real size now, so a destroyer needs water it actually fits in. Ballista and
Catapult carry the same big artwork and stay one tile: they appear at both
parities, so they are land units that merely look large.

**Unit placement options are remembered.** "Allow placing units at illegal
positions", "Allow units on top of each other", "Allow units on the map edge"
and "Mark special units" were read out of the Options dialog and written
nowhere, so every session opened with the rules back on. All of them persist
now, along with the two terrain-fitting options and "Keep units the terrain no
longer supports".

**Opening a map offers to remove units the game cannot place.** A `.pud` may
hold a footman in the sea, two units on one tile or a building hanging off the
edge; other editors write them, and this one has always loaded them unchanged.
Now it says so once, with a count, and offers to take them away as a single
undo step. It asks only about the rules you are actually keeping — turn
stacking on and it stops asking about stacks — and the answer defaults to No.
Start locations under a town hall and oil wells on their patch are what the
game intends and are never counted.

**Convert Units can be narrowed to the selection.** A new "Only the selected
units" switch, off by default, because the whole map is what the tool is for.
Greyed out when nothing is selected.

**Unused and special units are available behind an option.** "Offer unused and
special units" in Options adds PUDDraft's *Unused/Special Units* set to the
palette, the Units menu and the quick pick: the slots the game has no unit for,
the runtime leftovers, and the two campaign workers. Off by default and marked
as risky, because placing one of the dead slots crashes the game. The two
wall-as-unit ids stay hidden either way — walls are terrain here, and a second
way to place one that does not auto-tile makes walls the wall tool cannot fix.

**A bigger Generate window.** The preview is what you actually read while
moving the sliders, and at 132 dlu square it was a thumbnail. It is 2.5x in
both directions now, with the sheet grown around it.

## 0.1.26

First public release.

# Hardcoded overrides

Facts about Warcraft II that are not in any file the game ships, and so have to
be written down by hand.

Everything else in the core is derived: parsed from a `.pud`, read out of an
MPQ, or measured across the 556 shipped maps. The tables here cannot be. They
live in one directory, apart from `constants.cpp`, so that hand-written
judgement is visible as hand-written judgement rather than mixed in with data
transcribed from the game.

| File | What it decides |
|---|---|
| `portrait_frames.cpp` | Which command-button icon belongs to which unit |
| `race_counterparts.cpp` | What a unit becomes when a base changes race |
| `named_heroes.cpp` | Which units are heroes, where the game's flag is wrong |
| `hidden_units.cpp` | Which units a palette keeps behind an opt-in |
| `unit_footprints.cpp` | How many tiles the ships and fliers cover, where `unitSize` is wrong |

## Rules for anything added here

**Say where it came from.** Every file opens with the evidence: a section of
the format, a count across the corpus, a PUDDraft form in `reference/dfm/`, or
an admission that it was read off the artwork by eye. A table whose provenance
is not written down cannot be checked later, and will be wrong eventually.

**Name the ids.** `kGryphonAviary`, not `0x46`. A number in a table here is a
number nobody will ever verify.

**Never let it govern the format.** These decide what an editor *offers* and
how it *groups* things. What a `.pud` may contain is decided by the parser, and
by nothing in this directory. A map holding something these tables hide must
still load, edit and save byte for byte — that is what keeps round-trips exact.

**Test it against something.** `portrait_frames.cpp` is anchored by four icons
read out of `UGRD`; `hidden_units.cpp` is checked against PUDDraft's own menu;
`race_counterparts.cpp` is checked against every pairing in the unit table. A
hardcoded list with no test is a guess with good posture.

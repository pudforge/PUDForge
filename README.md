# PUDForge

PUDForge is a Warcraft II map editor for Windows. It reads and writes `.pud` files for both the Battle.net edition and Warcraft II Remastered.

[Download the latest release](https://github.com/pudforge/PUDForge/releases/latest)
[Feature walkthrough](https://www.youtube.com/watch?v=nTLuCuUqlOg)

A browser or a scanner sometimes calls the download a virus. It is not one - an
unsigned release is a file nothing has seen before and nobody's name is on, and
some scanners guess at that. Releases are being signed by SignPath Foundation
to stop it, and each one publishes the SHA-256 of the file GitHub Actions built;
[`docs/releasing.md`](docs/releasing.md) has the detail.

## Documentation

- [`docs/user_guide.md`](docs/user_guide.md) - the editor: what is on screen,
  what the tools do, and the behaviours that are not obvious.
- [`CHANGELOG.md`](CHANGELOG.md) - what changed in each release. It is also
  what the GitHub release notes are made of.
- [`docs/releasing.md`](docs/releasing.md) - how a release is cut, which is a
  push to `master` with a new version in `version.h`.

## Building

Visual Studio 2022's C++ workload and CMake, from the repo root:

```powershell
cmake -S src -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PUDForgeWin -- -m
```

The result is `build/Release/PUDForge.exe`. It is a WIN32-subsystem
application, so a shell neither waits for it nor sees its exit code - check it
without opening a window by rendering a map, which exercises the artwork path
too:

```powershell
$p = Start-Process .\build\Release\PUDForge.exe -ArgumentList `
  '--render','map.pud','--out','shot.png' -NoNewWindow -Wait -PassThru; $p.ExitCode
```

The core and its tests build anywhere:

```sh
cmake -S src -B build && cmake --build build
ctest --test-dir build -j4
```

## Game data

The editor draws with the tilesets, sprites and portraits from a Warcraft II
install, which are not redistributable and are not in this repository. Either
Battle.net Edition or Warcraft II Remastered works.

**An install is required.** On its first run PUDForge lists the copies it found
and asks which to use; closing that window closes the editor. It searches the
INI beside the executable, the registry, the usual install folders, and the tree
the executable sits in - so dropping `PUDForge.exe` into the game folder is
enough, and Tools > Select Warcraft II Data changes the choice later.

## Tests

`ctest` runs the C++ suite. The tests that need real maps or artwork skip
cleanly when neither is present, so a fresh clone gets a meaningful run from
the five fixtures in `test/fixtures/` - one per tileset, and enough `OILM`
content for the trigger test.

```sh
./build/pf_tests . --group terrain --filter shore
```

## Layout

```
src/PUDForgeCore/   the core - every map rule, behind a C ABI
  include/pudforge/pudforge.h   the ABI itself
  overrides/                    hand-written judgement, one file per decision
src/PUDForgeWin/    the Win32 client, and the PNGs its resources embed
src/Tests/          the C++ suite - one file per subject area
data/               the retail UDTA/UGRD payloads default_data.cpp is built from
test/fixtures/      five community maps, so the suite means something without the corpus
scripts/            the art converters and help builder the build calls
```

## Credits, Acknowledgements & Inspiration

- **Daniel Lemberg**, **Simon Pelsser** and **Lasse Jensen** - Community driven PUD format spec.
- **Alexander Cech** - PUDDraft and WarDraft, PUD format spec.
- **Simon Pelsser** - War2Unit.
- **Scott Sipe** - UDTAed.
- **[Mistral](https://github.com/Mistral-war2ru)** - War2mod and the War2 Trigger Editor.
- **Ladislav Zezula** - StormLib.

Warcraft II and all related intellectual property are owned by Blizzard
Entertainment. This project is an unofficial fan-made tool and is not affiliated
with, endorsed by, or sponsored by Blizzard Entertainment. All rights reserved.

## Licence

MIT, with one exception it names: the PKWARE decoder in
`src/PUDForgeCore/mpq.cpp` is derived from Mark Adler's zlib-licensed `blast.c`
and carries its attribution in place.

No Warcraft II data or artwork is included in this repository.
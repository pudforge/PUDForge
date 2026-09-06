# The Remastered artwork module

Warcraft II Remastered ships its artwork as PNG atlases with TexturePacker
metadata beside them, where the Battle.net edition keeps `.grp` files in
archives. This is the plan for drawing that artwork in the editor, and the
record of what has been measured so nobody has to measure it twice.

**It is off.** Two gates, both default off:

| Gate | Where | What it does |
|---|---|---|
| `PF_ENABLE_HD_ART` | `src/CMakeLists.txt` | An off build carries none of the module and no setting for it. On by `-DPF_ENABLE_HD_ART=ON`. |
| `HdArtTilePx` | the settings table in `Editor.cpp` | `0`, `64` or `96` — off, or the resolution asked for. Clamped on restore. **Today any non-zero value imports at `PF_TILE_PX`**, because that is what the renderer composes at; the number starts meaning something when the renderer can compose bigger. |

The PNG decoder in `PUDForgeCore/png_decode.cpp` is deliberately *outside*
both. A core that writes PNGs and cannot read one is a gap anything importing
images hits, so it builds and is tested whatever happens to the rest.

## What was measured

Against the install at `x86\Data\Art\hd` and the 357-map corpus, September
2026. These are the numbers the plan rests on; re-measure rather than trust
them if the game has been patched since.

- **205 sprite frames over 77 sprites** is the whole working set, built and
  counted rather than estimated. 110 unit types collapse to 78 distinct
  sprites because several units draw one file — a Footman and an Attack
  Peasant are both `human/peon` — and each wants at most 5 facings. The
  atlases hold roughly thirty thousand frames, because the game animates walk,
  attack and death cycles and an editor draws idle poses.
- **2.9 GB** decoded if the atlases were simply loaded; **462 MB** on disk.
- **216 px** per HD terrain tile against 32 px classic, so 6.75x linear. The
  zoom ladder stops at 800%, which is 256 px — native is barely above our
  maximum and far above the 50-200% anyone edits at.
- **96 of 99** sprites hold exactly as many frames in HD as in the classic
  `.grp`, so the frame indices line up and frame 0 is still the south-facing
  idle. The three that differ are all `human/spear` — Archer, Ranger, Alleria
  — where classic holds 50 and HD holds 60.
- **5.7 s** for the whole unit import, measured, because only **5 sheets** of
  the 42 have to be decoded at all — the wanted sprites concentrate in the
  common and forest ones. Decoding every atlas would be ~20 s; there is no
  reason to. Peak stays one sheet: 235 MB, transient.
- **Masks are cheap.** `units_common_forest_masks.png` is 418 x 486, about a
  megabyte. Team colour costs almost nothing.
- **The renderer composes at `PF_TILE_PX` and blits sprites one pixel for
  one.** Zoom is applied afterwards, by the client stretching the finished
  bitmap — `art.hpp` says as much: 32 px "whatever the zoom is, so it is also
  what a sprite's pixels are measured in". A sprite cut for 64 px tiles is
  therefore drawn at twice the size rather than in twice the detail, which is
  exactly what it looked like the first time.
  **This is the finding that decides what the module is worth.** Higher
  resolution artwork cannot show any benefit until `pf_map_render` can compose
  at a larger tile, because everything above it stretches a 32 px render. That
  is a change to the renderer and to every caller that measures in
  `PF_TILE_PX`, and it is not small.
- **The HD terrain atlas skips the 16 blank megatiles.** Every tileset opens
  with 16 blanks, and every atlas holds exactly 16 fewer frames than its
  tileset has megatiles: forest 356 against 372, winter 363 against 379,
  wasteland 357 against 373, swamp 368 against 384. So a megatile's frame is
  its index less 16, which is `hd_terrain_frame`.
- **The artwork is 216 HD pixels to a tile, and the canvases are not.** The
  drawn content of eight sprites, classic against HD, averages 6.88x where the
  terrain's 216/32 is 6.75. The *canvases* disagree wildly — a Footman is 72 px
  of classic canvas against 608 of HD, which is 9.9x — because the atlas pads
  its frames and the `.grp` does not. Scaling a sprite to fit a box therefore
  lands it two thirds of the size it should be; scale by `kHdPixelsPerTile`
  instead. The spike found this by drawing it wrong first.

Cache size at rest. The unit column at 64 px is **measured** — a built cache,
written to bytes; the rest scale from it and from the tile counts:

| Tile px | Units | Terrain, 4 tilesets | Resident | Crisp to |
|---|---|---|---|---|
| 48 | 15 MB | 13 MB | 28 MB | 150% |
| **64** | **27 MB** | 22 MB | **49 MB** | 200% |
| 96 | 62 MB | 50 MB | 112 MB | 300% |
| 128 | 110 MB | 89 MB | 199 MB | 400% |

The earlier estimate of 69 MB at 64 px was pessimistic twice over: it costed
every unit as a 3x3 footprint, and it counted 330 frames where deduplicating
shared sprites gives 205.

## The shape

Nothing from the HD tree is read at draw time. On the first run after the
setting is turned on, an import walks the 330 frames, decodes each atlas once,
cuts and downscales what it needs, writes its own cache, and frees the atlas.
Peak is one atlas — 235 MB at worst, transient. After that only the cache is
read.

The join key already exists: `kUnitSprites` maps every unit id to
`"human/grunt"`, and the atlas frame keys are `grunt_0`, `grunt_1` and so on.

## Phases

Each ends somewhere shippable. Estimates are focused days.

- [x] **Decoder** — 2-3 d. PNG reading in the core: inflate with dynamic
      Huffman, the container, the five row filters. Colour type 8-bit only,
      interlaced refused. Tested by round trip through our own encoder and by
      the checked-in icon sheets, which other tools wrote and which therefore
      exercise the dynamic codes our encoder never emits.
- [x] **Gates** — the build flag, the setting, and both configurations
      building in CI's shape.
- [x] **Spike: does it look better?** — six units drawn from both sources at
      100% and 200%. **A wash at 100%, and clearly better at 200%.** Pixel art
      drawn for 32 px holds its own against art reduced 6.75x, so the gain is
      not at the zoom most maps are edited at; magnified 2x it is the classic
      art that falls apart, blocky and showing its dithered shadows, while the
      HD frames stay smooth. So the feature earns its place above 100% and not
      below, which is worth knowing before anyone tunes the default.
      `human/spear` is still unverified — see below.
- [x] **Atlas reader and import** — the sidecar reader, the wanted list, the
      frame cutter and the cache format, all in the core and tested. Run end
      to end against the install it produces a 27.4 MB cache of 205 frames in
      5.7 s, and reads it back. All 102 installed sidecars parse with no frame
      falling outside its sheet.
      One sprite has no HD frame at all: `other/vcircle`, the Circle of Power.
      It falls back to the classic art, which is what any unresolved sprite
      should do.
- [x] **Units on the canvas** — the client finds the install's `Art/hd`, loads
      the cache or imports it, and `GameData::OpenUnitSprite` answers from it.
      Verified against the running app: units draw from the Remastered artwork
      and 5.2% of the canvas changes with the setting on.
      The cache is cut at `PF_TILE_PX` rather than at the size asked for, for
      the reason above — so this is HD art at the size the renderer draws,
      which is correct but is not yet sharper.
- [x] **Render at a bigger tile** — `pf_render_options` carries a `tile_px`,
      and the renderer composes at it: terrain through `draw_megatile_at`,
      sprites scaled from the size they were drawn for, and every piece of
      layout arithmetic — grid, overlay tint, placeholder outlines — with it.
      **Additive, so no existing caller changed**: the field is zero in
      zero-initialised options and zero means `PF_TILE_PX`, exactly as before.
      A sprite now carries the tile size it was drawn for, so classic and
      Remastered artwork can be composed side by side at either size.
      The canvas composes at whatever the loaded tiles were cut for, so the
      cache is built at the size the setting asks for and the artwork is drawn
      at its own resolution rather than stretched from 32.
      The patch fast-path is skipped at any tile size but 32 rather than
      having its row arithmetic converted — correct, and a little slower.
- [x] **The import off the UI thread** — the work is split three ways so the
      worker shares nothing: `PrepareHdRequest` gathers what it needs on the
      thread that owns the archives, `BuildHdCache` is static and reads only
      files, and `AdoptHdCache` installs the result back on the drawing
      thread, reached by a posted message. The window is now up in **0.2 s**
      against 12-18 s before, drawing the game's own artwork until the
      Remastered art lands and replaces it.
      Not done: a progress indication, and cancelling. Neither can leave a
      half-written cache — the file is only written once the cache is whole —
      but a first import is still twenty silent seconds.
- [x] **Terrain** — the tiles are imported per tileset and drawn through
      `TilesetArt::use_hd_tiles`, an overlay rather than a second kind of
      artwork: the tileset still says which megatile a tile value means, still
      carries the palette a sprite is tinted through, and still supplies the
      minimap averages, because the HD sheets hold none of that. Verified in
      the running app — with terrain and units both on, 97.8% of the canvas
      changes. Animated water is still the classic cycling palette, which is
      the cut the plan expected.
      The half of the test that compares against the real tilesets skips on a
      machine with no extracted classic tree, which is every machine that only
      has Remastered — the counts were taken by hand instead.
- [x] **The setting, and the rebuild-on-patch path** — Options carries an
      Artwork group with one tick, and changing it reopens the artwork rather
      than waiting for a restart. A tick and not a size, because a size is a
      promise the renderer cannot keep yet. The cache is keyed by tile size
      and by a stamp taken from the largest atlas, so a patched game rebuilds
      rather than drawing last year's art.
- [x] **Command icons.** `HUD/Portrait-face` is the sheet after all. The
      first attempt read it as the wrong asset and backed out; what was
      actually wrong was the indexing. Its keys run `<tileset>_<n>` with all
      four tilesets in one file, and taking the frames in packed order walks
      out of the current tileset's run and into the next one — which is why
      the toolbar filled with faces and looked like the wrong file. Indexing
      by the number in the key fixes it, and has to: a unit's icon *is* a
      frame number, so a gap in the run stays an empty frame rather than
      shifting every icon after it. `HUD/Portrait-mask` carries the team
      colour, keyed the same way with a `_team` suffix.
      This is why `IconCache` holds a sheet per owner. One sheet served
      everybody while icons came from the game's own artwork, which is
      palette-indexed and tinted as it is drawn; the Remastered icons carry
      their colour in the pixels. The per-owner sheets are built lazily and
      only when the module is on — `GameData::icons_vary_by_owner()` is false
      otherwise, because asking per owner on the classic path decoded the same
      file once for every player on the map.
      Measured: about 1.35 MB per owner sheet, 10.8 MB across all eight.

## Decisions already taken

- **The decoder lives in the core**, not the client. Host-side would mean WIC,
  which is Win32, and would leave the cross-build and the tests without it.
- **The setting is a size, not a tick.** The user is trading sharpness against
  memory and a checkbox hides the trade.
- **Team colour comes from the mask atlases.** HD sprites are full colour
  where classic ones are palette-indexed and tinted per owner at draw time, so
  the cache holds a frame and its mask and the draw path tints through it.
- **A team pixel takes its brightness from the brightest channel**, not from
  luma. Luma was the first attempt and it blackened every team pixel, because
  the weights sum against a saturated hue — buildings came out as black boxes.
  The brightest channel has the property the rule needs: the player the
  artwork was already drawn for reproduces exactly.
- **Nothing is redistributed.** The artwork stays in the user's install and
  the cache is derived data under their profile, which is the same footing as
  the rest of the artwork the editor already requires.

## Known traps

- **Alpha fringing.** Downscaling straight-alpha art darkens every soft edge.
  Premultiply before scaling and un-premultiply after; `scripts/make-ui-icons.ps1`
  documents the same trap from the other direction.
- **A half-written cache.** The import must survive being cancelled without
  leaving one, or the next run draws holes.
- **A patched game.** The cache key carries the build. Any frame the import
  cannot resolve falls back to the classic sprite the editor already draws.
- **`human/spear` holds 60 HD frames against 50 classic**, the only sprite of
  99 where the counts disagree. The editor draws frames 0-4, so appended
  frames would not matter — but which end they were appended to has not been
  checked, and the import should refuse a sprite whose counts disagree rather
  than draw the wrong pose.

// Editor state: tools, selection, and when to take undo checkpoints.
//
// A port of PUDForgeWeb's editor.mjs, and deliberately Win32-free for the same
// reason that file is DOM-free: the window layer translates events into these
// calls, and the tests drive the same calls with no window at all. Anything
// that is a rule about the *map* lives in the core.
//
// Compiled and tested on any platform, which is what lets half of the Windows
// client be verified on a machine with no MSVC.

#pragma once

#include <set>
#include <string>
#include <vector>

#include "pudforge/pudforge.h"

namespace pfwin {

/// The halves of map making, which barely overlap: you are shaping terrain,
/// placing units, or saying where things may walk. The mode decides what the
/// palette shows.
enum class Mode { kTerrain, kUnit, kMovement };

/// What a click on the map does. Tools are grouped under the modes.
enum class Tool {
  kPaint,    ///< terrain mode: paint with the current brush
  kRect,     ///< terrain mode: drag out a rectangle to fill, copy or clear
  kSelect,   ///< unit mode: click or band-select units
  kPlace,    ///< unit mode: place the current unit
  kErase,    ///< unit mode: click to delete
  kWalkable, ///< movement mode: paint a movement class over whatever is drawn
};

/// A tile rectangle. `w == 0` means "none" wherever one is optional.
struct TileRect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

class Editor {
 public:
  /// Borrows `map`; the caller owns it and keeps it alive.
  explicit Editor(pf_map* map);
  /// Frees the clipboard fragment, which is the one thing the editor owns.
  ~Editor();
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;

  void SetMap(pf_map* map);
  pf_map* map() const { return map_; }

  // ------------------------------------------------------------------ mode
  Mode mode() const { return mode_; }
  Tool tool() const { return tool_; }
  /// Which mode a tool belongs to. One place, so a tool added to one of them
  /// cannot be sorted differently by the two functions that ask.
  static Mode ModeOfTool(Tool tool);
  /// Switch mode, adopting its default tool unless the current one fits.
  void SetMode(Mode mode);
  /// Set the tool, switching mode when it belongs to the other one.
  void SetTool(Tool tool);
  /// The tool Escape or a right-click on the canvas backs out to.
  ///
  /// Painting is the tool a stray click costs an undo step on. It backs out
  /// *within* the mode: Escape while painting is not a request to edit units.
  /// @return the tool to switch to, or the current one when there is nothing
  ///         to back out of
  Tool ToolAfterCancel() const;

  // ----------------------------------------------------------------- brush
  /// Index into the core's brush list (pf_brush_terrain). The custom brush is
  /// index pf_brush_count(): one picked tile value, painted raw.
  int brush_index = 2;                  ///< grass, matching the web default
  int custom_tile = -1;                 ///< tile the custom brush paints
  int brush_size = 1;                   ///< tiles, one of pf_brush_size
  /// The bottom rung lays one corner of the grid rather than any whole tile,
  /// so it is aimed and previewed differently from every size above it.
  bool BrushIsCorner() const { return brush_size == PF_BRUSH_SIZE_CORNER; }

  /// The tiles the last edit touched, or empty when it was not bounded.
  ///
  /// Only painting fills this in; everything else leaves it empty, which is the
  /// honest answer — the canvas then recomposes the lot rather than guessing.
  const TileRect& touched() const { return touched_; }
  void ClearTouched() { touched_ = {}; }

  /// Move one rung up or down the brush-size ladder, and stop at its ends.
  ///
  /// Steps by index into the core's list rather than by tile, so a wheel notch
  /// is one rung and the same gesture covers 1 to 32 in a handful of turns.
  /// Clamped rather than wrapped: rolling past the top and landing back on a
  /// single tile is a way to paint something you did not mean to.
  /// @return the new size, or -1 when it was already at the end. Not 0: the
  ///         bottom rung *is* 0, so "did not move" needs a number no rung uses.
  int StepBrushSize(int dir);
  int brush_shape = PF_BRUSH_SQUARE;    ///< pf_brush_shape, or kShapeFill
  /// Fill is a shape in the sense that matters to the UI — "what does a click
  /// cover" — but it is not a pf_brush_shape, so it gets the next number.
  static constexpr int kShapeFill = PF_BRUSH_SCATTER + 1;

  /// Mix is the third shade, beside Light and Dark.
  ///
  /// Painting one flat shade leaves a map flat where a shipped one is mottled:
  /// across Blizzard's 35 forest maps 28.4% of shaded terrain is the dark member
  /// of its pair, and a map painted in one shade is 0%. Each tile takes
  /// whichever of the pair the core's noise gives it, as the stroke lays it.
  ///
  /// Asking per tile rather than re-shading the stroke's bounding box afterwards
  /// means nothing outside the stroke can be reached and no settled tile is
  /// picked twice.
  ///
  /// Exclusive with `paint_dark` in the panel: the three are one question.
  bool mix_shades = false;

  /// Whether the brush lays the dark drawing of a terrain that has two.
  ///
  /// One cell per terrain and a switch, as PUDDraft had it. Carrying both as
  /// separate cells meant ten brushes for seven terrains, with the pairs sitting
  /// next to each other looking like different things.
  bool paint_dark = false;
  /// Shift, held, borrows the other shade for as long as it is down. Kept
  /// apart from `paint_dark` so letting go puts the switch back rather than
  /// leaving it where the last stroke happened to need it.
  bool shade_flipped = false;
  bool DarkWanted() const { return paint_dark != shade_flipped; }

  int TerrainOfBrush() const;
  bool BrushIsCustom() const { return brush_index >= pf_brush_count(); }
  /// Adopt a brush, from the palette or the ring.
  ///
  /// The palette only offers the light member of each pair, so a dark brush —
  /// which the ring and a restored setting can both still name — becomes the
  /// light one with the shade switch on. Otherwise the palette would have no
  /// cell to show as selected.
  void SetBrush(int index);
  /// What the status bar should say about the brush now selected, in UTF-8.
  /// Here rather than in the panel because two places ask: the palette when it
  /// is clicked, and the canvas when the eyedropper picks.
  std::string BrushName() const;

  // ------------------------------------------------------------------ unit
  int placing_type = 0;
  int placing_owner = 0;

  // ----------------------------------------------------------------- view
  int unit_filter = PF_UNITS_ALL;       ///< pf_unit_filter
  int overlay = PF_OVERLAY_NONE;        ///< pf_overlay
  bool show_grid = false;

  // --------------------------------------------------------------- options
  /// Defaults match PUDDraft's: fitting on, illegal placement off.
  bool auto_fit_edges = true;
  /// Painting terrain removes units it strands, unless set.
  bool keep_stranded_units = false;
  bool mark_special_units = false;

  /// Offer every race's units in the palette, whatever the chosen player is.
  ///
  /// Off by default, and off is the honest default: of 33,838 units under a
  /// human or orc player across 556 shipped maps, exactly 71 sit under a player
  /// of the other race and every one is a named hero. A footman under an orc
  /// player is not a thing mappers make; it is a thing they do by accident.
  ///
  /// Off, the palette shows the chosen player's race plus what belongs to nobody
  /// in particular, and handing a unit across converts it. On, nothing is
  /// filtered and nothing is converted. See OffersUnit and TypeForOwner.
  bool show_all_races = false;

  /// Offer the units an editor normally keeps back: the five slots the game has
  /// no unit for, the runtime leftovers — corpses, rubble — and the two
  /// campaign workers. The membership is PUDDraft's "Unused/Special Units"
  /// submenu; see `overrides/hidden_units.cpp` for what is in it and why.
  ///
  /// Off by default, because placing one of the dead slots crashes the game.
  /// Opt-in rather than forbidden, because people really do build maps with
  /// these — and this governs only what the palette and the Units menu offer,
  /// never what a file may hold. A map that already contains one has always
  /// loaded, edited and saved unchanged whatever this says.
  ///
  /// The two wall-as-unit ids stay hidden even with this on. Walls are terrain
  /// in this editor, so a second way to place one that does not auto-tile makes
  /// walls the wall tool cannot fix — which is a bug, not a choice.
  bool offer_unused_units = false;

  /// The three escape hatches from the placement rules.
  ///
  /// Accessors rather than fields, and the setters push straight down: the rules
  /// are the core's, because paste has to obey them too and never comes through
  /// this class. A public bool would be a second copy of state that only took
  /// effect if somebody remembered to call something — the drift that let paste
  /// stack units in the first place.
  ///
  /// Kept here as well as on the map so a map swap can re-apply them.
  bool allow_illegal_placement() const { return allow_illegal_placement_; }
  /// Stacking is legal in the format, but placing a second unit on top of one
  /// you cannot see is almost always a slip.
  bool allow_stacked_units() const { return allow_stacked_units_; }
  /// The shipped maps disagree — 2,939 retail units sit on the outer ring —
  /// so this guards against accident rather than enforcing the format.
  bool allow_edge_placement() const { return allow_edge_placement_; }
  void SetAllowIllegalPlacement(bool on) {
    allow_illegal_placement_ = on;
    ApplyPlacementOption();
  }
  void SetAllowStackedUnits(bool on) {
    allow_stacked_units_ = on;
    ApplyPlacementOption();
  }
  void SetAllowEdgePlacement(bool on) {
    allow_edge_placement_ = on;
    ApplyPlacementOption();
  }

  /// Which drawings of a terrain painting may use; PF_VARIATION_PLAIN by
  /// default — a whole map painted with rocks in it looks like a mistake.
  void SetVariationPolicy(int policy);
  int variation_policy() const { return variation_policy_; }

  /// Push the placement option down to the core, which enforces it.
  void ApplyPlacementOption();

  // ------------------------------------------------------- saved options
  /// One preference that outlives a session: the key it is stored under, what
  /// a fresh install gets, and the two halves of reaching it.
  ///
  /// Capture-less lambdas, so this is a plain table of function pointers and
  /// the host needs nothing from us but a loop.
  struct Option {
    /// Never renamed. The name in a user's settings is the only link between
    /// what they chose last time and what they get this time.
    const char* name;
    int fallback;
    int (*get)(const Editor&);
    void (*set)(Editor&, int);
  };

  /// Every option the host should save and restore, in one place.
  ///
  /// A table rather than a line each at two call sites in the window: the four
  /// unit-placement options shipped unsaved because adding an option and
  /// remembering to persist it were separate edits, the second in a file no
  /// test can reach. `saved_options_round_trip` reaches this one.
  static const std::vector<Option>& SavedOptions();

  /// Mirror axes (pf_mirror flags). They combine; kNone clears them all.
  int mirrors = PF_MIRROR_NONE;
  /// Turn one mirror on or off; PF_MIRROR_NONE clears them all.
  int ToggleMirror(int flag);

  // ------------------------------------------------------------- undo/redo
  /// Snapshot before a change, unless one has already been taken for the action
  /// in progress — a mirrored placement is several calls and must still be one
  /// step in the history.
  void Checkpoint();
  bool CanUndo() const { return map_ && pf_map_can_undo(map_) != 0; }
  bool CanRedo() const { return map_ && pf_map_can_redo(map_) != 0; }
  bool Undo();
  bool Redo();

  // ------------------------------------------------------------- painting
  /// One stroke, one undo step: begin on button-down, end on button-up.
  void BeginStroke();
  bool StrokeActive() const { return stroke_; }
  /// Paint at a tile with the current brush, mirrored under the symmetry.
  /// Must sit between BeginStroke and EndStroke.
  bool PaintAt(int x, int y);
  /// Paint one corner of the grid, mirrored under the symmetry.
  ///
  /// Corner coordinates, not tile ones — see pf_map_paint_corner. Used instead
  /// of PaintAt when the size is on the bottom rung, because a corner is not a
  /// tile and rounding it to one is what makes a half-tile mark impossible.
  /// Shape is ignored: a single corner has nothing to scatter or round off.
  bool PaintCornerAt(int cx, int cy);
  /// One puff of a held scatter brush, at the density the press built up to.
  bool SprayAt(int x, int y, int held_ms);
  /// Ends the stroke: drops stranded units, rebuilds regions.
  /// @return units removed for standing on terrain that no longer holds them
  int EndStroke();

  /// Flood the region under a tile with the current brush, bounded by the
  /// terrain rectangle when there is one.
  bool FillAt(int x, int y);

  /// Repaint every tile of the clicked terrain, not just the region touching
  /// it — the bucket held down, which is what Shift means here.
  ///
  /// Scoped like the other bulk edits: the terrain selection when there is one,
  /// the whole map when there is not.
  /// @return tiles changed
  int FillTerrainEverywhere(int x, int y);

  /// Adopt the terrain under a tile as the current brush.
  ///
  /// When no brush lays that terrain — a decorated or wall-detail group the
  /// corner model never produces on its own — the custom brush adopts the exact
  /// tile instead. False when there is nothing under the pointer.
  bool PickBrush(int x, int y);

  /// The unit-mode eyedropper: adopt the type and owner of the unit under a
  /// tile as what the next placement will be.
  ///
  /// The same act as PickBrush, and worth having for the same reason: finding a
  /// unit again in a palette of a hundred and ten is slower than pointing at it.
  /// @return the unit's index, or -1 when nothing is there
  int PickUnitType(int x, int y);

  /// The same, aimed at a unit already found rather than at a tile.
  ///
  /// What Duplicate is: the selection says which unit, so there is no tile to
  /// hit-test and no way for the answer to be a different unit.
  /// @return `index` when it named a unit, or -1
  int PickUnitTypeOf(int index);

  bool InBounds(int x, int y) const;

  // ------------------------------------------------------------ selection
  const std::set<int>& selected() const { return selected_; }
  bool HasSelection() const { return !selected_.empty(); }
  /// The single selected unit index, or -1 when zero or several.
  int SelectedUnit() const;
  /// Select the unit under a tile; `additive` toggles instead of replacing.
  /// @return the unit's index, or -1
  int SelectAt(int x, int y, bool additive);
  /// Every unit whose footprint overlaps the rectangle.
  int SelectInRect(const TileRect& rect, bool additive);
  int SelectAll();
  int InvertSelection();
  /// Every unit in a pf_unit_group.
  int SelectGroup(int group, bool additive);
  int SelectOwner(int owner, bool additive);
  /// Every unit sharing a type (or an owner) with the current selection.
  int SelectSameType();
  int SelectSameOwner();
  void ClearSelection() { selected_.clear(); }

  // ---------------------------------------------------------------- units
  /// Why a unit of `type` cannot stand at (x, y), or empty when it can. Asked
  /// before editing, so a refusal costs no undo step. `ignore` holds indices
  /// that do not count as in the way — a unit being moved must not block itself.
  std::string PlacementRefusal(int x, int y, int type,
                               const std::vector<int>& ignore = {}) const;

  /// Whether a pasted unit of `type` would actually land at (x, y).
  ///
  /// The same rule as placing by hand, because paste runs the same check — it
  /// did not always, which is how a fragment came to drop units on top of units.
  /// Nothing is being moved here, so nothing is ignored.
  ///
  /// A fragment carries terrain or units and never both, so the terrain a pasted
  /// unit lands on is the terrain that is there now.
  bool PasteWouldPlace(int x, int y, int type) const;

  /// Top-left tile for a unit whose centre should sit under the cursor.
  void PlaceOrigin(int x, int y, int type, int& ox, int& oy) const;

  /// Place the current unit and its reflections, as one undo step. The
  /// reflections are best-effort; the return value is what the hand asked for.
  /// @return the placed unit's index, or -1 with last_refusal set
  int PlaceUnit(int x, int y);

  /// A drag that lays units, from button-down to button-up.
  ///
  /// A line of trees is a drag, and one that costs thirty undo steps is one
  /// nobody uses twice, so the run is a single checkpoint however many land.
  ///
  /// Refusals are counted rather than spoken: dragging a footman across a
  /// coastline refuses at every water tile.
  void BeginPlacementRun();
  bool PlacementRunActive() const { return placing_run_; }
  /// Ends the run. @return how many units it placed
  int EndPlacementRun();
  /// How many placements the run turned down. Meaningful after EndPlacementRun.
  int run_refused() const { return run_refused_; }

  /// Leave unit placement: Escape and the right button both mean "stop arming
  /// the next click", and selecting is what a map maker wants next.
  ///
  /// Here rather than in the window because an open placement drag has to be
  /// closed with it — the right button arrives while the left may still be down,
  /// and a run left open would let the next placement join a finished gesture's
  /// undo step.
  /// @return true when the tool changed, so the caller knows to say so
  bool LeavePlacement();

  /// Shift the whole selection by a tile delta, all or nothing, so a group
  /// shoved against a coastline does not smear along it. `checkpoint` is the
  /// caller's to decide: a drag is one edit made of many of these.
  bool MoveSelectionBy(int dx, int dy, bool checkpoint);

  /// Delete the whole selection as one undo step.
  bool DeleteSelected();
  /// Delete the unit under a tile.
  bool EraseAt(int x, int y);
  /// Reassign every selected unit to another player slot.
  ///
  /// With `show_all_races` off this can change *what* the units are as well as
  /// whose (see TypeForOwner), and a conversion is a remove and an add, so the
  /// selection is re-found by position afterwards rather than kept by index.
  bool SetSelectedOwner(int owner);

  /// Give one unit to a player and set the single number the format keeps for
  /// it, as one edit. The unit inspector's OK, in other words.
  ///
  /// Here rather than in the dialog so the counterpart rule is written once:
  /// this is the other way a unit changes hands, and the two disagreeing is how
  /// a Footman ends up under an orc player.
  /// @return the unit's index afterwards, which a conversion moves, or -1
  int SetUnitOwnerAndValue(int index, int owner, int value);

  /// Which misplacement checks the current options are asking for, as
  /// pf_misplaced flags.
  ///
  /// Off the map is always one of them: a unit outside its own map is broken
  /// however the options are set. The other two are the escape hatches read
  /// back — somebody who has turned stacking on is not to be asked whether to
  /// delete the units they stacked on purpose.
  int MisplacementChecks() const;

  /// How many units those checks find. Nothing is changed.
  int MisplacedUnitCount() const;

  /// Delete them, as one undo step. Returns how many went.
  int RemoveMisplacedUnits(int checks);

  /// Whether a palette should list this type at all, before any player filter.
  ///
  /// The catalogue question, where OffersUnit is the chosen-player one; a
  /// palette asks both, and the quick pick asks this one alone.
  ///
  /// Static, and `with_unused` is passed rather than read off the instance,
  /// because the quick pick has no Editor to ask and answering the question
  /// twice in two places is how the grid and the search end up disagreeing
  /// about what exists.
  static bool ListsUnit(int type, bool with_unused);

  /// Whether the units palette should offer this type for the chosen player.
  ///
  /// True for everything with `show_all_races` on. Otherwise neutral units and
  /// heroes always, anything else only when its race is the chosen player's —
  /// which takes the start-location markers with it. A player slot with no race
  /// is not a filter anybody could act on, so it offers everything.
  bool OffersUnit(int type) const;

  /// What a unit of `type` becomes when handed to `owner`, or `type` itself.
  ///
  /// Only ever different with `show_all_races` off. Heroes have no opposite
  /// number and stay as they are; so does anything whose counterpart could not
  /// stand where it is, because losing the unit is worse than a mismatched one.
  int TypeForOwner(int type, int owner, int x, int y) const;

  /// Re-arm the placement tool after the chosen player may have changed.
  ///
  /// With the filter on, choosing an orc player while holding a Footman takes it
  /// out of the palette, and leaving it armed means the palette shows nothing
  /// selected while a click still lays a Footman for an orc player.
  /// @return whether `placing_type` moved
  bool RetargetPlacingType();

  /// Bounding box of the selection in tiles. Empty when nothing is selected.
  TileRect SelectionBounds() const;

  // ---------------------------------------------------- terrain selection
  //
  // A persistent selection of tiles, separate from the unit selection because
  // the two are edited independently — PUDDraft kept two for the same reason.
  //
  // A region rather than a rectangle: one drag still makes a rectangle, shift
  // adds another and alt takes one away, so an L round a base is two drags
  // rather than impossible. The cost is that "the selection" and "the box round
  // it" are two questions, and whatever can only work on a box says so.

  /// How a drag combines with what is already selected.
  enum class Pick { kReplace, kAdd, kSubtract };

  /// The box around everything selected. Empty when nothing is.
  const TileRect& terrain_selection() const { return terrain_selection_; }
  /// Whether this particular tile is in the selection.
  bool TerrainSelected(int x, int y) const;
  /// How many tiles are selected, which is what the status bar counts: a
  /// region's area is not its box's, and the difference is the whole point.
  int terrain_selected_count() const { return terrain_selected_count_; }
  /// Whether the selection is exactly its own box, so a caller that can only
  /// act on a rectangle knows whether it is about to act on more than was
  /// asked for.
  bool TerrainSelectionIsRect() const {
    return terrain_selected_count_ == terrain_selection_.w * terrain_selection_.h;
  }

  /// Take a dragged rectangle into the selection, however `how` says.
  void SelectTerrain(int x, int y, int w, int h, Pick how = Pick::kReplace);

  /// The mask itself, for a caller that has to put it back.
  ///
  /// A drag is live, and adding to a selection means adding to what was there
  /// *before the drag*, not to what the last mouse move left — so the canvas
  /// keeps a copy from button-down and restores it each move. Nothing else has
  /// any business with these two.
  const std::vector<uint8_t>& TerrainMask() const { return terrain_mask_; }
  void SetTerrainMask(const std::vector<uint8_t>& mask) {
    terrain_mask_ = mask;
    RebuildTerrainBounds();
  }
  void SelectAllTerrain();
  void ClearTerrainSelection();
  /// Fill every selected tile with the current brush, as one undo step.
  int FillTerrainSelection();

  // ------------------------------------------------------ acting in bulk
  //
  // PUDDraft's whole-map edits, and the web client's Tools menu. Each decides
  // what it would do before doing any of it, so an operation that changes
  // nothing costs no undo step.
  //
  // All of them respect the terrain rectangle when there is one: "the
  // selection, or the map" is the same rule the fill and the shade pass use.

  /// What a bulk edit did, so a caller can say so without counting again.
  struct BulkResult {
    int changed = 0;   ///< tiles painted, or units replaced
    int skipped = 0;   ///< units that could not stand as their new type
    int kept = 0;      ///< units deliberately left alone (heroes, mostly)
    int removed = 0;   ///< units the terrain change stranded and took away
  };

  /// Repaint every tile whose dominant terrain is `from` as `to`.
  ///
  /// Several passes, because painting one tile refits its neighbours and can
  /// undo a tile painted a moment ago; it settles within a few, and the cap
  /// stops a pair of terrains that cannot coexist from spinning.
  int ReplaceTerrain(int from, int to);
  /// How many tiles in scope have this dominant terrain. Asked before
  /// replacing so the sheet can say what it is about to touch.
  int CountTerrain(int terrain) const;

  /// Scatter a terrain across the scope as isolated single tiles — PUDDraft's
  /// Decorate Terrain, for breaking up a flat expanse with trees or rock.
  /// Deterministic from `seed`: a decoration you cannot reproduce is one you
  /// cannot review.
  ///
  /// What it scatters is mostly forest and rock, which nothing can walk on, so
  /// it strands units the way a brush stroke does and clears up after itself
  /// the same way — `keep_stranded_units` included. `removed` counts them,
  /// because a scatter that quietly deletes a base is worse than one that
  /// leaves a footman standing in a tree.
  /// @param density fraction of tiles to seed, 0..1
  BulkResult DecorateTerrain(int terrain, double density, uint32_t seed);

  /// Turn units of one type into another, keeping position, owner and value.
  /// Units that could not stand as the new type are left alone.
  ///
  /// `selected_only` narrows it to the current selection — off by default in
  /// the dialog, because the whole map is what the tool is for and a scope
  /// that follows an invisible selection is how you convert three units and
  /// wonder where the rest went. Passed rather than defaulted, so the caller
  /// has to have decided.
  BulkResult ReplaceUnitType(int from, int to, bool selected_only);

  /// How many units of a type the map holds, or the selection does. What the
  /// convert dialog says before the press, so the count and the conversion
  /// cannot disagree about what is in scope.
  int CountUnitsOfType(int type, bool selected_only) const;

  /// Swap a player to the other race: every unit becomes its opposite number
  /// and `SIDE` follows, since the two disagreeing is what makes a base
  /// unbuildable rather than merely odd. Heroes and unpaired units stay.
  BulkResult SwitchPlayerRace(int owner, int race);

  // ---------------------------------------------------------- movement
  //
  // `MOVE` is written by painting, so almost every tile agrees with the tile
  // under it. The interesting ones are the handful that do not: a bridge a
  // mapper made walkable, a shallow a mapper closed. Those are what these two
  // are about — counting them, and putting them back.

  /// Tiles whose stored movement disagrees with the tile beneath them, in the
  /// terrain rectangle or the whole map.
  int MovementOverrides() const;
  /// Put the scope's movement back to what its terrain implies, as one undo
  /// step. @return tiles changed
  int ResetMovement();

  /// The value the movement brush lays.
  ///
  /// One number rather than a class and a set of modifiers beside it. A class
  /// in the palette is a value; a bit toggled on is that value with a bit
  /// changed; and a value no class has a name for is still a value. Two pieces
  /// of state for one word is how a "no flying" switch and a palette cell come
  /// to disagree about what the next stroke writes.
  int movement_value = 0x0001;

  /// Take whatever the terrain under each tile implies instead, which is how a
  /// single override comes off where Reset Movement does the lot. Not a value,
  /// so it is not `movement_value`.
  bool movement_from_terrain = false;

  /// Adopt the movement value already on a tile, which is the eyedropper the
  /// terrain brush has. Reading a value off the map is how you find out what a
  /// map you did not write says, and then paint more of it.
  /// @return false outside the map
  bool PickMovement(int x, int y);

  /// Which palette cell to show as chosen: the class whose value this is, or
  /// -1 when the bits have been taken somewhere no class has a name for.
  int MovementClassIndex() const;

  /// The movement brush's reach in tiles. The corner rung is a mark on an
  /// intersection, and this layer holds one value per tile, so it rounds up to
  /// a tile rather than painting nothing.
  int MovementBrushSize() const;

  /// The value the movement brush would lay, or -1 for "match the terrain".
  int MovementBrushValue() const;

  /// Paint the movement class over a tile and the rest of the brush, mirrored
  /// under the symmetry. What is drawn there is not touched: this layer says
  /// where things may walk, and the two disagreeing is the point of the tool.
  ///
  /// Must sit between BeginStroke and EndStroke, like painting terrain.
  /// @return tiles whose value changed
  int PaintMovementAt(int x, int y);

  // ------------------------------------------------------------ clipboard
  //
  // The fragment holds corner terrains rather than tile values, so it can be
  // rotated and mirrored without the artwork facing the wrong way, and it
  // outlives the map it came from — pasting into a different map is the same
  // call. The core owns all of that; what lives here is *what the user meant*:
  // which rectangle, and whether a paste is pending.

  /// What a copy captures. Terrain and units are never both taken: a fragment
  /// that carries both drops both, so pasting a formation onto ground you
  /// wanted to keep repaints it. One or the other, and the mode decides which
  /// unless a menu item says otherwise.
  enum class Grab { kByMode, kTerrain, kUnits };

  /// Copy a rectangle. With no rectangle given, the terrain selection is used,
  /// then the unit selection's bounds.
  /// @return units captured, or -1 when there was nothing to copy
  int Copy(Grab what = Grab::kByMode, const TileRect& rect = {});

  /// Which of the copy box's tiles the selection actually asked for, or empty
  /// when it asked for all of them.
  std::vector<uint8_t> CopyMaskFor(const TileRect& box,
                                   const TileRect& explicit_rect) const;
  /// Copy the units inside the rectangle, then delete them.
  ///
  /// Units only, whatever the mode: there is no "empty" terrain to leave behind
  /// and blanking it to water would be a guess about what the author wanted.
  /// Putting terrain on the clipboard too made a cut and a copy paste
  /// differently for no reason anybody could see.
  int Cut(const TileRect& rect = {});
  bool HasClipboard() const { return clipboard_ != nullptr; }
  /// Whether the fragment carries terrain, so a caller can say which kind of
  /// paste is armed without asking the core about corners.
  bool ClipboardHasTerrain() const;
  /// Bumped whenever the fragment is replaced or turned, so a view that
  /// rasterises it for a preview knows when its picture is stale. The pointer
  /// alone will not do: a freed fragment and its replacement can share an
  /// address.
  int clipboard_revision() const { return clipboard_revision_; }
  /// Fragment size in tiles, so a preview can be drawn before committing.
  TileRect ClipboardBounds() const;
  const pf_clipboard* clipboard() const { return clipboard_; }

  /// Paste with the fragment's top-left at a tile, as one undo step.
  /// @return units placed, or -1 with last_refusal set
  int PasteAt(int x, int y);
  /// Mirror, flip or rotate the pending fragment. Nothing is written to the
  /// map until it is pasted, so this is free to try.
  bool FlipClipboard();
  bool MirrorClipboard();
  bool RotateClipboard(int quarter_turns);

  /// Whether a paste is armed: the next canvas click drops the fragment.
  bool pasting() const { return pasting_; }
  /// Arm a paste. Drops the unit selection: the next click drops a fragment
  /// rather than acting on whatever was selected, and leaving marks around
  /// units the click will not touch says the opposite.
  void BeginPaste() {
    pasting_ = clipboard_ != nullptr;
    if (pasting_) ClearSelection();
  }
  void CancelPaste() { pasting_ = false; }

  /// Whether a paste blends its seam into the surrounding terrain. PUDDraft
  /// offered the same choice, and both answers are right for different work:
  /// off to move a fragment intact, on to graft it into a coastline.
  bool fit_pasted_edges = true;

  // ---------------------------------------------------------------- state
  /// Last thing that stopped an action, for the status line. Empty when the
  /// last action succeeded.
  std::string last_refusal;

  /// Bumped whenever the map changes, so views know to redraw and the title
  /// bar knows the file is dirty.
  int revision() const { return revision_; }
  /// Note that something outside the editor changed the map — a property
  /// sheet, say — so the views and the dirty marker catch up.
  void MarkMapChanged() { Bump(); }
  /// The revision the file on disk has, set after open and save.
  void MarkClean() { clean_revision_ = revision_; }
  bool Dirty() const { return revision_ != clean_revision_; }

 private:
  /// The race half of TypeForOwner, with no opinion about where the unit is.
  /// Separate because re-arming the palette has no position to check against.
  int CounterpartFor(int type, int owner) const;
  /// Every position a point maps to under the current symmetry, itself first.
  std::vector<int> SymmetryPoints(int x, int y, int w = 1, int h = 1) const;
  /// The tiles the current brush covers at a point, as x,y pairs. Empty for a
  /// square brush, which the core paints from a size instead.
  std::vector<int> BrushPoints(int x, int y, int shape, double density);
  /// Paint one brush application (all shapes), mirrored. Walls included.
  bool PaintBrushAt(int x, int y, double density);
  bool PaintOne(int x, int y, int size);
  /// Record a brush footprint as part of the current stroke.
  void MarkPainted(int x, int y, int size);
  /// Which drawing of a terrain to lay at a tile: the one asked for, or the
  /// mixture's answer for that tile when Mix is the shade in hand.
  int ShadedTerrain(int terrain, int x, int y) const;
  /// Which rectangle a copy or cut means, given what is selected.
  TileRect CopyBounds(const TileRect& rect) const;
  /// What a bulk edit acts on: the terrain selection's box, or the whole map.
  /// Paired with InBulkScope, which is what says whether a tile inside that
  /// box is really in the selection.
  TileRect BulkScope() const;
  /// Whether a tile is one a bulk edit should touch. True everywhere when
  /// nothing is selected, because then the scope is the map.
  bool InBulkScope(int x, int y) const;
  /// Recompute the box and the count from the mask, after it has been edited.
  void RebuildTerrainBounds();
  /// Indices of units the map cannot currently hold where they stand.
  std::set<int> StrandedNow() const;
  /// Drop units this stroke stranded — failing now but not at BeginStroke.
  int RemoveStrandedUnits();
  int PlaceOneUnit(int x, int y);
  bool allow_illegal_placement_ = false;
  bool allow_stacked_units_ = false;
  bool allow_edge_placement_ = false;
  void AfterHistoryStep();
  void Bump() { revision_++; }

  pf_map* map_ = nullptr;
  Mode mode_ = Mode::kTerrain;
  Tool tool_ = Tool::kPaint;
  int variation_policy_ = PF_VARIATION_PLAIN;
  std::set<int> selected_;
  /// Units already failing the placement check when the stroke began. A map's
  /// existing content is not this editor's to police: only what a stroke
  /// itself strands may be removed by it.
  std::set<int> pre_stranded_;
  /// The box around the selection, and which tiles inside it are really in it.
  ///
  /// One byte a tile, 16 KB at the largest map size, which is nothing next to
  /// answering "is this tile selected" without walking a list of rectangles.
  /// Empty when nothing is selected, so the common case costs nothing.
  TileRect terrain_selection_;
  std::vector<uint8_t> terrain_mask_;
  int terrain_selected_count_ = 0;
  pf_clipboard* clipboard_ = nullptr;
  int clipboard_revision_ = 0;
  bool pasting_ = false;
  bool stroke_ = false;
  /// What the current stroke has painted, so the shade pass covers that and
  /// not the whole map.
  TileRect painted_{};
  /// The union of every footprint painted since the canvas last asked.
  TileRect touched_{};
  /// And *which* tiles of that rectangle, one byte each.
  ///
  /// The rectangle alone is not enough: a stroke drawn diagonally has a bounding
  /// box far larger than the stroke, which is how mixing a dab of dirt used to
  /// come back having re-shaded the grass around it.
  std::vector<uint8_t> painted_mask_;
  /// Fixed, so the noise field is a property of position rather than of when
  /// you painted: strokes that overlap agree, and painting the same place
  /// twice does not reshuffle it.
  static constexpr uint32_t kShadeSeed = 0x5EEDu;
  bool grouping_ = false;
  /// A placement drag in progress, and what it has managed so far.
  bool placing_run_ = false;
  int run_placed_ = 0;
  int run_refused_ = 0;
  uint32_t scatter_seed_ = 1;
  int revision_ = 0;
  int clean_revision_ = 0;
};

}  // namespace pfwin

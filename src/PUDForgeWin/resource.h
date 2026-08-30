// Command and control ids, shared by the .rc script and the C++.
//
// Kept to plain #defines because the resource compiler reads this file too,
// and it understands nothing else.

#pragma once

#define IDR_MENU              100
#define IDR_ACCELERATORS      101
#define IDI_APP               102
/// The client's own button artwork, one 16x16 cell per button. See UiIcons.hpp
/// for which cell is which; scripts/make-ui-icons.ps1 builds the bitmap.
#define IDB_UI_ICONS          103
/// The user guide as one HTML file, built from docs/user_guide.md by
/// scripts/make-help.ps1. Extracted to %TEMP% and opened in a browser.
#define IDR_USER_GUIDE        104

// File
#define IDM_FILE_NEW          200
#define IDM_FILE_OPEN         201
#define IDM_FILE_SAVE         202
#define IDM_FILE_SAVE_AS      203
#define IDM_FILE_EXIT         204
#define IDM_FILE_GENERATE     205
#define IDM_FILE_ARCHIVE      206
/// Ten slots for the most recently opened maps, filled at runtime.
#define IDM_FILE_RECENT_FIRST 210
#define IDM_FILE_RECENT_LAST  219

// Edit
#define IDM_EDIT_UNDO         220
#define IDM_EDIT_REDO         221
#define IDM_EDIT_DELETE       222
#define IDM_EDIT_CUT          223
#define IDM_EDIT_COPY         224
#define IDM_EDIT_PASTE        225
#define IDM_EDIT_PASTE_FLIP   226
#define IDM_EDIT_PASTE_MIRROR 227
#define IDM_EDIT_PASTE_ROTATE 228
#define IDM_OPT_FIT_PASTE     229
/// Copy takes terrain or units, never both; plain Copy asks the mode which,
/// and these two are for when the mode is not the answer.
#define IDM_EDIT_COPY_TERRAIN 236
#define IDM_EDIT_COPY_UNITS   237

// View
#define IDM_VIEW_UNITS_ALL       230   /* order matches pf_unit_filter */
#define IDM_VIEW_UNITS_NONE      231
#define IDM_VIEW_UNITS_GROUND    232
#define IDM_VIEW_UNITS_AIR       233
#define IDM_VIEW_UNITS_BUILDINGS 234
#define IDM_VIEW_LAYER_ART       240   /* order matches pf_overlay */
#define IDM_VIEW_LAYER_MOVEMENT  241
#define IDM_VIEW_LAYER_REGIONS   242
#define IDM_VIEW_LAYER_TILES     243
#define IDM_VIEW_GRID            244
#define IDM_VIEW_ZOOM_FIT        245
#define IDM_VIEW_ZOOM_IN         246
#define IDM_VIEW_ZOOM_OUT        247
#define IDM_VIEW_ZOOM_50         248
#define IDM_VIEW_ZOOM_100        249
#define IDM_VIEW_ZOOM_200        250
#define IDM_VIEW_ZOOM_400        251
#define IDM_VIEW_MODE_TERRAIN    252
#define IDM_VIEW_MODE_UNITS      253
/// The third mode: painting where things may walk, over whatever is drawn.
/// 257 rather than a number beside the other two, which the View block has
/// run out of; the order of these ids means nothing.
#define IDM_VIEW_MODE_MOVEMENT   257
#define IDM_VIEW_WATER           254
/// The two pickers that open where the pointer is rather than in a panel.
#define IDM_VIEW_QUICK_PICK      255
#define IDM_VIEW_TERRAIN_RING    256

// Map
#define IDM_MAP_PROPERTIES    260
#define IDM_MAP_PLACE_STARTS  262
#define IDM_MAP_RANDOM_SHADES 263
#define IDM_MAP_PLAYERS       264
#define IDM_MAP_UNIT_DATA     265
#define IDM_MAP_UPGRADES      266
#define IDM_MAP_RESTRICTIONS  267
#define IDM_MAP_VALIDATE      268
#define IDM_MAP_STATS         269
/* 270 typed a unit and the tile it stood on, the way PUDDraft's Add Unit did.
   The Units menu names every type outright and the pointer has always been the
   way to say where. The id is left unused rather than reassigned. */

// Select
#define IDM_SELECT_ALL        280
#define IDM_SELECT_NONE       281
#define IDM_SELECT_INVERT     282
#define IDM_SELECT_SAME_TYPE  283
#define IDM_SELECT_SAME_OWNER 284
/// Ten entries, one per pf_unit_group, in the core's order.
#define IDM_SELECT_KIND_FIRST 290
#define IDM_SELECT_KIND_LAST  299
/// Sixteen entries, one per player slot: select everything that player owns.
#define IDM_SELECT_OWNER_FIRST 340
#define IDM_SELECT_OWNER_LAST  355
/// Down and up the brush-size ladder; the bracket keys, as every painter has.
#define IDM_EDIT_BRUSH_SMALLER 356
#define IDM_EDIT_BRUSH_BIGGER  357

/// Sixteen entries: give the selected units to that player. The first eight
/// also answer to their digit key, which is where the hand already is.
#define IDM_EDIT_OWNER_FIRST  360
#define IDM_EDIT_OWNER_LAST   375
#define IDM_EDIT_FILL         376

/// The Units menu: one item per unit type, at IDM_UNITS_FIRST + the type's own
/// id, so the item and the unit need no table between them. A hundred and ten
/// of them, which is why they start well past everything else.
#define IDM_UNITS_FIRST       900
#define IDM_UNITS_LAST        1009

// Tools
#define IDM_TOOLS_OPTIONS     310
#define IDM_TOOLS_GAME_FOLDER 311
#define IDM_TOOLS_CONVERT     312
/* 313 and 315 replaced and decorated terrain; both are buttons on the terrain
   dock now. 314 switched a player's race, which the player page does. 319 and
   406-408 imported and exported a section; both are buttons on the page that
   shows that section. The ids are left unused rather than reassigned, so an
   accelerator or a stale menu resource cannot land on the wrong one. */
#define IDM_TOOLS_EXPORT_PNG  316
#define IDM_TOOLS_MOVEMENT    317
#define IDM_TOOLS_AI_SCRIPTS  318
#define IDM_TOOLS_LOG         409

// Help
/// 325 rather than a number in the Tools run above it: 319 is retired and must
/// stay so, and this belongs with Help regardless of where the gap was.
#define IDM_HELP_GUIDE        325
#define IDM_HELP_ABOUT        320
/* 321 was Help > Keyboard Shortcuts. The guide lists the keys now. */

/// Only ever raised by the context menu; it has no menu-bar item of its own.
#define IDM_CONTEXT_INSPECT   322
/// Arm the placement tool with the selected unit's type and owner: make
/// another of this. Numbered among the context items because that is where it
/// started life, as "Place more"; it has an Edit item and Ctrl+D now, and the
/// number stays where it is so no stale resource lands on a different command.
#define IDM_EDIT_DUPLICATE    323
/// Select every unit of the type under the pointer.
#define IDM_CONTEXT_SAME_TYPE 324

// View, continued: showing and hiding the furniture
#define IDM_VIEW_ZOOM_SELECTION 380
#define IDM_VIEW_TOOLBARS       381
#define IDM_VIEW_STATUSBAR      382
#define IDM_VIEW_MINIMAP        383
#define IDM_VIEW_REACH          384
/// Eight entries: scroll to a player's start location.
#define IDM_VIEW_GOTO_START_FIRST 390
#define IDM_VIEW_GOTO_START_LAST  405

// Options. All of them now live in the Options dialog except the paste one,
// which stays a menu item because it is a property of the paste about to
// happen and the Edit menu is where you already are when you think about it.
#define IDM_OPT_FIT_EDGES     330   /* retired to IDD_OPTIONS */
#define IDM_OPT_ILLEGAL       331   /* retired to IDD_OPTIONS */
#define IDM_OPT_STACKED       332   /* retired to IDD_OPTIONS */
#define IDM_OPT_EDGE          333   /* retired to IDD_OPTIONS */
#define IDM_OPT_KEEP_STRANDED 334   /* retired to IDD_OPTIONS */
#define IDM_OPT_MARK_SPECIAL  335   /* retired to IDD_OPTIONS */

// Controls
#define IDC_STATUS_BAR        400
#define IDC_TERRAIN_PALETTE   401
#define IDC_UNIT_PALETTE      402
#define IDC_UNIT_OWNER        403
#define IDC_BRUSH_SIZE        404
#define IDC_MINIMAP           405
#define IDC_UNIT_SEARCH       406
/// Segmented rows: three exclusive Detail buttons, four exclusive Shape
/// buttons, five combining Mirror buttons. Consecutive ids, first named.
#define IDC_DETAIL_FIRST      410   /* +0 plain, +1 mixed, +2 detail */
#define IDC_SHAPE_FIRST       420   /* +0 square, +1 circle, +2 scatter, +3 fill */
#define IDC_MIRROR_FIRST      430   /* +0 none, then the four pf_mirror axes */
/* 440 and 441 were the terrain dock's Paint and Select. Painting is what a
   palette click means; selecting a rectangle is on the strip along the top. */
/* The rectangle's two actions, shown only while there is a rectangle. */
// 448 and 449 were IDC_RECT_FILL and IDC_RECT_CLEAR, the terrain dock's
// rectangle buttons. Left unused: the canvas menu does both jobs.
/* 450 and 451 were the units dock's Select and Place, gone the same way for
   the same reason. */
/* 460 was the Mix checkbox beside the Light/Dark radios. Mixing is the third
   radio now, so it is IDC_SHADE_FIRST + 2 and this id names nothing. Left
   unused rather than reassigned. */
/* Light and dark, the two drawings a terrain pair has. Exclusive, pushlike. */
#define IDC_SHADE_FIRST       461   /* +0 light, +1 dark, +2 mix */
/* The eyedropper, armed for one click. Ctrl and the left button do the same. */
#define IDC_TERRAIN_PICK      463
/* Straight to the player sheet from the unit dock's player dropdown. */
#define IDC_UNIT_PLAYER_PROPS 464

/* Dialogs. Laid out in PUDForge.rc; driven by Dialogs.cpp. */
#define IDD_MAP_PROPERTIES    500
#define IDD_PLAYERS           501
#define IDD_TILE_PICKER       502
#define IDD_UNIT_DATA         504
#define IDD_UPGRADES          505
#define IDD_RESTRICTIONS      506
#define IDD_GENERATE          507
#define IDD_QUICK_PICK        508
#define IDD_REPLACE_TERRAIN   509
#define IDD_DECORATE          510
#define IDD_CONVERT_UNITS     511
/* 512 was IDD_SWITCH_RACE: changing a player's race on the player page is
   what swaps their units now. */
#define IDD_MAP_STATS         513
#define IDD_EXPORT_PNG        514
#define IDD_OPTIONS           515
/* 516 was the keyboard shortcut list, retired with its menu item. */
#define IDD_NEW_MAP           517
#define IDD_UNIT_INSPECTOR    518
#define IDD_MOVEMENT          519
#define IDD_AI_SCRIPTS        520
#define IDD_ARCHIVE           521
#define IDD_LOG               522
/* 523 was IDD_BITS, the dialog a flags field's hex button opened to tick its
   bits in. The named bits are ticks on the unit page itself now. */
/// The six map sheets as tabs of one window.
#define IDD_MAP_SHEETS        524
#define IDC_SHEET_TABS        790

/// First run: which installed copy of the game to read the artwork out of.
#define IDD_GAME_SETUP        525
#define IDC_SETUP_HEAD        846
#define IDC_SETUP_LIST        847
#define IDC_SETUP_NOTE        848
#define IDC_SETUP_BROWSE      849
#define IDC_SETUP_ICON        850
#define IDC_SETUP_TITLE       851
#define IDC_SETUP_GUIDE       852
/// Options: wipe everything this client has remembered about itself.
#define IDC_OPT_RESET         850

#define IDC_MAP_DESCRIPTION   510
#define IDC_MAP_TILESET       511
#define IDC_MAP_SIZE          512
#define IDC_MAP_DESC_LEFT     513

// Master and detail: a list of the sixteen slots, and a form of whichever one
// is selected. The form is a custom control, so the template carries a
// placeholder and the form is created over it.
#define IDC_PLAYER_LIST       520
#define IDC_PLAYER_NOTE       521
#define IDC_PLAYER_FORM_SLOT  529
#define IDC_PLAYER_FORM       530

// The picker's grid is a PaletteGrid, which cannot be a template control, so
// the template carries a placeholder at IDC_TILES_GRID and the grid itself is
// created over it at IDC_TILES_GRID + 1.
#define IDC_TILES_GRID        530
#define IDC_TILES_NOTE        532

// Resizing, which lives on the map-properties page: the page already had to
// say how big the map is.
#define IDC_RESIZE_SIZE       540
#define IDC_RESIZE_NOTE       541
#define IDC_RESIZE_WARNING    542
/// Nine radio buttons, row-major: where the old map sits in the new grid.
#define IDC_RESIZE_ANCHOR     550

#define IDC_UDTA_UNITS        560
/// The selected unit's name, over the form, so the form always says whose
/// numbers it is showing.
#define IDC_UDTA_SUBJECT      561
#define IDC_UDTA_DEFAULTS     567
#define IDC_UDTA_NOTE         568

#define IDC_UGRD_LIST         570
#define IDC_UGRD_SUBJECT      571
#define IDC_UGRD_DEFAULTS     575
#define IDC_UGRD_NOTE         576

#define IDC_ALOW_BLOCK        580
/// The master list: the sixteen players, drawn in their own colours.
#define IDC_ALOW_PLAYER       581
#define IDC_ALOW_SUBJECT      582
#define IDC_ALOW_ALL          583
#define IDC_ALOW_NONE         584
#define IDC_ALOW_NOTE         585
/// What a tick means, which is not the same sentence for all six blocks.
#define IDC_ALOW_MEANING      586
/// "Use the game default restrictions", the switch the other two tables have.
/// Ticked means the saved map carries no `ALOW` at all rather than one holding
/// the unrestricted table, because that is the state most maps are in.
#define IDC_ALOW_DEFAULTS     587

#define IDC_GEN_SIZE          590
#define IDC_GEN_TILESET       591
#define IDC_GEN_WATER         592
#define IDC_GEN_COAST         593
#define IDC_GEN_FOREST        594
#define IDC_GEN_ROCK          595
#define IDC_GEN_WATER_VALUE   596
#define IDC_GEN_COAST_VALUE   597
#define IDC_GEN_FOREST_VALUE  598
#define IDC_GEN_ROCK_VALUE    599
#define IDC_GEN_CLEARINGS     600
#define IDC_GEN_RADIUS        601
#define IDC_GEN_SEED          602
#define IDC_GEN_ANOTHER       603
#define IDC_GEN_MINES         604
#define IDC_GEN_STARTS        605
#define IDC_GEN_PREVIEW       606
#define IDC_GEN_NOTE          607
/// The three octaves the map is layered from: one broad one deciding where the
/// land is, and two finer ones roughening what it meets. Scale is features per
/// tile, so a smaller number is a broader shape.
#define IDC_GEN_LAND          608
#define IDC_GEN_COASTLINE     609
#define IDC_GEN_DETAIL        610
#define IDC_GEN_LAND_VALUE    611
#define IDC_GEN_COASTLINE_VALUE 612
#define IDC_GEN_DETAIL_VALUE  613
/// Rolls every number on the sheet, and nothing about what is placed on it.
#define IDC_GEN_RANDOMIZE     614

#define IDC_QUICK_SEARCH      610
#define IDC_QUICK_LIST        611

#define IDC_REPLACE_FROM      620
#define IDC_REPLACE_TO        621
#define IDC_REPLACE_NOTE      622

#define IDC_DECORATE_TERRAIN  630
#define IDC_DECORATE_DENSITY  631
#define IDC_DECORATE_VALUE    632
#define IDC_DECORATE_NOTE     633

#define IDC_CONVERT_FROM      640
#define IDC_CONVERT_TO        641
#define IDC_CONVERT_NOTE      642
/* Narrow the conversion to the selected units. Off by default, and disabled
   when nothing is selected: see Editor::ReplaceUnitType. */
#define IDC_CONVERT_SELECTED  643

#define IDC_STATS_LIST        660

#define IDC_PNG_UNITS         670
#define IDC_PNG_GRID          671
#define IDC_PNG_SELECTION     672
#define IDC_PNG_SCALE         673
#define IDC_PNG_NOTE          674

/* One control per Editor option, in the order the Options sheet lists them. */
#define IDC_OPT_FIT_EDGES     680
#define IDC_OPT_FIT_PASTE     681
#define IDC_OPT_KEEP_STRANDED 682
#define IDC_OPT_ILLEGAL       683
#define IDC_OPT_STACKED       684
#define IDC_OPT_EDGE          685
#define IDC_OPT_MARK_SPECIAL  686
#define IDC_OPT_UNIT_ART      687
#define IDC_OPT_FACING        688
/// The game's own unit voices, played over an edit. Off unless asked for.
#define IDC_OPT_SOUNDS        689
/// Offer every race's units in the palette rather than the chosen player's.
#define IDC_OPT_ALL_RACES     690
/* Retired: water animation moved to the View menu and this control went with
   it. Left named rather than deleted, and given a number of its own rather
   than the 689 it used to share with IDC_OPT_SOUNDS — a duplicate is how a
   stale dialog template lands on the wrong control. Nothing uses it. */
#define IDC_OPT_WATER         691

/// Offer the units an editor normally keeps back — the dead slots, the runtime
/// leftovers, the campaign workers. 692 rather than reusing retired 691.
#define IDC_OPT_UNUSED_UNITS  692

/* 700 was the keyboard shortcut list box, retired with its dialog. */

#define IDC_NEW_TILESET       710
#define IDC_NEW_SIZE          711

#define IDC_INSPECT_TYPE      720
#define IDC_INSPECT_POS       721
#define IDC_INSPECT_OWNER     722
#define IDC_INSPECT_VALUE     723
#define IDC_INSPECT_VALUE_LBL 724
/// What the number means: the step it snaps to, or what 0 and 1 are.
#define IDC_INSPECT_NOTE      729
#define IDC_INSPECT_ICON      725
/// "Footman Unit Properties" — leaves the inspector, which is about this one
/// unit on the map, for the sheet that is about every Footman on it.
#define IDC_INSPECT_PROPS     726

#define IDC_MOVE_CLASSES      730
#define IDC_MOVE_COUNT        731
#define IDC_MOVE_RESET        732
#define IDC_MOVE_ABOUT        733
#define IDC_MOVE_LEGEND       734
#define IDC_MOVE_HINT         735

#define IDC_AI_LIST           740
#define IDC_AI_SUMMARY        741
#define IDC_AI_LISTING        742

#define IDC_ARCHIVE_FIND      750
#define IDC_ARCHIVE_LIST      751
#define IDC_ARCHIVE_NOTE      752

#define IDC_LOG_TEXT          760
#define IDC_LOG_COPY          761
#define IDC_LOG_CLEAR         762

/// The palettes' right-click menu: which of the column count and the tile size
/// the panel's width is allowed to move. 781 to 783 were the hand-picked counts
/// the menu no longer offers, and stay spent so an old build's saved menu
/// position cannot mean something new here.
#define IDM_COLUMNS_AUTO      780
/// The same menu's other half: which side of the canvas this dock sits on.
#define IDM_DOCK_LEFT         784
#define IDM_DOCK_RIGHT        785
/// The one item a panel can put above the rest, about whatever the pointer is
/// over. Its text is the caller's, so this id says nothing about what it does.
#define IDM_DOCK_EXTRA        787
/// Out of order because the ids either side of it were already spent: this
/// belongs beside IDM_COLUMNS_AUTO, which is where the menu puts it.
#define IDM_COLUMNS_SCALE     786

/* The two bulk terrain edits, on the terrain panel as well as in Tools. */
#define IDC_TERRAIN_REPLACE   800
#define IDC_TERRAIN_DECORATE  801
/// The strip of commands across the top. Its buttons carry the very command
/// ids the menu items do, so only the two windows need ids of their own.
#define IDC_TOOLBAR           810
#define IDC_TOOLBAR_ZOOM      811
/// Showing and hiding the strip. IDM_VIEW_TOOLBARS is the docks and has been
/// since before there was a toolbar to confuse it with.
#define IDM_VIEW_TOOLBAR      830
/// The two select tools, which live on the strip rather than in the docks: a
/// dock is about *what* to draw with, and selecting is not drawing with
/// anything. Both are toggles, and the strip shows which is in hand.
#define IDM_TOOL_TERRAIN_SELECT 831
#define IDM_TOOL_UNIT_SELECT    832

#define IDC_UDTA_FORM_SLOT    771
#define IDC_UDTA_FORM         772
#define IDC_UGRD_FORM_SLOT    773
#define IDC_UGRD_FORM         774
#define IDC_ALOW_FORM_SLOT    775
#define IDC_ALOW_FORM         776

/* Component files, on the sheet whose section they carry rather than in the
   Tools menu: a .un is the Units page's business and nothing else's. */
#define IDC_UDTA_IMPORT       840
#define IDC_UDTA_EXPORT       841
#define IDC_UGRD_IMPORT       842
#define IDC_UGRD_EXPORT       843
#define IDC_ALOW_IMPORT       844
#define IDC_ALOW_EXPORT       845
/* 846 was a read-only line saying whether the map carried an `ALOW` section,
   on the reasoning that the format has no `useDefaultData` for restrictions the
   way it does for the unit and upgrade tables. It is IDC_ALOW_DEFAULTS now, a
   real switch beside the other two pages' — the section's presence *is* the
   flag, so there was a switch to offer after all. Left unused rather than
   reassigned, like the rest. */
/* 847 was "Show this AI script…", a captioned button at the foot of the player
   page. It is the AI row's own companion button now — beside the dropdown it
   explains rather than a page away from it — and the form owns that button, so
   there is no template control and no id. Left unused rather than reassigned,
   like the rest. */

/* 525 and 850-856 were the Add-a-unit box: a type by name, a tile by number.
   The Units menu names every type outright and the pointer says where, so the
   box asked two questions the client answers better. Left unused rather than
   reassigned, so a stale template cannot land on the wrong control. */

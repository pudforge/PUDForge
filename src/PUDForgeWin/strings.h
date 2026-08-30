// String table ids. The text is in Strings.rc.
//
// Plain #defines because the resource compiler reads this file too, the same
// reason resource.h is. Ids are grouped by where the text appears and left with
// gaps, so a new string in one area does not renumber another.
//
// To translate: copy Strings.rc, translate the quoted text and nothing else,
// and build with the copy. The format specifiers (%d, %ls) are part of the
// string and may be reordered where a language needs it — that is why the code
// passes arguments rather than gluing sentences together.

#pragma once

// ---------------------------------------------------------------- the shell
#define IDS_UNTITLED              1000
#define IDS_TITLE                 1001
#define IDS_TITLE_DIRTY           1002
#define IDS_HOVER_TILE            1003
#define IDS_ZOOM_PERCENT          1004
#define IDS_MAP_SIZE              1005
#define IDS_RECENT_EMPTY          1006
#define IDS_RECENT_ITEM           1007
#define IDS_SELECTED_ONE          1008
#define IDS_SELECTED_MANY         1009
#define IDS_SELECTED_TILES        1010
/* A selection that is not a plain rectangle: its area, then its box. */
#define IDS_SELECTED_REGION       1011

// ------------------------------------------------------------------ file
#define IDS_NEW_MAP               1020
#define IDS_OPENED                1021
#define IDS_SAVED                 1022
#define IDS_CANNOT_OPEN           1023
#define IDS_SAVE_CHANGES          1024
#define IDS_GENERATED             1025

// ------------------------------------------------------------ check map
#define IDS_CHECK_TITLE           1040
#define IDS_CHECK_NONE            1041
#define IDS_SEVERITY_ERROR        1042
#define IDS_SEVERITY_WARNING      1043
#define IDS_SEVERITY_NOTE         1044

// --------------------------------------------------------------- editing
#define IDS_STARTS_PLACED_ONE     1060
#define IDS_TILES_FILLED          1061
#define IDS_FILL_NEEDS_RECT       1062
#define IDS_UNITS_REMOVED_ONE     1063
#define IDS_THAT_PLAYER           1064
#define IDS_OWNS_NOTHING          1065
#define IDS_OWNED_BY_ONE          1066
#define IDS_OWNED_BY_MANY         1067
#define IDS_GIVEN_TO_ONE          1068
#define IDS_GIVEN_TO_MANY         1069
#define IDS_PLACING_FOR           1070
#define IDS_PLAYER_N              1071
#define IDS_STARTS_PLACED_MANY    1072
#define IDS_UNITS_REMOVED_MANY    1073

// ------------------------------------------------------------- clipboard
#define IDS_COPIED_ONE            1080
#define IDS_COPIED_MANY           1081
#define IDS_CUT_ONE               1082
#define IDS_CUT_MANY              1083
#define IDS_NOTHING_COPIED        1084
#define IDS_PASTE_ARMED           1085
#define IDS_FRAGMENT_SIZE         1086
/* A terrain copy has no units to count, so it says its size instead. */
#define IDS_COPIED_TERRAIN        1087

// ------------------------------------------------------------------ help
#define IDS_ABOUT_TITLE           1100
#define IDS_ABOUT_BODY            1101

// -------------------------------------------------------- terrain panel
/* 1120 and 1121 were Paint and Select, the terrain panel's two tool buttons.
   Painting is what clicking a palette cell means and selecting a rectangle is
   on the strip along the top, so neither is a button in the dock any more. */
#define IDS_ROW_DETAIL            1122
#define IDS_DETAIL_PLAIN          1123
#define IDS_DETAIL_MIXED          1124
#define IDS_DETAIL_DETAIL         1125
#define IDS_ROW_SHAPE             1126
#define IDS_ROW_SIZE              1127
#define IDS_ROW_MIRROR            1128
#define IDS_ROW_SHADE             1129
/* 1130 was "Mix light and dark". Mixing shares a row with Light and Dark now
   and has to fit in a third of it, so it is IDS_MIX_SHADES_SHORT. */
#define IDS_MIX_SHADES_ON         1131
#define IDS_MIX_SHADES_OFF        1132
#define IDS_PICK_A_TILE           1133
#define IDS_CUSTOM_TILE           1134
#define IDS_NO_ARTWORK_TO_PICK    1135
/* 1136 and 1137 were IDS_RECT_FILL and IDS_RECT_CLEAR. Left unused. */

// ---------------------------------------------------------- units panel
/* 1150 and 1151 were Select and Place, which went the way the terrain
   panel's two did: a palette click means place, and select is on the strip. */
/* 1152 was "Erase", the units panel's third tool button. Deleting is Del and
   the canvas menu now, so the button went; the id is left unused rather than
   reassigned, for the same reason 1154 is. */
#define IDS_FIND_A_UNIT           1153
/* 1154 was "Best match first", the heading over the palette when it doubled
   as a search result. The search has its own window now and needs no heading;
   the id is left unused rather than reassigned, so a translation carried over
   from an older build cannot land its text on a different string. */
#define IDS_NOTHING_MATCHES       1155
#define IDS_RACE_HUMAN            1156
#define IDS_RACE_ORC              1157
#define IDS_KIND_LAND             1158
#define IDS_KIND_AIR              1159
#define IDS_KIND_WATER            1160
#define IDS_KIND_BUILDINGS        1161
#define IDS_KIND_HEROES           1162
#define IDS_GROUP_NEUTRAL         1163
#define IDS_GROUP_MARKERS         1164
/* Race and kind, joined into a palette heading. */
#define IDS_GROUP_HEADING         1165

// ---------------------------------------------------------- the canvas
#define IDS_PASTED_SHORT          1180
#define IDS_PASTED_ALL            1181
#define IDS_PASTE_CANCELLED       1182
#define IDS_PASTED_ONE            1183
#define IDS_PASTED_TERRAIN_ONLY   1184

// ------------------------------------------------------------- game data
#define IDS_FIND_GAME_TITLE       1200
#define IDS_SETUP_HEAD_FOUND      1201
#define IDS_SETUP_HEAD_NONE       1202
#define IDS_SETUP_NOTE            1203
#define IDS_SETUP_ROW             1204
#define IDS_SETUP_BAD_FOLDER      1205
#define IDS_SETUP_IN_USE          1206
#define IDS_SETUP_NOTE_REQUIRED   1207
/* 1208 was the setup dialog's Quit button, which the title bar already is. */
#define IDS_SETUP_CANCEL          1209
#define IDS_SETUP_TITLE_WELCOME   1210
#define IDS_SETUP_TITLE_FIND      1211
#define IDS_SETUP_GUIDE           1212

// ------------------------------------------------------------- the sheets
#define IDS_TILESET_FOREST        1220
#define IDS_TILESET_WINTER        1221
#define IDS_TILESET_WASTELAND     1222
#define IDS_TILESET_SWAMP         1223
#define IDS_OWNER_NOBODY          1224
#define IDS_OWNER_HUMAN           1225
#define IDS_OWNER_COMPUTER        1226
#define IDS_OWNER_PASSIVE         1227
#define IDS_OWNER_RESCUE_PASSIVE  1228
#define IDS_OWNER_RESCUE_ACTIVE   1229
#define IDS_RACE_HUMAN_NAME       1230
#define IDS_RACE_ORC_NAME         1231
#define IDS_RACE_NEUTRAL          1232

#define IDS_DESC_ROOM             1240
#define IDS_SIZE_IN_TILES         1241
#define IDS_PLAYER_ROW            1242
#define IDS_OWNS_ONE_HERE         1243
#define IDS_OWNS_MANY_HERE        1244
#define IDS_TILESET_CHANGED       1245
#define IDS_MAP_PROPS_UPDATED     1246
#define IDS_NO_PLAYERS_CHANGED    1247
#define IDS_PLAYERS_UPDATED_ONE   1248
#define IDS_PLAYERS_UPDATED_MANY  1249

#define IDS_FIELD_WIDTH           1260
#define IDS_FIELD_HEIGHT          1261
#define IDS_YES                   1262
#define IDS_NO                    1263
#define IDS_UNUSED_UNIT           1264
#define IDS_NOTHING_CHANGED       1265
#define IDS_UDTA_CHANGED_ONE      1266
#define IDS_UDTA_CHANGED_MANY     1267
#define IDS_UNIT_PROPS_NONE       1268
#define IDS_UNIT_PROPS_ONE        1269
#define IDS_UNIT_PROPS_MANY       1270
#define IDS_UGRD_CHANGED_ONE      1271
#define IDS_UGRD_CHANGED_MANY     1272
#define IDS_UPGRADE_PROPS_NONE    1273
#define IDS_UPGRADE_PROPS_ONE     1274
#define IDS_UPGRADE_PROPS_MANY    1275

#define IDS_ALOW_BIT_UNKNOWN      1280
#define IDS_ALOW_COUNT            1281
/* 1282 said the map carried no restrictions so everything was allowed. The
   Use-the-game-defaults tick says that now, and says it as something you can
   act on. Left unused rather than reassigned, like the rest. */
#define IDS_ALOW_MEANS_ALLOWED    1286
#define IDS_ALOW_MEANS_KNOWN      1287
#define IDS_ALOW_MEANS_PARTIAL    1288

/* 1283 said no restrictions changed. The window's own "nothing was changed"
   covers it, and the page no longer applies anything when nothing moved. */
#define IDS_ALOW_CHANGED_ONE      1284
#define IDS_ALOW_CHANGED_MANY     1285

#define IDS_RESIZE_SAME           1290
#define IDS_RESIZE_PADS           1291
#define IDS_RESIZE_CROPS_SAFE     1292
#define IDS_RESIZE_CROPS_ONE      1293
#define IDS_RESIZE_CROPS_MANY     1294
#define IDS_RESIZE_OILM_WARNING   1295
#define IDS_RESIZE_FAILED         1296
#define IDS_RESIZED               1297
#define IDS_RESIZED_DROPPED_ONE   1298
#define IDS_RESIZED_DROPPED_MANY  1299

#define IDS_TILES_PICK_ONE        1310
#define IDS_TILE_MOSTLY           1311
#define IDS_TERRAIN_OTHER         1312
#define IDS_UNKNOWN               1313
#define IDS_TILE_HEX              1314

#define IDS_GEN_NO_MAP            1320
#define IDS_GEN_SIZE              1321
#define IDS_GEN_SIZE_SHORT        1322
#define IDS_GEN_SHORT_MINES       1323
#define IDS_GEN_SHORT_STARTS      1324
#define IDS_GEN_SHORT_JOIN        1325
#define IDS_GEN_OF_THE_MAP        1326
#define IDS_GEN_OF_THE_LAND       1327
#define IDS_GEN_SHARE             1328

// ------------------------------------------------------- the file dialogs
#define IDS_FILTER_MAPS           1340
#define IDS_FILTER_ALL            1341
#define IDS_FILTER_PNG            1342

// ---------------------------------------------------------- the bulk edits
#define IDS_SCOPE_MAP             1360
#define IDS_SCOPE_SELECTION       1361
#define IDS_PICK_TWO_TERRAINS     1362
#define IDS_PICK_TWO_TYPES        1363
#define IDS_REPLACE_WOULD_TOUCH   1364
#define IDS_REPLACE_FOUND_NONE    1365
#define IDS_REPLACED_TILES        1366
#define IDS_REPLACED_NOTHING      1367
#define IDS_PER_CENT              1368
#define IDS_DECORATE_SCOPE        1369
#define IDS_DECORATED             1370
#define IDS_DECORATED_NOTHING     1371
#define IDS_CONVERT_WOULD_ONE     1372
#define IDS_CONVERT_WOULD_MANY    1373
#define IDS_CONVERTED             1374
#define IDS_CONVERTED_NOTHING     1375
/* 1376 and 1377 belonged to the Switch a player's race dialog, which is gone:
   changing the race on the player page is what swaps the units now, and it
   says so with IDS_RACE_FOLLOWED. */
/* The same count, said of the selection instead of the map. 1378 rather than
   the two retired ids above. */
#define IDS_CONVERT_SEL_ONE       1378
#define IDS_CONVERT_SEL_MANY      1379

// ------------------------------------------------------- map statistics
#define IDS_STAT_ROW              1390
#define IDS_STAT_HEAD_MAP         1391
#define IDS_STAT_HEAD_TERRAIN     1392
#define IDS_STAT_HEAD_UNITS       1393
#define IDS_STAT_HEAD_RESOURCES   1394
#define IDS_STAT_TOTAL            1395
#define IDS_STAT_GOLD             1396
#define IDS_STAT_OIL              1397
#define IDS_FIELD_SIZE            1398
#define IDS_FIELD_TILESET         1399

// --------------------------------------------------------- exporting a PNG
#define IDS_PNG_SIZE              1410
#define IDS_PNG_SCALE_X           1411
#define IDS_PNG_WROTE             1412
#define IDS_PNG_FAILED            1413

// ------------------------------------------------------------- the options
#define IDS_ART_PORTRAIT          1420
#define IDS_ART_SPRITE            1421

// ------------------------------------------------------- the unit inspector
#define IDS_INSPECT_AT            1430
#define IDS_INSPECT_AMOUNT        1431
#define IDS_INSPECT_FLAG          1432
#define IDS_UNIT_UPDATED          1433
/* The step the amount snaps to and what a new one gets, then what the other
   reading of the same field is. */
#define IDS_INSPECT_STEP          1434
#define IDS_INSPECT_FLAG_NOTE     1435

// --------------------------------------------------------- keyboard help
//
// 1450-1520 and 1830-1852 held it: two ids per row, what you press and what it
// does. The Help menu no longer lists the keys — the user guide does, and one
// reference beats two that can disagree — so the whole range is retired rather
// than reassigned, which a translation carried over from an older build would
// otherwise land on the wrong strings.

// ------------------------------------------------------------ the shell, 2
#define IDS_NO_SELECTION_TO_ZOOM  1520
#define IDS_NO_START_FOR          1521
#define IDS_WENT_TO_START         1522
#define IDS_DROPPED_FILE          1523
/// Why Enter opened nothing. The context menu never needs these: it only offers
/// the item over a unit, and it selects that unit on the way.
#define IDS_INSPECT_NEEDS_UNIT    1524
#define IDS_INSPECT_ONE_AT_A_TIME 1525

// ------------------------------------------------------- the context menu
/// Was "Open This Unit…". Same id, because the item is the same item.
#define IDS_CONTEXT_UNIT_PROPS    1530
#define IDS_CONTEXT_DELETE        1531
#define IDS_CONTEXT_COPY          1532
#define IDS_CONTEXT_CUT           1533
#define IDS_CONTEXT_PASTE         1534
#define IDS_CONTEXT_FILL          1535
#define IDS_CONTEXT_STATS         1536
/* %ls is the type of the unit under the pointer. */
#define IDS_CONTEXT_DUPLICATE     1537
#define IDS_CONTEXT_SAME_TYPE     1538

// ------------------------------------------------------- movement and AI
#define IDS_MOVE_OVERRIDDEN_ONE   1540
#define IDS_MOVE_OVERRIDDEN_MANY  1541
#define IDS_MOVE_MATCHES          1542
#define IDS_MOVE_RESET_DONE       1543
#define IDS_AI_ROW                1544
#define IDS_AI_NEEDS_GAME         1545
/* What the section is, what the list means, and what Reset does. */
#define IDS_MOVE_ABOUT            1546
#define IDS_MOVE_LEGEND           1547
#define IDS_MOVE_HINT             1548

// -------------------------------------------------------- component files
#define IDS_FILTER_COMPONENTS     1550
#define IDS_COMPONENT_IMPORTED    1551
#define IDS_COMPONENT_REFUSED     1552
#define IDS_COMPONENT_EXPORTED    1553
#define IDS_COMPONENT_ABSENT      1554

// ---------------------------------------------------------- the archive
#define IDS_FILTER_ARCHIVES       1560
#define IDS_ARCHIVE_FIND          1561
#define IDS_ARCHIVE_COUNT_ONE     1562
#define IDS_ARCHIVE_COUNT_MANY    1563
#define IDS_ARCHIVE_UNREADABLE    1564
#define IDS_ARCHIVE_NO_MAPS       1565
#define IDS_ARCHIVE_OPENED        1566

// ---------------------------------------------------------------- the log
/* The time, then the line. A translation may reorder them. */
#define IDS_LOG_LINE              1570
#define IDS_LOG_LINE_WARN         1571

// ------------------------------------------------------------- field labels
/* The fields of a player slot. Every other sheet's labels come from
   pf_field_label, which is the core's business; a player slot is the one
   subject whose fields are not a section of the file. */
#define IDS_COL_CONTROLLED_BY     1581
#define IDS_COL_RACE              1582
#define IDS_COL_GOLD              1583
#define IDS_COL_LUMBER            1584
#define IDS_COL_OIL               1585
#define IDS_COL_AI                1586
/* %ls the subject's name, %ls the field's or the block's. */
#define IDS_BITS_TITLE            1591
/* Under the player list: how much of the map the selected slot owns. */
#define IDS_PLAYER_OWNS_ONE       1592
#define IDS_PLAYER_OWNS_MANY      1593

// ----------------------------------------------- what the dock's glyphs mean
/* The shape, mirror and detail buttons are glyphs, which is what makes the
   row readable at a glance and unreadable the first time. Tooltips. */
#define IDS_TIP_SHAPE_SQUARE      1600
#define IDS_TIP_SHAPE_CIRCLE      1601
#define IDS_TIP_SHAPE_SCATTER     1602
#define IDS_TIP_SHAPE_FILL        1603
#define IDS_TIP_MIRROR_NONE       1604
#define IDS_TIP_MIRROR_LR         1605
#define IDS_TIP_MIRROR_TB         1606
#define IDS_TIP_MIRROR_SWNE       1607
#define IDS_TIP_MIRROR_NWSE       1608
#define IDS_TIP_DETAIL_PLAIN      1609
#define IDS_TIP_DETAIL_MIXED      1610
#define IDS_TIP_DETAIL_DETAIL     1611
#define IDS_TIP_SIZE              1612
/* 1613 and 1614 explained the two tool buttons the dock no longer has. */
#define IDS_TIP_SHADE             1615
/* Status line when the wheel or a bracket key resizes the brush. */
#define IDS_BRUSH_SIZED           1595
/* The rung below one tile. 1594 rather than the next number up: 1596 is the
   status bar's tooltip. */
#define IDS_BRUSH_SIZED_CORNER    1594
/* Tooltip on the status bar's message, which opens the log. */
#define IDS_STATUS_OPENS_LOG      1596
/* The palettes' right-click menu: the two sizing modes. 1598 was the explicit
   tiles-per-row count, which the menu no longer offers. */
#define IDS_COLUMNS_AUTO          1597
#define IDS_COLUMNS_SCALE         1700
/* How a placement drag went. %d units placed, %d turned down. */
#define IDS_PLACED_ALL            1616
#define IDS_PLACED_SOME           1617
#define IDS_PLACED_NONE           1618
/* Which side of the canvas a dock sits on. */
#define IDS_DOCK_LEFT             1619
#define IDS_DOCK_RIGHT            1620
/* The tabs of the map window, and what its OK did. */
#define IDS_TAB_MAP               1621
#define IDS_TAB_PLAYERS           1622
#define IDS_TAB_UNITS             1623
#define IDS_TAB_UPGRADES          1624
#define IDS_TAB_RESTRICTIONS      1625
#define IDS_TAB_STATISTICS        1626
#define IDS_SHEETS_NONE_CHANGED   1627
#define IDS_SHEETS_MANY_CHANGED   1628
#define IDS_SHEETS_TITLE          1629
/* The AI row's companion button on the player page. A number of its own: 1613
   and 1614 are free but retired, and reusing a retired id is how a translation
   carried over from an older build lands on the wrong string. */
/* 1630 was the tooltip on the player page's "show this AI script" button.
   The listing is under the dropdown itself now, so the button is gone. The
   id is left unused rather than reassigned. */
/* The two select tools, which the toolbar shows as pictures with no key
   written on them. Fresh numbers rather than any of the retired ones. */
/* Escape or a right-click put the terrain brush down. */
#define IDS_BRUSH_PUT_DOWN        1650
/* Status line when Escape or the right button leaves unit placement. */
#define IDS_PLACING_LEFT          1660
/* How big the map is now, above the control that changes it. */
#define IDS_MAP_SIZE_NOW          1670
/* When the map description will not hold what was typed or pasted. */
#define IDS_DESC_REFUSED_CHAR     1680
#define IDS_DESC_UNSTORABLE       1681
#define IDS_DESC_NOT_STORED       1682
/* The terrain panel's two bulk edits, and what each one does. The ellipsis
   is on the button because both open a sheet before they touch anything. */
#define IDS_BULK_REPLACE          1690
#define IDS_BULK_DECORATE         1691
#define IDS_TIP_BULK_REPLACE      1692
#define IDS_TIP_BULK_DECORATE     1693

// ------------------------------------------------- the strip along the top
/* Button captions. There is no icon strip in the resources, so these are what
   the buttons are: one word each, because a toolbar is read sideways. */
#define IDS_TB_NEW                1710
#define IDS_TB_OPEN               1711
#define IDS_TB_SAVE               1712
#define IDS_TB_UNDO               1713
#define IDS_TB_REDO               1714
#define IDS_TB_DELETE             1715
#define IDS_TB_COPY               1716
#define IDS_TB_CUT                1717
#define IDS_TB_PASTE              1718
/* The two select tools, which live here rather than in the docks. Two words
   each, because "Select" alone would not say which half of the editor. */
#define IDS_TB_SELECT_TERRAIN     1719
#define IDS_TB_SELECT_UNITS       1720
/* What each button does and the key that does it, for its tooltip. */
#define IDS_TBTIP_NEW             1721
#define IDS_TBTIP_OPEN            1722
#define IDS_TBTIP_SAVE            1723
#define IDS_TBTIP_UNDO            1724
#define IDS_TBTIP_REDO            1725
#define IDS_TBTIP_DELETE          1726
#define IDS_TBTIP_COPY            1727
#define IDS_TBTIP_CUT             1728
#define IDS_TBTIP_PASTE           1729
#define IDS_TBTIP_SELECT_TERRAIN  1740
#define IDS_TBTIP_SELECT_UNITS    1741

// ------------------------------------------------- the terrain dock, part two
/* The eyedropper, which was Ctrl and the left button and nothing else. */
#define IDS_TOOL_PICK             1730
#define IDS_TIP_PICK              1731
#define IDS_PICK_ARMED            1732
#define IDS_PICK_OFF              1733
/* Which of a terrain's two drawings a stroke lays. */
#define IDS_SHADE_LIGHT           1734
#define IDS_SHADE_DARK            1735
#define IDS_TIP_SHADE_LIGHT       1736
#define IDS_TIP_SHADE_DARK        1737
#define IDS_MIX_SHADES_SHORT      1738
/* Appended inside the brackets of a player's name, after its colour. */
#define IDS_PLAYER_RACE_SUFFIX    1739

/* 1750-1752 belonged to the Add-a-unit box, which the Units menu replaced. */

// ------------------------------------------------------ component files, redux
/* On the sheet whose section they carry, rather than in the Tools menu. */
#define IDS_SECTION_IMPORT        1760
#define IDS_SECTION_EXPORT        1761
#define IDS_SECTION_IMPORT_TIP    1762
#define IDS_SECTION_EXPORT_TIP    1763

// ------------------------------------------------------- start locations
/* Asked at save time, when the map has units but too few places to start. */
#define IDS_STARTS_MISSING        1770
#define IDS_STARTS_MISSING_TITLE  1771

// -------------------------------------------------- switching a player's race
/* Said after the player sheet changed a race and the units followed. */
#define IDS_RACE_FOLLOWED         1780

// ------------------------------------------------------ keyboard help, redux
/* Retired with the rest of the keyboard help: see 1450 above. */

// ------------------------------------------- putting the client back to new
#define IDS_RESET_TITLE           1853
#define IDS_RESET_CONFIRM         1854
#define IDS_RESET_DONE            1855

// ------------------------------------------- what a scatter took away with it
/* Asked when a map is opened holding units the game could not place. The
   title is IDS_CHECK_TITLE: it is the same check, said at another moment.
   1858 was a second copy of that title and is left unused. */
/* The movement palette's last cell: not a class but the absence of one. */
#define IDS_MOVE_FROM_TERRAIN      1863
/* The status bar in movement mode: which tool is in hand, and its chords. */
#define IDS_TOOL_WALKABLE          1864
#define IDS_HINT_MOVEMENT          1865

#define IDS_MISPLACED_ONE          1859
#define IDS_MISPLACED_MANY         1860
#define IDS_MISPLACED_REMOVED_ONE  1861
#define IDS_MISPLACED_REMOVED_MANY 1862

#define IDS_DECORATED_REMOVED_ONE  1856
#define IDS_DECORATED_REMOVED_MANY 1857

// ------------------------------------------- the tool and the hint cell
/* Which tool is in hand, in its own cell beside the hints. */
#define IDS_TOOL_PAINTING         1805
#define IDS_TOOL_RECT             1806
#define IDS_TOOL_PLACING          1807
#define IDS_TOOL_PICKING          1808
#define IDS_TOOL_PASTING          1809
/* One line of keyboard help for the tool in hand, in its own status cell. */
#define IDS_HINT_TERRAIN_PAINT    1810
#define IDS_HINT_TERRAIN_RECT     1811
#define IDS_HINT_UNIT_PLACE       1812
#define IDS_HINT_UNIT_SELECT      1813
#define IDS_HINT_PASTE            1814
/* Right-clicking a property row offers to put that one value back. */
#define IDS_FORM_RESET            1815
#define IDS_FORM_RESET_ONE        1816
/* Right-clicking a cell of the unit palette. %ls is that unit's name. */
#define IDS_UNIT_PROPS_FOR        1817
/* The mark on a list row this map changed, beside the accent colour. */
#define IDS_CHANGED_MARK          1818
/* Right-clicking a list row offers to put that whole subject back. %ls is the
   unit or upgrade named in the row. */
#define IDS_ROW_RESET             1819
#define IDS_ROW_RESET_DONE        1820

// ------------------------------------------------------ restrictions, redux
/* 1800 and 1801 said which of the two states the map was in, for a read-only
   line that is a tick box now. Left unused rather than reassigned. */
/* The two ways the section itself moves, as opposed to a tick inside it. Here
   rather than beside the other IDS_ALOW ids because that block ends at 1289. */
#define IDS_ALOW_CLEARED          1802
#define IDS_ALOW_ADDED            1803

// ------------------------------------------------------------------ help
/* Said only when the guide could not be handed to a browser, which is a
   temp directory that cannot be written or a machine with nothing
   registered for .html. */
#define IDS_GUIDE_FAILED          1830

// ------------------------------------------------------- the bucket
/* Shift over the bucket repaints that terrain everywhere in scope, so it
   reports a count the way the other bulk edits do. */
#define IDS_FILLED_EVERYWHERE     1831

// ------------------------------------------------------------ generator
/* An octave read as the width of what it makes rather than as its scale:
   the number the core wants is features per tile, which is not a thing
   anybody looks at a map and sees. */
#define IDS_GEN_FEATURE_WIDTH     1837
/* 1838 labelled the AI note on the player page. The note is bare prose under
   the dropdown now and carries no label, so the id is left unused. */
/* The companion button beside the AI dropdown. It opens the browser on the
   script the dropdown is showing, so the caption names the window. */
#define IDS_TIP_VIEW_AI           1839
/* The bucket says its own name and its own chords: it fills a region, and
   Shift makes that every tile of the terrain instead. */
#define IDS_TOOL_FILLING          1849
#define IDS_HINT_TERRAIN_FILL     1850

// ------------------------------------------------------ the Units menu
/* The palette shouts its headings and a menu item is Title Case, so the Units
   menu spells its groups out rather than borrowing the palette's. Same words,
   different voice; see the note at the top of Strings.rc. */
#define IDS_MENU_RACE_HUMAN       1840
#define IDS_MENU_RACE_ORC         1841
#define IDS_MENU_KIND_LAND        1842
#define IDS_MENU_KIND_AIR         1843
#define IDS_MENU_KIND_WATER       1844
#define IDS_MENU_KIND_BUILDINGS   1845
#define IDS_MENU_KIND_HEROES      1846
#define IDS_MENU_GROUP_NEUTRAL    1847
#define IDS_MENU_GROUP_MARKERS    1848

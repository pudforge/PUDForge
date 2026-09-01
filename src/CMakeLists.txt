# Build for the shared core, its tests and the Windows client.
#
# The core is platform-independent C++17 and builds anywhere; the client is
# guarded on WIN32 and builds with MSVC, or cross-compiled with
# cmake/mingw-w64.cmake.
#
#   cmake -S src -B build && cmake --build build && ./build/pf_tests

cmake_minimum_required(VERSION 3.16)
project(pudforge LANGUAGES CXX)

if(MSVC)
  # fopen is portable and used correctly; MSVC's "unsafe" advice would
  # otherwise bury real warnings.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # /MT rather than /MD: the difference between "unzip and run" and "install
  # the Visual C++ redistributable first", and the second one loses people.
  # It has to be set before any target is declared rather than on the client
  # alone — the runtime is recorded in every object file, and linking a /MD
  # core into a /MT executable is a hard LNK2038 rather than a warning.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # /utf-8: the sources are UTF-8 and have no BOM, and MSVC reads a BOM-less
  # file as the system ANSI code page. GCC assumes UTF-8, so this only ever
  # breaks here — and it breaks silently, in the output rather than the build.
  # The terrain panel's shape and mirror glyphs (L"●", L"↔") reached the screen
  # as "â—" and "â†”" until this line existed. It sets the execution charset
  # too, which is what the core already assumes: every narrow literal it hands
  # across the C ABI is UTF-8 and every client calls FromUtf8 on it.
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(pudforge STATIC
  PUDForgeCore/pud.cpp
  PUDForgeCore/tbl.cpp
  PUDForgeCore/terrain.cpp
  PUDForgeCore/view.cpp
  PUDForgeCore/art.cpp
  PUDForgeCore/constants.cpp
  PUDForgeCore/default_data.cpp
  PUDForgeCore/mpq.cpp
  PUDForgeCore/overrides/portrait_frames.cpp
  PUDForgeCore/overrides/hidden_units.cpp
  PUDForgeCore/overrides/named_heroes.cpp
  PUDForgeCore/overrides/race_counterparts.cpp
  PUDForgeCore/overrides/movement_classes.cpp
  PUDForgeCore/overrides/alow_upgrade_bits.cpp
  PUDForgeCore/overrides/alow_defaults.cpp
  PUDForgeCore/overrides/player_slots.cpp
  PUDForgeCore/overrides/neutral_owners.cpp
  PUDForgeCore/overrides/unit_sounds.cpp
  PUDForgeCore/overrides/ai_scripts.cpp
  PUDForgeCore/overrides/terrain_names.cpp
  PUDForgeCore/overrides/flat_colours.cpp
  PUDForgeCore/overrides/editing_policy.cpp
  PUDForgeCore/overrides/hall_clearance.cpp
  PUDForgeCore/overrides/tile_variations.cpp
  PUDForgeCore/overrides/ai_opcodes.cpp
  PUDForgeCore/overrides/unit_footprints.cpp
  PUDForgeCore/overrides/shared_tiles.cpp
  PUDForgeCore/ai_script.cpp
  PUDForgeCore/noise.cpp
  PUDForgeCore/png.cpp
  PUDForgeCore/capi.cpp
)
target_include_directories(pudforge
  PUBLIC  PUDForgeCore/include
  PRIVATE PUDForgeCore
)

enable_testing()

# One translation unit per subject area rather than one big file. As a single
# 3,200-line file the tests took 11 s to compile against 4 s to run, so editing
# a test cost three times as much as running the whole suite. Split, only the
# edited file rebuilds and the rest overlap under -j.
add_executable(pf_tests
  Tests/harness.cpp
  Tests/test_format.cpp
  Tests/test_terrain.cpp
  Tests/test_units.cpp
  Tests/test_art.cpp
  Tests/test_corpus.cpp
  Tests/test_api.cpp
  Tests/test_editor.cpp
  # The Windows client's editor state is deliberately Win32-free, which is
  # what lets the client's behaviour be tested on machines with no MSVC.
  PUDForgeWin/Editor.cpp
)
target_link_libraries(pf_tests PRIVATE pudforge)
target_include_directories(pf_tests PRIVATE PUDForgeCore)
# Register each group separately so `ctest -j` runs them in parallel, in their
# own processes. Per-test registration would be finer but slower: every process
# rediscovers the map corpus, and that walk costs more than most single tests.
foreach(group format terrain units art api editor)
  add_test(NAME ${group}
           COMMAND pf_tests "${CMAKE_CURRENT_SOURCE_DIR}/.." --group ${group})
endforeach()

# The corpus group is 85 % of the suite's runtime — it walks every shipped map
# several times over — so as one job it sets the floor for a parallel run no
# matter how many cores are free. Split into shards it runs alongside itself.
# The count is one number so shards always tile the group exactly.
set(PF_CORPUS_SHARDS 3)
math(EXPR pf_last_shard "${PF_CORPUS_SHARDS} - 1")
foreach(shard RANGE ${pf_last_shard})
  add_test(NAME corpus.${shard}
           COMMAND pf_tests "${CMAKE_CURRENT_SOURCE_DIR}/.."
                   --group corpus --shard ${shard}/${PF_CORPUS_SHARDS})
endforeach()

# ------------------------------------------------------------ Windows client
#
# Guarded so a configure on macOS or Linux still works: the core, the tests and
# the wasm build are cross-platform and this is the only Windows-only target.
#
# Builds two ways: MSVC on Windows, or cross-compiled from anywhere with
# cmake/mingw-w64.cmake — which is how it was first compiled, run and debugged,
# under Wine, before ever touching a Windows machine. That file says how.
if (WIN32)
  # ---------------------------------------------------------- artwork
  #
  # Three things the resources embed are generated: the app icon
  # (app-icon-src.png -> app.ico), the button sheet (ui-icons.png ->
  # ui-icons.bmp), and the user guide (docs/user_guide.md -> user-guide.html).
  # The PNGs and the Markdown are the originals; the .ico, .bmp and .html are
  # build output that happens to be checked in, and editing one of those by hand
  # is editing the wrong file.
  #
  # Rebuilt whenever the PNG is newer, so drawing an icon and pressing build
  # is the whole of the work. Before this the conversion was a script somebody
  # had to remember, which meant the exe could ship yesterday's drawing while
  # the PNG in the tree showed today's — a stale artefact that looks exactly
  # like a bug in the loader.
  #
  # Converted rather than embedded raw because rc.exe understands .ico and
  # .bmp and understands no PNG, and the core has a PNG encoder but no decoder.
  #
  # Why the outputs stay checked in: this needs PowerShell and System.Drawing,
  # so it cannot run on the cross-build (MinGW under Wine, which is how this
  # client was first compiled). Where there is no PowerShell the checked-in
  # .ico and .bmp are used as they are and the build says so once.
  find_program(PF_POWERSHELL NAMES pwsh powershell)
  if (PF_POWERSHELL)
    set(pf_win_dir "${CMAKE_CURRENT_SOURCE_DIR}/PUDForgeWin")
    # One entry per conversion: source PNG, script, generated file.
    set(pf_art_outputs)
    foreach(pf_art "app-icon-src.png|make-icon.ps1|app.ico"
                   "ui-icons.png|make-ui-icons.ps1|ui-icons.bmp")
      string(REPLACE "|" ";" pf_art_parts "${pf_art}")
      list(GET pf_art_parts 0 pf_art_png)
      list(GET pf_art_parts 1 pf_art_script)
      list(GET pf_art_parts 2 pf_art_out)
      add_custom_command(
        OUTPUT "${pf_win_dir}/${pf_art_out}"
        COMMAND "${PF_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/${pf_art_script}"
                -Sheet "${pf_win_dir}/${pf_art_png}"
                -Out "${pf_win_dir}/${pf_art_out}"
        # The script itself is a dependency as well as the drawing: changing
        # how a cell is cut has to rebuild the artefact just as redrawing does.
        DEPENDS "${pf_win_dir}/${pf_art_png}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/${pf_art_script}"
        COMMENT "Converting ${pf_art_png} -> ${pf_art_out}"
        VERBATIM)
      list(APPEND pf_art_outputs "${pf_win_dir}/${pf_art_out}")
    endforeach()

    # The user guide is the same arrangement with a source that lives somewhere
    # else: it is written as Markdown in docs/ because that is where a person
    # edits and reviews it, and the exe embeds the HTML built from it. Its own
    # command rather than another row in the loop above, which assumes the
    # source sits beside the .rc.
    #
    # Every path is absolute, the icon included. The script runs with the build
    # directory as its working directory, so a relative default resolves to
    # somewhere that does not exist and the page loses its icon without failing.
    add_custom_command(
      OUTPUT "${pf_win_dir}/user-guide.html"
      COMMAND "${PF_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
              -File "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/make-help.ps1"
              -Sheet "${CMAKE_CURRENT_SOURCE_DIR}/../docs/user_guide.md"
              -Icon "${pf_win_dir}/app-icon-src.png"
              -Out "${pf_win_dir}/user-guide.html"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../docs/user_guide.md"
              "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/make-help.ps1"
              "${pf_win_dir}/app-icon-src.png"
      COMMENT "Converting user_guide.md -> user-guide.html"
      VERBATIM)
    list(APPEND pf_art_outputs "${pf_win_dir}/user-guide.html")
    # A target rather than listing the outputs as sources of the client: what
    # consumes them is the resource compiler, and CMake cannot see inside a
    # .rc to know that. An ordering dependency is the honest way to say it.
    add_custom_target(PUDForgeArt DEPENDS ${pf_art_outputs})
  else()
    message(STATUS
      "No PowerShell found: using the checked-in app.ico and ui-icons.bmp "
      "as they are. Re-run scripts/make-icon.ps1 and scripts/make-ui-icons.ps1 "
      "on a Windows machine after changing either PNG.")
  endif()

  # Report an Issue is the only thing in this client that opens a socket, and
  # this compiles it out rather than hiding it. v0.1.66 was the first build to
  # import WINHTTP.dll at all, and it is the first anybody was told was a
  # virus; the question is whether a build carrying neither the import nor the
  # hostname stops being called one. A feature left in but unreachable would
  # not answer that, because the scanner reads the file and not the menu.
  # docs/releasing.md has the measurements. Back to ON once this is settled.
  option(PF_ENABLE_REPORT "Build Help > Report an Issue, which posts over HTTPS" OFF)

  add_executable(PUDForgeWin WIN32
    PUDForgeWin/main.cpp
    PUDForgeWin/Capture.cpp
    PUDForgeWin/Dialogs.cpp
  PUDForgeWin/Strings.cpp
    PUDForgeWin/MapWindow.cpp
    PUDForgeWin/GameData.cpp
    PUDForgeWin/Editor.cpp
    PUDForgeWin/Form.cpp
    PUDForgeWin/Icons.cpp
    PUDForgeWin/Log.cpp
    PUDForgeWin/Radial.cpp
    PUDForgeWin/Tools.cpp
    PUDForgeWin/PaletteGrid.cpp
    PUDForgeWin/Docks.cpp
    PUDForgeWin/Minimap.cpp
    PUDForgeWin/Toolbar.cpp
    PUDForgeWin/UiIcons.cpp
    PUDForgeWin/Sounds.cpp
    PUDForgeWin/PUDForge.rc)
  target_include_directories(PUDForgeWin PRIVATE
    PUDForgeCore
    PUDForgeCore/include)
  # The client is a Unicode application and says so, rather than relying on the
  # toolchain to assume it. Visual Studio defines these for new projects and
  # MinGW does not, and the difference is not a compile error: the generic
  # macros quietly resolve to their ANSI forms, so SB_SETTEXT becomes
  # SB_SETTEXTA and a wide string handed to it shows up as its first letter.
  # That is what the status bar did until this line existed.
  # NOMINMAX for the same reason: windows.h defines min and max as macros, so
  # every std::max( in the client parses as a macro call with a stray '::' in
  # front of it. MinGW's headers leave them out, so this only ever breaks on
  # MSVC — and it breaks in every file that reaches for <algorithm>.
  target_compile_definitions(PUDForgeWin PRIVATE UNICODE _UNICODE NOMINMAX)
  # The resource scripts are UTF-8 and both resource compilers assume the
  # system codepage unless told. Without this an em dash in Strings.rc reaches
  # the screen as three characters — which is what the palette headings and the
  # title bar did the moment their text moved out of the .cpp files, where the
  # C++ compiler had been reading them as UTF-8 all along.
  if (MSVC)
    string(APPEND CMAKE_RC_FLAGS " /c65001")
  else()
    string(APPEND CMAKE_RC_FLAGS " --codepage=65001")
  endif()
  # msimg32 is AlphaBlend, and only AlphaBlend: it is declared in wingdi.h with
  # everything else but lives in its own import library, so a blended blit
  # links nowhere without it (see BlitRgbaBlended in Blit.hpp).
  # winmm: PlaySound, for the unit voices. The clips are the game's own bytes,
  # so nothing is decoded here and no third-party audio library is involved.
  target_link_libraries(PUDForgeWin PRIVATE pudforge comctl32 shell32 ole32 shlwapi msimg32 winmm)
  # The source, the library and the definition move together: the definition
  # reaches the resource compiler too, which is what drops the menu item, the
  # dialog and its strings, so an off build carries none of the words either.
  if (PF_ENABLE_REPORT)
    target_sources(PUDForgeWin PRIVATE PUDForgeWin/Report.cpp)
    target_link_libraries(PUDForgeWin PRIVATE winhttp)
    target_compile_definitions(PUDForgeWin PRIVATE PF_ENABLE_REPORT=1)
  endif()
  set_target_properties(PUDForgeWin PROPERTIES OUTPUT_NAME PUDForge)
  # Before the resource compiler runs, so the .rc always embeds artwork built
  # from the PNGs as they are now.
  if (TARGET PUDForgeArt)
    add_dependencies(PUDForgeWin PUDForgeArt)
  endif()
  if (MSVC)
    # PUDForge.rc embeds the manifest itself, as resource 1, which is the only
    # way MinGW can do it. MSVC's linker generates one as well and then refuses
    # to merge two: CVT1100, duplicate resource. Keep the one that is checked
    # in — it is the same file both toolchains read — and turn the other off.
    target_link_options(PUDForgeWin PRIVATE /MANIFEST:NO)
  endif()
  if (MINGW)
    # The entry point is wWinMain. MSVC's CRT finds it by itself; MinGW links
    # the ANSI startup (expecting WinMain) unless told the app is Unicode.
    # Target-scoped: pf_tests has a plain main and must not inherit this.
    target_link_options(PUDForgeWin PRIVATE -municode)
  endif()

  # The same binary under a second name, for anyone who wants to *run* the
  # editor while still being able to build it.
  #
  # Windows holds an executable open for as long as it is running, so a live
  # PUDForge.exe fails the next link with LNK1104 and there is nothing the
  # build can do about it. Running this copy instead leaves PUDForge.exe free.
  # A copy rather than a second compile: it is the same code, and building the
  # client twice to get two names would double the slowest target in the tree.
  add_custom_target(PUDForgeTest ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:PUDForgeWin>
            $<TARGET_FILE_DIR:PUDForgeWin>/PUDForgeTest.exe
    DEPENDS PUDForgeWin
    COMMENT "Copying PUDForge.exe to PUDForgeTest.exe")
endif()

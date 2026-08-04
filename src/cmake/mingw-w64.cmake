# Cross-compile the Windows client from Linux with MinGW-w64.
#
#   cmake -S src -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-win --target PUDForgeWin -j
#
# This exists so the Win32 layer can be *compiled* without a Windows machine —
# which is most of what "the first hour on Windows" was budgeted for. MinGW's
# Win32 headers are strict about the same things MSVC is (missing includes,
# wrong argument types, W/A mismatches), so a clean build here removes most of
# that hour. It does not replace running on Windows: MSVC still differs on a
# few conversions, and behaviour needs real hardware.
#
# MinGW links against msvcrt/ucrt import libraries, so the "/MT static runtime"
# property in CMakeLists.txt does not apply; -static below is its equivalent,
# keeping the .exe free of libgcc/libstdc++ DLL dependencies.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

add_link_options(-static)
# Windows 10, matching docs/windows-plan.md ("Windows 10 1809 or later"). This
# is also what MSVC's SDK defaults to, so the two toolchains see the same
# declarations — the per-monitor DPI functions the client calls directly
# (GetDpiForWindow, SetProcessDpiAwarenessContext) only exist at this level.
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)

// Headless rendering to a PNG.
//
//   PUDForge.exe --render map.pud [--out shot.png] [--tiles x,y,w,h]
//                [--scale n] [--grid] [--no-units] [--overlay n]
//   PUDForge.exe --tilesheet <0-3> [--out sheet.png]
//   PUDForge.exe --version
//
// A PNG written without creating a window is checkable over SSH, in CI, and by
// a cross-build running under Wine — which matters more here than on macOS,
// because this client is developed and tested by people who may not be sitting
// at Windows.
//
// Composition goes through the same `pf_map_compose_region` the canvas paints
// with, so a capture is evidence about the editor rather than about a second
// renderer that happens to agree today.
//
// No window, no message loop, no window server: this runs and exits before
// WinMain creates anything. Diagnostics go to the parent console when there is
// one — a GUI-subsystem exe has no stdout of its own — and to the exit code
// always.

#pragma once

#include <windows.h>

namespace pfwin {

/// Whether the command line asks for a headless capture.
bool WantsCapture(int argc, wchar_t** argv);

/// Do the capture. Returns the process exit code; 0 is success.
int RunCapture(int argc, wchar_t** argv);

}  // namespace pfwin

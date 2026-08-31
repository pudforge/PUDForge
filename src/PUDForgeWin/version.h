// The client's version, written once.
//
// Four places show it — the title bar, Help > About, `--version`, and the
// VERSIONINFO block Explorer reads — and the resource compiler preprocesses, so
// PUDForge.rc includes this header the same way the sources do.
//
// This is the *client's* version and not `pf_version()`, which is the core's.
// Only this one is shown: the two moved together in practice, and a window
// quoting two numbers asked every bug report to explain which was which.
// `--version` still prints both, where a machine may be reading.
//
// Bump the patch on every commit, the minor on a release worth naming. A version
// that only moves when someone remembers makes a stale build look current.
// CI reads PF_APP_VERSION_STR below and cuts a release the first time it sees a
// version it has no tag for, so a bump is what ships a build.

#pragma once

#define PF_APP_VERSION_MAJOR 0
#define PF_APP_VERSION_MINOR 1
#define PF_APP_VERSION_PATCH 63

// Spelled out rather than stringised from the three numbers above: rc.exe's
// preprocessor does not do the two-level stringisation that would build it, and
// a VALUE expanding to something other than a string literal is a syntax error.
#define PF_APP_VERSION_STR "0.1.63"
#define PF_APP_VERSION_WSTR L"0.1.63"

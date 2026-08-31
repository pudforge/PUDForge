// Report an Issue, from the Help menu.

#pragma once

#include <windows.h>

namespace pfwin {

/// Ask what happened and send it to the issue list. Modal; says how it went.
///
/// Nothing secret is compiled in. It posts to the same Worker the Discord
/// command uses, and that holds the GitHub token - see Report.cpp for why the
/// alternative is not an alternative.
void ShowReportIssue(HWND owner, HINSTANCE instance);

}  // namespace pfwin

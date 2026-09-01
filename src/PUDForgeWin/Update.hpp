// Check for Updates, from the Help menu and once a day at start-up.
//
// The exe is the whole installation, so an update is one file fetched from
// the GitHub release and put where the running one is. A running exe cannot
// be overwritten on Windows, but it can be renamed, so the old one is moved
// aside and the new one copied into its place; the next start tidies the
// leftover. When the folder is one the person cannot write — the game's own
// folder under Program Files, which the release notes recommend — the copy is
// done by the new exe itself, run elevated with --install-update, so Windows
// asks for permission once and the editor never runs elevated itself.

#pragma once

#include <windows.h>

#include <string>

namespace pfwin {

/// What a check found. Made on the check's thread and handed to the window in
/// the message's LPARAM, which owns it from then on.
struct UpdateResult {
  bool ok = false;        ///< the feed was read; otherwise `why` says what happened
  bool quiet = false;     ///< a start-up check, which says nothing unless there is news
  std::wstring version;   ///< the newest release's
  std::wstring exe_url;
  std::wstring page_url;
  std::wstring notes;     ///< plain text with Windows line ends, for an edit control
  std::string sha256;     ///< as the notes state it, or empty
  std::wstring why;
};

/// Read the releases feed on a thread of its own and post `message` to
/// `notify` with an UpdateResult* in LPARAM. Off the window's thread because
/// a check at start-up must not hold the window up for as long as a bad
/// connection takes to say so.
void StartUpdateCheck(HWND notify, UINT message, bool quiet);

/// Whether a release is newer than the build that is running.
bool IsNewerThanThis(const std::wstring& version);

enum class UpdateChoice {
  kLater,            ///< nothing done; ask again next time
  kSkip,             ///< nothing done; do not mention this version again
  kRestart,          ///< installed, and the person wants it now
  kInstalledLater,   ///< installed; it runs next time the editor starts
};

/// The update window: what is new, and Update Now / Later / Skip. Update Now
/// downloads, checks the hash the notes state, installs, and then offers the
/// restart. Modal.
UpdateChoice OfferUpdate(HWND owner, HINSTANCE instance, const UpdateResult& found);

/// The running exe's own path.
std::wstring ThisExe();

/// Start `exe` afresh as the person, not elevated, and without waiting.
bool LaunchExe(const std::wstring& exe);

/// Delete the exe a previous update renamed aside, if it is still there. Quiet
/// when it is not, or is still in use.
void RemoveOldExe();

/// `--install-update <target>`: copy this exe over `target`. The elevated half
/// of an update, run from the downloaded file; the exit code is the Win32
/// error, so the caller can say why when it fails.
bool WantsInstallUpdate(int argc, wchar_t** argv);
int RunInstallUpdate(int argc, wchar_t** argv);

/// Today, as a day count, for "once a day".
int DayNumber();

}  // namespace pfwin

// Everything the client has said, kept.
//
// The status bar holds one line and then loses it, and the ones worth reading
// twice are exactly the ones that scroll away: how many units a paste could not
// place, what a bulk edit actually touched.
//
// Deliberately not a debug log. It records what was said to the user, in the
// user's own words, which is what makes it worth showing them.

#pragma once

#include <windows.h>

#include <deque>
#include <string>

namespace pfwin {

class Log {
 public:
  struct Entry {
    std::wstring at;      ///< local time, as the user's locale writes it
    std::wstring text;
    bool warn = false;
  };

  /// The one log. A free function rather than a member of App because the things
  /// worth logging happen further down than App can see.
  static Log& The();

  void Add(const std::wstring& text, bool warn);
  void Clear() { entries_.clear(); }
  const std::deque<Entry>& entries() const { return entries_; }

  /// The whole log as one block of text, newest last, for the window and for the
  /// clipboard. CRLF, because both of those want it.
  std::wstring AsText() const;

 private:
  /// Enough to cover a working session without growing without bound. Older
  /// lines fall off the front, which is the end nobody scrolls back to.
  static constexpr size_t kMax = 500;
  std::deque<Entry> entries_;
};

/// Show the log, with Copy and Clear.
void ShowLog(HWND owner, HINSTANCE instance);

}  // namespace pfwin

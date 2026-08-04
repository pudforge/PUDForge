#include "Log.hpp"

#include <vector>

#include "Dialogs.hpp"   // CentreOnScreen, which every modal here shares
#include "Strings.hpp"
#include "resource.h"
#include "strings.h"

namespace pfwin {
namespace {

/// The time, the way this machine's locale writes it. Not ISO: the audience is
/// whoever is sitting here, and they read their own clock faster.
std::wstring NowText() {
  wchar_t buffer[64] = {};
  GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, nullptr, buffer, 64);
  return buffer;
}

/// The window is modeless-in-spirit but modal in fact: it is a thing you open,
/// read and close, and a modal dialog is the cheapest correct way to do that.
INT_PTR CALLBACK LogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      CentreOnScreen(dialog);
      const std::wstring text = Log::The().AsText();
      SetDlgItemTextW(dialog, IDC_LOG_TEXT, text.c_str());
      // Newest last, so the useful end is the one in view.
      HWND edit = GetDlgItem(dialog, IDC_LOG_TEXT);
      SendMessageW(edit, EM_SETSEL, WPARAM(text.size()), LPARAM(text.size()));
      SendMessageW(edit, EM_SCROLLCARET, 0, 0);
      SetFocus(GetDlgItem(dialog, IDOK));
      return FALSE;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id == IDC_LOG_COPY) {
        const std::wstring text = Log::The().AsText();
        // The whole log, not the selection: somebody copying this is pasting it
        // into a bug report, and a partial log is worse than none.
        if (OpenClipboard(dialog)) {
          EmptyClipboard();
          const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
          if (HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* to = GlobalLock(block)) {
              memcpy(to, text.c_str(), bytes);
              GlobalUnlock(block);
              if (!SetClipboardData(CF_UNICODETEXT, block)) GlobalFree(block);
            } else {
              GlobalFree(block);
            }
          }
          CloseClipboard();
        }
        return TRUE;
      }
      if (id == IDC_LOG_CLEAR) {
        Log::The().Clear();
        SetDlgItemTextW(dialog, IDC_LOG_TEXT, L"");
        return TRUE;
      }
      if (id == IDOK || id == IDCANCEL) { EndDialog(dialog, id); return TRUE; }
      return FALSE;
    }
    default:
      return FALSE;
  }
}

}  // namespace

Log& Log::The() {
  static Log log;
  return log;
}

void Log::Add(const std::wstring& text, bool warn) {
  if (text.empty()) return;
  // The same line twice running is almost always one action repeating rather
  // than two things happening, and a log of "Grass" forty times tells nobody
  // anything.
  if (!entries_.empty() && entries_.back().text == text) return;
  entries_.push_back({NowText(), text, warn});
  while (entries_.size() > kMax) entries_.pop_front();
}

std::wstring Log::AsText() const {
  std::wstring out;
  for (const Entry& entry : entries_) {
    // A warning is marked rather than coloured: a read-only edit control has one
    // colour, and the mark survives being pasted into a bug report.
    out += Format(entry.warn ? IDS_LOG_LINE_WARN : IDS_LOG_LINE,
                  entry.at.c_str(), entry.text.c_str());
    out += L"\r\n";
  }
  return out;
}

void ShowLog(HWND owner, HINSTANCE instance) {
  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LOG), owner, LogProc, 0);
}

}  // namespace pfwin

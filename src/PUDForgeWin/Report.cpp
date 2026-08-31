// Report an Issue: what a user types here becomes an issue on the repo.
//
// It posts to the same Worker the Discord command talks to, which holds the
// GitHub token and files the issue. Nothing secret ships in the exe, which is
// the whole reason it goes the long way round: a token compiled into a program
// people download is a token those people have, and not committing it to git
// does nothing about that - the binary is the distribution.
//
// So the exe carries a URL. The worst anybody can do with it is file issues,
// which is what it is for, and the answer to abuse is a change at the Worker
// rather than a key to revoke and a release to push.

#include "Report.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>

#include <string>

#include "Log.hpp"
#include "Strings.hpp"
#include "resource.h"
#include "strings.h"
#include "version.h"

#pragma comment(lib, "winhttp.lib")

namespace pfwin {
namespace {

// The Worker's own address. Public on purpose; see the note at the top.
constexpr wchar_t kHost[] = L"pudforge-feedback.pudforge.workers.dev";
constexpr wchar_t kPath[] = L"/report";

/// Wide to UTF-8, because that is what the wire wants and what the issue is.
std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                                    nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n,
                      nullptr, nullptr);
  return out;
}

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()),
                                    nullptr, 0);
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), n);
  return out;
}

/// A JSON string, escaped. Hand-rolled because one object with three members is
/// not worth a JSON library, and everything that goes in it is text somebody
/// typed - so it is escaped properly rather than hopefully.
std::string JsonString(const std::string& text) {
  std::string out = "\"";
  for (const unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += char(c);
        }
    }
  }
  out += '"';
  return out;
}

/// Pull one string member out of the Worker's answer.
///
/// Enough of a parser for two known keys and no more. The Worker is ours and
/// answers `{"url": ...}` or `{"error": ...}`; anything else is reported as the
/// raw body, which is more use to somebody debugging than "unexpected reply".
std::string JsonMember(const std::string& body, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t at = body.find(needle);
  if (at == std::string::npos) return {};
  at = body.find(':', at + needle.size());
  if (at == std::string::npos) return {};
  at = body.find('"', at);
  if (at == std::string::npos) return {};
  std::string out;
  for (size_t i = at + 1; i < body.size(); i++) {
    if (body[i] == '\\' && i + 1 < body.size()) {
      const char c = body[++i];
      out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
      continue;
    }
    if (body[i] == '"') break;
    out += body[i];
  }
  return out;
}

/// POST the report. `answer` receives the issue's URL, or the reason it failed.
/// @return whether the issue was filed
bool Post(const std::string& body, std::wstring& answer) {
  // Named after the program and its version, so a stuck client can be told from
  // a current one in a log somewhere without asking the person.
  const std::wstring agent = std::wstring(L"PUDForge/") + PF_APP_VERSION_WSTR;
  HINTERNET session = WinHttpOpen(agent.c_str(),
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) { answer = L"No network."; return false; }

  // Twenty seconds all told. Long enough for a slow line, short enough that
  // somebody does not conclude the program has hung.
  WinHttpSetTimeouts(session, 10000, 10000, 10000, 20000);

  bool sent = false;
  HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (connection) {
    HINTERNET request = WinHttpOpenRequest(
        connection, L"POST", kPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request) {
      const wchar_t kType[] = L"Content-Type: application/json\r\n";
      if (WinHttpSendRequest(request, kType, DWORD(-1),
                             const_cast<char*>(body.data()), DWORD(body.size()),
                             DWORD(body.size()), 0) &&
          WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);

        std::string reply;
        for (;;) {
          DWORD available = 0;
          if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
          std::string chunk(size_t(available), '\0');
          DWORD read = 0;
          if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
          reply.append(chunk, 0, size_t(read));
        }

        const std::string url = JsonMember(reply, "url");
        if (status >= 200 && status < 300 && !url.empty()) {
          answer = Wide(url);
          sent = true;
        } else {
          const std::string why = JsonMember(reply, "error");
          answer = why.empty() ? Wide(reply) : Wide(why);
          if (answer.empty()) answer = L"The server said " + std::to_wstring(status) + L".";
        }
      } else {
        answer = L"Could not reach the server.";
      }
      WinHttpCloseHandle(request);
    }
    WinHttpCloseHandle(connection);
  } else {
    answer = L"Could not reach the server.";
  }
  WinHttpCloseHandle(session);
  return sent;
}

/// The task dialog's own callback, so a clicked link opens.
///
/// A hyperlink in a task dialog is not a control that does anything by itself.
/// The dialog reports the click and the program decides; which is the right way
/// round, because "open this in a browser" is a decision and not a side effect.
HRESULT CALLBACK ReportLinkProc(HWND, UINT message, WPARAM, LPARAM lparam,
                                LONG_PTR) {
  if (message == TDN_HYPERLINK_CLICKED) {
    ShellExecuteW(nullptr, L"open", reinterpret_cast<const wchar_t*>(lparam),
                  nullptr, nullptr, SW_SHOWNORMAL);
  }
  return S_OK;
}

/// Say it worked, with the issue's address as something to click.
///
/// A message box would show the same URL as dead text, and an address somebody
/// has to retype is an address nobody visits. A task dialog carries links, so
/// this is one.
void SaySent(HWND owner, const std::wstring& url) {
  // The anchor is markup rather than prose, so it is built here; the sentences
  // around it are in the string table where a translator can reach them.
  const std::wstring content =
      Str(IDS_REPORT_SENT_FOLLOW) + L"\n<a href=\"" + url + L"\">" + url + L"</a>";
  const std::wstring title = Str(IDS_REPORT_TITLE);
  const std::wstring main = Str(IDS_REPORT_SENT_MAIN);

  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
  config.dwCommonButtons = TDCBF_OK_BUTTON;
  config.pszWindowTitle = title.c_str();
  config.pszMainIcon = TD_INFORMATION_ICON;
  config.pszMainInstruction = main.c_str();
  config.pszContent = content.c_str();
  config.pfCallback = ReportLinkProc;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(url.c_str());

  // Falls back to a message box if the task dialog cannot be had - an old
  // comctl32, or a manifest that did not take. The link is not clickable then,
  // which is worse but not useless.
  if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr))) {
    MessageBoxW(owner, Format(IDS_REPORT_SENT, url.c_str()).c_str(),
                title.c_str(), MB_OK | MB_ICONINFORMATION);
  }
}

INT_PTR CALLBACK ReportProc(HWND dialog, UINT message, WPARAM wparam, LPARAM) {
  switch (message) {
    case WM_INITDIALOG: {
      CheckDlgButton(dialog, IDC_REPORT_LOG, BST_CHECKED);
      SetDlgItemTextW(dialog, IDC_REPORT_VERSION,
                      Format(IDS_REPORT_VERSION, PF_APP_VERSION_WSTR).c_str());
      SetDlgItemTextW(dialog, IDC_REPORT_NOTE, Str(IDS_REPORT_HINT).c_str());
      SetFocus(GetDlgItem(dialog, IDC_REPORT_TEXT));
      return FALSE;   // focus set by hand
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
      if (id != IDOK) return FALSE;

      wchar_t typed[4096] = {};
      GetDlgItemTextW(dialog, IDC_REPORT_TEXT, typed, ARRAYSIZE(typed));
      std::wstring said(typed);
      while (!said.empty() && iswspace(said.back())) said.pop_back();
      if (said.empty()) {
        SetDlgItemTextW(dialog, IDC_REPORT_NOTE, Str(IDS_REPORT_EMPTY).c_str());
        SetFocus(GetDlgItem(dialog, IDC_REPORT_TEXT));
        return TRUE;
      }

      std::string body = "{\"summary\":" + JsonString(Utf8(said));
      body += ",\"version\":" + JsonString(Utf8(PF_APP_VERSION_WSTR));
      if (IsDlgButtonChecked(dialog, IDC_REPORT_LOG) == BST_CHECKED) {
        body += ",\"log\":" + JsonString(Utf8(Log::The().AsText()));
      }
      body += "}";

      // Sent on this thread, which stops the window for as long as it takes.
      // A wait cursor and a disabled button rather than a background thread:
      // the window has nothing else to do until it knows, and a person who
      // pressed Send once should not be able to press it four times.
      EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
      EnableWindow(GetDlgItem(dialog, IDCANCEL), FALSE);
      SetDlgItemTextW(dialog, IDC_REPORT_NOTE, Str(IDS_REPORT_SENDING).c_str());
      HCURSOR was = SetCursor(LoadCursorW(nullptr, IDC_WAIT));

      std::wstring answer;
      const bool sent = Post(body, answer);

      SetCursor(was);
      EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
      EnableWindow(GetDlgItem(dialog, IDCANCEL), TRUE);

      if (sent) {
        SaySent(dialog, answer);
        EndDialog(dialog, IDOK);
      } else {
        // The window stays open with the text still in it. Losing what somebody
        // wrote because the network was down is the one outcome worth avoiding.
        MessageBoxW(dialog, Format(IDS_REPORT_FAILED, answer.c_str()).c_str(),
                    Str(IDS_REPORT_TITLE).c_str(), MB_OK | MB_ICONWARNING);
        SetDlgItemTextW(dialog, IDC_REPORT_NOTE, Str(IDS_REPORT_HINT).c_str());
      }
      return TRUE;
    }
    default:
      return FALSE;
  }
}

}  // namespace

void ShowReportIssue(HWND owner, HINSTANCE instance) {
  DialogBoxW(instance, MAKEINTRESOURCEW(IDD_REPORT), owner, ReportProc);
}

}  // namespace pfwin

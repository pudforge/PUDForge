#include "Update.hpp"

#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>

#include <iterator>
#include <string>
#include <vector>

#include "GameData.hpp"   // ToUtf8, FromUtf8
#include "Log.hpp"
#include "Strings.hpp"
#include "UpdateFeed.hpp"
#include "resource.h"
#include "strings.h"
#include "version.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace pfwin {
namespace {

// The feed. Public and read-only: the worst anybody can do with the address is
// read the same page the releases tab shows.
constexpr wchar_t kFeedUrl[] =
    L"https://api.github.com/repos/pudforge/PUDForge/releases/latest";

/// Posted to the update window by its download thread when the bytes are in.
constexpr UINT kMsgDownloaded = WM_APP + 40;

/// A Win32 error as the sentence Windows has for it, trimmed of its newline.
std::wstring ErrorText(DWORD code) {
  wchar_t* text = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD n = FormatMessageW(flags, GetModuleHandleW(L"winhttp.dll"), code, 0,
                                 reinterpret_cast<wchar_t*>(&text), 0, nullptr);
  std::wstring out = n && text ? std::wstring(text, n) : Format(IDS_UPDATE_ERROR_CODE, int(code));
  if (text) LocalFree(text);
  while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ')) {
    out.pop_back();
  }
  return out;
}

/// GET a URL over WinHTTP into memory. Follows the redirect GitHub answers a
/// download with. `why` is filled on failure.
bool HttpGet(const std::wstring& url, const wchar_t* headers, std::string& body,
             std::wstring& why) {
  wchar_t host[256] = {}, path[2048] = {};
  URL_COMPONENTS parts = {};
  parts.dwStructSize = sizeof(parts);
  parts.lpszHostName = host;
  parts.dwHostNameLength = DWORD(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = DWORD(std::size(path));
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
    why = ErrorText(GetLastError());
    return false;
  }

  // Named after the program and its version: GitHub refuses a request with no
  // agent at all, and a version in it tells a log a stuck client from a
  // current one.
  const std::wstring agent = std::wstring(L"PUDForge/") + PF_APP_VERSION_WSTR;
  HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    why = ErrorText(GetLastError());
    return false;
  }
  WinHttpSetTimeouts(session, 10000, 10000, 10000, 30000);

  bool ok = false;
  DWORD error = 0;
  HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
  if (connection) {
    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (request) {
      if (WinHttpSendRequest(request, headers, headers ? DWORD(-1) : 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
          WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        for (;;) {
          DWORD available = 0;
          if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
          std::vector<char> chunk(available);
          DWORD read = 0;
          if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
          body.append(chunk.data(), read);
        }
        if (status == 200) {
          ok = true;
        } else {
          why = Format(IDS_UPDATE_HTTP, int(status));
        }
      } else {
        error = GetLastError();
      }
      WinHttpCloseHandle(request);
    } else {
      error = GetLastError();
    }
    WinHttpCloseHandle(connection);
  } else {
    error = GetLastError();
  }
  WinHttpCloseHandle(session);
  if (!ok && why.empty()) why = ErrorText(error);
  return ok;
}

/// SHA-256 of a buffer as lower-case hex, through the system's own provider.
std::string Sha256Hex(const std::string& bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return {};
  }
  std::string hex;
  DWORD object_size = 0, got = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                        &got, 0) == 0) {
    std::vector<UCHAR> object(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) == 0) {
      UCHAR digest[32] = {};
      if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                         ULONG(bytes.size()), 0) == 0 &&
          BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
        static const char kDigits[] = "0123456789abcdef";
        for (UCHAR b : digest) {
          hex += kDigits[b >> 4];
          hex += kDigits[b & 15];
        }
      }
      BCryptDestroyHash(hash);
    }
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return hex;
}

/// Put `fresh` where `target` is: the running exe moved aside as `.old`, the
/// new one copied in. Put back on a failed copy, so a half-done update leaves
/// the editor that was there.
/// @return ERROR_SUCCESS, or the Win32 error of the step that failed
DWORD SwapExe(const std::wstring& fresh, const std::wstring& target) {
  const std::wstring old = target + L".old";
  DeleteFileW(old.c_str());
  if (!MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return GetLastError();
  }
  if (!CopyFileW(fresh.c_str(), target.c_str(), FALSE)) {
    const DWORD error = GetLastError();
    MoveFileExW(old.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
    return error;
  }
  return ERROR_SUCCESS;
}

/// The same swap, done by the downloaded exe run elevated. Windows asks;
/// ERROR_CANCELLED is the person saying no.
DWORD SwapExeElevated(const std::wstring& fresh, const std::wstring& target) {
  const std::wstring params = L"--install-update \"" + target + L"\"";
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
  info.lpVerb = L"runas";
  info.lpFile = fresh.c_str();
  info.lpParameters = params.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info)) return GetLastError();
  DWORD code = ERROR_GEN_FAILURE;
  if (info.hProcess) {
    WaitForSingleObject(info.hProcess, INFINITE);
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
  }
  return code;
}

std::wstring TempExe(const std::wstring& version) {
  wchar_t dir[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, dir);
  return std::wstring(dir) + L"PUDForge-" + version + L".exe";
}

bool WriteFileBytes(const std::wstring& path, const std::string& bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr);
  CloseHandle(file);
  return ok && written == bytes.size();
}

// ------------------------------------------------------------- the check

struct CheckJob {
  HWND notify;
  UINT message;
  bool quiet;
};

DWORD WINAPI CheckThread(void* param) {
  CheckJob* job = static_cast<CheckJob*>(param);
  auto* result = new UpdateResult();
  result->quiet = job->quiet;

  std::string body;
  std::wstring why;
  if (HttpGet(kFeedUrl,
              L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
              body, why)) {
    ReleaseInfo info;
    if (ParseLatestRelease(body, info)) {
      result->ok = true;
      result->version = FromUtf8(info.version);
      result->exe_url = FromUtf8(info.exe_url);
      result->page_url = FromUtf8(info.page_url);
      result->sha256 = info.sha256;
      // Windows line ends, for the edit control that shows them.
      const std::wstring notes = FromUtf8(ReleaseNotesPlain(info.notes));
      for (wchar_t c : notes) {
        if (c == L'\n') result->notes += L'\r';
        result->notes += c;
      }
    } else {
      result->why = Str(IDS_UPDATE_BAD_FEED);
    }
  } else {
    result->why = why;
  }

  if (!PostMessageW(job->notify, job->message, 0, reinterpret_cast<LPARAM>(result))) {
    delete result;   // the window has gone; nobody is waiting
  }
  delete job;
  return 0;
}

// ------------------------------------------------------------ the window

struct UpdateSheet {
  const UpdateResult* found = nullptr;
  HWND dialog = nullptr;
  HANDLE thread = nullptr;
  std::string bytes;      ///< the download, filled by the thread
  std::wstring why;       ///< why the download failed, filled by the thread
  bool downloaded = false;
  bool busy = false;      ///< a download is running: the window must stay
  bool installed = false;
};

DWORD WINAPI DownloadThread(void* param) {
  auto* sheet = static_cast<UpdateSheet*>(param);
  sheet->downloaded = HttpGet(sheet->found->exe_url, nullptr, sheet->bytes, sheet->why);
  PostMessageW(sheet->dialog, kMsgDownloaded, 0, 0);
  return 0;
}

void SetBusy(HWND dialog, UpdateSheet& sheet, bool busy) {
  sheet.busy = busy;
  for (int id : {IDOK, IDCANCEL, IDC_UPDATE_SKIP, IDC_UPDATE_PAGE}) {
    EnableWindow(GetDlgItem(dialog, id), !busy);
  }
  HWND progress = GetDlgItem(dialog, IDC_UPDATE_PROGRESS);
  ShowWindow(progress, busy ? SW_SHOW : SW_HIDE);
  SendMessageW(progress, PBM_SETMARQUEE, busy ? TRUE : FALSE, 0);
}

/// The download is in: check it, put it in place, and say how it went.
void Install(HWND dialog, UpdateSheet& sheet) {
  const UpdateResult& found = *sheet.found;
  auto fail = [&](const std::wstring& why) {
    SetBusy(dialog, sheet, false);
    SetDlgItemTextW(dialog, IDC_UPDATE_NOTE, Format(IDS_UPDATE_NOT_INSTALLED, why.c_str()).c_str());
    Log::The().Add(Format(IDS_UPDATE_NOT_INSTALLED, why.c_str()), true);
  };
  if (!sheet.downloaded) { fail(sheet.why); return; }

  // Against the hash the build that made it wrote into the notes. A release
  // without one is not refused — the file came over TLS from the release
  // itself — but it is written down that it went unchecked.
  if (!found.sha256.empty()) {
    if (Sha256Hex(sheet.bytes) != found.sha256) { fail(Str(IDS_UPDATE_MISMATCH)); return; }
  } else {
    Log::The().Add(Str(IDS_UPDATE_UNCHECKED), true);
  }

  const std::wstring fresh = TempExe(found.version);
  if (!WriteFileBytes(fresh, sheet.bytes)) { fail(ErrorText(GetLastError())); return; }

  SetDlgItemTextW(dialog, IDC_UPDATE_NOTE, Str(IDS_UPDATE_INSTALLING).c_str());
  const std::wstring target = ThisExe();
  DWORD code = SwapExe(fresh, target);
  if (code == ERROR_ACCESS_DENIED) {
    // Not ours to write. The downloaded exe does it with permission asked
    // for, and this window says so first, since the prompt names a file in a
    // temp folder rather than the editor.
    SetDlgItemTextW(dialog, IDC_UPDATE_NOTE, Str(IDS_UPDATE_PERMISSION).c_str());
    UpdateWindow(dialog);
    code = SwapExeElevated(fresh, target);
  }
  DeleteFileW(fresh.c_str());
  if (code == ERROR_CANCELLED) { fail(Str(IDS_UPDATE_DECLINED)); return; }
  if (code != ERROR_SUCCESS) { fail(ErrorText(code)); return; }

  sheet.installed = true;
  SetBusy(dialog, sheet, false);
  Log::The().Add(Format(IDS_UPDATE_INSTALLED, found.version.c_str()), false);
  SetDlgItemTextW(dialog, IDC_UPDATE_HEAD, Format(IDS_UPDATE_INSTALLED, found.version.c_str()).c_str());
  SetDlgItemTextW(dialog, IDC_UPDATE_NOTE, L"");
  SetDlgItemTextW(dialog, IDOK, Str(IDS_UPDATE_RESTART).c_str());
  ShowWindow(GetDlgItem(dialog, IDC_UPDATE_SKIP), SW_HIDE);
  SetFocus(GetDlgItem(dialog, IDOK));
}

INT_PTR CALLBACK UpdateProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* sheet = reinterpret_cast<UpdateSheet*>(GetWindowLongPtrW(dialog, DWLP_USER));
  switch (message) {
    case WM_INITDIALOG: {
      sheet = reinterpret_cast<UpdateSheet*>(lparam);
      sheet->dialog = dialog;
      SetWindowLongPtrW(dialog, DWLP_USER, LONG_PTR(sheet));
      const UpdateResult& found = *sheet->found;
      SetDlgItemTextW(dialog, IDC_UPDATE_HEAD,
                      Format(IDS_UPDATE_AVAILABLE, found.version.c_str(), PF_APP_VERSION_WSTR).c_str());
      SetDlgItemTextW(dialog, IDC_UPDATE_NOTES_LABEL, Format(IDS_UPDATE_NOTES, found.version.c_str()).c_str());
      SetDlgItemTextW(dialog, IDC_UPDATE_NOTES, found.notes.c_str());
      ShowWindow(GetDlgItem(dialog, IDC_UPDATE_PROGRESS), SW_HIDE);
      // A release with nothing to download can still be read about.
      if (found.exe_url.empty()) {
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        SetDlgItemTextW(dialog, IDC_UPDATE_NOTE, Str(IDS_UPDATE_NO_EXE).c_str());
      }
      // Focus on the answer, not on the notes: an edit control given focus
      // selects everything in it, and a page of highlighted text reads as a
      // question about the text.
      SendDlgItemMessageW(dialog, IDC_UPDATE_NOTES, EM_SETSEL, WPARAM(-1), 0);
      SetFocus(GetDlgItem(dialog, found.exe_url.empty() ? IDCANCEL : IDOK));
      return FALSE;   // focus set by hand
    }
    case WM_CLOSE:
      // Not while the bytes are coming: the thread writes into this window.
      if (sheet && sheet->busy) return TRUE;
      EndDialog(dialog, int(sheet && sheet->installed ? UpdateChoice::kInstalledLater
                                                       : UpdateChoice::kLater));
      return TRUE;
    case WM_DESTROY:
      if (sheet && sheet->thread) {
        WaitForSingleObject(sheet->thread, INFINITE);
        CloseHandle(sheet->thread);
        sheet->thread = nullptr;
      }
      return FALSE;
    case kMsgDownloaded:
      if (sheet) Install(dialog, *sheet);
      return TRUE;
    case WM_COMMAND: {
      if (!sheet) return FALSE;
      const int id = LOWORD(wparam);
      if (id == IDOK) {
        if (sheet->installed) { EndDialog(dialog, int(UpdateChoice::kRestart)); return TRUE; }
        if (sheet->busy) return TRUE;
        SetDlgItemTextW(dialog, IDC_UPDATE_NOTE,
                        Format(IDS_UPDATE_DOWNLOADING, sheet->found->version.c_str()).c_str());
        SetBusy(dialog, *sheet, true);
        sheet->bytes.clear();
        sheet->thread = CreateThread(nullptr, 0, DownloadThread, sheet, 0, nullptr);
        if (!sheet->thread) {
          sheet->downloaded = false;
          sheet->why = ErrorText(GetLastError());
          Install(dialog, *sheet);
        }
        return TRUE;
      }
      if (id == IDCANCEL) {
        if (sheet->busy) return TRUE;
        EndDialog(dialog, int(sheet->installed ? UpdateChoice::kInstalledLater : UpdateChoice::kLater));
        return TRUE;
      }
      if (id == IDC_UPDATE_SKIP) {
        EndDialog(dialog, int(UpdateChoice::kSkip));
        return TRUE;
      }
      if (id == IDC_UPDATE_PAGE) {
        ShellExecuteW(nullptr, L"open", sheet->found->page_url.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

}  // namespace

void StartUpdateCheck(HWND notify, UINT message, bool quiet) {
  auto* job = new CheckJob{notify, message, quiet};
  HANDLE thread = CreateThread(nullptr, 0, CheckThread, job, 0, nullptr);
  if (!thread) {
    auto* result = new UpdateResult();
    result->quiet = quiet;
    result->why = ErrorText(GetLastError());
    delete job;
    if (!PostMessageW(notify, message, 0, reinterpret_cast<LPARAM>(result))) delete result;
    return;
  }
  CloseHandle(thread);
}

bool IsNewerThanThis(const std::wstring& version) {
  return CompareVersions(ToUtf8(version), PF_APP_VERSION_STR) > 0;
}

UpdateChoice OfferUpdate(HWND owner, HINSTANCE instance, const UpdateResult& found) {
  UpdateSheet sheet;
  sheet.found = &found;
  const INT_PTR choice = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_UPDATE), owner,
                                         UpdateProc, LPARAM(&sheet));
  if (choice < 0) return UpdateChoice::kLater;
  return UpdateChoice(choice);
}

std::wstring ThisExe() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

bool LaunchExe(const std::wstring& exe) {
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                                 nullptr, SW_SHOWNORMAL)) > 32;
}

void RemoveOldExe() {
  DeleteFileW((ThisExe() + L".old").c_str());
}

bool WantsInstallUpdate(int argc, wchar_t** argv) {
  return argc >= 3 && std::wstring(argv[1]) == L"--install-update";
}

int RunInstallUpdate(int argc, wchar_t** argv) {
  if (!WantsInstallUpdate(argc, argv)) return int(ERROR_INVALID_PARAMETER);
  return int(SwapExe(ThisExe(), argv[2]));
}

int DayNumber() {
  FILETIME now = {};
  GetSystemTimeAsFileTime(&now);
  const ULONGLONG ticks = (ULONGLONG(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  return int(ticks / (10000000ULL * 86400ULL));
}

}  // namespace pfwin

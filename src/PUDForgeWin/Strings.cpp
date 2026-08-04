#include "Strings.hpp"

#include <cstdarg>
#include <vector>

namespace pfwin {
namespace {

/// Longest string the table is expected to hold, before growing.
///
/// LoadStringW cannot say how long a string is without a buffer to put it in, so
/// this starts generous and doubles. A translation may be considerably longer
/// than its original — German runs about a third longer — and silently
/// truncating one is the kind of bug nobody reports.
constexpr int kFirstGuess = 512;

}  // namespace

std::wstring Str(UINT id) {
  std::vector<wchar_t> buffer(kFirstGuess);
  for (;;) {
    const int written =
        LoadStringW(GetModuleHandleW(nullptr), id, buffer.data(), int(buffer.size()));
    if (written <= 0) {
      // No such string. Say which one, on screen, rather than showing nothing.
      return L"{" + std::to_wstring(id) + L"}";
    }
    // A full buffer means it may have been cut; ask again with more room.
    if (written < int(buffer.size()) - 1) {
      return std::wstring(buffer.data(), size_t(written));
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring Format(UINT id, ...) {
  const std::wstring form = Str(id);
  va_list args;
  va_start(args, id);
  // wvsprintfW has no length-limited form and a fixed 1024-character ceiling, so
  // this uses the CRT's, which reports what it needs.
  const int needed = _vscwprintf(form.c_str(), args);
  va_end(args);
  if (needed < 0) return form;

  std::vector<wchar_t> out(size_t(needed) + 1);
  va_start(args, id);
  vswprintf(out.data(), out.size(), form.c_str(), args);
  va_end(args);
  return std::wstring(out.data(), size_t(needed));
}

}  // namespace pfwin

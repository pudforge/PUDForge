#include "UpdateFeed.hpp"

#include <cstdlib>

namespace pfwin {
namespace {

/// Append one code point as UTF-8, for the `\uXXXX` escapes a JSON body
/// carries: the em dash in every changelog arrives that way.
void PutUtf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out += char(cp);
  } else if (cp < 0x800) {
    out += char(0xC0 | (cp >> 6));
    out += char(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += char(0xE0 | (cp >> 12));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  } else {
    out += char(0xF0 | (cp >> 18));
    out += char(0x80 | ((cp >> 12) & 0x3F));
    out += char(0x80 | ((cp >> 6) & 0x3F));
    out += char(0x80 | (cp & 0x3F));
  }
}

/// The JSON string starting at the quote at `at`, unescaped. `at` is left on
/// the closing quote, or at the end when there is none.
std::string ReadString(const std::string& json, size_t& at) {
  std::string out;
  for (at = at + 1; at < json.size(); at++) {
    const char c = json[at];
    if (c == '"') break;
    if (c != '\\' || at + 1 >= json.size()) { out += c; continue; }
    const char e = json[++at];
    switch (e) {
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'u': {
        if (at + 4 >= json.size()) return out;
        unsigned cp = unsigned(std::strtoul(json.substr(at + 1, 4).c_str(), nullptr, 16));
        at += 4;
        // A pair of surrogates is one code point outside the BMP.
        if (cp >= 0xD800 && cp < 0xDC00 && at + 6 < json.size() &&
            json[at + 1] == '\\' && json[at + 2] == 'u') {
          const unsigned low =
              unsigned(std::strtoul(json.substr(at + 3, 4).c_str(), nullptr, 16));
          if (low >= 0xDC00 && low < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            at += 6;
          }
        }
        PutUtf8(out, cp);
        break;
      }
      default: out += e; break;   // \" \\ \/
    }
  }
  return out;
}

/// The string value of the first `"key"` at or after `from`, or empty. Sets
/// `found` to where the value ended so a caller can walk on to the next one.
std::string StringMember(const std::string& json, const char* key, size_t from,
                         size_t* found = nullptr) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t at = json.find(needle, from);
  if (at == std::string::npos) return {};
  at = json.find(':', at + needle.size());
  if (at == std::string::npos) return {};
  at = json.find_first_not_of(" \t\r\n", at + 1);
  if (at == std::string::npos || json[at] != '"') {
    if (found) *found = at == std::string::npos ? json.size() : at;
    return {};
  }
  const std::string value = ReadString(json, at);
  if (found) *found = at;
  return value;
}

bool EndsWith(const std::string& text, const char* tail) {
  const std::string t = tail;
  return text.size() >= t.size() && text.compare(text.size() - t.size(), t.size(), t) == 0;
}

int Part(const std::string& version, size_t& at) {
  int value = 0;
  while (at < version.size() && version[at] >= '0' && version[at] <= '9') {
    value = value * 10 + (version[at] - '0');
    at++;
  }
  if (at < version.size() && version[at] == '.') at++;
  return value;
}

}  // namespace

bool ParseLatestRelease(const std::string& json, ReleaseInfo& out) {
  out = ReleaseInfo{};
  // The top-level members come before the nested author and assets objects,
  // so the first of each name is the release's own: `html_url` is repeated
  // inside every asset, and the first one is the page.
  std::string tag = StringMember(json, "tag_name", 0);
  if (tag.empty()) return false;
  if (tag[0] == 'v' || tag[0] == 'V') tag.erase(0, 1);
  out.version = tag;
  out.page_url = StringMember(json, "html_url", 0);
  out.notes = StringMember(json, "body", 0);

  // Which asset is the program: the one called PUDForge.exe, not the first.
  // A release can carry a source archive or a checksum file beside it.
  for (size_t at = 0;;) {
    size_t after = 0;
    const std::string url = StringMember(json, "browser_download_url", at, &after);
    if (url.empty()) break;
    if (EndsWith(url, "/PUDForge.exe")) { out.exe_url = url; break; }
    at = after;
  }

  // The hash CI writes into the notes, so the download can be checked against
  // what the run that built it measured rather than trusted on arrival.
  const size_t label = out.notes.find("SHA-256");
  if (label != std::string::npos) {
    size_t at = label + 7;
    for (; at < out.notes.size(); at++) {
      const char c = out.notes[at];
      const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (!hex) continue;
      size_t end = at;
      while (end < out.notes.size() &&
             ((out.notes[end] >= '0' && out.notes[end] <= '9') ||
              (out.notes[end] >= 'a' && out.notes[end] <= 'f') ||
              (out.notes[end] >= 'A' && out.notes[end] <= 'F'))) {
        end++;
      }
      if (end - at == 64) {
        out.sha256 = out.notes.substr(at, 64);
        for (char& c : out.sha256) c = char(c >= 'A' && c <= 'F' ? c + 32 : c);
        break;
      }
      at = end;
    }
  }
  return true;
}

int CompareVersions(const std::string& a, const std::string& b) {
  size_t ia = a.size() && (a[0] == 'v' || a[0] == 'V') ? 1 : 0;
  size_t ib = b.size() && (b[0] == 'v' || b[0] == 'V') ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    const int pa = Part(a, ia), pb = Part(b, ib);
    if (pa != pb) return pa < pb ? -1 : 1;
  }
  return 0;
}

std::string ReleaseNotesPlain(const std::string& body) {
  std::string text;
  text.reserve(body.size());
  for (size_t i = 0; i < body.size(); i++) {
    if (body[i] == '\r') continue;
    text += body[i];
  }
  // The rule is where the changelog stops and the standing text starts.
  for (size_t at = 0; at < text.size();) {
    const size_t end = text.find('\n', at);
    const std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (line == "---") { text.erase(at); break; }
    if (end == std::string::npos) break;
    at = end + 1;
  }
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*') { i++; continue; }
    out += text[i];
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  while (!out.empty() && (out.front() == '\n' || out.front() == ' ')) out.erase(0, 1);
  return out;
}

unsigned PackVersion(const std::string& version) {
  size_t at = version.size() && (version[0] == 'v' || version[0] == 'V') ? 1 : 0;
  unsigned packed = 0;
  for (int i = 0; i < 3; i++) {
    const int part = Part(version, at);
    packed = (packed << 8) | unsigned(part > 255 ? 255 : part < 0 ? 0 : part);
  }
  return packed;
}

}  // namespace pfwin

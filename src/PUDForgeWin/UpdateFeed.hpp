// What a GitHub release looks like from here, and how to read one.
//
// The updater asks the releases API rather than a web page: the API is a
// contract with a version header and a JSON shape, where a page is whatever
// the site looks like this month. This file is the reading of that JSON and
// nothing else — no sockets, no Win32 — so pf_tests can hold it to a sample
// of the real reply on any machine.

#pragma once

#include <string>

namespace pfwin {

/// One release, as much of it as the updater needs. Every string is UTF-8.
struct ReleaseInfo {
  std::string version;   ///< "0.1.70": the tag without its leading v
  std::string exe_url;   ///< where PUDForge.exe is, or empty when the release carries none
  std::string page_url;  ///< the release on the site, for a browser
  std::string notes;     ///< the release body as written
  std::string sha256;    ///< the exe's hash as the notes state it, lower-case, or empty
};

/// Read the reply from `repos/{owner}/{repo}/releases/latest`.
/// @return false when there is no tag in it, which is the one thing a release
///         cannot be without
bool ParseLatestRelease(const std::string& json, ReleaseInfo& out);

/// Compare two dotted versions numerically: "0.1.70" against "0.1.9" says the
/// first is newer, which a string comparison would get backwards. A leading v
/// is ignored and a missing part counts as zero.
/// @return positive when `a` is newer, negative when older, zero when the same
int CompareVersions(const std::string& a, const std::string& b);

/// The notes as text for a plain edit control: the changelog section alone,
/// without the standing paragraphs CI appends after the rule, and without the
/// Markdown emphasis around the headings.
std::string ReleaseNotesPlain(const std::string& body);

/// A version as one number, so it fits a registry DWORD. Parts are capped at
/// 255, which nothing here will reach.
unsigned PackVersion(const std::string& version);

}  // namespace pfwin

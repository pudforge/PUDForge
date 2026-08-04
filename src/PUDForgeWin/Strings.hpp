// Every word the user reads, in one file.
//
// The text lives in the STRINGTABLE in Strings.rc, addressed by the ids in
// strings.h. A literal that reaches the screen is a string a translator cannot
// reach, so a localised build is the same .exe with a translated resource
// script — which is one place to hand a translator, and not a bespoke format.
//
// Two rules:
//
//   Whole sentences, never fragments. `count + " units given to " + who` cannot
//   be translated: German puts the verb last, and every language disagrees about
//   where a number goes relative to its noun.
//
//   Nothing that is not prose. Window class names, registry paths, file
//   extensions and `%04X` field formats stay as literals — translating
//   "PUDForgeMain" would be a bug.

#pragma once

#include <windows.h>

#include <string>

namespace pfwin {

/// The text for an id, from the running module's string table.
///
/// Returns the id in braces when the table has no such string, so a missing
/// translation is visible and obviously ours rather than an empty label.
std::wstring Str(UINT id);

/// The text for an id with `wsprintf`-style arguments filled in.
///
/// Format specifiers travel *inside* the translated string, so a translation may
/// reorder them where the language needs it — the whole reason the caller passes
/// arguments rather than concatenating.
std::wstring Format(UINT id, ...);

/// One of two strings by count, for the languages this ships in.
///
/// English needs two forms and so do the languages most likely to come next.
/// Some need more — Polish and Russian three, Arabic six — and when one of those
/// is wanted this becomes a rule per language rather than a ternary.
inline UINT Plural(int count, UINT one, UINT many) {
  return count == 1 ? one : many;
}

}  // namespace pfwin

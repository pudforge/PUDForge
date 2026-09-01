# Releasing

**The push is the publish.** There is no separate release step to run and no
tag to create by hand: CI reads `src/PUDForgeWin/version.h` on every push to
`master`, and the first time it sees a version it has no tag for it builds,
runs the whole suite, then creates `v<version>` and a GitHub release with
`PUDForge.exe` attached. A commit that bumps the version ships; a commit that
does not just builds.

That is why everything worth checking has to be checked before the push, and
why `scripts/prep-release.ps1` stops without touching GitHub.

## The short version

```powershell
powershell -File scripts/prep-release.ps1            # patch bump, changelog, build, tests
# read CHANGELOG.md and rewrite the new section
git add -A
git commit
git push origin master
gh run watch --repo pudforge/PUDForge
```

## What the script does

`scripts/prep-release.ps1 [-Part patch|minor|major] [-NoBump] [-SkipTests]
[-AllowDirty]`

1. Refuses to run off `master`, or on a dirty tree unless `-AllowDirty` (which
   is the usual case: the release commit and the changes it ships are normally
   written together).
2. Bumps `version.h` — one place, which feeds the title bar, About,
   `--version` and the `VERSIONINFO` block Explorer reads. It refuses a version
   that origin already has a tag for.
3. Opens a `## <version>` section in `CHANGELOG.md` if there is not one,
   seeded as HTML comments with every commit subject since the last tag.
4. Configures, builds, checks the exe is not older than `pudforge.lib` — a
   stale unrelinked exe is a release without the change in it — and runs
   `ctest`.
5. Prints what is left. It never pushes, tags, or calls `gh`.

`-NoBump` checks the version that is already in `version.h` instead of moving
it. Use it for the last look before a push when the release was prepared over
several commits: `version.h` asks for a bump on every commit, so by then the
number is already the one that will ship.

## The changelog is the release notes

CI posts the `## <version>` section of `CHANGELOG.md` verbatim as the release
body, followed by the standing text about what the download is and that the
game is required. So the section is written for whoever lands on the releases
page, not for whoever wrote the commit — the seeded commit subjects are a
reminder of what happened, not the notes.

**Write the section in ASD-STE100** (Simplified Technical English), as a short
list of what is different for a user — one bullet per change, under **Fixed**,
**New** or **Changed**: one idea per sentence, 25 words at
most, active voice, present tense, the same word for the same thing every time,
and no metaphors or jargon. "Feature X now supports Y", or "a problem that
caused Z is corrected" — not the reasoning behind the change, which belongs in
the commit message and the code, and not the conditions and defaults, which
belong in the user guide. `CHANGELOG.md` repeats these rules at the top,
where whoever is editing it will see them.

A section is keyed to a *release*, not to a commit. `version.h` is bumped on
every commit, but only the version sitting on `master` when CI runs gets a tag,
so several patch bumps can fold into one released version. The section covers
everything since the previous tag.

If a version has no section, the release still goes out with the standing text
alone. That is deliberate: a release held back over a missing heading helps
nobody.

## Checking a release afterwards

```powershell
gh run list    --repo pudforge/PUDForge --limit 3
gh release view v0.1.28 --repo pudforge/PUDForge
```

The uploaded `PUDForge.exe` is the whole product: statically linked, with the
icons, strings and user guide inside it. `PUDForgeTest.exe` is a byte copy
under a second name so the editor can be run while the real one is being
relinked, and is deliberately not uploaded — shipping it would suggest there
are two programs.

## Signing

`PUDForge.exe` is signed by [SignPath Foundation](https://signpath.org), which
gives open source projects free code signing. The padlock is not the point.
Windows attaches reputation to the *signer*, and that outlives any single
build, so every signed release inherits what the ones before it earned.
Unsigned, each release is a hash the world has never seen, carrying no
publisher name, and browsers and scanners answer that by guessing. That is why
people were told the download was a virus.

Two things made it worse than the average unsigned binary, and both are worth
knowing when reading a report:

- **The exe now phones home.** v0.1.66 is the first build that imports
  `WINHTTP.dll` at all — ten new imports, every one of them network. Before it
  the program made no outbound connection of any kind. Check for Updates
  (0.1.71) keeps the import for good: it reads the releases API and downloads
  the exe from the release, which is a GET to `api.github.com` and
  `github.com` and nothing else. A downloader that replaces its own exe is a
  shape scanners weigh too, and the checks above are what a report should be
  read against.
- **What sits beside those imports reads badly.** v0.1.66 adds forty strings to
  v0.1.65, and `pudforge-feedback.pudforge.workers.dev`, `/report`,
  `Content-Type: application/json` and `,"log":` are all among them. To a
  classifier that is an unsigned executable carrying a hardcoded host, a JSON
  POST and a field called `log` — the feature vector of an infostealer — and
  `*.workers.dev` is a namespace heavily abused for command and control. A
  custom domain in front of the worker costs nothing and removes the worst of
  it.

  **Do not answer this by splitting or encoding the string.** Hiding a hostname
  from a scanner is what malware does, and string obfuscation is itself
  something scanners weigh. It would make the score worse, not better.

### How the workflow does it

CI never hands the binary to SignPath. It uploads `PUDForge.exe` as a workflow
artifact, and SignPath reads it back out of the run through GitHub's own API,
so what gets signed is provably what that run built rather than whatever the
holder of the API token felt like uploading. The signed file comes back to
`signed/`, and that is what is hashed and released — the hash in the release
notes is taken after signing, because signing rewrites the file.

**Signing is skipped, not failed, when it is not configured.** The API token is
the one part that cannot live in the repository, so its absence is exactly what
"not set up yet" looks like, and holding a release back over it would help
nobody. The run says `NOT SIGNED` in the log and the release notes fall back to
the paragraph about false positives. If a release goes out unsigned when it
should not have, that line in the log is where it says so — an expired token
looks the same as an unconfigured one.

### Setting it up

1. **Apply** at <https://signpath.org/apply>. The project qualifies: MIT
   licensed, public repository, built entirely by GitHub-hosted runners.
   Approval takes days rather than minutes and is done by a person.
2. **In SignPath**, once the organization exists: add the predefined
   `GitHub.com` trusted build system, create a project for this repository and
   link the build system to it.
3. **The artifact configuration** has to match what `upload-artifact` produces.
   It uploads a ZIP, so the root element must be `<zip-file>` containing the
   `<pe-file>` for `PUDForge.exe`. This is the step that usually goes wrong
   first, and it fails with a message about the artifact not matching the
   configuration.
4. **In this repository**, add one secret and three variables. All four go
   together; the workflow checks for the token and the organization id and
   skips signing unless both are present.

   | | |
   |---|---|
   | secret `SIGNPATH_API_TOKEN` | an API token for a SignPath user with submitter rights |
   | variable `SIGNPATH_ORGANIZATION_ID` | the organization GUID |
   | variable `SIGNPATH_PROJECT_SLUG` | the project slug |
   | variable `SIGNPATH_SIGNING_POLICY_SLUG` | `test-signing` while proving it out, `release-signing` after |

   ```powershell
   gh secret set SIGNPATH_API_TOKEN --repo pudforge/PUDForge
   gh variable set SIGNPATH_ORGANIZATION_ID --repo pudforge/PUDForge --body '<guid>'
   gh variable set SIGNPATH_PROJECT_SLUG --repo pudforge/PUDForge --body '<slug>'
   gh variable set SIGNPATH_SIGNING_POLICY_SLUG --repo pudforge/PUDForge --body 'release-signing'
   ```

5. **Check the first signed release** rather than assuming it. The run log
   should say `signed:` and not `NOT SIGNED`:

   ```powershell
   gh release download v<version> --pattern PUDForge.exe --output signed.exe
   Get-AuthenticodeSignature signed.exe | Format-List Status, SignerCertificate
   ```

   `Status` is `Valid` and the certificate names the project. Anything else and
   the release went out unsigned.

Reputation is not instant with the OV certificate SignPath Foundation issues —
it accrues over weeks of downloads. Releasing less often helps it along: a hash
that stays current for a fortnight collects some, and one replaced the same
afternoon collects none. Twelve releases in three days is what the last week
looked like, and it is the other half of why nothing was ever trusted.

Until reputation builds, a false positive is still worth reporting to whoever
made it. Microsoft's form is <https://www.microsoft.com/en-us/wdsi/filesubmission>
and usually answers within a few days. It clears one hash, so it is worth doing
for a release people are actually downloading and not for every patch bump.

## If something goes wrong

- **CI went red after the push.** Nothing was released: the release step runs
  after the tests. Fix, bump the patch again, push again.
- **Released with wrong notes.** `gh release edit v<version> --notes-file -`
  fixes the release; fix `CHANGELOG.md` in the next commit too, since that is
  the source.
- **Released by mistake.** `gh release delete v<version> --cleanup-tag`. Then
  bump past that version rather than reusing it, so nobody is left holding a
  binary that claims a version number that now means something else.
- **The version was bumped but nothing shipped.** CI only releases from
  `master` on a `push` event, and only when the version has no tag. Check that
  the push landed on `master` and that `git ls-remote --tags origin` does not
  already have it.

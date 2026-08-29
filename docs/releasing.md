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

`scripts/prep-release.ps1 [-Part patch|minor|major] [-SkipTests] [-AllowDirty]`

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

## The changelog is the release notes

CI posts the `## <version>` section of `CHANGELOG.md` verbatim as the release
body, followed by the standing text about what the download is and that the
game is required. So the section is written for whoever lands on the releases
page, not for whoever wrote the commit — the seeded commit subjects are a
reminder of what happened, not the notes.

**Write the section in ASD-STE100** (Simplified Technical English), as a short
list of what is different for a user — one bullet per change, no headings: one idea per sentence, 25 words at
most, active voice, present tense, the same word for the same thing every time,
and no metaphors or jargon. "Feature X now supports Y", or "a problem that
caused Z is corrected" — not the reasoning behind the change, which belongs in
the commit message and the code. `CHANGELOG.md` repeats these rules at the top,
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

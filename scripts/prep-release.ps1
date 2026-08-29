# Prepare a release. Does not publish one.
#
# Releasing is a push: CI reads `version.h`, and cuts a tag and a GitHub
# release the first time it sees a version it has no tag for. So everything
# that has to be right has to be right *before* the push, and this is the
# script that gets it there and then stops, leaving the commit and the push to
# a person who has read what it printed.
#
# What it does, in order:
#   1. refuses to start on a dirty tree or off master
#   2. bumps version.h  (-Part patch|minor|major, default patch)
#   3. opens a CHANGELOG.md section for the new version, seeded with every
#      commit since the last tag, if there is not one already
#   4. builds the client and runs the whole suite against it
#   5. prints exactly what is left to do
#
# Nothing here talks to GitHub. See docs/releasing.md.

[CmdletBinding()]
param(
    # Which number moves. A release worth naming takes the minor.
    [ValidateSet('patch', 'minor', 'major')]
    [string]$Part = 'patch',

    # Skip the build and the tests. For a second run when only the changelog
    # wording changed; never for the run that decides whether to push.
    [switch]$SkipTests,

    # Prepare against a tree that is not clean. Prints what is uncommitted and
    # carries on, for the usual case where the release commit is being written
    # at the same time as the changes it ships.
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $root 'src/PUDForgeWin/version.h'
$changelog = Join-Path $root 'CHANGELOG.md'

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }
function Note($text) { Write-Host "   $text" }

# ---------------------------------------------------------------- the tree

Step 'Checking the tree'
$branch = (git -C $root rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'master') {
    throw "on branch '$branch'. CI releases from master, so prepare there."
}
$dirty = git -C $root status --porcelain
if ($dirty -and -not $AllowDirty) {
    Write-Host $dirty
    throw 'the tree has uncommitted changes. Commit them, or pass -AllowDirty.'
}
if ($dirty) { Note 'uncommitted changes present (-AllowDirty)' }
Note "on $branch"

# ------------------------------------------------------------- the version

Step 'Bumping the version'
# Read through .NET rather than Get-Content: Windows PowerShell 5.1 decodes as
# the system codepage, which turns every em dash in these files into mojibake
# on the way back out. Sources here are UTF-8, and .NET reads them as UTF-8.
$text = [System.IO.File]::ReadAllText($versionFile)
if ($text -notmatch '#define PF_APP_VERSION_MAJOR (\d+)') { throw "no MAJOR in $versionFile" }
$major = [int]$Matches[1]
if ($text -notmatch '#define PF_APP_VERSION_MINOR (\d+)') { throw "no MINOR in $versionFile" }
$minor = [int]$Matches[1]
if ($text -notmatch '#define PF_APP_VERSION_PATCH (\d+)') { throw "no PATCH in $versionFile" }
$patch = [int]$Matches[1]
$was = "$major.$minor.$patch"

switch ($Part) {
    'major' { $major++; $minor = 0; $patch = 0 }
    'minor' { $minor++; $patch = 0 }
    'patch' { $patch++ }
}
$version = "$major.$minor.$patch"

# Every tag the remote has, so a version that is already out cannot be prepared
# twice. Asked of the remote because a checkout may have no tags at all.
$existing = git -C $root ls-remote --tags origin "refs/tags/v$version"
if (-not [string]::IsNullOrWhiteSpace($existing)) {
    throw "v$version is already tagged on origin. Bump further."
}

$text = $text -replace '#define PF_APP_VERSION_MAJOR \d+', "#define PF_APP_VERSION_MAJOR $major"
$text = $text -replace '#define PF_APP_VERSION_MINOR \d+', "#define PF_APP_VERSION_MINOR $minor"
$text = $text -replace '#define PF_APP_VERSION_PATCH \d+', "#define PF_APP_VERSION_PATCH $patch"
$text = $text -replace '#define PF_APP_VERSION_STR "[^"]*"', "#define PF_APP_VERSION_STR `"$version`""
$text = $text -replace '#define PF_APP_VERSION_WSTR L"[^"]*"', "#define PF_APP_VERSION_WSTR L`"$version`""
# Written without a BOM: Windows PowerShell's -Encoding utf8 adds one, and a
# release commit whose only visible change is three bytes at the top of a
# header is a diff nobody can read.
$noBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($versionFile, $text, $noBom)
Note "$was -> $version"

# ----------------------------------------------------------- the changelog

Step 'Changelog'
$notes = [System.IO.File]::ReadAllText($changelog)
if ($notes -match "(?m)^## $([regex]::Escape($version))\s*$") {
    Note "a section for $version is already there; left alone"
} else {
    # The previous tag, so the seed covers the whole release rather than the
    # last commit: a patch bump happens on every commit and only the version
    # that lands on master is ever tagged.
    $tags = @(git -C $root tag --list 'v*' --sort=-v:refname)
    $since = if ($tags.Count) { $tags[0] } else { '' }
    $range = if ($since) { "$since..HEAD" } else { 'HEAD' }
    $commits = @(git -C $root log $range --no-merges --format='%s')
    if (-not $commits.Count) { $commits = @('(no commits since ' + $since + ')') }

    $seed = @("## $version", '')
    $seed += '<!-- Written from these commits' + $(if ($since) { " since $since" }) + '; rewrite them'
    $seed += '     into what a person needs to know, then delete this block. -->'
    foreach ($c in $commits) { $seed += "<!-- $c -->" }
    $seed += ''

    # Under the preamble and above the newest existing section.
    $lines = @([System.IO.File]::ReadAllLines($changelog))
    $at = ($lines | Select-String -Pattern '^## ' | Select-Object -First 1).LineNumber - 1
    if ($at -lt 0) { $at = $lines.Count }
    $out = @()
    if ($at -gt 0) { $out += $lines[0..($at - 1)] }
    $out += $seed
    $out += $lines[$at..($lines.Count - 1)]
    $noBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($changelog, ($out -join "`r`n") + "`r`n", $noBom)
    Note "opened a $version section seeded with $($commits.Count) commit(s) since $since"
    Note 'Rewrite it before committing - it is the release notes verbatim.'
}

# ------------------------------------------------------- build and prove it

if ($SkipTests) {
    Step 'Build and tests'
    Note 'skipped (-SkipTests)'
} else {
    Step 'Building'
    $build = Join-Path $root 'build'
    cmake -S (Join-Path $root 'src') -B $build -G 'Visual Studio 17 2022' -A x64 | Out-Null
    if ($LASTEXITCODE) { throw 'configure failed' }
    cmake --build $build --config Release -- -m | Out-Null
    if ($LASTEXITCODE) { throw 'build failed' }

    # The exe can be stale when the build says it succeeded: MSBuild has left
    # PUDForge.exe unrelinked against a newer pudforge.lib, and a green suite
    # then proves nothing about the binary that ships. Asked of the exe's own
    # VERSIONINFO rather than of its timestamp, because that is the number
    # Explorer shows and the one the release is named after - and read out of
    # the file rather than run, since a WIN32-subsystem app gives a shell no
    # output to catch.
    $exe = Join-Path $build 'Release/PUDForge.exe'
    if (-not (Test-Path $exe)) { throw "no $exe after a successful build" }
    $built = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($exe).FileVersion
    if ($built -notmatch [regex]::Escape($version)) {
        throw "$exe reports version '$built', not $version. Delete it and build again."
    }
    Note "PUDForge.exe carries $built"

    Step 'Tests'
    ctest --test-dir $build -C Release -j4 --output-on-failure
    if ($LASTEXITCODE) { throw 'tests failed' }
}

# ------------------------------------------------------------- what is left

Step "Ready to release $version"
Write-Host @"
   Nothing has been pushed and no tag exists yet. What is left:

     1. Read CHANGELOG.md. The $version section is posted verbatim as the
        release notes, so it should read like something written for a person.
     2. git add -A && git commit
     3. git push origin master

   The push is the publish. CI builds, runs the suite, and only then creates
   the tag v$version and the GitHub release with PUDForge.exe attached - and
   only because $version has no tag yet. Watch it with:

     gh run watch --repo pudforge/PUDForge
     gh release view v$version --repo pudforge/PUDForge

   To back out before the push: git checkout -- src/PUDForgeWin/version.h CHANGELOG.md
"@

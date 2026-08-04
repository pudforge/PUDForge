# Convert the user guide from Markdown into the single HTML file the exe embeds.
#
#   powershell -File scripts/make-help.ps1 [-Sheet docs/user_guide.md]
#                                          [-Out src/PUDForgeWin/user-guide.html]
#
# The Markdown is the source and this is build output, the same arrangement the
# two PNGs have: edit docs/user_guide.md, press build, and the exe carries the
# new text. Editing the .html by hand is editing the wrong file.
#
# Self-contained on purpose. The page is written to %TEMP% and opened in
# whatever browser the user has, so it cannot fetch a stylesheet or a font —
# there may be no network, and a help page that renders unstyled because a CDN
# was unreachable is worse than one that was never pretty. Everything is inline.
#
# The converter handles the subset the guide actually uses: headings,
# paragraphs, fenced code, tables, bullet lists, and inline code/bold/links. It
# is deliberately not a Markdown implementation — anything it does not know
# passes through as text, which is visible in the output rather than silent.
#
# -Sheet and -Out are named to match make-icon.ps1 and make-ui-icons.ps1, so
# CMake drives all three the same way.
param(
  [string]$Sheet = "docs/user_guide.md",
  [string]$Out   = "src/PUDForgeWin/user-guide.html",
  [string]$Icon  = "src/PUDForgeWin/app-icon-src.png"
)

$ErrorActionPreference = "Stop"

# The application icon, inlined as a data URI. The same PNG the build converts
# into app.ico, so the page and the executable cannot show different marks. A
# missing file leaves the header text-only rather than failing the build.
#
# The file is the 64x32 sheet make-icon.ps1 reads, holding the 32x32 drawing at
# (0,0) and the 16x16 at (32,0). Only the first cell is wanted, and it is
# cropped in CSS rather than in System.Drawing: this script has no other reason
# to load an image, and background-size does the same job in one declaration.
$iconTag = ""
$iconCss = ""
$iconPath = if ([System.IO.Path]::IsPathRooted($Icon)) { $Icon }
            else { Join-Path (Get-Location) $Icon }
if (Test-Path $iconPath) {
  $b64 = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($iconPath))
  $iconTag = "<span class=""mark"" role=""img"" aria-label=""PUDForge""></span>"
  $iconCss = "  .mark { background-image: url(""data:image/png;base64,$b64""); }"
}

$md = Get-Content -Path $Sheet -Raw -Encoding UTF8
$md = $md -replace "`r`n", "`n"

function Esc([string]$t) {
  $t = $t -replace '&', '&amp;'
  $t = $t -replace '<', '&lt;'
  $t = $t -replace '>', '&gt;'
  return $t
}

# Inline runs, innermost first so a link's caption can still carry code.
# Escaping happens here rather than up front, because the fenced-code path
# needs the raw text and escapes it itself.
function Inline([string]$t) {
  $t = Esc $t
  $t = [regex]::Replace($t, '`([^`]+)`', '<code>$1</code>')
  $t = [regex]::Replace($t, '\*\*([^*]+)\*\*', '<strong>$1</strong>')
  $t = [regex]::Replace($t, '(?<![\w*])\*([^*]+)\*(?![\w*])', '<em>$1</em>')
  # Only same-page and http links survive. A relative .md link would 404 from
  # %TEMP%, so it keeps its caption and loses the href.
  $t = [regex]::Replace($t, '\[([^\]]+)\]\((https?://[^)]+)\)', '<a href="$2">$1</a>')
  $t = [regex]::Replace($t, '\[([^\]]+)\]\((#[^)]+)\)', '<a href="$2">$1</a>')
  $t = [regex]::Replace($t, '\[([^\]]+)\]\([^)]*\)', '$1')
  return $t
}

function Slug([string]$t) {
  $s = ($t -replace '<[^>]+>', '').ToLowerInvariant()
  $s = $s -replace '[^a-z0-9 ]', ''
  return ($s.Trim() -replace ' +', '-')
}

$body = New-Object System.Text.StringBuilder
$toc  = New-Object System.Text.StringBuilder
$lines = $md -split "`n"
$i = 0
$inList = $false

function CloseList() {
  if ($script:inList) { [void]$body.AppendLine('</ul>'); $script:inList = $false }
}

while ($i -lt $lines.Count) {
  $line = $lines[$i]

  # Fenced code, taken verbatim: the INI and command-line samples are the text
  # a reader is meant to copy, so nothing may be reflowed or substituted.
  if ($line -match '^```') {
    CloseList
    $i++
    $buf = New-Object System.Text.StringBuilder
    while ($i -lt $lines.Count -and $lines[$i] -notmatch '^```') {
      [void]$buf.AppendLine((Esc $lines[$i])); $i++
    }
    $i++
    [void]$body.AppendLine('<pre>' + $buf.ToString().TrimEnd() + '</pre>')
    continue
  }

  # Tables: a header row, a dashes row, then body rows.
  if ($line -match '^\|' -and $i + 1 -lt $lines.Count -and $lines[$i + 1] -match '^\|[\s\-:|]+\|$') {
    CloseList
    $cells = ($line.Trim('|') -split '\|') | ForEach-Object { Inline $_.Trim() }
    [void]$body.AppendLine('<table><thead><tr>')
    foreach ($c in $cells) { [void]$body.AppendLine("<th>$c</th>") }
    [void]$body.AppendLine('</tr></thead><tbody>')
    $i += 2
    while ($i -lt $lines.Count -and $lines[$i] -match '^\|') {
      $row = ($lines[$i].Trim('|') -split '\|') | ForEach-Object { Inline $_.Trim() }
      [void]$body.AppendLine('<tr>')
      foreach ($c in $row) { [void]$body.AppendLine("<td>$c</td>") }
      [void]$body.AppendLine('</tr>')
      $i++
    }
    [void]$body.AppendLine('</tbody></table>')
    continue
  }

  if ($line -match '^(#{1,4})\s+(.*)$') {
    CloseList
    $level = $Matches[1].Length
    $text  = Inline $Matches[2]
    $id    = Slug $Matches[2]
    # The title carries the application's own icon, so the page identifies
    # itself the way the window and the taskbar do.
    if ($level -eq 1) {
      [void]$body.AppendLine("<h1 id=""$id"">$iconTag<span>$text</span></h1>")
    } else {
      [void]$body.AppendLine("<h$level id=""$id"">$text</h$level>")
    }
    # Sections and their subsections. A sidebar has the room for both, and a
    # subsection that is not listed is one a reader has to scroll to find.
    if ($level -eq 2) { [void]$toc.AppendLine("<li><a href=""#$id"">$text</a></li>") }
    if ($level -eq 3) { [void]$toc.AppendLine("<li class=""sub""><a href=""#$id"">$text</a></li>") }
    $i++
    continue
  }

  if ($line -match '^[-*]\s+(.*)$') {
    if (-not $inList) { [void]$body.AppendLine('<ul>'); $script:inList = $true }
    $item = $Matches[1]
    # A wrapped bullet continues until a blank line or the next bullet.
    while ($i + 1 -lt $lines.Count -and $lines[$i + 1] -match '^\s{2,}\S' -and
           $lines[$i + 1] -notmatch '^\s*[-*]\s') {
      $item += ' ' + $lines[$i + 1].Trim(); $i++
    }
    [void]$body.AppendLine('<li>' + (Inline $item) + '</li>')
    $i++
    continue
  }

  if ($line.Trim() -eq '') { CloseList; $i++; continue }

  # A paragraph runs to the next blank line or block.
  $para = $line
  while ($i + 1 -lt $lines.Count -and $lines[$i + 1].Trim() -ne '' -and
         $lines[$i + 1] -notmatch '^(#{1,4}\s|[-*]\s|\||```)') {
    $para += ' ' + $lines[$i + 1].Trim(); $i++
  }
  CloseList
  [void]$body.AppendLine('<p>' + (Inline $para) + '</p>')
  $i++
}
CloseList

# The colour pair follows the system rather than being chosen: a help page that
# is a white rectangle on a dark desktop is the thing people complain about.
$html = @"
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PUDForge user guide</title>
<style>
  :root { color-scheme: light dark; --ink: #1a1a1a; --bg: #fff; --dim: #666;
          --rule: #d8d8d8; --panel: #f6f6f4; --link: #0b5ea8; }
  @media (prefers-color-scheme: dark) {
    :root { --ink: #e6e6e6; --bg: #1c1c1e; --dim: #9a9a9a;
            --rule: #3a3a3c; --panel: #262628; --link: #6fb3ff; }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--ink);
         font: 16px/1.65 "Segoe UI", system-ui, sans-serif; }
  /* Contents beside the text rather than above it: a reference page is read by
     jumping to a section, and a list that scrolls away is a list you scroll
     back up to. The grid collapses to one column on a narrow window. */
  .page { display: grid; grid-template-columns: 15rem minmax(0, 46rem);
          gap: 3rem; padding: 2.5rem 1.5rem 6rem; margin: 0 auto;
          max-width: 66rem; align-items: start; }
  main { min-width: 0; }
  h1 { font-size: 2rem; margin: 0 0 .25rem; display: flex; gap: .6rem;
       align-items: center; }
  /* Two cells wide, so 200% puts the second one past the right edge. Pixel art
     enlarged from 32 px: nearest-neighbour keeps the edges the artist drew. */
  h1 .mark { width: 2.5rem; height: 2.5rem; flex: none;
             background-repeat: no-repeat; background-size: 200% 100%;
             background-position: left top; image-rendering: pixelated; }
$iconCss
  h2 { font-size: 1.35rem; margin: 2.75rem 0 .75rem;
       padding-top: .75rem; border-top: 1px solid var(--rule); }
  h3 { font-size: 1.08rem; margin: 1.75rem 0 .5rem; }
  h4 { font-size: 1rem; margin: 1.25rem 0 .4rem; color: var(--dim); }
  p, li { margin: .6rem 0; }
  a { color: var(--link); }
  code { background: var(--panel); border: 1px solid var(--rule);
         border-radius: 3px; padding: .05em .35em;
         font: .88em/1.4 Consolas, "Cascadia Mono", monospace; }
  pre { background: var(--panel); border: 1px solid var(--rule);
        border-radius: 6px; padding: .9rem 1rem; overflow-x: auto;
        font: .86rem/1.45 Consolas, "Cascadia Mono", monospace; }
  pre code { background: none; border: 0; padding: 0; }
  table { border-collapse: collapse; width: 100%; margin: 1rem 0;
          display: block; overflow-x: auto; }
  th, td { border: 1px solid var(--rule); padding: .45rem .7rem;
           text-align: left; vertical-align: top; }
  th { background: var(--panel); font-weight: 600; }
  ul { padding-left: 1.4rem; }
  kbd, strong code { font-weight: 600; }
  .lede { color: var(--dim); margin-top: 0; }
  nav { position: sticky; top: 2.5rem; max-height: calc(100vh - 5rem);
        overflow-y: auto; font-size: .92rem; }
  nav p { margin: 0 0 .5rem; font-weight: 600; text-transform: uppercase;
          letter-spacing: .04em; font-size: .74rem; color: var(--dim); }
  nav ul { list-style: none; margin: 0; padding: 0;
           border-left: 1px solid var(--rule); }
  nav li { margin: 0; }
  nav a { display: block; padding: .22rem 0 .22rem .9rem;
          margin-left: -1px; border-left: 2px solid transparent;
          color: var(--ink); text-decoration: none; }
  nav a:hover { color: var(--link); border-left-color: var(--rule); }
  nav .sub a { padding-left: 1.8rem; color: var(--dim); font-size: .95em; }
  footer { margin-top: 4rem; padding-top: 1rem; border-top: 1px solid var(--rule);
           color: var(--dim); font-size: .85rem; }
  @media (max-width: 60rem) {
    .page { grid-template-columns: minmax(0, 1fr); gap: 1.5rem;
            padding: 1.5rem 1rem 4rem; }
    nav { position: static; max-height: none; }
    nav ul { columns: 2; column-gap: 2rem; }
    nav li { break-inside: avoid; }
  }
  @media (max-width: 34rem) { nav ul { columns: 1; } }
</style>
</head>
<body>
<div class="page">
<nav><p>Contents</p><ul>
$($toc.ToString().TrimEnd())
</ul></nav>
<main>
$($body.ToString().TrimEnd())
<footer>PUDForge user guide. Generated from <code>docs/user_guide.md</code>.
Edit that file, not this one.</footer>
</main>
</div>
</body>
</html>
"@

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out }
           else { Join-Path (Get-Location) $Out }
$dir = Split-Path -Parent $outPath
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

# UTF-8 without a BOM: rc.exe embeds these bytes verbatim and the <meta charset>
# in the page is what the browser reads, so a BOM would just be three stray
# bytes at the top of the file.
[System.IO.File]::WriteAllText($outPath, $html, (New-Object System.Text.UTF8Encoding($false)))
"wrote $Out ($((Get-Item $outPath).Length) bytes)"

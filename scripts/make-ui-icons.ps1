# Build src/PUDForgeWin/ui-icons.bmp from the UI icon sheet.
#
# The sheet (src/ui-icons.png) is a grid of 16x16 cells, ten to a
# row, read left to right and top to bottom. Which cell is which button is
# written down once, in UiIcons.hpp — this script only moves pixels.
#
# A BMP rather than the PNG itself: the resource compiler understands BMP, and
# the core has a PNG *encoder* but no decoder, so a PNG in the resources would
# need one written to get it back out. The alpha channel is what matters and a
# 32bpp BI_RGB DIB carries it.
#
# Straight alpha, not premultiplied: the client turns these cells into HICONs,
# and an icon's colour bitmap is straight. Premultiplying here would show up as
# a dark fringe round every antialiased edge.
#
#   pwsh scripts/make-ui-icons.ps1
#
# The per-cell opaque counts printed at the end are how you tell a cell that
# has been drawn from one that is still blank — a blank cell is not an error,
# it just means that button keeps its caption until you draw it.

param(
  [string]$Sheet = "src/PUDForgeWin/ui-icons.png",
  [string]$Out   = "src/PUDForgeWin/ui-icons.bmp"
)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$img = New-Object System.Drawing.Bitmap((Resolve-Path $Sheet).Path)
if ($img.Width % 16 -ne 0 -or $img.Height % 16 -ne 0) {
  throw "expected a sheet that divides into 16x16 cells, got $($img.Width)x$($img.Height)"
}

$w = $img.Width
$h = $img.Height
$stride = 4 * $w

# BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40) + the pixels. rc.exe strips the
# file header; what lands in the exe is the header and the bits.
$bytes = New-Object byte[] (14 + 40 + $stride * $h)

$bytes[0] = 0x42   # 'B'
$bytes[1] = 0x4D   # 'M'
[BitConverter]::GetBytes([int]$bytes.Length).CopyTo($bytes, 2)
[BitConverter]::GetBytes([int](14 + 40)).CopyTo($bytes, 10)   # offset to bits

[BitConverter]::GetBytes([int]40).CopyTo($bytes, 14)
[BitConverter]::GetBytes([int]$w).CopyTo($bytes, 18)
[BitConverter]::GetBytes([int]$h).CopyTo($bytes, 22)          # positive: bottom-up
[BitConverter]::GetBytes([int16]1).CopyTo($bytes, 26)         # planes
[BitConverter]::GetBytes([int16]32).CopyTo($bytes, 28)        # bits per pixel
[BitConverter]::GetBytes([int]0).CopyTo($bytes, 30)           # BI_RGB
[BitConverter]::GetBytes([int]($stride * $h)).CopyTo($bytes, 34)

$pix = 14 + 40
for ($row = 0; $row -lt $h; $row++) {
  $y = $h - 1 - $row                       # bottom-up
  for ($col = 0; $col -lt $w; $col++) {
    $c = $img.GetPixel($col, $y)
    $at = $pix + $row * $stride + $col * 4
    $bytes[$at]     = $c.B
    $bytes[$at + 1] = $c.G
    $bytes[$at + 2] = $c.R
    $bytes[$at + 3] = $c.A
  }
}

# Where the result goes. `Join-Path (Get-Location) $Out` alone was fine while
# this was only ever run by hand from the repo root; the build runs it from the
# build directory and passes absolute paths, and joining two rooted paths gives
# a malformed one rather than an error.
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out }
           else { Join-Path (Get-Location) $Out }
[System.IO.File]::WriteAllBytes($outPath, $bytes)

# Which cells have been drawn. A run of zeros at the end is the normal state of
# a sheet that is still being filled in.
$cols = $w / 16
$rows = $h / 16
$drawn = 0
for ($cy = 0; $cy -lt $rows; $cy++) {
  $line = ""
  for ($cx = 0; $cx -lt $cols; $cx++) {
    $opaque = 0
    for ($y = 0; $y -lt 16; $y++) {
      for ($x = 0; $x -lt 16; $x++) {
        if ($img.GetPixel($cx * 16 + $x, $cy * 16 + $y).A -ge 8) { $opaque++ }
      }
    }
    if ($opaque -gt 0) { $drawn++ }
    $line += "{0,5}" -f $opaque
  }
  "row $cy :$line"
}
$img.Dispose()

"$drawn of $($cols * $rows) cells drawn"
"wrote $Out ($((Get-Item $outPath).Length) bytes, ${w}x${h})"

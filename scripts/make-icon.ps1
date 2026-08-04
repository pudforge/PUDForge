# Build src/PUDForgeWin/app.ico from the icon sheet.
#
# The sheet (src/PUDForgeWin/app-icon-src.png) is 64x32 and holds both drawings
# side by side: the 32x32 at (0,0) and the 16x16 at (32,0). Two drawings rather
# than one and a downscale, because 16 pixels of pixel art is not 32 pixels of
# pixel art halved — the small one drops detail on purpose and redraws the rest
# so it still reads. The right-hand 16x16 of the sheet is unused padding.
#
# Written by hand rather than by a converter for one reason: the entries must be
# classic 32bpp DIBs, not PNG-compressed ones, which older rc.exe rejects. Most
# converters emit PNG entries for anything over 48px and some do it for every
# size.
#
#   pwsh scripts/make-icon.ps1
#
# Verify by reading the result back, not by looking at a window: the opaque
# pixel counts printed at the end are the ones the sheet has.

param(
  [string]$Sheet = "src/PUDForgeWin/app-icon-src.png",
  [string]$Out   = "src/PUDForgeWin/app.ico"
)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = "Stop"

$img = New-Object System.Drawing.Bitmap((Resolve-Path $Sheet).Path)
if ($img.Width -ne 64 -or $img.Height -ne 32) {
  throw "expected a 64x32 sheet, got $($img.Width)x$($img.Height)"
}

# Where each drawing sits in the sheet, and the size it is written out at.
$entries = @(
  @{ x = 0;  y = 0; size = 32 },
  @{ x = 32; y = 0; size = 16 }
)

# One image = BITMAPINFOHEADER, the BGRA pixels bottom-up, then the 1bpp AND
# mask. The mask is redundant with the alpha channel on every Windows that has
# shipped this century, but it is part of the format and a zero-length one is
# not something every reader forgives.
function Build-Image($img, $x0, $y0, $size) {
  $stride = 4 * $size                       # BGRA
  $maskRow = [int][Math]::Ceiling($size / 32.0) * 4   # rows pad to 4 bytes
  $bytes = New-Object byte[] (40 + $stride * $size + $maskRow * $size)

  # BITMAPINFOHEADER. biHeight is doubled: the XOR bitmap and the AND mask are
  # stacked, and the header describes both.
  [BitConverter]::GetBytes([int]40).CopyTo($bytes, 0)
  [BitConverter]::GetBytes([int]$size).CopyTo($bytes, 4)
  [BitConverter]::GetBytes([int]($size * 2)).CopyTo($bytes, 8)
  [BitConverter]::GetBytes([int16]1).CopyTo($bytes, 12)    # planes
  [BitConverter]::GetBytes([int16]32).CopyTo($bytes, 14)   # bits per pixel
  [BitConverter]::GetBytes([int]0).CopyTo($bytes, 16)      # BI_RGB
  [BitConverter]::GetBytes([int]($stride * $size)).CopyTo($bytes, 20)

  $opaque = 0
  $pix = 40
  $mask = 40 + $stride * $size
  for ($row = 0; $row -lt $size; $row++) {
    $y = $y0 + $size - 1 - $row            # bottom-up
    for ($col = 0; $col -lt $size; $col++) {
      $c = $img.GetPixel($x0 + $col, $y)
      $at = $pix + $row * $stride + $col * 4
      $bytes[$at]     = $c.B
      $bytes[$at + 1] = $c.G
      $bytes[$at + 2] = $c.R
      $bytes[$at + 3] = $c.A
      if ($c.A -ge 128) { $opaque++ }
      else {
        # Set in the AND mask means "leave the screen alone here".
        $bit = $mask + $row * $maskRow + [int][Math]::Floor($col / 8)
        $bytes[$bit] = $bytes[$bit] -bor (0x80 -shr ($col % 8))
      }
    }
  }
  return @{ bytes = $bytes; opaque = $opaque }
}

$images = @()
foreach ($e in $entries) { $images += Build-Image $img $e.x $e.y $e.size }

# ICONDIR, then one ICONDIRENTRY per image, then the images.
$dir = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($dir)
$w.Write([int16]0)                  # reserved
$w.Write([int16]1)                  # 1 = icon
$w.Write([int16]$entries.Count)
$offset = 6 + 16 * $entries.Count
for ($i = 0; $i -lt $entries.Count; $i++) {
  $size = $entries[$i].size
  $w.Write([byte]$size)             # 0 would mean 256
  $w.Write([byte]$size)
  $w.Write([byte]0)                 # palette entries: none, it is 32bpp
  $w.Write([byte]0)                 # reserved
  $w.Write([int16]1)                # planes
  $w.Write([int16]32)               # bits per pixel
  $w.Write([int]$images[$i].bytes.Length)
  $w.Write([int]$offset)
  $offset += $images[$i].bytes.Length
}
foreach ($im in $images) { $w.Write($im.bytes) }
$w.Flush()

# Where the result goes. `Join-Path (Get-Location) $Out` alone was fine while
# this was only ever run by hand from the repo root; the build runs it from the
# build directory and passes absolute paths, and joining two rooted paths gives
# a malformed one rather than an error.
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out }
           else { Join-Path (Get-Location) $Out }
[System.IO.File]::WriteAllBytes($outPath, $dir.ToArray())
$w.Dispose(); $dir.Dispose(); $img.Dispose()

for ($i = 0; $i -lt $entries.Count; $i++) {
  "$($entries[$i].size)x$($entries[$i].size): $($images[$i].opaque) opaque pixels"
}
"wrote $Out ($((Get-Item $outPath).Length) bytes)"

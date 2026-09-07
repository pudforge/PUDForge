# Drive the built client from outside it, without touching the keyboard.
#
# Dot-source it and the `Pf*` functions below are available:
#
#     . scripts/ui-harness.ps1
#     $app = Start-PfEditor -Map test/fixtures/skirmish-a.pud
#     Invoke-PfMenu $app IDM_VIEW_GRID
#     Send-PfClick (Get-PfCanvas $app) 200 150
#     Save-PfShot $app.Main out.png
#     Stop-PfEditor $app
#
# `-SelfTest` runs that sequence against the built exe and reports.
#
# Three rules from CLAUDE.md are built in here rather than left to be
# remembered, because each one has already cost an afternoon:
#
#   * No cross-process message at or above WM_USER. `SendMessage(status,
#     SB_GETTEXT, ...)` kills the target - the pointer is not marshalled, so
#     the window proc writes into *this* process and the target dies at
#     0xc0000005 inside COMCTL32, which reads exactly like an editor bug.
#     Send-PfMessage refuses those ids outright. Read a status bar by
#     screenshotting it.
#   * Nothing steals focus. Every keystroke and click is PostMessage and every
#     picture is PrintWindow, so a person can keep typing in another window
#     while a test runs.
#   * The test copy, not the real exe. A running PUDForge.exe holds its own
#     binary and the next link fails with LNK1104, so this drives
#     PUDForgeTest.exe - the copy the build already makes for exactly this.

[CmdletBinding()]
param([switch]$SelfTest)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class PfWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int c);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int c);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public struct RECT { public int Left, Top, Right, Bottom; }

    public static string Cls(IntPtr h){ var s=new StringBuilder(256); GetClassName(h,s,s.Capacity); return s.ToString(); }
    public static string Txt(IntPtr h){ var s=new StringBuilder(1024); GetWindowText(h,s,s.Capacity); return s.ToString(); }

    // Top-level windows belonging to one process, so a stray copy of the
    // editor started by hand is never the one a test drives.
    public static List<IntPtr> TopLevel(uint pid){
        var found = new List<IntPtr>();
        EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h, out p); if(p==pid) found.Add(h); return true; }, IntPtr.Zero);
        return found;
    }
    public static List<IntPtr> Children(IntPtr parent){
        var found = new List<IntPtr>();
        EnumChildWindows(parent, (h,l)=>{ found.Add(h); return true; }, IntPtr.Zero);
        return found;
    }
}
'@

$script:PfRoot = Split-Path $PSScriptRoot -Parent

# ------------------------------------------------------------------ messages

$script:WM = @{
  SETTEXT = 0x000C; GETTEXT = 0x000D; GETTEXTLENGTH = 0x000E
  KEYDOWN = 0x0100; KEYUP = 0x0101; CHAR = 0x0102
  SYSKEYDOWN = 0x0104; SYSKEYUP = 0x0105
  COMMAND = 0x0111
  MOUSEMOVE = 0x0200
  LBUTTONDOWN = 0x0201; LBUTTONUP = 0x0202; LBUTTONDBLCLK = 0x0203
  RBUTTONDOWN = 0x0204; RBUTTONUP = 0x0205
  MOUSEWHEEL = 0x020A
}
$script:WM_USER = 0x0400

function New-PfLParam([int]$x, [int]$y) {
  # MAKELPARAM, and the cast matters: a negative y in the high word wraps and
  # the target reads a coordinate off the other edge of the window.
  [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
}

<#
.SYNOPSIS
  Post a message, refusing the ones that kill the target across a process.
#>
function Send-PfMessage {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][int]$Message,
        [IntPtr]$WParam = [IntPtr]::Zero, [IntPtr]$LParam = [IntPtr]::Zero)
  if ($Message -ge $script:WM_USER) {
    throw ("Refusing message 0x{0:X} to another process. At or above WM_USER the " +
           "lParam is a pointer this process owns, so the target's window proc " +
           "writes into our address space and dies at 0xc0000005 - which looks " +
           "exactly like an editor bug. Screenshot the control instead." -f $Message)
  }
  [void][PfWin]::PostMessage($Window, [uint32]$Message, $WParam, $LParam)
}

# -------------------------------------------------------------------- launch

<#
.SYNOPSIS
  Start the test copy of the client and wait for its main window.
.DESCRIPTION
  Kills any client already running first: one holds its own binary open and
  the next link fails with LNK1104.
#>
function Start-PfEditor {
  param(
    [string]$Map,
    [string]$Exe = (Join-Path $script:PfRoot 'build\Release\PUDForgeTest.exe'),
    [int]$TimeoutSeconds = 40,
    [switch]$Fresh                  # forget the saved settings first
  )
  if (-not (Test-Path $Exe)) {
    throw "No client at $Exe. Build the PUDForgeTest target first."
  }
  Get-Process PUDForge, PUDForgeTest -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 400
  if ($Fresh) { Remove-Item -Recurse -Force 'HKCU:\Software\PUDForge' -ErrorAction SilentlyContinue }

  $args = @()
  if ($Map) {
    if (-not [IO.Path]::IsPathRooted($Map)) { $Map = Join-Path $script:PfRoot $Map }
    if (-not (Test-Path $Map)) { throw "No map at $Map" }
    $args += "`"$Map`""
  }
  $proc = if ($args) { Start-Process $Exe -ArgumentList $args -PassThru }
          else       { Start-Process $Exe -PassThru }

  $sw = [Diagnostics.Stopwatch]::StartNew()
  $main = [IntPtr]::Zero
  while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    foreach ($h in [PfWin]::TopLevel([uint32]$proc.Id)) {
      if ([PfWin]::Cls($h) -eq 'PUDForgeMain') { $main = $h; break }
    }
    if ($main -ne [IntPtr]::Zero) { break }
    if ($proc.HasExited) { throw "The client exited before it showed a window (code $($proc.ExitCode))." }
    Start-Sleep -Milliseconds 40
  }
  if ($main -eq [IntPtr]::Zero) { throw "No PUDForgeMain window after $TimeoutSeconds s." }
  # The window exists before it is laid out, and a canvas of no area captures
  # as "window has no area". Waiting for the canvas to have size is waiting
  # for the client to be ready to be driven, which is what a caller means.
  if ($Map) {
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
      $canvas = Get-PfChildren $main |
        Where-Object { $_.Class -eq 'PUDForgeMap' -and $_.W -gt 0 -and $_.H -gt 0 }
      if ($canvas) { break }
      Start-Sleep -Milliseconds 50
    }
  }
  [pscustomobject]@{
    Process = $proc
    Main    = $main
    Started = $sw.Elapsed.TotalSeconds
  }
}

function Stop-PfEditor {
  param([Parameter(Mandatory)]$App)
  Stop-Process -Id $App.Process.Id -Force -ErrorAction SilentlyContinue
  Start-Sleep -Milliseconds 300
}

# ------------------------------------------------------------------- finding

function Get-PfChildren {
  param([Parameter(Mandatory)][IntPtr]$Window)
  foreach ($h in [PfWin]::Children($Window)) {
    $r = New-Object PfWin+RECT
    [void][PfWin]::GetWindowRect($h, [ref]$r)
    [pscustomobject]@{
      Handle = $h; Class = [PfWin]::Cls($h); Id = [PfWin]::GetDlgCtrlID($h)
      Text = [PfWin]::Txt($h); Visible = [PfWin]::IsWindowVisible($h)
      Enabled = [PfWin]::IsWindowEnabled($h)
      W = $r.Right - $r.Left; H = $r.Bottom - $r.Top
    }
  }
}

<#
.SYNOPSIS
  The map canvas. Everything that paints a map is drawn on this one child.
#>
<#
.SYNOPSIS
  The visible property form on a tabbed sheet.
.DESCRIPTION
  Every page's form is a child of the sheet whether or not it is on top, so
  the one to read is the one that is showing.
#>
function Get-PfForm {
  param([Parameter(Mandatory)][IntPtr]$Window)
  $hit = Get-PfChildren $Window |
    Where-Object { $_.Class -eq 'PUDForgeForm' -and $_.Visible } | Select-Object -First 1
  if (-not $hit) { throw "No visible PUDForgeForm on this sheet." }
  $hit.Handle
}

function Get-PfCanvas {
  param([Parameter(Mandatory)]$App)
  $hit = Get-PfChildren $App.Main | Where-Object { $_.Class -eq 'PUDForgeMap' } | Select-Object -First 1
  if (-not $hit) { throw "No PUDForgeMap canvas - is a map open?" }
  $hit.Handle
}

<#
.SYNOPSIS
  A top-level window of the client other than the main one - a dialog or a
  message box that has just opened.
#>
function Get-PfDialog {
  param([Parameter(Mandatory)]$App, [string]$Class, [string]$TitleLike, [int]$TimeoutMs = 4000)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
    foreach ($h in [PfWin]::TopLevel([uint32]$App.Process.Id)) {
      if ($h -eq $App.Main) { continue }
      if (-not [PfWin]::IsWindowVisible($h)) { continue }
      if ($Class     -and [PfWin]::Cls($h) -ne $Class) { continue }
      if ($TitleLike -and [PfWin]::Txt($h) -notlike $TitleLike) { continue }
      return $h
    }
    Start-Sleep -Milliseconds 50
  }
  return [IntPtr]::Zero
}

# ------------------------------------------------------------------ commands

$script:PfCommandIds = $null

<#
.SYNOPSIS
  Resolve a name from resource.h, so tests name a command instead of a number.
#>
function Get-PfCommandId {
  param([Parameter(Mandatory)][string]$Name)
  if ($null -eq $script:PfCommandIds) {
    $script:PfCommandIds = @{}
    $header = Join-Path $script:PfRoot 'src\PUDForgeWin\resource.h'
    foreach ($line in Get-Content $header) {
      if ($line -match '^\s*#define\s+(ID[A-Z]_\w+)\s+(\d+)') {
        $script:PfCommandIds[$Matches[1]] = [int]$Matches[2]
      }
    }
  }
  if (-not $script:PfCommandIds.ContainsKey($Name)) { throw "No id named $Name in resource.h" }
  $script:PfCommandIds[$Name]
}

<#
.SYNOPSIS
  Invoke a menu command by name.
.DESCRIPTION
  This is how a shortcut is driven, rather than by faking its keystroke. A
  posted VK_CONTROL does not enter the target thread's key state, so
  GetKeyState there still says Ctrl is up and a posted Ctrl+V arrives as a
  bare V. Accelerators and menu items raise the same WM_COMMAND, so sending
  the command is both simpler and the thing that actually works.
#>
function Invoke-PfMenu {
  param([Parameter(Mandatory)]$App, [Parameter(Mandatory)][string]$Command, [int]$SettleMs = 250)
  $id = Get-PfCommandId $Command
  Send-PfMessage $App.Main $script:WM.COMMAND ([IntPtr]$id) ([IntPtr]::Zero)
  Start-Sleep -Milliseconds $SettleMs
}

# ------------------------------------------------------------------ keyboard

$script:PfKeys = @{
  ESC=0x1B; ENTER=0x0D; TAB=0x09; SPACE=0x20; BACKSPACE=0x08; DELETE=0x2E
  LEFT=0x25; UP=0x26; RIGHT=0x27; DOWN=0x28; HOME=0x24; END=0x23
  PAGEUP=0x21; PAGEDOWN=0x22; PLUS=0xBB; MINUS=0xBD
}

function ConvertTo-PfVk([string]$Key) {
  if ($script:PfKeys.ContainsKey($Key.ToUpper())) { return $script:PfKeys[$Key.ToUpper()] }
  if ($Key.Length -eq 1) { return [int][char]$Key.ToUpper() }
  if ($Key -match '^F(\d+)$') { return 0x6F + [int]$Matches[1] }   # F1 = 0x70
  throw "Unknown key '$Key'"
}

<#
.SYNOPSIS
  Post one keystroke. Bare keys only - see Invoke-PfMenu for shortcuts.
#>
function Send-PfKey {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][string]$Key,
        [int]$SettleMs = 80)
  $vk = ConvertTo-PfVk $Key
  Send-PfMessage $Window $script:WM.KEYDOWN ([IntPtr]$vk) ([IntPtr]1)
  Send-PfMessage $Window $script:WM.KEYUP   ([IntPtr]$vk) ([IntPtr]0xC0000001)
  Start-Sleep -Milliseconds $SettleMs
}

<#
.SYNOPSIS
  Type into an edit control, one WM_CHAR at a time.
#>
function Send-PfText {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][string]$Text)
  foreach ($c in $Text.ToCharArray()) {
    Send-PfMessage $Window $script:WM.CHAR ([IntPtr][int][char]$c) ([IntPtr]1)
    Start-Sleep -Milliseconds 12
  }
}

# --------------------------------------------------------------------- mouse

<#
.SYNOPSIS
  Click at a point in a window's client area.
#>
function Send-PfClick {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][int]$X,
        [Parameter(Mandatory)][int]$Y, [switch]$Right, [switch]$Double, [int]$SettleMs = 120)
  $at = New-PfLParam $X $Y
  # The move first: the canvas tracks the cursor to decide what a click means,
  # and a click with no move before it arrives from nowhere.
  Send-PfMessage $Window $script:WM.MOUSEMOVE ([IntPtr]0) $at
  $down = if ($Right) { $script:WM.RBUTTONDOWN } else { $script:WM.LBUTTONDOWN }
  $up   = if ($Right) { $script:WM.RBUTTONUP }   else { $script:WM.LBUTTONUP }
  $btn  = if ($Right) { 2 } else { 1 }
  Send-PfMessage $Window $down ([IntPtr]$btn) $at
  Send-PfMessage $Window $up   ([IntPtr]0)    $at
  if ($Double) {
    Send-PfMessage $Window $script:WM.LBUTTONDBLCLK ([IntPtr]1) $at
    Send-PfMessage $Window $script:WM.LBUTTONUP     ([IntPtr]0) $at
  }
  Start-Sleep -Milliseconds $SettleMs
}

<#
.SYNOPSIS
  Press, move in steps, release - a paint stroke or a rubber-band selection.
.DESCRIPTION
  The steps are the point. One move from start to end paints two tiles and
  leaves the line between them untouched, because the editor draws where the
  mouse went and not where it ended up.
#>
function Send-PfDrag {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][int]$X1,
        [Parameter(Mandatory)][int]$Y1, [Parameter(Mandatory)][int]$X2,
        [Parameter(Mandatory)][int]$Y2, [int]$Steps = 12, [int]$SettleMs = 150)
  Send-PfMessage $Window $script:WM.MOUSEMOVE ([IntPtr]0) (New-PfLParam $X1 $Y1)
  Send-PfMessage $Window $script:WM.LBUTTONDOWN ([IntPtr]1) (New-PfLParam $X1 $Y1)
  for ($i = 1; $i -le $Steps; $i++) {
    $x = [int]($X1 + ($X2 - $X1) * $i / $Steps)
    $y = [int]($Y1 + ($Y2 - $Y1) * $i / $Steps)
    Send-PfMessage $Window $script:WM.MOUSEMOVE ([IntPtr]1) (New-PfLParam $x $y)
    Start-Sleep -Milliseconds 15
  }
  Send-PfMessage $Window $script:WM.LBUTTONUP ([IntPtr]0) (New-PfLParam $X2 $Y2)
  Start-Sleep -Milliseconds $SettleMs
}

<#
.SYNOPSIS
  Park the pointer at a fixed spot before a shot meant to be compared.
.DESCRIPTION
  The canvas draws a brush preview under the pointer, so two otherwise equal
  shots differ wherever the mouse was last left - a drag that ends at 320,240
  leaves a 30x30 ghost there and a comparison calls it a change. Park both
  shots at the same place and the difference is only what the edit did.
#>
function Move-PfMouse {
  param([Parameter(Mandatory)][IntPtr]$Window, [int]$X = 4, [int]$Y = 4, [int]$SettleMs = 120)
  Send-PfMessage $Window $script:WM.MOUSEMOVE ([IntPtr]0) (New-PfLParam $X $Y)
  Start-Sleep -Milliseconds $SettleMs
}

function Send-PfWheel {
  param([Parameter(Mandatory)][IntPtr]$Window, [int]$Notches = 1, [int]$X = 10, [int]$Y = 10)
  # WM_MOUSEWHEEL carries screen coordinates, not client ones.
  $r = New-Object PfWin+RECT
  [void][PfWin]::GetWindowRect($Window, [ref]$r)
  $w = [IntPtr](($Notches * 120) -shl 16)
  Send-PfMessage $Window $script:WM.MOUSEWHEEL $w (New-PfLParam ($r.Left + $X) ($r.Top + $Y))
  Start-Sleep -Milliseconds 120
}

# ------------------------------------------------------------------- reading

<#
.SYNOPSIS
  Read a window's text. WM_GETTEXT is below WM_USER, so this one is safe.
.DESCRIPTION
  Not for a status bar: SB_GETTEXT is above WM_USER and kills the target.
  Screenshot that instead.
#>
function Get-PfText {
  param([Parameter(Mandatory)][IntPtr]$Window)
  $buf = New-Object Text.StringBuilder 2048
  [void][PfWin]::SendMessage($Window, [uint32]$script:WM.GETTEXT, [IntPtr]2048, $buf)
  $buf.ToString()
}

<#
.SYNOPSIS
  Read an edit control's value across a process, which WM_GETTEXT will not do.
.DESCRIPTION
  An Edit in another process answers WM_GETTEXT with an empty string here, so
  this asks UI Automation instead, which is built for reading another process
  and needs no focus. Static and Button text comes back fine from
  GetWindowText, so this is only for fields the user types in.
#>
function Get-PfValue {
  param([Parameter(Mandatory)][IntPtr]$Window)
  Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction SilentlyContinue
  $el = [Windows.Automation.AutomationElement]::FromHandle($Window)
  if (-not $el) { return $null }
  $pattern = $null
  if ($el.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
    return $pattern.Current.Value
  }
  # A combo box answers through its selection rather than a value.
  if ($el.TryGetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern, [ref]$pattern)) {
    $sel = $pattern.Current.GetSelection()
    if ($sel.Length) { return $sel[0].Current.Name }
  }
  $el.Current.Name
}

<#
.SYNOPSIS
  Pair a property form's labels with the controls they name.
.DESCRIPTION
  PUDForgeForm builds its rows at run time, so the fields have no ids worth
  naming - the label is a Static with id 0 and the field is whatever input
  control comes next. Reading them in z-order and pairing them is how a test
  asks for "Gold Cost" instead of "the edit with id 105", which would change
  the moment a row is inserted above it.
#>
function Get-PfFormFields {
  param([Parameter(Mandatory)][IntPtr]$Window)
  $fields = [ordered]@{}
  $label = $null
  # Visible only. A tabbed sheet keeps every page's controls as children and
  # hides the ones not on top, so an unfiltered walk pairs labels from a tab
  # nobody is looking at - which is how "Gold Cost" comes back as a checkbox
  # from the Restrictions page.
  foreach ($c in (Get-PfChildren $Window | Where-Object { $_.Visible })) {
    if ($c.Class -eq 'Static') {
      if ($c.Text -and $c.Text.Trim()) { $label = $c.Text.Trim() }
      continue
    }
    if ($c.Class -in @('Edit', 'Button', 'ComboBox') -and $label) {
      if (-not $fields.Contains($label)) { $fields[$label] = $c }
      $label = $null
    }
  }
  $fields
}

<#
.SYNOPSIS
  PrintWindow a window to a PNG. Never brings it to the front.
#>
function Save-PfShot {
  param([Parameter(Mandatory)][IntPtr]$Window, [Parameter(Mandatory)][string]$Path)
  $r = New-Object PfWin+RECT
  [void][PfWin]::GetWindowRect($Window, [ref]$r)
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  if ($w -le 0 -or $h -le 0) { throw "Window has no area to capture." }
  $bmp = New-Object Drawing.Bitmap $w, $h
  $g = [Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  # 2 = PW_RENDERFULLCONTENT, which is what captures a window drawn with a
  # composited or layered child. Without it the canvas comes back black.
  [void][PfWin]::PrintWindow($Window, $dc, 2)
  $g.ReleaseHdc($dc); $g.Dispose()
  $dir = Split-Path $Path -Parent
  if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  $bmp.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  (Resolve-Path $Path).Path
}

<#
.SYNOPSIS
  How much of a shot is not the background - a blunt "did anything paint".
#>
function Measure-PfShot {
  param([Parameter(Mandatory)][string]$Path)
  $bmp = New-Object Drawing.Bitmap $Path
  $seen = @{}
  for ($y = 0; $y -lt $bmp.Height; $y += 4) {
    for ($x = 0; $x -lt $bmp.Width; $x += 4) {
      $seen[$bmp.GetPixel($x, $y).ToArgb()] = $true
    }
  }
  $result = [pscustomobject]@{ Path = $Path; W = $bmp.Width; H = $bmp.Height; Colours = $seen.Count }
  $bmp.Dispose()
  $result
}

# ------------------------------------------------------------------ selftest

if ($SelfTest) {
  $out = Join-Path $env:TEMP 'pf-ui-harness'
  New-Item -ItemType Directory -Force $out | Out-Null
  $fail = 0
  function Check($name, [scriptblock]$body) {
    try { & $body; Write-Host ("  ok    {0}" -f $name) }
    catch { $script:fail++; Write-Host ("  FAIL  {0}`n        {1}" -f $name, $_.Exception.Message) }
  }

  Write-Host "ui-harness self-test"
  $app = Start-PfEditor -Map 'test\fixtures\skirmish-a.pud'
  Write-Host ("  window up in {0:N2}s" -f $app.Started)
  try {
    Check "the guardrail refuses a message above WM_USER" {
      try { Send-PfMessage $app.Main 0x040A ([IntPtr]0) ([IntPtr]0); throw "it was allowed" }
      catch { if ($_.Exception.Message -notlike '*WM_USER*') { throw } }
    }
    Check "resource.h resolves a command name" {
      if ((Get-PfCommandId 'IDM_FILE_NEW') -ne 200) { throw "IDM_FILE_NEW is not 200" }
    }
    Check "the canvas is there" { $null = Get-PfCanvas $app }
    Check "the main window has children" {
      if ((Get-PfChildren $app.Main).Count -lt 3) { throw "too few children" }
    }
    $canvas = Get-PfCanvas $app
    Check "a shot of the canvas has real content" {
      $p = Save-PfShot $canvas (Join-Path $out 'canvas.png')
      $m = Measure-PfShot $p
      if ($m.Colours -lt 32) { throw "only $($m.Colours) colours - it looks blank" }
    }
    Check "a drag paints, and changes the canvas" {
      Move-PfMouse $canvas
      $before = Save-PfShot $canvas (Join-Path $out 'before.png')
      Send-PfDrag $canvas 120 120 320 240
      Move-PfMouse $canvas
      $after = Save-PfShot $canvas (Join-Path $out 'after.png')
      $b = (Get-FileHash $before).Hash; $a = (Get-FileHash $after).Hash
      if ($a -eq $b) { throw "the canvas did not change" }
    }
    Check "undo puts it back" {
      Invoke-PfMenu $app 'IDM_EDIT_UNDO' -SettleMs 400
      Move-PfMouse $canvas
      $undone = Save-PfShot $canvas (Join-Path $out 'undone.png')
      $b = (Get-FileHash (Join-Path $out 'before.png')).Hash
      if ((Get-FileHash $undone).Hash -ne $b) { throw "the canvas did not come back" }
    }
    Check "a menu command opens a dialog, and Escape closes it" {
      Invoke-PfMenu $app 'IDM_MAP_PROPERTIES' -SettleMs 600
      $dlg = Get-PfDialog $app
      if ($dlg -eq [IntPtr]::Zero) { throw "no dialog appeared" }
      $null = Save-PfShot $dlg (Join-Path $out 'dialog.png')
      Send-PfKey $dlg 'ESC' -SettleMs 400
      if ((Get-PfDialog $app -TimeoutMs 800) -ne [IntPtr]::Zero) { throw "it stayed open" }
    }
    $null = Save-PfShot $app.Main (Join-Path $out 'main.png')
  }
  finally { Stop-PfEditor $app }

  Write-Host ""
  if ($fail) { Write-Host "$fail failed. Shots in $out"; exit 1 }
  Write-Host "all passed. Shots in $out"
}

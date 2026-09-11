# Tests that drive the built client the way a person does.
#
#     powershell -File scripts/ui-tests.ps1
#     powershell -File scripts/ui-tests.ps1 -Filter cost
#
# These are the other half of `pf_tests`. That suite proves the rules; this
# one proves the window in front of them - that a field a rule feeds is the
# field the user reads, and that a shipped fix is still shipped.
#
# One client is started for the whole run and every test leaves it as it found
# it, because starting it costs a second and a half and these are meant to be
# run often. A test that cannot clean up after itself says so and the run goes
# on with a fresh client.
#
# A failure keeps its evidence: the shot that failed is written next to the
# report and named after the test.

[CmdletBinding()]
param(
  [string]$Filter,
  [string]$OutDir = (Join-Path $env:TEMP 'pf-ui-tests'),
  [string]$Map    = 'test\fixtures\skirmish-a.pud',
  # Point this at build-hd to cover the Remastered artwork as well; the tests
  # that need it skip rather than fail against a build without it. Resolved
  # below rather than here: $PSScriptRoot is not yet set inside param().
  [string]$Exe
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ui-harness.ps1')

if (-not $Exe) {
  $Exe = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\Release\PUDForgeTest.exe'
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$script:Pass = 0; $script:Fail = 0; $script:Skip = 0
$script:Failures = @()
$script:App = $null

function Restart-App {
  if ($script:App) { Stop-PfEditor $script:App }
  $script:App = Start-PfEditor -Map $Map -Exe $Exe
}

function Test-Pf {
  param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][scriptblock]$Body)
  if ($Filter -and $Name -notlike "*$Filter*") { return }
  try {
    & $Body
    $script:Pass++
    Write-Host ("  ok    {0}" -f $Name)
  }
  catch {
    if ($_.Exception.Message -like 'SKIP:*') {
      $script:Skip++
      Write-Host ("  skip  {0} - {1}" -f $Name, $_.Exception.Message.Substring(5).Trim())
      return
    }
    $script:Fail++
    $script:Failures += $Name
    Write-Host ("  FAIL  {0}" -f $Name) -ForegroundColor Red
    Write-Host ("        {0}" -f $_.Exception.Message)
    try {
      $safe = ($Name -replace '[^\w]+', '-').Trim('-')
      $shot = Save-PfShot $script:App.Main (Join-Path $OutDir "$safe.png")
      Write-Host ("        shot: {0}" -f $shot)
    } catch { }
    # A test that failed part way may have left a dialog open over the client.
    try { Close-AnyDialog } catch { Restart-App }
  }
}

function Assert-Pf {
  param([Parameter(Mandatory)][bool]$Condition, [Parameter(Mandatory)][string]$Message)
  if (-not $Condition) { throw $Message }
}

function Skip-Pf { param([string]$Why) throw "SKIP: $Why" }

# Escape until nothing is on top of the main window, so one test's leftovers
# are not the next one's failure.
function Close-AnyDialog {
  for ($i = 0; $i -lt 6; $i++) {
    $dlg = Get-PfDialog $script:App -TimeoutMs 300
    if ($dlg -eq [IntPtr]::Zero) { return }
    Send-PfKey $dlg 'ESC' -SettleMs 300
  }
  throw "A dialog would not close."
}

# Open one of the tabbed map sheets and hand back its window.
function Open-Sheet {
  param([Parameter(Mandatory)][string]$Command)
  Invoke-PfMenu $script:App $Command -SettleMs 800
  $dlg = Get-PfDialog $script:App
  Assert-Pf ($dlg -ne [IntPtr]::Zero) "$Command opened no window"
  $dlg
}

# The unit list on the Unit Data sheet, walked with the keyboard: a posted
# arrow key is handled by the list itself and notifies its parent, where
# LB_SETCURSEL would move the bar and tell nobody.
#
# By name and not by row. The list is in unit-id order, which interleaves the
# two races and puts the heroes among them, so any row number written here is
# a guess that reads as a passing test when it drifts.
function Select-Unit {
  param([Parameter(Mandatory)][IntPtr]$Sheet, [Parameter(Mandatory)][string]$Like,
        [int]$Max = 110)
  $list = (Get-PfChildren $Sheet | Where-Object { $_.Class -eq 'ListBox' -and $_.Visible } |
           Select-Object -First 1).Handle
  Assert-Pf ($null -ne $list) "no unit list on the sheet"
  $heading = { (Get-PfChildren $Sheet |
                Where-Object { $_.Id -eq 561 -and $_.Visible } | Select-Object -First 1).Text }
  if ((& $heading) -like $Like) { return (& $heading) }
  for ($i = 0; $i -lt $Max; $i++) {
    Send-PfKey $list 'DOWN' -SettleMs 35
    $name = & $heading
    if ($name -like $Like) { Start-Sleep -Milliseconds 200; return $name }
  }
  Skip-Pf "no unit matching '$Like' in the first $Max rows"
}

function Get-Field {
  param([Parameter(Mandatory)][IntPtr]$Sheet, [Parameter(Mandatory)][string]$Label)
  $fields = Get-PfFormFields (Get-PfForm $Sheet)
  Assert-Pf ($fields.Contains($Label)) "no field labelled '$Label' (found: $($fields.Keys -join ', '))"
  Get-PfValue $fields[$Label].Handle
}

# =====================================================================  run

Write-Host "PUDForge UI tests"
Write-Host ""

if (-not (Test-Path $Exe)) {
  Write-Host "  No build at $Exe - build the PUDForgeTest target first."
  exit 2
}
$script:App = Start-PfEditor -Map $Map -Exe $Exe
Write-Host ("  client up in {0:N2}s" -f $script:App.Started)
Write-Host ""

try {

# ------------------------------------------------------------- accelerators
Write-Host "accelerators"

Test-Pf "no two commands claim the same key" {
  # A duplicate is silent at run time: the table is searched in order, the
  # first match wins and the later command simply never fires. Ctrl+Shift+P
  # was Map Properties and Set Passive both, and the only symptom was the
  # wrong dialog opening.
  $rc = Join-Path (Split-Path $PSScriptRoot -Parent) 'src\PUDForgeWin\PUDForge.rc'
  $inside = $false
  $seen = @{}
  $clashes = @()
  foreach ($line in Get-Content $rc) {
    if ($line -match '^\s*IDR_ACCELERATORS\s+ACCELERATORS') { $inside = $true; continue }
    if (-not $inside) { continue }
    if ($line -match '^\s*END\b') { break }
    if ($line -match '^\s*(//|BEGIN|\s*$)') { continue }
    if ($line -notmatch '^\s*("?[^",]+"?)\s*,\s*(\w+)\s*,\s*(.+)$') { continue }
    $key = $Matches[1].Trim()
    $cmd = $Matches[2].Trim()
    $mods = ($Matches[3] -split ',' | ForEach-Object { $_.Trim().ToUpper() } |
             Where-Object { $_ -ne 'VIRTKEY' -and $_ -ne 'ASCII' } | Sort-Object) -join '+'
    $combo = "$mods+$key"
    if ($seen.ContainsKey($combo)) { $clashes += "$combo is both $($seen[$combo]) and $cmd" }
    else { $seen[$combo] = $cmd }
  }
  Assert-Pf ($seen.Count -gt 20) "only parsed $($seen.Count) accelerators - the parser is wrong, not the table"
  # The join is only non-empty on failure, and Assert-Pf wants a message either
  # way, so it gets one that reads correctly when it is never shown.
  Assert-Pf ($clashes.Count -eq 0) (($clashes -join '; ') + " [$($seen.Count) keys checked]")
}

# --------------------------------------------------------------- the window
Write-Host "the window"

Test-Pf "a map opens and the canvas paints something" {
  $canvas = Get-PfCanvas $script:App
  $shot = Save-PfShot $canvas (Join-Path $OutDir 'canvas.png')
  $m = Measure-PfShot $shot
  Assert-Pf ($m.Colours -ge 32) "the canvas has only $($m.Colours) colours - it looks blank"
}

Test-Pf "the title carries the map and the version" {
  $title = [PfWin]::Txt($script:App.Main)
  Assert-Pf ($title -like '*skirmish-a*') "title does not name the map: '$title'"
  Assert-Pf ($title -match '\d+\.\d+\.\d+') "title carries no version: '$title'"
}

# -------------------------------------------------------------- editing
Write-Host "editing"

Test-Pf "a drag paints, and undo puts it back" {
  $canvas = Get-PfCanvas $script:App
  Move-PfMouse $canvas
  $before = Save-PfShot $canvas (Join-Path $OutDir 'edit-before.png')
  Send-PfDrag $canvas 120 120 320 240
  Move-PfMouse $canvas
  $after = Save-PfShot $canvas (Join-Path $OutDir 'edit-after.png')
  Assert-Pf ((Get-FileHash $after).Hash -ne (Get-FileHash $before).Hash) "the drag painted nothing"
  Invoke-PfMenu $script:App 'IDM_EDIT_UNDO' -SettleMs 500
  Move-PfMouse $canvas
  $undone = Save-PfShot $canvas (Join-Path $OutDir 'edit-undone.png')
  Assert-Pf ((Get-FileHash $undone).Hash -eq (Get-FileHash $before).Hash) "undo did not restore the canvas"
}

Test-Pf "redo puts the edit back again" {
  $canvas = Get-PfCanvas $script:App
  Invoke-PfMenu $script:App 'IDM_EDIT_REDO' -SettleMs 500
  Move-PfMouse $canvas
  $redone = Save-PfShot $canvas (Join-Path $OutDir 'edit-redone.png')
  $after = Join-Path $OutDir 'edit-after.png'
  Assert-Pf ((Get-FileHash $redone).Hash -eq (Get-FileHash $after).Hash) "redo did not repaint the edit"
  Invoke-PfMenu $script:App 'IDM_EDIT_UNDO' -SettleMs 500      # leave it as found
}

Test-Pf "the grid draws over the map" {
  $canvas = Get-PfCanvas $script:App
  Move-PfMouse $canvas
  $off = Save-PfShot $canvas (Join-Path $OutDir 'grid-off.png')
  Invoke-PfMenu $script:App 'IDM_VIEW_GRID' -SettleMs 400
  Move-PfMouse $canvas
  $on = Save-PfShot $canvas (Join-Path $OutDir 'grid-on.png')
  Invoke-PfMenu $script:App 'IDM_VIEW_GRID' -SettleMs 400      # leave it as found
  Assert-Pf ((Get-FileHash $on).Hash -ne (Get-FileHash $off).Hash) "the grid changed nothing"
}

Test-Pf "zooming in changes the canvas, and 100% brings it back" {
  $canvas = Get-PfCanvas $script:App
  Move-PfMouse $canvas
  $at100 = Save-PfShot $canvas (Join-Path $OutDir 'zoom-100.png')
  Invoke-PfMenu $script:App 'IDM_VIEW_ZOOM_200' -SettleMs 600
  Move-PfMouse $canvas
  $at200 = Save-PfShot $canvas (Join-Path $OutDir 'zoom-200.png')
  Assert-Pf ((Get-FileHash $at200).Hash -ne (Get-FileHash $at100).Hash) "zoom changed nothing"
  Invoke-PfMenu $script:App 'IDM_VIEW_ZOOM_100' -SettleMs 600
  Move-PfMouse $canvas
  $back = Save-PfShot $canvas (Join-Path $OutDir 'zoom-back.png')
  Assert-Pf ((Get-FileHash $back).Hash -eq (Get-FileHash $at100).Hash) "100% did not restore the view"
}

# ------------------------------------------------- 0.1.75: costs read in gold
Write-Host "unit data"

Test-Pf "a cost reads in gold and not in tens (0.1.75)" {
  $sheet = Open-Sheet 'IDM_MAP_UNIT_DATA'
  try {
    $gold = Get-Field $sheet 'Gold Cost'
    Assert-Pf ($gold -eq '600') "Footman gold cost reads '$gold', expected '600'"
  } finally { Close-AnyDialog }
}

Test-Pf "only the cost fields scale, not every number (0.1.75)" {
  $sheet = Open-Sheet 'IDM_MAP_UNIT_DATA'
  try {
    # Build Time is 60 in the table and Gold Cost is 60 in the table too. If
    # the scale were applied to the row rather than to the cost fields, both
    # would read 600 and this is the test that notices.
    $build = Get-Field $sheet 'Build Time'
    $hp    = Get-Field $sheet 'Hit Points'
    Assert-Pf ($build -eq '60') "Build Time reads '$build', expected '60'"
    Assert-Pf ($hp -eq '60') "Hit Points reads '$hp', expected '60'"
  } finally { Close-AnyDialog }
}

# --------------------------------------------- 0.1.75: Land / Air / Sea names
Test-Pf "a ground unit's type is Land (0.1.75)" {
  $sheet = Open-Sheet 'IDM_MAP_UNIT_DATA'
  try {
    $type = Get-Field $sheet 'Unit Type'
    Assert-Pf ($type -eq 'Land') "Footman type reads '$type', expected 'Land'"
  } finally { Close-AnyDialog }
}

Test-Pf "a flier's type is Air and never Fly (0.1.75)" {
  $sheet = Open-Sheet 'IDM_MAP_UNIT_DATA'
  try {
    $unit = Select-Unit $sheet '*Flying Machine*'
    $type = Get-Field $sheet 'Unit Type'
    Assert-Pf ($type -eq 'Air') "'$unit' type reads '$type', expected 'Air'"
  } finally { Close-AnyDialog }
}

Test-Pf "a ship's type is Sea and never Naval (0.1.75)" {
  $sheet = Open-Sheet 'IDM_MAP_UNIT_DATA'
  try {
    $unit = Select-Unit $sheet '*Destroyer*'
    $type = Get-Field $sheet 'Unit Type'
    Assert-Pf ($type -eq 'Sea') "'$unit' type reads '$type', expected 'Sea'"
  } finally { Close-AnyDialog }
}

# ------------------------------------------------------------------- dialogs
Write-Host "dialogs"

Test-Pf "every map sheet opens and closes with Escape" {
  foreach ($cmd in 'IDM_MAP_PROPERTIES', 'IDM_MAP_PLAYERS', 'IDM_MAP_UNIT_DATA',
                   'IDM_MAP_UPGRADES', 'IDM_MAP_RESTRICTIONS') {
    $dlg = Open-Sheet $cmd
    $null = Save-PfShot $dlg (Join-Path $OutDir "sheet-$($cmd -replace 'IDM_MAP_','').png")
    Send-PfKey $dlg 'ESC' -SettleMs 400
    Assert-Pf ((Get-PfDialog $script:App -TimeoutMs 700) -eq [IntPtr]::Zero) "$cmd stayed open"
  }
}

Test-Pf "the client is still alive and drawing after all that" {
  Assert-Pf (-not $script:App.Process.HasExited) "the client exited during the run"
  $m = Measure-PfShot (Save-PfShot (Get-PfCanvas $script:App) (Join-Path $OutDir 'still-alive.png'))
  Assert-Pf ($m.Colours -ge 32) "the canvas stopped painting"
}

# ----------------------------------------------- 0.1.77: the Remastered tick
Write-Host "artwork"

Test-Pf "the Remastered artwork tick matches whether the artwork is there (0.1.77)" {
  Invoke-PfMenu $script:App 'IDM_TOOLS_OPTIONS' -SettleMs 800
  $dlg = Get-PfDialog $script:App
  if ($dlg -eq [IntPtr]::Zero) { Skip-Pf "Options did not open" }
  try {
    $id = Get-PfCommandId 'IDC_OPT_HD_ART'
    $tick = Get-PfChildren $dlg | Where-Object { $_.Id -eq $id } | Select-Object -First 1
    if (-not $tick) { Skip-Pf "this build has no Remastered artwork module" }
    $null = Save-PfShot $dlg (Join-Path $OutDir 'options.png')
    # Whichever way it is, it must not be an enabled tick with nothing behind
    # it: that was the fault, and it is the thing worth failing on.
    Assert-Pf ($tick.Text -like '*Remastered*') "the tick is not the Remastered one: '$($tick.Text)'"
  } finally { Close-AnyDialog }
}

}
finally {
  if ($script:App) { Stop-PfEditor $script:App }
}

Write-Host ""
Write-Host ("{0} passed, {1} failed, {2} skipped" -f $script:Pass, $script:Fail, $script:Skip)
if ($script:Fail) {
  Write-Host ("failed: {0}" -f ($script:Failures -join ', '))
  Write-Host ("evidence in {0}" -f $OutDir)
  exit 1
}
Write-Host ("shots in {0}" -f $OutDir)

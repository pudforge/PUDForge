<#
.SYNOPSIS
  What users have asked for, split by whether it may be worked on yet.

.DESCRIPTION
  Feedback arrives from Discord as an issue labelled `feedback` `triage`. It is
  not work until a person adds `accepted`; that gate is the whole point of the
  loop and this script exists to make it visible rather than to bypass it.

  Three lists, and the order is the order they matter in:

    Ready      accepted, open        - may be worked on now
    Waiting    triage, not accepted  - needs Kalle to say yes or no
    Blocked    accepted, blocked     - looked at, and could not be done

.PARAMETER Number
  Show one issue in full rather than the lists.

.PARAMETER Ready
  Only the actionable list, one number per line. For a script that wants to
  iterate rather than a person who wants to read.

.EXAMPLE
  ./scripts/feedback.ps1
  ./scripts/feedback.ps1 -Number 12
  ./scripts/feedback.ps1 -Ready
#>
[CmdletBinding()]
param(
  [int]$Number = 0,
  [switch]$Ready
)

$ErrorActionPreference = 'Stop'
$repo = 'pudforge/PUDForge'

function Get-Issues($labels) {
  # --state open on purpose: a closed issue is a decided one, and this script
  # is only ever about what is still open.
  $json = gh issue list --repo $repo --state open --label $labels `
    --limit 100 --json number,title,labels,createdAt,url 2>$null
  if (-not $json) { return @() }
  return $json | ConvertFrom-Json
}

function Has-Label($issue, $name) {
  return [bool]($issue.labels | Where-Object { $_.name -eq $name })
}

if ($Number -gt 0) {
  gh issue view $Number --repo $repo --comments
  exit 0
}

$accepted = Get-Issues 'accepted'
$blocked = @($accepted | Where-Object { Has-Label $_ 'blocked' })
# Not $ready: PowerShell is case-insensitive, so that is the -Ready switch
# itself, and assigning an array to it makes the parameter unbindable.
$actionable = @($accepted | Where-Object { -not (Has-Label $_ 'blocked') })

if ($Ready) {
  # Nothing but numbers, so a caller can loop over them.
  $actionable | ForEach-Object { $_.number }
  exit 0
}

$waiting = @(Get-Issues 'triage' | Where-Object { -not (Has-Label $_ 'accepted') })

function Show($title, $issues, $colour, $note) {
  Write-Host ''
  Write-Host "== $title ($($issues.Count))" -ForegroundColor $colour
  if ($note) { Write-Host "   $note" -ForegroundColor DarkGray }
  if (-not $issues.Count) { Write-Host '   nothing' -ForegroundColor DarkGray; return }
  foreach ($i in $issues) {
    $kind = if (Has-Label $i 'idea') { 'idea' } else { 'bug ' }
    $age = [int]((Get-Date) - [datetime]$i.createdAt).TotalDays
    Write-Host ("   #{0,-5} {1} {2,3}d  {3}" -f $i.number, $kind, $age, $i.title)
  }
}

Show 'Ready to work' $actionable 'Green' 'accepted, and nothing is stopping them'
Show 'Waiting for you' $waiting 'Yellow' 'add `accepted` to allow work, or close to decline'
Show 'Blocked' $blocked 'Red' 'accepted, but could not be done - read the comment'

Write-Host ''
Write-Host '   ./scripts/feedback.ps1 -Number N   to read one' -ForegroundColor DarkGray
Write-Host ''

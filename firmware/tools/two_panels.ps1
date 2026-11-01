# Two-panel checks: both panels on USB at once, driven over their serial ports.
#
#   powershell -ExecutionPolicy Bypass -File tools\two_panels.ps1
#   powershell -ExecutionPolicy Bypass -File tools\two_panels.ps1 -Upstairs COM5 -Downstairs COM6 -Quiet
#
# It asks each port which floor its panel is (`info`), then runs the checks in docs/second-panel.md:
# the radio set-up, both clocks, the calendars, the Outside tile, status and dog each way, then mail
# and the doorbell each way. Mail and the doorbell play sounds; -Quiet leaves them out. Whatever it
# changes (status, dog, mail) it puts back at the end, even when a check fails. What only eyes can
# check (the banner, the bell shaking) it prints as LOOK lines. Each run's serial traffic from both
# panels goes to sim/out/two-panels/.
#
# Serial from PowerShell is fine; builds aren't (Bitdefender), they stay in bash (tools/fw.sh).
# Close any serial monitor on the panels' ports first. With one panel it runs the checks one panel
# can answer. Written 2026-11-01 before the second panel arrived: the two-panel parts are
# UNVERIFIED until it runs with both.

param(
  [string]$Upstairs,         # its port, e.g. COM5; found by asking each port when left out
  [string]$Downstairs,
  [switch]$Quiet,            # leave out mail and the doorbell, which play sounds
  [int]$Pause = 2,           # seconds after each change, to see it on the screens
  [int]$CalendarWait = 120,  # seconds to wait for the two calendars to match
  [int]$WeatherWait = 0,     # minutes to wait for downstairs's Outside tile (it fills at the next weather send)
  [int]$ReplyWait = 15       # seconds to wait for a reply tapped on the rung panel's banner; 0 skips it
)

$ErrorActionPreference = 'Stop'
$Inv = [Globalization.CultureInfo]::InvariantCulture
$StatusNum = @{ OPEN = 0; BUSY = 1; PRIVATE = 2; SLEEPING = 3; AWAY = 4 }
$Quality = @('none', 'from the other panel or the weather', 'set by hand', 'from the internet')

$outDir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\sim\out\two-panels'
New-Item -ItemType Directory -Force $outDir | Out-Null
$script:logPath = Join-Path $outDir ((Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$script:results = New-Object System.Collections.ArrayList

function Log([string]$text) { Add-Content -Path $script:logPath -Value $text -Encoding utf8 }

function Result([string]$kind, [string]$text) {
  [void]$script:results.Add([pscustomobject]@{ Kind = $kind; Text = $text })
  $color = @{ PASS = 'Green'; FAIL = 'Red'; WARN = 'Yellow'; SKIP = 'DarkGray'; LOOK = 'Cyan'; NOTE = 'Gray' }[$kind]
  Write-Host ('{0,-5} {1}' -f $kind, $text) -ForegroundColor $color
  Log "== $kind $text"
}

# ---- Serial ----

# Whatever the panel has sent since the last look, returned, and written to the log line by line.
function Take($p) {
  $chunk = ''
  try { $chunk = $p.Port.ReadExisting() } catch {}
  if ($chunk) {
    $lines = ($p.Partial + $chunk) -split "`n"
    $p.Partial = $lines[-1]
    for ($k = 0; $k -lt $lines.Count - 1; $k++) {
      $l = $lines[$k].TrimEnd("`r")
      if ($l) { Log ('{0} {1,-10} {2}' -f (Get-Date -Format 'HH:mm:ss.fff'), $p.Name, $l) }
    }
  }
  return $chunk
}

# Sends a command and collects the answer until a line matches $until (or for $ms without one).
function Ask($p, [string]$cmd, [string]$until, [int]$ms = 3000) {
  [void](Take $p)
  Log ('{0} {1,-10} > {2}' -f (Get-Date -Format 'HH:mm:ss.fff'), $p.Name, $cmd)
  $p.SentAt = Get-Date  # the panel answers within a loop pass (20 ms): its "s ago" counts from about now
  $p.Port.Write("$cmd`n")
  $text = ''
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $ms) {
    $text += Take $p
    if ($until -and $text -match $until) {
      Start-Sleep -Milliseconds 150
      $text += Take $p
      break
    }
    Start-Sleep -Milliseconds 30
  }
  return $text
}

function Get-Info($p) {
  $t = Ask $p 'info' '(?m)^i2c:.*\n'
  $i = [ordered]@{ Ok = $false; Floor = ''; Status = ''; ClockSet = $false; Quality = 0; Utc = $null; Plays = 0; Heard = 0;
                   Silent = 0; LastSound = -1; Waiting = 0; Ringtone = ''; Slots = -1; At = Get-Date }
  foreach ($l in ($t -split "`r?`n")) {
    if ($l -match '^floor (UPSTAIRS|DOWNSTAIRS), status (\w+), clock (set|NOT SET) \(quality (\d+)(, utc (\d+))?\)') {
      $i.Ok = $true
      $i.Floor = $matches[1]
      $i.Status = $matches[2]
      $i.ClockSet = $matches[3] -eq 'set'
      $i.Quality = [int]$matches[4]
      if ($matches[6]) { $i.Utc = [long]$matches[6] }  # builds from 2026-11-01 on
    } elseif ($l -match '^audio: (\d+) plays \((\d+) cut short, (\d+) heard, (\d+) SILENT.*last sound (-?\d+) at volume \d+;.*ringtone (.+?) held, (.+?) chosen; (\d+) waiting') {
      $i.Plays = [int]$matches[1]
      $i.Heard = [int]$matches[3]
      $i.Silent = [int]$matches[4]
      $i.LastSound = [int]$matches[5]
      $i.Ringtone = $matches[7]
      $i.Waiting = [int]$matches[8]
    } elseif ($l -match '^calendar: (\d+) of \d+ slots') {
      $i.Slots = [int]$matches[1]
    }
  }
  return [pscustomobject]$i
}

function Get-Radio($p) {
  $t = Ask $p 'radio' "(?m)^dog pill:.*\n"
  $r = [ordered]@{ Ok = $false; Board = ''; NotSet = @(); In = 0; Unknown = 0; OtherDrops = 0; LastUnknown = ''; Out = 0;
                   Acked = 0; Missed = 0; Refused = 0; Traces = @(); Outside = ''; OutsideShowing = $false; Mail = '';
                   Pill = ''; OtherTile = ''; At = $p.SentAt }
  foreach ($l in ($t -split "`r?`n")) {
    if ($l -match '^radio: on, channel \d+, this board is ([^;]+);(.*)$') {
      $r.Ok = $true
      $r.Board = $matches[1]
      $macs = $matches[2]
      foreach ($w in 'upstairs', 'downstairs', 'gateway') { if ($macs -match "\b$w not set") { $r.NotSet += $w } }
    } elseif ($l -match '^radio: in (\d+); dropped (\d+) unknown MAC, (\d+) wrong sender byte, (\d+) bad length, (\d+) queue full') {
      $r.In = [int]$matches[1]
      $r.Unknown = [int]$matches[2]
      $r.OtherDrops = [int]$matches[3] + [int]$matches[4] + [int]$matches[5]
    } elseif ($l -match '^radio: last unknown MAC\s+(\S+)') {
      $r.LastUnknown = $matches[1]
    } elseif ($l -match '^radio: out (\d+) \(a radio acknowledged (\d+), none did for (\d+)\), (\d+) refused') {
      $r.Out = [int]$matches[1]
      $r.Acked = [int]$matches[2]
      $r.Missed = [int]$matches[3]
      $r.Refused = [int]$matches[4]
    } elseif ($l -match '^\s*([\d.]+) s ago\s+(in from|out to)\s+(unknown MAC|\S+)\s+(\S+)\s+seq\s+(\d+)') {
      $r.Traces += [pscustomobject]@{ Ago = [double]::Parse($matches[1], $Inv); In = $matches[2] -eq 'in from'; Who = $matches[3];
                                      Type = $matches[4]; Dropped = $l -match 'DROPPED|REFUSED' }
    } elseif ($l -match '^outside tile: (showing|dashes), (.*)$') {
      $r.OutsideShowing = $matches[1] -eq 'showing'
      $r.Outside = $matches[2]
    } elseif ($l -match '^mail: (waiting for downstairs|none waiting)') {
      $r.Mail = $matches[1]
    } elseif ($l -match "^dog pill: (.*); other floor's tile: (.*)$") {
      $r.Pill = $matches[1]
      $r.OtherTile = $matches[2].Trim()
    }
  }
  return [pscustomobject]$r
}

# Runs $probe every quarter second until it returns something, which it returns; $null after $seconds.
function Wait-Until([scriptblock]$probe, [double]$seconds) {
  $__clock = [Diagnostics.Stopwatch]::StartNew()
  do {
    $__got = & $probe
    if ($__got) { return $__got }
    Start-Sleep -Milliseconds 250
  } while ($__clock.Elapsed.TotalSeconds -lt $seconds)
  return $null
}

# How long after $since the newest frame of this type from that board arrived, by the panel's own
# list of recent frames (to a tenth of a second); $null if none came after $since.
function Arrival($radio, [string]$from, [string]$type, [datetime]$since) {
  for ($k = $radio.Traces.Count - 1; $k -ge 0; $k--) {
    $tr = $radio.Traces[$k]
    if (-not $tr.In -or $tr.Dropped -or $tr.Who -ne $from -or $tr.Type -ne $type) { continue }
    $when = $radio.At.AddSeconds(-$tr.Ago)
    if ($when -gt $since.AddSeconds(-0.15)) { return [math]::Max(0.0, ($when - $since).TotalSeconds) }
    return $null
  }
  return $null
}

function After($d) {
  if ($d -eq $null) { return '(no frame in its list to time it by)' }
  return 'after {0} s' -f $d.ToString('0.0', $Inv)
}

function Open-Panel([string]$com) {
  $sp = New-Object System.IO.Ports.SerialPort $com, 115200
  $sp.DtrEnable = $true
  $sp.RtsEnable = $false
  $sp.ReadBufferSize = 65536
  try { $sp.Open() } catch {
    Write-Host "${com}: can't open it (a serial monitor still open on it?)"
    return $null
  }
  $p = [pscustomobject]@{ Com = $com; Port = $sp; Name = $com; Lower = ''; Partial = ''; SentAt = Get-Date }
  Start-Sleep -Milliseconds 300
  $i = Get-Info $p
  if (-not $i.Ok) {  # once more, in case it was busy
    Start-Sleep -Milliseconds 1500
    $i = Get-Info $p
  }
  if (-not $i.Ok) {
    $sp.Close()
    Write-Host "${com}: no panel answered"
    return $null
  }
  $p.Name = $i.Floor
  $p.Lower = $i.Floor.ToLower()
  Write-Host "${com}: $($i.Floor) panel, status $($i.Status)"
  return $p
}

# ---- Checks ----

function Check-Setup($p, $other) {
  $r = Get-Radio $p
  if (-not $r.Ok) {
    Result FAIL "$($p.Name) on $($p.Com): no radio report. Is it the radio build? (bash tools/fw.sh radio $($p.Com))"
    return
  }
  if ($r.Board -ne $p.Lower) {
    Result FAIL "$($p.Name) on $($p.Com): runs as $($p.Name), but its MAC is '$($r.Board)' in secrets.h"
  } else {
    Result PASS "$($p.Name) on $($p.Com): its MAC is the $($r.Board) one in secrets.h"
  }
  $missing = @($r.NotSet | Where-Object { $other -or $_ -eq 'gateway' -or $_ -eq $p.Lower })
  if ($missing.Count) { Result FAIL "$($p.Name) was built with no MAC for: $($missing -join ', '). Fill in secrets.h and flash it again." }
  if (-not $other -and $r.NotSet.Count -gt $missing.Count) {
    Result NOTE "$($p.Name) knows no MAC for the other floor yet (as expected until the second panel's MAC is in secrets.h)"
  }
  if ($r.Unknown) {
    Result WARN "$($p.Name) dropped $($r.Unknown) frames from a MAC it doesn't know (last $($r.LastUnknown)). If that's the other panel, flash this one again."
  }
  if ($r.OtherDrops) { Result WARN "$($p.Name) dropped $($r.OtherDrops) frames for a wrong sender byte, a bad length or a full queue" }
  if (-not $other) { return }
  $seen = Wait-Until { $x = Get-Radio $p; if ($x.OtherTile -and $x.OtherTile -ne 'NO SIGNAL') { $x } } 15
  if ($seen) { Result PASS "$($p.Name) shows $($other.Name) as $($seen.OtherTile)" }
  else { Result FAIL "$($p.Name) still shows NO SIGNAL for $($other.Name) after 15 s" }
  return [bool]$seen
}

function Check-Clock($panels) {
  $read = @()
  foreach ($p in $panels) {
    $i = Get-Info $p
    $read += [pscustomobject]@{ P = $p; I = $i; Pc = [DateTimeOffset]::new($i.At).ToUnixTimeSeconds() }
  }
  foreach ($x in $read) {
    $q = 'quality ' + $x.I.Quality
    if ($x.I.Quality -lt $Quality.Count) { $q = $Quality[$x.I.Quality] }
    if (-not $x.I.ClockSet) { Result FAIL "$($x.P.Name): clock NOT SET"; continue }
    if ($x.I.Utc -eq $null) { Result NOTE "$($x.P.Name): clock set ($q). This build doesn't print the time (it's from before 2026-11-01), so it can't be compared."; continue }
    $off = $x.I.Utc - $x.Pc
    $kind = 'PASS'
    if ([math]::Abs($off) -gt 60) { $kind = 'FAIL' } elseif ([math]::Abs($off) -gt 2) { $kind = 'WARN' }
    Result $kind "$($x.P.Name): clock set $q, $off s from the PC's"
  }
  if ($read.Count -eq 2 -and $read[0].I.Utc -ne $null -and $read[1].I.Utc -ne $null -and $read[0].I.ClockSet -and $read[1].I.ClockSet) {
    $d = ($read[0].I.Utc - $read[0].Pc) - ($read[1].I.Utc - $read[1].Pc)
    $kind = 'PASS'
    if ([math]::Abs($d) -gt 2) { $kind = 'WARN' }
    Result $kind "clocks: the two panels are $([math]::Abs($d)) s apart"
  }
}

function Check-Calendar($a, $b) {
  $ia = Get-Info $a
  $ib = Get-Info $b
  if ($ia.Slots -ne $ib.Slots) { Write-Host "      calendars: $($a.Name) $($ia.Slots) events, $($b.Name) $($ib.Slots); waiting up to $CalendarWait s for them to match" }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($ia.Slots -ne $ib.Slots -and $sw.Elapsed.TotalSeconds -lt $CalendarWait) {
    Start-Sleep -Seconds 10
    $ia = Get-Info $a
    $ib = Get-Info $b
  }
  if ($ia.Slots -ge 0 -and $ia.Slots -eq $ib.Slots) {
    Result PASS "calendars: both hold $($ia.Slots) events (the counts match; the events themselves aren't compared)"
  } else {
    Result FAIL "calendars: $($a.Name) holds $($ia.Slots) events, $($b.Name) $($ib.Slots), after $CalendarWait s"
  }
}

function Check-Outside($up, $down) {
  $ru = Get-Radio $up
  if (-not $ru.OutsideShowing) { Result SKIP "Outside tile: upstairs shows dashes itself (no weather from the gateway lately), so there's nothing to pass on"; return }
  $rd = Get-Radio $down
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while (-not $rd.OutsideShowing -and $sw.Elapsed.TotalMinutes -lt $WeatherWait) {
    Start-Sleep -Seconds 15
    $rd = Get-Radio $down
  }
  if ($rd.OutsideShowing) {
    if ($rd.Outside -eq $ru.Outside) { Result PASS "Outside tile: both show $($rd.Outside)" }
    else { Result WARN "Outside tile: upstairs shows $($ru.Outside), downstairs $($rd.Outside) (a new reading in between?)" }
  } elseif ($WeatherWait -eq 0) {
    Result WARN "Outside tile: downstairs still shows dashes. It fills at the gateway's next weather send; run again with -WeatherWait 15 to wait for it."
  } else {
    Result FAIL "Outside tile: downstairs still shows dashes after $WeatherWait minutes"
  }
}

function Check-Status($a, $b) {
  $orig = (Get-Info $a).Status
  if (-not $StatusNum.ContainsKey($orig)) { Result FAIL "status: $($a.Name) didn't say its status"; return }
  $new = 'BUSY'
  if ($orig -eq 'BUSY') { $new = 'OPEN' }
  $ok = $true
  foreach ($step in @($new, $orig)) {
    $t0 = Get-Date
    [void](Ask $a "status $($StatusNum[$step])" $null 200)
    if (-not $ok) { break }  # back to how it was; with the change never seen, the way back proves nothing
    $hit = Wait-Until { $x = Get-Radio $b; if ($x.OtherTile -eq $step) { $x } } 10
    $what = "status $($a.Name) -> $($b.Name): $($b.Name) showed $step"
    if ($step -eq $orig) { $what = "status $($a.Name) -> $($b.Name), back: $($b.Name) showed $step" }
    if ($hit) { Result PASS "$what $(After (Arrival $hit $a.Lower 'STATE' $t0))" }
    else { Result FAIL "status $($a.Name) -> $($b.Name): $($b.Name) didn't show $step within 10 s"; $ok = $false }
    Start-Sleep -Seconds $Pause
  }
}

function Check-Dog($a, $b) {
  $before = (Get-Radio $a).Pill
  $ok = $true
  foreach ($back in @($false, $true)) {
    $t0 = Get-Date
    [void](Ask $a 'dog' $null 200)
    $script:dogTaps[$a.Name] = -not $script:dogTaps[$a.Name]
    if (-not $ok) { break }  # put back; with the change never seen, the way back proves nothing
    if ($back) { $want = $before }
    else {
      $mine = Wait-Until { $x = Get-Radio $a; if ($x.Pill -ne $before) { $x } } 3
      if (-not $mine) {
        $script:dogTaps[$a.Name] = $false  # the tap didn't take, so there's nothing to put back
        Result FAIL "dog: tapping $($a.Name)'s dog left its own pill as '$before'"
        return
      }
      $want = $mine.Pill
    }
    $hit = Wait-Until { $x = Get-Radio $b; if ($x.Pill -eq $want) { $x } } 10
    $what = "dog $($a.Name) -> $($b.Name)"
    if ($back) { $what += ', back' }
    if ($hit) { Result PASS "${what}: $($b.Name) shows '$want' $(After (Arrival $hit $a.Lower 'DOG' $t0))" }
    else { Result FAIL "${what}: $($b.Name) didn't show '$want' within 10 s"; $ok = $false }
    Start-Sleep -Seconds $Pause
  }
}

# What a panel played since $i0, once its microphone has measured it ($null if nothing played).
function Wait-Sound($p, $i0, [double]$seconds) {
  $done = Wait-Until { $x = Get-Info $p; if ($x.Plays -gt $i0.Plays -and ($x.Heard + $x.Silent) -gt ($i0.Heard + $i0.Silent) -and $x.Waiting -eq 0) { $x } } $seconds
  if ($done) { return $done }
  $x = Get-Info $p
  if ($x.Plays -gt $i0.Plays) { return $x }
  return $null
}

function Say-Sound($p, $i0, $i, [string]$expect) {
  $names = @{ 0 = "the chime ($($i.Ringtone))"; 1 = 'the soft chime'; 2 = 'come on'; 3 = 'not now' }
  if (-not $i) { Result NOTE "$($p.Name) played no sound (its doorbell rules for status $($i0.Status) may say so)"; return }
  $name = $names[$i.LastSound]
  if ($i.Silent -gt $i0.Silent) { Result FAIL "$($p.Name) played $name but its microphone heard nothing"; return }
  if ($i.Heard -gt $i0.Heard) { $how = 'and its microphone heard it' } else { $how = "(its microphone hadn't measured it yet)" }
  $kind = 'PASS'
  if ($expect -and $i.LastSound -ne [int]$expect) { $kind = 'NOTE' }
  Result $kind "$($p.Name) played $name $how"
}

function Check-Mail($up, $down) {
  $ru = Get-Radio $up
  if ($ru.Mail -ne 'none waiting') { Result SKIP "mail: real mail is waiting for downstairs, so the test leaves it alone"; return }
  $id0 = Get-Info $down
  $t0 = Get-Date
  [void](Ask $up 'mail' '(?m)^mail:.*\n' 1500)
  $script:mailMarked = $true
  $hit = Wait-Until { $x = Get-Radio $down; if ($x.Mail -eq 'waiting for downstairs') { $x } } 10
  if (-not $hit) { Result FAIL "mail UPSTAIRS -> DOWNSTAIRS: downstairs didn't get it within 10 s"; return }  # put back at the end
  Result PASS "mail UPSTAIRS -> DOWNSTAIRS: downstairs has mail waiting $(After (Arrival $hit 'upstairs' 'MAIL' $t0))"
  Result LOOK 'DOWNSTAIRS: the mail badge pulses at the right end of the DOWNSTAIRS line, and the screen wakes'
  Say-Sound $down $id0 (Wait-Sound $down $id0 8) 1
  Start-Sleep -Seconds $Pause
  $t1 = Get-Date
  [void](Ask $down 'mail' '(?m)^mail:.*\n' 1500)
  $hit = Wait-Until { $x = Get-Radio $up; if ($x.Mail -eq 'none waiting') { $x } } 10
  if ($hit) {
    $script:mailMarked = $false
    Result PASS "mail acknowledged DOWNSTAIRS -> UPSTAIRS: upstairs shows none waiting $(After (Arrival $hit 'downstairs' 'MAIL' $t1))"
  } else {
    Result FAIL "mail acknowledged DOWNSTAIRS -> UPSTAIRS: upstairs still shows mail waiting after 10 s"
  }
  Start-Sleep -Seconds $Pause
}

function Check-Bell($a, $b) {
  $ib0 = Get-Info $b
  $ia0 = Get-Info $a
  $ra0 = Get-Radio $a
  if ($ib0.Status -ne 'OPEN') { Result NOTE "$($b.Name) is $($ib0.Status): its doorbell rules may soften or silence the chime" }
  $t0 = Get-Date
  [void](Ask $a 'bell' $null 200)
  $hit = Wait-Until { $x = Get-Radio $b; if ((Arrival $x $a.Lower 'KNOCK' $t0) -ne $null) { $x } } 10
  if (-not $hit) { Result FAIL "doorbell $($a.Name) -> $($b.Name): no ring arrived within 10 s"; return }
  Result PASS "doorbell $($a.Name) -> $($b.Name): the ring arrived $(After (Arrival $hit $a.Lower 'KNOCK' $t0))"
  Result LOOK "$($b.Name): the banner shows, and the bell shakes and turns red"
  Say-Sound $b $ib0 (Wait-Sound $b $ib0 15) 0
  $ra = Get-Radio $a
  if ($ra.Acked -gt $ra0.Acked) { Result PASS "$($a.Name)'s radio got the acknowledgement" }
  else { Result WARN "$($a.Name)'s radio counted no acknowledgement for the ring" }
  if ($ReplyWait -gt 0) {
    Result LOOK "On $($b.Name), tap a reply on the banner (come on or not now) within $ReplyWait s, or leave it"
    $rep = Wait-Until { $x = Get-Info $a; if ($x.Plays -gt $ia0.Plays -and ($x.LastSound -eq 2 -or $x.LastSound -eq 3)) { $x } } $ReplyWait
    if ($rep) {
      $said = 'come on'
      if ($rep.LastSound -eq 3) { $said = 'not now' }
      Result PASS "the reply reached $($a.Name), which played '$said'"
    } else {
      Result SKIP "no reply tapped on $($b.Name)"
    }
  }
  Start-Sleep -Seconds $Pause
}

# Everything after the ports are open: the checks, then putting back what they changed.
function Run-Checks($panels) {
  $script:dogTaps = @{}
  $script:mailMarked = $false
  $orig = @{}
  $start = @{}
  foreach ($p in $panels) {
    $orig[$p.Name] = (Get-Info $p).Status
    $start[$p.Name] = Get-Radio $p
  }

  try {
    if ($Upstairs -and ($panels | Where-Object { $_.Com -eq $Upstairs -and $_.Name -ne 'UPSTAIRS' })) { Result FAIL "$Upstairs answers as DOWNSTAIRS, not UPSTAIRS" }
    if ($Downstairs -and ($panels | Where-Object { $_.Com -eq $Downstairs -and $_.Name -ne 'DOWNSTAIRS' })) { Result FAIL "$Downstairs answers as UPSTAIRS, not DOWNSTAIRS" }
    $up = $panels | Where-Object { $_.Name -eq 'UPSTAIRS' } | Select-Object -First 1
    $down = $panels | Where-Object { $_.Name -eq 'DOWNSTAIRS' } | Select-Object -First 1
    $two = $up -and $down
    if ($panels.Count -ge 2 -and -not $two) {
      Result FAIL "both panels answer as $($panels[0].Name). Is the new panel's MAC on TCOMM_MAC_DOWNSTAIRS in secrets.h, and were both flashed after?"
    }

    Write-Host ''
    Write-Host '-- set-up --'
    $hear = $true
    if ($two) {
      if (-not (Check-Setup $up $down)) { $hear = $false }
      if (-not (Check-Setup $down $up)) { $hear = $false }
    } else {
      foreach ($p in $panels) { [void](Check-Setup $p $null) }
    }

    Write-Host ''
    Write-Host '-- clocks, calendars, the Outside tile --'
    Check-Clock $panels
    if ($two) {
      Check-Calendar $up $down
      Check-Outside $up $down
    } else {
      foreach ($p in $panels) {
        $r = Get-Radio $p
        $i = Get-Info $p
        Result NOTE "$($p.Name): calendar $($i.Slots) events; Outside tile $(if ($r.OutsideShowing) { $r.Outside } else { 'dashes' }); dog pill $($r.Pill); other floor's tile $($r.OtherTile)"
      }
      Result SKIP 'status, dog, mail and doorbell each way: they need both panels'
    }

    if ($two -and -not $hear) {
      Result SKIP "status, dog, mail and doorbell: the panels don't hear each other yet"
    } elseif ($two) {
      Write-Host ''
      Write-Host '-- status and dog, each way --'
      Check-Status $up $down
      Check-Status $down $up
      Check-Dog $up $down
      Check-Dog $down $up
      if ($Quiet) {
        Result SKIP 'mail and the doorbell (-Quiet): they play sounds'
      } else {
        Write-Host ''
        Write-Host '-- mail and the doorbell, each way (sounds) --'
        Check-Mail $up $down
        Check-Bell $up $down
        Check-Bell $down $up
      }
    }
  } finally {
    # Put back what the checks changed.
    foreach ($p in $panels) {
      if ($script:dogTaps[$p.Name]) { [void](Ask $p 'dog' $null 300); Write-Host "put back: $($p.Name)'s dog" }
    }
    if ($script:mailMarked -and $up) {
      if ((Get-Radio $up).Mail -eq 'waiting for downstairs') { [void](Ask $up 'mail' '(?m)^mail:.*\n' 1500); Write-Host 'put back: no mail waiting' }
    }
    foreach ($p in $panels) {
      $now = (Get-Info $p).Status
      if ($orig[$p.Name] -and $now -ne $orig[$p.Name]) {
        [void](Ask $p "status $($StatusNum[$orig[$p.Name]])" $null 300)
        Write-Host "put back: $($p.Name) status $($orig[$p.Name])"
      }
    }

    Write-Host ''
    Write-Host '-- radio during the run --'
    foreach ($p in $panels) {
      $r = Get-Radio $p
      $s = $start[$p.Name]
      if ($r.Ok -and $s.Ok) {
        $out = $r.Out - $s.Out
        $missed = $r.Missed - $s.Missed
        $kind = 'PASS'
        if ($out -gt 0 -and $missed * 10 -gt $out) { $kind = 'WARN' }
        Result $kind "$($p.Name): sent $out frames ($missed with no radio acknowledgement, resent), received $($r.In - $s.In), dropped $(($r.Unknown - $s.Unknown) + ($r.OtherDrops - $s.OtherDrops))"
      }
      [void](Take $p)
      $p.Port.Close()
    }
  }
}

# ---- Run ----

$coms = @()
if ($Upstairs) { $coms += $Upstairs }
if ($Downstairs) { $coms += $Downstairs }
if (-not $coms) { $coms = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique }
$panels = @()
foreach ($c in $coms) {
  $p = Open-Panel $c
  if ($p) { $panels += $p }
}
if (-not $panels) { Write-Host "No panel answered on $($coms -join ', ')."; exit 2 }


Run-Checks $panels

$fails = @($script:results | Where-Object { $_.Kind -eq 'FAIL' }).Count
$warns = @($script:results | Where-Object { $_.Kind -eq 'WARN' }).Count
$passes = @($script:results | Where-Object { $_.Kind -eq 'PASS' }).Count
Write-Host ''
Write-Host "$passes passed, $fails failed, $warns warnings. Serial log: $script:logPath"
exit $fails

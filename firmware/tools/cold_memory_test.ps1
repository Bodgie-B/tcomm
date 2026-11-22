# Danger-cold memory test (2026-11-22). Measures what the danger-cold icicles cost in PSRAM and in
# redraw time, on the main screen and with the screensaver running, and checks that turning the
# cold on and off five times leaks nothing.
#
# Needs a panel on USB running a build with the serial "cold test on/off" command, and a fresh
# outside reading from the gateway (the icicles only show while the outside tile has a reading).
# Puts everything back at the end: cold test off, screensaver stopped, screensaver auto start on.
#
#   powershell -ExecutionPolicy Bypass -File tools\cold_memory_test.ps1 -Port COM5
#
# The port is opened with DTR and RTS off, so opening it doesn't restart the panel. The raw serial
# log goes next to the summary in -OutDir (default: docs/memory/cold-test-<date>.txt).
param(
  [Parameter(Mandatory = $true)][string]$Port,
  [int]$Settle = 10,     # seconds after each change before measuring
  [int]$Window = 60,     # seconds of redraw or frame statistics per measurement
  [int]$Cycles = 5,      # cold on/off cycles for the leak check
  [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\memory' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$logPath = Join-Path $OutDir "cold-test-$stamp.log"
$sumPath = Join-Path $OutDir "cold-test-$stamp.txt"
$log = New-Object System.IO.StreamWriter($logPath, $false, [System.Text.Encoding]::ASCII)

$p = New-Object System.IO.Ports.SerialPort $Port, 115200
$p.ReadBufferSize = 2 * 1024 * 1024
$p.DtrEnable = $false
$p.RtsEnable = $false
$p.Open()
Start-Sleep -Milliseconds 500
[void]$p.ReadExisting()

function Send-Cmd([string]$cmd, [double]$secs = 2) {
  $log.WriteLine(">> $cmd"); $log.Flush()
  $p.Write($cmd + "`n")
  $end = (Get-Date).AddSeconds($secs)
  $out = ''
  while ((Get-Date) -lt $end) { $out += $p.ReadExisting(); Start-Sleep -Milliseconds 100 }
  $out = $out.Replace("`r", '')
  $log.WriteLine($out.TrimEnd()); $log.Flush()
  return $out
}
function Wait-Quiet([int]$secs) {
  $end = (Get-Date).AddSeconds($secs)
  while ((Get-Date) -lt $end) { $t = $p.ReadExisting(); if ($t) { $log.Write($t.Replace("`r", '')) }; Start-Sleep -Milliseconds 200 }
  $log.Flush()
}
function Num([string]$text, [string]$pattern) {
  $m = [regex]::Match($text, $pattern)
  if ($m.Success) { return [long]$m.Groups[1].Value } else { return $null }
}

# One "info" reading: PSRAM, internal RAM, icicles, redraw statistics since the last info.
function Read-Info([string]$label) {
  $t = Send-Cmd 'info' 3
  $r = [ordered]@{
    label      = $label
    psram      = Num $t 'PSRAM free (\d+) \(largest block'
    psramBig   = Num $t 'PSRAM free \d+ \(largest block (\d+)\)'
    internal   = Num $t 'internal free (\d+)'
    icicles    = Num $t 'icicles held \((\d+) bytes\)'
    secs       = Num $t 'since the last info \((\d+) s\)'
    redraws    = Num $t '(\d+) redraws'
    px         = Num $t 'redraws, (\d+) px'
    drawMs     = Num $t 'px, (\d+) ms drawing'
    slowest    = Num $t 'slowest (\d+) ms'
    raw        = $t
  }
  if ($null -eq $r.icicles) { $r.icicles = 0 }
  return $r
}
# One "ss stats" reading: frame rate and PSRAM while the screensaver runs.
function Read-SS([string]$label) {
  $t = Send-Cmd 'ss stats' 2
  $fps = [regex]::Match($t, '\(([\d.]+) fps\)')
  $draw = [regex]::Match($t, 'draw ([\d.]+) ms')
  $worst = [regex]::Match($t, 'worst ([\d.]+) ms')
  return [ordered]@{
    label    = $label
    running  = $t -match 'screensaver: running'
    fps      = if ($fps.Success) { $fps.Groups[1].Value } else { '?' }
    draw     = if ($draw.Success) { $draw.Groups[1].Value } else { '?' }
    worst    = if ($worst.Success) { $worst.Groups[1].Value } else { '?' }
    psram    = Num $t 'free PSRAM (\d+) \(largest'
    psramBig = Num $t 'free PSRAM \d+ \(largest (\d+)\)'
    raw      = $t
  }
}

$rows = @(); $ss = @(); $notes = @()
try {
  # --- Setting up: no screensaver starting by itself, cold off, screensaver stopped.
  $radio = Send-Cmd 'radio' 3
  if ($radio -notmatch 'outside tile: showing') { $notes += 'WARNING: the outside tile has no fresh reading, so the icicles cannot show. Results below are not valid.' }
  if ($radio -match 'danger cold') { $notes += 'Note: the gateway itself reported danger cold at the start.' }
  [void](Send-Cmd 'ss auto 0' 2)
  $st = Send-Cmd 'ss stats' 2
  if ($st -match 'screensaver: running') { [void](Send-Cmd 'ss' 3) }
  [void](Send-Cmd 'cold test off' 2)
  Wait-Quiet $Settle
  [void](Read-Info 'reset counters')

  # --- 1. Main screen, no cold (the baseline).
  Wait-Quiet $Window
  $base = Read-Info '1. main screen, no cold'
  $rows += $base
  $audio = ($base.raw -split "`n" | Where-Object { $_ -match 'rendered|chosen|ringtone' }) -join ' | '

  # --- 2. Main screen, danger cold.
  [void](Send-Cmd 'cold test on' 2)
  Wait-Quiet $Settle
  $coldNow = Read-Info '2a. cold just shown'
  $rows += $coldNow
  Wait-Quiet $Window
  $rows += Read-Info '2b. main screen, cold, after the window'

  # --- 3. Screensaver with danger cold (same seed both times so the scenes match).
  $r = Send-Cmd 'ss seed 4242' 4
  Wait-Quiet $Settle
  [void](Read-SS 'reset')
  Wait-Quiet $Window
  $ss += Read-SS '3. screensaver, cold'
  $rows += Read-Info '3. screensaver running, cold'
  [void](Send-Cmd 'ss' 4)
  Wait-Quiet $Settle

  # --- 4. Cold off again: the icicles should be freed and PSRAM back to the baseline.
  [void](Send-Cmd 'cold test off' 2)
  Wait-Quiet $Settle
  $off = Read-Info '4. cold off again'
  $rows += $off

  # --- 5. Screensaver without cold, for comparison.
  $r = Send-Cmd 'ss seed 4242' 4
  Wait-Quiet $Settle
  [void](Read-SS 'reset')
  Wait-Quiet $Window
  $ss += Read-SS '5. screensaver, no cold'
  $rows += Read-Info '5. screensaver running, no cold'
  [void](Send-Cmd 'ss' 4)
  Wait-Quiet $Settle

  # --- 6. Leak check: cold on and off $Cycles times.
  $before = Read-Info '6. before the cycles'
  for ($i = 1; $i -le $Cycles; $i++) {
    [void](Send-Cmd 'cold test on' 2); Wait-Quiet 5
    [void](Send-Cmd 'cold test off' 2); Wait-Quiet 5
  }
  Wait-Quiet $Settle
  $after = Read-Info "6. after $Cycles cold cycles"
  $rows += $before; $rows += $after
}
finally {
  # Put everything back whatever happened.
  try { [void](Send-Cmd 'cold test off' 2) } catch {}
  try { $st = Send-Cmd 'ss stats' 2; if ($st -match 'screensaver: running') { [void](Send-Cmd 'ss' 3) } } catch {}
  try { [void](Send-Cmd 'ss auto 1' 2) } catch {}
  $p.Close()
  $log.Close()
}

# --- Summary.
$out = New-Object System.Collections.Generic.List[string]
$out.Add("Danger-cold memory test, $stamp, port $Port")
$out.Add(($radio -split "`n" | Where-Object { $_ -match 'outside tile' } | Select-Object -First 1))
foreach ($n in $notes) { $out.Add($n) }
if ($audio) { $out.Add("audio: $audio") }
$out.Add('')
# (Each format in its own parentheses: inside Add(...) the commas would split the arguments.)
$out.Add(('{0,-42} {1,10} {2,10} {3,9} {4,9} {5,8} {6,8} {7,8}' -f 'state', 'PSRAM free', 'largest', 'internal', 'icicles', 'redraws', 'draw ms', 'slowest'))
foreach ($r in $rows) {
  $out.Add(('{0,-42} {1,10} {2,10} {3,9} {4,9} {5,8} {6,8} {7,8}' -f $r.label, $r.psram, $r.psramBig, $r.internal, $r.icicles, $r.redraws, $r.drawMs, $r.slowest))
}
$out.Add('')
$out.Add(('{0,-30} {1,6} {2,9} {3,9} {4,10} {5,10}' -f 'screensaver', 'fps', 'draw ms', 'worst ms', 'PSRAM free', 'largest'))
foreach ($s in $ss) { $out.Add(('{0,-30} {1,6} {2,9} {3,9} {4,10} {5,10}' -f $s.label, $s.fps, $s.draw, $s.worst, $s.psram, $s.psramBig)) }
$out.Add('')
if ($base.psram -and $coldNow.psram) { $out.Add("Icicles cost on the main screen: $($base.psram - $coldNow.psram) bytes of PSRAM (expected about 14,400).") }
if ($ss.Count -ge 2 -and $ss[0].psram -and $ss[1].psram) { $out.Add("Cold with the screensaver running costs $($ss[1].psram - $ss[0].psram) bytes more than without.") }
if ($base.psram -and $off.psram) { $out.Add("After cold off: $($off.psram - $base.psram) bytes compared with the baseline (0 or close means everything was freed).") }
if ($before.psram -and $after.psram) { $out.Add("Leak check over $Cycles cycles: $($after.psram - $before.psram) bytes (0 or close means no leak).") }
if ($base.drawMs -ne $null -and $rows[2].drawMs -ne $null) {
  $out.Add("Redraw time over the window: $($base.drawMs) ms without cold, $($rows[2].drawMs) ms with it; slowest single redraw $($base.slowest) ms against $($rows[2].slowest) ms.")
}
$out.Add('')
$out.Add("Raw serial log: $logPath")
$out | Set-Content -Encoding ASCII $sumPath
$out

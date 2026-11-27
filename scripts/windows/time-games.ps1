# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Times compiled games against stock PCSX2 on sessions run again.

.DESCRIPTION
What a release's notes and the website say of speed is measured so
(docs\adr\0047, 0051): a session someone played, run again without a window,
by stock PCSX2 and by the compiled game in turns, timed from its first frame
to its last.

  stock   stock PCSX2's tree built as PCSX2's releases are: link-time
          optimization, no profile, none of PS2Native's changes to PCSX2's
          code (build.ps1 -Module on -RuntimeLto on -RuntimePgo off
          -Pcsx2Patches off, into -StockTree)
  twins   beside each measurement folder, two twins of it: <folder>-release
          with the runtime of the tree a release is made from, <folder>-stock
          with the stock tree's. Both run the folder's own modules
  time    -Rounds rounds, in turns, the order turned round every other round:
          stock PCSX2 (stock-jit) and the compiled game (ps2n-static), both
          with VU1 on PCSX2's VU thread, as PCSX2 has it by default where the
          computer has three cores or more. -CpuThread: both with VU1 on the
          main processor thread instead

Which games and sessions: a list in the build tree, which git ignores
(-List, build\measure\timed.txt), one line to a game:

    <name> | <disc image> | <measurement folder> | <session> | <renderer>

A measurement folder is a copy of a game's folder, never the folder someone
plays from; its counts are its own, taken on another session than the one
timed. The renderer is null where the game allows it (PCSX2's null renderer
asserts on Gran Turismo 3's drawing: vulkan there).

Time alone on the computer: two runs at once, or a game played meanwhile,
spoil the numbers. Two rounds of four games take about ten minutes.

What it prints is a measurement of this computer on these sessions. A speed
claim made from it says which stock PCSX2 it is against: the first line of
the table says so.

Every run is appended to -Results (build\measure\results.txt), one line each.

.EXAMPLE
& scripts\windows\hidden.ps1 -Script time-games.ps1 -Arguments '-Bios C:\bios\scph70004.bin -Parts stock,twins' -Output build\measure\time-games.log
& scripts\windows\hidden.ps1 -Script time-games.ps1 -Arguments '-Bios C:\bios\scph70004.bin -Parts time' -Output build\measure\time-games-time.log
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    [string]$List = 'build\measure\timed.txt',
    [string]$Parts = 'stock,twins,time',
    [int]$Rounds = 2,
    # What this timing's lines are called in -Results. Letters and digits.
    [string]$Mark = '',
    # Only these of the list's games (comma-separated names).
    [string]$Games = '',
    [string]$ReleaseTree = 'build\windows-clang-module',
    [string]$StockTree = 'build\windows-clang-stock',
    [string]$Results = 'build\measure\results.txt',
    [switch]$CpuThread
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'common.ps1')
Set-QuietPriority

function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $Ps2nRoot $path)
}
$part = @($Parts -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Mark) { $Mark = 'T' + (Get-Date -Format 'MMddHHmm') }
if ($Mark -notmatch '^[A-Za-z0-9]+$') { throw "-Mark is letters and digits: $Mark" }
$releaseDir = InRoot $ReleaseTree
$stockDir = InRoot $StockTree
$resultsFile = InRoot $Results
$logs = Join-Path (Split-Path $resultsFile) 'logs'
Set-Location $Ps2nRoot

$wanted = @($Games -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$timed = @()
foreach ($row in (Read-List (InRoot $List))) {
    if ($row.Count -ne 5) { throw "A line of $List has $($row.Count) parts, not 5: $($row -join ' | ')" }
    if ($wanted -and $wanted -notcontains $row[0]) { continue }
    if ($row[0] -notmatch '^[A-Za-z0-9_-]+$') { throw "A game's name in $List is letters, digits, - and _: $($row[0])" }
    foreach ($path in $row[1], $row[2], $row[3]) { if (-not (Test-Path -LiteralPath $path)) { throw "$List names what is not there: $path" } }
    $timed += [pscustomobject]@{ Name = $row[0]; Image = $row[1]; Folder = $row[2].TrimEnd('\'); Session = $row[3]; Renderer = $row[4] }
}
if (-not $timed) { throw "No game to time: $List has none$(if ($wanted) { " of $Games" })." }
if ($part -contains 'time') {
    if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
    $Bios = (Resolve-Path -LiteralPath $Bios).Path
}

$pcsx2 = "$(git -C third_party\pcsx2 describe --tags 2>$null)"
Stamp "time-games ($Parts) at $(git rev-parse --short HEAD), files differing from it: $(@(git status --porcelain).Count); PCSX2 $pcsx2; games: $(($timed | ForEach-Object { $_.Name }) -join ', ')"

if ($part -contains 'stock') {
    Wait-NoGame
    $log = Join-Path $logs 'time-games-build-stock.log'
    $started = Get-Date
    $code = Invoke-Script -Script 'build.ps1' -Arguments @('-BuildDir', $StockTree, '-Module', 'on', '-RuntimeLto', 'on', '-RuntimePgo', 'off', '-Pcsx2Patches', 'off') -Log $log
    Stamp ("stock PCSX2's tree, built as its releases are: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) {
        Get-Content $log | Select-String -Pattern 'error|FAILED' | Select-Object -First 12 | ForEach-Object { Write-Host "  | $($_.Line)" }
        Stamp 'stopping'
        exit 1
    }
    Select-String -LiteralPath (Join-Path $stockDir 'CMakeCache.txt') -Pattern '^PS2N_(RUNTIME_LTO|RUNTIME_PGO|PCSX2_PATCHES)[A-Z_]*:' | ForEach-Object { Stamp "  $($_.Line)" }
    Stamp "  ps2n-host.exe $(Get-FileNote (Join-Path $stockDir 'ps2n\bin\ps2n-host.exe'))"
}

if ($part -contains 'twins') {
    foreach ($game in $timed) {
        foreach ($twin in @(@('release', $releaseDir), @('stock', $stockDir))) {
            $to = "$($game.Folder)-$($twin[0])"
            $count = New-FolderTwin $game.Folder $to (Join-Path $twin[1] 'ps2n\bin')
            Stamp "$(Split-Path $to -Leaf): $count files; ps2n-host.exe $(Get-FileNote (Join-Path $to 'ps2n-host.exe'))"
        }
        Stamp "  its modules: $((Get-ChildItem -LiteralPath $game.Folder -Filter 'ps2n-*.dll' | ForEach-Object { '{0} {1:MM-dd HH:mm}' -f $_.Name, $_.LastWriteTime }) -join ', ')"
    }
}

if ($part -contains 'time') {
    $thread = -not $CpuThread
    $all = @()
    foreach ($game in $timed) {
        $turns = @(@('stock', 'stock-jit'), @('release', 'ps2n-static'))
        for ($round = 1; $round -le $Rounds; $round++) {
            $order = $turns.Clone()
            if ($round % 2 -eq 0) { [array]::Reverse($order) }
            foreach ($turn in $order) {
                Wait-NoGame
                $folder = "$($game.Folder)-$($turn[0])"
                $name = "t-$($game.Name)-$Mark-$($turn[0])-$round"
                $run = Invoke-Replay -Image $game.Image -Bios $Bios -Runtime (Join-Path $folder 'ps2n-host.exe') -Session $game.Session -Config $turn[1] `
                    -Renderer $game.Renderer -Name $name -Seconds 2400 -Marks -VuThread:$thread -Log (Join-Path $logs "$name.log")
                $seconds = ''
                if ($null -ne $run.Seconds) { $seconds = Format-Seconds $run.Seconds }
                $line = '{0} {1,-22} {2,-13} round {3}: {4,8} s (frames 1..{5}); {6}; {7}' -f (Get-Date -Format 'MM-dd HH:mm'), "$($game.Name)-$Mark-$($turn[0])",
                    $turn[1], $round, $seconds, $run.LastFrame, $run.Ee, $run.Verdict
                Add-Content -LiteralPath $resultsFile -Value $line
                Write-Host $line
                $all += [pscustomobject]@{ Game = $game.Name; Side = $turn[0]; Seconds = $run.Seconds; IsSession = $run.IsSession; Whole = ($run.Compared -eq $run.Written) }
                # A runtime of this measurement left running would spoil the next run's time: only one of the twins is stopped.
                $left = Stop-StrayHost @($folder)
                if ($left) { Stamp "a runtime of this measurement was still running ($left): stopped, and stopping here"; exit 1 }
            }
        }
    }
    $which = if ($thread) { "with its VU thread on, the compiled game with VU1 on PCSX2's VU thread" } else { 'with VU1 on the main processor thread, as the compiled game' }
    Write-Host ''
    Write-Host "Against stock PCSX2 $pcsx2 built as its releases are, $which; $Rounds rounds in turns:"
    Write-Host ('  {0,-12} {1,-26} {2,-26} {3,6}   {4}' -f 'game', 'stock PCSX2 (s)', 'compiled (s)', 'ratio', 'the session at every fingerprint')
    foreach ($game in $timed) {
        $means = @{}
        $texts = @{}
        foreach ($side in 'stock', 'release') {
            $times = @($all | Where-Object { $_.Game -eq $game.Name -and $_.Side -eq $side -and $null -ne $_.Seconds } | ForEach-Object { $_.Seconds })
            if ($times) {
                $means[$side] = ($times | Measure-Object -Average).Average
                $texts[$side] = "$(($times | ForEach-Object { Format-Seconds $_ }) -join ', '): $(Format-Seconds $means[$side])"
            } else { $texts[$side] = 'no time' }
        }
        $ratio = ''
        if ($means.ContainsKey('stock') -and $means.ContainsKey('release') -and $means['stock'] -gt 0) { $ratio = Format-Seconds ($means['release'] / $means['stock']) }
        $runs = @($all | Where-Object { $_.Game -eq $game.Name })
        $whole = @($runs | Where-Object { $_.IsSession -and $_.Whole }).Count
        Write-Host ('  {0,-12} {1,-26} {2,-26} {3,6}   in {4} of {5} runs' -f $game.Name, $texts['stock'], $texts['release'], $ratio, $whole, $runs.Count)
    }
    Write-Host ''
}
Stamp "time-games done ($Parts)"
exit 0

# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Gives the game folders of a PS2Native folder this tree's programs, and checks each.

.DESCRIPTION
The maintainer plays from a PS2Native folder of their own, build\kit, made by
kit.ps1, with their games in its games\ folder. After a change they want in
their games, this brings those folders up to date the way a user's are, by the
wizard (docs\adr\0048), and checks that each still plays what it played.

  build    the tree the folder is made from, built (nothing to do, usually)
  kit      the PS2Native folder made again from it (kit.ps1 keeps its games)
  saves    each game folder's memory cards copied aside first. Nothing below
           touches a save; the copies are to be sure of it
  update   <kit>\runtime\ps2n-wizard.exe --update <folder>, without a window:
           the folder gets this PS2Native's runtime, launcher and toolchain
  remake   the folder's own ps2n-remake --before-play, as its launcher runs it
           before the game starts: it compiles the game again. Its modules are
           put in place as the launcher puts them
  check    the start of one of the folder's sessions run again with the new
           programs (game.ps1 -Runtime: nothing is written into the folder).
           It must be the session at every fingerprint compared
  again    the wizard asked again: the folder must be up to date and nothing
           change; the memory cards compared with the copies

Which session: the folder's newest of -Frames frames or more; where that one
parts from itself, the one before, three at most; where none is that long,
the longest there is, whole. A list says otherwise
(-Sessions, build\measure\update-sessions.txt): a line "<folder's name> |
<session's name or path>", or "<folder's name> | -" for no check.

Each step waits while a game is played from the folder, and everything runs at
below-normal priority. Run it on the hidden desktop: ps2n-remake has a window.

Make the PS2Native folder only from a tree with nothing left to commit: its
licences name the commit, and say so when the tree had changes.

A folder that has counted how its game is played counts once more after its
next session of three minutes (docs\adr\0049): say so when reporting.

.EXAMPLE
& scripts\windows\hidden.ps1 -Script update-games.ps1 -Arguments '-Bios C:\bios\scph70004.bin' -Output build\measure\update-games.log

A trial first, on copies of two folders:
scripts\windows\update-games.ps1 -Bios <BIOS image> -Parts saves,update,remake,check,again -Games build\measure\trial\games
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    # The PS2Native folder: its wizard brings the games up to date, and the
    # 'kit' part makes it again.
    [string]$Kit = 'build\kit',
    # A folder of game folders other than <Kit>\games: copies, for a trial.
    [string]$Games = '',
    [string]$Parts = 'build,kit,saves,update,remake,check,again',
    # Only the folders whose names hold one of these (comma-separated).
    [string]$Only = '',
    # Not the folders with these names (comma-separated).
    [string]$Except = '',
    # The list of sessions to check with, where the rule is not wanted.
    [string]$Sessions = 'build\measure\update-sessions.txt',
    # How much of a session is run again.
    [int]$Frames = 900,
    [string]$Renderer = 'vulkan',
    # The tree the PS2Native folder is made from.
    [string]$BuildDir = 'build\windows-clang-module',
    # Where the logs and the copies of the memory cards go.
    [string]$Work = 'build\measure\update-games'
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'common.ps1')
Set-QuietPriority

function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $Ps2nRoot $path)
}
$part = @($Parts -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$kitDir = InRoot $Kit
$gamesRoot = if ($Games) { InRoot $Games } else { Join-Path $kitDir 'games' }
$workDir = InRoot $Work
$saves = Join-Path $workDir 'saves'
$wizard = Join-Path $kitDir 'runtime\ps2n-wizard.exe'
if ($part -contains 'check') {
    if (-not $Bios) { throw "A BIOS image is needed for the check: -Bios <file>, or set PS2N_BIOS." }
    $Bios = (Resolve-Path -LiteralPath $Bios).Path
}
if (-not (Test-Path -LiteralPath $gamesRoot)) { throw "No game folders at $gamesRoot." }
New-Item -ItemType Directory -Force $workDir | Out-Null
Set-Location $Ps2nRoot

$onlyNames = @($Only -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$exceptNames = @($Except -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$chosen = @{}
foreach ($row in (Read-List (InRoot $Sessions))) { if ($row.Count -ge 2) { $chosen[$row[0]] = $row[1] } }

$folders = @(Get-ChildItem -LiteralPath $gamesRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'settings.txt') } |
        Where-Object { $exceptNames -notcontains $_.Name } |
        Where-Object { $name = $_.Name; -not $onlyNames -or @($onlyNames | Where-Object { $name -like "*$_*" }) } | Sort-Object Name)

function Programs([string]$folder) {
    Stamp "  ps2n-host.exe $(Get-FileNote (Join-Path $folder 'ps2n-host.exe')); toolchain: $((Get-ChildItem -LiteralPath (Join-Path $folder 'toolchain') -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count) files"
    Stamp "  modules: $((Get-ChildItem -LiteralPath $folder -Filter 'ps2n-*.dll' | ForEach-Object { '{0} {1:MM-dd HH:mm}' -f $_.Name, $_.LastWriteTime }) -join ', ')"
    foreach ($file in 'programs.txt', 'work\brought-up-to-date.txt', 'work\count-again.txt') {
        $path = Join-Path $folder $file
        $says = 'none'
        if (Test-Path -LiteralPath $path) { $says = (Get-Content -LiteralPath $path | Where-Object { $_ -notmatch '^#' }) -join '; ' }
        Stamp "  ${file}: $says"
    }
}

# The sessions of a folder to check with, the first to try first.
function CheckSessions([string]$folder, [string]$name) {
    if ($chosen.ContainsKey($name)) {
        $one = $chosen[$name]
        if ($one -eq '-') { return @() }
        if (-not [IO.Path]::IsPathRooted($one)) { $one = Join-Path $folder "sessions\$one" }
        return @($one)
    }
    $all = @(Get-ChildItem -LiteralPath (Join-Path $folder 'sessions') -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'input.txt') } |
            ForEach-Object { [pscustomobject]@{ Path = $_.FullName; End = (Get-SessionEnd $_.FullName) } })
    $long = @($all | Where-Object { $_.End -ge $Frames } | Select-Object -First 3 | ForEach-Object { $_.Path })
    if ($long) { return $long }
    # None is that long: the longest there is, whole.
    @($all | Where-Object { $_.End -gt 0 } | Sort-Object End -Descending | Select-Object -First 1 | ForEach-Object { $_.Path })
}

Stamp "update-games ($Parts) at $(git rev-parse --short HEAD), files differing from it: $(@(git status --porcelain).Count)"
Stamp "  the game folders of ${gamesRoot}: $(($folders | ForEach-Object { $_.Name }) -join '; ')"

if ($part -contains 'build') {
    Wait-NoGame
    $started = Get-Date
    $code = Invoke-Script -Script 'build.ps1' -Arguments @('-BuildDir', $BuildDir) -Log (Join-Path $workDir 'build.log')
    Stamp ("the tree the folder is made from, built: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    Get-Content (Join-Path $workDir 'build.log') | Select-String -Pattern 'ninja: no work|Linking|error|FAILED' | Select-Object -First 8 |
        ForEach-Object { Write-Host "  | $($_.Line.Trim().Substring(0, [Math]::Min(200, $_.Line.Trim().Length)))" }
    if ($code -ne 0) { Stamp 'stopping'; exit 1 }
}
if ($part -contains 'kit') {
    Wait-NoGame
    $started = Get-Date
    # kit.ps1 takes its folder by its path in the repository's tree.
    $out = $Kit
    if ([IO.Path]::IsPathRooted($Kit)) {
        if (-not $kitDir.StartsWith($Ps2nRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "kit.ps1 makes a folder inside the repository's tree: $Kit is not." }
        $out = $kitDir.Substring($Ps2nRoot.Length + 1)
    }
    $code = Invoke-Script -Script 'kit.ps1' -Arguments @('-BuildDir', $BuildDir, '-Out', $out) -Log (Join-Path $workDir 'kit.log')
    Stamp ("the PS2Native folder made again, its games kept: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    Get-Content (Join-Path $workDir 'kit.log') | Select-String -Pattern "runtime is built|PCSX2's core|WARNING|Warning|not a release" |
        ForEach-Object { Write-Host "  | $($_.Line.Trim().Substring(0, [Math]::Min(220, $_.Line.Trim().Length)))" }
    if ($code -ne 0) { Get-Content (Join-Path $workDir 'kit.log') -Tail 12 | ForEach-Object { Write-Host "  | $_" }; Stamp 'stopping'; exit 1 }
}
if (-not (Test-Path -LiteralPath $wizard)) { throw "No wizard at ${wizard}: make the PS2Native folder first (the 'kit' part, from a tree built with -Wizard on)." }
Stamp "the PS2Native folder: ps2n-host.exe $(Get-FileNote (Join-Path $kitDir 'runtime\ps2n-host.exe')); ps2n-wizard.exe $(Get-FileNote $wizard)"
Stamp "  its licences say: $((Select-String -LiteralPath (Join-Path $kitDir 'licences\README.txt') -Pattern '^commit [0-9a-f]{40}|changes not committed' -ErrorAction SilentlyContinue | Select-Object -First 2 | ForEach-Object { $_.Line }) -join ' ')"

$summary = @()
$bad = @()
foreach ($entry in $folders) {
    $name = $entry.Name
    $folder = $entry.FullName
    $plain = $name -replace '[^A-Za-z0-9]', ''
    Wait-NoGame
    Stamp "=== $name"
    $note = [ordered]@{ Game = $name; Compiled = ''; Checked = '' }
    $sessionsBefore = @(Get-ChildItem -LiteralPath (Join-Path $folder 'sessions') -Directory -ErrorAction SilentlyContinue).Count
    $list = Join-Path $saves "$plain.sha256"

    if ($part -contains 'saves') {
        $to = Join-Path $saves $name
        if (Test-Path -LiteralPath $to) { Remove-Item -LiteralPath $to -Recurse -Force }
        $cards = @(Get-ChildItem -LiteralPath $folder -Recurse -File -Filter '*.ps2' -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -notlike (Join-Path $folder 'sessions\*') -and $_.FullName -notlike (Join-Path $folder 'work\*') })
        foreach ($card in $cards) {
            $relative = $card.FullName.Substring($folder.Length + 1)
            New-Item -ItemType Directory -Force (Split-Path (Join-Path $to $relative)) | Out-Null
            Copy-Item -LiteralPath $card.FullName -Destination (Join-Path $to $relative) -Force
        }
        New-Item -ItemType Directory -Force $saves | Out-Null
        Get-Cards $folder | Set-Content -LiteralPath $list -Encoding utf8
        Stamp "  its memory cards copied aside: $(($cards | ForEach-Object { '{0} ({1:N0} bytes, {2:MM-dd HH:mm})' -f $_.FullName.Substring($folder.Length + 1), $_.Length, $_.LastWriteTime }) -join ', ')"
    }

    if ($part -contains 'update') {
        Programs $folder
        $started = Get-Date
        $code = Update-GameFolder $wizard $folder
        Stamp ("  ps2n-wizard --update: exit {0} in {1:N1} s" -f $code, ((Get-Date) - $started).TotalSeconds)
        if ($code -ne 0) {
            Stamp '  NOT BROUGHT UP TO DATE: left as it is'
            $bad += "${name}: not brought up to date"
            $note.Compiled = 'not brought up to date'
            $summary += [pscustomobject]$note
            continue
        }
        Programs $folder
    }

    if ($part -contains 'remake') {
        Wait-NoGame
        $made = Invoke-Remake $folder
        Stamp ("  ps2n-remake --before-play: exit {0} in {1:N0} s" -f $made.ExitCode, $made.Seconds)
        if ($made.Modules) { Stamp "  the modules of $($made.Modules) put in place" } else { Stamp '  no new modules: nothing was left to compile' }
        $note.Compiled = '{0:N0} s' -f $made.Seconds
        if ($made.ExitCode -ne 0) { $bad += "${name}: its compile ended with $($made.ExitCode)"; $note.Compiled = "exit $($made.ExitCode)" }
        Programs $folder
    }

    if ($part -contains 'check') {
        $candidates = @(CheckSessions $folder $name)
        if (-not $candidates) {
            Stamp '  no session to check with'
            $note.Checked = 'no session'
        }
        $image = Get-FolderImage $folder
        $passed = $false
        foreach ($session in $candidates) {
            Wait-NoGame
            $end = Get-SessionEnd $session
            $count = [Math]::Min($Frames, $end)
            $run = Invoke-Replay -Image $image -Bios $Bios -Runtime (Join-Path $folder 'ps2n-host.exe') -Session $session -Frames $count `
                -Renderer $Renderer -Name "update-$plain" -Seconds 600
            Stamp "  $count frames of $(Split-Path $session -Leaf) run again with its new programs: $($run.Verdict)"
            if ($run.Ee) { Stamp "  $($run.Ee.Substring(0, [Math]::Min(170, $run.Ee.Length)))" }
            $run.Errors | ForEach-Object { Stamp "  : $($_.Substring(0, [Math]::Min(200, $_.Length)))" }
            if ($run.IsSession) {
                $passed = $true
                $note.Checked = "$count frames of $(Split-Path $session -Leaf): $($run.Compared) of $($run.Compared) fingerprints"
                break
            }
            Stamp '  NOT THE SESSION: the one before is tried'
        }
        if ($candidates -and -not $passed) { $bad += "${name}: no session ran again as itself"; $note.Checked = 'NOT THE SESSION' }
    }

    if ($part -contains 'again') {
        $before = @(Get-ChildItem -LiteralPath $folder -File | ForEach-Object { "$($_.Name) $($_.LastWriteTimeUtc.Ticks) $($_.Length)" })
        $code = Update-GameFolder $wizard $folder
        $after = @(Get-ChildItem -LiteralPath $folder -File | ForEach-Object { "$($_.Name) $($_.LastWriteTimeUtc.Ticks) $($_.Length)" })
        $changed = @(Compare-Object $before $after).Count
        Stamp "  asked again: exit $code; files of the folder's root changed: $changed"
        if ($code -ne 0 -or $changed -ne 0) { $bad += "${name}: the wizard asked again changed $changed files (exit $code)" }
        if (Test-Path -LiteralPath $list) {
            $was = @(Get-Content -LiteralPath $list)
            $now = @(Get-Cards $folder)
            $differing = @(Compare-Object $was $now).Count
            $sessionsNow = @(Get-ChildItem -LiteralPath (Join-Path $folder 'sessions') -Directory -ErrorAction SilentlyContinue).Count
            Stamp "  its memory cards: $($now.Count), differing from before: $differing; sessions: $sessionsNow where $sessionsBefore"
            if ($differing -ne 0) { $bad += "${name}: $differing memory cards differ" }
        }
        Stamp "  left to compile or count: next-* $(@(Get-ChildItem -LiteralPath (Join-Path $folder 'work') -Directory -Filter 'next-*' -ErrorAction SilentlyContinue).Count); brought-up-to-date.txt $(Test-Path -LiteralPath (Join-Path $folder 'work\brought-up-to-date.txt')); count-again.txt $(Test-Path -LiteralPath (Join-Path $folder 'work\count-again.txt')); its counts $(Get-FileNote (Join-Path $folder 'captures\profile.profdata'))"
    }
    $summary += [pscustomobject]$note
}

Stamp 'in short:'
$summary | ForEach-Object { Write-Host ('  {0,-42} compiled: {1,-10} checked: {2}' -f $_.Game, $_.Compiled, $_.Checked) }
if ($bad) {
    Stamp 'NOT AS IT SHOULD BE:'
    $bad | ForEach-Object { Write-Host "  $_" }
    Stamp "update-games done ($Parts), with $($bad.Count) to look at"
    exit 1
}
Stamp "update-games done ($Parts)"
exit 0

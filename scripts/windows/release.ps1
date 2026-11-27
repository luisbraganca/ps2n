# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Makes a release's Windows download from the committed and pushed tree, and tries it.

.DESCRIPTION
A release is made from a tree that is committed and pushed, its version in
version.txt (docs\adr\0042). Its runtime is built with link-time optimization
and a profile of play made from that same tree (docs\adr\0047). The download
is tried before anyone else gets it. scripts/linux/release.sh does the same
inside WSL; docs\development.md, "A release", has the whole order and what
has gone wrong before.

  first     the tree a release is made from built as it stands, and a first
            PS2Native folder made from it (<release>\first-kit): its wizard is
            what brings the training folders up to date
  training  the folders the profile is trained on (the third part of each line
            of -Training) brought up to date with that wizard and compiled by
            their own ps2n-remake --before-play: their modules have to be this
            tree's for the training runs to take them
  profile   the runtime's profile of play made again (runtime-profile.ps1 on
            the runs of -Training), the one before kept beside it
  build     the tree built with link-time optimization and that profile
  test      every test in it. One that fails, or is skipped, stops here
  kit       the folder for others (kit.ps1 -Out <release>\PS2Native, a fresh
            folder) and its download, PS2Native-<version>-windows-x64.zip,
            made with Windows' own tar.exe; its SHA-256
  unpacked  the download unpacked elsewhere and tried with nothing but Windows
            on PATH: its runtime reads a disc (-Disc), its wizard makes a game
            from its disc image with every default (-Image), then finds that
            folder up to date, and a session of that game (-Session) is run
            again whole from the folder made. Its code is interpreted there,
            as in a first session: slow, and a check, not a timing
  earlier   a copy of that game's folder as the release before made it
            (-Earlier) brought up to date by this download's wizard, compiled
            by its own ps2n-remake --before-play, the start of the session run
            again, and the wizard asked again

Only when asked for by name:

  theirs    the maintainer's own PS2Native folder, build\kit, made again from
            the same tree. Its games and patches stay as they are
  draft     the release made on GitHub as a draft, with both downloads and the
            notes (-Notes, build\release\notes-<version>.md), by gh. Nothing
            is public until it is published, which is done at the maintainer's
            word and not by this script:
                gh release edit v<version> --draft=false

<release> is build\release\<version>. Every step waits while a game is played
from build\kit, at below-normal priority. Run it on the hidden desktop: the
wizard has windows.

It stops where the tree has changes not committed, is not pushed, or does not
say -Version, since the folder's licences name the commit: keep the tree as it
is, documents too, until both systems' downloads are made. -Rehearsal goes
through it on any tree, into build\release\rehearsal: it makes no draft and
never touches build\kit.

Timing comes after, alone on the computer (time-games.ps1), and before the
notes quote a number.

.EXAMPLE
& scripts\windows\hidden.ps1 -Script release.ps1 -Output build\release\release.log -Arguments '-Bios C:\bios\scph70004.bin -Image "C:\discs\a game.iso" -Session "C:\somewhere\sessions\20260101-120000" -Disc "C:\discs\another game.chd" -Earlier "C:\somewhere\games\A Game"'
scripts\windows\release.ps1 -Parts draft
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    # The version released: what version.txt says, where not given.
    [string]$Version = '',
    [string]$Parts = 'first,training,profile,build,test,kit,unpacked,earlier',
    # The runs the runtime's profile is trained on (runtime-profile.ps1 has the format).
    [string]$Training = 'build\pgo\training.txt',
    # The disc image the unpacked download's wizard makes a game from.
    [string]$Image = '',
    # A session of that game, run again whole from the folder made.
    [string]$Session = '',
    # A disc in another format, which the unpacked runtime reads. Optional.
    [string]$Disc = '',
    # That game's folder as the release before's download made it. Optional.
    [string]$Earlier = '',
    # The renderer of the runs again (PCSX2's null renderer asserts on some games' drawing: vulkan there).
    [string]$Renderer = 'null',
    # The notes the draft is made with.
    [string]$Notes = '',
    # A release that is not a pre-release.
    [switch]$Stable,
    [switch]$Rehearsal
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'common.ps1')
Set-QuietPriority
Set-Location $Ps2nRoot

function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $Ps2nRoot $path)
}
$part = @($Parts -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Version) { $Version = "$(Get-Content -LiteralPath (Join-Path $Ps2nRoot 'version.txt') -TotalCount 1)".Trim() }
$label = if ($Rehearsal) { 'rehearsal' } else { $Version }
$relPath = "build\release\$label"
$rel = Join-Path $Ps2nRoot $relPath
$treePath = 'build\windows-clang-module'
$tree = Join-Path $Ps2nRoot $treePath
$zipName = "PS2Native-$label-windows-x64.zip"
$zip = Join-Path $rel $zipName
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$profileData = 'build\pgo\runtime-windows.profdata'
$logs = Join-Path $rel 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

$needsBios = @($part | Where-Object { $_ -in 'profile', 'test', 'unpacked', 'earlier' })
if ($needsBios) {
    if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
    $Bios = (Resolve-Path -LiteralPath $Bios).Path
}
if ($part -contains 'unpacked') {
    if (-not $Image -or -not $Session) { throw "The unpacked download is tried with a game: -Image <disc image> and -Session <a session of it>." }
    foreach ($path in $Image, $Session) { if (-not (Test-Path -LiteralPath $path)) { throw "Not there: $path" } }
}
if ($Rehearsal -and @($part | Where-Object { $_ -in 'theirs', 'draft' })) { throw "A rehearsal makes no draft and leaves build\kit alone." }

# What a release is made from: nothing left to commit, pushed, and the version it says.
$head = "$(git rev-parse HEAD)"
$differing = @(git status --porcelain).Count
$pushed = $head -eq "$(git rev-parse origin/main 2>$null)"
$says = "$(Get-Content -LiteralPath (Join-Path $Ps2nRoot 'version.txt') -TotalCount 1)".Trim()
Stamp "release $label ($Parts) at $($head.Substring(0, 7)); files differing from it: $differing; pushed: $pushed; version.txt says $says"
if (-not $Rehearsal -and @($part | Where-Object { $_ -in 'kit', 'theirs', 'draft' })) {
    if ($differing) { throw "The tree has changes not committed: a release is made from a committed tree (or -Rehearsal)." }
    if (-not $pushed) { throw "The commit is not pushed: a release is made from a pushed tree (or -Rehearsal)." }
    if ($says -ne $Version) { throw "version.txt says $says, not ${Version}." }
}

function Stop-Here([string]$why) {
    Stamp "${why}: stopping"
    exit 1
}
function Modules([string]$folder) {
    Stamp "  modules: $((Get-ChildItem -LiteralPath $folder -Filter 'ps2n-*.dll' | ForEach-Object { '{0} {1:N0} {2:MM-dd HH:mm}' -f $_.Name, $_.Length, $_.LastWriteTime }) -join ', ')"
    $programs = Join-Path $folder 'programs.txt'
    $text = 'none'
    if (Test-Path -LiteralPath $programs) { $text = (Get-Content -LiteralPath $programs | Where-Object { $_ -notmatch '^#' }) -join '; ' }
    Stamp "  programs.txt: $text"
}
# A session, or its start, run again from a game folder's own runtime and modules.
function Again([string]$folder, [string]$image, [int]$frames, [string]$name) {
    Wait-NoGame
    $run = Invoke-Replay -Image $image -Bios $Bios -Runtime (Join-Path $folder 'ps2n-host.exe') -Session $Session -Frames $frames -Renderer $Renderer `
        -Name $name -Seconds 1800
    Stamp ("  in {0:N0} s: {1}" -f $run.Wall, $run.Verdict)
    if ($run.Ee) { Stamp "  $($run.Ee.Substring(0, [Math]::Min(170, $run.Ee.Length)))" }
    $run.Errors | ForEach-Object { Stamp "  : $($_.Substring(0, [Math]::Min(200, $_.Length)))" }
    return $run.IsSession
}
$bad = @()

if ($part -contains 'first') {
    Wait-NoGame
    $started = Get-Date
    $code = Invoke-Script -Script 'build.ps1' -Arguments @('-BuildDir', $treePath) -Log (Join-Path $logs 'first-build.log')
    Stamp ("the tree a release is made from, built as it stands: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) { Get-Content (Join-Path $logs 'first-build.log') | Select-String -Pattern 'error|FAILED' | Select-Object -First 12 | ForEach-Object { Write-Host "  | $($_.Line)" }; Stop-Here 'the build failed' }
    $started = Get-Date
    $code = Invoke-Script -Script 'kit.ps1' -Arguments @('-BuildDir', $treePath, '-Out', "$relPath\first-kit") -Log (Join-Path $logs 'first-kit.log')
    Stamp ("a first PS2Native folder made from it, for its wizard: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) { Get-Content (Join-Path $logs 'first-kit.log') -Tail 12 | ForEach-Object { Write-Host "  | $_" }; Stop-Here 'the folder was not made' }
}

if ($part -contains 'training') {
    $wizard = Join-Path $rel 'first-kit\runtime\ps2n-wizard.exe'
    if (-not (Test-Path -LiteralPath $wizard)) { Stop-Here "no wizard at $wizard (the 'first' part makes it)" }
    $folders = @((Read-List (InRoot $Training)) | ForEach-Object { $_[2] } | Select-Object -Unique)
    if (-not $folders) { Stop-Here "$Training names no folder" }
    Stamp "the training folders brought up to date with this tree's wizard: $(($folders | ForEach-Object { Split-Path $_ -Leaf }) -join ', ')"
    foreach ($folder in $folders) {
        Wait-NoGame
        Stamp "=== $folder"
        Modules $folder
        $code = Update-GameFolder $wizard $folder
        Stamp "  ps2n-wizard --update: exit $code"
        if ($code -ne 0) { Stop-Here 'a training folder was not brought up to date' }
        # A training folder's counts stay what they are: it is not asked to count again.
        Remove-Item -LiteralPath (Join-Path $folder 'work\count-again.txt') -ErrorAction SilentlyContinue
        $made = Invoke-Remake $folder
        Stamp ("  ps2n-remake --before-play: exit {0} in {1:N0} s; modules: {2}" -f $made.ExitCode, $made.Seconds, $(if ($made.Modules) { $made.Modules } else { 'none new' }))
        if ($made.ExitCode -ne 0) { Stop-Here 'a training folder did not compile' }
        Modules $folder
    }
}

if ($part -contains 'profile') {
    Wait-NoGame
    $kept = "build\pgo\runtime-windows-before-$label.profdata"
    if ((Test-Path -LiteralPath (InRoot $profileData)) -and -not (Test-Path -LiteralPath (InRoot $kept))) { Copy-Item -LiteralPath (InRoot $profileData) -Destination (InRoot $kept) }
    $log = Join-Path $logs 'runtime-profile.log'
    $started = Get-Date
    $code = Invoke-Script -Script 'runtime-profile.ps1' -Arguments @('-Bios', $Bios, '-Training', $Training, '-Out', $profileData) -Log $log
    Stamp ("the runtime's profile of play made again: exit {0} in {1:N0} s (the one before kept as {2})" -f $code, ((Get-Date) - $started).TotalSeconds, $kept)
    Get-Content $log | Select-String -Pattern '^[a-z0-9-]+: |the session at|counts:|The profile|Total functions|Maximum|not trained|did not|could not|error|is not the session' |
        ForEach-Object { Write-Host "  $($_.Line.Substring(0, [Math]::Min(230, $_.Line.Length)))" }
    if ($code -ne 0) { Get-Content $log -Tail 12 | ForEach-Object { Write-Host "  | $_" }; Stop-Here 'the profile was not made' }
}

if ($part -contains 'build') {
    Wait-NoGame
    $log = Join-Path $logs 'build.log'
    $started = Get-Date
    $code = Invoke-Script -Script 'build.ps1' -Arguments @('-BuildDir', $treePath, '-RuntimeLto', 'on', '-RuntimePgo', 'use', '-RuntimePgoData', $profileData) -Log $log
    Stamp ("the tree built, its runtime as releases have it: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) { Get-Content $log | Select-String -Pattern 'error|FAILED' | Select-Object -First 12 | ForEach-Object { Write-Host "  | $($_.Line)" }; Stop-Here 'the build failed' }
    Get-Content $log | Select-String -Pattern "PS2Native: the runtime|PS2Native: PCSX2's code" | ForEach-Object { Write-Host "  $($_.Line)" }
    Get-ChildItem (Join-Path $tree 'ps2n\bin') -File | Where-Object { $_.Extension -in '.exe', '.dll' -and $_.Name -like 'ps2n-*' } |
        ForEach-Object { Stamp ("  {0}: {1:N0} bytes, {2:MM-dd HH:mm}" -f $_.Name, $_.Length, $_.LastWriteTime) }
}

if ($part -contains 'test') {
    Wait-NoGame
    $log = Join-Path $logs 'test.log'
    $started = Get-Date
    $code = Invoke-Script -Script 'test.ps1' -Arguments @('-Bios', $Bios, '-BuildDir', $treePath) -Log $log
    $count = Get-TestCount $log
    Stamp ("every test in it: exit {0} in {1:N0} s: {2}" -f $code, ((Get-Date) - $started).TotalSeconds, $count.Said)
    $count.FailedNames | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
    if ($code -ne 0 -or $count.Failed -ne 0) { Stop-Here 'a test failed: no download is made' }
    if ($count.Skipped -ne 0) { Stop-Here "$($count.Skipped) tests were skipped, which is not passed: no download is made" }
}

if ($part -contains 'kit') {
    Wait-NoGame
    $folder = Join-Path $rel 'PS2Native'
    if (Test-Path -LiteralPath $folder) { Remove-Item -LiteralPath $folder -Recurse -Force }
    Remove-Item -LiteralPath $zip -ErrorAction SilentlyContinue
    $log = Join-Path $logs 'kit.log'
    $started = Get-Date
    $code = Invoke-Script -Script 'kit.ps1' -Arguments @('-BuildDir', $treePath, '-Out', "$relPath\PS2Native") -Log $log
    Stamp ("the folder for others, $relPath\PS2Native: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    if ($code -ne 0) { Get-Content $log -Tail 12 | ForEach-Object { Write-Host "  | $_" }; Stop-Here 'the folder was not made' }
    Get-Content $log | Select-String -Pattern "runtime is built|PCSX2's core|WARNING|Warning|not a release|commit|changes not committed" |
        ForEach-Object { Write-Host "  | $($_.Line.Trim().Substring(0, [Math]::Min(220, $_.Line.Trim().Length)))" }
    $files = @(Get-ChildItem -LiteralPath $folder -Recurse -File)
    Stamp ("  the folder: {0:N0} files, {1:N0} bytes; games in it: {2}; patches in it: {3}" -f $files.Count, ($files | Measure-Object Length -Sum).Sum,
        (Test-Path (Join-Path $folder 'games')), (Test-Path (Join-Path $folder 'patches')))
    foreach ($program in 'ps2n-host.exe', 'ps2n-wizard.exe', 'ps2n-remake.exe') { Stamp "  runtime\${program}: $(Get-FileNote (Join-Path $folder "runtime\$program"))" }
    Get-ChildItem -LiteralPath (Join-Path $folder 'licences') -File | Select-String -Pattern 'commit|not committed|PS2Native \d|changed it' | Select-Object -First 6 |
        ForEach-Object { Write-Host "  | $($_.Filename): $($_.Line.Trim().Substring(0, [Math]::Min(200, $_.Line.Trim().Length)))" }
    Stamp "  licences\PCSX2-changes: $((Get-ChildItem -LiteralPath (Join-Path $folder 'licences\PCSX2-changes') -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ', ')"
    if ((Test-Path (Join-Path $folder 'games')) -or (Test-Path (Join-Path $folder 'patches'))) { Stop-Here 'the folder for others holds games or patches' }
    Push-Location $rel
    $started = Get-Date
    & $tar -a -c -f $zipName PS2Native
    $code = $LASTEXITCODE
    Pop-Location
    Stamp ("its download, {0}: exit {1} in {2:N0} s; {3}" -f $zipName, $code, ((Get-Date) - $started).TotalSeconds, (Get-FileNote $zip))
    if ($code -ne 0) { Stop-Here 'the download was not made' }
    Stamp "  SHA-256 $((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower())"
}

if ($part -contains 'unpacked') {
    Wait-NoGame
    if (-not (Test-Path -LiteralPath $zip)) { Stop-Here "no download at $zip (the 'kit' part makes it)" }
    $check = Join-Path $rel 'check'
    if (Test-Path -LiteralPath $check) { Remove-Item -LiteralPath $check -Recurse -Force }
    New-Item -ItemType Directory -Force $check | Out-Null
    & $tar -x -f $zip -C $check
    $there = Join-Path $check 'PS2Native'
    Stamp ("the download unpacked into {0}\check: exit {1}; {2:N0} files where the folder has {3:N0}" -f $relPath, $LASTEXITCODE,
        @(Get-ChildItem -LiteralPath $there -Recurse -File).Count, @(Get-ChildItem -LiteralPath (Join-Path $rel 'PS2Native') -Recurse -File).Count)
    $path = $env:Path
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    if ($Disc) {
        Stamp 'with nothing but Windows on PATH: its runtime reads a disc'
        $run = Invoke-Quiet -Program (Join-Path $there 'runtime\ps2n-host.exe') -Arguments @('-disc-executables', $Disc) -Minutes 5 -Show 12
        Stamp "  exit $($run.ExitCode)"
        if ($run.ExitCode -ne 0) { $bad += 'the unpacked runtime did not read the disc' }
    }
    $before = @(Get-ChildItem -LiteralPath (Join-Path $there 'games') -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    Stamp 'its wizard makes a game from its disc image, every default accepted'
    $started = Get-Date
    $run = Invoke-Quiet -Program (Join-Path $there 'runtime\ps2n-wizard.exe') -Arguments @('--image', $Image, '--bios', $Bios, '--auto', '--shots', (Join-Path $rel 'shots')) -Minutes 60 -Show 12
    Stamp ("  the wizard: exit {0} in {1:N0} s" -f $run.ExitCode, ((Get-Date) - $started).TotalSeconds)
    $made = @(Get-ChildItem -LiteralPath (Join-Path $there 'games') -Directory -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name -and (Test-Path -LiteralPath (Join-Path $_.FullName 'make.txt')) })
    if ($made.Count -ne 1) { $env:Path = $path; Stop-Here "the wizard made $($made.Count) games, not one" }
    $game = $made[0].FullName
    Stamp "  $($made[0].Name): $(@(Get-ChildItem -LiteralPath $game -Recurse -File).Count) files"
    Modules $game
    Stamp 'the wizard asked to bring that folder up to date'
    $code = Update-GameFolder (Join-Path $there 'runtime\ps2n-wizard.exe') $game
    Stamp "  exit $code"
    $env:Path = $path
    $end = Get-SessionEnd $Session
    Stamp "the session $(Split-Path $Session -Leaf) run again whole from it ($end frames)"
    if (-not (Again $game $Image $end "release-$label-check")) { $bad += 'the session run again from the unpacked download was not the session' }
}

if ($part -contains 'earlier' -and -not $Earlier) { Stamp "no folder of the release before given (-Earlier): the update from it is not tried" }
if ($part -contains 'earlier' -and $Earlier) {
    Wait-NoGame
    $wizard = Join-Path $rel 'check\PS2Native\runtime\ps2n-wizard.exe'
    if (-not (Test-Path -LiteralPath $wizard)) { Stop-Here "no unpacked download at $rel\check (the 'unpacked' part makes it)" }
    $copy = Join-Path $rel "from-earlier\games\$(Split-Path $Earlier -Leaf)"
    if (Test-Path -LiteralPath (Join-Path $rel 'from-earlier')) { cmd /c rmdir /s /q "$(Join-Path $rel 'from-earlier')" | Out-Null }
    New-Item -ItemType Directory -Force (Split-Path $copy) | Out-Null
    robocopy $Earlier $copy /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    Stamp "a copy of the folder the release before made (robocopy $LASTEXITCODE)"
    Modules $copy
    $code = Update-GameFolder $wizard $copy
    Stamp "  this download's ps2n-wizard --update: exit $code"
    Modules $copy
    if ($code -ne 0) { $bad += 'the earlier folder was not brought up to date' }
    $made = Invoke-Remake $copy
    Stamp ("  its ps2n-remake --before-play: exit {0} in {1:N0} s; modules: {2}" -f $made.ExitCode, $made.Seconds, $(if ($made.Modules) { $made.Modules } else { 'NONE NEW' }))
    if ($made.ExitCode -ne 0) { $bad += 'the earlier folder did not compile' }
    Stamp '900 frames of the session run again from it'
    if (-not (Again $copy $Image 900 "release-$label-earlier")) { $bad += 'the session run again from the earlier folder was not the session' }
    $code = Update-GameFolder $wizard $copy
    Stamp "  the wizard asked again: exit $code"
}

if ($part -contains 'theirs') {
    Wait-NoGame
    $log = Join-Path $logs 'theirs.log'
    $games = @(Get-ChildItem -LiteralPath (Join-Path $Ps2nRoot 'build\kit\games') -Directory -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name) $((Get-Item -LiteralPath (Join-Path $_.FullName 'ps2n-host.exe') -ErrorAction SilentlyContinue).LastWriteTimeUtc.Ticks)" })
    $started = Get-Date
    $code = Invoke-Script -Script 'kit.ps1' -Arguments @('-BuildDir', $treePath) -Log $log
    Stamp ("the maintainer's own PS2Native folder, build\kit, made again from the same tree: exit {0} in {1:N0} s" -f $code, ((Get-Date) - $started).TotalSeconds)
    Get-Content $log | Select-String -Pattern "runtime is built|PCSX2's core|WARNING|Warning|not a release" | ForEach-Object { Write-Host "  | $($_.Line.Trim().Substring(0, [Math]::Min(220, $_.Line.Trim().Length)))" }
    $after = @(Get-ChildItem -LiteralPath (Join-Path $Ps2nRoot 'build\kit\games') -Directory -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name) $((Get-Item -LiteralPath (Join-Path $_.FullName 'ps2n-host.exe') -ErrorAction SilentlyContinue).LastWriteTimeUtc.Ticks)" })
    Stamp "  its game folders, as they were: $(if (@(Compare-Object $games $after).Count -eq 0) { 'yes' } else { 'NO' }) ($($after.Count))"
    if ($code -ne 0) { $bad += 'build\kit was not made again' }
}

if ($part -contains 'draft') {
    $notesFile = if ($Notes) { InRoot $Notes } else { Join-Path $Ps2nRoot "build\release\notes-$Version.md" }
    $linux = Join-Path $rel "PS2Native-$Version-linux-x64.tar.xz"
    foreach ($needed in $notesFile, $zip, $linux) { if (-not (Test-Path -LiteralPath $needed)) { Stop-Here "not there: $needed" } }
    $gh = (Get-Command gh.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $gh) { $gh = Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe' }
    if (-not (Test-Path -LiteralPath $gh)) { Stop-Here 'GitHub CLI (gh) is not installed' }
    $origin = "$(git remote get-url origin)"
    if ($origin -notmatch 'github\.com[:/]([^/]+/[^/]+?)(\.git)?$') { Stop-Here "origin is not a GitHub repository: $origin" }
    $repository = $Matches[1]
    $arguments = @('release', 'create', "v$Version", '--repo', $repository, '--draft', '--target', $head, '--title', "PS2Native $Version", '--notes-file', $notesFile)
    if (-not $Stable) { $arguments += '--prerelease' }
    $arguments += @($zip, $linux)
    Stamp "gh $($arguments -join ' ')"
    & $gh @arguments
    if ($LASTEXITCODE -ne 0) { Stop-Here "gh ended with $LASTEXITCODE" }
    Stamp "a draft of v$Version is on GitHub: read it there. It is published at the maintainer's word: gh release edit v$Version --repo $repository --draft=false"
}

if ($bad) {
    Stamp 'NOT AS IT SHOULD BE:'
    $bad | ForEach-Object { Write-Host "  $_" }
    Stamp "release $label done ($Parts), with $($bad.Count) to look at"
    exit 1
}
Stamp "release $label done ($Parts)"
exit 0

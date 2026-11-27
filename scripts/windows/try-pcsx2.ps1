# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Tries PS2Native on a newer PCSX2, and writes a report.

.DESCRIPTION
PS2Native follows one version of PCSX2, a pinned submodule (docs\adr\0004,
0054). This says what a newer one would take, without moving anything:
whether PS2Native moves is the maintainer's to decide, with the report
(docs\adr\0053, 0061). docs\development.md, "A newer PCSX2 tried", has the
whole of it, and how a move is made afterwards.

  check   No build, about a minute. PCSX2's commits and files between the
          version followed and -Tag (PCSX2's newest where not given); which of
          those files are on the watch list (scripts\pcsx2-watch.txt: what
          PS2Native follows or compiles), their changes saved to read; whether
          PCSX2's dependency scripts changed; whether PS2Native's own changes
          to PCSX2's code (patches\pcsx2) still apply; where the repository
          names the version followed

The rest only when asked for by name, or all of it with -Parts all:

  tree    a worktree beside the repository (..\ps2n-pcsx2-<n>) on a branch of
          its own (pcsx2-<version>), the submodule at the tag there, and so
          build trees of its own: a tree built before PCSX2's dependencies
          changed mixes objects of both
  build   the development tree and the tree a release is made from, built in
          the worktree with this tree's settings. Where PS2Native has to
          follow a change of PCSX2's first, the build says where
  test    every test of both, counted
  games   a PS2Native folder made from the worktree; a copy of each folder of
          -List brought up to date with it and compiled; each session run
          again whole, by stock PCSX2 and by the compiled game, on the version
          followed and on the one tried: is each the session at every
          fingerprint. One run of each: not a timing
  linux   the same inside WSL, in a worktree there, this worktree's changes
          carried over (scripts/linux/try-pcsx2.sh)
  report  everything said, put together in build\pcsx2-try\<tag>\report.md,
          and the worktree's changes saved beside it as a patch

Following what PCSX2 changed is work done in the worktree, between "build"
and "test", until every test passes; nothing is committed. Then the report
goes to the maintainer.

-Remove takes the worktree and its branch away, here and inside WSL, its
changes saved as a patch first.

Every part waits while a game is played from build\kit, at below-normal
priority. Run the parts that build on the hidden desktop.

.EXAMPLE
scripts\windows\try-pcsx2.ps1
& scripts\windows\hidden.ps1 -Script try-pcsx2.ps1 -Arguments '-Tag v2.9.198 -Bios C:\bios\scph70004.bin -Parts all' -Output build\pcsx2-try\v2.9.198\all.log
scripts\windows\try-pcsx2.ps1 -Tag v2.9.198 -Remove
#>
param(
    # PCSX2's version to try: a tag of its repository. Its newest where not given.
    [string]$Tag = '',
    [string]$Bios = $env:PS2N_BIOS,
    [string]$Parts = 'check',
    # The worktree. Beside the repository where not given.
    [string]$Worktree = '',
    # The folders and sessions 'games' runs again (time-games.ps1 has the format).
    [string]$List = 'build\measure\timed.txt',
    # For 'linux': the list inside the clone there (scripts/linux/release.sh has the format).
    [string]$LinuxList = 'build/timed.txt',
    # For 'linux': its own parts (scripts/linux/try-pcsx2.sh has them).
    [string]$LinuxParts = 'setup,sync,build,test,games',
    # PCSX2's Windows dependency package fetched into the worktree where PCSX2's scripts for it changed.
    [switch]$FetchDeps,
    [switch]$Remove
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'common.ps1')
Set-QuietPriority
Set-Location $Ps2nRoot

function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $Ps2nRoot $path)
}
# git, what it says on its error stream left out.
function Git-Quiet([string[]]$arguments) {
    $was = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    $said = & git @arguments 2>$null
    $ErrorActionPreference = $was
    return $said
}
function ForGit([string]$path) { return 'file:///' + ($path -replace '\\', '/') }

$part = @($Parts -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($part -contains 'all') { $part = @('check', 'tree', 'build', 'test', 'games', 'report') }

$pinned = "$(Git-Quiet @('-C', 'third_party\pcsx2', 'describe', '--tags', '--exact-match'))".Trim()
if (-not $pinned) { $pinned = "$(Git-Quiet @('-C', 'third_party\pcsx2', 'describe', '--tags'))".Trim() }
if (-not $pinned) { throw "The submodule third_party\pcsx2 names no tag." }
$origin = 'https://github.com/PCSX2/pcsx2'
if (-not $Tag) {
    $newest = Git-Quiet @('ls-remote', '--tags', '--refs', '--sort=-v:refname', $origin, 'v*') | Select-Object -First 1
    if ("$newest" -notmatch 'refs/tags/(v[\d.]+)$') { throw "PCSX2's newest tag could not be asked for." }
    $Tag = $Matches[1]
}
if ($Tag -notmatch '^v[\d.]+$') { throw "A tag of PCSX2's looks like v2.9.198: $Tag" }
$number = $Tag.Substring(1)
$branch = "pcsx2-$number"
$w = if ($Worktree) { InRoot $Worktree } else { Join-Path (Split-Path $Ps2nRoot) "ps2n-pcsx2-$(($number -split '\.')[-1])" }
$reading = Join-Path $Ps2nRoot 'build\pcsx2-try\pcsx2'
$dir = Join-Path $Ps2nRoot "build\pcsx2-try\$Tag"
$logs = Join-Path $dir 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

# What a part says goes to the screen and into its own file, which 'report' puts together.
$script:saying = ''
function Say([string]$text) {
    Write-Host $text
    if ($script:saying) { Add-Content -LiteralPath (Join-Path $dir "$($script:saying).md") -Value $text -Encoding UTF8 }
}
function Start-Part([string]$name, [string]$title) {
    $script:saying = $name
    Set-Content -LiteralPath (Join-Path $dir "$name.md") -Value "## $title`n" -Encoding UTF8
    Stamp "--- $title"
}

# PCSX2's own repository, for reading: the version followed as one commit, and
# the one tried with the commits since.
function Get-Reading {
    if (-not (Test-Path -LiteralPath (Join-Path $reading '.git'))) {
        New-Item -ItemType Directory -Force $reading | Out-Null
        git -C $reading init -q
        git -C $reading remote add origin $origin
    }
    $null = Git-Quiet @('-C', $reading, 'fetch', '-q', '--depth', '1', 'origin', 'tag', $pinned)
    if ($Tag -ne $pinned) {
        $null = Git-Quiet @('-C', $reading, 'fetch', '-q', "--shallow-exclude=$pinned", 'origin', 'tag', $Tag)
        if (-not "$(Git-Quiet @('-C', $reading, 'rev-parse', '--verify', '--quiet', "$Tag^{commit}"))") {
            $null = Git-Quiet @('-C', $reading, 'fetch', '-q', '--depth', '1', 'origin', 'tag', $Tag)
        }
    }
    foreach ($one in $pinned, $Tag) {
        if (-not "$(Git-Quiet @('-C', $reading, 'rev-parse', '--verify', '--quiet', "$one^{commit}"))") { throw "PCSX2's $one could not be fetched into $reading." }
    }
}
# The files PCSX2 changed between the two: added lines, deleted lines, path.
function Get-Changed {
    @(git -C $reading diff --numstat $pinned $Tag | ForEach-Object {
            $bits = $_ -split "`t"
            if ($bits.Count -ge 3) { [pscustomobject]@{ Added = $bits[0]; Deleted = $bits[1]; Path = $bits[2] } }
        })
}
function Test-DepsChanged([object[]]$changed, [string]$system) {
    return [bool]@($changed | Where-Object { $_.Path -like ".github/workflows/scripts/$system/*" -or $_.Path -like '.github/workflows/scripts/common/*' }).Count
}
# The settings a build tree of this repository remembers, as build.ps1's arguments.
function Get-TreeSettings([string]$tree) {
    $cache = Join-Path $Ps2nRoot "$tree\CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) { throw "Build $tree here first: the worktree's trees take its settings." }
    $value = @{}
    foreach ($line in Get-Content -LiteralPath $cache) { if ($line -match '^(PS2N_[A-Z0-9_]+):[A-Z]+=(.*)$') { $value[$Matches[1]] = $Matches[2] } }
    $arguments = @()
    $paths = [ordered]@{ PS2N_GAME_ELF = '-Game'; PS2N_GAME_CAPTURE = '-GameCapture'; PS2N_BIOS_IMAGE = '-Bios'; PS2N_BIOS_CAPTURE = '-BiosCapture'
        PS2N_VU_CAPTURE = '-VuCapture'; PS2N_VU_SELFTEST_CAPTURE = '-VuSelftestCapture'; PS2N_IOP_CAPTURE = '-IopCapture'; PS2N_IOP_SELFTEST_CAPTURE = '-IopSelftestCapture' }
    foreach ($name in $paths.Keys) {
        $arguments += $paths[$name]
        if ($value[$name]) { $arguments += ($value[$name] -replace '/', '\') } else { $arguments += 'none' }
    }
    $arguments += '-GameMore'
    if ($value['PS2N_GAME_MORE']) { $arguments += (($value['PS2N_GAME_MORE'] -split ';' | ForEach-Object { $_ -replace '/', '\' }) -join '|') } else { $arguments += 'none' }
    if ($value['PS2N_PGO'] -eq 'use' -and $value['PS2N_PGO_DATA']) { $arguments += @('-Pgo', 'use', '-PgoData', ($value['PS2N_PGO_DATA'] -replace '/', '\')) } else { $arguments += @('-Pgo', 'off') }
    $arguments += @('-Lto', $(if ($value['PS2N_LTO']) { $value['PS2N_LTO'] } else { 'off' }))
    $arguments += @('-RuntimeLto', $(if ($value['PS2N_RUNTIME_LTO'] -eq 'ON') { 'on' } else { 'off' }))
    if ($value['PS2N_RUNTIME_PGO'] -eq 'use' -and $value['PS2N_RUNTIME_PGO_DATA']) {
        $arguments += @('-RuntimePgo', 'use', '-RuntimePgoData', ($value['PS2N_RUNTIME_PGO_DATA'] -replace '/', '\'))
    } else { $arguments += @('-RuntimePgo', 'off') }
    $arguments += @('-Module', $(if ($value['PS2N_GAME_IMPORTS'] -eq 'ON') { 'imports' } elseif ($value['PS2N_GAME_MODULE'] -eq 'ON') { 'on' } else { 'off' }))
    $arguments += @('-Wizard', $(if ($value['PS2N_WIZARD'] -eq 'ON') { 'on' } else { 'off' }))
    $arguments += @('-Pcsx2Patches', $(if ($value['PS2N_PCSX2_PATCHES'] -eq 'OFF') { 'off' } else { 'on' }))
    return $arguments
}
# The worktree's changes, as a patch and a list of its files not yet in git.
function Save-Changes([string]$to) {
    New-Item -ItemType Directory -Force $to | Out-Null
    git -C $w -c core.safecrlf=false diff HEAD --binary --ignore-submodules=all --output="$(Join-Path $to 'tree.patch')"
    $untracked = @(git -C $w ls-files --others --exclude-standard)
    [IO.File]::WriteAllText((Join-Path $to 'untracked.txt'), (($untracked | ForEach-Object { $_ + "`n" }) -join ''))
    return "$(@(git -C $w diff HEAD --name-only --ignore-submodules=all).Count) files changed, $($untracked.Count) new"
}

Stamp "try-pcsx2 ($($part -join ','))$(if ($Remove) { ' -Remove' }): PS2Native follows PCSX2 $pinned; trying $Tag; the worktree $w, the branch $branch"

if ($Remove) {
    if (Test-Path -LiteralPath $w) {
        Stamp "the worktree's changes saved first: $(Save-Changes (Join-Path $dir 'removed')) ($dir\removed)"
        # Its deps folder is a link to this tree's: the link goes first, alone, so that nothing is ever followed into it.
        $link = Join-Path $w 'deps'
        if (Test-Path -LiteralPath $link) {
            if ((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { cmd /c rmdir "$link" }
            else { Get-ChildItem -LiteralPath $link -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | ForEach-Object { cmd /c rmdir "$($_.FullName)" } }
        }
        if ((Test-Path -LiteralPath $link) -and ((Get-Item -LiteralPath $link -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "The link $link could not be taken away: nothing removed." }
        if (-not (Test-Path -LiteralPath (Join-Path $Ps2nRoot 'deps\windows-x64\deps\lib'))) { throw "This tree's deps are not where they were: stopping." }
        cmd /c rmdir /s /q "$w"
        Stamp "the worktree removed: $(-not (Test-Path -LiteralPath $w)); this tree's deps still there: $(Test-Path -LiteralPath (Join-Path $Ps2nRoot 'deps\windows-x64\deps\lib'))"
    }
    git worktree prune
    if ("$(git branch --list $branch)") { git branch -D $branch }
    Stamp 'try-pcsx2 done (-Remove). Inside WSL: scripts/linux/try-pcsx2.sh --tag <tag> --parts remove'
    exit 0
}

if ($part -contains 'check') {
    Start-Part 'check' "What PCSX2 changed from $pinned to $Tag"
    Get-Reading
    $changed = @(Get-Changed)
    $commits = @(git -C $reading log '--format=%h %s' "$pinned..$Tag")
    Say "$($commits.Count) commits, $($changed.Count) files."
    Say ''
    $watch = Read-List (Join-Path $Ps2nRoot 'scripts\pcsx2-watch.txt')
    $watched = @()
    foreach ($file in $changed) {
        foreach ($row in $watch) {
            if ($file.Path -like $row[0]) { $watched += [pscustomobject]@{ Path = $file.Path; Added = $file.Added; Deleted = $file.Deleted; Follows = $row[1] }; break }
        }
    }
    if ($watched) {
        Say "**On the watch list** (scripts/pcsx2-watch.txt), $($watched.Count) of them:"
        Say ''
        Say '| PCSX2''s file | + | - | What follows it in PS2Native |'
        Say '|---|---:|---:|---|'
        $watched | ForEach-Object { Say "| ``$($_.Path)`` | $($_.Added) | $($_.Deleted) | $($_.Follows) |" }
        Say ''
        $paths = @($watched | ForEach-Object { $_.Path })
        git -C $reading diff "--output=$(Join-Path $dir 'watched.diff')" $pinned $Tag -- @paths
        Say "Their changes are in ``build\pcsx2-try\$Tag\watched.diff``. The commits that made them:"
        Say ''
        @(git -C $reading log '--format=%h %s' "$pinned..$Tag" -- @paths) | ForEach-Object { Say "- $_" }
    } else { Say 'None of them is on the watch list (scripts/pcsx2-watch.txt).' }
    Say ''
    Say "**PCSX2's dependencies:** its scripts for them $(if (Test-DepsChanged $changed 'windows') { 'CHANGED for Windows: the package has to be fetched for the worktree (-FetchDeps)' } else { 'did not change for Windows' }), and $(if (Test-DepsChanged $changed 'linux') { 'CHANGED for Linux: they are built again in the worktree there' } else { 'did not change for Linux' })."
    Say ''
    $null = Git-Quiet @('-C', $reading, 'checkout', '-q', '--detach', $Tag)
    foreach ($patch in Get-ChildItem -LiteralPath (Join-Path $Ps2nRoot 'patches\pcsx2') -Filter *.patch) {
        $null = Git-Quiet @('-C', $reading, 'apply', '--check', '--whitespace=nowarn', $patch.FullName)
        Say "**PS2Native's change to PCSX2's code** ``$($patch.Name)``: $(if ($LASTEXITCODE -eq 0) { 'still applies' } else { 'DOES NOT APPLY any more' })."
    }
    Say ''
    $names = @(git grep -c -F $pinned.Substring(1) -- . ':!docs/app-log.md' ':!docs/adr' ':!docs/history.md')
    $names | Set-Content -LiteralPath (Join-Path $dir 'names.txt') -Encoding UTF8
    Say "**The version followed is named** in $($names.Count) files outside the logs and the decisions (``build\pcsx2-try\$Tag\names.txt``): a move changes those that say what PS2Native follows."
    Say ''
    Say 'All the commits:'
    Say ''
    $commits | ForEach-Object { Say "- $_" }
}

if ($part -contains 'tree') {
    Start-Part 'tree' "The worktree"
    Get-Reading
    $changed = @(Get-Changed)
    if (-not (Test-Path -LiteralPath $w)) {
        if ("$(git branch --list $branch)") { git worktree add $w $branch | Out-Null } else { git worktree add $w -b $branch | Out-Null }
        if (-not (Test-Path -LiteralPath (Join-Path $w 'CMakeLists.txt'))) { Stamp "the worktree was not made at ${w}: stopping"; exit 1 }
    }
    $sha = "$(git -C $reading rev-parse "$Tag^{commit}")".Trim()
    git -C $w update-index --cacheinfo "160000,$sha,third_party/pcsx2"
    $sub = Join-Path $w 'third_party\pcsx2'
    if (-not (Test-Path -LiteralPath (Join-Path $sub 'CMakeLists.txt'))) {
        if (Test-Path -LiteralPath $sub) { Remove-Item -LiteralPath $sub -Recurse -Force }
        $null = Git-Quiet @('clone', '-q', '--depth', '1', '--branch', $Tag, (ForGit $reading), $sub)
    }
    $null = Git-Quiet @('-C', $w, 'submodule', 'update', '--init', 'third_party/ps2autotests')
    Say "``$w``, on the branch ``$branch``, from $("$(git -C $w rev-parse --short HEAD)".Trim()). PCSX2 in it: $("$(Git-Quiet @('-C', $sub, 'describe', '--tags'))".Trim()) ($($sha.Substring(0, 8))). The console's test programs: $(if (Test-Path -LiteralPath (Join-Path $w 'third_party\ps2autotests\tests')) { 'there' } else { 'NOT THERE' })."
    $link = Join-Path $w 'deps'
    if (-not (Test-Path -LiteralPath $link)) {
        if (Test-DepsChanged $changed 'windows') {
            if (-not $FetchDeps) {
                Say "PCSX2's scripts for its Windows dependencies changed: the worktree needs the package PCSX2 builds from them, not this tree's. -FetchDeps fetches it (about 170 MB, from PCSX2's own releases) into the worktree's deps."
                Stamp 'stopping'
                exit 1
            }
            New-Item -ItemType Directory -Force $link | Out-Null
            foreach ($entry in Get-ChildItem -LiteralPath (Join-Path $Ps2nRoot 'deps') -Directory | Where-Object { $_.Name -notlike 'windows-x64*' }) { cmd /c mklink /J "$(Join-Path $link $entry.Name)" "$($entry.FullName)" | Out-Null }
            $archive = Join-Path $link 'pcsx2-windows-dependencies.7z'
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri 'https://github.com/PCSX2/pcsx2-windows-dependencies/releases/download/latest-windows-dependencies/pcsx2-windows-dependencies.7z' -OutFile $archive -UseBasicParsing
            $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/PCSX2/pcsx2-windows-dependencies/releases/tags/latest-windows-dependencies' -Headers @{ 'User-Agent' = 'ps2n-try-pcsx2' }
            $wanted = "$(($release.assets | Where-Object { $_.name -eq 'pcsx2-windows-dependencies.7z' }).digest)" -replace '^sha256:', ''
            $got = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLower()
            if (-not $wanted -or $got -ne $wanted) { Say "The package fetched is not the one PCSX2's release names (SHA-256 $got, not $wanted)."; exit 1 }
            $zip = (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
            if (-not $zip) { $zip = Join-Path $env:ProgramFiles '7-Zip\7z.exe' }
            if (-not (Test-Path -LiteralPath $zip)) { Say "The package is at $archive (SHA-256 checked). 7-Zip is not installed: unpack it into $link\windows-x64, so that $link\windows-x64\deps\lib is there, and run this part again."; exit 1 }
            & $zip x "-o$(Join-Path $link 'windows-x64')" -y $archive | Out-Null
            Say "PCSX2's Windows dependency package of $($release.published_at) fetched into the worktree's deps (SHA-256 $got)."
        } else {
            cmd /c mklink /J "$link" "$(Join-Path $Ps2nRoot 'deps')" | Out-Null
            Say "Its deps are this tree's (a link): PCSX2's scripts for its Windows dependencies did not change."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $link 'windows-x64\deps\lib'))) { Say "No dependency package at $link\windows-x64."; Stamp 'stopping'; exit 1 }
}

if ($part -contains 'build') {
    Start-Part 'build' "Built in the worktree"
    if (-not (Test-Path -LiteralPath (Join-Path $w 'scripts\windows\build.ps1'))) { Stamp "no worktree at $w (the 'tree' part makes it): stopping"; exit 1 }
    foreach ($tree in 'build\windows-clang', 'build\windows-clang-module') {
        Wait-NoGame
        $log = Join-Path $logs "build-$(Split-Path $tree -Leaf).log"
        $started = Get-Date
        $code = Invoke-Script -Root $w -Script 'build.ps1' -Arguments (@('-BuildDir', $tree, '-Reconfigure') + (Get-TreeSettings $tree)) -Log $log
        $seconds = [int]((Get-Date) - $started).TotalSeconds
        if ($code -ne 0) {
            Say "``$tree``: **does not build** (exit $code, $seconds s). What the build said first:"
            Say ''
            Say '```'
            Get-Content -LiteralPath $log | Select-String -Pattern 'error|FAILED|static assertion|static_assert|does not apply' | Select-Object -First 25 |
                ForEach-Object { Say $_.Line.Trim().Substring(0, [Math]::Min(260, $_.Line.Trim().Length)) }
            Say '```'
            Say ''
            Say "PS2Native has to follow PCSX2 there, in the worktree; then this part again. The whole log: ``$log``."
            Stamp 'stopping'
            exit 1
        }
        Say "``$tree``: built in $seconds s. $((Get-Content -LiteralPath $log | Select-String -Pattern "PS2Native: PCSX2's code" | Select-Object -Last 1 | ForEach-Object { $_.Line.Trim() }))"
    }
    $layout = 'runtime\include\ps2n\pcsx2_layout.h'
    $version = { param($tree) "$((Select-String -LiteralPath (Join-Path $tree 'runtime\game_module.h') -Pattern 'kGameModuleVersion\s*=\s*(\d+)' | Select-Object -First 1).Matches.Groups[1].Value)" }
    $same = (Get-FileHash -LiteralPath (Join-Path $Ps2nRoot $layout)).Hash -eq (Get-FileHash -LiteralPath (Join-Path $w $layout)).Hash
    $here = & $version $Ps2nRoot
    $there = & $version $w
    Say ''
    Say "PS2Native's description of PCSX2's structures (``pcsx2_layout.h``): $(if ($same) { 'as in this tree' } else { 'changed in the worktree' }); the module version: $there there, $here here.$(if (-not $same -and $here -eq $there) { ' **The description changed and the module version did not: raise kGameModuleVersion (runtime/game_module.h), or modules compiled before will be loaded and crash.**' })"
}

if ($part -contains 'test') {
    Start-Part 'test' "Every test, in the worktree"
    if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
    $Bios = (Resolve-Path -LiteralPath $Bios).Path
    Say '| Tree | Tests | Seconds |'
    Say '|---|---|---:|'
    $failing = @()
    foreach ($tree in 'build\windows-clang', 'build\windows-clang-module') {
        Wait-NoGame
        $log = Join-Path $logs "test-$(Split-Path $tree -Leaf).log"
        $started = Get-Date
        $code = Invoke-Script -Root $w -Script 'test.ps1' -Arguments @('-Bios', $Bios, '-BuildDir', $tree) -Log $log
        $count = Get-TestCount $log
        Say "| ``$tree`` | $($count.Said) (exit $code) | $([int]((Get-Date) - $started).TotalSeconds) |"
        $failing += @($count.FailedNames | ForEach-Object { "$tree  $_" })
    }
    Say ''
    if ($failing) {
        Say "Those that did not pass ($($failing.Count)):"
        Say ''
        Say '```'
        $failing | Select-Object -First 80 | ForEach-Object { Say $_ }
        Say '```'
        Say ''
        Say "A test's own files are in the worktree's ``<tree>\ps2n\tests\<its name>``. Where selftest/vu-compiled or selftest/iop-compiled says blocks had to be interpreted, the self-test's capture is made again in the worktree (vu-selftest.ps1, iop-selftest.ps1)."
    } else { Say 'None failed.' }
}

if ($part -contains 'games') {
    Start-Part 'games' "The sessions run again"
    if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
    $Bios = (Resolve-Path -LiteralPath $Bios).Path
    Wait-NoGame
    $log = Join-Path $logs 'kit.log'
    $code = Invoke-Script -Root $w -Script 'kit.ps1' -Arguments @('-Out', 'build\kit-try') -Log $log
    if ($code -ne 0) { Say "The worktree's PS2Native folder was not made (exit $code): ``$log``."; Stamp 'stopping'; exit 1 }
    $kit = Join-Path $w 'build\kit-try'
    $wizard = Join-Path $kit 'runtime\ps2n-wizard.exe'
    Say "A PS2Native folder made from the worktree. A copy of each measurement folder brought up to date with it and compiled; then each session whole, VU1 on the main processor thread, one run of each."
    Say ''
    Say '| Game | PCSX2 | Stock PCSX2 | The compiled game | Its EE cycles |'
    Say '|---|---|---|---|---|'
    foreach ($row in (Read-List (InRoot $List))) {
        if ($row.Count -ne 5) { Say "A line of $List has $($row.Count) parts, not 5: left out."; continue }
        $name = $row[0]; $image = $row[1]; $folder = $row[2].TrimEnd('\'); $session = $row[3]; $renderer = $row[4]
        Wait-NoGame
        $copy = Join-Path $kit "games\$name"
        if (Test-Path -LiteralPath $copy) { cmd /c rmdir /s /q "$copy" | Out-Null }
        New-Item -ItemType Directory -Force (Split-Path $copy) | Out-Null
        robocopy $folder $copy /E /XD sessions /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
        Stamp "=== ${name}: a copy of $folder"
        $updated = Update-GameFolder $wizard $copy
        # It is played with the counts it has.
        Remove-Item -LiteralPath (Join-Path $copy 'work\count-again.txt') -ErrorAction SilentlyContinue
        $made = Invoke-Remake $copy
        Stamp ("  brought up to date: exit {0}; compiled: exit {1} in {2:N0} s; modules: {3}" -f $updated, $made.ExitCode, $made.Seconds, $(if ($made.Modules) { $made.Modules } else { 'none new' }))
        foreach ($side in @(@($pinned, $folder, 'followed'), @($Tag, $copy, 'tried'))) {
            $said = @{}
            foreach ($config in 'stock-jit', 'ps2n-static') {
                Wait-NoGame
                $run = Invoke-Replay -Image $image -Bios $Bios -Runtime (Join-Path $side[1] 'ps2n-host.exe') -Session $session -Config $config -Renderer $renderer `
                    -Name "try-$name-$($side[2])-$config" -Seconds 2400 -Log (Join-Path $logs "run-$name-$($side[2])-$config.log")
                $said[$config] = if ($run.IsSession) { "the session at $($run.Compared) of $($run.Written) fingerprints" } elseif ($run.Verdict -match '(parts from it at frame \d+)') { "**$($Matches[1])**" } else { '**no verdict**' }
                if ($config -eq 'ps2n-static') { $said['ee'] = if ($run.Ee -match '^EE cycles (\d+): interpreter (\d+ \([\d.]+%\))') { "$($Matches[1]), interpreted $($Matches[2])" } else { '' } }
                Stamp "  $($side[0]) ${config}: $($said[$config])"
            }
            Say "| $name | $($side[0]) | $($said['stock-jit']) | $($said['ps2n-static']) | $($said['ee']) |"
        }
    }
}

if ($part -contains 'linux') {
    Start-Part 'linux' "Inside WSL"
    if (-not (Test-Path -LiteralPath $w)) { Stamp "no worktree at $w (the 'tree' part makes it): stopping"; exit 1 }
    $staging = Join-Path $dir 'linux-sync'
    Say "The worktree's changes carried over: $(Save-Changes $staging)."
    $toLinux = { param($path) $full = [IO.Path]::GetFullPath($path); "/mnt/$($full.Substring(0, 1).ToLower())/$($full.Substring(3) -replace '\\', '/')" }
    $arguments = "--tag $Tag --from '$(& $toLinux $w)' --staging '$(& $toLinux $staging)' --pcsx2 '$(& $toLinux $reading)' --timed '$LinuxList' --parts $LinuxParts"
    $log = Join-Path $logs 'linux.log'
    $wsl = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'wsl.ps1'), '-Script', 'try-pcsx2.sh', '-Sync', 'tree', '-Arguments', $arguments, '-Log', $log)
    if ($Bios) { $wsl += @('-Bios', (Resolve-Path -LiteralPath $Bios).Path) }
    & powershell @wsl | ForEach-Object { Write-Host "  $_" }
    Say "scripts/linux/try-pcsx2.sh: exit $LASTEXITCODE. What it said:"
    Say ''
    Say '```'
    Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^\s*$' } | Select-Object -Last 60 | ForEach-Object { Say $_ }
    Say '```'
}

if ($part -contains 'report') {
    $script:saying = ''
    $report = Join-Path $dir 'report.md'
    $text = @("# PS2Native tried on PCSX2 $Tag", '', "PS2Native follows $pinned. Tried $(Get-Date -Format 'yyyy-MM-dd'), from $("$(git rev-parse --short HEAD)".Trim()).", '')
    if (Test-Path -LiteralPath $w) {
        $text += "The worktree's changes, PS2Native following PCSX2 there: $(Save-Changes (Join-Path $dir 'branch')) (``build\pcsx2-try\$Tag\branch\tree.patch``)."
        $text += ''
        $text += '```'
        $text += @(git -C $w status --short --ignore-submodules=all | Select-Object -First 60)
        $text += '```'
        $text += ''
    }
    foreach ($name in 'check', 'tree', 'build', 'test', 'games', 'linux') {
        $file = Join-Path $dir "$name.md"
        if (Test-Path -LiteralPath $file) { $text += @(Get-Content -LiteralPath $file -Encoding UTF8); $text += '' }
    }
    [IO.File]::WriteAllLines($report, [string[]]$text, (New-Object Text.UTF8Encoding($false)))
    Stamp "the report: $report"
}

Stamp "try-pcsx2 done ($($part -join ','))"
exit 0

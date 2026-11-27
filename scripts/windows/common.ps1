# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
What PS2Native's longer scripts share. Dot-source it.

.DESCRIPTION
    . (Join-Path $PSScriptRoot 'common.ps1')

release.ps1, update-games.ps1, time-games.ps1 and try-pcsx2.ps1 run for
minutes or hours on a computer whose owner may be playing a game on it. What
they share is here:

  Stamp               a line with the time
  Set-QuietPriority   this process, and what it starts, at below-normal
                      priority
  Wait-NoGame         waits while a game is played from the PS2Native folder
                      (build\kit; $Ps2nPlayedFrom says otherwise): nothing
                      heavy runs meanwhile
  Stop-StrayHost      stops a runtime that a run left behind, and only one
                      from the folders it is given: never a game someone plays
  Invoke-Quiet        a program without a console run to its end, what it
                      wrote read after
  Invoke-Script       one of the repository's scripts in a process of its own,
                      everything it says in a log
  Get-TestCount       what a run of test.ps1 said: passed, failed, skipped,
                      disabled
  Update-GameFolder   a game's folder brought up to date by a wizard
  Invoke-Remake       a game's folder compiling what is left, as its launcher
                      has it do before the game starts, and its new modules
                      put in place as the launcher puts them
  Invoke-Replay       a session run again through game.ps1, and what the run
                      said of itself
  New-FolderTwin      a game's folder again with another runtime, its other
                      files hard links
  Get-FolderImage, Get-SessionEnd, Get-Cards, Get-FileNote, Read-List

Nothing here knows a game, a disc or a session: the scripts are given those,
by their parameters and by lists in the build tree, which git ignores.

On 2026-11-21 a check stopped every ps2n-host it found, and so closed a game
the maintainer was playing. Hence Stop-StrayHost's folders.
#>

$Ps2nRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# Numbers are written the same on every computer, whatever its language: a log
# is read by the next script as well as by a person.
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture

# Where games are played from on this computer: a process started from one of
# these is someone playing (the launcher, the runtime, its ps2n-remake).
if (-not $Ps2nPlayedFrom) {
    $Ps2nPlayedFrom = @((Join-Path $Ps2nRoot 'build\kit\*'), (Join-Path $Ps2nRoot 'build\kit-*\*'))
}

function Stamp([string]$What) {
    Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $What"
}

function Set-QuietPriority {
    [Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal'
}

function Get-GameProcess {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $path = $null
            try { $path = $_.Path } catch { }
            $found = $false
            if ($path) { foreach ($pattern in $Ps2nPlayedFrom) { if ($path -like $pattern) { $found = $true } } }
            $found
        })
}

function Wait-NoGame {
    $said = $false
    while ($true) {
        $playing = Get-GameProcess
        if (-not $playing) {
            if ($said) { Stamp 'the game has closed: going on' }
            return
        }
        if (-not $said) {
            Stamp "a game is being played ($(($playing | ForEach-Object { $_.ProcessName } | Sort-Object -Unique) -join ', ')): waiting"
            $said = $true
        }
        Start-Sleep -Seconds 30
    }
}

# Stops the runtimes started from under one of the folders given, and says how
# many there were. Never one from anywhere else.
function Stop-StrayHost([string[]]$Under) {
    $left = @(Get-Process ps2n-host -ErrorAction SilentlyContinue | Where-Object {
            $path = $null
            try { $path = $_.Path } catch { }
            $found = $false
            if ($path) { foreach ($folder in $Under) { if ($folder -and $path -like (Join-Path $folder '*')) { $found = $true } } }
            $found
        })
    if ($left) { $left | Stop-Process -Force }
    return $left.Count
}

# The size and time of a file, or 'none'.
function Get-FileNote([string]$File) {
    if (Test-Path -LiteralPath $File) {
        $item = Get-Item -LiteralPath $File
        return ('{0:N0} bytes, {1:yyyy-MM-dd HH:mm}' -f $item.Length, $item.LastWriteTime)
    }
    return 'none'
}

# A program without a console run to its end. What it wrote is shown, its
# first lines, and returned with its exit code.
function Invoke-Quiet {
    param([string]$Program, [string[]]$Arguments = @(), [string]$In = '', [int]$Minutes = 60, [int]$Show = 12)
    $out = Join-Path $env:TEMP "ps2n-quiet-$PID-out.txt"
    $err = Join-Path $env:TEMP "ps2n-quiet-$PID-err.txt"
    Remove-Item -LiteralPath $out, $err -ErrorAction SilentlyContinue
    $quoted = @($Arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    $start = @{ FilePath = $Program; PassThru = $true; RedirectStandardOutput = $out; RedirectStandardError = $err }
    if ($In) { $start.WorkingDirectory = $In } else { $start.WorkingDirectory = (Split-Path $Program) }
    if ($quoted) { $start.ArgumentList = $quoted }
    $process = Start-Process @start
    # Without this the exit code of a process that has ended cannot be read.
    $null = $process.Handle
    if (-not $process.WaitForExit($Minutes * 60000)) {
        Stamp "  $(Split-Path $Program -Leaf) still running after $Minutes minutes: stopping it"
        $process.Kill()
        $process.WaitForExit()
    }
    $lines = @(Get-Content -LiteralPath $out, $err -ErrorAction SilentlyContinue | Where-Object { $_ })
    $lines | Select-Object -First $Show | ForEach-Object { Write-Host "  | $($_.Substring(0, [Math]::Min(230, $_.Length)))" }
    Remove-Item -LiteralPath $out, $err -ErrorAction SilentlyContinue
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Lines = $lines }
}

# One of a repository's scripts in a process of its own, everything it says
# written to a log. Returns its exit code. (Through cmd: Windows PowerShell
# turns what a program writes to its error stream into error records.)
function Invoke-Script {
    param([string]$Script, [string[]]$Arguments = @(), [string]$Log, [string]$Root = $Ps2nRoot)
    $words = @('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Root "scripts\windows\$Script")) + $Arguments
    $line = (@($words | ForEach-Object { if ($_ -eq '' -or $_ -match '[\s()&,;|<>^]') { '"' + $_ + '"' } else { $_ } }) -join ' ')
    New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
    Push-Location $Root
    try { cmd /c "$line > `"$Log`" 2>&1" } finally { Pop-Location }
    return $LASTEXITCODE
}

# What a run of test.ps1 said, read from what it wrote. It runs ctest twice:
# the tools' unit tests, then the tree's own tests, which are the count the
# logs give for a tree. A skipped test is not a passed one: ctest counts it
# among those that did not fail.
function Get-TestCount([string]$Log) {
    $text = @(Get-Content -LiteralPath $Log -ErrorAction SilentlyContinue)
    $runs = @()
    foreach ($line in $text) {
        if ($line -cmatch '^\d+% tests passed, (\d+) tests failed out of (\d+)') { $runs += , @([int]$Matches[1], [int]$Matches[2]) }
    }
    $failed = 0; $total = 0; $tools = ''
    if ($runs.Count -ge 1) { $failed = $runs[-1][0]; $total = $runs[-1][1] }
    if ($runs.Count -ge 2) { $tools = "; the tools' unit tests: $($runs[0][1] - $runs[0][0]) passed, $($runs[0][0]) failed"; $failed += $runs[0][0] }
    # A newer ctest writes a test's labels after its status: nothing is
    # required after it.
    $skipped = @($text | Where-Object { $_ -cmatch '^\s*\d+ - .+ \(Skipped\)' }).Count
    $disabled = @($text | Where-Object { $_ -cmatch '^\s*\d+ - .+ \(Disabled\)' }).Count
    $passed = $total - $runs[-1][0] - $skipped
    $names = @($text | Where-Object { $_ -cmatch '^\s*\d+ - .+ \((Failed|Timeout|Not Run|SEGFAULT|Exception[^)]*)\)' } | ForEach-Object { $_.Trim() })
    return [pscustomobject]@{ Passed = $passed; Failed = $failed; Skipped = $skipped; Disabled = $disabled; Total = $total; FailedNames = $names
        Said = "$passed passed, $failed failed, $skipped skipped, $disabled disabled$tools" }
}

# A game's folder given a PS2Native folder's programs by its wizard, without a
# window. Returns the wizard's exit code.
function Update-GameFolder([string]$Wizard, [string]$Folder) {
    $run = Invoke-Quiet -Program $Wizard -Arguments @('--update', $Folder) -Minutes 10 -Show 8
    return $run.ExitCode
}

# The newest modules a folder's compile made ready, put in place as its
# launcher puts them at the next start. Returns their folder's name, or ''.
function Use-ReadyModules([string]$Folder) {
    $next = @(Get-ChildItem -LiteralPath (Join-Path $Folder 'work') -Directory -Filter 'next-*' -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ready') } | Sort-Object Name)
    if (-not $next) { return '' }
    Copy-Item -Path (Join-Path $next[-1].FullName 'ps2n-*.dll') -Destination $Folder -Force
    $next | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
    return $next[-1].Name
}

# A game's folder compiling what is left to compile, as its launcher has it do
# before the game starts. What its log says of it is shown.
function Invoke-Remake([string]$Folder, [int]$Minutes = 90) {
    $log = Join-Path $Folder 'work\remake.log'
    $mark = 0
    if (Test-Path -LiteralPath $log) { $mark = @(Get-Content -LiteralPath $log).Count }
    $started = Get-Date
    $run = Invoke-Quiet -Program (Join-Path $Folder 'ps2n-remake.exe') -Arguments @('--before-play') -In $Folder -Minutes $Minutes -Show 4
    $seconds = ((Get-Date) - $started).TotalSeconds
    Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | Select-Object -Skip $mark | Where-Object { $_ -notmatch 'frame \d+ of' } |
        Select-String -Pattern 'before the game|brought up to date|nothing new|new code|Compiling|compiled|ready for|not compiled|failed|left for|toolchain' |
        Select-Object -First 12 | ForEach-Object { Write-Host ('  | ' + $_.Line.Substring(0, [Math]::Min(230, $_.Line.Length))) }
    $modules = Use-ReadyModules $Folder
    return [pscustomobject]@{ ExitCode = $run.ExitCode; Seconds = $seconds; Modules = $modules }
}

# The disc image a game's folder plays: the last line of its settings.txt.
function Get-FolderImage([string]$Folder) {
    $lines = @(Get-Content -LiteralPath (Join-Path $Folder 'settings.txt') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
    return $lines[-1]
}

# How many frames a session written down has.
function Get-SessionEnd([string]$Session) {
    $found = Select-String -LiteralPath (Join-Path $Session 'input.txt') -Pattern '^end (\d+)' | Select-Object -Last 1
    if ($found) { return [int]$found.Matches[0].Groups[1].Value }
    return 0
}

# The memory cards of a game's folder, each with its SHA-256, by its path in
# the folder. Those of its sessions and of its compile's runs are not its own.
function Get-Cards([string]$Folder) {
    @(Get-ChildItem -LiteralPath $Folder -Recurse -File -Filter '*.ps2' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notlike (Join-Path $Folder 'sessions\*') -and $_.FullName -notlike (Join-Path $Folder 'work\*') } |
            ForEach-Object { '{0} {1}' -f $_.FullName.Substring($Folder.Length + 1), (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}

# The lines of a list in the build tree: parts separated by |, lines that
# begin with # left out (build\pgo\training.txt is one).
function Read-List([string]$File) {
    if (-not (Test-Path -LiteralPath $File)) { return @() }
    $rows = @()
    foreach ($line in Get-Content -LiteralPath $File) {
        if (-not $line.Trim() -or $line -match '^\s*#') { continue }
        $rows += , @($line -split '\|' | ForEach-Object { $_.Trim() })
    }
    return , $rows
}

# A session run again through game.ps1, or a run from boot where no session
# is given, and what the run said of itself: whether it was the session at
# every fingerprint compared, the EE's cycles, and with -Marks the time from
# frame 1 to the last frame. -Root: another checkout's game.ps1 (a worktree).
function Invoke-Replay {
    param(
        [string]$Image, [string]$Bios, [string]$Runtime, [string]$Session = '', [int]$Frames = 0,
        [string]$Config = 'ps2n-static', [string]$Renderer = 'null', [string]$Name = 'replay', [int]$Seconds = 900,
        [switch]$VuThread, [switch]$Marks, [string]$Modules = '', [string[]]$Extra = @(), [string]$Log = '', [string]$Root = $Ps2nRoot
    )
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Root 'scripts\windows\game.ps1'), '-Image', $Image, '-Bios', $Bios,
        '-Config', $Config, '-Renderer', $Renderer, '-Upscale', '1', '-Name', $Name, '-Seconds', "$Seconds")
    if ($Runtime) { $arguments += @('-Runtime', $Runtime) }
    if ($Modules) { $arguments += @('-Modules', $Modules) }
    if ($Session) { $arguments += @('-Replay', $Session) }
    if ($Frames -gt 0) { $arguments += @('-Frames', "$Frames") }
    if ($Marks) { $arguments += @('-Marks', '1') }
    if ($VuThread) { $arguments += '-Pcsx2VuThread' }
    $arguments += $Extra
    $started = Get-Date
    $output = @(& powershell @arguments 2>&1 | ForEach-Object { "$_" })
    $wall = ((Get-Date) - $started).TotalSeconds
    if ($Log) {
        New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null
        [IO.File]::WriteAllLines($Log, [string[]]$output)
    }
    $time = $null; $last = 0
    $marked = $output | Where-Object { $_ -match 'wall-clock time since the host started' } | Select-Object -Last 1
    if ($marked -match 'frame 1 at ([\d.]+) s.*frame (\d+) at ([\d.]+) s$') {
        $time = [double]$Matches[3] - [double]$Matches[1]
        $last = [int]$Matches[2]
    }
    $ee = "$($output | Where-Object { $_ -match 'ps2n-host: EE cycles' } | Select-Object -Last 1)" -replace '^ps2n-host: ', ''
    $verdict = "$($output | Where-Object { $_ -match 'fingerprint' -and $_ -match '(session|every|differ)' } | Select-Object -Last 1)" -replace '^ps2n-host: ', ''
    $compared = 0; $written = 0
    $isSession = $false
    if ($verdict -match 'the run is the session at every one of (\d+) fingerprints compared, of the (\d+) written down') {
        $isSession = $true
        $compared = [int]$Matches[1]
        $written = [int]$Matches[2]
    }
    $ended = [bool]($output | Where-Object { $_ -match 'stopped at frame limit' })
    $errors = @($output | Where-Object { $_ -match 'refus|cannot|could not|error|assert' } | Select-Object -First 6)
    return [pscustomobject]@{ Seconds = $time; LastFrame = $last; Ee = $ee; Verdict = $verdict; IsSession = $isSession; Compared = $compared
        Written = $written; Ended = $ended; Errors = $errors; Wall = $wall; Output = $output }
}

# A game's folder again with the runtime of a build tree, its other files hard
# links to the folder's own (no space taken): the same modules run by another
# runtime, to time the two in turns. A run again needs neither the folder's
# work nor its toolchain, sessions and captures, which are left out.
function New-FolderTwin([string]$From, [string]$To, [string]$HostDir) {
    if (Test-Path -LiteralPath $To) { cmd /c rmdir /s /q "$To" | Out-Null }
    New-Item -ItemType Directory -Force $To | Out-Null
    $skip = @('work', 'toolchain', 'sessions', 'captures')
    $count = 0
    foreach ($item in Get-ChildItem -LiteralPath $From -Recurse -File) {
        $relative = $item.FullName.Substring($From.Length + 1)
        if ($skip -contains ($relative -split '\\')[0]) { continue }
        if ($relative -eq 'ps2n-host.exe' -or $relative -eq 'ps2n-host.pdb') { continue }
        $target = Join-Path $To $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null
        $count++
    }
    foreach ($file in 'ps2n-host.exe', 'ps2n-host.pdb') {
        $source = Join-Path $HostDir $file
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $To; $count++ }
    }
    return $count
}

# A number as the logs have it, whatever this computer's language.
function Format-Seconds([double]$Value) {
    return $Value.ToString('F2', [Globalization.CultureInfo]::InvariantCulture)
}

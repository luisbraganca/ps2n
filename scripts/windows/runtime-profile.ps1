# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Makes the profile of play a release's runtime is built with (docs\adr\0047).

.DESCRIPTION
A release's runtime, ps2n-host (PCSX2's core and what PS2Native adds to it),
is built with link-time optimization and with a profile of play: counts of
what the runtime ran in runs of games. The counts come from playing games, so
they are made on the computer that has the games, stay in the build tree, and
are made again for a release from the tree released. This script:

1. builds the runtime to count what it runs, in a build tree of its own
   (-CountDir: scripts\windows\build.ps1 -Module on -RuntimePgo generate);
2. runs each training run with it, the game's code from the game's own folder
   (ps2n-host -modules), each run writing its counts;
3. puts the counts together without the functions no run ran (llvm-profdata
   merge -sparse), which the compiler then takes as it does without a
   profile, into -Out.

The runtime is then built with them:

  scripts\windows\build.ps1 -BuildDir build\windows-clang-module -RuntimeLto on -RuntimePgo use -RuntimePgoData build\pgo\runtime-windows.profdata

The training runs are named in a text file (-Training), which names the
user's own games and so stays in the build tree too. One run to a line, its
parts separated by "|"; a line that begins with # is a comment:

  <name> | <disc image> | <game folder> | <session folder, or -> | <frames, or 0>

<game folder> is a folder that plays the game, made with this tree's programs
(the wizard, or make-game.ps1): the run takes the modules of compiled code
from it and writes nothing into it. <session folder> is a session written
down (a folder's sessions\<time>), run again; "-" runs the game from boot.
<frames> is how far the run goes; 0 is the session whole. A run of a session
must be the session at every fingerprint compared, or the script stops.

Runs worth having: play from folders that have compiled what their sessions
met; a boot; and a first session, from a folder made from the disc alone,
where the game's code is interpreted. A session whose time is measured
against stock PCSX2 is never among them.

.EXAMPLE
scripts\windows\runtime-profile.ps1 -Bios C:\bios\scph70004.bin
#>
param(
    [string]$Training = 'build\pgo\training.txt',
    [string]$Bios = $env:PS2N_BIOS,
    [string]$CountDir = 'build\windows-clang-count',
    [string]$Out = 'build\pgo\runtime-windows.profdata',
    # The runs already made (their counts in <CountDir>\counts) are kept, and only the others made: after one failed.
    [switch]$Resume,
    # The runtime to count with is there already: not built again.
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
function InRoot([string]$path) { if ([IO.Path]::IsPathRooted($path)) { $path } else { Join-Path $root $path } }
$trainingFile = InRoot $Training
$count = InRoot $CountDir
$profile = InRoot $Out
$raw = Join-Path $count 'counts'
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
if (-not (Test-Path -LiteralPath $trainingFile)) { throw "No training runs: $trainingFile (this script's help says what it holds)." }

# The runs.
$runs = @()
foreach ($line in Get-Content -LiteralPath $trainingFile) {
    $text = $line.Trim()
    if (-not $text -or $text.StartsWith('#')) { continue }
    $parts = @($text -split '\|' | ForEach-Object { $_.Trim() })
    if ($parts.Count -ne 5) { throw "${trainingFile}: not a training run (five parts separated by |): $text" }
    $run = @{ Name = $parts[0]; Image = $parts[1]; Folder = $parts[2]; Session = $(if ($parts[3] -eq '-') { '' } else { $parts[3] }); Frames = [int]$parts[4] }
    if ($run.Name -notmatch '^[A-Za-z0-9_-]+$') { throw "${trainingFile}: a run's name is letters, digits, - and _: $($run.Name)" }
    foreach ($path in @($run.Image, $run.Folder, $run.Session) | Where-Object { $_ }) {
        if (-not (Test-Path -LiteralPath $path)) { throw "${trainingFile}, $($run.Name): $path is not there." }
    }
    if (-not $run.Session -and $run.Frames -le 0) { throw "${trainingFile}, $($run.Name): a run from boot needs its frames." }
    $runs += $run
}
if (-not $runs) { throw "${trainingFile} names no training run." }

# cmake, ninja, clang-cl and llvm-profdata only exist inside a Visual Studio developer shell.
if (-not (Get-Command llvm-profdata.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "Visual Studio (or its Build Tools) is not installed." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio installation with the x64 C++ tools was found." }
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    # clang's own tools are beside clang-cl, which the developer shell does not put on the path whole.
    $llvm = Join-Path $vs 'VC\Tools\Llvm\x64\bin'
    if (-not (Get-Command llvm-profdata.exe -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $llvm 'llvm-profdata.exe'))) { $env:PATH = "$llvm;$env:PATH" }
}
if (-not (Get-Command llvm-profdata.exe -ErrorAction SilentlyContinue)) { throw "llvm-profdata is not on the path (Visual Studio's VC\Tools\Llvm\x64\bin)." }

# 1. The runtime built to count.
if (-not $SkipBuild) {
    Write-Host "The runtime built to count what it runs, in $CountDir"
    & (Join-Path $PSScriptRoot 'build.ps1') -BuildDir $CountDir -Module on -Wizard off -RuntimeLto off -RuntimePgo generate
    if ($LASTEXITCODE -ne 0) { throw "The runtime could not be built to count." }
}
if (-not (Test-Path -LiteralPath (Join-Path $count 'ps2n\bin\ps2n-host.exe'))) { throw "No runtime in ${count}: leave -SkipBuild out." }

# 2. The training runs.
New-Item -ItemType Directory -Force $raw | Out-Null
if (-not $Resume) { Get-ChildItem -LiteralPath $raw -Filter '*.profraw' | Remove-Item -Force }
foreach ($run in $runs) {
    $written = @(Get-ChildItem -LiteralPath $raw -Filter "$($run.Name)-*.profraw")
    if ($Resume -and $written) { Write-Host "$($run.Name): its counts are there"; continue }
    $written | Remove-Item -Force
    $arguments = @{ Image = $run.Image; Bios = $Bios; BuildDir = $CountDir; Modules = $run.Folder; Config = 'ps2n-static'; Renderer = 'vulkan'
        Upscale = 1; Name = "train-$($run.Name)"; Seconds = 3600 }
    if ($run.Session) { $arguments.Replay = $run.Session }
    if ($run.Frames -gt 0) { $arguments.Frames = $run.Frames }
    Write-Host "$($run.Name): $(if ($run.Session) { "the session $(Split-Path $run.Session -Leaf)" } else { 'from boot' })$(if ($run.Frames -gt 0) { ", $($run.Frames) frames" }), the modules of $($run.Folder)"
    # (%p: the process's number, so that no run writes over another's counts.)
    $env:LLVM_PROFILE_FILE = Join-Path $raw "$($run.Name)-%p.profraw"
    try { $output = & (Join-Path $PSScriptRoot 'game.ps1') @arguments 6>&1 | ForEach-Object { "$_" }; $code = $LASTEXITCODE }
    finally { Remove-Item Env:\LLVM_PROFILE_FILE }
    $fingerprints = ($output | Where-Object { $_ -match 'the run is the session at' } | Select-Object -Last 1) -replace '^.*the run is the session at', 'the session at'
    if ($code -ne 0) { $output | Select-Object -Last 15 | ForEach-Object { Write-Host "  | $_" }; throw "$($run.Name): the run did not end as expected." }
    if ($run.Session -and $fingerprints -notmatch 'every one of') { throw "$($run.Name): the run is not the session ($fingerprints): not trained on." }
    $written = @(Get-ChildItem -LiteralPath $raw -Filter "$($run.Name)-*.profraw")
    if (-not $written) { throw "$($run.Name): the run wrote no counts. Was the runtime in $CountDir built to count?" }
    Write-Host ("  {0}; counts: {1:N0} bytes" -f $(if ($fingerprints) { $fingerprints } else { 'ran' }), ($written | Measure-Object Length -Sum).Sum)
}

# 3. The counts put together, without the functions no run ran.
$files = @(Get-ChildItem -LiteralPath $raw -Filter '*.profraw' | ForEach-Object { $_.FullName })
New-Item -ItemType Directory -Force (Split-Path $profile) | Out-Null
& llvm-profdata merge -sparse -o $profile @files
if ($LASTEXITCODE -ne 0) { throw "llvm-profdata could not put the counts together." }
Write-Host ("The profile: {0} ({1:N0} bytes, from {2} runs)" -f $profile, (Get-Item -LiteralPath $profile).Length, $runs.Count)
& llvm-profdata show $profile | Select-Object -First 8 | ForEach-Object { Write-Host "  $_" }
Write-Host "Build the runtime with it:"
Write-Host "  scripts\windows\build.ps1 -BuildDir build\windows-clang-module -RuntimeLto on -RuntimePgo use -RuntimePgoData $Out"

# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Runs a game from its disc image on ps2n-host for a number of frames.

.DESCRIPTION
Boots the disc image through the BIOS under one CPU configuration and stops
after -Frames frames. Prints what the host reports about the run: where it
stopped, the frame on display, and, for PS2Native's provider, what was
compiled, interpreted and delegated.

The configurations:

  stock-jit     PCSX2's recompilers: the reference
  stock-interp  PCSX2's interpreters
  ps2n-ee       PS2Native's EE provider, interpreting everything
  ps2n-static   PS2Native's EE provider with the game's code compiled ahead of
                time (build it in first: scripts\windows\build.ps1 -Game <elf>)
  lockstep      ps2n-ee and then ps2n-static, the second compared with the
                first at every event test

The disc image is read where it is and nothing is written beside it. The
run's files (the game's console output, the core's log, the last frame as a
picture, what the run reports for the recompiler) go to
build\windows-clang\ps2n\tests\game\<Name>\<Config>, which git ignores. They
are derived from the game: keep them out of the repository.

With -Capture, what the run reports for the recompiler is added to that file
(ps2n-recomp capture), which scripts\windows\build.ps1 -GameCapture takes.

.EXAMPLE
scripts\windows\game.ps1 -Image "C:\ps2\killzone\Killzone (Europe) (En,Fr,De,Es,It,Nl).iso" -Config stock-jit
scripts\windows\game.ps1 -Image <iso> -Config ps2n-static -Capture build\game\killzone\capture.toml
scripts\windows\game.ps1 -Image <iso> -Config lockstep -Frames 900
#>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [string]$Bios = $env:PS2N_BIOS,
    [ValidateSet('stock-jit', 'stock-interp', 'ps2n-ee', 'ps2n-static', 'lockstep')][string]$Config = 'ps2n-static',
    [int]$Frames = 900,
    # null (nothing drawn, fastest), sw (PCSX2's software renderer: the picture to compare), vulkan, opengl, dx11, dx12
    [string]$Renderer = 'sw',
    [int]$Upscale = 1,
    # Also fingerprint and save the frame on display every so many frames (0: only the last).
    [int]$FrameEvery = 0,
    # A script of controller input for port 1 (ps2n-host -help has the format; tests\games has some).
    [string]$Pad = '',
    # Wall-clock limit of each run.
    [int]$Seconds = 1800,
    # Names the run's folder.
    [string]$Name = 'game',
    [string]$Capture = '',
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root $BuildDir
$hostExe = Join-Path $build 'ps2n\bin\ps2n-host.exe'
if (-not (Test-Path $hostExe)) { throw "Build first: scripts\windows\build.ps1" }
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
$env:PS2N_BIOS = (Resolve-Path -LiteralPath $Bios).Path
$imagePath = (Resolve-Path -LiteralPath $Image).Path

# cmake only exists inside a Visual Studio developer shell.
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "Visual Studio (or its Build Tools) is not installed." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio installation with the x64 C++ tools was found." }
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function ForCMake([string]$path) { $path -replace '\\', '/' }

$providers = @{
    'stock-jit'    = @('jit', 'jit', 'jit', 'on')
    'stock-interp' = @('interp', 'interp', 'interp', 'on')
    'ps2n-ee'      = @('ps2n', 'jit', 'jit', 'off')
    'ps2n-static'  = @('ps2n', 'jit', 'jit', 'on')
    'lockstep'     = @('ps2n', 'jit', 'jit', 'on')
}
$p = $providers[$Config]
$work = Join-Path $build "ps2n\tests\game\$Name\$Config"

# A game prints no last line to stop at: the run ends at the frame limit.
$arguments = @(
    "-DHOST=$(ForCMake $hostExe)", "-DWORK_DIR=$(ForCMake $work)",
    "-DDLL_DIR=$(ForCMake (Join-Path $root 'deps\windows-x64\deps\bin'))", "-DELF=$(ForCMake $imagePath)",
    "-DEE=$($p[0])", "-DIOP=$($p[1])", "-DVU=$($p[2])", "-DSTATIC=$($p[3])", '-DIMAGE=game',
    "-DFRAMES=$Frames", "-DSECONDS=$Seconds", '-DUNTIL=@@ no such line @@', '-DEXPECT_EXIT=2', '-DWHOLE=1', '-DTIMES=1')
if ($Renderer -ne 'null') { $arguments += @("-DRENDERER=$Renderer", "-DUPSCALE=$Upscale", "-DFRAME_EVERY=$FrameEvery") }
if ($Pad) { $arguments += "-DPAD=$(ForCMake (Resolve-Path -LiteralPath $Pad).Path)" }
if ($Config -eq 'lockstep') { $arguments += '-DLOCKSTEP=1' }
elseif ($p[0] -eq 'ps2n') { $arguments += '-DCAPTURE=1' }
$arguments += @('-P', (Join-Path $root 'tests\guest_test.cmake'))

# The driver reports on its error stream, which Windows PowerShell would
# otherwise take for a failure.
$watch = [Diagnostics.Stopwatch]::StartNew()
$ErrorActionPreference = 'Continue'
$output = & cmake @arguments 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$watch.Stop()

$output | Where-Object { $_ -match '^(ps2n-host:|---|The compiled run|First difference|  recorded:|  this run:|CMake Error|  )' } | ForEach-Object { Write-Host $_ }
Write-Host ("{0}: {1:N1} s; files in {2}" -f $Config, $watch.Elapsed.TotalSeconds, $work)

# The comparison of whole outputs is for test programs; a run that ended at
# the frame limit as expected has done what was asked.
$ended = $output | Where-Object { $_ -match 'stopped at frame limit' }
if (-not $ended -or ($Config -eq 'lockstep' -and $code -ne 0)) {
    Write-Host "The run did not end as expected (driver exit code $code)."
    exit 1
}

if ($Capture -and $p[0] -eq 'ps2n') {
    $reported = Join-Path $work 'capture.toml'
    if (Test-Path $reported) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($Capture)) { $Capture } else { Join-Path $root $Capture }
        & $recomp capture --out $target $target $reported
    }
}
exit 0

# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Builds the self-test's random micro programs into the host, compiled ahead of time.

.DESCRIPTION
The self-test holds PS2Native's VU provider to PCSX2's VU recompiler on
random micro programs. Code compiled ahead of time can only take part in
that if a build has compiled those programs' blocks, and a build can only
compile blocks a run has written down for it. This script does both steps:

  1. it runs the first programs of the self-test (as many for each VU and
     setting as the test selftest/vu-compiled runs: PS2N_VU_SELFTEST_PROGRAMS
     in tests\tests.cmake) with every block interpreted, and has the host
     write those blocks' plans to build\vu\selftest-capture.toml;
  2. it builds the host with that capture (scripts\windows\build.ps1
     -VuSelftestCapture), which the build tree then remembers.

After it, scripts\windows\test.ps1 -Filter selftest runs those programs on
the compiled blocks too. Run it again whenever that test says its blocks
had to be interpreted: a change to how blocks are planned gives them other
plans than the ones the host has compiled.

Needs a BIOS image, as every test that starts the machine: -Bios, or the
PS2N_BIOS environment variable. Nothing is written beside it.

.EXAMPLE
scripts\windows\vu-selftest.ps1 -Bios "$HOME\Documents\PCSX2\bios\SCPH-70004_BIOS_V12_PAL_200.BIN"
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    # As many of the programs of each VU and setting as selftest/vu-compiled runs.
    [int]$Programs = 150,
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root $BuildDir
$hostExe = Join-Path $build 'ps2n\bin\ps2n-host.exe'
if (-not (Test-Path $hostExe)) { throw "Build first: scripts\windows\build.ps1" }
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
$env:PS2N_BIOS = (Resolve-Path -LiteralPath $Bios).Path

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

# 1. The programs' blocks, written down by a run that interprets them all.
$captureDir = Join-Path $root 'build\vu'
New-Item -ItemType Directory -Force $captureDir | Out-Null
$capture = Join-Path $captureDir 'selftest-capture.toml'
$work = Join-Path $build 'ps2n\tests\selftest\vu-capture'
$arguments = @(
    "-DHOST=$(ForCMake $hostExe)", "-DWORK_DIR=$(ForCMake $work)",
    "-DDLL_DIR=$(ForCMake (Join-Path $root 'deps\windows-x64\deps\bin'))",
    '-DSELFTEST=1', "-DSELFTEST_VU=$Programs", '-DVU_STATIC=off', "-DVU_CAPTURE=$(ForCMake $capture)", '-DSECONDS=600',
    '-P', (Join-Path $root 'tests\guest_test.cmake'))

# The driver reports on its error stream, which Windows PowerShell would
# otherwise take for a failure.
$ErrorActionPreference = 'Continue'
$output = & cmake @arguments 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$output | Where-Object { $_ -match '^(selftest:|ps2n-host:|CMake Error|  )' } | ForEach-Object { Write-Host $_ }
if ($code -ne 0 -or -not (Test-Path $capture)) {
    Write-Host "The self-test's micro programs did not run to the end (driver exit code $code): nothing built."
    exit 1
}

# 2. The host built with them.
& (Join-Path $PSScriptRoot 'build.ps1') -BuildDir $BuildDir -VuSelftestCapture $capture
exit $LASTEXITCODE

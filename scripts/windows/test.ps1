# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Runs PS2Native's tests on Windows x64.

.DESCRIPTION
Runs CTest in the two build trees that scripts\windows\build.ps1 made:

  1. the unit tests of the build-time tools, which need nothing else;
  2. the tests that boot PS2 programs on the PCSX2 core. These need a BIOS
     image: pass -Bios, or set the PS2N_BIOS environment variable. Without
     one they are reported as skipped.

The BIOS image is not copied and nothing is written beside it.

.EXAMPLE
scripts\windows\test.ps1 -Bios "$HOME\Documents\PCSX2\bios\SCPH-70004_BIOS_V12_PAL_200.BIN"
scripts\windows\test.ps1 -Filter ps2autotests/stock-jit/cpu
scripts\windows\test.ps1 -Filter unit
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    [string]$Filter = '',
    [int]$Jobs = 8,
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$tools = Join-Path $root 'build\tools-windows'
$build = Join-Path $root $BuildDir
if (-not (Test-Path (Join-Path $build 'CTestTestfile.cmake')) -or -not (Test-Path (Join-Path $tools 'CTestTestfile.cmake'))) {
    throw "Build trees are missing. Run scripts\windows\build.ps1 first."
}

# ctest only exists inside a Visual Studio developer shell.
if (-not (Get-Command ctest.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "Visual Studio (or its Build Tools) is not installed." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio installation with the x64 C++ tools was found." }
    # The developer shell's own start-up script calls vswhere by name.
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$failed = 0

# 1. Unit tests of the build-time tools.
if (-not $Filter -or $Filter -like 'unit*') {
    $arguments = @('--test-dir', $tools, '--output-on-failure')
    if ($Filter) { $arguments += @('-R', $Filter) }
    ctest @arguments
    if ($LASTEXITCODE -ne 0) { $failed = $LASTEXITCODE }
}

# 2. Tests that boot PS2 programs on the PCSX2 core.
if (-not ($Filter -like 'unit*')) {
    if ($Bios) {
        $env:PS2N_BIOS = (Resolve-Path $Bios).Path
    } else {
        Write-Host "No BIOS image given (-Bios or PS2N_BIOS): the tests that boot PS2 programs will be reported as skipped."
    }

    $arguments = @('--test-dir', $build, '-j', $Jobs, '--output-on-failure')
    if ($Filter) { $arguments += @('-R', $Filter) }
    ctest @arguments
    if ($LASTEXITCODE -ne 0) { $failed = $LASTEXITCODE }
}

exit $failed

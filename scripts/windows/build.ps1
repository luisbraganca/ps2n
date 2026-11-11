# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Configures and builds PS2Native's proof of concept on Windows x64.

.DESCRIPTION
Drives the CMake project of the pinned PCSX2 submodule, unmodified, and adds
PS2Native's own targets to it through cmake\ps2n-inject.cmake.

Needs Visual Studio or its Build Tools with clang-cl, CMake and Ninja, and
PCSX2's Windows dependency package extracted so that deps\windows-x64\deps
holds bin, include and lib (docs\poc-log.md says where it comes from).

.EXAMPLE
scripts\windows\build.ps1
scripts\windows\build.ps1 -Target ps2n-host, pcsx2-gsrunner
#>
param(
    [string[]]$Target = @('ps2n-host'),
    [string]$BuildDir = 'build\windows-clang',
    [switch]$Reconfigure,
    # Build only the build-time tools (core/, recompiler/) and their unit tests.
    [switch]$ToolsOnly
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pcsx2 = Join-Path $root 'third_party\pcsx2'
$deps = Join-Path $root 'deps\windows-x64\deps'
$build = Join-Path $root $BuildDir

if (-not (Test-Path (Join-Path $pcsx2 'CMakeLists.txt'))) {
    throw "The PCSX2 submodule is missing. Run: git submodule update --init --depth 1"
}
if (-not (Test-Path (Join-Path $deps 'lib'))) {
    throw "PCSX2's Windows dependency package is not at $deps (see docs\poc-log.md)."
}

# cmake, ninja and clang-cl only exist inside a Visual Studio developer shell.
if (-not (Get-Command clang-cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "Visual Studio (or its Build Tools) is not installed." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio installation with the x64 C++ tools was found." }
    # The developer shell's own start-up script calls vswhere by name.
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function ForCMake([string]$path) { $path -replace '\\', '/' }

# 1. The build-time tools and their unit tests (core/, recompiler/,
#    tests/unit/). They do not depend on PCSX2.
$tools = Join-Path $root 'build\tools-windows'
if ($Reconfigure -or -not (Test-Path (Join-Path $tools 'build.ninja'))) {
    cmake -S $root -B $tools -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
cmake --build $tools
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($ToolsOnly) { exit 0 }

# 2. The run-time side (runtime/, host/), inside PCSX2's own CMake project.
#    Its build runs the recompiler built in step 1 on the test programs.
if ($Reconfigure -or -not (Test-Path (Join-Path $build 'build.ninja'))) {
    # The flags PCSX2's own CI uses for its Windows CMake build with clang-cl,
    # minus the Qt interface and link-time optimization.
    cmake -S $pcsx2 -B $build -G Ninja `
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_PREFIX_PATH=$(ForCMake $deps)" `
        -DDISABLE_ADVANCE_SIMD=ON `
        -DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF `
        "-DCMAKE_PROJECT_Pcsx2_INCLUDE=$(ForCMake (Join-Path $root 'cmake\ps2n-inject.cmake'))" `
        "-DPS2N_ROOT=$(ForCMake $root)" `
        "-DPS2N_RECOMP=$(ForCMake (Join-Path $tools 'bin\ps2n-recomp.exe'))"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $build --target $Target
exit $LASTEXITCODE

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

With -Game, a game's boot executable is compiled into the host as well. The
executable is read where it is; what is generated from it stays in the build
tree. The choice is remembered by the build tree until -Game is given again
(-Game none takes the game out).

With -Bios, the EE code of a BIOS dump is compiled into the host as well
(docs\adr\0017): the user's own dump, read where it is, and what is
generated from it stays in the build tree and never goes into the
repository. -BiosCapture adds what runs reported of the BIOS
(ps2n-host -bios-capture). Both are remembered like -Game; "none" takes
them out.

With -Pcsx2Source, another PCSX2 source tree is built in the place of the pinned
submodule: to try PS2Native on a newer PCSX2 before the submodule is moved,
or to check a change someone made to PCSX2 against the tests here. Give it a
build tree of its own with -BuildDir (under build\, which git ignores), and
the same -BuildDir to scripts\windows\test.ps1. That tree has to be close
enough to the pinned version for PS2Native's host to compile against it.

.EXAMPLE
scripts\windows\build.ps1
scripts\windows\build.ps1 -Target ps2n-host, pcsx2-gsrunner
scripts\windows\build.ps1 -Game "C:\ps2\killzone\Killzone (Europe) (En,Fr,De,Es,It,Nl)\SCES_520.04"
scripts\windows\build.ps1 -Pcsx2Source C:\src\pcsx2 -BuildDir build\windows-clang-other
#>
param(
    [string[]]$Target = @('ps2n-host'),
    [string]$BuildDir = 'build\windows-clang',
    # Another PCSX2 source tree to build in the place of third_party\pcsx2.
    # (Not named Pcsx2: PowerShell would take it for the variable $pcsx2 below.)
    [string]$Pcsx2Source = '',
    [switch]$Reconfigure,
    # Build only the build-time tools (core/, recompiler/) and their unit tests.
    [switch]$ToolsOnly,
    # A game's boot executable to compile into the host, or "none".
    [string]$Game = '',
    # What earlier runs of the game reported for the recompiler (ps2n-host -capture), or "none".
    [string]$GameCapture = '',
    # A BIOS dump whose EE code to compile into the host, or "none".
    [string]$Bios = '',
    # What earlier runs reported of the BIOS for the recompiler (ps2n-host -bios-capture), or "none".
    [string]$BiosCapture = ''
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pcsx2 = Join-Path $root 'third_party\pcsx2'
$deps = Join-Path $root 'deps\windows-x64\deps'
$build = Join-Path $root $BuildDir

if ($Pcsx2Source) {
    $pcsx2 = (Resolve-Path -LiteralPath $Pcsx2Source).Path
    if (-not (Test-Path (Join-Path $pcsx2 'pcsx2\CMakeLists.txt'))) { throw "$pcsx2 is not a PCSX2 source tree." }
    if ($BuildDir -eq 'build\windows-clang') {
        throw "Give another PCSX2 source tree a build tree of its own: -BuildDir build\<name>."
    }
}
elseif (-not (Test-Path (Join-Path $pcsx2 'CMakeLists.txt'))) {
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
# A game to compile into the host: given here, it is put into the build tree's
# settings, which keep it until it is given again.
$gameSettings = @()
if ($Game) {
    $gamePath = if ($Game -eq 'none') { '' } else { ForCMake (Resolve-Path -LiteralPath $Game).Path }
    $gameSettings += "-DPS2N_GAME_ELF=$gamePath"
}
if ($GameCapture) {
    $capturePath = if ($GameCapture -eq 'none') { '' } else { ForCMake (Resolve-Path -LiteralPath $GameCapture).Path }
    $gameSettings += "-DPS2N_GAME_CAPTURE=$capturePath"
}
if ($Bios) {
    $biosPath = if ($Bios -eq 'none') { '' } else { ForCMake (Resolve-Path -LiteralPath $Bios).Path }
    $gameSettings += "-DPS2N_BIOS_IMAGE=$biosPath"
}
if ($BiosCapture) {
    $biosCapturePath = if ($BiosCapture -eq 'none') { '' } else { ForCMake (Resolve-Path -LiteralPath $BiosCapture).Path }
    $gameSettings += "-DPS2N_BIOS_CAPTURE=$biosCapturePath"
}

if ($Reconfigure -or $gameSettings -or -not (Test-Path (Join-Path $build 'build.ninja'))) {
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
        "-DPS2N_RECOMP=$(ForCMake (Join-Path $tools 'bin\ps2n-recomp.exe'))" `
        @gameSettings
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

cmake --build $build --target $Target
exit $LASTEXITCODE

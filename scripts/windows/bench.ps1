# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Measures how fast the EE's code runs under each CPU configuration.

.DESCRIPTION
Runs tests\programs\bench.s (built by scripts\windows\build.ps1) once per
configuration, one after the other so that the runs do not compete, and
prints what ps2n-host measured for each workload: wall-clock milliseconds and
millions of EE cycles per second. The EE's clock on a console is 294.912 MHz.

The configurations:

  stock-jit     PCSX2's recompilers
  stock-interp  PCSX2's interpreters
  ps2n-ee       PS2Native's EE provider, interpreting everything
  ps2n-static   PS2Native's EE provider with the program compiled ahead of time

.EXAMPLE
scripts\windows\bench.ps1 -Bios "$HOME\Documents\PCSX2\bios\SCPH-70004_BIOS_V12_PAL_200.BIN"
scripts\windows\bench.ps1 -Config ps2n-static, stock-jit -Repeat 3
#>
param(
    [string]$Bios = $env:PS2N_BIOS,
    [string[]]$Config = @('stock-jit', 'stock-interp', 'ps2n-ee', 'ps2n-static'),
    [int]$Repeat = 1,
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root $BuildDir
$hostExe = Join-Path $build 'ps2n\bin\ps2n-host.exe'
$elf = Join-Path $build 'ps2n\programs\bench.elf'
if (-not (Test-Path $hostExe) -or -not (Test-Path $elf)) { throw "Build first: scripts\windows\build.ps1" }
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
$env:PS2N_BIOS = (Resolve-Path $Bios).Path

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
}

$results = [ordered]@{}   # workload -> configuration -> list of milliseconds
$rates = [ordered]@{}
$checksums = @{}
foreach ($name in $Config) {
    if (-not $providers.ContainsKey($name)) { throw "Unknown configuration '$name'." }
    $p = $providers[$name]
    for ($run = 1; $run -le $Repeat; $run++) {
        $work = Join-Path $build "ps2n\tests\bench\$name"
        # The driver reports on its error stream, which Windows PowerShell would
        # otherwise take for a failure.
        $ErrorActionPreference = 'Continue'
        # Timed runs are kept on the processor's fastest cores (-DFAST_CORES=1):
        # left to the system, the same run can take twice as long the next time.
        $output = & cmake "-DHOST=$(ForCMake $hostExe)" "-DWORK_DIR=$(ForCMake $work)" `
            "-DDLL_DIR=$(ForCMake (Join-Path $root 'deps\windows-x64\deps\bin'))" "-DELF=$(ForCMake $elf)" `
            "-DEE=$($p[0])" "-DIOP=$($p[1])" "-DVU=$($p[2])" "-DSTATIC=$($p[3])" -DIMAGE=bench `
            -DSECONDS=900 -DFRAMES=1000000 -DFAST_CORES=1 -P (Join-Path $root 'tests\guest_test.cmake') 2>&1 | ForEach-Object { "$_" }
        $ErrorActionPreference = 'Stop'
        if ($LASTEXITCODE -ne 0) {
            $output | ForEach-Object { Write-Host $_ }
            throw "The run under $name failed."
        }
        foreach ($line in $output) {
            if ("$line" -match 'bench (\w+): ([\d.]+) ms, (\d+) EE cycles, (\d+) million') {
                $workload = $Matches[1]
                if (-not $results.Contains($workload)) { $results[$workload] = @{}; $rates[$workload] = @{} }
                if (-not $results[$workload].ContainsKey($name)) { $results[$workload][$name] = @(); $rates[$workload][$name] = @() }
                $results[$workload][$name] += [double]$Matches[2]
                $rates[$workload][$name] += [double]$Matches[4]
            }
        }
        $sum = (Get-FileHash (Join-Path $work 'actual.txt')).Hash
        $checksums[$name] = $sum
    }
}

if (($checksums.Values | Select-Object -Unique).Count -ne 1) {
    Write-Host "WARNING: the configurations did not print the same checksums:"
    $checksums.GetEnumerator() | ForEach-Object { Write-Host "  $($_.Key): $($_.Value)" }
}

# The best of the repeats: the run least disturbed by the rest of the machine.
Write-Host ""
Write-Host ("{0,-8}" -f 'workload') -NoNewline
foreach ($name in $Config) { Write-Host ("{0,26}" -f $name) -NoNewline }
Write-Host ""
foreach ($workload in $results.Keys) {
    Write-Host ("{0,-8}" -f $workload) -NoNewline
    foreach ($name in $Config) {
        $ms = ($results[$workload][$name] | Measure-Object -Minimum).Minimum
        $rate = ($rates[$workload][$name] | Measure-Object -Maximum).Maximum
        Write-Host ("{0,14:N1} ms {1,6:N0} M/s" -f $ms, $rate) -NoNewline
    }
    Write-Host ""
}
Write-Host ""
Write-Host "M/s: millions of EE cycles per second of wall-clock time. A console runs 295."

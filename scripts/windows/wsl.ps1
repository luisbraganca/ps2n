# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Runs one of scripts/linux inside WSL, on this tree.

.DESCRIPTION
Linux is built and tested in a clone of the repository inside WSL 2 (Ubuntu
22.04, ~/ps2n: docs\adr\0038), never on the Windows drive. This brings that
clone to where this tree is, and runs a script there, everything it says in a
log on the Windows side.

  -Sync commit   (default) the clone's git moved to this tree's commit. It is
                 left as it was where it has changes of its own
  -Sync tree     this working tree as it is, changes not committed too: to
                 try a change on Linux before it is committed
  -Sync none     the clone as it is

Then the script, in the clone, with PS2N_BIOS set where -Bios is given. Paths
in -Arguments are Linux paths: a Windows file C:\a\b is /mnt/c/a/b there.

It waits first while a game is played on Windows: a build inside WSL takes
the processor from it as one outside does. The scripts themselves keep to
half the processors.

.EXAMPLE
scripts\windows\wsl.ps1 -Script build.sh -Arguments '--build-dir build/linux-clang-module' -Log build\wsl\build.log
scripts\windows\wsl.ps1 -Script test.sh -Sync tree -Bios C:\bios\scph70004.bin -Log build\wsl\test.log
#>
param(
    # One of scripts/linux, by name.
    [Parameter(Mandatory = $true)][string]$Script,
    # Its arguments, as they would be typed in a Linux shell.
    [string]$Arguments = '',
    # Where everything it says goes, on the Windows side.
    [string]$Log = 'build\wsl\last.log',
    [ValidateSet('commit', 'tree', 'none')][string]$Sync = 'commit',
    [string]$Bios = $env:PS2N_BIOS,
    [string]$Distribution = 'Ubuntu-22.04',
    # The clone inside WSL.
    [string]$Clone = '~/ps2n',
    # A clone of PCSX2, on Windows, that has the commit this tree names: where the clone's own has not.
    [string]$Pcsx2 = '',
    # Not waiting for a game played on Windows to end.
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $Ps2nRoot $path)
}
# A Windows path as WSL sees it.
function ToLinux([string]$path) {
    $full = [IO.Path]::GetFullPath($path)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') { throw "Not a path on a drive: $path" }
    return "/mnt/$($Matches[1].ToLower())/$($Matches[2] -replace '\\', '/')"
}
# A word of a shell's command line, in single quotes.
function Quoted([string]$word) { return "'" + ($word -replace "'", "'\''") + "'" }

if (-not (Test-Path -LiteralPath (Join-Path $Ps2nRoot "scripts\linux\$Script"))) { throw "No such script: scripts/linux/$Script" }
$logFile = InRoot $Log
$staging = Join-Path $Ps2nRoot 'build\wsl\sync'
New-Item -ItemType Directory -Force (Split-Path $logFile), $staging | Out-Null
$env:WSL_UTF8 = '1'

if (-not $NoWait) { Wait-NoGame }

# What runs inside WSL is written to a file first: a command line through
# wsl.exe loses its quotes on the way.
$lines = @('#!/usr/bin/env bash', 'set -uo pipefail')
if ($Sync -ne 'none') {
    $syncLine = "bash $(Quoted (ToLinux (Join-Path $Ps2nRoot 'scripts\linux\sync.sh'))) --from $(Quoted (ToLinux $Ps2nRoot)) --clone $(Quoted $Clone) --mode $Sync"
    if ($Sync -eq 'tree') {
        $patch = Join-Path $staging 'tree.patch'
        git -C $Ps2nRoot -c core.safecrlf=false diff HEAD --binary --output="$patch"
        if ($LASTEXITCODE -ne 0) { throw "git diff failed." }
        $untracked = @(git -C $Ps2nRoot ls-files --others --exclude-standard)
        [IO.File]::WriteAllText((Join-Path $staging 'untracked.txt'), (($untracked | ForEach-Object { $_ + "`n" }) -join ''))
        $syncLine += " --staging $(Quoted (ToLinux $staging))"
    }
    if ($Pcsx2) { $syncLine += " --pcsx2 $(Quoted (ToLinux (Resolve-Path -LiteralPath $Pcsx2).Path))" }
    $lines += "$syncLine || { echo 'the clone was not brought to this tree: stopping'; exit 1; }"
}
$there = $Clone; if ($Clone.StartsWith('~')) { $there = '"$HOME"' + $Clone.Substring(1) }
$lines += "cd $there || exit 1"
if ($Bios) { $lines += "export PS2N_BIOS=$(Quoted (ToLinux (Resolve-Path -LiteralPath $Bios).Path))" }
$lines += "bash scripts/linux/$Script $Arguments"
$lines += 'code=$?'
$lines += 'echo "[$(date -u +%H:%M:%S)] ' + "$Script" + ' exit $code"'
$lines += 'exit $code'
$launcher = Join-Path $staging "run-$PID.sh"
[IO.File]::WriteAllText($launcher, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))

$command = "wsl.exe -d $Distribution -- bash $(ToLinux $launcher) > `"$logFile`" 2>&1"
cmd /c $command
$code = $LASTEXITCODE
Remove-Item -LiteralPath $launcher -ErrorAction SilentlyContinue
Write-Host "scripts/linux/$Script inside WSL: exit $code (log: $logFile)"
exit $code

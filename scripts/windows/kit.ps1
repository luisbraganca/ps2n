<#
.SYNOPSIS
Puts together the folder a computer with nothing else installed compiles and plays a game from
(docs\phase4-proposal.md, steps P4.3 and P4.4; docs\adr\0030, 0031).

.DESCRIPTION
The folder (build\kit unless -Out says otherwise) holds:

  runtime\     the runtime, built here once: ps2n-host.exe from a tree built with -Module on, the
               libraries it uses (PCSX2's, from deps; Microsoft's C++ runtime, from Visual Studio's
               redistributable files, ADR 0031) and PCSX2's resources; and ps2n-launcher.exe, which a
               game's folder holds under the game's name
  toolchain\   what compiles a game's modules: LLVM's own clang-cl and lld-link (22.1.3, the version
               the development builds use; fetched from LLVM's release into deps\llvm-22.1.3 when
               not there yet, its SHA-256 checked) with the compiler's headers; ps2n-recomp;
               PS2Native's headers the generated code includes; the runtime's import library
  licences\    LLVM's, PCSX2's and PS2Native's
  make-game.ps1, README.txt   the one command (scripts\windows\kit\make-game.ps1) and how to use it

Nothing in it comes from a game or a BIOS. Copied to another computer, make-game.ps1 compiles a game
there from its disc image and the BIOS, and plays it.

.EXAMPLE
scripts\windows\kit.ps1
#>
param(
    # A build tree built with -Module on: its runtime and import library go in.
    [string]$BuildDir = 'build\windows-clang-module',
    [string]$Out = 'build\kit'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
$build = Join-Path $root $BuildDir
$cache = Join-Path $build 'CMakeCache.txt'
if (-not ((Test-Path $cache) -and (Select-String -LiteralPath $cache -Pattern '^PS2N_GAME_MODULE:BOOL=ON$' -Quiet))) {
    throw "$BuildDir is not a tree built with -Module on (scripts\windows\build.ps1 -BuildDir $BuildDir -Module on)."
}
$hostExe = Join-Path $build 'ps2n\bin\ps2n-host.exe'
$launcherExe = Join-Path $build 'ps2n\bin\ps2n-launcher.exe'
$implib = Join-Path $build 'ps2n-host.lib'
$recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
foreach ($needed in $hostExe, $launcherExe, $implib, $recomp) { if (-not (Test-Path $needed)) { throw "Build first: $needed is missing." } }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$readobj = Join-Path $vs 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'

# ---- LLVM's own release (ADR 0030) -------------------------------------------------------------------
$llvmVersion = '22.1.3'
$llvmArchive = "clang+llvm-$llvmVersion-x86_64-pc-windows-msvc"
$llvmSha256 = 'f2d7d200f72c83880aa2c1c00f6b6a67d0ea607ea663dafbd0cb2db6e2819ed0'
$llvmDir = Join-Path $root "deps\llvm-$llvmVersion"
$llvm = Join-Path $llvmDir $llvmArchive
if (-not (Test-Path (Join-Path $llvm 'bin\clang-cl.exe'))) {
    New-Item -ItemType Directory -Force $llvmDir | Out-Null
    $tar = Join-Path $llvmDir "$llvmArchive.tar.xz"
    if (-not (Test-Path $tar)) {
        $url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$llvmVersion/$($llvmArchive.Replace('+', '%2B')).tar.xz"
        Write-Host "Fetching $url"
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $url -OutFile $tar -UseBasicParsing
    }
    $hash = (Get-FileHash -LiteralPath $tar -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $llvmSha256) { throw "$tar is not LLVM's release (SHA-256 $hash, not $llvmSha256)." }
    # Of the archive, what a module needs: clang-cl, lld-link and the compiler's headers.
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $tar -C $llvmDir "$llvmArchive/bin/clang-cl.exe" "$llvmArchive/bin/lld-link.exe" "$llvmArchive/lib/clang"
    if ($LASTEXITCODE -ne 0) { throw "Could not unpack $tar." }
}
$llvmLicence = Join-Path $llvmDir 'LICENSE.TXT'
if (-not (Test-Path $llvmLicence)) {
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-$llvmVersion/llvm/LICENSE.TXT" -OutFile $llvmLicence -UseBasicParsing
}

# ---- the folder ------------------------------------------------------------------------------------
$kit = Join-Path $root $Out
foreach ($part in 'runtime', 'toolchain', 'licences') {
    $path = Join-Path $kit $part
    if (Test-Path $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    New-Item -ItemType Directory -Force $path | Out-Null
}
$runtime = Join-Path $kit 'runtime'
$toolchain = Join-Path $kit 'toolchain'

# The runtime: the host, the libraries it imports from PCSX2's dependencies (and theirs), the Vulkan
# renderer's shader compiler, which PCSX2 loads while it runs, and PCSX2's resources.
Copy-Item -LiteralPath $hostExe, $launcherExe -Destination $runtime
Copy-Item -LiteralPath (Join-Path $build 'ps2n\bin\resources') -Destination $runtime -Recurse
$deps = Join-Path $root 'deps\windows-x64\deps\bin'
function Get-Imports([string]$file) {
    & $readobj --coff-imports $file | Select-String '^\s*Name: (.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value }
}
$wanted = New-Object System.Collections.Generic.Queue[string]
$wanted.Enqueue($hostExe)
$wanted.Enqueue((Join-Path $deps 'shaderc_shared.dll'))
$taken = @{}
while ($wanted.Count -gt 0) {
    $file = $wanted.Dequeue()
    foreach ($dll in (Get-Imports $file)) {
        $from = Join-Path $deps $dll
        if ($taken.ContainsKey($dll.ToLower()) -or -not (Test-Path $from)) { continue }
        $taken[$dll.ToLower()] = $true
        Copy-Item -LiteralPath $from -Destination $runtime
        $wanted.Enqueue($from)
    }
    if ($file -like "$deps*" -and -not $taken.ContainsKey((Split-Path $file -Leaf).ToLower())) {
        $taken[(Split-Path $file -Leaf).ToLower()] = $true
        Copy-Item -LiteralPath $file -Destination $runtime
    }
}
# Microsoft's C++ runtime, which those libraries and the host use, from Visual Studio's redistributable
# files: it travels in the folder (docs\adr\0031).
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Where-Object { $_.Name -match '^\d' } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
$crt = Get-ChildItem (Join-Path $crt.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $runtime
}

# The toolchain.
New-Item -ItemType Directory -Force (Join-Path $toolchain 'bin'), (Join-Path $toolchain 'lib'), (Join-Path $toolchain 'include\ps2n'),
    (Join-Path $toolchain 'include\runtime'), (Join-Path $toolchain 'src') | Out-Null
Copy-Item -LiteralPath (Join-Path $llvm 'bin\clang-cl.exe'), (Join-Path $llvm 'bin\lld-link.exe') -Destination (Join-Path $toolchain 'bin')
Copy-Item -LiteralPath (Join-Path $llvm 'lib\clang') -Destination (Join-Path $toolchain 'lib') -Recurse
Copy-Item -LiteralPath $recomp -Destination (Join-Path $toolchain 'bin')
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination (Join-Path $toolchain 'bin')
}
Copy-Item -LiteralPath $implib -Destination (Join-Path $toolchain 'lib')
Copy-Item -Path (Join-Path $root 'runtime\include\ps2n\*.h') -Destination (Join-Path $toolchain 'include\ps2n')
foreach ($header in 'r3000a.h', 'vu_step.h', 'vu_steps.inc') {
    Copy-Item -LiteralPath (Join-Path $root "core\include\ps2n\$header") -Destination (Join-Path $toolchain 'include\ps2n')
}
Copy-Item -LiteralPath (Join-Path $root 'runtime\game_module.h') -Destination (Join-Path $toolchain 'include\runtime')
Copy-Item -LiteralPath (Join-Path $root 'runtime\module_entry.cpp') -Destination (Join-Path $toolchain 'src')

# The licences, the command and how to use it.
Copy-Item -LiteralPath $llvmLicence -Destination (Join-Path $kit 'licences\LLVM-LICENSE.TXT')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $kit 'licences\PS2Native-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\pcsx2\COPYING.GPLv3') -Destination (Join-Path $kit 'licences\PCSX2-COPYING.GPLv3.txt')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'kit\make-game.ps1'), (Join-Path $PSScriptRoot 'kit\README.txt') -Destination $kit

$size = (Get-ChildItem $kit -Recurse -File | Where-Object { $_.FullName -notlike (Join-Path $kit 'games*') } | Measure-Object Length -Sum).Sum
Write-Host ("The folder is ready in {0}: {1:N0} bytes; runtime: {2}" -f $kit, $size,
    ((Get-ChildItem $runtime -File -Filter *.dll | ForEach-Object Name) -join ', '))

<#
.SYNOPSIS
Puts together the folder a computer with nothing else installed compiles and plays a game from
(docs\phase4-proposal.md, steps P4.3 and P4.4; docs\adr\0030, 0031).

.DESCRIPTION
The folder (build\kit unless -Out says otherwise) holds:

  runtime\     the runtime, built here once: ps2n-host.exe from a tree built with -Module on, the
               libraries it uses (PCSX2's, from deps; Microsoft's C++ runtime, from Visual Studio's
               redistributable files, ADR 0031) and PCSX2's resources; ps2n-launcher.exe, which a
               game's folder holds under the game's name; the wizard (PS2Native.exe at the root starts
               it) and ps2n-remake, with which a game compiles new code itself (ADR 0034), with Qt. A
               game's folder gets all of it but the wizard, and a copy of the toolchain
  toolchain\   what compiles a game's modules: LLVM's own clang-cl and lld-link (22.1.3, the version
               the development builds use; fetched from LLVM's release into deps\llvm-22.1.3 when
               not there yet, its SHA-256 checked) with the compiler's headers; ps2n-recomp;
               PS2Native's headers the generated code includes; the runtime's import library; and
               what a game's folder counts what of its code runs with (ADR 0045): LLVM's
               llvm-profdata and profile runtime, and module_counts.cpp
  licences\    the licence of everything in it, and where the source of each is (licences\README.txt;
               step P4.8): PS2Native's, PCSX2's and its dependencies' (PCSX2's own list), SDL's, LLVM's,
               Qt's (Qt-NOTICE.txt, made from the SBOMs Qt's build wrote into PCSX2's dependency package,
               with the licence texts of Qt's source, fetched into deps\qt-<version>-licences when not
               there yet, its SHA-256 checked against PCSX2's script), and where Microsoft's terms for its
               C++ runtime are
  make-game.ps1, README.txt   the one command (scripts\windows\kit\make-game.ps1) and how to use it

Nothing in it comes from a game or a BIOS, but what the folder's own use puts there: games\, the games made
with it, and patches\, PCSX2's archive of patches when the wizard fetched it. Neither goes to anyone else.
Copied to another computer, make-game.ps1 compiles a game there from its disc image and the BIOS, and plays
it.

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
# The wizard (P4.5), where the tree builds it (build.ps1 -Wizard on), and what a game compiles what its
# sessions meet with itself (P4.6, docs\adr\0034), built with it.
$wizardExe = Join-Path $build 'ps2n\bin\ps2n-wizard.exe'
$remakeExe = Join-Path $build 'ps2n\bin\ps2n-remake.exe'
$implib = Join-Path $build 'ps2n-host.lib'
$recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
foreach ($needed in $hostExe, $launcherExe, $implib, $recomp) { if (-not (Test-Path $needed)) { throw "Build first: $needed is missing." } }
# How the tree's runtime was built: a release's has link-time optimization and a profile of play (docs\adr\0047).
$runtimeLto = [bool](Select-String -LiteralPath $cache -Pattern '^PS2N_RUNTIME_LTO:BOOL=ON$' -Quiet)
$runtimePgo = "$(Select-String -LiteralPath $cache -Pattern '^PS2N_RUNTIME_PGO:[A-Z]+=(.*)$' | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Groups[1].Value })"
if ($runtimePgo -eq 'generate') { throw "$BuildDir has a runtime built to count what it runs (-RuntimePgo generate): that is for training runs, not for a folder." }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$readobj = Join-Path $vs 'VC\Tools\Llvm\x64\bin\llvm-readobj.exe'

# ---- LLVM's own release (ADR 0030) -------------------------------------------------------------------
$llvmVersion = '22.1.3'
$llvmArchive = "clang+llvm-$llvmVersion-x86_64-pc-windows-msvc"
$llvmSha256 = 'f2d7d200f72c83880aa2c1c00f6b6a67d0ea607ea663dafbd0cb2db6e2819ed0'
$llvmMajor = $llvmVersion.Split('.')[0]
$llvmDir = Join-Path $root "deps\llvm-$llvmVersion"
$llvm = Join-Path $llvmDir $llvmArchive
# Of the archive, what a module needs: clang-cl, lld-link and the compiler's headers; and what a game's folder
# counts what of its code runs with (docs\adr\0045): llvm-profdata, and LLVM's profile runtime.
$profileRuntime = "lib/clang/$llvmMajor/lib/windows/clang_rt.profile-x86_64.lib"
$llvmMembers = @('bin/clang-cl.exe', 'bin/lld-link.exe', 'bin/llvm-profdata.exe', "lib/clang/$llvmMajor/include", $profileRuntime)
$llvmMissing = @($llvmMembers | Where-Object { -not (Test-Path (Join-Path $llvm $_)) })
if ($llvmMissing) {
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
    & (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $tar -C $llvmDir @($llvmMissing | ForEach-Object { "$llvmArchive/$_" })
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
$taken = @{}
# The libraries of PCSX2's dependency package `start` imports, and theirs, copied into the runtime folder;
# their names, lower case, as the walk took them.
function Copy-Imports([string[]]$start) {
    $got = New-Object System.Collections.Generic.List[string]
    $wanted = New-Object System.Collections.Generic.Queue[string]
    foreach ($file in $start) { $wanted.Enqueue($file) }
    while ($wanted.Count -gt 0) {
        $file = $wanted.Dequeue()
        foreach ($dll in (Get-Imports $file)) {
            $from = Join-Path $deps $dll
            if ($taken.ContainsKey($dll.ToLower()) -or -not (Test-Path $from)) { continue }
            $taken[$dll.ToLower()] = $true
            $got.Add($dll.ToLower())
            Copy-Item -LiteralPath $from -Destination $runtime
            $wanted.Enqueue($from)
        }
        if ($file -like "$deps*" -and -not $taken.ContainsKey((Split-Path $file -Leaf).ToLower())) {
            $taken[(Split-Path $file -Leaf).ToLower()] = $true
            $got.Add((Split-Path $file -Leaf).ToLower())
            Copy-Item -LiteralPath $file -Destination $runtime
        }
    }
    return $got.ToArray()
}
$null = Copy-Imports @($hostExe, (Join-Path $deps 'shaderc_shared.dll'))
# Microsoft's C++ runtime, which those libraries and the host use, from Visual Studio's redistributable
# files: it travels in the folder (docs\adr\0031).
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Where-Object { $_.Name -match '^\d' } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
$crt = Get-ChildItem (Join-Path $crt.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $runtime
}

# The wizard beside the runtime, whose libraries and resources it shares, with Qt's own (from PCSX2's
# dependency package): its libraries, and the plugins it loads, under qt\plugins (qt.conf says where); and
# ps2n-remake, with which a game compiles what its sessions meet for the first time itself (docs\adr\0034).
# A game's folder has all of it but the wizard (scripts\windows\kit\make-game.ps1, wizard/package.cpp), and
# a copy of the toolchain. PS2Native.exe in the folder's root, the launcher, starts the wizard
# (host/launcher.cpp).
Remove-Item -LiteralPath (Join-Path $kit 'PS2Native.exe') -ErrorAction SilentlyContinue
if (Test-Path $wizardExe) {
    if (-not (Test-Path $remakeExe)) { throw "Build first: $remakeExe is missing (build.ps1 -BuildDir $BuildDir)." }
    Copy-Item -LiteralPath $wizardExe, $remakeExe -Destination $runtime
    $plugins = Join-Path $root 'deps\windows-x64\deps\plugins'
    $pluginFiles = @('platforms\qwindows.dll', 'styles\qmodernwindowsstyle.dll', 'imageformats\qsvg.dll', 'imageformats\qico.dll',
        'iconengines\qsvgicon.dll')
    foreach ($plugin in $pluginFiles) {
        $to = Join-Path $runtime "qt\plugins\$(Split-Path $plugin)"
        New-Item -ItemType Directory -Force $to | Out-Null
        Copy-Item -LiteralPath (Join-Path $plugins $plugin) -Destination $to
    }
    # The libraries the wizard, ps2n-remake and the plugins import that the runtime does not: Qt's, and theirs.
    $null = Copy-Imports (@($wizardExe, $remakeExe) + @($pluginFiles | ForEach-Object { Join-Path $plugins $_ }))
    [IO.File]::WriteAllLines((Join-Path $runtime 'qt.conf'), [string[]]@('[Paths]', 'Plugins = qt/plugins'))
    # What of the runtime folder only the wizard needs, for a game's folder to leave out (wizard/package.cpp,
    # make-game.ps1): one path to a line, from the runtime folder.
    [IO.File]::WriteAllLines((Join-Path $runtime 'wizard-files.txt'), [string[]]@('wizard-files.txt', 'ps2n-wizard.exe'))
    Copy-Item -LiteralPath $launcherExe -Destination (Join-Path $kit 'PS2Native.exe')
}
else {
    Write-Host "No wizard in $BuildDir (build.ps1 -BuildDir $BuildDir -Wizard on): the folder has make-game.ps1 alone."
}

# The toolchain.
New-Item -ItemType Directory -Force (Join-Path $toolchain 'bin'), (Join-Path $toolchain 'lib'), (Join-Path $toolchain 'include\ps2n'),
    (Join-Path $toolchain 'include\runtime'), (Join-Path $toolchain 'src') | Out-Null
Copy-Item -LiteralPath (Join-Path $llvm 'bin\clang-cl.exe'), (Join-Path $llvm 'bin\lld-link.exe'), (Join-Path $llvm 'bin\llvm-profdata.exe') -Destination (Join-Path $toolchain 'bin')
New-Item -ItemType Directory -Force (Join-Path $toolchain "lib\clang\$llvmMajor") | Out-Null
Copy-Item -LiteralPath (Join-Path $llvm "lib\clang\$llvmMajor\include") -Destination (Join-Path $toolchain "lib\clang\$llvmMajor") -Recurse
Copy-Item -LiteralPath (Join-Path $llvm $profileRuntime) -Destination (Join-Path $toolchain 'lib')
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
Copy-Item -LiteralPath (Join-Path $root 'runtime\module_entry.cpp'), (Join-Path $root 'runtime\module_counts.cpp') -Destination (Join-Path $toolchain 'src')

# ---- the licences (step P4.8, docs\adr\0033) --------------------------------------------------------
# Of everything the folder carries: PS2Native's and PCSX2's (GPL-3.0); the libraries of PCSX2's dependency
# package and what PCSX2 builds in, as PCSX2's own list has them (ThirdPartyLicenses.html); SDL's, which
# that list leaves out; LLVM's; Qt's, made from Qt's own record of what each of its libraries and plugins
# holds (the SBOMs Qt's build wrote into PCSX2's dependency package) with the licence texts of Qt's source;
# and where Microsoft's C++ runtime's terms are. licences\README.txt says which is which, and where the
# source of each is.
$licences = Join-Path $kit 'licences'
$utf8 = New-Object System.Text.UTF8Encoding($false)
Copy-Item -LiteralPath $llvmLicence -Destination (Join-Path $licences 'LLVM-LICENSE.TXT')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $licences 'PS2Native-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\pcsx2\COPYING.GPLv3') -Destination (Join-Path $licences 'PCSX2-COPYING.GPLv3.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\pcsx2\bin\docs\ThirdPartyLicenses.html') -Destination (Join-Path $licences 'PCSX2-ThirdPartyLicenses.html')
Copy-Item -LiteralPath (Join-Path $root 'deps\windows-x64\deps\licenses\SDL3\LICENSE.txt') -Destination (Join-Path $licences 'SDL3-LICENSE.txt')

# Qt: its release and its source, as PCSX2's script for its dependency package names them.
$depsScript = [IO.File]::ReadAllText((Join-Path $root 'third_party\pcsx2\.github\workflows\scripts\windows\build-dependencies.bat'))
if ($depsScript -notmatch '(?m)^set QT=(\S+)') { throw "PCSX2's dependency script names no Qt release." }
$qtVersion = $Matches[1]
if ($depsScript -notmatch '(?m)^set QTMINOR=(\S+)') { throw "PCSX2's dependency script names no Qt release." }
$qtMinor = $Matches[1]
if ($depsScript -notmatch '"qtbase-everywhere-src-%QT%\.zip" "(\S+)" ([0-9a-f]{64})') { throw "PCSX2's dependency script names no qtbase source." }
$qtbaseUrl = $Matches[1].Replace('%QTMINOR%', $qtMinor).Replace('%QT%', $qtVersion)
$qtbaseSha256 = $Matches[2]
if ($depsScript -notmatch '"qtsvg-everywhere-src-%QT%\.zip" "(\S+)"') { throw "PCSX2's dependency script names no qtsvg source." }
$qtsvgUrl = $Matches[1].Replace('%QTMINOR%', $qtMinor).Replace('%QT%', $qtVersion)
$sbomDir = Join-Path $root 'deps\windows-x64\deps\sbom'
foreach ($module in 'qtbase', 'qtsvg') {
    if (-not (Test-Path (Join-Path $sbomDir "$module-$qtVersion.spdx"))) { throw "PCSX2's dependency package has no SBOM of Qt $qtVersion's $module in $sbomDir." }
}
$qtSourceDir = Join-Path $root "deps\qt-$qtVersion-licences"
$qtbaseZip = Join-Path $qtSourceDir "qtbase-everywhere-src-$qtVersion.zip"
if (-not (Test-Path $qtbaseZip)) {
    New-Item -ItemType Directory -Force $qtSourceDir | Out-Null
    Write-Host "Fetching $qtbaseUrl"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $qtbaseUrl -OutFile $qtbaseZip -UseBasicParsing
}
$hash = (Get-FileHash -LiteralPath $qtbaseZip -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $qtbaseSha256) { throw "$qtbaseZip is not Qt's source (SHA-256 $hash, not $qtbaseSha256)." }

# The packages of an SPDX document (tag and value), each a table of its fields; a <text> value that
# runs over several lines is put together.
function Read-Spdx([string]$path) {
    $packages = New-Object System.Collections.Generic.List[object]
    $current = $null
    $lines = [IO.File]::ReadAllLines($path)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^([A-Za-z]+):\s?(.*)$') { continue }
        $key = $Matches[1]; $value = $Matches[2]
        if ($value.StartsWith('<text>')) {
            $parts = @($value.Substring(6))
            while (-not $parts[-1].Contains('</text>') -and $i + 1 -lt $lines.Count) { $i++; $parts += $lines[$i] }
            $value = ($parts -join "`n")
            $value = $value.Substring(0, $value.IndexOf('</text>'))
        }
        if ($key -eq 'PackageName') { $current = @{ PackageName = $value }; $packages.Add($current) }
        elseif ($key -eq 'LicenseID') { $current = $null }
        elseif ($current -and -not $current.ContainsKey($key)) { $current[$key] = $value }
    }
    return $packages
}
# Qt's files in the folder, and what Qt's build calls each: the module or plugin, and for Qt Core the
# PCRE2 it holds.
$qtFiles = [ordered]@{
    'Qt6Core.dll' = @('qtbase', 'Core', 'BundledPcre2'); 'Qt6Gui.dll' = @('qtbase', 'Gui'); 'Qt6Widgets.dll' = @('qtbase', 'Widgets')
    'Qt6Svg.dll' = @('qtsvg', 'Svg'); 'qt\plugins\platforms\qwindows.dll' = @('qtbase', 'QWindowsIntegrationPlugin')
    'qt\plugins\styles\qmodernwindowsstyle.dll' = @('qtbase', 'QModernWindowsStylePlugin')
    'qt\plugins\imageformats\qico.dll' = @('qtbase', 'QICOPlugin'); 'qt\plugins\imageformats\qsvg.dll' = @('qtsvg', 'QSvgPlugin')
    'qt\plugins\iconengines\qsvgicon.dll' = @('qtsvg', 'QSvgIconPlugin')
}
foreach ($file in (Get-ChildItem $runtime -Recurse -File -Include 'Qt6*.dll', 'q*.dll')) {
    $relative = $file.FullName.Substring($runtime.Length + 1)
    if (-not $qtFiles.Contains($relative)) { throw "Qt's $relative is in the runtime, but not in the licences' list of Qt's files (kit.ps1, `$qtFiles)." }
}
$present = @($qtFiles.Keys | Where-Object { Test-Path (Join-Path $runtime $_) })
if ($present) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $sbom = @{ qtbase = (Read-Spdx (Join-Path $sbomDir "qtbase-$qtVersion.spdx")); qtsvg = (Read-Spdx (Join-Path $sbomDir "qtsvg-$qtVersion.spdx")) }
    $zip = [IO.Compression.ZipFile]::OpenRead($qtbaseZip)
    $sourceRoot = "qtbase-everywhere-src-$qtVersion"
    function Read-QtSource([string]$path) {
        $parts = New-Object System.Collections.Generic.List[string]
        foreach ($p in ("$sourceRoot/$path" -split '/')) {
            if ($p -eq '..') { $parts.RemoveAt($parts.Count - 1) } elseif ($p -and $p -ne '.') { $parts.Add($p) }
        }
        $entry = $zip.GetEntry($parts -join '/')
        if (-not $entry) { throw "$path is not in Qt's source ($qtbaseZip)." }
        $reader = New-Object IO.StreamReader($entry.Open())
        try { return $reader.ReadToEnd().TrimEnd() } finally { $reader.Dispose() }
    }
    $rule = '-' * 78
    $generic = [ordered]@{ 'LGPL-3.0-only' = 1; 'GPL-3.0-only' = 1 }
    $text = New-Object System.Collections.Generic.List[string]
    $text.Add("Qt, in this folder and in every game's folder made with it")
    $text.Add('=' * 58)
    $text.Add('')
    $text.Add("Qt $qtVersion (https://www.qt.io), in runtime\: $(($present | ForEach-Object { $_ }) -join ', ').")
    $text.Add('')
    $text.Add("Qt is The Qt Company's and its contributors', offered under the GNU Lesser General Public License, version 3")
    $text.Add('(LGPL-3.0-only), the GNU General Public License, version 2 or 3, or a commercial licence. PS2Native carries it')
    $text.Add('under the LGPL, version 3, whose text is at the end of this file with the GPL, version 3, which it builds on.')
    $text.Add('Each of these libraries is a file of its own, which another build of the same Qt can replace.')
    $text.Add('')
    $text.Add("It is Qt $qtVersion as its source has it, unchanged: $qtbaseUrl and")
    $text.Add("$qtsvgUrl, built for PCSX2's dependency package by PCSX2's script, which says how")
    $text.Add("(.github/workflows/scripts/windows/build-dependencies.bat in PCSX2's source; see README.txt beside this).")
    $text.Add('')
    $text.Add("Below, from Qt's own record of what it built (its SBOMs), each of those files with the code of others")
    $text.Add("built into it, and that code's licence and copyright: its own licence file from Qt's source where it has one,")
    $text.Add('and otherwise the licence it names, whose text is at the end of this file.')
    foreach ($file in $present) {
        $doc, $targets = $qtFiles[$file][0], @($qtFiles[$file] | Select-Object -Skip 1)
        $module = $sbom[$doc] | Where-Object { $_.PackageName -eq $targets[0] } | Select-Object -First 1
        $text.Add('')
        $text.Add($rule)
        $text.Add("$file`: Qt's $($targets[0]) $($module.PackageVersion)")
        $text.Add($module.PackageCopyrightText)
        $text.Add('Licence: LGPL-3.0-only (at the end of this file)')
        $parts = @($sbom[$doc] | Where-Object { $name = $_.PackageName; $targets | Where-Object { $name -eq $_ -or $name -like "$($_)_Attribution_*" } })
        $parts = @($parts | Where-Object { $_.PackageName -ne $targets[0] })
        if ($parts) { $text.Add(''); $text.Add('Built into it:') }
        foreach ($part in $parts) {
            $comment = [string]$part.PackageComment
            $name = if ($comment -match '(?m)^\s*Name: (.+)$') { $Matches[1].Trim() } else { $part.PackageName }
            $text.Add('')
            $text.Add("* $name $(if ($part.PackageVersion -and $part.PackageVersion -ne 'unknown') { $part.PackageVersion })".TrimEnd())
            if ($part.PackageDownloadLocation -and $part.PackageDownloadLocation -ne 'NOASSERTION') { $text.Add("  From: $($part.PackageDownloadLocation)") }
            $text.Add("  Licence: $($part.PackageLicenseConcluded)")
            if ($part.PackageCopyrightText -and $part.PackageCopyrightText -ne 'NOASSERTION') {
                foreach ($line in ($part.PackageCopyrightText -split "`n")) { $text.Add("  $($line.Trim())") }
            }
            $licenceFiles = @([regex]::Matches($comment, '(?m)^\s*License file: (.+)$') | ForEach-Object { $_.Groups[1].Value.Trim() })
            $where = if ($comment -match '(?m)^\s*/src_dir/qtbase/(.+)/qt_attribution\.json\s*$') { $Matches[1] } else { '' }
            $ownFile = [bool]($licenceFiles -and $where)
            foreach ($licenceFile in @(if ($ownFile) { $licenceFiles })) {
                $text.Add('')
                $text.Add("  Its licence ($where/$licenceFile in Qt's source):")
                $text.Add('')
                foreach ($line in ((Read-QtSource "$where/$licenceFile") -split "\r?\n")) { $text.Add("    $line".TrimEnd()) }
            }
            # Every licence it names, at the end too; one it has no file of its own for must be there.
            foreach ($id in ($part.PackageLicenseConcluded -split '\s+(?:OR|AND|WITH)\s+|[()]')) {
                $id = $id.Trim()
                if (-not $id -or $id -eq 'NOASSERTION') { continue }
                $generic[$id] = [bool]$generic[$id] -or -not $ownFile
            }
        }
    }
    $text.Add('')
    $text.Add($rule)
    $text.Add('The licences named above, as Qt''s source has them (its LICENSES folder)')
    foreach ($id in $generic.Keys) {
        $entry = $zip.GetEntry("$sourceRoot/LICENSES/$id.txt")
        if (-not $entry) {
            if ($generic[$id]) { throw "Qt's source has no text of the licence $id, which a part of Qt names without a file of its own." }
            continue
        }
        $text.Add('')
        $text.Add($rule)
        $text.Add("$id")
        $text.Add('')
        foreach ($line in ((Read-QtSource "LICENSES/$id.txt") -split "\r?\n")) { $text.Add($line.TrimEnd()) }
    }
    $zip.Dispose()
    [IO.File]::WriteAllLines((Join-Path $licences 'Qt-NOTICE.txt'), [string[]]$text, $utf8)
}

# Microsoft's C++ runtime: where its terms are, as Visual Studio's own Redist.txt points there.
$redistNote = Get-Content -LiteralPath (Join-Path $vs 'Licenses\1033\Redist.txt') -ErrorAction SilentlyContinue
$redistUrl = if ("$redistNote" -match '(https://\S+?)\.?(\s|$)') { $Matches[1] } else { 'https://visualstudio.microsoft.com/license-terms/' }
$crtVersion = (Get-Item (Join-Path $crt.FullName 'msvcp140.dll')).VersionInfo.FileVersion
[IO.File]::WriteAllLines((Join-Path $licences 'Microsoft-CPP-runtime.txt'), [string[]]@(
        "msvcp140.dll, vcruntime140.dll and vcruntime140_1.dll (in runtime\, in toolchain\bin, and in every game's",
        "folder) are Microsoft's Visual C++ runtime, version $crtVersion, taken from Visual Studio's redistributable files",
        "($(Split-Path (Split-Path $crt.FullName) -Parent | Split-Path -Leaf)), which Visual Studio's licence terms allow to be distributed with a program",
        '("Distributable Code"). Microsoft''s terms for them, as Visual Studio''s own Redist.txt names where they are:',
        "$redistUrl"), $utf8)

# Which is which, and where the source of each is. (What git says on stderr does not stop this.)
$ErrorActionPreference = 'Continue'
$ps2nVersion = (Get-Content -LiteralPath (Join-Path $root 'version.txt') -TotalCount 1).Trim()
$ps2nRepo = ("$(& git -C $root remote get-url origin 2>&1)" -replace '\.git$', '').Trim()
$ps2nCommit = "$(& git -C $root rev-parse HEAD 2>&1)".Trim()
$ps2nChanged = [bool]("$(& git -C $root status --porcelain --untracked-files=no 2>&1)".Trim())
$pcsx2Tag = "$(& git -C (Join-Path $root 'third_party\pcsx2') describe --tags --exact-match 2>&1)".Trim()
$pcsx2Commit = "$(& git -C (Join-Path $root 'third_party\pcsx2') rev-parse HEAD 2>&1)".Trim()
$ErrorActionPreference = 'Stop'
if ($ps2nChanged) { Write-Host "Warning: the tree has changes not committed; licences\README.txt says so, and the folder is not for others." }
$changedNote = if ($ps2nChanged) { ', with changes not committed when this folder was made' } else { '' }
# (Each line one string: in an array, a comma binds before a +.)
[IO.File]::WriteAllLines((Join-Path $licences 'README.txt'), [string[]]@(
        'The licences of what this folder holds',
        '======================================',
        '',
        "PS2Native $ps2nVersion`: PS2Native-LICENSE.txt (GNU General Public License, version 3). Its source: $ps2nRepo,",
        "commit $ps2nCommit$changedNote.",
        '',
        'PCSX2, whose core the runtime is built on: PCSX2-COPYING.GPLv3.txt (GNU General Public License, version 3).',
        "Its source: https://github.com/PCSX2/pcsx2, $pcsx2Tag (commit $pcsx2Commit).",
        '',
        'The libraries in runtime\ that come from PCSX2''s dependency package, and what PCSX2 builds into its core:',
        'PCSX2-ThirdPartyLicenses.html, PCSX2''s own list of them with their licences. The package is built by PCSX2''s',
        'script .github/workflows/scripts/windows/build-dependencies.bat, in PCSX2''s source above, from the sources it',
        'names. SDL 3, which that list leaves out: SDL3-LICENSE.txt.',
        '',
        "Qt $qtVersion (runtime\Qt6*.dll and runtime\qt): Qt-NOTICE.txt, made from Qt's own record of what each file holds,",
        'with every licence text it names, Qt''s own LGPL, version 3, among them. Its source: Qt''s release, as Qt-NOTICE.txt says.',
        '',
        "LLVM's clang-cl, lld-link and llvm-profdata, the compiler's headers and its profile runtime (toolchain\):",
        "LLVM-LICENSE.TXT (Apache License 2.0 with LLVM exceptions). Their source: https://github.com/llvm/llvm-project,",
        "llvmorg-$llvmVersion.",
        '',
        "Microsoft's C++ runtime: Microsoft-CPP-runtime.txt says where Microsoft's terms for it are."), $utf8)

# The command and how to use it.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'kit\make-game.ps1'), (Join-Path $PSScriptRoot 'kit\README.txt') -Destination $kit

$size = (Get-ChildItem $kit -Recurse -File | Where-Object { $_.FullName -notlike (Join-Path $kit 'games*') } | Measure-Object Length -Sum).Sum
Write-Host ("The folder is ready in {0}: {1:N0} bytes; runtime: {2}" -f $kit, $size,
    ((Get-ChildItem $runtime -File -Filter *.dll | ForEach-Object Name) -join ', '))
Write-Host ("Its runtime is built {0} link-time optimization and {1} a profile of play." -f $(if ($runtimeLto) { 'with' } else { 'without' }),
    $(if ($runtimePgo -eq 'use') { 'with' } else { 'without' }))
if (-not ($runtimeLto -and $runtimePgo -eq 'use')) {
    Write-Host "Warning: a release's runtime is built with both (docs\adr\0047: scripts\windows\runtime-profile.ps1, then build.ps1 -RuntimeLto on -RuntimePgo use). Games play slower with this one."
}

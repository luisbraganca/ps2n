<#
.SYNOPSIS
Compiles a PS2 game into a folder of its own and plays it: PS2Native on a computer with nothing else
installed (docs\phase4-proposal.md, step P4.3).

.DESCRIPTION
Part of the folder scripts\windows\kit.ps1 puts together; run it from there. From the disc image and the
BIOS image, both the user's own and read where they are, it writes the game's compiled code with the
toolchain beside it (LLVM's clang and lld, PS2Native's headers and recompiler): the game's executables
taken from the disc image, translated to C++ (ps2n-recomp), compiled and linked into four modules (the
game's EE code, the BIOS's, the micro code, the IOP's code), which go into games\<name> with a copy of the
runtime. With -Play it then opens the game in a window: Vulkan, three times the console's resolution,
the keyboard and the first controller (F11 or Alt+Enter covers the screen), until the window is closed.

Everything it writes is in games\<name>: the modules and the runtime, the game's memory cards and the
console's settings (data), every session played written down to be run again (sessions), what sessions
had to interpret (captures: what a session met that no compile had seen is added there when its window
closes, and compiled the next time this runs), and the generated C++ and objects (work), which can be
deleted. Nothing is installed and nothing is written elsewhere, but the console's settings beside a
BIOS image that cannot be linked into the folder.

.EXAMPLE
.\make-game.ps1 -Image 'D:\games\Agent Under Fire.iso' -Bios 'D:\bios\SCPH-70004.BIN' -More MOVIE.ELF,ACTION.UK,DRIVING.ELF -Captures .\captures -Play
#>
param(
    # The game's disc image (an .iso), read where it is.
    [Parameter(Mandatory = $true)][string]$Image,
    # The BIOS image, read where it is.
    [Parameter(Mandatory = $true)][string]$Bios,
    # The executables on the disc the game loads itself, not through the BIOS (a game made of several
    # executables, as Agent Under Fire is): their names on the disc, separated by commas.
    [string]$More = '',
    # A folder with what earlier runs had to interpret, from elsewhere (the development tree, another
    # computer): capture.toml (the game's EE code), vu-capture.toml (micro code), iop-capture.toml (the
    # IOP's code), bios-capture.toml (the BIOS's code). Each that is there is added to the game's own
    # (games\<name>\captures), all of which is compiled.
    [string]$Captures = '',
    # The game's folder under games: by default named after the executable the disc boots.
    [string]$Name = '',
    # Open the game in a window once it is compiled.
    [switch]$Play,
    # Only open the game in a window, compiled as it was last time.
    [switch]$PlayOnly,
    # How many files to compile at once: by default as many as the computer has processors.
    [int]$Jobs = 0,
    # The renderer the game is played on: vulkan by default; dx11, dx12, opengl, sw (PCSX2's software
    # renderer) or auto (the one PCSX2 chooses for this computer) where Vulkan does not work.
    [ValidateSet('', 'vulkan', 'dx11', 'dx12', 'opengl', 'sw', 'auto')][string]$Renderer = '',
    # More options for the runtime when it plays (ps2n-host -help has them): '-mute', '-window-size',
    # '1280x720', '-frames', '3000' and the like.
    [string[]]$HostOptions = @(),
    # PCSX2's archive of game patches (patches.zip, in an installed PCSX2's resources folder or in PCSX2's
    # own release), for the game's widescreen and frame-rate patches (ADR 0025): an installed PCSX2's is
    # taken by itself; this gives one where PCSX2 is not installed. PS2Native ships none (ADR 0028).
    # Without one the game plays as its disc has it, 4:3.
    [string]$PatchArchive = ''
)

$ErrorActionPreference = 'Stop'
$kit = $PSScriptRoot
$toolchain = Join-Path $kit 'toolchain'
$runtime = Join-Path $kit 'runtime'
$recomp = Join-Path $toolchain 'bin\ps2n-recomp.exe'
$clang = Join-Path $toolchain 'bin\clang-cl.exe'
$lld = Join-Path $toolchain 'bin\lld-link.exe'
foreach ($needed in $recomp, $clang, $lld, (Join-Path $runtime 'ps2n-host.exe')) {
    if (-not (Test-Path -LiteralPath $needed)) { throw "This folder is not whole: $needed is missing." }
}
$imagePath = (Resolve-Path -LiteralPath $Image).Path
$biosPath = (Resolve-Path -LiteralPath $Bios).Path
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
$started = Get-Date

function Say([string]$text) {
    Write-Host ("{0,6:N1} s  {1}" -f ((Get-Date) - $started).TotalSeconds, $text)
}

# A program of the toolchain, its output shown only when it fails.
function Run([string]$exe, [string[]]$arguments) {
    $ErrorActionPreference = 'Continue'
    $output = & $exe @arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) {
        $output | ForEach-Object { Write-Host "    $_" }
        throw "$(Split-Path $exe -Leaf) failed (exit $code)"
    }
    return $output
}

# ---- the game's executables, from the disc image ----------------------------------------------
$probe = Join-Path ([IO.Path]::GetTempPath()) ("ps2n-" + [Guid]::NewGuid().ToString('N'))
$bootLine = Run $recomp @('extract', $imagePath, '--out-dir', $probe) | Select-Object -First 1
$boot = ($bootLine -split ' ', 2)[0]
Remove-Item -LiteralPath $probe -Recurse -Force -ErrorAction SilentlyContinue
if (-not $Name) { $Name = $boot -replace '[^A-Za-z0-9._-]', '_' }
$game = Join-Path $kit "games\$Name"
$work = Join-Path $game 'work'
$disc = Join-Path $work 'disc'
$gen = Join-Path $work 'generated'
$obj = Join-Path $work 'objects'
$own = Join-Path $game 'captures'

# What runs had to interpret, put into the game's own captures (ps2n-recomp merges each kind, every
# block or address once).
$captureKinds = [ordered]@{ 'capture' = 'capture'; 'vu-capture' = 'vu-capture'; 'iop-capture' = 'iop-capture'; 'bios-capture' = 'capture' }
function Add-Captures([string]$from) {
    New-Item -ItemType Directory -Force $own | Out-Null
    foreach ($kind in $captureKinds.Keys) {
        $new = Join-Path $from "$kind.toml"
        if (-not (Test-Path -LiteralPath $new)) { continue }
        $mine = Join-Path $own "$kind.toml"
        if (Test-Path -LiteralPath $mine) { Run $recomp @($captureKinds[$kind], '--out', $mine, $mine, $new) | Out-Null }
        else { Copy-Item -LiteralPath $new -Destination $mine }
    }
}

if (-not $PlayOnly) {
    Say "the game boots $boot; its folder: $game"
    New-Item -ItemType Directory -Force $disc, $gen, $obj | Out-Null
    $moreNames = @($More -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    Run $recomp (@('extract', $imagePath, $boot) + $moreNames + @('--out-dir', $disc)) | Out-Null
    $bootElf = Join-Path $disc (Split-Path $boot -Leaf)
    Say "taken from the disc image: $(@($boot) + $moreNames -join ', ')"

    # ---- the C++ ---------------------------------------------------------------------------------
    if ($Captures) { Add-Captures (Resolve-Path -LiteralPath $Captures).Path }
    $capture = @{}
    foreach ($kind in $captureKinds.Keys) {
        $file = Join-Path $own "$kind.toml"
        $capture[$kind] = if (Test-Path -LiteralPath $file) { $file } else { '' }
    }
    Say "compiling what runs had to interpret too: $(@($captureKinds.Keys | Where-Object { $capture[$_] }) -join ', ')"
    # As the development build does (tests\tests.cmake): the boot executable in 64 files, an executable
    # it loads itself in one file for each 64 KB of it, at most 64, the micro code and IOP code in 16.
    $programIds = @('game')
    $arguments = @('compile', $bootElf, '--id', 'game', '--out', (Join-Path $gen 'game.cpp'), '--parts', '64')
    if ($capture['capture']) {
        $arguments += @('--capture', $capture['capture'], '--outside', (Join-Path $gen 'game_outside.cpp'))
        $programIds += 'game_outside'
    }
    Run $recomp $arguments | Out-Null
    foreach ($m in $moreNames) {
        $file = Join-Path $disc (Split-Path $m -Leaf)
        $id = 'game_loads_' + ((Split-Path $m -Leaf) -replace '[^A-Za-z0-9]', '_').ToLower()
        $parts = [Math]::Min(64, [int][Math]::Floor((Get-Item -LiteralPath $file).Length / 65536) + 1)
        $arguments = @('compile', $file, '--id', $id, '--out', (Join-Path $gen "$id.cpp"), '--loaded-by', $bootElf)
        if ($parts -gt 1) { $arguments += @('--parts', "$parts") }
        if ($capture['capture']) { $arguments += @('--capture', $capture['capture']) }
        Run $recomp $arguments | Out-Null
        $programIds += $id
    }
    $arguments = @('bios', $biosPath, '--out-dir', (Join-Path $gen 'bios'))
    if ($capture['bios-capture']) { $arguments += @('--capture', $capture['bios-capture']) }
    Run $recomp $arguments | Out-Null
    $vuList = @()
    if ($capture['vu-capture']) {
        Run $recomp @('vu', $capture['vu-capture'], '--id', 'game', '--out', (Join-Path $gen 'vu\vu_game.cpp'), '--parts', '16') | Out-Null
        $vuList = @('--vu', 'game')
    }
    $iopList = @()
    if ($capture['iop-capture']) {
        Run $recomp @('iop', $capture['iop-capture'], '--id', 'game', '--out', (Join-Path $gen 'iop\iop_game.cpp'), '--parts', '16') | Out-Null
        $iopList = @('--iop', 'game')
    }
    $lists = @{
        game = $programIds
        bios = @('bios_rom0', 'bios_rom1', 'bios_kernel', 'bios_loader')
        vu = $vuList
        iop = $iopList
    }
    foreach ($kind in 'game', 'bios', 'vu', 'iop') {
        Run $recomp (@('registry', '--out', (Join-Path $gen "registry_$kind.cpp")) + $lists[$kind]) | Out-Null
    }
    Say 'translated to C++'

    # ---- compiled ----------------------------------------------------------------------------------
    # What each module is made of: the files the recompiler wrote for it, its list and its entry.
    $sources = @{
        game = @(Get-ChildItem -LiteralPath $gen -File -Filter 'game*.cpp' | ForEach-Object { $_.FullName })
        bios = @(Get-ChildItem -LiteralPath (Join-Path $gen 'bios') -File -Filter '*.cpp' | ForEach-Object { $_.FullName })
        vu = @(Get-ChildItem -LiteralPath (Join-Path $gen 'vu') -File -Filter '*.cpp' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        iop = @(Get-ChildItem -LiteralPath (Join-Path $gen 'iop') -File -Filter '*.cpp' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    }
    # The flags of the development build's modules (tests\tests.cmake, ps2n_compile_freestanding): no
    # system header, nothing but PS2Native's headers and the compiler's own.
    $flags = @('/nologo', '/c', '/X', '/clang:-ffreestanding', '/Zl', '/GS-', '/O2', '/Ob2', '-std:c++20', '-msse4.1',
        '/DNDEBUG', '/DPS2N_MODULE', "-I$(Join-Path $toolchain 'include')", '-Werror=global-constructors', '-Werror=exit-time-destructors')
    $todo = New-Object System.Collections.Generic.List[object]
    foreach ($kind in 'game', 'bios', 'vu', 'iop') {
        New-Item -ItemType Directory -Force (Join-Path $obj $kind) | Out-Null
        foreach ($source in @($sources[$kind]) + @((Join-Path $gen "registry_$kind.cpp"), (Join-Path $toolchain 'src\module_entry.cpp'))) {
            $todo.Add([pscustomobject]@{ Kind = $kind; Source = $source; Object = (Join-Path $obj "$kind\$([IO.Path]::GetFileNameWithoutExtension($source)).obj") })
        }
    }
    $running = New-Object System.Collections.Generic.List[object]
    $next = 0
    $failed = @()
    while ($next -lt $todo.Count -or $running.Count -gt 0) {
        while ($running.Count -lt $Jobs -and $next -lt $todo.Count) {
            $t = $todo[$next]; $next++
            $log = $t.Object + '.log'
            $quoted = @($flags + @("/Fo$($t.Object)", $t.Source)) | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
            $p = Start-Process -FilePath $clang -ArgumentList $quoted -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError ($log + '.err')
            # Taken while it runs: without it Windows PowerShell 5.1 may not have the exit code afterwards.
            $null = $p.Handle
            $running.Add([pscustomobject]@{ Process = $p; Task = $t; Log = $log })
        }
        Start-Sleep -Milliseconds 100
        # (A copy, since finished ones are taken out; @() of the list fails in Windows PowerShell 5.1.)
        foreach ($r in $running.ToArray()) {
            if ($r.Process.HasExited) {
                $r.Process.WaitForExit()
                if ($r.Process.ExitCode -ne 0) { $failed += $r }
                [void]$running.Remove($r)
            }
        }
    }
    if ($failed) {
        foreach ($f in $failed) {
            Write-Host "  $($f.Task.Source):"
            Get-Content -LiteralPath $f.Log, ($f.Log + '.err') -ErrorAction SilentlyContinue | Select-Object -First 20 | ForEach-Object { Write-Host "    $_" }
        }
        throw "$($failed.Count) of $($todo.Count) files did not compile"
    }
    Say "compiled: $($todo.Count) files, $Jobs at a time"

    # ---- linked ------------------------------------------------------------------------------------
    New-Item -ItemType Directory -Force $game | Out-Null
    foreach ($kind in 'game', 'bios', 'vu', 'iop') {
        $objects = @($todo | Where-Object { $_.Kind -eq $kind } | ForEach-Object { $_.Object })
        $rsp = Join-Path $obj "$kind.rsp"
        [IO.File]::WriteAllLines($rsp, [string[]]($objects | ForEach-Object { '"' + $_ + '"' }))
        Run $lld @('/nologo', '/DLL', '/NOENTRY', '/NODEFAULTLIB', '/MACHINE:X64', '/OPT:REF', '/OPT:ICF',
            "/OUT:$(Join-Path $game "ps2n-$kind.dll")", "@$rsp", (Join-Path $toolchain 'lib\ps2n-host.lib')) | Out-Null
    }
    Say "linked: $((Get-ChildItem -LiteralPath $game -Filter 'ps2n-*.dll' | ForEach-Object { '{0} ({1:N0} bytes)' -f $_.Name, $_.Length }) -join ', ')"

    # ---- the runtime beside them -------------------------------------------------------------------
    Copy-Item -Path (Join-Path $runtime '*') -Destination $game -Recurse -Force
    Say "the runtime copied beside them; the game is ready in $game"
}

if (-not ($Play -or $PlayOnly)) { exit 0 }

# ---- played -----------------------------------------------------------------------------------------
# As scripts\windows\game.ps1 -Play: the BIOS image linked into the game's folder, so that the console's
# settings are kept there and not beside the image (or read where it is, when it cannot be linked); the
# session written down to be run again; what it had to interpret written down to be compiled in.
$hostExe = Join-Path $game 'ps2n-host.exe'
if (-not (Test-Path -LiteralPath $hostExe)) { throw "The game has not been compiled yet: run this without -PlayOnly first." }
$biosDir = Join-Path $game 'bios'
New-Item -ItemType Directory -Force $biosDir | Out-Null
$biosFile = Get-Item -LiteralPath $biosPath
$biosUsed = Join-Path $biosDir $biosFile.Name
# With the image, the ROM1, ROM2 and EROM images the core loads from beside it.
$biosFiles = @($biosFile) + @(Get-ChildItem -LiteralPath $biosFile.DirectoryName -File | Where-Object {
        $_.Extension -match '^\.(rom1|rom2|erom)$' -and ($_.BaseName -eq $biosFile.BaseName -or $_.BaseName -eq $biosFile.Name) })
foreach ($file in $biosFiles) {
    $link = Join-Path $biosDir $file.Name
    if (-not (Test-Path -LiteralPath $link)) {
        try { New-Item -ItemType HardLink -Path $link -Target $file.FullName | Out-Null }
        catch { if ($file.FullName -eq $biosFile.FullName) { $biosUsed = $biosFile.FullName } }
    }
}
$record = Join-Path $game ("sessions\" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $record | Out-Null
foreach ($card in 'Mcd001.ps2', 'Mcd002.ps2') {
    $cardPath = Join-Path $game "data\PCSX2\memcards\$card"
    if (Test-Path -LiteralPath $cardPath) { Copy-Item -LiteralPath $cardPath -Destination $record }
}
# And the console's NVRAM as it is now, which the core keeps beside the BIOS image it is given: the link
# in the game's folder, or the image itself where it could not be linked.
foreach ($extension in '.nvm', '.mec') {
    $nvram = Join-Path (Split-Path $biosUsed) ($biosFile.BaseName + $extension)
    if (Test-Path -LiteralPath $nvram) { Copy-Item -LiteralPath $nvram -Destination $record }
}
$hostArgs = @('-bios', $biosUsed, '-data', (Join-Path $game 'data'), '-log', (Join-Path $game 'core.log'),
    '-out', (Join-Path $game 'guest.txt'), '-ee', 'ps2n', '-iop', 'jit', '-vu', 'jit', '-static', 'on', '-image', 'game',
    '-until', '@@ no such line @@', '-window', '-record', $record,
    '-capture', (Join-Path $record 'capture.toml'), '-vu-ps2n', 'both', '-vu-capture', (Join-Path $record 'vu-capture.toml'),
    '-iop-ps2n', '-iop-capture', (Join-Path $record 'iop-capture.toml'))
# PCSX2's archive of patches, the one given or where an installed PCSX2 keeps it (ADR 0025): the game's
# widescreen and frame-rate patches by default, when it has them. ($archive: to PowerShell $patchArchive
# would be the parameter -PatchArchive.)
$archive = if ($PatchArchive) { (Resolve-Path -LiteralPath $PatchArchive).Path } else { Join-Path ${env:ProgramFiles} 'PCSX2\resources\patches.zip' }
if (Test-Path -LiteralPath $archive) { $hostArgs += @('-patch-archive', $archive) }
else { Say 'no archive of PCSX2''s patches here: the game plays as its disc has it, 4:3 (-PatchArchive gives one)' }
if ($Renderer) { $hostArgs += @('-renderer', $Renderer) }
$hostArgs += $HostOptions
$hostArgs += $imagePath
Say "playing; close the window to end the session, which is written down in $record"
$ErrorActionPreference = 'Continue'
& $hostExe @hostArgs 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match '^ps2n-host:' -and $_ -notmatch '^ps2n-host: frame \d+:' } | ForEach-Object { Write-Host $_ }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Add-Captures $record
Say "what the session had to interpret is in $own, for the next compile"
exit $code

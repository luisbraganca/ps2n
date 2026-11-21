<#
.SYNOPSIS
Makes a PS2 game into a folder of its own that plays it: PS2Native on a computer with nothing else
installed (docs\phase4-proposal.md, steps P4.3 and P4.4).

.DESCRIPTION
Part of the folder scripts\windows\kit.ps1 puts together; run it from there. From the disc image and the
BIOS image, both the user's own and read where they are, it writes the game's compiled code with the
toolchain beside it (LLVM's clang and lld, PS2Native's headers and recompiler): the game's executables
taken from the disc image (by the runtime, through PCSX2's own readers of every format PCSX2 plays),
translated to C++ (ps2n-recomp), compiled and linked into four modules (the game's EE code, the BIOS's,
the micro code, the IOP's code).

They go into games\<name>, the game's folder, with the runtime and a launcher named after the game
(<title>.exe): started, it plays the game as settings.txt beside it says (the disc image, the BIOS, the
patches, the window's title; written by this), on Vulkan at three times the console's resolution, with the
keyboard and the first controller (F11 or Alt+Enter covers the screen), until its window is closed. With
-Play this opens it once it is made.

Everything is in the game's folder: the modules and the runtime, the game's memory cards and the console's
settings (data; the BIOS image is linked in where it can be, so that the console's settings are kept
there), every session played, written down to be run again, with what it had to interpret (sessions; the
next run of this compiles that too), and the generated C++ and objects (work), which can be deleted.
Nothing is installed and nothing is written elsewhere, but the console's settings beside a BIOS image that
cannot be linked into the folder.

.EXAMPLE
.\make-game.ps1 -Image 'D:\games\Agent Under Fire.iso' -Bios 'D:\bios\SCPH-70004.BIN' -Title 'Agent Under Fire' -More MOVIE.ELF,ACTION.UK,DRIVING.ELF -Captures .\captures -Play
#>
param(
    # The game's disc image, read where it is: any format PCSX2 plays (.iso; a raw .bin, .img or .mdf;
    # .chd; .cso or .zso; .gz), or a .cue sheet, for the image its first FILE line names.
    [Parameter(Mandatory = $true)][string]$Image,
    # The BIOS image, read where it is.
    [Parameter(Mandatory = $true)][string]$Bios,
    # The game's name: the launcher's file name and the window's title. By default the folder's name.
    [string]$Title = '',
    # The executables on the disc the game loads itself, not through the BIOS (a game made of several
    # executables, as Agent Under Fire is): their names on the disc, separated by commas.
    [string]$More = '',
    # A folder with what earlier runs had to interpret, from elsewhere (the development tree, another
    # computer): capture.toml (the game's EE code), vu-capture.toml (micro code), iop-capture.toml (the
    # IOP's code), bios-capture.toml (the BIOS's code), and functions-ran.txt (the compiled functions runs
    # ran, the order the modules' functions are placed in). Each that is there is added to the game's own
    # (games\<name>\captures), all of which is compiled, with what the sessions in the game's folder had to
    # interpret.
    [string]$Captures = '',
    # The game's folder under games: by default named after -Title, or after the executable the disc boots.
    [string]$Name = '',
    # Open the game in a window once it is made.
    [switch]$Play,
    # Only open the game in a window, made as it was last time (its settings written again).
    [switch]$PlayOnly,
    # How many files to compile at once: by default as many as the computer has processors.
    [int]$Jobs = 0,
    # For measurements only: the modules compiled with line tables and linked with their debugging information
    # beside them (ps2n-game.pdb and the others), so that a profile of a session run again (ps2n-host -profile,
    # game.ps1 -TimeProfile) names the game's functions and generated lines. The code is the same; a folder
    # made to be played has no use for it.
    [switch]$LineTables,
    # For measurements only: more options for the compiler, separated by spaces (/clang:-march=native, ...), to
    # time what they make of the same generated code.
    [string]$ExtraFlags = '',
    # For measurements only: more inputs and options for the linker, separated by spaces (a profile runtime and the
    # libraries it needs, /EXPORT:...), for modules compiled with -ExtraFlags that need them.
    [string]$ExtraLink = '',
    # For measurements only: more options for the recompiler's compile and bios commands, separated by spaces
    # (--cop2-decide off, ...), to time what they make of the same game.
    [string]$RecompFlags = '',
    # The renderer the game is played on: vulkan by default; dx11, dx12, opengl, sw (PCSX2's software
    # renderer) or auto (the one PCSX2 chooses for this computer) where Vulkan does not work.
    [ValidateSet('', 'vulkan', 'dx11', 'dx12', 'opengl', 'sw', 'auto')][string]$Renderer = '',
    # More options for the runtime when -Play or -PlayOnly opens the game (ps2n-host -help has them):
    # '-mute', '-window-size', '1280x720', '-frames', '3000' and the like. Not written into the settings.
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
# The runtime reads the disc's files (ps2n-host -disc-extract), through PCSX2's own readers.
$discReader = Join-Path $runtime 'ps2n-host.exe'
foreach ($needed in $recomp, $clang, $lld, $discReader, (Join-Path $runtime 'ps2n-launcher.exe')) {
    if (-not (Test-Path -LiteralPath $needed)) { throw "This folder is not whole: $needed is missing." }
}
# A .cue sheet stands for the image its first FILE line names, beside it (the data track's): PCSX2 reads
# the image, not the sheet.
function Resolve-Cue([string]$path) {
    if ([IO.Path]::GetExtension($path) -ne '.cue') { return $path }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ($line -match '^\s*FILE\s+"([^"]+)"' -or $line -match '^\s*FILE\s+(\S+)') {
            $named = $Matches[1]
            $file = Join-Path (Split-Path $path -Parent) $named
            if (-not (Test-Path -LiteralPath $file)) { throw "The .cue sheet $path names $named, which is not beside it." }
            return (Resolve-Path -LiteralPath $file).Path
        }
    }
    throw "The .cue sheet $path names no file."
}
$imagePath = Resolve-Cue (Resolve-Path -LiteralPath $Image).Path
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

# ---- the game, and its folder -----------------------------------------------------------------------
$onDisc = Run $discReader @('-disc-executables', $imagePath)
$bootLine = @($onDisc | Where-Object { $_ -match '^boot ' }) | Select-Object -First 1
if (-not $bootLine) { throw "The disc image names no executable to boot: $($onDisc -join '; ')" }
$boot = ($bootLine -split ' ', 2)[1].Trim()
# A name Windows takes for a file or a folder.
function Plain([string]$text) { ($text -replace '[\\/:*?"<>|]', ' ').Trim() }
if (-not $Name) { $Name = if ($Title) { Plain $Title } else { $boot -replace '[^A-Za-z0-9._-]', '_' } }
if (-not $Title) { $Title = $Name }
$game = Join-Path $kit "games\$Name"
$work = Join-Path $game 'work'
$disc = Join-Path $work 'disc'
$gen = Join-Path $work 'generated'
$obj = Join-Path $work 'objects'
$own = Join-Path $game 'captures'
$sessions = Join-Path $game 'sessions'

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
    # The compiled functions a run ran (functions-ran.txt), each once, in the order they first ran, after those of
    # the runs before: the order the game's functions are placed in its modules (docs\adr\0044), as the wizard does.
    $ran = Join-Path $from 'functions-ran.txt'
    if (Test-Path -LiteralPath $ran) {
        $mine = Join-Path $own 'functions-ran.txt'
        $list = New-Object 'System.Collections.Generic.List[string]'
        $seen = New-Object 'System.Collections.Generic.HashSet[string]'
        foreach ($file in @($mine, $ran)) {
            if (-not (Test-Path -LiteralPath $file)) { continue }
            foreach ($line in [IO.File]::ReadAllLines($file)) {
                $name = $line.Trim()
                if ($name -and $seen.Add($name)) { $list.Add($name) }
            }
        }
        [IO.File]::WriteAllLines($mine, [string[]]$list)
    }
}

if (-not $PlayOnly) {
    Say "the game boots $boot; its folder: $game"
    New-Item -ItemType Directory -Force $disc, $gen, $obj | Out-Null
    $moreNames = @($More -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    Run $discReader (@('-disc-extract', $imagePath, $disc, $boot) + $moreNames) | Out-Null
    $bootElf = Join-Path $disc (Split-Path $boot -Leaf)
    Say "taken from the disc image: $(@($boot) + $moreNames -join ', ')"

    # ---- the C++ ---------------------------------------------------------------------------------
    if ($Captures) { Add-Captures (Resolve-Path -LiteralPath $Captures).Path }
    if (Test-Path -LiteralPath $sessions) {
        foreach ($session in (Get-ChildItem -LiteralPath $sessions -Directory)) { Add-Captures $session.FullName }
    }
    $capture = @{}
    foreach ($kind in $captureKinds.Keys) {
        $file = Join-Path $own "$kind.toml"
        $capture[$kind] = if (Test-Path -LiteralPath $file) { $file } else { '' }
    }
    Say "compiling what runs had to interpret too: $(@($captureKinds.Keys | Where-Object { $capture[$_] }) -join ', ')"
    # As the development build does (tests\tests.cmake): the boot executable in 64 files, an executable
    # it loads itself in one file for each 64 KB of it, at most 64, the micro code and IOP code in 16.
    $programIds = @('game')
    $recompMore = @($RecompFlags -split ' ' | Where-Object { $_ })
    $arguments = @('compile', $bootElf, '--id', 'game', '--out', (Join-Path $gen 'game.cpp'), '--parts', '64') + $recompMore
    if ($capture['capture']) {
        # The code runs met outside the executable, in 16 files as the micro code and IOP code are: all of
        # a game's own code when its executable is a loader (Gran Turismo 3's).
        $arguments += @('--capture', $capture['capture'], '--outside', (Join-Path $gen 'game_outside.cpp'), '--outside-parts', '16')
        $programIds += 'game_outside'
    }
    Run $recomp $arguments | Out-Null
    foreach ($m in $moreNames) {
        $file = Join-Path $disc (Split-Path $m -Leaf)
        $id = 'game_loads_' + ((Split-Path $m -Leaf) -replace '[^A-Za-z0-9]', '_').ToLower()
        $parts = [Math]::Min(64, [int][Math]::Floor((Get-Item -LiteralPath $file).Length / 65536) + 1)
        $arguments = @('compile', $file, '--id', $id, '--out', (Join-Path $gen "$id.cpp"), '--loaded-by', $bootElf) + $recompMore
        if ($parts -gt 1) { $arguments += @('--parts', "$parts") }
        if ($capture['capture']) { $arguments += @('--capture', $capture['capture']) }
        Run $recomp $arguments | Out-Null
        $programIds += $id
    }
    $arguments = @('bios', $biosPath, '--out-dir', (Join-Path $gen 'bios')) + $recompMore
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
    if ($LineTables) { $flags += @('/Z7', '-gline-tables-only') }
    if ($ExtraFlags) { $flags += @($ExtraFlags -split ' ' | Where-Object { $_ }) }
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
    $order = Join-Path $own 'functions-ran.txt'
    foreach ($kind in 'game', 'bios', 'vu', 'iop') {
        $objects = @($todo | Where-Object { $_.Kind -eq $kind } | ForEach-Object { $_.Object })
        $rsp = Join-Path $obj "$kind.rsp"
        [IO.File]::WriteAllLines($rsp, [string[]]($objects | ForEach-Object { '"' + $_ + '"' }))
        # (The import library the linker writes for a DLL goes with the objects: nothing in the folder uses it.)
        $linkFlags = @('/nologo', '/DLL', '/NOENTRY', '/NODEFAULTLIB', '/MACHINE:X64', '/OPT:REF', '/OPT:ICF')
        # The game's functions and the BIOS's in the order runs ran them (docs\adr\0044); a name the module does not
        # define is passed over (/ignore:4037), the list being the runs' for all the modules.
        if (($kind -eq 'game' -or $kind -eq 'bios') -and (Test-Path -LiteralPath $order)) { $linkFlags += @("/ORDER:@$order", '/ignore:4037') }
        if ($LineTables) { $linkFlags += '/DEBUG' }
        if ($ExtraLink) { $linkFlags += @($ExtraLink -split ' ' | Where-Object { $_ }) }
        Run $lld ($linkFlags + @("/OUT:$(Join-Path $game "ps2n-$kind.dll")", "/IMPLIB:$(Join-Path $obj "ps2n-$kind.lib")", "@$rsp",
            (Join-Path $toolchain 'lib\ps2n-host.lib'))) | Out-Null
    }
    Say "linked: $((Get-ChildItem -LiteralPath $game -Filter 'ps2n-*.dll' | ForEach-Object { '{0} ({1:N0} bytes)' -f $_.Name, $_.Length }) -join ', ')"

    # ---- the runtime beside them, and the launcher named after the game ------------------------------
    # All of the runtime but what only the wizard needs, as its wizard-files.txt names it (the wizard), as
    # the wizard packages a game: with ps2n-remake and Qt, with which the game compiles what its sessions
    # meet for the first time itself (docs\adr\0034), and a copy of the toolchain for it.
    $wizardFiles = @('ps2n-launcher.exe')
    $listed = Join-Path $runtime 'wizard-files.txt'
    if (Test-Path -LiteralPath $listed) { $wizardFiles += @(Get-Content -LiteralPath $listed | Where-Object { $_.Trim() }) }
    Get-ChildItem -LiteralPath $runtime | Where-Object { $wizardFiles -notcontains $_.Name } |
        Copy-Item -Destination $game -Recurse -Force
    $launcher = Join-Path $game ((Plain $Title) + '.exe')
    Copy-Item -LiteralPath (Join-Path $runtime 'ps2n-launcher.exe') -Destination $launcher -Force
    $ownToolchain = Join-Path $game 'toolchain'
    if (Test-Path -LiteralPath $ownToolchain) { Remove-Item -LiteralPath $ownToolchain -Recurse -Force }
    Copy-Item -LiteralPath $toolchain -Destination $game -Recurse
    Say "the runtime beside them, the toolchain, and the launcher: $launcher"
}

# ---- the game's settings ------------------------------------------------------------------------------
# What its launcher starts the runtime with (settings.txt; ps2n-host -settings), written every time, so
# that -PlayOnly with another -Renderer or -PatchArchive changes them. The BIOS image is linked into the
# folder with the ROM1, ROM2 and EROM images beside it, so that the core keeps the console's settings
# there and not beside the image; where it cannot be linked (another drive), it is read where it is.
$hostExe = Join-Path $game 'ps2n-host.exe'
if (-not (Test-Path -LiteralPath $hostExe)) { throw "The game has not been made yet: run this without -PlayOnly first." }
$biosDir = Join-Path $game 'bios'
New-Item -ItemType Directory -Force $biosDir | Out-Null
$biosFile = Get-Item -LiteralPath $biosPath
$biosUsed = '%GAME%\bios\' + $biosFile.Name
$biosFiles = @($biosFile) + @(Get-ChildItem -LiteralPath $biosFile.DirectoryName -File | Where-Object {
        $_.Extension -match '^\.(rom1|rom2|erom)$' -and ($_.BaseName -eq $biosFile.BaseName -or $_.BaseName -eq $biosFile.Name) })
foreach ($file in $biosFiles) {
    $link = Join-Path $biosDir $file.Name
    if (-not (Test-Path -LiteralPath $link)) {
        try { New-Item -ItemType HardLink -Path $link -Target $file.FullName | Out-Null }
        catch { if ($file.FullName -eq $biosFile.FullName) { $biosUsed = $biosFile.FullName } }
    }
}
# PCSX2's archive of patches, the one given or where an installed PCSX2 keeps it (ADR 0025): the game's
# widescreen and frame-rate patches by default, when it has them. ($archive: to PowerShell $patchArchive
# would be the parameter -PatchArchive.)
$archive = if ($PatchArchive) { (Resolve-Path -LiteralPath $PatchArchive).Path } else { Join-Path ${env:ProgramFiles} 'PCSX2\resources\patches.zip' }
$settings = @(
    "# $Title, made into this folder by PS2Native's make-game.ps1 on $((Get-Date).ToString('yyyy-MM-dd HH:mm')).",
    "# What its launcher ($((Plain $Title)).exe) starts the runtime with: one option to a line (ps2n-host -help",
    '# has them); %GAME% is this folder.',
    '-bios', $biosUsed, '-data', '%GAME%\data', '-log', '%GAME%\core.log', '-out', '%GAME%\guest.txt',
    '-ee', 'ps2n', '-iop', 'jit', '-vu', 'jit', '-static', 'on', '-vu-ps2n', 'both', '-iop-ps2n', '-image', 'game',
    '-until', '@@ no such line @@', '-window', '-title', $Title, '-sessions', '%GAME%\sessions')
if (Test-Path -LiteralPath $archive) { $settings += @('-patch-archive', $archive) }
else { Say 'no archive of PCSX2''s patches here: the game plays as its disc has it, 4:3 (-PatchArchive gives one)' }
if ($Renderer) { $settings += @('-renderer', $Renderer) }
$settings += $imagePath
[IO.File]::WriteAllLines((Join-Path $game 'settings.txt'), [string[]]$settings, (New-Object System.Text.UTF8Encoding($false)))
Say "its settings: $(Join-Path $game 'settings.txt')"
# How its modules are made, for the game's own recompile (ps2n-remake; docs\adr\0034).
if (-not $PlayOnly) {
    $make = @(
        "# How this game's modules are made, for the game to compile by itself what its sessions meet for",
        "# the first time (ps2n-remake.exe; PS2Native's docs/adr/0034). %GAME% is this folder.",
        "title=$Title", "image=$imagePath", "bios=$biosUsed")
    $make += @(@($boot) + $moreNames | ForEach-Object { "executable=$_" })
    [IO.File]::WriteAllLines((Join-Path $game 'make.txt'), [string[]]$make, (New-Object System.Text.UTF8Encoding($false)))
}

if (-not ($Play -or $PlayOnly)) { exit 0 }

# ---- played -----------------------------------------------------------------------------------------
# As its launcher plays it, but here, with the runtime's report in this window and -HostOptions.
Say 'playing; close the window to end the session'
$ErrorActionPreference = 'Continue'
& $hostExe -settings (Join-Path $game 'settings.txt') @HostOptions 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match '^ps2n-host:' -and $_ -notmatch '^ps2n-host: frame \d+:' } | ForEach-Object { Write-Host $_ }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
Say "the session is written down in $sessions; what it had to interpret is compiled the next time this runs"
exit $code

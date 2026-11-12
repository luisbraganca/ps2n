# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Runs a game from its disc image on ps2n-host for a number of frames.

.DESCRIPTION
Boots the disc image through the BIOS under one CPU configuration and stops
after -Frames frames. Prints what the host reports about the run: where it
stopped, the frame on display, and, for PS2Native's provider, what was
compiled, interpreted and delegated.

The configurations:

  stock-jit     PCSX2's recompilers: the reference
  stock-interp  PCSX2's interpreters
  ps2n-ee       PS2Native's EE provider, interpreting everything
  ps2n-static   PS2Native's EE provider with the game's code compiled ahead of
                time (build it in first: scripts\windows\build.ps1 -Game <elf>)
  lockstep      ps2n-ee and then ps2n-static, the second compared with the
                first at every event test
  same-as-stock stock-jit and then ps2n-static, the second compared with the
                first at every sync point (where the EE is and its cycle count
                must be the same; which registers differ, if any, is reported)
                and at every frame fingerprinted

The disc image is read where it is and nothing is written beside it. The
run's files (the game's console output, the core's log, the last frame as a
picture, what the run reports for the recompiler) go to
build\windows-clang\ps2n\tests\game\<Name>\<Config>, which git ignores. They
are derived from the game: keep them out of the repository.

With -Capture, what the run reports for the recompiler is added to that file
(ps2n-recomp capture), which scripts\windows\build.ps1 -GameCapture takes.

With -Play the game is played instead: in a window, at the console's speed,
with sound, on the keyboard and on the first controller, until the window is
closed (ps2n-host -help has the keys; Alt+Enter covers the screen). The
configuration chooses what runs it, stock PCSX2 included, to compare by eye
and ear. What a game keeps from one session to the next (the memory card,
the console's settings, the renderer's caches) is in build\game\<Name>\play,
shared by the configurations so that a game saved under one can be loaded
under another: so one session at a time. -Frames and -Seconds end a session
only when given. What a session had to interpret of the game is written to
capture.toml in that folder, and with -Capture added to the file given.

.EXAMPLE
scripts\windows\game.ps1 -Image "C:\ps2\killzone\Killzone (Europe) (En,Fr,De,Es,It,Nl).iso" -Config stock-jit
scripts\windows\game.ps1 -Image <iso> -Config ps2n-static -Capture build\game\killzone\capture.toml
scripts\windows\game.ps1 -Image <iso> -Config lockstep -Frames 900
scripts\windows\game.ps1 -Image <iso> -Config same-as-stock -Frames 4200 -FrameEvery 1 -Pad tests\games\killzone\start.pad
scripts\windows\game.ps1 -Image <iso> -Play -Name killzone
scripts\windows\game.ps1 -Image <iso> -Play -Name killzone -Config stock-jit
#>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [string]$Bios = $env:PS2N_BIOS,
    [ValidateSet('stock-jit', 'stock-interp', 'ps2n-ee', 'ps2n-static', 'lockstep', 'same-as-stock')][string]$Config = 'ps2n-static',
    [int]$Frames = 900,
    # null (nothing drawn, fastest), sw (PCSX2's software renderer: the picture to compare), vulkan, opengl, dx11, dx12,
    # auto (the one PCSX2 chooses for this computer). With -Play the default is auto.
    [string]$Renderer = 'sw',
    [int]$Upscale = 1,
    # Play the game in a window instead of running it for a number of frames (Windows only).
    [switch]$Play,
    # With -Play: no sound.
    [switch]$Mute,
    # With -Play: let the core draw its messages and performance figures over the picture.
    [switch]$Osd,
    # Also write profile.txt among the run's files: where the CPU thread's time went (ps2n-host -profile).
    [switch]$TimeProfile,
    # With -TimeProfile: look from this frame on (ps2n-host -profile-from), to leave the game's startup out.
    [int]$ProfileFrom = 0,
    # Frames at which the wall-clock time is noted, reported after the run with the first and the last frame's
    # (ps2n-host -mark): to time a game's startup and its play apart. Separated by commas, "6600,12000": through
    # powershell -File an array of numbers would arrive as one number.
    [string]$Marks = '',
    # Also fingerprint and save the frame on display every so many frames (0: only the last).
    [int]$FrameEvery = 0,
    # A script of controller input for port 1 (ps2n-host -help has the format; tests\games has some).
    [string]$Pad = '',
    # Wall-clock limit of each run.
    [int]$Seconds = 1800,
    # Names the run's folder.
    [string]$Name = 'game',
    [string]$Capture = '',
    # off: the BIOS on the interpreter even where the host has it compiled (ps2n-host -bios-static off), to time the two.
    [ValidateSet('on', 'off')][string]$BiosStatic = 'on',
    # off: every call through a register goes through the runtime's dispatcher (ps2n-host -call-sites off), to time the two.
    [ValidateSet('on', 'off')][string]$CallSites = 'on',
    [string]$BuildDir = 'build\windows-clang'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root $BuildDir
$hostExe = Join-Path $build 'ps2n\bin\ps2n-host.exe'
if (-not (Test-Path $hostExe)) { throw "Build first: scripts\windows\build.ps1" }
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
$env:PS2N_BIOS = (Resolve-Path -LiteralPath $Bios).Path
$imagePath = (Resolve-Path -LiteralPath $Image).Path

# What runs the EE, the IOP and the VUs under each configuration, and whether
# PS2Native's provider uses the code compiled ahead of time.
$providers = @{
    'stock-jit'    = @('jit', 'jit', 'jit', 'on')
    'stock-interp' = @('interp', 'interp', 'interp', 'on')
    'ps2n-ee'      = @('ps2n', 'jit', 'jit', 'off')
    'ps2n-static'  = @('ps2n', 'jit', 'jit', 'on')
    'lockstep'     = @('ps2n', 'jit', 'jit', 'on')
    'same-as-stock' = @('ps2n', 'jit', 'jit', 'on')
}
$p = $providers[$Config]

if ($Play) {
    if ($Config -eq 'lockstep' -or $Config -eq 'same-as-stock') {
        throw "-Play runs one machine: choose stock-jit, stock-interp, ps2n-ee or ps2n-static."
    }

    # (Not named $play: to PowerShell that is the parameter -Play.)
    $session = Join-Path $root "build\game\$Name\play"
    $biosDir = Join-Path $session 'bios'
    New-Item -ItemType Directory -Force $biosDir | Out-Null

    # The BIOS image is not copied. The core gets a link to it, and so keeps
    # the console's NVRAM beside the link and not beside the image; the link
    # stays, because the console's settings should outlast a session. With the
    # image go the ROM1, ROM2 and EROM images the core loads from beside it.
    $biosFile = Get-Item -LiteralPath $env:PS2N_BIOS
    $biosFiles = @($biosFile) + @(Get-ChildItem -LiteralPath $biosFile.DirectoryName -File | Where-Object {
            $_.Extension -match '^\.(rom1|rom2|erom)$' -and ($_.BaseName -eq $biosFile.BaseName -or $_.BaseName -eq $biosFile.Name) })
    foreach ($file in $biosFiles) {
        $link = Join-Path $biosDir $file.Name
        if (-not (Test-Path -LiteralPath $link)) {
            try { New-Item -ItemType HardLink -Path $link -Target $file.FullName | Out-Null }
            catch { throw "Could not link $($file.FullName) into $biosDir (is it on another drive?): $_" }
        }
    }

    $hostArgs = @('-bios', (Join-Path $biosDir $biosFile.Name), '-data', (Join-Path $session 'data'),
        '-log', (Join-Path $session 'core.log'), '-out', (Join-Path $session 'guest.txt'), '-frame', (Join-Path $session 'frame.png'),
        '-ee', $p[0], '-iop', $p[1], '-vu', $p[2], '-static', $p[3], '-bios-static', $BiosStatic, '-call-sites', $CallSites, '-image', 'game', '-until', '@@ no such line @@', '-window')
    if ($PSBoundParameters.ContainsKey('Renderer')) { $hostArgs += @('-renderer', $Renderer) }
    if ($Upscale -ne 1) { $hostArgs += @('-upscale', "$Upscale") }
    if ($Mute) { $hostArgs += '-mute' }
    if ($Osd) { $hostArgs += '-osd' }
    if ($Pad) { $hostArgs += @('-pad', (Resolve-Path -LiteralPath $Pad).Path) }
    if ($PSBoundParameters.ContainsKey('Frames')) { $hostArgs += @('-frames', "$Frames") }
    if ($PSBoundParameters.ContainsKey('Seconds')) { $hostArgs += @('-seconds', "$Seconds") }
    if ($FrameEvery -gt 0) { $hostArgs += @('-frame-every', "$FrameEvery") }

    # Someone playing goes where no script has been. What the session had to
    # interpret of the game is written down for the recompiler, as after any
    # other run.
    $reported = Join-Path $session 'capture.toml'
    if ($p[0] -eq 'ps2n') {
        Remove-Item -LiteralPath $reported -ErrorAction SilentlyContinue
        $hostArgs += @('-capture', $reported)
    }
    $hostArgs += $imagePath

    $env:PATH = (Join-Path $root 'deps\windows-x64\deps\bin') + ';' + $env:PATH
    Write-Host "Playing in a window ($Config). Close the window to end the session. Its files are in $session"

    # The host reports on its error stream, which Windows PowerShell would
    # otherwise take for a failure.
    $ErrorActionPreference = 'Continue'
    & $hostExe @hostArgs 2>&1 | ForEach-Object { "$_" } |
        Where-Object { $_ -match '^ps2n-host:' -and $_ -notmatch '^ps2n-host: frame \d+:' } | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'

    # With -Capture, that goes into the file scripts\windows\build.ps1
    # -GameCapture takes, beside what earlier runs reported.
    if ($Capture -and $p[0] -eq 'ps2n' -and (Test-Path -LiteralPath $reported)) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($Capture)) { $Capture } else { Join-Path $root $Capture }
        & $recomp capture --out $target $target $reported
    }
    exit $code
}

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

$work = Join-Path $build "ps2n\tests\game\$Name\$Config"

# A game prints no last line to stop at: the run ends at the frame limit.
$arguments = @(
    "-DHOST=$(ForCMake $hostExe)", "-DWORK_DIR=$(ForCMake $work)",
    "-DDLL_DIR=$(ForCMake (Join-Path $root 'deps\windows-x64\deps\bin'))", "-DELF=$(ForCMake $imagePath)",
    "-DEE=$($p[0])", "-DIOP=$($p[1])", "-DVU=$($p[2])", "-DSTATIC=$($p[3])", "-DBIOS_STATIC=$BiosStatic", "-DCALL_SITES=$CallSites", '-DIMAGE=game',
    "-DFRAMES=$Frames", "-DSECONDS=$Seconds", '-DUNTIL=@@ no such line @@', '-DEXPECT_EXIT=2', '-DWHOLE=1', '-DTIMES=1')
if ($Renderer -ne 'null') { $arguments += @("-DRENDERER=$Renderer", "-DUPSCALE=$Upscale", "-DFRAME_EVERY=$FrameEvery") }
if ($Pad) { $arguments += "-DPAD=$(ForCMake (Resolve-Path -LiteralPath $Pad).Path)" }
if ($TimeProfile) { $arguments += '-DPROFILE=1' }
if ($TimeProfile -and $ProfileFrom -gt 0) { $arguments += "-DPROFILE_FROM=$ProfileFrom" }
$markList = @($Marks -split '[,;\s]+' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
if ($markList.Count -gt 0) { $arguments += "-DMARKS=$($markList -join ';')" }
# Every run here is timed, so it is kept on the processor's fastest cores:
# left to the system, the same run took between 75 and 150 s.
$arguments += '-DFAST_CORES=1'
if ($Config -eq 'lockstep') { $arguments += '-DLOCKSTEP=1' }
# A game is held to the stock run in place and time at every sync point, and
# in what it shows; where its registers differ, the host says which and how
# often (an instruction that follows the console on purpose can do that).
elseif ($Config -eq 'same-as-stock') { $arguments += @('-DSAME_AS_STOCK=1', '-DREGISTERS=report') }
elseif ($p[0] -eq 'ps2n') { $arguments += '-DCAPTURE=1' }
$arguments += @('-P', (Join-Path $root 'tests\guest_test.cmake'))

# The driver reports on its error stream, which Windows PowerShell would
# otherwise take for a failure.
$watch = [Diagnostics.Stopwatch]::StartNew()
$ErrorActionPreference = 'Continue'
$output = & cmake @arguments 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$watch.Stop()

# The lines for the frames fingerprinted along the way are left out when
# there are very many of them (-FrameEvery 1); the driver compares them.
$frameLines = @($output | Where-Object { $_ -match '^ps2n-host: frame \d+:' }).Count
$output | Where-Object {
    $_ -match '^(ps2n-host:|---|The compiled run|First difference|  recorded:|  this run:|  stock:|  compiled:|CMake Error|  )' -and
    ($frameLines -le 60 -or $_ -notmatch '^ps2n-host: frame \d+:')
} | ForEach-Object { Write-Host $_ }
Write-Host ("{0}: {1:N1} s; files in {2}" -f $Config, $watch.Elapsed.TotalSeconds, $work)

# The comparison of whole outputs is for test programs; a run that ended at
# the frame limit as expected has done what was asked.
$ended = $output | Where-Object { $_ -match 'stopped at frame limit' }
if (-not $ended -or (($Config -eq 'lockstep' -or $Config -eq 'same-as-stock') -and $code -ne 0)) {
    Write-Host "The run did not end as expected (driver exit code $code)."
    exit 1
}

if ($Capture -and $p[0] -eq 'ps2n') {
    $reported = Join-Path $work 'capture.toml'
    if (Test-Path $reported) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($Capture)) { $Capture } else { Join-Path $root $Capture }
        & $recomp capture --out $target $target $reported
    }
}
exit 0

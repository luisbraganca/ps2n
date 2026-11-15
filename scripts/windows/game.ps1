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
  ps2n-ee       PS2Native's EE, IOP and VU providers, interpreting everything
  ps2n-static   PS2Native's EE, IOP and VU providers with the game's code
                compiled ahead of time (build it in first:
                scripts\windows\build.ps1 -Game <elf>, for the vector units'
                micro code -VuCapture, and for the IOP's code -IopCapture)
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
With -VuCapture, the blocks of the vector units' micro code that the run had
to interpret are added to that file (ps2n-recomp vu-capture), which
scripts\windows\build.ps1 -VuCapture takes.

With -IopCapture, the blocks of the IOP's code that the run had to interpret
are added to that file (ps2n-recomp iop-capture), which
scripts\windows\build.ps1 -IopCapture takes.

In PS2Native's configurations the vector units' micro programs run on
PS2Native's VU provider (-VuPs2n none puts PCSX2's VU recompiler back under
them, as before step G11), and the IOP's code on PS2Native's IOP provider
(-IopPs2n off puts PCSX2's IOP recompiler back, as before step G12).

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
    # auto (the one PCSX2 chooses for this computer). With -Play the default is vulkan.
    [string]$Renderer = 'sw',
    # The internal resolution of a hardware renderer, a multiple of the console's. With -Play the default is 3
    # (about 1080p).
    [int]$Upscale = 1,
    # With -Play: the area the picture goes into, as 1280x720. Left out: 1920x1080, which covers the screen
    # where a window that size does not fit (ADR 0025).
    [string]$WindowSize = '',
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
    # Also write vu-report.txt among the run's files: what was written into the VUs' micro memory and what
    # they ran, with the counts frame by frame and the contents written beside it (ps2n-host -vu-report).
    [switch]$VuReport,
    # Also write iop-report.txt among the run's files: what the IOP ran, which modules were loaded where, how
    # often its memory was written, with the counts frame by frame beside it (ps2n-host -iop-report).
    [switch]$IopReport,
    # Which VUs' micro programs run on PS2Native's VU provider (ps2n-host -vu-ps2n): none, 0, 1 or both. Left out:
    # both in PS2Native's configurations, none under stock PCSX2. Never in the stock run a same-as-stock run is
    # compared with.
    [ValidateSet('', 'none', '0', '1', 'both')][string]$VuPs2n = '',
    # off has PS2Native's VU provider interpret every block of micro code, the ones the host has compiled too
    # (ps2n-host -vu-static off), to time the two or to have a reference.
    [ValidateSet('on', 'off')][string]$VuStatic = 'on',
    # Add the blocks of micro code the run had to interpret to this file (ps2n-recomp vu-capture), which
    # scripts\windows\build.ps1 -VuCapture takes.
    [string]$VuCapture = '',
    # Whether the IOP's code runs on PS2Native's IOP provider (ps2n-host -iop-ps2n): on or off. Left out: on in
    # PS2Native's configurations, off under stock PCSX2. Never in the stock run a same-as-stock run is compared with.
    [ValidateSet('', 'on', 'off')][string]$IopPs2n = '',
    # off has PS2Native's IOP provider interpret every block of IOP code, the ones the host has compiled too
    # (ps2n-host -iop-static off), to time the two or to have a reference.
    [ValidateSet('on', 'off')][string]$IopStatic = 'on',
    # Add the blocks of IOP code the run had to interpret to this file (ps2n-recomp iop-capture), which
    # scripts\windows\build.ps1 -IopCapture takes.
    [string]$IopCapture = '',
    # Frames at which the wall-clock time is noted, reported after the run with the first and the last frame's
    # (ps2n-host -mark): to time a game's startup and its play apart. Separated by commas, "6600,12000": through
    # powershell -File an array of numbers would arrive as one number.
    [string]$Marks = '',
    # Also fingerprint and save the frame on display every so many frames (0: only the last).
    [int]$FrameEvery = 0,
    # A script of controller input for port 1 (ps2n-host -help has the format; tests\games has some).
    [string]$Pad = '',
    # A session played in the window, run again without one (step G13): its folder under
    # build\game\<name>\play\sessions. The run gets the session's controller, clock, memory cards and
    # NVRAM, as many frames as it had unless -Frames says otherwise, and the patches it was played with
    # unless -Patches does (a session written down before 2026-11-15 has none written down); any
    # configuration, a comparison included.
    [string]$Replay = '',
    # The game's patches to apply (ADR 0025): none, or widescreen and fps (the frame-rate patch), separated
    # by a comma, with the EE overclock a patch applied asks for in its note. Left out: widescreen,fps with
    # -Play, the session's with -Replay, none otherwise; a comparison applies them to both of its runs. The
    # game's own patch file is taken from the archive of the PCSX2 installed on this computer, into the
    # run's data folder.
    [string]$Patches = '',
    # PCSX2's EE cycle rate setting (ps2n-host -ee-cycle-rate): 1, 2 and 3 its overclock of 130%, 180% and
    # 300%, -1 to -3 75%, 60% and 50%, 0 its default; given, a patch does not change it. Left out: 0, or
    # what a patch asks for.
    [ValidateRange(-3, 3)][int]$EeCycleRate = 0,
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

# PCSX2's archive of patches, read where the installed PCSX2 keeps it (ADR 0025).
$patchArchive = Join-Path ${env:ProgramFiles} 'PCSX2\resources\patches.zip'
if (-not (Test-Path -LiteralPath $patchArchive)) { $patchArchive = '' }

# The VUs on PS2Native's VU provider: both of them wherever PS2Native's EE
# provider runs, unless -VuPs2n says otherwise.
$vuOurs = if ($VuPs2n) { $VuPs2n } elseif ($p[0] -eq 'ps2n') { 'both' } else { 'none' }
if ($vuOurs -ne 'none' -and $p[0] -ne 'ps2n') { throw "-VuPs2n is for PS2Native's configurations, not $Config." }

# The IOP on PS2Native's IOP provider, likewise.
$iopOurs = if ($IopPs2n) { $IopPs2n -eq 'on' } else { $p[0] -eq 'ps2n' }
if ($iopOurs -and $p[0] -ne 'ps2n') { throw "-IopPs2n is for PS2Native's configurations, not $Config." }

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

    # The session is written down to be run again (-Replay): the host writes the
    # controller frame by frame into its folder, which first gets the memory
    # cards and the console's NVRAM as they are now.
    $record = Join-Path $session ("sessions\" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force $record | Out-Null
    foreach ($card in 'Mcd001.ps2', 'Mcd002.ps2') {
        $cardPath = Join-Path $session "data\PCSX2\memcards\$card"
        if (Test-Path -LiteralPath $cardPath) { Copy-Item -LiteralPath $cardPath -Destination $record }
    }
    foreach ($extension in '.nvm', '.mec') {
        $nvram = Join-Path $biosDir ($biosFile.BaseName + $extension)
        if (Test-Path -LiteralPath $nvram) { Copy-Item -LiteralPath $nvram -Destination $record }
    }

    $hostArgs = @('-bios', (Join-Path $biosDir $biosFile.Name), '-data', (Join-Path $session 'data'),
        '-log', (Join-Path $session 'core.log'), '-out', (Join-Path $session 'guest.txt'), '-frame', (Join-Path $session 'frame.png'),
        '-ee', $p[0], '-iop', $p[1], '-vu', $p[2], '-static', $p[3], '-bios-static', $BiosStatic, '-call-sites', $CallSites, '-image', 'game', '-until', '@@ no such line @@', '-window')
    if ($PSBoundParameters.ContainsKey('Renderer')) { $hostArgs += @('-renderer', $Renderer) }
    if ($PSBoundParameters.ContainsKey('Upscale')) { $hostArgs += @('-upscale', "$Upscale") }
    if ($WindowSize) { $hostArgs += @('-window-size', $WindowSize) }
    if ($Mute) { $hostArgs += '-mute' }
    if ($Osd) { $hostArgs += '-osd' }
    if ($Pad) { $hostArgs += @('-pad', (Resolve-Path -LiteralPath $Pad).Path) }
    if ($PSBoundParameters.ContainsKey('Frames')) { $hostArgs += @('-frames', "$Frames") }
    if ($PSBoundParameters.ContainsKey('Seconds')) { $hostArgs += @('-seconds', "$Seconds") }
    if ($FrameEvery -gt 0) { $hostArgs += @('-frame-every', "$FrameEvery") }
    foreach ($mark in @($Marks -split '[,;\s]+' | Where-Object { $_ -ne '' })) { $hostArgs += @('-mark', "$([int]$mark)") }
    $hostArgs += @('-record', $record)
    if ($Patches) { $hostArgs += @('-patches', $Patches) }
    if ($patchArchive) { $hostArgs += @('-patch-archive', $patchArchive) }
    if ($PSBoundParameters.ContainsKey('EeCycleRate')) { $hostArgs += @('-ee-cycle-rate', "$EeCycleRate") }

    # Someone playing goes where no script has been. What the session had to
    # interpret of the game is written down for the recompiler, as after any
    # other run.
    $reported = Join-Path $session 'capture.toml'
    if ($p[0] -eq 'ps2n') {
        Remove-Item -LiteralPath $reported -ErrorAction SilentlyContinue
        $hostArgs += @('-capture', $reported)
    }
    # The same for the blocks of the vector units' micro code.
    $vuReported = Join-Path $session 'vu-capture.toml'
    if ($vuOurs -ne 'none') {
        Remove-Item -LiteralPath $vuReported -ErrorAction SilentlyContinue
        $hostArgs += @('-vu-ps2n', $vuOurs, '-vu-capture', $vuReported)
        if ($VuStatic -eq 'off') { $hostArgs += @('-vu-static', 'off') }
    }
    # And for the blocks of the IOP's code.
    $iopReported = Join-Path $session 'iop-capture.toml'
    if ($iopOurs) {
        Remove-Item -LiteralPath $iopReported -ErrorAction SilentlyContinue
        $hostArgs += @('-iop-ps2n', '-iop-capture', $iopReported)
        if ($IopStatic -eq 'off') { $hostArgs += @('-iop-static', 'off') }
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
    Write-Host "The session is written down in $record (game.ps1 -Replay runs it again)"

    # With -Capture, that goes into the file scripts\windows\build.ps1
    # -GameCapture takes, beside what earlier runs reported.
    if ($Capture -and $p[0] -eq 'ps2n' -and (Test-Path -LiteralPath $reported)) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($Capture)) { $Capture } else { Join-Path $root $Capture }
        & $recomp capture --out $target $target $reported
    }
    if ($VuCapture -and $vuOurs -ne 'none' -and (Test-Path -LiteralPath $vuReported)) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($VuCapture)) { $VuCapture } else { Join-Path $root $VuCapture }
        & $recomp vu-capture --out $target $target $vuReported
    }
    if ($IopCapture -and $iopOurs -and (Test-Path -LiteralPath $iopReported)) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($IopCapture)) { $IopCapture } else { Join-Path $root $IopCapture }
        & $recomp iop-capture --out $target $target $iopReported
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
    "-DEE=$($p[0])", "-DIOP=$(if ($iopOurs) { 'ps2n' } else { $p[1] })", "-DVU=$($p[2])", "-DSTATIC=$($p[3])", "-DBIOS_STATIC=$BiosStatic",
    "-DCALL_SITES=$CallSites", '-DIMAGE=game',
    "-DFRAMES=$Frames", "-DSECONDS=$Seconds", '-DUNTIL=@@ no such line @@', '-DEXPECT_EXIT=2', '-DWHOLE=1', '-DTIMES=1')
if ($Renderer -ne 'null') { $arguments += @("-DRENDERER=$Renderer", "-DUPSCALE=$Upscale", "-DFRAME_EVERY=$FrameEvery") }
if ($Pad) { $arguments += "-DPAD=$(ForCMake (Resolve-Path -LiteralPath $Pad).Path)" }
if ($Patches) { $arguments += "-DPATCHES=$Patches" }
# The archive the game's patch file is taken from: for -Patches, and for a session run again, which has
# the patches it was played with.
if ($patchArchive -and ($Patches -or $Replay)) { $arguments += "-DPATCH_ARCHIVE=$(ForCMake $patchArchive)" }
if ($PSBoundParameters.ContainsKey('EeCycleRate')) { $arguments += "-DEE_CYCLE_RATE=$EeCycleRate" }
if ($Replay) {
    if ($Pad) { throw "-Replay has the session's own controller: leave -Pad out." }
    $replayDir = (Resolve-Path -LiteralPath $Replay).Path
    $sessionInput = Join-Path $replayDir 'input.txt'
    if (-not (Test-Path -LiteralPath $sessionInput)) { throw "No session written down in $replayDir (input.txt)." }
    $arguments += @("-DINPUT=$(ForCMake $sessionInput)", "-DMEMCARDS_FROM=$(ForCMake $replayDir)", "-DNVRAM_FROM=$(ForCMake $replayDir)")
    if (-not $PSBoundParameters.ContainsKey('Frames')) {
        $end = Select-String -LiteralPath $sessionInput -Pattern '^end (\d+)' | Select-Object -Last 1
        if (-not $end) { throw "The session in $replayDir did not end (no 'end' line in input.txt)." }
        $Frames = [int]$end.Matches[0].Groups[1].Value
        $arguments = $arguments | ForEach-Object { if ($_ -like '-DFRAMES=*') { "-DFRAMES=$Frames" } else { $_ } }
    }
}
if ($TimeProfile) { $arguments += '-DPROFILE=1' }
if ($TimeProfile -and $ProfileFrom -gt 0) { $arguments += "-DPROFILE_FROM=$ProfileFrom" }
if ($VuReport) { $arguments += '-DVU_REPORT=1' }
if ($IopReport) { $arguments += '-DIOP_REPORT=1' }
if ($vuOurs -ne 'none') {
    $arguments += "-DVU_PS2N=$vuOurs"
    if ($VuStatic -eq 'off') { $arguments += '-DVU_STATIC=off' }
    # What the run had to interpret of the micro code is always written down among its files.
    $arguments += "-DVU_CAPTURE=$(ForCMake (Join-Path $work 'vu-capture.toml'))"
}
if ($iopOurs) {
    if ($IopStatic -eq 'off') { $arguments += '-DIOP_STATIC=off' }
    # What the run had to interpret of the IOP's code is always written down among its files.
    $arguments += "-DIOP_CAPTURE=$(ForCMake (Join-Path $work 'iop-capture.toml'))"
}
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
if ($VuCapture -and $vuOurs -ne 'none') {
    $reported = Join-Path $work 'vu-capture.toml'
    if (Test-Path $reported) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($VuCapture)) { $VuCapture } else { Join-Path $root $VuCapture }
        & $recomp vu-capture --out $target $target $reported
    }
}
if ($IopCapture -and $iopOurs) {
    $reported = Join-Path $work 'iop-capture.toml'
    if (Test-Path $reported) {
        $recomp = Join-Path $root 'build\tools-windows\bin\ps2n-recomp.exe'
        $target = if ([IO.Path]::IsPathRooted($IopCapture)) { $IopCapture } else { Join-Path $root $IopCapture }
        & $recomp iop-capture --out $target $target $reported
    }
}
exit 0

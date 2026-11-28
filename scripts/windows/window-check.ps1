# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Checks the window a game is played in, by machine: starts a session of
game.ps1 -Play with the sound off and drives its window from outside.

.DESCRIPTION
Finds the game's window (its class is PS2NativeGame), then carries out the
steps it was given, each at a number of seconds after the window appeared:

  "6 shot name.png"    the picture on display into -OutDir, as the game's
                       renderer has it (at its internal resolution: the
                       host writes it when asked, whatever the renderer),
                       and beside it name-window.png, a photograph of the
                       window: from the screen when it is in front,
                       otherwise as Windows draws it. A picture Vulkan
                       presents comes out white in such a photograph on
                       this computer; the window's size and title still
                       show. The output says which way it was taken.
  "8 key K 0.2"        press a key in the window for 0.2 s: a letter, a
                       digit, or Return Backspace Up Down Left Right F11
                       Escape Delete Tab Space
  "9 click 640 360"    move the pointer to that place of the picture (its
                       pixels, from the top left) and click its left button
                       ("rclick" the right one, "move" only moves it)
  "12 pointer hidden"  whether the pointer is hidden over the picture now
                       (ADR 0068): printed, and the check fails where it is
                       not as given ("hidden" or "shown"; nothing given,
                       only printed)
  "20 close"           close the window, without the game asking first
  "18 close-button"    the window's close button, as a player presses it: the
                       game asks first (its menu on Quit's question)

and prints what the window was called and how large it was at each step, and
what the host reported when the session ended. The photographs are of the
game: keep them out of the repository. -OutDir is under the build tree unless
given.

The session is always muted: this is for checking, and the sound would come
out of the loudspeakers of whoever is not there to hear it.

.EXAMPLE
scripts\windows\window-check.ps1 -Image <iso> -Name killzone -Steps '5 shot a.png', '12 shot b.png', '14 close'
scripts\windows\window-check.ps1 -Image <iso> -Name killzone -Steps '41 shot film.png', '42 key K', '45 shot title.png', '52 key Return', '58 shot menu.png', '60 close'
scripts\windows\window-check.ps1 -Image <iso> -Name killzone -Config stock-jit -Steps '9 shot a.png', '10 key F11', '12 shot full.png', '13 key F11', '16 close'
#>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [Parameter(Mandatory = $true)][string[]]$Steps,
    [string]$Bios = $env:PS2N_BIOS,
    [ValidateSet('stock-jit', 'stock-interp', 'ps2n-ee', 'ps2n-static')][string]$Config = 'ps2n-static',
    [string]$Name = 'game',
    [string]$OutDir = '',
    # The area the picture goes into. A window of this size by default, so that a check does not cover the
    # screen of whoever is at the computer; '' for the game's own default (1920x1080, full screen where a
    # window that size does not fit).
    [string]$WindowSize = '1280x720',
    # A renderer other than the game's own default (game.ps1 -Renderer): dx12, for example, whose picture
    # a photograph of the window shows.
    [string]$Renderer = '',
    # The game's patches, when not its default (game.ps1 -Patches).
    [string]$Patches = '',
    # How long the session may take to end after the last step.
    [int]$ExitWait = 60
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Bios) { throw "A BIOS image is needed: -Bios <file>, or set PS2N_BIOS." }
if (-not $OutDir) { $OutDir = Join-Path $root "build\game\$Name\window-check" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Ps2nGameWindow
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, System.Text.StringBuilder text, int max);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    // Sizes in the display's own pixels, as the game's window counts them.
    public static void Init() { SetProcessDPIAware(); }
    public static IntPtr Find() { return FindWindowW("PS2NativeGame", null); }
    public static string Title(IntPtr hwnd) { var sb = new System.Text.StringBuilder(256); GetWindowTextW(hwnd, sb, 256); return sb.ToString(); }
    public static string Size(IntPtr hwnd)
    {
        RECT c, w; GetClientRect(hwnd, out c); GetWindowRect(hwnd, out w);
        return "the picture's area " + (c.Right - c.Left) + "x" + (c.Bottom - c.Top) + ", the window " + (w.Right - w.Left) + "x" + (w.Bottom - w.Top);
    }
    public static string LastWay = "";
    public static bool Capture(IntPtr hwnd, string path)
    {
        RECT r; if (!GetWindowRect(hwnd, out r)) return false;
        int w = r.Right - r.Left, h = r.Bottom - r.Top; if (w <= 0 || h <= 0) return false;
        using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            using (Graphics g = Graphics.FromImage(bmp))
            {
                if (GetForegroundWindow() == hwnd)
                {
                    // What is on the screen where the window is: PrintWindow does not see
                    // a picture Vulkan presents (it comes out white).
                    g.CopyFromScreen(r.Left, r.Top, 0, 0, new Size(w, h));
                    LastWay = "from the screen";
                }
                else
                {
                    IntPtr hdc = g.GetHdc();
                    bool ok = PrintWindow(hwnd, hdc, 2); // what the window shows, a Direct3D picture included
                    g.ReleaseHdc(hdc);
                    if (!ok) return false;
                    LastWay = "as Windows draws it, the window not being in front";
                }
            }
            bmp.Save(path, ImageFormat.Png);
        }
        return true;
    }
    public static void Key(IntPtr hwnd, int vk, bool down)
    {
        long lp = down ? 1L : unchecked((long)0xC0000001L);
        PostMessageW(hwnd, down ? 0x0100u : 0x0101u, new IntPtr(vk), new IntPtr(lp));
    }
    // WM_CLOSE as the close button sends it (`ask`: the game asks first, its menu on Quit's question), or
    // with window::kCloseWithoutAsking for its wParam, which closes it at once.
    public static void Close(IntPtr hwnd, bool ask) { PostMessageW(hwnd, 0x0010, ask ? IntPtr.Zero : new IntPtr(0x50534E), IntPtr.Zero); }
    // The pointer at (x, y) of the picture: moved there, and a button pressed and let go (0 none, 1 left, 2 right).
    public static void Pointer(IntPtr hwnd, int x, int y, int button)
    {
        IntPtr at = new IntPtr((y << 16) | (x & 0xFFFF));
        PostMessageW(hwnd, 0x0200, IntPtr.Zero, at); // WM_MOUSEMOVE
        if (button == 1) { PostMessageW(hwnd, 0x0201, new IntPtr(1), at); PostMessageW(hwnd, 0x0202, IntPtr.Zero, at); }
        if (button == 2) { PostMessageW(hwnd, 0x0204, new IntPtr(2), at); PostMessageW(hwnd, 0x0205, IntPtr.Zero, at); }
    }
    // ps2n::host::window::kShotMessage: the host writes shot-<number>.png beside its last frame.
    public static void AskShot(IntPtr hwnd, int number) { PostMessageW(hwnd, 0x8050, new IntPtr(number), IntPtr.Zero); }
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
    // ps2n::host::window::kPointerMessage: 1 the pointer is hidden over the picture, 0 it is not, -1 the window did
    // not answer. Sent, so answered before what was posted and not yet taken up: a step leaves a moment after one
    // that posts.
    public static int PointerHidden(IntPtr hwnd)
    {
        IntPtr result;
        if (SendMessageTimeoutW(hwnd, 0x8051, IntPtr.Zero, IntPtr.Zero, 0x0002, 5000, out result) == IntPtr.Zero) return -1; // SMTO_ABORTIFHUNG
        return result.ToInt32();
    }
}
'@
[Ps2nGameWindow]::Init()

$virtualKeys = @{ 'Return' = 0x0D; 'Backspace' = 0x08; 'Up' = 0x26; 'Down' = 0x28; 'Left' = 0x25; 'Right' = 0x27; 'F11' = 0x7A
    'Escape' = 0x1B; 'Delete' = 0x2E; 'Tab' = 0x09; 'Space' = 0x20 }
function VirtualKey([string]$key) {
    if ($virtualKeys.ContainsKey($key)) { return $virtualKeys[$key] }
    if ($key -match '^[A-Za-z0-9]$') { return [int][char]$key.ToUpper() }
    throw "Unknown key '$key': a letter, a digit, or one of $($virtualKeys.Keys -join ' ')."
}
function Seconds([string]$text) { [double]::Parse($text, [Globalization.CultureInfo]::InvariantCulture) }

$log = Join-Path $OutDir 'session.log'
$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root 'scripts\windows\game.ps1'),
    '-Image', (Resolve-Path -LiteralPath $Image).Path, '-Bios', (Resolve-Path -LiteralPath $Bios).Path,
    '-Play', '-Mute', '-Name', $Name, '-Config', $Config)
if ($WindowSize) { $arguments += @('-WindowSize', $WindowSize) }
if ($Renderer) { $arguments += @('-Renderer', $Renderer) }
if ($Patches) { $arguments += @('-Patches', $Patches) }
$commandLine = ($arguments | ForEach-Object { if ($_ -match '[\s()]') { '"' + $_ + '"' } else { $_ } }) -join ' '
$process = Start-Process -FilePath 'powershell.exe' -ArgumentList $commandLine -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $log -RedirectStandardError (Join-Path $OutDir 'session.err')
$null = $process.Handle # so that its exit code can be read once it has ended

$hwnd = [IntPtr]::Zero
$watch = [Diagnostics.Stopwatch]::StartNew()
while ($watch.Elapsed.TotalSeconds -lt 60 -and $hwnd -eq [IntPtr]::Zero -and -not $process.HasExited) {
    Start-Sleep -Milliseconds 100
    $hwnd = [Ps2nGameWindow]::Find()
}
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "No window appeared."
    if (-not $process.HasExited) { cmd /c "taskkill /T /F /PID $($process.Id) >nul 2>&1" }
    Get-Content -LiteralPath $log, (Join-Path $OutDir 'session.err') -ErrorAction SilentlyContinue | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host ("The window appeared after {0:N1} s: called [{1}], {2}" -f $watch.Elapsed.TotalSeconds, [Ps2nGameWindow]::Title($hwnd), [Ps2nGameWindow]::Size($hwnd))

$failed = $false
$shots = 0
$session = Join-Path $root "build\game\$Name\play"
$watch.Restart()
foreach ($step in $Steps) {
    $words = $step.Trim() -split '\s+'
    $wait = (Seconds $words[0]) - $watch.Elapsed.TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
    if ($process.HasExited) { Write-Host "The session ended before the step [$step]."; $failed = $true; break }
    switch ($words[1]) {
        'shot' {
            $shots++
            $fromHost = Join-Path $session ("shot-{0}.png" -f $shots)
            Remove-Item -LiteralPath $fromHost -ErrorAction SilentlyContinue
            [Ps2nGameWindow]::AskShot($hwnd, $shots)
            $photo = [IO.Path]::GetFileNameWithoutExtension($words[2]) + '-window.png'
            $ok = [Ps2nGameWindow]::Capture($hwnd, (Join-Path $OutDir $photo))
            $waited = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $fromHost) -and $waited.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 100 }
            $pictureOk = Test-Path -LiteralPath $fromHost
            if ($pictureOk) { Move-Item -LiteralPath $fromHost -Destination (Join-Path $OutDir $words[2]) -Force }
            if (-not $ok -or -not $pictureOk) { $failed = $true }
            Write-Host ("{0,6:N1} s: the picture {1} {2}, the window's photograph {3} {4}; called [{5}], {6}" -f $watch.Elapsed.TotalSeconds,
                $words[2], $(if ($pictureOk) { 'written by the host' } else { 'NOT WRITTEN' }), $photo,
                $(if ($ok) { 'taken ' + [Ps2nGameWindow]::LastWay } else { 'FAILED' }), [Ps2nGameWindow]::Title($hwnd), [Ps2nGameWindow]::Size($hwnd))
        }
        'key' {
            $vk = VirtualKey $words[2]
            $held = if ($words.Count -gt 3) { Seconds $words[3] } else { 0.15 }
            [Ps2nGameWindow]::Key($hwnd, $vk, $true)
            Start-Sleep -Milliseconds ([int]($held * 1000))
            [Ps2nGameWindow]::Key($hwnd, $vk, $false)
            Write-Host ("{0,6:N1} s: key {1} for {2} s" -f $watch.Elapsed.TotalSeconds, $words[2], $held)
        }
        'close' {
            [Ps2nGameWindow]::Close($hwnd, $false)
            Write-Host ("{0,6:N1} s: the window is asked to close" -f $watch.Elapsed.TotalSeconds)
        }
        'close-button' {
            [Ps2nGameWindow]::Close($hwnd, $true)
            Write-Host ("{0,6:N1} s: the window's close button, as a player presses it" -f $watch.Elapsed.TotalSeconds)
        }
        { $_ -in 'click', 'rclick', 'move' } {
            $button = @{ 'move' = 0; 'click' = 1; 'rclick' = 2 }[$words[1]]
            [Ps2nGameWindow]::Pointer($hwnd, [int]$words[2], [int]$words[3], $button)
            Write-Host ("{0,6:N1} s: {1} at {2},{3}" -f $watch.Elapsed.TotalSeconds, $words[1], $words[2], $words[3])
        }
        'pointer' {
            $said = switch ([Ps2nGameWindow]::PointerHidden($hwnd)) { 1 { 'hidden' } 0 { 'shown' } default { 'not known (no answer)' } }
            $expected = if ($words.Count -gt 2) { $words[2] } else { '' }
            $verdict = ''
            if ($expected) {
                if ($said -eq $expected) { $verdict = ', as expected' } else { $verdict = ", NOT $expected as expected"; $failed = $true }
            }
            Write-Host ("{0,6:N1} s: the pointer {1}{2}" -f $watch.Elapsed.TotalSeconds, $said, $verdict)
        }
        default { throw "Unknown step [$step]: shot <file>, key <key> [seconds], click|rclick|move <x> <y>, pointer [hidden|shown], close, or close-button." }
    }
}

if (-not $process.WaitForExit($ExitWait * 1000)) {
    Write-Host "The session did not end within $ExitWait s of the last step; ending it."
    # The runtime game.ps1 started with it: its whole tree.
    cmd /c "taskkill /T /F /PID $($process.Id) >nul 2>&1"
    $failed = $true
}
elseif ($process.ExitCode -ne 0) {
    Write-Host "The session ended with exit code $($process.ExitCode)."
    $failed = $true
}

# What the host reported; its long lines are cut.
Get-Content -LiteralPath $log -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host $(if ($_.Length -gt 300) { $_.Substring(0, 300) + ' ...' } else { $_ })
}
Get-Content -LiteralPath (Join-Path $OutDir 'session.err') -ErrorAction SilentlyContinue | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
Write-Host "The photographs and the session's output are in $OutDir"
exit $(if ($failed) { 1 } else { 0 })

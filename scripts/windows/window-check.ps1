# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Checks the window a game is played in, by machine: starts a session of
game.ps1 -Play with the sound off and drives its window from outside.

.DESCRIPTION
Finds the game's window (its class is PS2NativeGame), then carries out the
steps it was given, each at a number of seconds after the window appeared:

  "6 shot name.png"    photograph the window into -OutDir (as Windows draws
                       it, whether or not it is in front)
  "8 key K 0.2"        press a key in the window for 0.2 s: a letter, a
                       digit, or Return Backspace Up Down Left Right F11
  "20 close"           close the window, as its close button would

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
    public static bool Capture(IntPtr hwnd, string path)
    {
        RECT r; if (!GetWindowRect(hwnd, out r)) return false;
        int w = r.Right - r.Left, h = r.Bottom - r.Top; if (w <= 0 || h <= 0) return false;
        using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            using (Graphics g = Graphics.FromImage(bmp))
            {
                IntPtr hdc = g.GetHdc();
                bool ok = PrintWindow(hwnd, hdc, 2); // what the window shows, a renderer's picture included
                g.ReleaseHdc(hdc);
                if (!ok) return false;
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
    public static void Close(IntPtr hwnd) { PostMessageW(hwnd, 0x0010, IntPtr.Zero, IntPtr.Zero); }
}
'@
[Ps2nGameWindow]::Init()

$virtualKeys = @{ 'Return' = 0x0D; 'Backspace' = 0x08; 'Up' = 0x26; 'Down' = 0x28; 'Left' = 0x25; 'Right' = 0x27; 'F11' = 0x7A }
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
    if (-not $process.HasExited) { $process.Kill() }
    Get-Content -LiteralPath $log, (Join-Path $OutDir 'session.err') -ErrorAction SilentlyContinue | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
    exit 1
}
Write-Host ("The window appeared after {0:N1} s: called [{1}], {2}" -f $watch.Elapsed.TotalSeconds, [Ps2nGameWindow]::Title($hwnd), [Ps2nGameWindow]::Size($hwnd))

$failed = $false
$watch.Restart()
foreach ($step in $Steps) {
    $words = $step.Trim() -split '\s+'
    $wait = (Seconds $words[0]) - $watch.Elapsed.TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
    if ($process.HasExited) { Write-Host "The session ended before the step [$step]."; $failed = $true; break }
    switch ($words[1]) {
        'shot' {
            $ok = [Ps2nGameWindow]::Capture($hwnd, (Join-Path $OutDir $words[2]))
            if (-not $ok) { $failed = $true }
            Write-Host ("{0,6:N1} s: photograph {1} {2}; called [{3}], {4}" -f $watch.Elapsed.TotalSeconds, $words[2],
                $(if ($ok) { 'taken' } else { 'FAILED' }), [Ps2nGameWindow]::Title($hwnd), [Ps2nGameWindow]::Size($hwnd))
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
            [Ps2nGameWindow]::Close($hwnd)
            Write-Host ("{0,6:N1} s: the window is asked to close" -f $watch.Elapsed.TotalSeconds)
        }
        default { throw "Unknown step [$step]: shot <file>, key <key> [seconds], or close." }
    }
}

if (-not $process.WaitForExit($ExitWait * 1000)) {
    Write-Host "The session did not end within $ExitWait s of the last step; ending it."
    $process.Kill()
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

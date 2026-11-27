# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Runs a command on a desktop of its own that nobody sees.

.DESCRIPTION
A long script here starts programs that have windows (the wizard, a game's
own compile) and programs that may raise a box (an assertion of PCSX2's, an
error box of Windows'). On the screen of whoever uses the computer, a window
of those takes the keyboard away from a game being played.

This starts the command on another desktop of the same session (CreateDesktop),
where everything it starts in turn stays too, and waits for it. Every two
seconds it looks at that desktop: a ps2n-host that has a dialog box open there
would wait for a click that never comes, so it is stopped, and the log says
so. Only there: nothing on the visible desktop is touched.

Give it a whole command line (-Command), or one of this folder's scripts
(-Script, with -Arguments and -Output), which it wraps so that everything the
script says goes to -Output:

    cmd /c "powershell -NoProfile -ExecutionPolicy Bypass -File <script> <arguments> > <output> 2>&1"

Call it with & from a PowerShell that stays, not through "powershell -File".
It writes "exit <code>" when the command has ended, and returns that code.

.EXAMPLE
& scripts\windows\hidden.ps1 -Script update-games.ps1 -Arguments '-Bios C:\bios\bios.bin' -Output build\measure\update-games.log
& scripts\windows\hidden.ps1 -Command 'cmd /c "C:\somewhere\a.exe > C:\somewhere\a.log 2>&1"' -Log C:\somewhere\hidden.log
#>
param(
    # A whole command line.
    [string]$Command = '',
    # Or: one of scripts\windows, by name, or any script by its path.
    [string]$Script = '',
    # Its arguments, as they would be typed.
    [string]$Arguments = '',
    # Where everything the script says goes.
    [string]$Output = '',
    # This script's own log: what it stopped, and the exit code. Beside -Output
    # where not given.
    [string]$Log = '',
    # The desktop's name. Scripts started on the same one share it.
    [string]$Desktop = 'ps2n-measure'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
function InRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $root $path)
}

if ($Script) {
    if ($Command) { throw "Give -Command or -Script, not both." }
    if (-not $Output) { throw "A script's output needs a file: -Output <file>." }
    $file = $Script
    if (-not (Test-Path -LiteralPath $file)) { $file = Join-Path $PSScriptRoot $Script }
    if (-not (Test-Path -LiteralPath $file)) { throw "No such script: $Script" }
    $file = (Resolve-Path -LiteralPath $file).Path
    $Output = InRoot $Output
    New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null
    $Command = "cmd /c `"powershell -NoProfile -ExecutionPolicy Bypass -File `"$file`" $Arguments > `"$Output`" 2>&1`""
    if (-not $Log) { $Log = [IO.Path]::ChangeExtension($Output, $null).TrimEnd('.') + '-hidden.log' }
}
if (-not $Command) { throw "Nothing to run: -Command <command line>, or -Script <script>." }
if (-not $Log) { throw "A log is needed: -Log <file>." }
$Log = InRoot $Log
New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null

Add-Type -TypeDefinition @'
using System; using System.Text; using System.Diagnostics; using System.Runtime.InteropServices;
public static class Ps2nHidden {
  [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, uint access, IntPtr sa);
  [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desk);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  struct STARTUPINFO { public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
    public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError; }
  [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir,
    ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h, out uint code);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desk, EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static int Run(string command, string desktop, string log) {
    // GENERIC_ALL: the desktop is this process's own.
    IntPtr desk = CreateDesktop(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000u, IntPtr.Zero);
    if (desk == IntPtr.Zero) { System.IO.File.AppendAllText(log, "CreateDesktop failed " + Marshal.GetLastWin32Error() + "\n"); return -1; }
    var si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si); si.lpDesktop = "WinSta0\\" + desktop;
    PROCESS_INFORMATION pi;
    if (!CreateProcess(null, new StringBuilder(command), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, null, ref si, out pi)) {
      System.IO.File.AppendAllText(log, "CreateProcess failed " + Marshal.GetLastWin32Error() + "\n"); CloseDesktop(desk); return -1; }
    while (WaitForSingleObject(pi.hProcess, 2000) != 0) {
      EnumDesktopWindows(desk, (h, l) => {
        var c = new StringBuilder(64); GetClassName(h, c, 64);
        if (c.ToString() != "#32770") return true;    // a dialog box
        uint pid; GetWindowThreadProcessId(h, out pid);
        try {
          var p = Process.GetProcessById((int)pid);
          if (p.ProcessName == "ps2n-host") {
            var t = new StringBuilder(256); GetWindowText(h, t, 256);
            System.IO.File.AppendAllText(log, DateTime.Now.ToString("HH:mm:ss") + " a dialog of ps2n-host " + pid + " ('" + t + "'): stopped\n");
            p.Kill();
          }
        } catch (Exception) {}
        return true; }, IntPtr.Zero);
    }
    uint code; GetExitCodeProcess(pi.hProcess, out code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseDesktop(desk);
    return (int)code;
  }
}
'@

$code = [Ps2nHidden]::Run($Command, $Desktop, $Log)
Add-Content -LiteralPath $Log -Value "$(Get-Date -Format 'HH:mm:ss') exit $code"
Write-Output "exit $code"
exit $code

# Usage: send.ps1 key <scancode-hex> [holdMs]   |   send.ps1 mouse <dx> <dy> [steps]   |   send.ps1 shot <name>
param([string]$cmd, [string]$a, [string]$b, [string]$c)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Inj {
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT m; [FieldOffset(0)] public KEYBDINPUT k; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U u; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public static void Key(ushort scan, bool up) {
    var i = new INPUT[1]; i[0].type = 1; i[0].u.k.scan = scan; i[0].u.k.flags = 0x8u | (up ? 0x2u : 0u);
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Move(int dx, int dy) {
    var i = new INPUT[1]; i[0].type = 0; i[0].u.m.dx = dx; i[0].u.m.dy = dy; i[0].u.m.flags = 0x1;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
$p = Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue
if ($p) { [Inj]::SetForegroundWindow($p.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 150 }
switch ($cmd) {
  'key'   { $s = [Convert]::ToUInt16($a, 16); $hold = if ($b) { [int]$b } else { 80 }; [Inj]::Key($s, $false); Start-Sleep -Milliseconds $hold; [Inj]::Key($s, $true) }
  'mouse' { $steps = if ($c) { [int]$c } else { 10 }; for ($n = 0; $n -lt $steps; $n++) { [Inj]::Move([int]$a / $steps, [int]$b / $steps); Start-Sleep -Milliseconds 16 } }
}

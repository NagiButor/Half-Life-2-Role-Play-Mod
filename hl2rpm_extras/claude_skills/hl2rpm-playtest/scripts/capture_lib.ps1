# Window capture that works for the game window (DirectX) and VGUI menus,
# unlike the in-game "jpeg" command which is black outside of a map.
Add-Type -AssemblyName System.Drawing
if (-not ("HL2RPMWinCap" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HL2RPMWinCap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
}
# the desktop runs at 125 % DPI: without this the capture is cropped/scaled
[HL2RPMWinCap]::SetProcessDPIAware() | Out-Null

function Save-WindowCapture([IntPtr]$h, [string]$path) {
    $r = New-Object HL2RPMWinCap+RECT
    [HL2RPMWinCap]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [HL2RPMWinCap]::PrintWindow($h, $hdc, 2) | Out-Null   # 2 = PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "saved $path (${w}x${hh})"
}

<#
Drives the game UI with mouse clicks/keys and captures the window after each
action (menus, Options dialog, in-game VGUI panels).

  .\ui_clicks.ps1 -Actions "wait:25", "click:141,557", "wait:3", "shot", "click:1010,320", "wait:2", "shot"
  .\ui_clicks.ps1 -Map test_deferred -Actions "wait:40", "key:F1", "wait:2", "shot", "key:ESCAPE", "shot"

Actions (executed in order):
  wait:<sec>           sleep
  click:<x>,<y>        left click at window coordinates as seen in the captures
                       (the capture includes the title bar; top-left = 0,0)
  rclick:<x>,<y>       right click
  key:<NAME>           tap a key (VK name: F1, ESCAPE, RETURN, TAB, SPACE, A..Z, 0..9, OEM_3 = tilde)
  type:<text>          type text (ASCII)
  shot                 capture -> <OutDir>\ui_<n>.png
Always ends with a capture and closes the game. config.cfg is backed up/restored.
#>
param(
    [string[]]$Actions = @(),
    [string]$Map = "",
    [string]$Extra = "",
    [string]$OutDir = "$env:TEMP\hl2rpm_ui",
    [int]$Width = 1600,
    [int]$Height = 900
)
. "$PSScriptRoot\capture_lib.ps1"
if (-not ("HL2RPMInput" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HL2RPMInput {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char c);
}
"@
}
$mod = "E:\Steam\steamapps\sourcemods\hl2rpm"
$game = "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\hl2.exe"
if (Get-Process hl2 -ErrorAction SilentlyContinue) { throw "hl2.exe is already running" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$cfgBackup = "$env:TEMP\hl2rpm_ui_config_backup.cfg"
Copy-Item "$mod\cfg\config.cfg" $cfgBackup -Force

$mapArg = ""
if ($Map -ne "") { $mapArg = "+map $Map" }
$p = Start-Process $game -ArgumentList "-game `"$mod`" -window -w $Width -h $Height -novid -condebug $mapArg $Extra" -PassThru
Start-Sleep -Seconds 3

$vk = @{ F1 = 0x70; F2 = 0x71; F5 = 0x74; ESCAPE = 0x1B; RETURN = 0x0D; TAB = 0x09; SPACE = 0x20; OEM_3 = 0xC0;
         LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28; SHIFT = 0x10; CONTROL = 0x11 }
$n = 0
function Shot {
    $p.Refresh()
    $path = Join-Path $OutDir ("ui_{0}.png" -f $script:n)
    Save-WindowCapture $p.MainWindowHandle $path
    $script:n++
}
function WinOrigin {
    $p.Refresh()
    $r = New-Object HL2RPMWinCap+RECT
    [HL2RPMWinCap]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    [HL2RPMInput]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 200
    return $r
}
function TapKey([byte]$code) {
    [HL2RPMInput]::keybd_event($code, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
    [HL2RPMInput]::keybd_event($code, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
}

foreach ($a in $Actions) {
    if ($p.HasExited) { Write-Warning "game exited early"; break }
    $kind, $arg = $a -split ":", 2
    switch ($kind) {
        "wait"  { Start-Sleep -Milliseconds ([int]([double]$arg * 1000)) }
        "shot"  { Shot }
        { $_ -in "click", "rclick" } {
            $x, $y = $arg -split ","
            $r = WinOrigin
            [HL2RPMInput]::SetCursorPos($r.Left + [int]$x, $r.Top + [int]$y) | Out-Null
            Start-Sleep -Milliseconds 150
            if ($kind -eq "click") { $dn = 2; $up = 4 } else { $dn = 8; $up = 16 }
            [HL2RPMInput]::mouse_event($dn, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 70
            [HL2RPMInput]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 150
        }
        "key" {
            WinOrigin | Out-Null
            if ($vk.ContainsKey($arg)) { $code = $vk[$arg] } else { $code = [byte][char]$arg.ToUpper() }
            TapKey ([byte]$code)
        }
        "type" {
            WinOrigin | Out-Null
            foreach ($c in $arg.ToCharArray()) { TapKey ([byte]([HL2RPMInput]::VkKeyScan($c) -band 0xFF)) }
        }
    }
}
if (-not $p.HasExited) { Shot; Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2 }
Copy-Item $cfgBackup "$mod\cfg\config.cfg" -Force
"captures in $OutDir"

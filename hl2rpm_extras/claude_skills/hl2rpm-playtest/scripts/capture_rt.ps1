<#
Real-time window capture of a running test (no host_framerate): what the player sees
when background work (GI probes, streaming) has only wall-clock time - a `startmovie`
recording runs the game at ~6 fps and hides it.

Captures the hl2.exe window (PrintWindow, ~20 jpg/s at 960x540) between the console
lines CAPSTART and CAPEND (put `echo CAPSTART` / `echo CAPEND` into tour steps).
Frames: <OutDir>\f<n>_<ms since CAPSTART>.jpg; the ms counter restarts at every CAPSTART.

  $job = Start-Job { & run_game.ps1 -Map m -StepsFile s.txt -Width 960 -Height 540 }
  Start-Sleep 3
  & capture_rt.ps1 -OutDir C:\tmp\rt
  Receive-Job $job -Wait

Steps example (the walk lasts the step after the one that sets cl_weather_tour_step 7):
  |cl_weather_tour_step 7; setpos ...; setang ...; noclip; sv_noclipspeed 0.65
  |cl_weather_tour_step 3; setpos ...; setang ...; echo CAPSTART; +forward
  |-forward; echo CAPEND
#>
param(
    [string]$OutDir,
    [int]$MaxSec = 400,
    [string]$Start = "CAPSTART",
    [string]$Stop = "CAPEND",
    [long]$Quality = 92
)
. "$PSScriptRoot\capture_lib.ps1"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = "E:\Steam\steamapps\sourcemods\hl2rpm\console.log"
$enc = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$ep = New-Object System.Drawing.Imaging.EncoderParameters 1
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), $Quality

$sw = [Diagnostics.Stopwatch]::StartNew()
$pos = -1
$capturing = $false
$sawGame = $false
$n = 0
$iter = 0
$t0 = 0
while ($sw.Elapsed.TotalSeconds -lt $MaxSec) {
    $p = Get-Process hl2 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { if ($sawGame) { break }; Start-Sleep -Milliseconds 200; continue }
    $sawGame = $true
    if ($pos -lt 0) { $pos = (Get-Item $log).Length }
    $iter++
    # (the log is read every 4th capture while capturing: reading it costs a frame)
    if (-not $capturing -or ($iter % 4) -eq 0) {
        $len = (Get-Item $log).Length
        if ($len -gt $pos) {
            $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
            [void]$fs.Seek($pos, 0)
            $buf = New-Object byte[] ($len - $pos)
            [void]$fs.Read($buf, 0, $buf.Length)
            $fs.Close()
            $pos = $len
            $txt = [Text.Encoding]::UTF8.GetString($buf)
            if ($txt -match $Start) { $capturing = $true; $t0 = $sw.Elapsed.TotalMilliseconds }
            if ($txt -match $Stop) { $capturing = $false }
        }
    }
    if ($capturing) {
        $h = $p.MainWindowHandle
        $r = New-Object HL2RPMWinCap+RECT
        [HL2RPMWinCap]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [HL2RPMWinCap]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $ms = [int]($sw.Elapsed.TotalMilliseconds - $t0)
        $bmp.Save(("{0}\f{1:D4}_{2:D6}.jpg" -f $OutDir, $n, $ms), $enc, $ep); $bmp.Dispose()
        $n++
    } else {
        Start-Sleep -Milliseconds 40
    }
}
"captured $n frames"

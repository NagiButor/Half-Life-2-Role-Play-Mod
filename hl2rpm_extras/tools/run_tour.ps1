# HL2RPM photo tour runner: starts the mod with +cl_weather_tour_map <map> <extra args>,
# waits for it to quit, lists the screenshots and the interesting console.log lines.
# Example: .\run_tour.ps1 -Map test_deferred -Extra "+cl_weather_tour_script my_steps.txt"
# Don't pass FCVAR_ARCHIVE convars in -Extra (they end up in cfg\config.cfg).
param(
    [string]$Map = "test_deferred",
    [string]$Extra = "",
    [int]$TimeoutSec = 480
)
$mod = "E:\Steam\steamapps\sourcemods\hl2rpm"
$shots = "$mod\screenshots"
New-Item -ItemType Directory -Force $shots | Out-Null
# previous screenshots go outside the mod folder (it is zipped for releases)
$archive = "$env:TEMP\hl2rpm_tour_archive\" + (Get-Date -Format "yyyyMMdd_HHmmss")
$old = Get-ChildItem $shots -Filter *.jpg
if ($old) { New-Item -ItemType Directory -Force $archive | Out-Null; $old | Move-Item -Destination $archive }

$logLen = 0
if (Test-Path "$mod\console.log") { $logLen = (Get-Item "$mod\console.log").Length }

$game = "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\hl2.exe"
$args = "-game `"$mod`" -window -w 1600 -h 900 -novid -condebug +cl_weather_tour_map $Map $Extra"
$p = Start-Process $game -ArgumentList $args -PassThru
if ($p.WaitForExit($TimeoutSec * 1000)) { "exit code: 0x{0:X8}" -f $p.ExitCode } else { "TIMEOUT - killing"; $p.Kill() }

Get-ChildItem $shots -Filter *.jpg | ForEach-Object { "{0}  {1,8}" -f $_.Name, $_.Length }

# new console output, filtered
$fs = [IO.File]::Open("$mod\console.log", 'Open', 'Read', 'ReadWrite')
$fs.Seek($logLen, 'Begin') | Out-Null
$sr = New-Object IO.StreamReader($fs)
$text = $sr.ReadToEnd(); $sr.Close()
$text -split "`n" | Where-Object { $_ -match "(?i)error|warning|weather|unknown|fail|unexpected|not a texture|crash|assert|exception|\[tour\]" -and $_ -notmatch "(?i)cheat cvar|caption|particle function|Parent cvar|materialsphere|abovewater|defaultcubemap|mapbase_russian|motd" } | Select-Object -First 60


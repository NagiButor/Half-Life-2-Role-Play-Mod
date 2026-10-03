<#
Runs HL2RPM for an automated test and cleans up after itself.

  # photo tour over a map with scripted steps (see SKILL.md for the step format)
  .\run_game.ps1 -Map test_deferred -Steps "shot|sv_timecycle_set_time 15; sv_weather clear 0", "shot,sun|sv_weather misty 0"
  .\run_game.ps1 -Map demo_map -StepsFile my_steps.txt
  # built-in weather tour
  .\run_game.ps1 -Map test_deferred -BuiltinTour
  # just load a map, run commands, take a window capture after 40 s, then close
  .\run_game.ps1 -Map map_v11 -Extra "+sv_cheats 1" -CaptureAfter 40 -CaptureOut C:\tmp\v11.png

What it guarantees:
  - cfg\config.cfg is backed up before and restored after the run (convars
    set on the command line or by the test would otherwise stay archived);
  - old screenshots\*.jpg are moved to %TEMP%\hl2rpm_shots_archive first, so
    screenshots\ holds only this run's shots afterwards;
  - the temporary steps file in cfg\ is deleted;
  - prints the exit code, the new screenshots and the interesting new lines of
    console.log ([tour] FPS lines, errors, warnings).
Exit code 0xC0000005 on quit is the known deferred exit crash (see CLAUDE.md).
#>
param(
    [string]$Map = "test_deferred",
    [string[]]$Steps = @(),
    [string]$StepsFile = "",
    [switch]$BuiltinTour,
    [switch]$NoMap,
    [string]$Extra = "",
    [int]$TimeoutSec = 420,
    [int]$Width = 1600,
    [int]$Height = 900,
    [int]$CaptureAfter = 0,
    [string]$CaptureOut = "",
    [switch]$KeepConfig,
    [string]$LogFilter = "(?i)error|warning|unknown command|fail|crash|assert|exception|\[tour\]|\[test\]"
)

$mod = "E:\Steam\steamapps\sourcemods\hl2rpm"
$game = "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\hl2.exe"
$shots = "$mod\screenshots"
$cfg = "$mod\cfg\config.cfg"

if (Get-Process hl2 -ErrorAction SilentlyContinue) { throw "hl2.exe is already running" }

# --- config backup
$cfgBackup = "$env:TEMP\hl2rpm_config_backup_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".cfg"
if (Test-Path $cfg) { Copy-Item $cfg $cfgBackup -Force }

# --- screenshots: archive old ones
New-Item -ItemType Directory -Force $shots | Out-Null
$old = Get-ChildItem $shots -Filter *.jpg
if ($old) {
    $archive = "$env:TEMP\hl2rpm_shots_archive\" + (Get-Date -Format "yyyyMMdd_HHmmss")
    New-Item -ItemType Directory -Force $archive | Out-Null
    $old | Move-Item -Destination $archive
}

# --- steps
$tourArgs = ""
$stepsName = ""
if ($StepsFile -ne "" -or $Steps.Count -gt 0) {
    $stepsName = "_claude_steps.txt"
    $lines = @()
    if ($StepsFile -ne "") { $lines += Get-Content $StepsFile }
    $lines += $Steps
    Set-Content -Path "$mod\cfg\$stepsName" -Value $lines -Encoding ASCII
    $tourArgs = "+cl_weather_tour_map $Map +cl_weather_tour_script $stepsName"
} elseif ($BuiltinTour) {
    $tourArgs = "+cl_weather_tour_map $Map"
} elseif ($NoMap) {
    $tourArgs = ""
} else {
    $tourArgs = "+map $Map"
}

$logLen = 0
if (Test-Path "$mod\console.log") { $logLen = (Get-Item "$mod\console.log").Length }

$argsLine = "-game `"$mod`" -window -w $Width -h $Height -novid -condebug $tourArgs $Extra"
Write-Host "hl2.exe $argsLine" -ForegroundColor Cyan
$p = Start-Process $game -ArgumentList $argsLine -PassThru

if ($CaptureAfter -gt 0) {
    . "$PSScriptRoot\capture_lib.ps1"
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $CaptureAfter -and -not $p.HasExited) { Start-Sleep -Milliseconds 500 }
    if (-not $p.HasExited) {
        $p.Refresh()
        if ($CaptureOut -eq "") { $CaptureOut = "$env:TEMP\hl2rpm_capture.png" }
        Save-WindowCapture $p.MainWindowHandle $CaptureOut
        if ($tourArgs -eq "" -or $tourArgs -like "+map *") { Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2 }
    }
}

if (-not $p.HasExited) {
    if ($p.WaitForExit($TimeoutSec * 1000)) { } else { Write-Warning "TIMEOUT - killing hl2.exe"; $p.Kill(); Start-Sleep -Seconds 2 }
}
"exit code: 0x{0:X8}" -f $p.ExitCode

# --- cleanup
if ($stepsName -ne "") { Remove-Item "$mod\cfg\$stepsName" -ErrorAction SilentlyContinue }
if (-not $KeepConfig -and (Test-Path $cfgBackup)) {
    Copy-Item $cfgBackup $cfg -Force
    Remove-Item $cfgBackup
    "config.cfg restored"
}

Get-ChildItem $shots -Filter *.jpg | ForEach-Object { "{0}  {1,8}" -f $_.FullName, $_.Length }

if (Test-Path "$mod\console.log") {
    $fs = [IO.File]::Open("$mod\console.log", 'Open', 'Read', 'ReadWrite')
    if ($fs.Length -ge $logLen) { $fs.Seek($logLen, 'Begin') | Out-Null }
    $sr = New-Object IO.StreamReader($fs)
    $text = $sr.ReadToEnd(); $sr.Close()
    $text -split "`n" | Where-Object { $_ -match $LogFilter -and $_ -notmatch "(?i)cheat cvar|caption|particle function|Parent cvar|materialsphere|mapbase_russian|motd" } | Select-Object -First 80
}

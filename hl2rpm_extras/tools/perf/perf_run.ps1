<#
Runs a perf scenario through the playtest skill's run_game.ps1 and prints the per-step
profiler summary (perf_report.py).

  .\perf_run.ps1 -Map rpm_trainstation_testoptimization -StepsFile perf_trainstation.txt -Out C:\tmp\base_train.log
  .\perf_run.ps1 -Map building_test -StepsFile perf_building.txt -Extra "+r_deferred_ssao 0"

The scenario turns on r_hl2rpm_gpuprof 1 itself. -Out keeps this run's part of console.log.
#>
param(
    [Parameter(Mandatory = $true)][string]$Map,
    [Parameter(Mandatory = $true)][string]$StepsFile,
    [string]$Extra = "",
    [string[]]$Steps = @(),
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$TimeoutSec = 600,
    [string]$Out = "",
    [int]$Skip = 2
)

$rg = "E:\SourceModding\HL2RPMSOURCE\AI_instructions\skills\hl2rpm-playtest\scripts\run_game.ps1"
$log = "E:\Steam\steamapps\sourcemods\hl2rpm\console.log"
if (-not (Test-Path $StepsFile)) { $StepsFile = Join-Path $PSScriptRoot $StepsFile }

$len = 0
if (Test-Path $log) { $len = (Get-Item $log).Length }

$res = & $rg -Map $Map -StepsFile $StepsFile -Steps $Steps -Extra $Extra -Width $Width -Height $Height -TimeoutSec $TimeoutSec -LogFilter "(?i)\[gpuprof\] (D3D9|GPU timing)|error|crash|\[tour\] \d+ steps"
$res | Where-Object { $_ -notmatch '\.jpg' }

if ($Out -eq "") { $Out = Join-Path $env:TEMP ("perf_" + $Map + "_" + (Get-Date -Format "HHmmss") + ".log") }
$fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
if ($fs.Length -ge $len) { $fs.Seek($len, 'Begin') | Out-Null }
$sr = New-Object IO.StreamReader($fs)
$text = $sr.ReadToEnd(); $sr.Close()
[IO.File]::WriteAllText($Out, $text)
"log: $Out"

$env:PYTHONUTF8 = 1
python -I (Join-Path $PSScriptRoot "perf_report.py") $Out --skip $Skip

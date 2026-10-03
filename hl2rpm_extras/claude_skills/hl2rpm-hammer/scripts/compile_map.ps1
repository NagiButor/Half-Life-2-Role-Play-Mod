<#
HL2RPM map compile: vbsp -> vvis -> vrad with the Source SDK Base 2013 SP tools
(the same ones Hammer++ uses), then copies the BSP into the mod's maps folder.

  .\compile_map.ps1 -Map test_deferred                 # normal compile (fast vis, normal rad, LDR+HDR)
  .\compile_map.ps1 -Map test_deferred -Profile final  # full vis, -final rad
  .\compile_map.ps1 -Map test_deferred -Profile quick  # no vis, -fast rad (entity/layout iteration)
  .\compile_map.ps1 -Vmf "D:\path\other.vmf"           # any VMF
  .\compile_map.ps1 -Map x -RadArgs "-StaticPropLighting -StaticPropPolys"

The previous BSP in the mod is kept in E:\SourceModding\HL2RPMSOURCE\map_backups.
Exit code 0 = compiled and copied, 1 = a tool failed or the map leaked.
#>
param(
    [string]$Map = "",
    [string]$Vmf = "",
    [ValidateSet("quick", "normal", "final")] [string]$Profile = "normal",
    [string]$VbspArgs = "",
    [string]$VvisArgs = "",
    [string]$RadArgs = "",
    [switch]$NoCopy,
    [switch]$AllowLeak
)

$ErrorActionPreference = "Stop"
$bin = "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin"
$game = "E:\Steam\steamapps\sourcemods\hl2rpm"
$mapsrc = "E:\XBLAH's modding tool\content\Mapbase\hl2rpm\mapsrc"
$backupDir = "E:\SourceModding\HL2RPMSOURCE\map_backups"

if ($Vmf -eq "") {
    if ($Map -eq "") { throw "pass -Map <name> or -Vmf <path>" }
    $Vmf = Join-Path $mapsrc "$Map.vmf"
}
if (-not (Test-Path $Vmf)) { throw "VMF not found: $Vmf" }
$Vmf = (Resolve-Path $Vmf).Path
$name = [IO.Path]::GetFileNameWithoutExtension($Vmf)
$base = Join-Path ([IO.Path]::GetDirectoryName($Vmf)) $name
$bspOut = "$base.bsp"

if (Get-Process hl2 -ErrorAction SilentlyContinue) {
    Write-Warning "hl2.exe is running: the copy into the mod may fail if this map is loaded"
}

switch ($Profile) {
    "quick"  { $vis = $null;   $rad = "-fast -both" }
    "normal" { $vis = "-fast"; $rad = "-both" }
    "final"  { $vis = "";      $rad = "-final -both -StaticPropLighting -StaticPropPolys -TextureShadows" }
}

function Run-Tool([string]$exe, [string]$toolArgs) {
    $full = Join-Path $bin $exe
    Write-Host "==> $exe $toolArgs" -ForegroundColor Cyan
    $p = Start-Process -FilePath $full -ArgumentList $toolArgs -WorkingDirectory $bin -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$env:TEMP\hl2rpm_compile_out.txt" -RedirectStandardError "$env:TEMP\hl2rpm_compile_err.txt"
    $out = Get-Content "$env:TEMP\hl2rpm_compile_out.txt" -Raw -ErrorAction SilentlyContinue
    $err = Get-Content "$env:TEMP\hl2rpm_compile_err.txt" -Raw -ErrorAction SilentlyContinue
    # keep the console readable: warnings/errors and the timing lines
    ($out + $err) -split "`n" | Where-Object {
        $_ -match "(?i)error|warning|leak|\*\*\*|elapsed|seconds elapsed|too many|exceeded|bad|invalid"
    } | Select-Object -First 40 | ForEach-Object { Write-Host "    $_" }
    return @{ Code = $p.ExitCode; Text = ($out + $err) }
}

$start = Get-Date
$r = Run-Tool "vbsp.exe" "-game `"$game`" $VbspArgs `"$base`""
if ($r.Code -ne 0) { Write-Error "vbsp failed ($($r.Code))"; exit 1 }
$leaked = $r.Text -match "(?i)\*\*\*\* leaked \*\*\*\*"
if ($leaked) {
    Write-Warning "MAP LEAKED (see $base.lin / pointfile). vvis will not run."
    if (-not $AllowLeak) { exit 1 }
}

if ($vis -ne $null -and -not $leaked) {
    $r = Run-Tool "vvis.exe" "-game `"$game`" $vis $VvisArgs `"$base`""
    if ($r.Code -ne 0) { Write-Error "vvis failed ($($r.Code))"; exit 1 }
}

$r = Run-Tool "vrad.exe" "-game `"$game`" $rad $RadArgs `"$base`""
if ($r.Code -ne 0) { Write-Error "vrad failed ($($r.Code))"; exit 1 }

$secs = [int]((Get-Date) - $start).TotalSeconds
Write-Host "compiled $name in $secs s -> $bspOut" -ForegroundColor Green

if (-not $NoCopy) {
    $dest = Join-Path $game "maps\$name.bsp"
    if (Test-Path $dest) {
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        Copy-Item $dest (Join-Path $backupDir "$name`_$stamp.bsp")
    }
    Copy-Item $bspOut $dest -Force
    Write-Host "copied to $dest" -ForegroundColor Green
}
exit 0

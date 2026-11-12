<#
HL2RPM map compile: vbsp -> vvis -> vrad, then copies the BSP into the mod's maps folder.
The tools are the mod's own (hl2rpm_vbsp/vvis/vrad in the SDK's bin, built from sp\src\utils by
hl2rpm_extras\tools\install_compilers.ps1; Hammer++ runs the same ones). -Tools stock = Valve's.

  .\compile_map.ps1 -Map test_deferred                 # normal: quick vis, flat lightmaps (LDR+HDR)
  .\compile_map.ps1 -Map test_deferred -Profile final  # full vis, baked -final rad (for -nodeferred)
  .\compile_map.ps1 -Map test_deferred -Profile quick  # no vis (entity/layout iteration)
  .\compile_map.ps1 -Vmf "D:\path\other.vmf"           # any VMF
  .\compile_map.ps1 -Map x -Tools stock                # Valve's tools (normal: vvis -fast, vrad baked)

The previous BSP in the mod is kept in E:\SourceModding\HL2RPMSOURCE\map_backups.
Exit code 0 = compiled and copied, 1 = a tool failed or the map leaked.
#>
param(
    [string]$Map = "",
    [string]$Vmf = "",
    [ValidateSet("quick", "normal", "final")] [string]$Profile = "normal",
    [ValidateSet("rpm", "stock")] [string]$Tools = "rpm",
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
    # test / developer maps live in mapsrc\test (and compile into hl2rpm\testmaps\maps)
    if (-not (Test-Path $Vmf)) { $Vmf = Join-Path $mapsrc "test\$Map.vmf" }
}
if (-not (Test-Path $Vmf)) { throw "VMF not found: $Vmf" }
$Vmf = (Resolve-Path $Vmf).Path
$name = [IO.Path]::GetFileNameWithoutExtension($Vmf)
$base = Join-Path ([IO.Path]::GetDirectoryName($Vmf)) $name
$bspOut = "$base.bsp"

if (Get-Process hl2 -ErrorAction SilentlyContinue) {
    Write-Warning "hl2.exe is running: the copy into the mod may fail if this map is loaded"
}

if ($Tools -eq "rpm") {
    # hl2rpm_vvis: quick portal vis unless -full; hl2rpm_vrad: flat lightmaps unless -full / -final
    $exe = @{ bsp = "hl2rpm_vbsp.exe"; vis = "hl2rpm_vvis.exe"; rad = "hl2rpm_vrad.exe" }
    switch ($Profile) {
        "quick"  { $vis = $null;   $rad = "-both" }
        "normal" { $vis = "";      $rad = "-both" }
        "final"  { $vis = "-full"; $rad = "-final -both -StaticPropLighting -StaticPropPolys -TextureShadows" }
    }
} else {
    $exe = @{ bsp = "vbsp.exe"; vis = "vvis.exe"; rad = "vrad.exe" }
    switch ($Profile) {
        "quick"  { $vis = $null;   $rad = "-fast -both" }
        "normal" { $vis = "-fast"; $rad = "-both" }
        "final"  { $vis = "";      $rad = "-final -both -StaticPropLighting -StaticPropPolys -TextureShadows" }
    }
}
foreach ($t in $exe.Values) {
    if (-not (Test-Path (Join-Path $bin $t))) { throw "$t is not in $bin (run hl2rpm_extras\tools\install_compilers.ps1, or pass -Tools stock)" }
}

$runId = [guid]::NewGuid().ToString("N").Substring(0, 8)   # (two compiles at once must not share the capture files)
function Run-Tool([string]$exe, [string]$toolArgs) {
    $full = Join-Path $bin $exe
    Write-Host "==> $exe $toolArgs" -ForegroundColor Cyan
    $outFile = "$env:TEMP\hl2rpm_compile_out_$runId.txt"
    $errFile = "$env:TEMP\hl2rpm_compile_err_$runId.txt"
    $p = Start-Process -FilePath $full -ArgumentList $toolArgs -WorkingDirectory $bin -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $out = Get-Content $outFile -Raw -ErrorAction SilentlyContinue
    $err = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
    Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    # keep the console readable: warnings/errors and the timing lines
    ($out + $err) -split "`n" | Where-Object {
        $_ -match "(?i)error|warning|leak|\*\*\*|elapsed|seconds elapsed|too many|exceeded|bad|invalid|assert|nearly full|t-junction"
    } | Select-Object -First 40 | ForEach-Object { Write-Host "    $_" }
    return @{ Code = $p.ExitCode; Text = ($out + $err) }
}

$start = Get-Date
$r = Run-Tool $exe.bsp "-game `"$game`" $VbspArgs `"$base`""
if ($r.Code -ne 0) { Write-Error "vbsp failed ($($r.Code))"; exit 1 }
$leaked = $r.Text -match "(?i)\*\*\*\* leaked \*\*\*\*"
if ($leaked) {
    Write-Warning "MAP LEAKED (see $base.lin / pointfile). vvis will not run."
    if (-not $AllowLeak) { exit 1 }
}

if ($vis -ne $null -and -not $leaked) {
    $r = Run-Tool $exe.vis "-game `"$game`" $vis $VvisArgs `"$base`""
    if ($r.Code -ne 0) { Write-Error "vvis failed ($($r.Code))"; exit 1 }
}

$r = Run-Tool $exe.rad "-game `"$game`" $rad $RadArgs `"$base`""
if ($r.Code -ne 0) { Write-Error "vrad failed ($($r.Code))"; exit 1 }

$secs = [int]((Get-Date) - $start).TotalSeconds
Write-Host "compiled $name in $secs s -> $bspOut" -ForegroundColor Green

if (-not $NoCopy) {
    # game maps -> hl2rpm\maps; test maps (VMF in mapsrc\test or BSP already in testmaps) -> hl2rpm\testmaps\maps
    $isTest = ($Vmf -like (Join-Path $mapsrc "test\*")) -or (Test-Path (Join-Path $game "testmaps\maps\$name.bsp"))
    if ($isTest) {
        New-Item -ItemType Directory -Force (Join-Path $game "testmaps\maps") | Out-Null
        $dest = Join-Path $game "testmaps\maps\$name.bsp"
        $stale = Join-Path $game "maps\$name.bsp"
        if (Test-Path $stale) { Write-Warning "a copy in hl2rpm\maps shadows the test map: $stale" }
    } else {
        $dest = Join-Path $game "maps\$name.bsp"
    }
    if (Test-Path $dest) {
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        Copy-Item $dest (Join-Path $backupDir "$name`_$stamp.bsp")
    }
    Copy-Item $bspOut $dest -Force
    Write-Host "copied to $dest" -ForegroundColor Green
}
exit 0

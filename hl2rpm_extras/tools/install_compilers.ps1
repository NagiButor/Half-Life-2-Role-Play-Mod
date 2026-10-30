<#
HL2RPM map compilers (a fork of the Mapbase vbsp / vvis / vrad in sp\src\utils): build and install.

  .\install_compilers.ps1            # build Release, then install
  .\install_compilers.ps1 -NoBuild   # only install what sp\game\bin already has

The tools must live in the SDK's bin folder (they load its steam.dll, filesystem_stdio.dll...).
They go there beside the stock ones, under their own names: hl2rpm_vbsp.exe,
hl2rpm_vvis.exe + hl2rpm_vvis_dll.dll, hl2rpm_vrad.exe + hl2rpm_vrad_dll.dll (the Mapbase
launchers load "<their own name>_dll.dll"). The stock vbsp / vvis / vrad are not touched.
#>
param([switch]$NoBuild)

$ErrorActionPreference = "Stop"
$src = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path          # sp\src
$utils = Join-Path $src "utils"
$built = (Resolve-Path (Join-Path $src "..\game\bin")).Path           # where the projects' PostBuild puts them
$sdkBin = "E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin"
$msb = "E:\Programs\Microsoft Visual Studio\Community 2022\MSBuild\Current\Bin\MSBuild.exe"

if (-not $NoBuild) {
    foreach ($p in "vbsp\vbsp.vcxproj", "vvis\vvis_dll.vcxproj", "vvis_launcher\vvis_launcher.vcxproj",
                   "vrad\vrad_dll.vcxproj", "vrad_launcher\vrad_launcher.vcxproj") {
        Write-Host "==> building $p" -ForegroundColor Cyan
        & $msb (Join-Path $utils $p) /p:Configuration=Release /p:Platform=Win32 /m /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw "build failed: $p" }
    }
}

foreach ($tool in "hl2rpm_vbsp.exe", "hl2rpm_vvis.exe", "hl2rpm_vrad.exe") {
    if (Get-Process ([IO.Path]::GetFileNameWithoutExtension($tool)) -ErrorAction SilentlyContinue) {
        throw "$tool is running (a map is being compiled): wait for it to finish"
    }
}

$names = [ordered]@{
    "vbsp.exe"     = "hl2rpm_vbsp.exe"
    "vvis.exe"     = "hl2rpm_vvis.exe"
    "vvis_dll.dll" = "hl2rpm_vvis_dll.dll"
    "vrad.exe"     = "hl2rpm_vrad.exe"
    "vrad_dll.dll" = "hl2rpm_vrad_dll.dll"
}
foreach ($from in $names.Keys) {
    $to = Join-Path $sdkBin $names[$from]
    Copy-Item (Join-Path $built $from) $to -Force
    Write-Host ("installed {0,-20} {1:yyyy-MM-dd HH:mm}" -f $names[$from], (Get-Item $to).LastWriteTime)
}

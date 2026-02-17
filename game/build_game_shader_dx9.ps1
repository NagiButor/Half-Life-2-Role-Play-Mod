[CmdletBinding()]
param(
	[ValidateSet("hl2","episodic")]
	[string]$Game = "episodic",

	[ValidateSet("Debug","Release")]
	[string]$Configuration = "Release",

	[ValidateSet("Win32","x64")]
	[string]$Platform = "Win32",

	[string]$ModDir = "e:\Steam\steamapps\sourcemods\hl2rpm"
)

$ErrorActionPreference = "Stop"

$gameDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcRoot = Split-Path -Parent $gameDir
$projDir = Join-Path $srcRoot "materialsystem\stdshaders"

$projFile = if ($Game -eq "hl2") {
	Join-Path $projDir "game_shader_dx9_hl2.vcxproj"
} else {
	Join-Path $projDir "game_shader_dx9_episodic.vcxproj"
}

if (-not (Test-Path $projFile)) {
	throw "Project not found: $projFile"
}

$msbuildCandidates = @(
	"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
	"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $msbuildCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $msbuild) {
	throw @"
MSBuild.exe not found.
Install Visual Studio Build Tools 2022 with workload:
  Desktop development with C++ (Microsoft.VisualStudio.Workload.VCTools)

Then re-run:
  powershell -ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`"
"@
}

Write-Host "Building: $projFile"
Write-Host "MSBuild : $msbuild"

& $msbuild $projFile /m /t:Build /p:Configuration=$Configuration /p:Platform=$Platform

$builtDll = Join-Path $projDir "$Configuration`_dx9_mod_$Game\game_shader_dx9.dll"
if (-not (Test-Path $builtDll)) {
	throw "Build succeeded but DLL not found: $builtDll"
}

$modBin = Join-Path $ModDir "bin"
New-Item -ItemType Directory -Force $modBin | Out-Null

$dstDll = Join-Path $modBin "game_shader_dx9.dll"
Copy-Item -Force $builtDll $dstDll

Write-Host "Copied -> $dstDll"


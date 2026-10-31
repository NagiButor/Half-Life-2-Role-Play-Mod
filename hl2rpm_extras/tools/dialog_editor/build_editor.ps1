# Builds "HL2RPM Creation Kit.exe" (PyInstaller, one folder) into the install folder.
#   powershell -File build_editor.ps1 [-Out <folder>] [-BuildDir <folder>]
# The venv with PySide6/numpy/PyInstaller is created in BuildDir on the first run.
param(
    [string]$Out = "E:\SourceModding\HL2RPMSOURCE\HL2RPM Creation Kit",
    [string]$BuildDir = "E:\SourceModding\HL2RPMSOURCE\dialog_editor_build"
)
$ErrorActionPreference = "Stop"
$src = $PSScriptRoot
$venv = Join-Path $BuildDir "venv"
$py = Join-Path $venv "Scripts\python.exe"
New-Item -ItemType Directory -Force $BuildDir | Out-Null
if (-not (Test-Path $py)) {
    python -m venv $venv
}
& $py -m pip install --disable-pip-version-check -q -r (Join-Path $src "requirements.txt")
$work = Join-Path $BuildDir "work"
$dist = Join-Path $BuildDir "dist"
$ico = Join-Path $BuildDir "editor.ico"
& $py (Join-Path $src "make_icon.py") $ico
& $py -m PyInstaller --noconfirm --clean --windowed --name "HL2RPM Creation Kit" `
    --icon $ico --distpath $dist --workpath $work --specpath $work `
    --paths $src `
    --hidden-import dlgedit.lipsync_gen.vcd_english --hidden-import dlgedit.lipsync_gen.vcd_russian `
    --hidden-import OpenGL.platform.win32 --collect-submodules OpenGL.arrays `
    --exclude-module tkinter --exclude-module matplotlib `
    (Join-Path $src "hl2rpm_creation_kit.py")
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed" }
$built = Join-Path $dist "HL2RPM Creation Kit"
if (Test-Path $Out) {
    Get-Process "HL2RPM Creation Kit" -ErrorAction SilentlyContinue | ForEach-Object { throw "Close the running Creation Kit first" }
    Remove-Item -Recurse -Force $Out
}
Copy-Item -Recurse $built $Out
Copy-Item (Join-Path $src "README.md") (Join-Path $Out "README.md")
Write-Host "Built: $Out\HL2RPM Creation Kit.exe"

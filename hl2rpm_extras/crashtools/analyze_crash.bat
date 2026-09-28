@echo off
rem ==========================================================================
rem  HL2RPM crash analysis.
rem  Usage: analyze_crash.bat <crash.dmp> [crash.txt]
rem  Runs cdb (Windows SDK Debugging Tools) on a minidump written by the
rem  in-game crash debugger and saves <dump>.analysis.txt next to it.
rem  Started automatically by the game after a crash; can be re-run by hand
rem  (or drag a .dmp file onto this .bat).
rem ==========================================================================
setlocal
set "DUMP=%~1"
if "%DUMP%"=="" (
	echo Usage: analyze_crash.bat ^<crash.dmp^>
	pause
	exit /b 1
)
set "OUT=%~dpn1.analysis.txt"
for %%I in ("%~dp0..") do set "MOD=%%~fI"
for %%I in ("%MOD%\..\..\common\Source SDK Base 2013 Singleplayer\bin") do set "GAMEBIN=%%~fI"

set "CDB="
if exist "%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x86\cdb.exe" set "CDB=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x86\cdb.exe"
if not defined CDB if exist "%ProgramFiles%\Windows Kits\10\Debuggers\x86\cdb.exe" set "CDB=%ProgramFiles%\Windows Kits\10\Debuggers\x86\cdb.exe"
if not defined CDB (
	echo cdb.exe not found. Install "Debugging Tools for Windows" from the Windows SDK. > "%OUT%"
	goto open
)

rem Mod PDBs (bin), then Microsoft public symbols (cached locally).
set "SYMS=%MOD%\bin;srv*%LOCALAPPDATA%\hl2rpm_symcache*https://msdl.microsoft.com/download/symbols"
set "IMGS=%MOD%\bin;%GAMEBIN%"

echo Analyzing %DUMP% ... > "%OUT%"
"%CDB%" -z "%DUMP%" -y "%SYMS%" -i "%IMGS%" -lines -c ".echo ===== EXCEPTION =====;.ecxr;r;.echo;.echo ===== CALL STACK (crashing thread) =====;kvn 64;.echo;.echo ===== !analyze =====;!analyze -v;.echo;.echo ===== ALL THREADS =====;~*kn 30;.echo;.echo ===== MOD MODULES =====;lmvm client;lmvm server;lmvm game_shader_dx9;q" > "%OUT%" 2>&1

:open
if "%RPM_NO_OPEN%"=="" start "" notepad "%OUT%"
endlocal

@echo off
rem ==========================================================================
rem  HL2RPM: launch the game under cdb (fallback crash catcher).
rem  Use when the in-game crash debugger does not produce a report
rem  (e.g. the crash happens before client.dll loads, or kills the process
rem  instantly). Steam must be running.
rem
rem  On a real (second-chance) crash cdb prints the stack into
rem  crashes\debugger_<time>.log and writes a full dump crashes\debugger_*.dmp,
rem  then closes the game. Extra arguments are passed to hl2.exe.
rem ==========================================================================
setlocal
for %%I in ("%~dp0..") do set "MOD=%%~fI"
for %%I in ("%MOD%\..\..\common\Source SDK Base 2013 Singleplayer") do set "GAMEDIR=%%~fI"

set "CDB="
if exist "%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x86\cdb.exe" set "CDB=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x86\cdb.exe"
if not defined CDB if exist "%ProgramFiles%\Windows Kits\10\Debuggers\x86\cdb.exe" set "CDB=%ProgramFiles%\Windows Kits\10\Debuggers\x86\cdb.exe"
if not defined CDB (
	echo cdb.exe not found. Install "Debugging Tools for Windows" from the Windows SDK.
	pause
	exit /b 1
)

if not exist "%MOD%\crashes" mkdir "%MOD%\crashes"
for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"') do set "STAMP=%%T"
set "LOG=%MOD%\crashes\debugger_%STAMP%.log"
set "ONCRASH=%TEMP%\hl2rpm_oncrash.txt"
set "INIT=%TEMP%\hl2rpm_dbginit.txt"

rem Commands run when a crash reaches second chance (nobody handled it).
> "%ONCRASH%" (
	echo .echo ===== HL2RPM: CRASH =====
	echo .ecxr
	echo r
	echo kvn 64
	echo .echo ===== ALL THREADS =====
	echo ~*kn 30
	echo .dump /ma /u "%MOD%\crashes\debugger.dmp"
	echo q
)

rem First chance is ignored (engine/drivers may handle those themselves);
rem only second-chance exceptions run the crash script.
> "%INIT%" (
	echo .sympath %MOD%\bin;srv*%LOCALAPPDATA%\hl2rpm_symcache*https://msdl.microsoft.com/download/symbols
	echo sxd -c2 "$$>< %ONCRASH%" av
	echo sxd -c2 "$$>< %ONCRASH%" sov
	echo sxd -c2 "$$>< %ONCRASH%" ii
	echo sxd -c2 "$$>< %ONCRASH%" dz
	echo sxd -c2 "$$>< %ONCRASH%" c0000374
	echo sxi ld
	echo sxi ud
	echo g
)

echo Log: %LOG%
"%CDB%" -G -g -lines -logo "%LOG%" -cf "%INIT%" "%GAMEDIR%\hl2.exe" -game "%MOD%" %*
echo.
echo Game exited. Log: %LOG%
pause
endlocal

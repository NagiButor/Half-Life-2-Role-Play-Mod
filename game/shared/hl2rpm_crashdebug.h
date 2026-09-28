//========= HL2RPM ============================================================//
//
// Purpose: Crash debugger.
//
//  - Breadcrumbs: a process-wide ring buffer of recent game events. Both
//    client.dll and server.dll write into the same buffer (named shared
//    memory), so a crash report shows what happened right before the crash
//    regardless of which DLL it came from.
//  - Crash handler (installed by client.dll): on a fatal exception writes
//    <mod>/crashes/crash_<time>.dmp + .txt (symbolized stack, registers,
//    modules, breadcrumbs, last console lines) and starts
//    <mod>/crashtools/analyze_crash.bat on the dump (cdb analysis).
//
// This header is plain C++ and safe to include anywhere.
//
//=============================================================================//

#ifndef HL2RPM_CRASHDEBUG_H
#define HL2RPM_CRASHDEBUG_H
#pragma once

// Record a breadcrumb (printf-style, truncated to ~110 chars). Cheap; thread-safe.
void RPMCrash_Crumb( const char *pFmt, ... );

// Record a line of console output (used by the client spew hook).
void RPMCrash_ConsoleText( const char *pText );

// Crash handler control (client.dll only).
// pszModDir: absolute path to the mod directory (where gameinfo.txt is).
bool RPMCrash_InstallHandler( const char *pszModDir );
void RPMCrash_RemoveHandler();
bool RPMCrash_IsHandlerInstalled();

// Print the last nCount breadcrumbs through pfnPrint (one line per call).
void RPMCrash_DumpCrumbs( void (*pfnPrint)( const char *pLine ), int nCount );

// Write a dump + report right now without crashing (for testing the pipeline).
void RPMCrash_WriteManualReport();

#define RPM_CRUMB( ... ) RPMCrash_Crumb( __VA_ARGS__ )

#endif // HL2RPM_CRASHDEBUG_H

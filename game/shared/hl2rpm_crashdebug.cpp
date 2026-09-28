//========= HL2RPM ============================================================//
//
// Purpose: Crash debugger core (see hl2rpm_crashdebug.h).
//
// Plain Win32, deliberately independent of the Source headers so it can be
// compiled without the precompiled header and used from any code path,
// including the crash itself. Compiled into both client.dll and server.dll;
// only client.dll installs the exception handler.
//
//=============================================================================//

// The game projects poison these CRT names via /D to force the Q_ versions; this
// file intentionally stays independent of tier1, so use the CRT directly.
#undef strncpy
#undef _snprintf
#undef fopen
#undef sprintf

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "hl2rpm_crashdebug.h"

#if defined( CLIENT_DLL )
#define RPM_DLL_TAG 'C'
#elif defined( GAME_DLL )
#define RPM_DLL_TAG 'S'
#else
#define RPM_DLL_TAG '?'
#endif

//-----------------------------------------------------------------------------
// Shared (process-wide) state
//-----------------------------------------------------------------------------
#define RPM_SHARED_MAGIC	0x52504D31	// 'RPM1'
#define RPM_CRUMB_COUNT		512
#define RPM_CRUMB_LEN		119
#define RPM_CONSOLE_COUNT	128
#define RPM_CONSOLE_LEN		248
#define RPM_INFO_COUNT		8
#define RPM_INFO_LEN		120

struct RPMCrumb_t
{
	DWORD	tick;
	DWORD	thread;
	char	dll;
	char	text[RPM_CRUMB_LEN];
};

struct RPMShared_t
{
	LONG			magic;
	volatile LONG	nCrumbs;
	volatile LONG	nConsole;
	RPMCrumb_t		crumbs[RPM_CRUMB_COUNT];
	char			console[RPM_CONSOLE_COUNT][RPM_CONSOLE_LEN];
};

static RPMShared_t *volatile s_pShared = NULL;

static RPMShared_t *GetShared()
{
	if ( s_pShared )
		return s_pShared;

	char szName[64];
	_snprintf( szName, sizeof( szName ), "Local\\hl2rpm_crashdbg_%lu", GetCurrentProcessId() );
	szName[sizeof( szName ) - 1] = 0;

	// Creates the mapping, or opens it if the other DLL already did. The handle is
	// intentionally never closed: the memory must outlive either DLL.
	HANDLE hMap = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof( RPMShared_t ), szName );
	if ( !hMap )
		return NULL;

	RPMShared_t *pShared = (RPMShared_t *)MapViewOfFile( hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof( RPMShared_t ) );
	if ( !pShared )
		return NULL;

	InterlockedCompareExchange( &pShared->magic, RPM_SHARED_MAGIC, 0 );
	if ( InterlockedCompareExchangePointer( (PVOID volatile *)&s_pShared, pShared, NULL ) != NULL )
		UnmapViewOfFile( pShared );	// another thread won the race

	return s_pShared;
}

//-----------------------------------------------------------------------------
// Breadcrumbs / console
//-----------------------------------------------------------------------------
void RPMCrash_Crumb( const char *pFmt, ... )
{
	RPMShared_t *pShared = GetShared();
	if ( !pShared )
		return;

	char szText[RPM_CRUMB_LEN];
	va_list args;
	va_start( args, pFmt );
	_vsnprintf( szText, sizeof( szText ), pFmt, args );
	va_end( args );
	szText[sizeof( szText ) - 1] = 0;

	LONG nIndex = InterlockedIncrement( &pShared->nCrumbs ) - 1;
	RPMCrumb_t &crumb = pShared->crumbs[(DWORD)nIndex % RPM_CRUMB_COUNT];
	crumb.tick = GetTickCount();
	crumb.thread = GetCurrentThreadId();
	crumb.dll = RPM_DLL_TAG;
	memcpy( crumb.text, szText, sizeof( szText ) );
}

void RPMCrash_ConsoleText( const char *pText )
{
	RPMShared_t *pShared = GetShared();
	if ( !pShared || !pText || !pText[0] )
		return;

	LONG nIndex = InterlockedIncrement( &pShared->nConsole ) - 1;
	char *pLine = pShared->console[(DWORD)nIndex % RPM_CONSOLE_COUNT];
	strncpy( pLine, pText, RPM_CONSOLE_LEN - 1 );
	pLine[RPM_CONSOLE_LEN - 1] = 0;

	// Strip trailing newlines; the report adds its own.
	size_t nLen = strlen( pLine );
	while ( nLen > 0 && ( pLine[nLen - 1] == '\n' || pLine[nLen - 1] == '\r' ) )
		pLine[--nLen] = 0;
}

static void FormatCrumb( const RPMCrumb_t &crumb, DWORD nNowTick, char *pOut, size_t nOutSize )
{
	char szText[RPM_CRUMB_LEN];
	memcpy( szText, crumb.text, sizeof( szText ) );
	szText[sizeof( szText ) - 1] = 0;

	_snprintf( pOut, nOutSize, "[-%6lu ms] [%c] [tid %5lu] %s",
		nNowTick - crumb.tick, crumb.dll ? crumb.dll : '?', crumb.thread, szText );
	pOut[nOutSize - 1] = 0;
}

void RPMCrash_DumpCrumbs( void ( *pfnPrint )( const char *pLine ), int nCount )
{
	RPMShared_t *pShared = GetShared();
	if ( !pShared || !pfnPrint )
		return;

	LONG nTotal = pShared->nCrumbs;
	if ( nCount > RPM_CRUMB_COUNT )
		nCount = RPM_CRUMB_COUNT;
	if ( nCount > nTotal )
		nCount = nTotal;

	DWORD nNow = GetTickCount();
	char szLine[256];
	for ( LONG i = nTotal - nCount; i < nTotal; i++ )
	{
		FormatCrumb( pShared->crumbs[(DWORD)i % RPM_CRUMB_COUNT], nNow, szLine, sizeof( szLine ) );
		pfnPrint( szLine );
	}
}

//-----------------------------------------------------------------------------
// Crash handler
//-----------------------------------------------------------------------------
typedef BOOL ( WINAPI *MiniDumpWriteDump_t )( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );
typedef BOOL ( WINAPI *SymInitialize_t )( HANDLE, PCSTR, BOOL );
typedef BOOL ( WINAPI *SymCleanup_t )( HANDLE );
typedef DWORD ( WINAPI *SymSetOptions_t )( DWORD );
typedef BOOL ( WINAPI *SymFromAddr_t )( HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO );
typedef BOOL ( WINAPI *SymGetLineFromAddr64_t )( HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64 );
typedef BOOL ( WINAPI *StackWalk64_t )( DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64 );

static struct
{
	HMODULE						hModule;
	MiniDumpWriteDump_t			MiniDumpWriteDump;
	SymInitialize_t				SymInitialize;
	SymCleanup_t				SymCleanup;
	SymSetOptions_t				SymSetOptions;
	SymFromAddr_t				SymFromAddr;
	SymGetLineFromAddr64_t		SymGetLineFromAddr64;
	StackWalk64_t				StackWalk64;
	PFUNCTION_TABLE_ACCESS_ROUTINE64	SymFunctionTableAccess64;
	PGET_MODULE_BASE_ROUTINE64			SymGetModuleBase64;
} s_Dbg;

#define RPM_MAX_REPORTS 3

static PVOID			s_hVeh = NULL;
static HANDLE			s_hWorker = NULL;
static HANDLE			s_hStart = NULL;
static HANDLE			s_hDone = NULL;
static DWORD			s_nWorkerThreadId = 0;
static volatile LONG	s_bBusy = 0;
static volatile LONG	s_nReports = 0;
static volatile bool	s_bQuit = false;
static char				s_szModDir[MAX_PATH];

// Job handed to the worker thread
static EXCEPTION_POINTERS	*s_pJobException = NULL;
static DWORD				s_nJobThreadId = 0;
static bool					s_bJobManual = false;

static bool LoadDbgHelp()
{
	if ( s_Dbg.hModule )
		return true;

	HMODULE h = LoadLibraryA( "dbghelp.dll" );
	if ( !h )
		return false;

	s_Dbg.MiniDumpWriteDump			= (MiniDumpWriteDump_t)GetProcAddress( h, "MiniDumpWriteDump" );
	s_Dbg.SymInitialize				= (SymInitialize_t)GetProcAddress( h, "SymInitialize" );
	s_Dbg.SymCleanup				= (SymCleanup_t)GetProcAddress( h, "SymCleanup" );
	s_Dbg.SymSetOptions				= (SymSetOptions_t)GetProcAddress( h, "SymSetOptions" );
	s_Dbg.SymFromAddr				= (SymFromAddr_t)GetProcAddress( h, "SymFromAddr" );
	s_Dbg.SymGetLineFromAddr64		= (SymGetLineFromAddr64_t)GetProcAddress( h, "SymGetLineFromAddr64" );
	s_Dbg.StackWalk64				= (StackWalk64_t)GetProcAddress( h, "StackWalk64" );
	s_Dbg.SymFunctionTableAccess64	= (PFUNCTION_TABLE_ACCESS_ROUTINE64)GetProcAddress( h, "SymFunctionTableAccess64" );
	s_Dbg.SymGetModuleBase64		= (PGET_MODULE_BASE_ROUTINE64)GetProcAddress( h, "SymGetModuleBase64" );
	s_Dbg.hModule = h;
	return true;
}

static bool IsFatalException( DWORD nCode )
{
	switch ( nCode )
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
	case 0xC0000374:	// STATUS_HEAP_CORRUPTION
	case 0xC0000409:	// STATUS_STACK_BUFFER_OVERRUN
	case 0x40000015:	// STATUS_FATAL_APP_EXIT (abort())
	// tier0 Error() / DebuggerBreak() end in int 3. The handler only runs without a
	// debugger attached, and then a breakpoint always kills the process.
	case EXCEPTION_BREAKPOINT:
		return true;
	}
	return false;
}

static const char *ExceptionName( DWORD nCode )
{
	switch ( nCode )
	{
	case 0:									return "MANUAL_REPORT (rpm_crash_report, no crash)";
	case EXCEPTION_ACCESS_VIOLATION:		return "ACCESS_VIOLATION";
	case EXCEPTION_STACK_OVERFLOW:			return "STACK_OVERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:		return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_PRIV_INSTRUCTION:		return "PRIV_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:	return "ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_IN_PAGE_ERROR:			return "IN_PAGE_ERROR";
	case EXCEPTION_DATATYPE_MISALIGNMENT:	return "DATATYPE_MISALIGNMENT";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
	case 0xC0000374:						return "HEAP_CORRUPTION";
	case 0xC0000409:						return "STACK_BUFFER_OVERRUN";
	case 0x40000015:						return "FATAL_APP_EXIT (abort)";
	case EXCEPTION_BREAKPOINT:				return "BREAKPOINT (engine Error()/Assert - see the message in 'Last console output')";
	}
	return "UNKNOWN";
}

// "client.dll+0x1234" for any address
static void DescribeAddress( DWORD64 nAddr, char *pOut, size_t nOutSize )
{
	HMODULE hMod = NULL;
	char szPath[MAX_PATH] = "";
	if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)nAddr, &hMod ) && hMod )
	{
		GetModuleFileNameA( hMod, szPath, sizeof( szPath ) );
		const char *pBase = strrchr( szPath, '\\' );
		pBase = pBase ? pBase + 1 : szPath;
		_snprintf( pOut, nOutSize, "%s+0x%llx", pBase, nAddr - (DWORD64)(UINT_PTR)hMod );
	}
	else
	{
		_snprintf( pOut, nOutSize, "<no module>" );
	}
	pOut[nOutSize - 1] = 0;
}

static void DescribeSymbol( DWORD64 nAddr, char *pOut, size_t nOutSize )
{
	pOut[0] = 0;
	if ( !s_Dbg.SymFromAddr )
		return;

	char buffer[sizeof( SYMBOL_INFO ) + 512];
	SYMBOL_INFO *pSym = (SYMBOL_INFO *)buffer;
	memset( buffer, 0, sizeof( buffer ) );
	pSym->SizeOfStruct = sizeof( SYMBOL_INFO );
	pSym->MaxNameLen = 511;

	DWORD64 nDisp = 0;
	if ( !s_Dbg.SymFromAddr( GetCurrentProcess(), nAddr, &nDisp, pSym ) )
		return;

	IMAGEHLP_LINE64 line;
	memset( &line, 0, sizeof( line ) );
	line.SizeOfStruct = sizeof( line );
	DWORD nLineDisp = 0;
	if ( s_Dbg.SymGetLineFromAddr64 && s_Dbg.SymGetLineFromAddr64( GetCurrentProcess(), nAddr, &nLineDisp, &line ) )
		_snprintf( pOut, nOutSize, "%s+0x%llx  [%s:%lu]", pSym->Name, nDisp, line.FileName, line.LineNumber );
	else
		_snprintf( pOut, nOutSize, "%s+0x%llx", pSym->Name, nDisp );
	pOut[nOutSize - 1] = 0;
}

static void WriteStack( FILE *fp, const CONTEXT *pContext, DWORD nThreadId )
{
	if ( !s_Dbg.StackWalk64 )
	{
		fprintf( fp, "  (dbghelp StackWalk64 unavailable)\n" );
		return;
	}

	CONTEXT ctx = *pContext;
	STACKFRAME64 frame;
	memset( &frame, 0, sizeof( frame ) );
	DWORD nMachine;
#if defined( _M_IX86 )
	nMachine = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset = ctx.Eip;
	frame.AddrFrame.Offset = ctx.Ebp;
	frame.AddrStack.Offset = ctx.Esp;
#else
	nMachine = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrStack.Offset = ctx.Rsp;
#endif
	frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

	HANDLE hThread = OpenThread( THREAD_ALL_ACCESS, FALSE, nThreadId );
	for ( int i = 0; i < 64; i++ )
	{
		if ( !s_Dbg.StackWalk64( nMachine, GetCurrentProcess(), hThread ? hThread : GetCurrentThread(), &frame, &ctx, NULL,
			s_Dbg.SymFunctionTableAccess64, s_Dbg.SymGetModuleBase64, NULL ) )
			break;
		if ( frame.AddrPC.Offset == 0 )
			break;

		char szModule[MAX_PATH + 32], szSymbol[1024];
		DescribeAddress( frame.AddrPC.Offset, szModule, sizeof( szModule ) );
		DescribeSymbol( frame.AddrPC.Offset, szSymbol, sizeof( szSymbol ) );
		fprintf( fp, "  #%02d  %08llx  %-32s %s\n", i, frame.AddrPC.Offset, szModule, szSymbol );
	}
	if ( hThread )
		CloseHandle( hThread );
}

static void WriteModules( FILE *fp )
{
	HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE, GetCurrentProcessId() );
	if ( hSnap == INVALID_HANDLE_VALUE )
		return;

	MODULEENTRY32 me;
	me.dwSize = sizeof( me );
	for ( BOOL bOk = Module32First( hSnap, &me ); bOk; bOk = Module32Next( hSnap, &me ) )
	{
		fprintf( fp, "  %p  %8lx  %s\n", me.modBaseAddr, me.modBaseSize, me.szExePath );
	}
	CloseHandle( hSnap );
}

static void LaunchAnalysis( const char *pszDumpPath, const char *pszReportPath )
{
	char szBat[MAX_PATH];
	_snprintf( szBat, sizeof( szBat ), "%s\\crashtools\\analyze_crash.bat", s_szModDir );
	szBat[sizeof( szBat ) - 1] = 0;
	if ( GetFileAttributesA( szBat ) == INVALID_FILE_ATTRIBUTES )
		return;

	char szCmd[MAX_PATH * 4];
	_snprintf( szCmd, sizeof( szCmd ), "cmd.exe /c \"\"%s\" \"%s\" \"%s\"\"", szBat, pszDumpPath, pszReportPath );
	szCmd[sizeof( szCmd ) - 1] = 0;

	STARTUPINFOA si;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	PROCESS_INFORMATION pi;
	if ( CreateProcessA( NULL, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi ) )
	{
		CloseHandle( pi.hThread );
		CloseHandle( pi.hProcess );
	}
}

static void WriteReport( EXCEPTION_POINTERS *pExc, DWORD nThreadId, bool bManual )
{
	LoadDbgHelp();

	SYSTEMTIME st;
	GetLocalTime( &st );

	char szDir[MAX_PATH], szBase[MAX_PATH], szDump[MAX_PATH], szReport[MAX_PATH];
	_snprintf( szDir, sizeof( szDir ), "%s\\crashes", s_szModDir );
	CreateDirectoryA( szDir, NULL );
	_snprintf( szBase, sizeof( szBase ), "%s\\%s_%04d-%02d-%02d_%02d-%02d-%02d", szDir, bManual ? "report" : "crash",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	_snprintf( szDump, sizeof( szDump ), "%s.dmp", szBase );
	_snprintf( szReport, sizeof( szReport ), "%s.txt", szBase );

	// 1) Minidump first: it is the most valuable artifact if anything below fails.
	bool bDumpOk = false;
	if ( s_Dbg.MiniDumpWriteDump )
	{
		HANDLE hFile = CreateFileA( szDump, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile != INVALID_HANDLE_VALUE )
		{
			MINIDUMP_EXCEPTION_INFORMATION mei;
			mei.ThreadId = nThreadId;
			mei.ExceptionPointers = pExc;
			mei.ClientPointers = FALSE;
			MINIDUMP_TYPE type = (MINIDUMP_TYPE)( MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
				MiniDumpWithUnloadedModules | MiniDumpWithHandleData | MiniDumpWithProcessThreadData );
			bDumpOk = s_Dbg.MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mei, NULL, NULL ) != FALSE;
			CloseHandle( hFile );
		}
	}

	// 2) Human-readable report
	FILE *fp = fopen( szReport, "w" );
	if ( !fp )
		return;

	const EXCEPTION_RECORD *pRec = pExc->ExceptionRecord;
	const CONTEXT *pCtx = pExc->ContextRecord;
	DWORD64 nAddr = (DWORD64)(UINT_PTR)pRec->ExceptionAddress;

	char szWhere[MAX_PATH + 32], szSym[1024];
	DescribeAddress( nAddr, szWhere, sizeof( szWhere ) );

	if ( s_Dbg.SymSetOptions && s_Dbg.SymInitialize )
	{
		s_Dbg.SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS );
		char szSymPath[MAX_PATH * 2];
		_snprintf( szSymPath, sizeof( szSymPath ), "%s\\bin", s_szModDir );
		szSymPath[sizeof( szSymPath ) - 1] = 0;
		s_Dbg.SymInitialize( GetCurrentProcess(), szSymPath, TRUE );
	}
	DescribeSymbol( nAddr, szSym, sizeof( szSym ) );

	fprintf( fp, "==================== HL2RPM crash report ====================\n" );
	fprintf( fp, "Time:        %04d-%02d-%02d %02d:%02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	fprintf( fp, "client.dll:  built %s %s\n", __DATE__, __TIME__ );
	fprintf( fp, "Exception:   0x%08lX %s%s\n", pRec->ExceptionCode, ExceptionName( pRec->ExceptionCode ),
		bManual ? "" : "  (first-chance: if the game did NOT close, this may have been handled)" );
	if ( pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRec->NumberParameters >= 2 )
	{
		const char *pOp = pRec->ExceptionInformation[0] == 0 ? "READING" : ( pRec->ExceptionInformation[0] == 1 ? "WRITING" : "EXECUTING" );
		fprintf( fp, "             %s address 0x%08llx%s\n", pOp, (DWORD64)pRec->ExceptionInformation[1],
			pRec->ExceptionInformation[1] < 0x10000 ? "  <-- NULL pointer (+offset)" : "" );
	}
	fprintf( fp, "Address:     %08llx  %s\n", nAddr, szWhere );
	fprintf( fp, "Function:    %s\n", szSym[0] ? szSym : "(no symbols - engine/driver module or missing .pdb)" );
	fprintf( fp, "Thread:      %lu%s\n", nThreadId, nThreadId == GetCurrentThreadId() ? "" : "" );
	fprintf( fp, "Minidump:    %s%s\n", szDump, bDumpOk ? "" : "  (FAILED to write)" );

#if defined( _M_IX86 )
	fprintf( fp, "\n---- Registers ----\n" );
	fprintf( fp, "  EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx\n", pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx, pCtx->Esi, pCtx->Edi );
	fprintf( fp, "  EIP=%08lx ESP=%08lx EBP=%08lx EFL=%08lx\n", pCtx->Eip, pCtx->Esp, pCtx->Ebp, pCtx->EFlags );
#endif

	fprintf( fp, "\n---- Call stack (crashing thread, most recent call first) ----\n" );
	WriteStack( fp, pCtx, nThreadId );
	fflush( fp );

	fprintf( fp, "\n---- Breadcrumbs (oldest -> newest, time before report; C=client S=server) ----\n" );
	RPMShared_t *pShared = GetShared();
	if ( pShared )
	{
		LONG nTotal = pShared->nCrumbs;
		LONG nCount = nTotal < 100 ? nTotal : 100;
		DWORD nNow = GetTickCount();
		char szLine[256];
		for ( LONG i = nTotal - nCount; i < nTotal; i++ )
		{
			FormatCrumb( pShared->crumbs[(DWORD)i % RPM_CRUMB_COUNT], nNow, szLine, sizeof( szLine ) );
			fprintf( fp, "  %s\n", szLine );
		}
		if ( nTotal == 0 )
			fprintf( fp, "  (none)\n" );

		fprintf( fp, "\n---- Last console output ----\n" );
		LONG nConTotal = pShared->nConsole;
		LONG nConCount = nConTotal < RPM_CONSOLE_COUNT ? nConTotal : RPM_CONSOLE_COUNT;
		for ( LONG i = nConTotal - nConCount; i < nConTotal; i++ )
		{
			char szCon[RPM_CONSOLE_LEN];
			memcpy( szCon, pShared->console[(DWORD)i % RPM_CONSOLE_COUNT], sizeof( szCon ) );
			szCon[sizeof( szCon ) - 1] = 0;
			fprintf( fp, "  %s\n", szCon );
		}
	}
	fflush( fp );

	fprintf( fp, "\n---- Loaded modules ----\n" );
	WriteModules( fp );
	fprintf( fp, "\n(Full analysis by cdb, if available: %s.analysis.txt)\n", szBase );
	fclose( fp );

	if ( s_Dbg.SymCleanup )
		s_Dbg.SymCleanup( GetCurrentProcess() );

	if ( bDumpOk )
		LaunchAnalysis( szDump, szReport );
}

static DWORD WINAPI WorkerThread( LPVOID )
{
	for ( ;; )
	{
		WaitForSingleObject( s_hStart, INFINITE );
		if ( s_bQuit )
			break;
		WriteReport( s_pJobException, s_nJobThreadId, s_bJobManual );
		SetEvent( s_hDone );
	}
	return 0;
}

// Runs the report on the worker thread (so stack overflow / a broken stack on the
// crashing thread doesn't matter) and waits for it.
static void RunJob( EXCEPTION_POINTERS *pExc, DWORD nThreadId, bool bManual )
{
	s_pJobException = pExc;
	s_nJobThreadId = nThreadId;
	s_bJobManual = bManual;
	ResetEvent( s_hDone );
	SetEvent( s_hStart );
	WaitForSingleObject( s_hDone, 120 * 1000 );
}

static LONG CALLBACK VectoredHandler( EXCEPTION_POINTERS *pExc )
{
	if ( !IsFatalException( pExc->ExceptionRecord->ExceptionCode ) )
		return EXCEPTION_CONTINUE_SEARCH;

	// A real debugger (VS / cdb) is attached: let it take over.
	if ( IsDebuggerPresent() )
		return EXCEPTION_CONTINUE_SEARCH;

	// Never recurse from the worker, never run two reports at once.
	if ( GetCurrentThreadId() == s_nWorkerThreadId || s_nReports >= RPM_MAX_REPORTS )
		return EXCEPTION_CONTINUE_SEARCH;
	if ( InterlockedCompareExchange( &s_bBusy, 1, 0 ) != 0 )
		return EXCEPTION_CONTINUE_SEARCH;

	InterlockedIncrement( &s_nReports );
	RunJob( pExc, GetCurrentThreadId(), false );
	InterlockedExchange( &s_bBusy, 0 );

	// Let the engine / Windows continue normally (the game will still close).
	return EXCEPTION_CONTINUE_SEARCH;
}

bool RPMCrash_InstallHandler( const char *pszModDir )
{
	if ( s_hVeh )
		return true;

	GetShared();
	strncpy( s_szModDir, pszModDir, sizeof( s_szModDir ) - 1 );
	s_szModDir[sizeof( s_szModDir ) - 1] = 0;

	// Load dbghelp now, not while crashing (avoids the loader lock inside a crash).
	LoadDbgHelp();

	s_bQuit = false;
	s_hStart = CreateEventA( NULL, FALSE, FALSE, NULL );
	s_hDone = CreateEventA( NULL, TRUE, FALSE, NULL );
	s_hWorker = CreateThread( NULL, 256 * 1024, WorkerThread, NULL, 0, &s_nWorkerThreadId );
	if ( !s_hStart || !s_hDone || !s_hWorker )
		return false;

	s_hVeh = AddVectoredExceptionHandler( 1, VectoredHandler );
	return s_hVeh != NULL;
}

void RPMCrash_RemoveHandler()
{
	if ( s_hVeh )
	{
		RemoveVectoredExceptionHandler( s_hVeh );
		s_hVeh = NULL;
	}

	if ( s_hWorker )
	{
		s_bQuit = true;
		SetEvent( s_hStart );
		WaitForSingleObject( s_hWorker, 5000 );
		CloseHandle( s_hWorker );
		s_hWorker = NULL;
	}
	if ( s_hStart ) { CloseHandle( s_hStart ); s_hStart = NULL; }
	if ( s_hDone ) { CloseHandle( s_hDone ); s_hDone = NULL; }
	s_nWorkerThreadId = 0;
}

bool RPMCrash_IsHandlerInstalled()
{
	return s_hVeh != NULL;
}

void RPMCrash_WriteManualReport()
{
	if ( !s_hWorker )
		return;

	CONTEXT ctx;
	memset( &ctx, 0, sizeof( ctx ) );
	RtlCaptureContext( &ctx );

	EXCEPTION_RECORD rec;
	memset( &rec, 0, sizeof( rec ) );
#if defined( _M_IX86 )
	rec.ExceptionAddress = (PVOID)(UINT_PTR)ctx.Eip;
#else
	rec.ExceptionAddress = (PVOID)ctx.Rip;
#endif

	EXCEPTION_POINTERS ep;
	ep.ExceptionRecord = &rec;
	ep.ContextRecord = &ctx;

	if ( InterlockedCompareExchange( &s_bBusy, 1, 0 ) != 0 )
		return;
	RunJob( &ep, GetCurrentThreadId(), true );
	InterlockedExchange( &s_bBusy, 0 );
}

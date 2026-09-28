//========= HL2RPM ============================================================//
//
// Purpose: Client-side glue for the crash debugger (hl2rpm_crashdebug.h):
//          installs the handler, captures console output, console commands.
//
//=============================================================================//

#include "cbase.h"
#include "hl2rpm_crashdebug.h"
#include "igamesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static SpewOutputFunc_t s_pfnPrevSpew = NULL;

static SpewRetval_t RPMCrash_SpewFunc( SpewType_t spewType, const tchar *pMsg )
{
	RPMCrash_ConsoleText( pMsg );
	return s_pfnPrevSpew ? s_pfnPrevSpew( spewType, pMsg ) : SPEW_CONTINUE;
}

static void RPMCrash_Install()
{
	if ( RPMCrash_IsHandlerInstalled() )
		return;

	if ( RPMCrash_InstallHandler( engine->GetGameDirectory() ) )
		Msg( "[crash debugger] installed, reports go to %s\\crashes\n", engine->GetGameDirectory() );
	else
		Warning( "[crash debugger] failed to install\n" );
}

static void RPMCrash_HandlerChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar rpm_crash_handler( "rpm_crash_handler", "1", FCVAR_ARCHIVE, "HL2RPM crash debugger: write crashes/*.dmp + *.txt on a crash", RPMCrash_HandlerChanged );

static void RPMCrash_HandlerChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	if ( rpm_crash_handler.GetBool() )
		RPMCrash_Install();
	else
		RPMCrash_RemoveHandler();
}

void RPMCrash_ClientInit()
{
	s_pfnPrevSpew = GetSpewOutputFunc();
	SpewOutputFunc( RPMCrash_SpewFunc );

	// The convar may not be loaded from config.cfg yet; install now and let the
	// change callback remove it if the user disabled it.
	RPMCrash_Install();
}

void RPMCrash_ClientShutdown()
{
	// Both must be undone before client.dll is unloaded, or the engine would call
	// into freed code.
	if ( GetSpewOutputFunc() == RPMCrash_SpewFunc )
		SpewOutputFunc( s_pfnPrevSpew );
	RPMCrash_RemoveHandler();
}

//-----------------------------------------------------------------------------
// Level transitions as breadcrumbs
//-----------------------------------------------------------------------------
class CRPMCrashLevelCrumbs : public CAutoGameSystem
{
public:
	CRPMCrashLevelCrumbs() : CAutoGameSystem( "CRPMCrashLevelCrumbs" ) {}

	virtual void LevelInitPreEntity()		{ RPM_CRUMB( "Level init: %s", MapName() ); }
	virtual void LevelInitPostEntity()		{ RPM_CRUMB( "Level ready: %s", MapName() ); }
	virtual void LevelShutdownPreEntity()	{ RPM_CRUMB( "Level shutdown: %s", MapName() ); }

private:
	static const char *MapName() { return engine->GetLevelName() ? engine->GetLevelName() : "?"; }
};
static CRPMCrashLevelCrumbs s_RPMCrashLevelCrumbs;

//-----------------------------------------------------------------------------
// Console commands
//-----------------------------------------------------------------------------
static void PrintLine( const char *pLine )
{
	Msg( "%s\n", pLine );
}

CON_COMMAND( rpm_crash_crumbs, "Print the last N crash-debugger breadcrumbs (default 40)" )
{
	int nCount = args.ArgC() > 1 ? atoi( args[1] ) : 40;
	RPMCrash_DumpCrumbs( PrintLine, nCount > 0 ? nCount : 40 );
}

CON_COMMAND( rpm_crash_report, "Write a crash-debugger report + minidump now, without crashing" )
{
	if ( !RPMCrash_IsHandlerInstalled() )
	{
		Warning( "rpm_crash_handler is 0\n" );
		return;
	}
	RPM_CRUMB( "rpm_crash_report" );
	RPMCrash_WriteManualReport();
	Msg( "Report written to %s\\crashes\n", engine->GetGameDirectory() );
}

static int RPMCrash_Recurse( int n )
{
	volatile char buf[4096];
	buf[0] = (char)n;
	if ( n < 0 )	// never true; keeps the compiler from flagging infinite recursion
		return 0;
	return RPMCrash_Recurse( n + 1 ) + buf[0];
}

CON_COMMAND_F( rpm_crash_test, "Deliberately crash the game to test the crash debugger: null | div | stack", FCVAR_CHEAT )
{
	const char *pType = args.ArgC() > 1 ? args[1] : "null";
	RPM_CRUMB( "rpm_crash_test %s", pType );

	if ( !Q_stricmp( pType, "div" ) )
	{
		volatile int nZero = 0;
		Msg( "%d\n", 1 / nZero );
	}
	else if ( !Q_stricmp( pType, "stack" ) )
	{
		Msg( "%d\n", RPMCrash_Recurse( 0 ) );
	}
	else
	{
		volatile int *pNull = NULL;
		*pNull = 1;
	}
}

//========= HL2RPM ============//
//
// Проверка проходимости уровня (dev, cheat): игрок сам идёт по точкам - как идёт живой игрок, со
// столкновениями, дверями, NPC и невидимыми стенами. Маршрут - cl_autowalk "x y z, x y z, ...",
// файл cl_autowalk_file <cfg/...> (строка "x y z", "use" - нажать E на этой точке) или маршрут сервера
// по сетке узлов уровня (rpg_route_dump <x y z> пишет cfg/_route.txt). В консоли/console.log:
//   [autowalk] wp 3/12 reached at (x y z), 14.2 s
//   [autowalk] STUCK near (x y z) -> wp 4 (x y z)       - сюда игрок не проходит
//   [autowalk] done
// Пока открыт диалог или экран RPG, автопилот ждёт (выбор делает сценарий теста: rpg_dialog_choose).
//
//=============================//

#include "cbase.h"
#include "c_baseplayer.h"
#include "usercmd.h"
#include "in_buttons.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/rpg_dialog_panel.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar cl_autowalk_speed( "cl_autowalk_speed", "450", FCVAR_CHEAT, "Autowalk: forward move (450 - normal walk speed of HL2)" );
static ConVar cl_autowalk_radius( "cl_autowalk_radius", "28", FCVAR_CHEAT, "Autowalk: a waypoint counts as reached this close (2D)" );

struct AutoWP_t
{
	Vector pos;
	bool bUse;
};

static CUtlVector<AutoWP_t> s_Route;
static int s_iWP = -1;
static float s_flStart, s_flLastCheck, s_flStuckSince, s_flUseUntil, s_flPausedUntil;
static Vector s_vecLastPos;
static int s_iUnstickTries;

static void StartRoute()
{
	s_iWP = s_Route.Count() ? 0 : -1;
	s_flStart = gpGlobals->curtime;
	s_flLastCheck = gpGlobals->curtime;
	s_flStuckSince = -1.0f;
	s_flUseUntil = 0.0f;
	s_flPausedUntil = 0.0f;
	s_iUnstickTries = 0;
	s_vecLastPos = vec3_origin;
	Msg( "[autowalk] start, %d waypoints\n", s_Route.Count() );
}

static bool ParsePoint( const char *psz, AutoWP_t &wp )
{
	wp.bUse = false;
	// кавычки и пробелы вокруг (ArgS() отдаёт строку как набрана)
	while ( *psz && !( ( *psz >= '0' && *psz <= '9' ) || *psz == '-' || *psz == '+' || *psz == '.' ) )
		++psz;
	float x, y, z;
	if ( sscanf( psz, "%f %f %f", &x, &y, &z ) != 3 )
		return false;
	wp.pos.Init( x, y, z );
	wp.bUse = Q_stristr( psz, "use" ) != NULL;
	return true;
}

CON_COMMAND_F( cl_autowalk, "Autowalk the player: cl_autowalk \"x y z, x y z [use], ...\" (no argument - stop)", FCVAR_CHEAT )
{
	s_Route.RemoveAll();
	if ( args.ArgC() < 2 )
	{
		s_iWP = -1;
		Msg( "[autowalk] stopped\n" );
		return;
	}
	CUtlStringList parts;
	V_SplitString( args.ArgS(), ",", parts );		// "," - ";" разрезает команду консоли
	for ( int i = 0; i < parts.Count(); ++i )
	{
		AutoWP_t wp;
		if ( ParsePoint( parts[i], wp ) )
			s_Route.AddToTail( wp );
	}
	StartRoute();
}

CON_COMMAND_F( cl_autowalk_file, "Autowalk along the waypoints of a file (one \"x y z [use]\" per line), e.g. cfg/_route.txt", FCVAR_CHEAT )
{
	s_Route.RemoveAll();
	if ( args.ArgC() < 2 )
		return;
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !g_pFullFileSystem->ReadFile( args[1], "MOD", buf ) )
	{
		Warning( "[autowalk] can't read %s\n", args[1] );
		return;
	}
	char szLine[256];
	while ( buf.IsValid() )
	{
		buf.GetLine( szLine, sizeof( szLine ) );
		if ( !szLine[0] )
			break;
		if ( szLine[0] == '/' || szLine[0] == '#' )
			continue;
		AutoWP_t wp;
		if ( ParsePoint( szLine, wp ) )
			s_Route.AddToTail( wp );
	}
	StartRoute();
}

// зовётся из C_BaseHLPlayer::CreateMove
void RPG_AutoWalk_CreateMove( C_BasePlayer *pPlayer, CUserCmd *pCmd )
{
	if ( s_iWP < 0 || !pPlayer || !pCmd )
		return;
	if ( s_iWP >= s_Route.Count() )
	{
		Msg( "[autowalk] done, %.1f s\n", gpGlobals->curtime - s_flStart );
		s_iWP = -1;
		return;
	}
	const float now = gpGlobals->curtime;
	// разговор или экран: ждём, это не застревание
	if ( RPGScreen_AnyOpen() || RPGDialogPanel_IsOpen() )
	{
		s_flPausedUntil = now + 1.0f;
		s_flStuckSince = -1.0f;
		return;
	}
	if ( now < s_flPausedUntil )
	{
		s_vecLastPos = pPlayer->GetAbsOrigin();
		return;
	}
	const AutoWP_t &wp = s_Route[s_iWP];
	const Vector &org = pPlayer->GetAbsOrigin();
	Vector2D d( wp.pos.x - org.x, wp.pos.y - org.y );
	const float dist = d.Length();
	if ( dist < cl_autowalk_radius.GetFloat() )
	{
		Msg( "[autowalk] wp %d/%d reached at (%.0f %.0f %.0f), %.1f s\n", s_iWP + 1, s_Route.Count(), org.x, org.y, org.z, now - s_flStart );
		if ( wp.bUse )
			s_flUseUntil = now + 0.2f;
		++s_iWP;
		s_flStuckSince = -1.0f;
		s_iUnstickTries = 0;
		return;
	}
	QAngle ang = pCmd->viewangles;
	ang.y = RAD2DEG( atan2f( d.y, d.x ) );
	ang.x = 0.0f;
	pCmd->viewangles = ang;
	engine->SetViewAngles( ang );
	pCmd->forwardmove = cl_autowalk_speed.GetFloat();
	pCmd->sidemove = 0.0f;
	pCmd->buttons |= IN_FORWARD;
	pCmd->buttons &= ~( IN_SPEED | IN_BACK );
	if ( now < s_flUseUntil )
		pCmd->buttons |= IN_USE;

	// застревание: за 1.5 с сдвинулся меньше чем на 10 ед.
	if ( now - s_flLastCheck >= 0.5f )
	{
		const float moved = ( org - s_vecLastPos ).Length2D();
		s_vecLastPos = org;
		s_flLastCheck = now;
		if ( moved < 5.0f )
		{
			if ( s_flStuckSince < 0.0f )
				s_flStuckSince = now;
		}
		else
			s_flStuckSince = -1.0f;
	}
	if ( s_flStuckSince > 0.0f && now - s_flStuckSince > 1.5f )
	{
		if ( s_iUnstickTries < 3 )
		{
			// дверь? нажать E; ступенька? прыгнуть
			++s_iUnstickTries;
			s_flUseUntil = now + 0.3f;
			if ( s_iUnstickTries == 2 )
				pCmd->buttons |= IN_JUMP;
			s_flStuckSince = now;
			Msg( "[autowalk] blocked at (%.0f %.0f %.0f), try %d (use%s)\n", org.x, org.y, org.z, s_iUnstickTries, s_iUnstickTries == 2 ? " + jump" : "" );
			return;
		}
		Msg( "[autowalk] STUCK near (%.0f %.0f %.0f) -> wp %d (%.0f %.0f %.0f), %.1f s\n", org.x, org.y, org.z, s_iWP + 1,
			wp.pos.x, wp.pos.y, wp.pos.z, now - s_flStart );
		s_iWP = -1;
	}
}

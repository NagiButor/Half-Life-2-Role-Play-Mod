//========= HL2RPM ============================================================//
//
// Terminals (M6.5): rpm_terminal - a computer or a Combine console the player uses
// like in Fallout 4: the camera moves to its screen and the interface is drawn on
// the screen itself (client: hl2rpm_terminal_ui.cpp). Records to read (lore,
// diaries, orders), menus, entries that do things (doors, force fields, turrets,
// any action of the Creation Kit), a lock hacked with the Fallout word game.
//
// scripts/rpg/terminals.txt (Creation Kit):
//   "Terminals" { "<id>" {
//       "style" "pc|combine"  "title" ".."  "title_<lang>" ".."  "header" ".." (welcome lines)
//       "lock_level" "0..100" (0 = open)  "lock_skill" "science"  "password_item" "<item id>" (opens it at once)
//       "entries" { "<n>" { "title" ".." "text" ".." (a record to read; its "actions" run when it is read)
//                          "actions" { .. } (without a text: a command) "result" ".." (what the screen says after it)
//                          "conditions" { .. } (shown only when they hold) "once" "1" (gone after use)
//                          "entries" { .. } (a submenu) } } } }
// The map: rpm_terminal with "terminal" <id> (a layer ref "terminal" of the CK, or Hammer).
//
// The state of every terminal (hacked, locked out, used entries) is in RPMState "terminals".
// The client gets the screen contents as KeyValues text (user message RPMTerm, in pieces)
// and answers with rpm_terminal_cmd <token> hacked|lockout|run <id>|read <id>|close.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_char.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "inventory_system.h"
#include "player.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define RPM_TERMINALS_FILE "scripts/rpg/terminals.txt"

enum { TSTYLE_PC = 0, TSTYLE_COMBINE = 1 };
enum { TLOCK_OPEN = 0, TLOCK_HACK, TLOCK_LOCKOUT, TLOCK_SKILL };

static ConVar rpm_terminal_debug( "rpm_terminal_debug", "0", FCVAR_NONE, "HL2RPM: print terminal sessions and what they send" );

//-----------------------------------------------------------------------------
// definitions
//-----------------------------------------------------------------------------
static KeyValues *s_pTermDefs = NULL;
static long s_TermDefTime = -1;
static double s_flTermNextCheck = 0;

static KeyValues *TermDefs()
{
	double now = Plat_FloatTime();
	if ( s_pTermDefs && now < s_flTermNextCheck )
		return s_pTermDefs;
	s_flTermNextCheck = now + 1.0;
	long t = filesystem->GetFileTime( RPM_TERMINALS_FILE, "GAME" );
	if ( s_pTermDefs && t == s_TermDefTime )
		return s_pTermDefs;
	if ( s_pTermDefs )
		s_pTermDefs->deleteThis();
	s_pTermDefs = new KeyValues( "Terminals" );
	s_TermDefTime = t;
	if ( s_pTermDefs->LoadFromFile( filesystem, RPM_TERMINALS_FILE, "GAME" ) )
		DevMsg( "RPMTerminal: definitions from %s\n", RPM_TERMINALS_FILE );
	return s_pTermDefs;
}

static KeyValues *FindTermDef( const char *id )
{
	if ( !id || !id[0] )
		return NULL;
	return TermDefs()->FindKey( id );
}

// an entry by its path "2.1" (keys of nested "entries")
static KeyValues *FindEntry( KeyValues *pDef, const char *path )
{
	if ( !pDef || !path || !path[0] )
		return NULL;
	CUtlStringList parts;
	V_SplitString( path, ".", parts );
	KeyValues *e = pDef;
	for ( int i = 0; i < parts.Count() && e; i++ )
	{
		KeyValues *list = e->FindKey( "entries" );
		e = list ? list->FindKey( parts[i] ) : NULL;
	}
	return e;
}

//-----------------------------------------------------------------------------
// screens of the stock models (model space): center, angles of the screen normal, size
//-----------------------------------------------------------------------------
struct TermScreen_t { const char *model; float x, y, z, pitch, yaw, w, h; int style; };
static const TermScreen_t s_TermScreens[] =
{
	{ "models/props_lab/monitor01a.mdl", 12.6f, 0.0f, 3.4f, -6.0f, 0.0f, 16.0f, 12.4f, TSTYLE_PC },
	{ "models/props_combine/combine_intmonitor001.mdl", -3.9f, -1.55f, 30.25f, 0.0f, 0.0f, 24.6f, 42.5f, TSTYLE_COMBINE },
	{ "models/props_combine/combine_smallmonitor001.mdl", 4.1f, 0.15f, 13.75f, 0.0f, 11.0f, 11.8f, 15.0f, TSTYLE_COMBINE },
	{ "models/props_combine/combine_interface001.mdl", 0.6f, -2.05f, 45.4f, -48.0f, 0.0f, 15.4f, 7.6f, TSTYLE_COMBINE },
};

static const char *DefaultModel( int style )
{
	return style == TSTYLE_COMBINE ? "models/props_combine/combine_intmonitor001.mdl" : "models/props_lab/monitor01a.mdl";
}

static int StyleFromString( const char *s, int def )
{
	if ( !s || !s[0] )
		return def;
	if ( !Q_stricmp( s, "combine" ) )
		return TSTYLE_COMBINE;
	return TSTYLE_PC;
}

//-----------------------------------------------------------------------------
// the entity
//-----------------------------------------------------------------------------
class CRPMTerminal : public CBaseAnimating
{
public:
	DECLARE_CLASS( CRPMTerminal, CBaseAnimating );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CRPMTerminal() : m_iLockLevelKey( -1 ) {}

	KeyValues *Def() { return FindTermDef( STRING( m_iszTerminal ) ); }

	int Style()
	{
		KeyValues *d = Def();
		return StyleFromString( STRING( m_iszStyle ), StyleFromString( d ? d->GetString( "style" ) : "", TSTYLE_PC ) );
	}

	int LockLevel()
	{
		if ( m_iLockLevelKey >= 0 )
			return m_iLockLevelKey;
		KeyValues *d = Def();
		return d ? clamp( d->GetInt( "lock_level", 0 ), 0, 100 ) : 0;
	}

	void Precache()
	{
		if ( GetModelName() == NULL_STRING )
			SetModelName( AllocPooledString( DefaultModel( Style() ) ) );
		PrecacheModel( STRING( GetModelName() ) );
		PrecacheScriptSound( "HL2Player.Use" );
	}

	void Spawn()
	{
		Precache();
		SetModel( STRING( GetModelName() ) );
		SetSolid( SOLID_VPHYSICS );
		SetMoveType( MOVETYPE_NONE );
		VPhysicsInitStatic();
		m_iStyle = Style();
		ComputeScreen();
		RefreshIdle();
	}

	void Activate()
	{
		BaseClass::Activate();
		RefreshIdle();
	}

	// where the screen is (model space): keys of the map / CK, else the table, else the front of the box
	void ComputeScreen()
	{
		const char *mdl = STRING( GetModelName() );
		Vector pos( 0, 0, 0 );
		QAngle ang( 0, 0, 0 );
		float w = 0, h = 0;
		bool bFound = false;
		for ( int i = 0; i < ARRAYSIZE( s_TermScreens ); i++ )
		{
			if ( !Q_stricmp( mdl, s_TermScreens[i].model ) )
			{
				const TermScreen_t &s = s_TermScreens[i];
				pos.Init( s.x, s.y, s.z );
				ang.Init( s.pitch, s.yaw, 0 );
				w = s.w;
				h = s.h;
				bFound = true;
				break;
			}
		}
		if ( !bFound )
		{
			Vector mins = CollisionProp()->OBBMins(), maxs = CollisionProp()->OBBMaxs();
			pos.Init( maxs.x + 0.3f, ( mins.y + maxs.y ) * 0.5f, ( mins.z + maxs.z ) * 0.5f );
			w = ( maxs.y - mins.y ) * 0.8f;
			h = ( maxs.z - mins.z ) * 0.8f;
		}
		if ( m_iszScreenPos != NULL_STRING )
			UTIL_StringToVector( pos.Base(), STRING( m_iszScreenPos ) );
		if ( m_iszScreenAng != NULL_STRING )
			UTIL_StringToVector( ang.Base(), STRING( m_iszScreenAng ) );
		if ( m_iszScreenSize != NULL_STRING )
		{
			float v[2] = { w, h };
			UTIL_StringToFloatArray( v, 2, STRING( m_iszScreenSize ) );
			w = v[0];
			h = v[1];
		}
		m_vecScreenPos = pos;
		m_angScreen = ang;
		m_flScreenW = Max( 1.0f, w );
		m_flScreenH = Max( 1.0f, h );
	}

	// the idle screen: the title and whether it is locked
	void RefreshIdle()
	{
		KeyValues *d = Def();
		const char *t = d ? RPMLogic::Localized( d, "title" ) : "";
		if ( !t[0] )
			t = m_iStyle == TSTYLE_COMBINE ? "OVERWATCH" : ( RPMLogic::IsRussian() ? "ТЕРМИНАЛ" : "TERMINAL" );
		Q_strncpy( m_szTitle.GetForModify(), t, 64 );
		char id[160];
		RPMLock::LockId( this, id, sizeof( id ) );
		int st = RPMState::Section( "terminals" )->GetInt( id, 0 );
		m_iLockState = st == 2 ? TLOCK_LOCKOUT : ( st == 1 || LockLevel() <= 0 ? TLOCK_OPEN : TLOCK_HACK );
	}

	int ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; }
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	string_t m_iszTerminal, m_iszStyle, m_iszScreenPos, m_iszScreenAng, m_iszScreenSize;
	int m_iLockLevelKey;
	COutputEvent m_OnUsed, m_OnHacked, m_OnLockout;

	CNetworkVar( int, m_iStyle );
	CNetworkVar( int, m_iLockState );
	CNetworkVector( m_vecScreenPos );
	CNetworkQAngle( m_angScreen );
	CNetworkVar( float, m_flScreenW );
	CNetworkVar( float, m_flScreenH );
	CNetworkString( m_szTitle, 64 );
};

LINK_ENTITY_TO_CLASS( rpm_terminal, CRPMTerminal );

BEGIN_DATADESC( CRPMTerminal )
	DEFINE_KEYFIELD( m_iszTerminal, FIELD_STRING, "terminal" ),
	DEFINE_KEYFIELD( m_iszStyle, FIELD_STRING, "style" ),
	DEFINE_KEYFIELD( m_iszScreenPos, FIELD_STRING, "screen_pos" ),
	DEFINE_KEYFIELD( m_iszScreenAng, FIELD_STRING, "screen_ang" ),
	DEFINE_KEYFIELD( m_iszScreenSize, FIELD_STRING, "screen_size" ),
	DEFINE_KEYFIELD( m_iLockLevelKey, FIELD_INTEGER, "lock_level" ),
	DEFINE_FIELD( m_iStyle, FIELD_INTEGER ),
	DEFINE_FIELD( m_iLockState, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecScreenPos, FIELD_VECTOR ),
	DEFINE_FIELD( m_angScreen, FIELD_VECTOR ),
	DEFINE_FIELD( m_flScreenW, FIELD_FLOAT ),
	DEFINE_FIELD( m_flScreenH, FIELD_FLOAT ),
	DEFINE_OUTPUT( m_OnUsed, "OnUsed" ),
	DEFINE_OUTPUT( m_OnHacked, "OnHacked" ),
	DEFINE_OUTPUT( m_OnLockout, "OnLockout" ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CRPMTerminal, DT_RPMTerminal )
	SendPropInt( SENDINFO( m_iStyle ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iLockState ), 3, SPROP_UNSIGNED ),
	SendPropVector( SENDINFO( m_vecScreenPos ), -1, SPROP_NOSCALE ),
	SendPropQAngles( SENDINFO( m_angScreen ), 13 ),
	SendPropFloat( SENDINFO( m_flScreenW ), -1, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flScreenH ), -1, SPROP_NOSCALE ),
	SendPropString( SENDINFO( m_szTitle ) ),
END_SEND_TABLE()

// the use prompt (hl2rpm_prompt.cpp)
const char *RPMTerminal_GetTitle( CBaseEntity *pEnt )
{
	CRPMTerminal *p = dynamic_cast<CRPMTerminal *>( pEnt );
	return p ? p->m_szTitle.Get() : "";
}

//-----------------------------------------------------------------------------
// sessions
//-----------------------------------------------------------------------------
struct TermSession_t
{
	CHandle<CRPMTerminal> hTerm;
	EHANDLE hPlayer;
	int token;
};
static TermSession_t s_TS;
static int s_iTermToken = 0;

static void TermStateKey( CRPMTerminal *pTerm, char *out, int len )
{
	RPMLock::LockId( pTerm, out, len );
}

static bool UsedOnce( const char *stateKey, const char *entry )
{
	return RPMState::Section( "terminals_once" )->GetInt( CFmtStr( "%s#%s", stateKey, entry ), 0 ) != 0;
}

static void MarkOnce( const char *stateKey, const char *entry )
{
	RPMState::Section( "terminals_once" )->SetInt( CFmtStr( "%s#%s", stateKey, entry ), 1 );
}

static void AddEntries( KeyValues *pOut, KeyValues *pList, const char *path, const RPMLogic::Ctx_t &ctx, const char *stateKey )
{
	if ( !pList )
		return;
	for ( KeyValues *e = pList->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
	{
		CFmtStr id( "%s%s%s", path, path[0] ? "." : "", e->GetName() );
		KeyValues *c = e->FindKey( "conditions" );
		if ( c && !RPMLogic::Check( c, ctx ) )
			continue;
		if ( e->GetBool( "once" ) && UsedOnce( stateKey, id ) )
			continue;
		KeyValues *o = pOut->CreateNewKey();
		o->SetString( "id", id );
		o->SetString( "title", RPMLogic::Localized( e, "title" ) );
		KeyValues *sub = e->FindKey( "entries" );
		const char *text = RPMLogic::Localized( e, "text" );
		if ( sub && sub->GetFirstTrueSubKey() )
		{
			o->SetString( "kind", "menu" );
			AddEntries( o->FindKey( "entries", true ), sub, id, ctx, stateKey );
		}
		else if ( text[0] )
		{
			o->SetString( "kind", "text" );
			o->SetString( "text", text );
		}
		else
			o->SetString( "kind", "action" );
	}
}

static int LockStatus( CRPMTerminal *pTerm, CBasePlayer *pPlayer, KeyValues *pDef, CUtlString &message )
{
	char key[160];
	TermStateKey( pTerm, key, sizeof( key ) );
	int st = RPMState::Section( "terminals" )->GetInt( key, 0 );
	if ( st == 1 )
		return TLOCK_OPEN;
	if ( st == 2 )
		return TLOCK_LOCKOUT;
	int level = pTerm->LockLevel();
	if ( level <= 0 )
		return TLOCK_OPEN;
	// a password (a note with it) opens it at once
	const char *pw = pDef ? pDef->GetString( "password_item" ) : "";
	if ( pw[0] && InventorySystem::CountItem( pPlayer, pw ) > 0 )
	{
		RPMState::Section( "terminals" )->SetInt( key, 1 );
		message.Format( RPMLogic::IsRussian() ? "> Введён пароль: %s" : "> Password entered: %s", RPMItems::Name( pw ) );
		pTerm->RefreshIdle();
		return TLOCK_OPEN;
	}
	const char *skill = pDef ? pDef->GetString( "lock_skill", "science" ) : "science";
	if ( !skill[0] )
		skill = "science";
	if ( RPMState::PlayerSkill( pPlayer, skill ) < RPMLock::RequiredSkill( level ) )
		return TLOCK_SKILL;
	return TLOCK_HACK;
}

static void SendContent( CBasePlayer *pPlayer, CRPMTerminal *pTerm, const char *pszMessage, bool bOpen )
{
	KeyValues *pDef = pTerm->Def();
	KeyValues *k = new KeyValues( "Term" );
	k->SetInt( "token", s_TS.token );
	k->SetInt( "ent", pTerm->entindex() );
	k->SetInt( "open", bOpen ? 1 : 0 );
	k->SetString( "style", pTerm->m_iStyle == TSTYLE_COMBINE ? "combine" : "pc" );
	k->SetString( "title", pTerm->m_szTitle.Get() );
	k->SetString( "header", pDef ? RPMLogic::Localized( pDef, "header" ) : "" );
	CUtlString msg( pszMessage ? pszMessage : "" );
	int lock = LockStatus( pTerm, pPlayer, pDef, msg );
	k->SetInt( "lock", lock );
	int level = pTerm->LockLevel();
	const char *skill = pDef ? pDef->GetString( "lock_skill", "science" ) : "science";
	if ( !skill[0] )
		skill = "science";
	k->SetInt( "level", level );
	k->SetInt( "skill", RPMState::PlayerSkill( pPlayer, skill ) );
	k->SetInt( "need", RPMLock::RequiredSkill( level ) );
	k->SetString( "skill_name", RPMLogic::SkillName( skill ) );
	k->SetString( "language", RPMItems::Language() );
	if ( !msg.IsEmpty() )
		k->SetString( "message", msg );
	char key[160];
	TermStateKey( pTerm, key, sizeof( key ) );
	if ( lock == TLOCK_OPEN && pDef )
	{
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pSelf = pTerm;
		AddEntries( k->FindKey( "entries", true ), pDef->FindKey( "entries" ), "", ctx, key );
	}

	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	k->RecursiveSaveToFile( buf, 0 );
	k->deleteThis();
	CUtlString text;
	text.SetDirect( (const char *)buf.Base(), buf.TellPut() );
	if ( rpm_terminal_debug.GetBool() )
		Msg( "[terminal] send %d bytes:\n%s\n", text.Length(), text.Get() );

	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	const int CHUNK = 200;
	int len = text.Length();
	for ( int off = 0; off < len || off == 0; off += CHUNK )
	{
		char piece[CHUNK + 1];
		int n = Min( CHUNK, len - off );
		Q_memcpy( piece, text.Get() + off, Max( 0, n ) );
		piece[Max( 0, n )] = 0;
		int flags = ( off == 0 ? 1 : 0 ) | ( off + CHUNK >= len ? 2 : 0 );
		UserMessageBegin( filter, "RPMTerm" );
		WRITE_BYTE( flags );
		WRITE_STRING( piece );
		MessageEnd();
		if ( len == 0 )
			break;
	}
}

void CRPMTerminal::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	if ( !pPlayer )
		return;
	s_TS.hTerm = this;
	s_TS.hPlayer = pPlayer;
	s_TS.token = ++s_iTermToken;
	m_OnUsed.FireOutput( pActivator, this );
	RPMLogic::NoteUse( pPlayer, this );
	if ( rpm_terminal_debug.GetBool() )
		Msg( "[terminal] %s (%s) used, token %d\n", GetDebugName(), STRING( m_iszTerminal ), s_TS.token );
	SendContent( pPlayer, this, NULL, true );
}

static void EndTermSession()
{
	s_TS.hTerm = NULL;
	s_TS.hPlayer = NULL;
	s_TS.token = 0;
}

CON_COMMAND( rpm_terminal_cmd, "HL2RPM (internal): rpm_terminal_cmd <token> hacked|lockout|run <entry>|read <entry>|close - from the terminal screen of the client" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer || args.ArgC() < 3 )
		return;
	if ( !s_TS.token || atoi( args[1] ) != s_TS.token || s_TS.hPlayer.Get() != pPlayer )
		return;
	CRPMTerminal *pTerm = s_TS.hTerm.Get();
	const char *what = args[2];
	if ( !pTerm || !Q_stricmp( what, "close" ) )
	{
		EndTermSession();
		return;
	}
	KeyValues *pDef = pTerm->Def();
	char key[160];
	TermStateKey( pTerm, key, sizeof( key ) );
	bool ru = RPMLogic::IsRussian();

	if ( !Q_stricmp( what, "hacked" ) || !Q_stricmp( what, "lockout" ) )
	{
		CUtlString msg;
		if ( LockStatus( pTerm, pPlayer, pDef, msg ) != TLOCK_HACK )
			return;
		if ( !Q_stricmp( what, "hacked" ) )
		{
			RPMState::Section( "terminals" )->SetInt( key, 1 );
			pTerm->m_OnHacked.FireOutput( pPlayer, pTerm );
			RPMChar::AddXP( pPlayer, (int)RPMRPG::Rule( "xp_hack" ) + pTerm->LockLevel() / 10 );
			pTerm->RefreshIdle();
			SendContent( pPlayer, pTerm, ru ? "> Доступ разрешён" : "> Access granted", false );
		}
		else
		{
			RPMState::Section( "terminals" )->SetInt( key, 2 );
			pTerm->m_OnLockout.FireOutput( pPlayer, pTerm );
			pTerm->RefreshIdle();
			SendContent( pPlayer, pTerm, NULL, false );
		}
		return;
	}

	if ( ( !Q_stricmp( what, "run" ) || !Q_stricmp( what, "read" ) ) && args.ArgC() >= 4 )
	{
		CUtlString msg;
		if ( LockStatus( pTerm, pPlayer, pDef, msg ) != TLOCK_OPEN )
			return;
		const char *id = args[3];
		KeyValues *e = FindEntry( pDef, id );
		if ( !e )
			return;
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pSelf = pTerm;
		KeyValues *c = e->FindKey( "conditions" );
		if ( c && !RPMLogic::Check( c, ctx ) )
			return;
		bool bRead = !Q_stricmp( what, "read" );
		// a record runs its actions the first time it is read; a command every time (or once)
		if ( bRead && UsedOnce( key, CFmtStr( "%s#read", id ) ) )
			return;
		if ( e->GetBool( "once" ) && UsedOnce( key, id ) )
			return;
		if ( rpm_terminal_debug.GetBool() )
			Msg( "[terminal] %s entry %s\n", what, id );
		RPMLogic::Run( e->FindKey( "actions" ), ctx );
		if ( bRead )
			MarkOnce( key, CFmtStr( "%s#read", id ) );
		else if ( e->GetBool( "once" ) )
			MarkOnce( key, id );
		const char *res = RPMLogic::Localized( e, "result" );
		if ( bRead )
			res = "";
		else if ( !res[0] )
			res = ru ? "> Команда выполнена" : "> Command executed";
		SendContent( pPlayer, pTerm, res, false );
		return;
	}
}

CON_COMMAND_F( rpm_terminal_reset, "HL2RPM: forget hacked / locked out terminals and used entries of this map (test)", FCVAR_CHEAT )
{
	const char *secs[2] = { "terminals", "terminals_once" };
	CFmtStr prefix( "%s.", STRING( gpGlobals->mapname ) );
	for ( int s = 0; s < 2; s++ )
	{
		KeyValues *sec = RPMState::Section( secs[s] );
		for ( KeyValues *k = sec->GetFirstSubKey(); k; )
		{
			KeyValues *next = k->GetNextKey();
			if ( !Q_strnicmp( k->GetName(), prefix, Q_strlen( prefix ) ) )
			{
				sec->RemoveSubKey( k );
				k->deleteThis();
			}
			k = next;
		}
	}
	for ( CBaseEntity *p = gEntList.FindEntityByClassname( NULL, "rpm_terminal" ); p; p = gEntList.FindEntityByClassname( p, "rpm_terminal" ) )
		static_cast<CRPMTerminal *>( p )->RefreshIdle();
	Msg( "rpm_terminal_reset: done\n" );
}

CON_COMMAND_F( rpm_terminal_use, "HL2RPM: rpm_terminal_use [name] - use the nearest terminal (or the named one) as if E was pressed (test)", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	CBaseEntity *pBest = NULL;
	float flBest = FLT_MAX;
	for ( CBaseEntity *p = gEntList.FindEntityByClassname( NULL, "rpm_terminal" ); p; p = gEntList.FindEntityByClassname( p, "rpm_terminal" ) )
	{
		if ( args.ArgC() > 1 && !RPMLogic::NameMatches( args[1], p ) )
			continue;
		float d = ( p->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).LengthSqr();
		if ( d < flBest )
		{
			flBest = d;
			pBest = p;
		}
	}
	if ( pBest )
		pBest->Use( pPlayer, pPlayer, USE_TOGGLE, 0 );
	else
		Msg( "rpm_terminal_use: no terminal\n" );
}

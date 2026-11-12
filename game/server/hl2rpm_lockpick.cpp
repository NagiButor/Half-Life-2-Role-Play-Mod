//========= HL2RPM ============================================================//
//
// Lockpicking like in Fallout (M6.5): a locked container or door with a lock level
// (Creation Kit: lock_level / rpm_lock_level) opens the minigame of the client
// (hl2rpm_lockpick_ui.cpp) instead of the old automatic skill check.
//
// The server picks the "sweet spot" of the pin (0..180 degrees) and how wide it is
// (lock level against the Lockpick skill), the client plays and reports:
//   rpm_lockpick_report <token> broke   - a pin broke (one bobby_pin is spent)
//   rpm_lockpick_report <token> open    - the lock turned all the way: unlocked for good
//   rpm_lockpick_report <token> force   - "force the lock": a chance, else jammed forever
//   rpm_lockpick_report <token> cancel  - left the minigame
// A jammed lock opens only with its key. Locks opened once stay open (RPMState
// "unlocked"); experience once per lock (RPMState "picked", as before).
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_char.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "inventory_system.h"
#include "player.h"
#include "BasePropDoor.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern bool RPMLoot_DescribeTarget( CBaseEntity *pEnt, const char *&pszName, bool &bCorpse, bool &bEmpty );

ConVar rpm_lockpick( "rpm_lockpick", "1", FCVAR_NONE, "HL2RPM: locks with a lock level open the lockpicking minigame (0 = the old automatic skill check)" );
static ConVar rpm_lockpick_debug( "rpm_lockpick_debug", "0", FCVAR_CHEAT, "HL2RPM: print the sweet spot of the lockpicking minigame" );

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }

static void Toast( CBasePlayer *pPlayer, const char *msg, const char *snd )
{
	CUtlString m( msg );
	m = m.Replace( '"', '\'' );
	engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"%s\"\n", m.Get(), snd );
}

struct LockpickSession_t
{
	EHANDLE hEnt;
	EHANDLE hPlayer;
	int token;
	int level;
	int skill;
	float sweet;
};

static LockpickSession_t s_Pick;
static int s_iToken = 0;
static EHANDLE s_hPass;		// the lock the minigame just opened: its own Use() lets it through once

namespace RPMLock
{
	void LockId( CBaseEntity *pEnt, char *out, int len )
	{
		const char *map = STRING( gpGlobals->mapname );
		if ( !pEnt )
			Q_snprintf( out, len, "%s.0", map );
		else if ( pEnt->GetEntityName() != NULL_STRING )
			Q_snprintf( out, len, "%s.%s", map, pEnt->GetEntityName().ToCStr() );
		else if ( pEnt->FindContextByName( "rpmref" ) != -1 )
			Q_snprintf( out, len, "%s.ref_%s", map, pEnt->GetContextValue( "rpmref" ) );
		else if ( pEnt->m_iHammerID > 0 )
			Q_snprintf( out, len, "%s.h%d", map, pEnt->m_iHammerID );
		else
			Q_snprintf( out, len, "%s.%d", map, pEnt->entindex() );
	}

	bool IsJammed( CBaseEntity *pEnt )
	{
		char id[160];
		LockId( pEnt, id, sizeof( id ) );
		return RPMState::Section( "jammed" )->GetInt( id, 0 ) != 0;
	}

	bool TakePass( CBaseEntity *pEnt )
	{
		if ( !pEnt || s_hPass.Get() != pEnt )
			return false;
		s_hPass = NULL;
		return true;
	}

	// experience once per lock
	void GrantPicked( CBaseEntity *pEnt, CBasePlayer *pPlayer, int iLevel )
	{
		char id[160];
		LockId( pEnt, id, sizeof( id ) );
		if ( !RPMState::Section( "picked" )->GetInt( id, 0 ) )
		{
			RPMState::Section( "picked" )->SetInt( id, 1 );
			RPMChar::AddXP( pPlayer, (int)RPMRPG::Rule( "xp_lockpick" ) + iLevel / 10 );
		}
	}

	const char *DifficultyName( int iLevel )
	{
		bool ru = IsRussian();
		if ( iLevel < 13 ) return ru ? "Очень легко" : "Very easy";
		if ( iLevel < 38 ) return ru ? "Легко" : "Easy";
		if ( iLevel < 63 ) return ru ? "Средне" : "Average";
		if ( iLevel < 88 ) return ru ? "Сложно" : "Hard";
		return ru ? "Очень сложно" : "Very hard";
	}

	// may the player try at all: the skill no further than 25 below the lock
	int RequiredSkill( int iLevel )
	{
		return Max( 0, iLevel - 25 );
	}

	bool StartMinigame( CBaseEntity *pEnt, CBasePlayer *pPlayer, int iLevel )
	{
		bool ru = IsRussian();
		int skill = RPMState::PlayerSkill( pPlayer, "lockpick" );
		if ( skill < RequiredSkill( iLevel ) )
		{
			Toast( pPlayer, CFmtStr( ru ? "Заперто (%s). Нужен навык «Взлом» %d" : "Locked (%s). Requires Lockpick %d",
				DifficultyName( iLevel ), RequiredSkill( iLevel ) ), "doors/default_locked.wav" );
			return false;
		}
		int pins = InventorySystem::CountItem( pPlayer, RPM_ITEM_BOBBYPIN );
		if ( pins <= 0 )
		{
			Toast( pPlayer, CFmtStr( ru ? "Заперто (%s). Нет шпилек" : "Locked (%s). No bobby pins", DifficultyName( iLevel ) ), "doors/default_locked.wav" );
			return false;
		}

		// the sweet spot: half width in degrees - narrow on hard locks, wider with skill above the lock
		float half = RemapValClamped( (float)iLevel, 0.0f, 100.0f, 16.0f, 5.0f ) + clamp( ( skill - iLevel ) * 0.12f, -3.0f, 8.0f );
		half = clamp( half, 2.5f, 26.0f );
		// how long a pin holds when the lock is forced the wrong way (seconds)
		float hold = RemapValClamped( (float)iLevel, 0.0f, 100.0f, 1.4f, 0.65f ) + clamp( ( skill - iLevel ) * 0.006f, -0.15f, 0.4f );
		hold = Max( 0.4f, hold );

		s_Pick.hEnt = pEnt;
		s_Pick.hPlayer = pPlayer;
		s_Pick.token = ++s_iToken;
		s_Pick.level = iLevel;
		s_Pick.skill = skill;
		s_Pick.sweet = RandomFloat( 6.0f, 174.0f );
		if ( rpm_lockpick_debug.GetBool() )
			Msg( "[lockpick] token %d level %d skill %d sweet %.1f half %.1f hold %.2f\n", s_Pick.token, iLevel, skill, s_Pick.sweet, half, hold );

		const char *pszName = "";
		bool bCorpse = false, bEmpty = false;
		if ( !RPMLoot_DescribeTarget( pEnt, pszName, bCorpse, bEmpty ) || !pszName[0] )
			pszName = dynamic_cast<CBasePropDoor *>( pEnt ) ? ( ru ? "Дверь" : "Door" ) : ( ru ? "Замок" : "Lock" );
		CUtlString name( pszName );
		name = name.Replace( '"', '\'' );
		// force: a chance from the skill against the lock
		int force = (int)( 100.0f * clamp( 0.3f + ( skill - iLevel ) / 100.0f, 0.05f, 0.85f ) );
		engine->ClientCommand( pPlayer->edict(), "rpm_lockpick_ui %d %d %d %.1f %.1f %.2f %d %d \"%s\" \"%s\"\n",
			s_Pick.token, iLevel, skill, s_Pick.sweet, half, hold, pins, force, name.Get(), DifficultyName( iLevel ) );
		return true;
	}
}

static void EndSession()
{
	s_Pick.hEnt = NULL;
	s_Pick.hPlayer = NULL;
	s_Pick.token = 0;
}

// the lock gives way: unlocked for good, the entity's own Use() opens it (loot window / door)
static void OpenPicked( CBasePlayer *pPlayer, CBaseEntity *pEnt, int iLevel, bool bForced )
{
	char id[160];
	RPMLock::LockId( pEnt, id, sizeof( id ) );
	RPMState::Section( "unlocked" )->SetInt( id, 1 );
	Toast( pPlayer, bForced ? ( IsRussian() ? "Замок вскрыт силой" : "Lock forced open" ) : ( IsRussian() ? "Замок взломан" : "Lock picked" ),
		"doors/latchunlocked1.wav" );
	RPMLock::GrantPicked( pEnt, pPlayer, iLevel );
	s_hPass = pEnt;
	pEnt->Use( pPlayer, pPlayer, USE_TOGGLE, 0 );
	s_hPass = NULL;
}

CON_COMMAND( rpm_lockpick_report, "HL2RPM (internal): rpm_lockpick_report <token> broke|open|force|cancel - the lockpicking minigame of the client" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer || args.ArgC() < 3 )
		return;
	if ( atoi( args[1] ) != s_Pick.token || !s_Pick.token || s_Pick.hPlayer.Get() != pPlayer )
		return;
	CBaseEntity *pEnt = s_Pick.hEnt.Get();
	const char *what = args[2];
	bool ru = IsRussian();
	if ( !pEnt || ( pEnt->WorldSpaceCenter() - pPlayer->EyePosition() ).Length() > 200.0f )
	{
		EndSession();
		return;
	}

	if ( !Q_stricmp( what, "broke" ) )
	{
		InventorySystem::TakeItems( pPlayer, RPM_ITEM_BOBBYPIN, 1 );
		InventorySystem::SendInventoryToPlayer( pPlayer );
		int left = InventorySystem::CountItem( pPlayer, RPM_ITEM_BOBBYPIN );
		engine->ClientCommand( pPlayer->edict(), "rpm_lockpick_pins %d %d\n", s_Pick.token, left );
		if ( left <= 0 )
			EndSession();
		return;
	}
	if ( !Q_stricmp( what, "open" ) )
	{
		int lvl = s_Pick.level;
		EndSession();
		OpenPicked( pPlayer, pEnt, lvl, false );
		return;
	}
	if ( !Q_stricmp( what, "force" ) )
	{
		int lvl = s_Pick.level;
		float chance = clamp( 0.3f + ( s_Pick.skill - lvl ) / 100.0f, 0.05f, 0.85f );
		EndSession();
		if ( RandomFloat( 0.0f, 1.0f ) < chance )
		{
			OpenPicked( pPlayer, pEnt, lvl, true );
			return;
		}
		// failed: the pin breaks and the lock is jammed - only its key opens it now
		InventorySystem::TakeItems( pPlayer, RPM_ITEM_BOBBYPIN, 1 );
		InventorySystem::SendInventoryToPlayer( pPlayer );
		char id[160];
		RPMLock::LockId( pEnt, id, sizeof( id ) );
		RPMState::Section( "jammed" )->SetInt( id, 1 );
		Toast( pPlayer, ru ? "Замок заклинило. Теперь его откроет только ключ" : "The lock is jammed. Only its key opens it now", "physics/metal/metal_box_break1.wav" );
		return;
	}
	EndSession();
}

CON_COMMAND_F( rpm_lockpick_reset, "HL2RPM: forget picked / jammed locks of this map (test)", FCVAR_CHEAT )
{
	KeyValues *secs[2] = { RPMState::Section( "unlocked" ), RPMState::Section( "jammed" ) };
	CFmtStr prefix( "%s.", STRING( gpGlobals->mapname ) );
	for ( int s = 0; s < 2; s++ )
	{
		for ( KeyValues *k = secs[s]->GetFirstSubKey(); k; )
		{
			KeyValues *next = k->GetNextKey();
			if ( !Q_strnicmp( k->GetName(), prefix, Q_strlen( prefix ) ) )
			{
				secs[s]->RemoveSubKey( k );
				k->deleteThis();
			}
			k = next;
		}
	}
	Msg( "rpm_lockpick_reset: done (the locks themselves stay as they are until the map is reloaded)\n" );
}

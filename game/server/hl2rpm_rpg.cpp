//========= HL2RPM ============================================================//
//
// RPG state of the game (see hl2rpm_rpg.h). The state is one KeyValues tree, saved
// as text in its own save/restore block: new fields don't break old saves.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_char.h"
#include "hl2rpm_world.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_logic.h"
#include "ai_basenpc.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "inventory_system.h"
#include "player.h"
#include "saverestore.h"
#include "saverestore_utlvector.h"
#include "isaverestore.h"
#include "timecycle/env_timecycle.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static KeyValues *g_pRPMState = NULL;

static KeyValues *State()
{
	if ( !g_pRPMState )
		g_pRPMState = new KeyValues( "RPMState" );
	return g_pRPMState;
}

static void ResetState()
{
	if ( g_pRPMState )
		g_pRPMState->deleteThis();
	g_pRPMState = new KeyValues( "RPMState" );
}

namespace RPMState
{
	bool GetFlag( const char *name )
	{
		return State()->FindKey( "flags", true )->GetInt( name, 0 ) != 0;
	}

	void SetFlag( const char *name, bool on )
	{
		KeyValues *f = State()->FindKey( "flags", true );
		if ( on )
			f->SetInt( name, 1 );
		else if ( KeyValues *k = f->FindKey( name ) )
			f->RemoveSubKey( k ), k->deleteThis();
	}

	int GetVar( const char *name, int def )
	{
		return State()->FindKey( "vars", true )->GetInt( name, def );
	}

	void SetVar( const char *name, int value )
	{
		State()->FindKey( "vars", true )->SetInt( name, value );
	}

	KeyValues *Section( const char *name )
	{
		return State()->FindKey( name, true );
	}

	double GameHours()
	{
		KeyValues *c = State()->FindKey( "clock", true );
		return c->GetInt( "day", 0 ) * 24.0 + c->GetFloat( "hours", 0.0f );
	}

	int PlayerLevel( CBasePlayer *pPlayer )
	{
		return RPMChar::Level();
	}

	int PlayerSkill( CBasePlayer *pPlayer, const char *skill )
	{
		return RPMChar::Skill( skill );		// attributes, tag, points, perks (hl2rpm_char.cpp)
	}
}

// the day counter: the time of day wraps over midnight
class CRPMStateSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMStateSystem() : CAutoGameSystemPerFrame( "CRPMStateSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		if ( gpGlobals->eLoadType == MapLoad_NewGame )
			ResetState();
	}

	virtual void FrameUpdatePostEntityThink()
	{
		float h;
		if ( !WorldClock_GetHours( h ) )
			return;
		KeyValues *c = State()->FindKey( "clock", true );
		float last = c->GetFloat( "hours", -1.0f );
		if ( last >= 0.0f && h + 12.0f < last )		// went past midnight (not a jump backwards by a command)
			c->SetInt( "day", c->GetInt( "day", 0 ) + 1 );
		c->SetFloat( "hours", h );
	}
};
static CRPMStateSystem g_RPMStateSystem;

//-----------------------------------------------------------------------------
// Save / restore: the tree as text
//-----------------------------------------------------------------------------
static short RPMSTATE_SAVE_VERSION = 1;

class CRPMStateSaveRestoreBlockHandler : public CDefSaveRestoreBlockHandler
{
public:
	const char *GetBlockName() { return "RPMState"; }

	void WriteSaveHeaders( ISave *pSave )
	{
		pSave->WriteShort( &RPMSTATE_SAVE_VERSION );
	}

	void Save( ISave *pSave )
	{
		CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
		State()->RecursiveSaveToFile( buf, 0 );
		int len = buf.TellPut();
		pSave->StartBlock( "RPMStateText" );
		pSave->WriteInt( &len );
		if ( len > 0 )
			pSave->WriteData( (const char *)buf.Base(), len );
		pSave->EndBlock();
	}

	void ReadRestoreHeaders( IRestore *pRestore )
	{
		short version;
		pRestore->ReadShort( &version );
		m_bDoLoad = ( version == RPMSTATE_SAVE_VERSION ) && ( gpGlobals->eLoadType == MapLoad_LoadGame );
	}

	void Restore( IRestore *pRestore, bool createPlayers )
	{
		if ( !m_bDoLoad )
			return;
		pRestore->StartBlock();
		int len = 0;
		pRestore->ReadInt( &len );
		ResetState();
		if ( len > 0 && len < 16 * 1024 * 1024 )
		{
			char *data = new char[len + 1];
			pRestore->ReadData( data, len, len );
			data[len] = 0;
			CUtlBuffer buf( data, len, CUtlBuffer::TEXT_BUFFER | CUtlBuffer::READ_ONLY );
			g_pRPMState->LoadFromBuffer( "RPMState", buf );
			delete[] data;
		}
		pRestore->EndBlock();
	}

private:
	bool m_bDoLoad = false;
};

static CRPMStateSaveRestoreBlockHandler g_RPMStateSaveRestore;

ISaveRestoreBlockHandler *GetRPMStateSaveRestoreBlockHandler()
{
	return &g_RPMStateSaveRestore;
}

//-----------------------------------------------------------------------------
// Loot lists: scripts/rpg/lootlists.txt
//-----------------------------------------------------------------------------
namespace RPMLoot
{
	static KeyValues *g_pLists = NULL;
	static long g_ListsTime = 0;

	static KeyValues *Lists()
	{
		long t = filesystem->GetFileTime( "scripts/rpg/lootlists.txt", "GAME" );
		if ( !g_pLists || t != g_ListsTime )
		{
			if ( g_pLists )
				g_pLists->deleteThis();
			g_pLists = new KeyValues( "LootLists" );
			g_pLists->LoadFromFile( filesystem, "scripts/rpg/lootlists.txt", "GAME" );
			g_ListsTime = t;
		}
		return g_pLists;
	}

	static void AddItem( CUtlString &out, const char *item, int count )
	{
		if ( count <= 0 || !item[0] )
			return;
		bool bStack = RPMItems::IsStackable( item ) || InventorySystem::IsAmmoItem( item );
		int n = bStack ? 1 : count;
		for ( int i = 0; i < n; i++ )
		{
			if ( !out.IsEmpty() )
				out.Append( ";" );
			out.Append( item );
			if ( bStack && count > 1 )
				out.Append( CFmtStr( ":%d", count ).Access() );
		}
	}

	static void Roll( const char *id, CUtlString &out, int level, int depth )
	{
		KeyValues *l = depth < 5 ? Lists()->FindKey( id ) : NULL;
		if ( !l )
		{
			Warning( "RPMLoot: no loot list '%s'\n", id );
			return;
		}
		bool bOne = !Q_stricmp( l->GetString( "pick", "" ), "one" );
		CUtlVector<KeyValues *> ok;
		int total = 0;
		for ( KeyValues *e = l->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
		{
			if ( level < e->GetInt( "level", 0 ) )
				continue;
			if ( bOne )
			{
				ok.AddToTail( e );
				total += Max( 1, e->GetInt( "chance", 100 ) );
				continue;
			}
			if ( RandomInt( 1, 100 ) > e->GetInt( "chance", 100 ) )
				continue;
			ok.AddToTail( e );
		}
		if ( bOne && ok.Count() )
		{
			int r = RandomInt( 1, total );
			KeyValues *pick = ok[0];
			for ( int i = 0; i < ok.Count(); i++ )
			{
				r -= Max( 1, ok[i]->GetInt( "chance", 100 ) );
				if ( r <= 0 )
				{
					pick = ok[i];
					break;
				}
			}
			ok.RemoveAll();
			ok.AddToTail( pick );
		}
		for ( int i = 0; i < ok.Count(); i++ )
		{
			KeyValues *e = ok[i];
			const char *item = e->GetString( "item", "" );
			int lo = e->GetInt( "min", 1 ), hi = Max( lo, e->GetInt( "max", lo ) );
			int n = RandomInt( lo, hi );
			if ( !Q_strnicmp( item, "list:", 5 ) )
			{
				for ( int k = 0; k < n; k++ )
					Roll( item + 5, out, level, depth + 1 );
			}
			else
				AddItem( out, item, n );
		}
	}

	bool Resolve( const char *pszIn, CUtlString &out, CBasePlayer *pPlayer )
	{
		out.Clear();
		if ( !pszIn || !Q_stristr( pszIn, "list:" ) )
		{
			out = pszIn ? pszIn : "";
			return false;
		}
		int level = RPMState::PlayerLevel( pPlayer );
		CUtlStringList parts;
		V_SplitString( pszIn, ";", parts );
		for ( int i = 0; i < parts.Count(); i++ )
		{
			const char *p = parts[i];
			while ( *p == ' ' )
				p++;
			if ( !Q_strnicmp( p, "list:", 5 ) )
				Roll( p + 5, out, level, 0 );
			else if ( p[0] )
			{
				if ( !out.IsEmpty() )
					out.Append( ";" );
				out.Append( p );
			}
		}
		return true;
	}
}

//-----------------------------------------------------------------------------
// Locks
//-----------------------------------------------------------------------------
static bool PlayerHasItem( CBasePlayer *pPlayer, const char *item )
{
	CUtlVector<CUtlString> items;
	InventorySystem::GetInventoryItemStringsForPlayer( pPlayer, items );
	int n = Q_strlen( item );
	for ( int i = 0; i < items.Count(); i++ )
	{
		const char *s = items[i].Get();
		if ( !Q_strnicmp( s, item, n ) && ( s[n] == 0 || s[n] == ':' ) )
			return true;
	}
	return false;
}

//=============================================================================
// Theft (New Vegas): the owner of a container is an NPC or a faction
//=============================================================================
namespace RPMTheft
{
	const char *OwnerName( const char *pszOwner )
	{
		if ( !pszOwner || !pszOwner[0] )
			return "";
		const char *n = RPMWorld::GetNPCDisplayName( pszOwner );
		if ( n && n[0] )
			return n;
		n = RPMWorld::FactionName( pszOwner );
		return n && n[0] ? n : pszOwner;
	}

	// the faction behind the owner: the faction itself, or the faction of the owner NPC
	static const char *OwnerFaction( const char *pszOwner )
	{
		CBaseEntity *pOwner = gEntList.FindEntityByName( NULL, pszOwner );
		CAI_BaseNPC *pNPC = pOwner ? pOwner->MyNPCPointer() : NULL;
		if ( pNPC )
			return pNPC->GetFactionTag();
		return pszOwner;
	}

	void OnTake( CBasePlayer *pPlayer, CBaseEntity *pContainer, const char *pszOwner )
	{
		if ( !pPlayer || !pContainer || !pszOwner || !pszOwner[0] )
			return;
		// once per container in a few seconds (take all, several items in a row)
		static EHANDLE s_hLast;
		static float s_flLast = -100.0f;
		if ( s_hLast.Get() == pContainer && gpGlobals->curtime - s_flLast < 5.0f )
			return;
		const char *pszFaction = OwnerFaction( pszOwner );
		bool bSeen = false;
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *pNPC = ppAIs[i];
			if ( !pNPC || !pNPC->IsAlive() )
				continue;
			const bool bOwner = pNPC->GetEntityName() != NULL_STRING && !Q_stricmp( STRING( pNPC->GetEntityName() ), pszOwner );
			const char *pszTag = pNPC->GetFactionTag();
			const bool bFaction = pszFaction && pszFaction[0] && pszTag && pszTag[0] && !Q_stricmp( pszTag, pszFaction );
			if ( !bOwner && !bFaction )
				continue;
			const float d = ( pNPC->EyePosition() - pPlayer->EyePosition() ).Length();
			if ( d > 1200.0f || ( d > 150.0f && ( !pNPC->FInViewCone( pPlayer ) || !pNPC->FVisible( pPlayer ) ) ) )
				continue;
			bSeen = true;
			RPMAudio::Bark( pNPC, "theft" );
			pNPC->AddEntityRelationship( pPlayer, D_HT, 50 );
			DevMsg( "RPMTheft: %s saw the player steal from %s\n", pNPC->GetDebugName(), pContainer->GetDebugName() );
		}
		if ( !bSeen )
			return;
		s_hLast = pContainer;
		s_flLast = gpGlobals->curtime;
		RPMLogic::Toast( pPlayer, "rep", !Q_stricmp( RPMItems::Language(), "russian" ) ? "Кражу заметили!" : "Your theft was seen!", "buttons/button10.wav" );
		if ( pszFaction && pszFaction[0] && RPMWorld::FactionName( pszFaction )[0] )
			RPMChar::AddFactionRep( pPlayer, pszFaction, -(int)RPMRPG::Rule( "rep_theft" ) );
	}
}

namespace RPMLock
{
	bool TryOpen( CBaseEntity *pEnt, CBasePlayer *pPlayer, const char *pszKey, int iLevel )
	{
		if ( !pPlayer )
			return false;
		// the lockpicking minigame has just opened it
		if ( RPMLock::TakePass( pEnt ) )
			return true;
		bool ru = !Q_stricmp( RPMItems::Language(), "russian" );
		if ( pszKey && pszKey[0] && PlayerHasItem( pPlayer, pszKey ) )
		{
			engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s: %s\" \"doors/default_locked.wav\"\n",
				ru ? "Открыто ключом" : "Unlocked with", RPMItems::Name( pszKey ) );
			return true;
		}
		char id[160];
		RPMLock::LockId( pEnt, id, sizeof( id ) );
		if ( RPMState::Section( "unlocked" )->GetInt( id, 0 ) )
			return true;
		// a failed "force" jammed it
		if ( RPMLock::IsJammed( pEnt ) )
		{
			char msg[256];
			if ( pszKey && pszKey[0] )
				Q_snprintf( msg, sizeof( msg ), ru ? "Замок заклинило. Нужен ключ: %s" : "The lock is jammed. Requires: %s", RPMItems::Name( pszKey ) );
			else
				Q_strncpy( msg, ru ? "Замок заклинило" : "The lock is jammed", sizeof( msg ) );
			engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"doors/default_locked.wav\"\n", msg );
			return false;
		}
		if ( iLevel > 0 && iLevel <= 100 )
		{
			static ConVarRef rpm_lockpick( "rpm_lockpick" );
			if ( !rpm_lockpick.IsValid() || rpm_lockpick.GetBool() )
			{
				// the minigame (or a message why not); the lock opens later from there
				RPMLock::StartMinigame( pEnt, pPlayer, iLevel );
				return false;
			}
			if ( RPMState::PlayerSkill( pPlayer, "lockpick" ) >= iLevel )
			{
				engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"doors/latchunlocked1.wav\"\n",
					ru ? "Замок взломан" : "Lock picked" );
				RPMLock::GrantPicked( pEnt, pPlayer, iLevel );
				return true;
			}
		}
		char msg[256];
		if ( pszKey && pszKey[0] && iLevel > 0 && iLevel <= 100 )
			Q_snprintf( msg, sizeof( msg ), ru ? "Заперто. Нужен ключ: %s или Взлом %d" : "Locked. Requires: %s or Lockpick %d", RPMItems::Name( pszKey ), iLevel );
		else if ( pszKey && pszKey[0] )
			Q_snprintf( msg, sizeof( msg ), ru ? "Заперто. Нужен ключ: %s" : "Locked. Requires: %s", RPMItems::Name( pszKey ) );
		else if ( iLevel > 0 && iLevel <= 100 )
			Q_snprintf( msg, sizeof( msg ), ru ? "Заперто. Взлом %d" : "Locked. Lockpick %d", iLevel );
		else
			Q_strncpy( msg, ru ? "Заперто" : "Locked", sizeof( msg ) );
		engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"doors/default_locked.wav\"\n", msg );
		return false;
	}
}

//-----------------------------------------------------------------------------
// Console
//-----------------------------------------------------------------------------
CON_COMMAND( rpm_flag, "HL2RPM: rpm_flag <name> [0|1] - show or set a world flag" )
{
	if ( args.ArgC() < 2 )
		return;
	if ( args.ArgC() >= 3 )
		RPMState::SetFlag( args[1], atoi( args[2] ) != 0 );
	Msg( "flag %s = %d\n", args[1], RPMState::GetFlag( args[1] ) ? 1 : 0 );
}

CON_COMMAND( rpm_var, "HL2RPM: rpm_var <name> [value] - show or set a world variable" )
{
	if ( args.ArgC() < 2 )
		return;
	if ( args.ArgC() >= 3 )
		RPMState::SetVar( args[1], atoi( args[2] ) );
	Msg( "var %s = %d\n", args[1], RPMState::GetVar( args[1] ) );
}

CON_COMMAND( rpm_state_dump, "HL2RPM: print the RPG state (flags, variables, clock, player)" )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	State()->RecursiveSaveToFile( buf, 0 );
	buf.PutChar( 0 );
	Msg( "%s\nRPMState: game time %.1f h (day %d)\n", (const char *)buf.Base(), RPMState::GameHours(), (int)( RPMState::GameHours() / 24.0 ) );
}

CON_COMMAND( rpm_loot_roll, "HL2RPM: rpm_loot_roll <list id> - roll a loot list and print what drops (test)" )
{
	if ( args.ArgC() < 2 )
		return;
	CUtlString out;
	RPMLoot::Resolve( CFmtStr( "list:%s", args[1] ), out, UTIL_GetLocalPlayer() );
	Msg( "loot: %s\n", out.Get() );
}

CON_COMMAND_F( rpm_give, "HL2RPM: rpm_give <item id> [count] - put items of the database into the player's inventory (test)", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer || args.ArgC() < 2 )
		return;
	int n = args.ArgC() > 2 ? Max( 1, atoi( args[2] ) ) : 1;
	if ( RPMItems::IsStackable( args[1] ) || InventorySystem::IsAmmoItem( args[1] ) )
		InventorySystem::AddItemToPlayerWithBullets( pPlayer, args[1], n );
	else
		for ( int i = 0; i < n; i++ )
			InventorySystem::AddItemToPlayer( pPlayer, args[1] );
	Msg( "rpm_give: %s x%d (now %d)\n", args[1], n, InventorySystem::CountItem( pPlayer, args[1] ) );
}

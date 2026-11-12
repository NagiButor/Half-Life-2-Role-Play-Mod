//========= HL2RPM ============================================================//
//
// Conditions, actions and events of the Creation Kit (see hl2rpm_logic.h).
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_items.h"
#include "hl2rpm_world.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_char.h"
#include "hl2rpm_rpgdefs.h"
#include "quest_system.h"
#include "dialog_definitions.h"
#include "dialogsystem_server.h"
#include "inventory_system.h"
#include "player.h"
#include "ai_basenpc.h"
#include "eventqueue.h"
#include "world.h"
#include "weather/env_weather.h"
#include "weather/weather_shared.h"
#include "timecycle/env_timecycle.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar rpm_logic_debug( "rpm_logic_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print actions, events and presence changes of the Creation Kit as they run (2 - also every condition checked)" );
ConVar rpm_test_state( "rpm_test_state", "", FCVAR_GAMEDLL, "HL2RPM: a file (scripts/...) with the state to apply once the map is loaded: quests, flags, variables, items, skills, time, weather (Creation Kit: play from here)" );

namespace RPMLogic
{
	static CBasePlayer *Player( const Ctx_t &ctx )
	{
		return ctx.pPlayer ? ctx.pPlayer : UTIL_GetLocalPlayer();
	}

	bool IsRussian()
	{
		return !Q_stricmp( DialogDefinitions::GetDialogLanguage(), "russian" );
	}

	const char *Localized( KeyValues *pKV, const char *pszKey )
	{
		if ( !pKV )
			return "";
		const char *psz = pKV->GetString( CFmtStr( "%s_%s", pszKey, DialogDefinitions::GetDialogLanguage() ), "" );
		return psz[0] ? psz : pKV->GetString( pszKey, "" );
	}

	static CUtlString Escape( const char *s )
	{
		CUtlString out;
		for ( const char *p = s ? s : ""; *p; ++p )
		{
			if ( *p == '"' )
				out.Append( "'" );
			else if ( *p == '\n' || *p == '\r' )
				out.Append( " " );
			else
			{
				char t[2] = { *p, 0 };
				out.Append( t );
			}
		}
		return out;
	}

	void Toast( CBasePlayer *pPlayer, const char *pszCategory, const char *pszText, const char *pszSound )
	{
		if ( !pPlayer || !pszText || !pszText[0] )
			return;
		engine->ClientCommand( pPlayer->edict(), "ui_toast \"%s\" \"%s\" \"%s\"\n", pszCategory ? pszCategory : "ui",
			Escape( pszText ).Get(), Escape( pszSound ).Get() );
	}

	void AddXP( CBasePlayer *pPlayer, int xp )
	{
		RPMChar::AddXP( pPlayer, xp );		// levels, perks, the toast
	}

	//-------------------------------------------------------------------------
	// names
	//-------------------------------------------------------------------------
	static const struct { const char *id, *ru, *en; } s_Skills[] =
	{
		{ "guns", "Огнестрел", "Guns" },
		{ "energy", "Оружие Альянса", "Combine Weapons" },
		{ "melee", "Ближний бой", "Melee" },
		{ "explosives", "Взрывчатка", "Explosives" },
		{ "lockpick", "Взлом", "Lockpick" },
		{ "science", "Наука", "Science" },
		{ "repair", "Ремонт", "Repair" },
		{ "medicine", "Медицина", "Medicine" },
		{ "sneak", "Скрытность", "Sneak" },
		{ "speech", "Красноречие", "Speech" },
		{ "barter", "Бартер", "Barter" },
		{ "survival", "Выживание", "Survival" },
		{ "str", "Сила", "Strength" },
		{ "per", "Восприятие", "Perception" },
		{ "end", "Выносливость", "Endurance" },
		{ "cha", "Обаяние", "Charisma" },
		{ "int", "Интеллект", "Intelligence" },
		{ "agi", "Ловкость", "Agility" },
		{ "lck", "Удача", "Luck" },
		{ "level", "Уровень", "Level" },
	};

	const char *SkillName( const char *pszId )
	{
		for ( int i = 0; i < ARRAYSIZE( s_Skills ); i++ )
			if ( !Q_stricmp( s_Skills[i].id, pszId ) )
				return IsRussian() ? s_Skills[i].ru : s_Skills[i].en;
		return pszId;
	}

	static int PlayerStat( const char *pszId )
	{
		return RPMChar::Stat( pszId );
	}

	//-------------------------------------------------------------------------
	// targets
	//-------------------------------------------------------------------------
	static bool WildMatch( const char *pat, const char *s )
	{
		// '*' - any run of characters, case-insensitive
		for ( ; *pat; ++pat, ++s )
		{
			if ( *pat == '*' )
			{
				while ( pat[1] == '*' )
					++pat;
				if ( !pat[1] )
					return true;
				for ( ; *s; ++s )
					if ( WildMatch( pat + 1, s ) )
						return true;
				return false;
			}
			if ( !*s || tolower( (unsigned char)*pat ) != tolower( (unsigned char)*s ) )
				return false;
		}
		return !*s;
	}

	static bool AnyPatternMatches( const char *pszPatterns, const char *pszName )
	{
		if ( !pszName || !pszName[0] )
			return false;
		CUtlStringList parts;
		V_SplitString( pszPatterns, ";", parts );
		for ( int i = 0; i < parts.Count(); i++ )
		{
			char *p = parts[i];
			while ( *p == ' ' )
				++p;
			if ( p[0] && WildMatch( p, pszName ) )
				return true;
		}
		return false;
	}

	bool NameMatches( const char *pszPatterns, CBaseEntity *pEnt )
	{
		if ( !pEnt || !pszPatterns || !pszPatterns[0] )
			return false;
		return AnyPatternMatches( pszPatterns, pEnt->GetEntityName().ToCStr() ) || AnyPatternMatches( pszPatterns, pEnt->GetClassname() );
	}

	CBaseEntity *FindTarget( const char *pszName, const Ctx_t &ctx )
	{
		if ( !pszName || !pszName[0] || !Q_stricmp( pszName, "!self" ) )
			return ctx.pSelf;
		if ( !Q_stricmp( pszName, "!player" ) )
			return Player( ctx );
		if ( !Q_stricmp( pszName, "!other" ) )
			return ctx.pOther;
		// the nearest living one of that name (several NPCs of one definition share it)
		CBasePlayer *pPlayer = Player( ctx );
		CBaseEntity *pBest = NULL;
		float flBest = FLT_MAX;
		for ( CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, pszName ); pEnt; pEnt = gEntList.FindEntityByName( pEnt, pszName ) )
		{
			float d = pPlayer ? ( pEnt->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).LengthSqr() : 0.0f;
			if ( pEnt->IsAlive() || !pEnt->MyCombatCharacterPointer() )
				d *= 0.001f;		// living before dead
			if ( d < flBest )
			{
				flBest = d;
				pBest = pEnt;
			}
		}
		return pBest;
	}

	bool TargetPos( const char *pszName, const Ctx_t &ctx, Vector &pos )
	{
		int zone = RPMWorld::FindZone( pszName );
		if ( zone >= 0 )
		{
			pos = RPMWorld::ZoneCenter( zone );
			return true;
		}
		CBaseEntity *pEnt = FindTarget( pszName, ctx );
		if ( !pEnt )
			return false;
		pos = pEnt->WorldSpaceCenter();
		return true;
	}

	//-------------------------------------------------------------------------
	// conditions
	//-------------------------------------------------------------------------
	static bool Compare( const char *op, float a, float b )
	{
		if ( !op || !op[0] || !Q_strcmp( op, ">=" ) )	return a >= b;
		if ( !Q_strcmp( op, "==" ) || !Q_strcmp( op, "=" ) )	return a == b;
		if ( !Q_strcmp( op, "!=" ) )	return a != b;
		if ( !Q_strcmp( op, "<=" ) )	return a <= b;
		if ( !Q_strcmp( op, ">" ) )	return a > b;
		if ( !Q_strcmp( op, "<" ) )	return a < b;
		return a >= b;
	}

	static int QuestStateFromString( const char *s )
	{
		if ( !Q_stricmp( s, "active" ) )	return kQuestState_Active;
		if ( !Q_stricmp( s, "completed" ) || !Q_stricmp( s, "complete" ) || !Q_stricmp( s, "done" ) )	return kQuestState_Completed;
		if ( !Q_stricmp( s, "failed" ) || !Q_stricmp( s, "fail" ) )	return kQuestState_Failed;
		return kQuestState_Inactive;
	}

	static const char *CurrentWeather()
	{
		CEnvWeather *pW = dynamic_cast<CEnvWeather *>( gEntList.FindEntityByClassname( NULL, "env_weather" ) );
		if ( !pW )
			return "";
		return GetWeatherPresetInfo( pW->GetTargetPreset() ).pszName;
	}

	const char *CurrentWeatherName()
	{
		return CurrentWeather();
	}

	static bool CheckRaw( KeyValues *c, const Ctx_t &ctx )
	{
		const char *type = c->GetString( "type" );
		const char *op = c->GetString( "op" );
		CBasePlayer *pPlayer = Player( ctx );
		const char *pszQuest = c->GetString( "quest" );
		if ( !pszQuest[0] && ctx.pszQuest )
			pszQuest = ctx.pszQuest;

		if ( !Q_stricmp( type, "quest_state" ) )
		{
			const PlayerQuestState *st = pPlayer ? QuestSystem().FindPlayerQuest( pPlayer, pszQuest ) : NULL;
			int state = st ? (int)st->state : (int)kQuestState_Inactive;
			bool eq = state == QuestStateFromString( c->GetString( "value", "active" ) );
			return !Q_strcmp( op, "!=" ) ? !eq : eq;
		}
		if ( !Q_stricmp( type, "quest_stage" ) )
		{
			const PlayerQuestState *st = pPlayer ? QuestSystem().FindPlayerQuest( pPlayer, pszQuest ) : NULL;
			return st && st->state == kQuestState_Active && Compare( op, st->stage, c->GetFloat( "value" ) );
		}
		if ( !Q_stricmp( type, "objective" ) )
			return QuestSystem().IsObjectiveDone( pszQuest, c->GetString( "obj" ) );
		if ( !Q_stricmp( type, "flag" ) )
			return RPMState::GetFlag( c->GetString( "name" ) ) == ( c->GetInt( "value", 1 ) != 0 );
		if ( !Q_stricmp( type, "var" ) )
			return Compare( op, RPMState::GetVar( c->GetString( "name" ) ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "time" ) )
		{
			float h = RPMWorld::GetHours();
			float from = c->GetFloat( "from", 0 ), to = c->GetFloat( "to", 24 );
			return ( from <= to ) ? ( h >= from && h < to ) : ( h >= from || h < to );
		}
		if ( !Q_stricmp( type, "day" ) )
			return Compare( op, (int)( RPMState::GameHours() / 24.0 ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "weather" ) )
			return AnyPatternMatches( c->GetString( "value" ), CurrentWeather() );
		if ( !Q_stricmp( type, "map" ) )
			return AnyPatternMatches( c->GetString( "value" ), STRING( gpGlobals->mapname ) );
		if ( !Q_stricmp( type, "item" ) )
			return pPlayer && Compare( op, InventorySystem::CountItem( pPlayer, c->GetString( "item" ) ), c->GetFloat( "value", 1 ) );
		if ( !Q_stricmp( type, "level" ) )
			return Compare( op, RPMState::PlayerLevel( pPlayer ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "skill" ) )
			return Compare( op, RPMState::PlayerSkill( pPlayer, c->GetString( "name" ) ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "stat" ) )
			return Compare( op, PlayerStat( c->GetString( "name" ) ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "rep" ) )
		{
			const char *pszFaction = c->GetString( "faction" );
			if ( pszFaction[0] )
				return Compare( op, RPMChar::FactionRep( pszFaction ), c->GetFloat( "value" ) );
			return pPlayer && Compare( op, QuestSystem().GetReputation( pPlayer ), c->GetFloat( "value" ) );
		}
		if ( !Q_stricmp( type, "perk" ) )
			return RPMChar::HasPerk( c->GetString( "name" ) );
		if ( !Q_stricmp( type, "created" ) )
			return RPMChar::IsCreated();
		if ( !Q_stricmp( type, "health" ) )
			return pPlayer && Compare( op, 100.0f * pPlayer->GetHealth() / Max( 1, pPlayer->GetMaxHealth() ), c->GetFloat( "value" ) );
		if ( !Q_stricmp( type, "suit" ) )
			return pPlayer && pPlayer->IsSuitEquipped() == ( c->GetInt( "value", 1 ) != 0 );
		if ( !Q_stricmp( type, "alive" ) )
		{
			CBaseEntity *pEnt = FindTarget( c->GetString( "target" ), ctx );
			return pEnt && pEnt->IsAlive();
		}
		if ( !Q_stricmp( type, "hostile" ) )
		{
			CBaseEntity *pEnt = FindTarget( c->GetString( "target" ), ctx );
			CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
			return pNPC && pPlayer && pNPC->IRelationType( pPlayer ) == D_HT;
		}
		if ( !Q_stricmp( type, "near" ) )
		{
			Vector pos;
			return pPlayer && TargetPos( c->GetString( "target" ), ctx, pos ) &&
				( pos - pPlayer->GetAbsOrigin() ).Length() <= c->GetFloat( "value", 256 );
		}
		if ( !Q_stricmp( type, "zone" ) )
		{
			int z = RPMWorld::FindZone( c->GetString( "name" ) );
			return pPlayer && z >= 0 && RPMWorld::IsInZone( z, pPlayer->GetAbsOrigin() );
		}
		if ( !Q_stricmp( type, "talked" ) )
			return DialogDefinitions::IsEntitySpoken( c->GetString( "target" ) );
		if ( !Q_stricmp( type, "event" ) )
			return RPMState::Section( "events" )->GetInt( c->GetString( "name" ), 0 ) > 0;
		if ( !Q_stricmp( type, "chance" ) )
			return RandomFloat( 0, 100 ) < c->GetFloat( "value", 50 );
		if ( !Q_stricmp( type, "always" ) )
			return true;
		Warning( "RPMLogic: unknown condition '%s'\n", type );
		return false;
	}

	bool CheckOne( KeyValues *c, const Ctx_t &ctx )
	{
		bool v = CheckRaw( c, ctx );
		if ( c->GetBool( "not" ) )
			v = !v;
		if ( rpm_logic_debug.GetInt() >= 2 )
			Msg( "[logic] condition %s %s%s -> %d\n", c->GetString( "type" ), c->GetBool( "not" ) ? "(not) " : "",
				c->GetString( "name", c->GetString( "quest", c->GetString( "target", c->GetString( "item" ) ) ) ), v ? 1 : 0 );
		return v;
	}

	bool Check( KeyValues *pConds, const Ctx_t &ctx )
	{
		if ( !pConds )
			return true;
		bool bGroup = false, bPending = false;
		for ( KeyValues *c = pConds->GetFirstTrueSubKey(); c; c = c->GetNextTrueSubKey() )
		{
			bGroup = bGroup || CheckOne( c, ctx );
			bPending = true;
			if ( c->GetBool( "or" ) )
				continue;
			if ( !bGroup )
				return false;
			bGroup = bPending = false;
		}
		return !bPending || bGroup;
	}

	bool CheckPasses( KeyValues *pCheck, CBasePlayer *pPlayer, char *pszLabel, int nLabel )
	{
		const char *pszSkill = pCheck->GetString( "skill" );
		const char *pszStat = pCheck->GetString( "stat" );
		int need = pCheck->GetInt( "value", 0 );
		int have;
		const char *pszId;
		if ( pszStat[0] )
		{
			pszId = pszStat;
			have = PlayerStat( pszStat );
		}
		else
		{
			pszId = pszSkill;
			have = !Q_stricmp( pszSkill, "level" ) ? RPMState::PlayerLevel( pPlayer ) : RPMState::PlayerSkill( pPlayer, pszSkill );
		}
		bool bPass = have >= need;
		if ( pszLabel )
		{
			if ( bPass )
				Q_snprintf( pszLabel, nLabel, "[%s %d] ", SkillName( pszId ), need );
			else
				Q_snprintf( pszLabel, nLabel, "[%s %d/%d] ", SkillName( pszId ), have, need );
		}
		return bPass;
	}

	//-------------------------------------------------------------------------
	// owned blocks
	//-------------------------------------------------------------------------
	static CUtlVector<KeyValues *> s_Pools[POOL_COUNT];

	KeyValues *Keep( KeyValues *pBlock, int iPool )
	{
		if ( !pBlock || !pBlock->GetFirstSubKey() )
			return NULL;
		KeyValues *p = pBlock->MakeCopy();
		s_Pools[iPool].AddToTail( p );
		return p;
	}

	void FreePool( int iPool )
	{
		for ( int i = 0; i < s_Pools[iPool].Count(); i++ )
			s_Pools[iPool][i]->deleteThis();
		s_Pools[iPool].RemoveAll();
	}

	//-------------------------------------------------------------------------
	// actions
	//-------------------------------------------------------------------------
	struct Pending_t
	{
		KeyValues *pAction;
		CHandle<CBasePlayer> hPlayer;
		EHANDLE hSelf, hOther;
		CUtlString quest;
		float flTime;			// run at
		bool bAfterDialog;		// and not while the dialog window is open
	};
	static CUtlVector<Pending_t> s_Pending;
	static bool s_bDialogOpen = false;
	static float s_flDialogOpenSince = 0.0f;

	static bool NeedsClosedDialog( const char *type )
	{
		return !Q_stricmp( type, "hostile" ) || !Q_stricmp( type, "faction_hostile" ) || !Q_stricmp( type, "teleport" ) ||
			!Q_stricmp( type, "dialog" ) || !Q_stricmp( type, "trade" ) || !Q_stricmp( type, "kill" );
	}

	static void Queue( KeyValues *a, const Ctx_t &ctx, float flDelay, bool bAfterDialog )
	{
		Pending_t p;
		p.pAction = a->MakeCopy();
		p.pAction->SetFloat( "delay", 0 );
		p.hPlayer = ctx.pPlayer;
		p.hSelf = ctx.pSelf;
		p.hOther = ctx.pOther;
		p.quest = ctx.pszQuest ? ctx.pszQuest : "";
		p.flTime = gpGlobals->curtime + flDelay;
		p.bAfterDialog = bAfterDialog;
		s_Pending.AddToTail( p );
	}

	static void GiveItems( CBasePlayer *pPlayer, const char *pszItem, int n )
	{
		if ( !pPlayer || !pszItem[0] || n <= 0 )
			return;
		if ( RPMItems::IsStackable( pszItem ) || InventorySystem::IsAmmoItem( pszItem ) )
			InventorySystem::AddItemToPlayerWithBullets( pPlayer, pszItem, n );
		else
			for ( int i = 0; i < n; i++ )
				InventorySystem::AddItemToPlayer( pPlayer, pszItem );
		InventorySystem::NotifyPlayerReward( pPlayer, pszItem, n );
		InventorySystem::SendInventoryToPlayer( pPlayer );
	}

	static void TakeItems( CBasePlayer *pPlayer, const char *pszItem, int n )
	{
		if ( !pPlayer || !pszItem[0] || n <= 0 )
			return;
		int taken = InventorySystem::TakeItems( pPlayer, pszItem, n );
		if ( taken > 0 )
			Toast( pPlayer, "inventory", CFmtStr( IsRussian() ? "Отдано: %s x%d" : "Removed: %s x%d", RPMItems::Name( pszItem ), taken ), "" );
		InventorySystem::SendInventoryToPlayer( pPlayer );
	}

	static void HatePlayer( CBaseEntity *pEnt, CBasePlayer *pPlayer )
	{
		CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
		if ( pNPC && pNPC->IsAlive() )
			pNPC->AcceptInput( "HatePlayer", pPlayer, pPlayer, variant_t(), 0 );
	}

	// targets of door / field / turret: "!self"-like names - that one; else every entity whose targetname
	// or classname matches ("gate_1;npc_turret_*"), within "radius" of the source (the terminal) if given
	static void CollectTargets( KeyValues *a, const Ctx_t &ctx, const char *pszDefault, CUtlVector<CBaseEntity *> &out )
	{
		const char *pszTarget = a->GetString( "target" );
		if ( !pszTarget[0] )
			pszTarget = pszDefault;
		if ( !pszTarget || !pszTarget[0] )
			return;
		if ( pszTarget[0] == '!' )
		{
			CBaseEntity *p = FindTarget( pszTarget, ctx );
			if ( p )
				out.AddToTail( p );
			return;
		}
		float r = a->GetFloat( "radius", 0.0f );
		CBaseEntity *pSrc = ctx.pSelf ? ctx.pSelf : Player( ctx );
		for ( CBaseEntity *p = gEntList.FirstEnt(); p; p = gEntList.NextEnt( p ) )
		{
			if ( p->IsPlayer() || !NameMatches( pszTarget, p ) )
				continue;
			if ( r > 0.0f && pSrc && ( p->GetAbsOrigin() - pSrc->GetAbsOrigin() ).Length() > r )
				continue;
			out.AddToTail( p );
		}
	}

	static void Fire( CBaseEntity *pEnt, const char *pszInput, const char *pszValue, float flDelay, const Ctx_t &ctx )
	{
		variant_t v;
		v.SetString( AllocPooledString( pszValue ? pszValue : "" ) );
		CBasePlayer *pPlayer = Player( ctx );
		g_EventQueue.AddEvent( pEnt, pszInput, v, flDelay, pPlayer, ctx.pSelf ? ctx.pSelf : pPlayer );
	}

	// machines a terminal works: doors, force fields, turrets and cameras ("op")
	static void RunMachine( KeyValues *a, const char *type, const Ctx_t &ctx )
	{
		const char *op = a->GetString( "op" );
		CUtlVector<CBaseEntity *> ents;
		if ( !Q_stricmp( type, "door" ) )
		{
			CollectTargets( a, ctx, NULL, ents );
			for ( int i = 0; i < ents.Count(); i++ )
			{
				if ( !Q_stricmp( op, "close" ) )
					Fire( ents[i], "Close", "", 0.0f, ctx );
				else if ( !Q_stricmp( op, "lock" ) )
				{
					Fire( ents[i], "Close", "", 0.0f, ctx );
					Fire( ents[i], "Lock", "", 0.05f, ctx );
				}
				else if ( !Q_stricmp( op, "unlock" ) )
					Fire( ents[i], "Unlock", "", 0.0f, ctx );
				else if ( !Q_stricmp( op, "toggle" ) )
				{
					Fire( ents[i], "Unlock", "", 0.0f, ctx );
					Fire( ents[i], "Toggle", "", 0.05f, ctx );
				}
				else	// open (unlocks first, as the terminal of Fallout does)
				{
					Fire( ents[i], "Unlock", "", 0.0f, ctx );
					Fire( ents[i], "Open", "", 0.05f, ctx );
				}
			}
		}
		else if ( !Q_stricmp( type, "field" ) )
		{
			CollectTargets( a, ctx, NULL, ents );
			bool bOn = !Q_stricmp( op, "on" ), bToggle = !Q_stricmp( op, "toggle" );
			for ( int i = 0; i < ents.Count(); i++ )
			{
				CBaseEntity *p = ents[i];
				if ( !Q_strnicmp( p->GetClassname(), "prop_", 5 ) )
				{
					// the emitters / field models: visible and solid or not
					bool on = bToggle ? p->IsEffectActive( EF_NODRAW ) : bOn;
					Fire( p, on ? "TurnOn" : "TurnOff", "", 0.0f, ctx );
					Fire( p, on ? "EnableCollision" : "DisableCollision", "", 0.0f, ctx );
				}
				else
					Fire( p, bToggle ? "Toggle" : ( bOn ? "Enable" : "Disable" ), "", 0.0f, ctx );
			}
		}
		else	// turret: off / on / friendly / hostile; turrets and cameras near the terminal by default
		{
			CollectTargets( a, ctx, "npc_turret_floor;npc_turret_ceiling;npc_turret_ground;npc_combine_camera", ents );
			if ( !a->GetString( "target" )[0] && a->GetFloat( "radius", 0.0f ) <= 0.0f )
			{
				// no target and no radius: only the ones within 1500 units of the terminal
				CBaseEntity *pSrc = ctx.pSelf ? ctx.pSelf : Player( ctx );
				for ( int i = ents.Count() - 1; i >= 0; i-- )
					if ( pSrc && ( ents[i]->GetAbsOrigin() - pSrc->GetAbsOrigin() ).Length() > 1500.0f )
						ents.Remove( i );
			}
			static const char *s_pszCombine[] = { "npc_combine_s", "npc_metropolice", "npc_hunter", "npc_manhack", "npc_cscanner",
				"npc_clawscanner", "npc_strider", "npc_combinegunship", "npc_helicopter", "npc_stalker" };
			for ( int i = 0; i < ents.Count(); i++ )
			{
				CBaseEntity *p = ents[i];
				if ( !Q_stricmp( op, "on" ) )
					Fire( p, "Enable", "", 0.0f, ctx );
				else if ( !Q_stricmp( op, "friendly" ) || !Q_stricmp( op, "hostile" ) )
				{
					bool bFriendly = !Q_stricmp( op, "friendly" );
					Fire( p, "SetRelationship", bFriendly ? "!player D_LI 99" : "!player D_HT 99", 0.0f, ctx );
					for ( int k = 0; k < ARRAYSIZE( s_pszCombine ); k++ )
						Fire( p, "SetRelationship", CFmtStr( "%s %s 98", s_pszCombine[k], bFriendly ? "D_HT" : "D_LI" ), 0.0f, ctx );
					// and the Combine answer in kind
					if ( bFriendly )
						for ( CBaseEntity *q = gEntList.FirstEnt(); q; q = gEntList.NextEnt( q ) )
						{
							if ( !q->MyNPCPointer() )
								continue;
							for ( int k = 0; k < ARRAYSIZE( s_pszCombine ); k++ )
								if ( FClassnameIs( q, s_pszCombine[k] ) && p->GetEntityName() != NULL_STRING )
									Fire( q, "SetRelationship", CFmtStr( "%s D_HT 98", p->GetEntityName().ToCStr() ), 0.0f, ctx );
						}
					Fire( p, "Enable", "", 0.05f, ctx );
				}
				else
					Fire( p, "Disable", "", 0.0f, ctx );
			}
		}
		if ( rpm_logic_debug.GetBool() )
			Msg( "[logic] %s %s: %d targets\n", type, op, ents.Count() );
	}

	static void RunOne( KeyValues *a, const Ctx_t &ctx )
	{
		const char *type = a->GetString( "type" );
		CBasePlayer *pPlayer = Player( ctx );
		const char *pszQuest = a->GetString( "quest" );
		if ( !pszQuest[0] && ctx.pszQuest )
			pszQuest = ctx.pszQuest;
		if ( rpm_logic_debug.GetBool() )
			Msg( "[logic] action %s %s\n", type, a->GetString( "item", a->GetString( "name", a->GetString( "target", pszQuest ) ) ) );

		if ( !Q_stricmp( type, "give" ) )
			GiveItems( pPlayer, a->GetString( "item" ), a->GetInt( "count", 1 ) );
		else if ( !Q_stricmp( type, "take" ) )
			TakeItems( pPlayer, a->GetString( "item" ), a->GetInt( "count", 1 ) );
		else if ( !Q_stricmp( type, "money" ) )
		{
			int v = a->GetInt( "value" );
			if ( v > 0 )
				GiveItems( pPlayer, "rpm_credits", v );
			else
				TakeItems( pPlayer, "rpm_credits", -v );
		}
		else if ( !Q_stricmp( type, "xp" ) )
			AddXP( pPlayer, a->GetInt( "value" ) );
		else if ( !Q_stricmp( type, "flag" ) )
			RPMState::SetFlag( a->GetString( "name" ), a->GetInt( "value", 1 ) != 0 );
		else if ( !Q_stricmp( type, "var" ) )
		{
			const char *op = a->GetString( "op", "set" );
			int v = a->GetInt( "value" ), cur = RPMState::GetVar( a->GetString( "name" ) );
			if ( !Q_stricmp( op, "add" ) || !Q_strcmp( op, "+" ) )
				v = cur + v;
			else if ( !Q_stricmp( op, "sub" ) || !Q_strcmp( op, "-" ) )
				v = cur - v;
			RPMState::SetVar( a->GetString( "name" ), v );
		}
		else if ( !Q_stricmp( type, "quest_start" ) )
			QuestSystem().GrantQuest( pPlayer, pszQuest );
		else if ( !Q_stricmp( type, "quest_stage" ) )
			QuestSystem().SetQuestStage( pPlayer, pszQuest, a->GetInt( "value" ) );
		else if ( !Q_stricmp( type, "quest_complete" ) )
			QuestSystem().CompleteQuest( pPlayer, pszQuest );
		else if ( !Q_stricmp( type, "quest_fail" ) )
			QuestSystem().FailQuest( pPlayer, pszQuest, 0 );
		else if ( !Q_stricmp( type, "objective" ) )
			QuestSystem().CompleteObjective( pPlayer, pszQuest, a->GetString( "obj" ) );
		else if ( !Q_stricmp( type, "rep" ) )
		{
			if ( a->GetString( "faction" )[0] )
				RPMChar::AddFactionRep( pPlayer, a->GetString( "faction" ), a->GetInt( "value" ) );
			else
				QuestSystem().AddReputation( pPlayer, a->GetInt( "value" ) );
		}
		else if ( !Q_stricmp( type, "perk" ) )
		{
			KeyValues *perks = RPMState::Section( "player" )->FindKey( "perks", true );
			const char *id = a->GetString( "name" );
			if ( RPMRPG::Perk( id ) )
			{
				perks->SetInt( id, perks->GetInt( id, 0 ) + 1 );
				Toast( pPlayer, "perk", CFmtStr( IsRussian() ? "Новый перк: %s" : "New perk: %s", RPMRPG::Name( id ) ), "" );
			}
		}
		else if ( !Q_stricmp( type, "char_create" ) )
			RPMChar::OpenCreation( pPlayer );
		else if ( !Q_stricmp( type, "skill" ) || !Q_stricmp( type, "stat" ) )
		{
			bool bStat = !Q_stricmp( type, "stat" );
			KeyValues *k = RPMState::Section( "player" )->FindKey( bStat ? "stats" : "skills", true );
			const char *name = a->GetString( "name" );
			int v = a->GetInt( "value" );
			if ( !Q_stricmp( a->GetString( "op", "add" ), "add" ) )
				v += k->GetInt( name, bStat ? 5 : 0 );
			k->SetInt( name, v );
		}
		else if ( !Q_stricmp( type, "hostile" ) )
			HatePlayer( FindTarget( a->GetString( "target" ), ctx ), pPlayer );
		else if ( !Q_stricmp( type, "faction_hostile" ) )
		{
			const char *pszFaction = a->GetString( "faction" );
			if ( !pszFaction[0] && ctx.pSelf && ctx.pSelf->MyNPCPointer() )
				pszFaction = ctx.pSelf->MyNPCPointer()->GetFactionTag();
			CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
			for ( int i = 0; pszFaction[0] && i < g_AI_Manager.NumAIs(); i++ )
				if ( ppAIs[i] && !Q_stricmp( ppAIs[i]->GetFactionTag(), pszFaction ) )
					HatePlayer( ppAIs[i], pPlayer );
		}
		else if ( !Q_stricmp( type, "message" ) )
			Toast( pPlayer, "ui", Localized( a, "text" ), "" );
		else if ( !Q_stricmp( type, "sound" ) )
		{
			const char *s = a->GetString( "value" );
			if ( pPlayer && s[0] )
				engine->ClientCommand( pPlayer->edict(), "%s \"%s\"\n", Q_strstr( s, "." ) ? "play" : "playgamesound", s );
		}
		else if ( !Q_stricmp( type, "fire" ) )
		{
			variant_t v;
			v.SetString( AllocPooledString( a->GetString( "value" ) ) );
			const char *pszTarget = a->GetString( "target" );
			const char *pszInput = a->GetString( "input" );
			CBaseEntity *pSpecial = ( pszTarget[0] == '!' || !pszTarget[0] ) ? FindTarget( pszTarget, ctx ) : NULL;
			if ( pSpecial )
				g_EventQueue.AddEvent( pSpecial, pszInput, v, 0.0f, pPlayer, ctx.pSelf ? ctx.pSelf : pPlayer );
			else if ( pszTarget[0] )
				g_EventQueue.AddEvent( pszTarget, pszInput, v, 0.0f, pPlayer, ctx.pSelf ? ctx.pSelf : pPlayer );
		}
		else if ( !Q_stricmp( type, "door" ) || !Q_stricmp( type, "field" ) || !Q_stricmp( type, "turret" ) )
			RunMachine( a, type, ctx );
		else if ( !Q_stricmp( type, "teleport" ) )
		{
			const char *pszTarget = a->GetString( "target" );
			Vector pos;
			QAngle ang = pPlayer ? pPlayer->EyeAngles() : vec3_angle;
			int zone = RPMWorld::FindZone( pszTarget );
			CBaseEntity *pEnt = zone < 0 ? FindTarget( pszTarget, ctx ) : NULL;
			if ( zone >= 0 )
				pos = RPMWorld::ZoneCenter( zone ) - Vector( 0, 0, RPMWorld::ZoneSize( zone ).z * 0.5f );
			else if ( pEnt )
			{
				pos = pEnt->GetAbsOrigin();
				ang = QAngle( 0, pEnt->GetAbsAngles().y, 0 );
			}
			if ( pPlayer && ( zone >= 0 || pEnt ) )
			{
				pPlayer->Teleport( &pos, &ang, &vec3_origin );
				pPlayer->SnapEyeAngles( ang );
			}
		}
		else if ( !Q_stricmp( type, "dialog" ) )
		{
			CBaseEntity *pNPC = FindTarget( a->GetString( "target" ), ctx );
			if ( pPlayer && pNPC )
				DialogSystemServer::StartDialogForEntity( pPlayer, pNPC );
		}
		else if ( !Q_stricmp( type, "trade" ) )
			RPMTrade::Open( pPlayer, FindTarget( a->GetString( "target" ), ctx ) );
		else if ( !Q_stricmp( type, "kill" ) )
		{
			CBaseEntity *pEnt = FindTarget( a->GetString( "target" ), ctx );
			if ( pEnt && pEnt->IsAlive() && !pEnt->IsPlayer() )
			{
				CTakeDamageInfo info( GetWorldEntity(), GetWorldEntity(), pEnt->GetHealth() * 10.0f + 1000.0f, DMG_GENERIC );
				pEnt->TakeDamage( info );
			}
		}
		else if ( !Q_stricmp( type, "remove" ) )
		{
			CBaseEntity *pEnt = FindTarget( a->GetString( "target" ), ctx );
			if ( pEnt && !pEnt->IsPlayer() )
				UTIL_Remove( pEnt );
		}
		else if ( !Q_stricmp( type, "fade" ) )
		{
			// to black and back: value - seconds of each half, hold - seconds in black
			float t = Max( 0.05f, a->GetFloat( "value", 1.0f ) ), hold = a->GetFloat( "hold", 0.5f );
			color32 black = { 0, 0, 0, 255 };
			if ( pPlayer )
			{
				UTIL_ScreenFade( pPlayer, black, t, hold, FFADE_OUT | FFADE_PURGE );
				KeyValues *pIn = new KeyValues( "a" );
				pIn->SetString( "type", "fade_in" );
				pIn->SetFloat( "value", t );
				Queue( pIn, ctx, t + hold, false );
				pIn->deleteThis();
			}
		}
		else if ( !Q_stricmp( type, "fade_in" ) )
		{
			color32 black = { 0, 0, 0, 255 };
			if ( pPlayer )
				UTIL_ScreenFade( pPlayer, black, a->GetFloat( "value", 1.0f ), 0, FFADE_IN | FFADE_PURGE );
		}
		else if ( !Q_stricmp( type, "time" ) )
			engine->ServerCommand( CFmtStr( "sv_timecycle_set_time %.2f\n", a->GetFloat( "value" ) ) );
		else if ( !Q_stricmp( type, "weather" ) )
			engine->ServerCommand( CFmtStr( "sv_weather %s %d\n", a->GetString( "value", "clear" ), a->GetInt( "seconds", 20 ) ) );
		else if ( !Q_stricmp( type, "command" ) )
			engine->ServerCommand( CFmtStr( "%s\n", a->GetString( "value" ) ) );
		else if ( !Q_stricmp( type, "music" ) )
			RPMAudio::SetMusicOverride( a->GetString( "value" ) );
		else if ( !Q_stricmp( type, "stinger" ) )
			RPMAudio::Stinger( a->GetString( "value" ) );
		else if ( !Q_stricmp( type, "bark" ) )
			RPMAudio::Bark( FindTarget( a->GetString( "target" ), ctx ), a->GetString( "value", "custom" ), "" );
		else
			Warning( "RPMLogic: unknown action '%s'\n", type );
	}

	void Run( KeyValues *pActions, const Ctx_t &ctx )
	{
		if ( !pActions )
			return;
		for ( KeyValues *a = pActions->GetFirstTrueSubKey(); a; a = a->GetNextTrueSubKey() )
		{
			float flDelay = a->GetFloat( "delay", 0.0f );
			bool bAfter = s_bDialogOpen && NeedsClosedDialog( a->GetString( "type" ) );
			if ( flDelay > 0.0f || bAfter )
				Queue( a, ctx, flDelay, bAfter );
			else
				RunOne( a, ctx );
		}
	}

	void NoteDialogOpen( CBasePlayer *pPlayer )
	{
		if ( !s_bDialogOpen )
			s_flDialogOpenSince = gpGlobals->curtime;
		s_bDialogOpen = true;
	}

	bool IsDialogOpen()
	{
		return s_bDialogOpen;
	}

	static void RunPending( bool bAll )
	{
		// a copy: actions may queue new ones
		for ( int i = 0; i < s_Pending.Count(); )
		{
			Pending_t &p = s_Pending[i];
			if ( !bAll && ( p.flTime > gpGlobals->curtime || ( p.bAfterDialog && s_bDialogOpen ) ) )
			{
				i++;
				continue;
			}
			Pending_t run = p;
			s_Pending.Remove( i );
			Ctx_t ctx;
			ctx.pPlayer = run.hPlayer.Get();
			ctx.pSelf = run.hSelf.Get();
			ctx.pOther = run.hOther.Get();
			ctx.pszQuest = run.quest.IsEmpty() ? NULL : run.quest.Get();
			RunOne( run.pAction, ctx );
			run.pAction->deleteThis();
		}
	}

	static EHANDLE s_hDialogNPC;

	void DialogClosed( CBasePlayer *pPlayer )
	{
		bool bWasOpen = s_bDialogOpen;
		s_bDialogOpen = false;
		RunPending( false );
		if ( bWasOpen && s_hDialogNPC.Get() )
			RPMAudio::NoteDialogEnd( s_hDialogNPC.Get() );		// "farewell" barks
		s_hDialogNPC = NULL;
	}

	//-------------------------------------------------------------------------
	// events: scripts/rpg/events.txt
	//-------------------------------------------------------------------------
	static KeyValues *s_pEvents = NULL;

	void ReloadEvents()
	{
		if ( s_pEvents )
			s_pEvents->deleteThis();
		s_pEvents = new KeyValues( "Events" );
		if ( !s_pEvents->LoadFromFile( filesystem, "scripts/rpg/events.txt", "MOD" ) )
			s_pEvents->Clear();
	}

	static void Fire( const char *pszOn, CBaseEntity *pTargetEnt, const char *pszTarget, int iValue, const Ctx_t &ctx )
	{
		if ( !s_pEvents )
			return;
		KeyValues *pFired = RPMState::Section( "events" );
		for ( KeyValues *e = s_pEvents->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
		{
			if ( Q_stricmp( e->GetString( "on" ), pszOn ) )
				continue;
			const char *pszMap = e->GetString( "map" );
			if ( pszMap[0] && !AnyPatternMatches( pszMap, STRING( gpGlobals->mapname ) ) )
				continue;
			const char *pszWant = e->GetString( "target" );
			if ( pszWant[0] )
			{
				if ( pTargetEnt ? !NameMatches( pszWant, pTargetEnt ) : !AnyPatternMatches( pszWant, pszTarget ) )
					continue;
			}
			const char *pszValue = e->GetString( "value" );
			if ( pszValue[0] && iValue != INT_MIN && atoi( pszValue ) != iValue )
				continue;
			const char *id = e->GetName();
			if ( e->GetBool( "once" ) && pFired->GetInt( id, 0 ) > 0 )
				continue;
			if ( !Check( e->FindKey( "conditions" ), ctx ) )
				continue;
			pFired->SetInt( id, pFired->GetInt( id, 0 ) + 1 );
			if ( rpm_logic_debug.GetBool() )
				Msg( "[logic] event %s (%s %s)\n", id, pszOn, pszTarget ? pszTarget : "" );
			KeyValues *pActions = e->FindKey( "actions" );
			float flDelay = e->GetFloat( "delay", 0.0f );
			if ( flDelay > 0.0f && pActions )
			{
				for ( KeyValues *a = pActions->GetFirstTrueSubKey(); a; a = a->GetNextTrueSubKey() )
					Queue( a, ctx, flDelay + a->GetFloat( "delay", 0.0f ), false );
			}
			else
				Run( pActions, ctx );
		}
	}

	void NoteKill( CBaseEntity *pVictim, CBaseEntity *pAttacker )
	{
		if ( !pVictim )
			return;
		Ctx_t ctx;
		ctx.pPlayer = ToBasePlayer( pAttacker );
		ctx.pSelf = pVictim;
		ctx.pOther = pAttacker;
		Fire( "kill", pVictim, pVictim->GetEntityName().ToCStr(), INT_MIN, ctx );
		RPMAudio::NoteDeath( pVictim, pAttacker );
		// experience for an NPC of the Creation Kit killed by the player
		if ( ctx.pPlayer && pVictim->MyNPCPointer() )
		{
			const RPMNPCDef_t *pDef = RPMWorld::FindNPCDef( pVictim->GetEntityName().ToCStr() );
			if ( pDef && pDef->xp > 0 )
				AddXP( ctx.pPlayer, pDef->xp );
		}
		RPMChar::NoteKill( pVictim, pAttacker );		// experience for other enemies, reputation
	}

	void NoteUse( CBasePlayer *pPlayer, CBaseEntity *pUsed )
	{
		if ( !pPlayer || !pUsed )
			return;
		Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pSelf = pUsed;
		Fire( "use", pUsed, pUsed->GetEntityName().ToCStr(), INT_MIN, ctx );
		QuestSystem().OnUse( pPlayer, pUsed );
	}

	void NoteTalk( CBasePlayer *pPlayer, CBaseEntity *pNPC )
	{
		if ( !pPlayer || !pNPC )
			return;
		Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pSelf = pNPC;
		s_hDialogNPC = pNPC;
		Fire( "talk", pNPC, pNPC->GetEntityName().ToCStr(), INT_MIN, ctx );
		QuestSystem().OnTalk( pPlayer, pNPC );
	}

	void NoteQuest( CBasePlayer *pPlayer, const char *pszQuest, const char *pszWhat, int iStage )
	{
		Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pszQuest = pszQuest;
		Fire( pszWhat, NULL, pszQuest, iStage, ctx );
		if ( !Q_stricmp( pszWhat, "quest_complete" ) || !Q_stricmp( pszWhat, "quest_fail" ) || !Q_stricmp( pszWhat, "quest_start" ) )
			RPMAudio::StingerEvent( pszWhat );
	}
}

using namespace RPMLogic;

//-----------------------------------------------------------------------------
// The tick: delayed actions, zones, hours, "when" events, quest objectives,
// globals of a new game, the test state
//-----------------------------------------------------------------------------
class CRPMLogicSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMLogicSystem() : CAutoGameSystemPerFrame( "CRPMLogicSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		ReloadEvents();
		m_bNewGame = ( gpGlobals->eLoadType == MapLoad_NewGame );
		m_bMapEnter = ( gpGlobals->eLoadType != MapLoad_LoadGame && gpGlobals->eLoadType != MapLoad_Background );
		m_bZonesKnown = false;
		m_flNextTick = 0;
		m_flLastHours = -1;
		s_bDialogOpen = false;
	}

	virtual void LevelInitPostEntity()
	{
		if ( m_bNewGame )
			ApplyGlobals();
	}

	virtual void LevelShutdownPostEntity()
	{
		for ( int i = 0; i < s_Pending.Count(); i++ )
			s_Pending[i].pAction->deleteThis();
		s_Pending.RemoveAll();
		m_ZoneInside.RemoveAll();
		s_bDialogOpen = false;
	}

	void ApplyGlobals()
	{
		// default values of the world variables / flags (Creation Kit: "Logic" > flags and variables)
		KeyValues *pKV = new KeyValues( "Globals" );
		if ( pKV->LoadFromFile( filesystem, "scripts/rpg/globals.txt", "MOD" ) )
		{
			KeyValues *pFlags = pKV->FindKey( "flags" );
			for ( KeyValues *f = pFlags ? pFlags->GetFirstValue() : NULL; f; f = f->GetNextValue() )
				if ( f->GetInt() )
					RPMState::SetFlag( f->GetName(), true );
			KeyValues *pVars = pKV->FindKey( "vars" );
			for ( KeyValues *v = pVars ? pVars->GetFirstValue() : NULL; v; v = v->GetNextValue() )
				RPMState::SetVar( v->GetName(), v->GetInt() );
		}
		pKV->deleteThis();
	}

	void ApplyTestState( CBasePlayer *pPlayer )
	{
		KeyValues *pKV = new KeyValues( "TestState" );
		if ( !pKV->LoadFromFile( filesystem, rpm_test_state.GetString(), "MOD" ) )
		{
			Warning( "rpm_test_state: can't read %s\n", rpm_test_state.GetString() );
			pKV->deleteThis();
			return;
		}
		KeyValues *k;
		if ( ( k = pKV->FindKey( "quests" ) ) != NULL )
		{
			for ( KeyValues *q = k->GetFirstTrueSubKey(); q; q = q->GetNextTrueSubKey() )
				QuestSystem().DebugSetQuest( pPlayer, q->GetName(), (QuestState)QuestStateFromString( q->GetString( "state", "active" ) ), q->GetInt( "stage", 0 ) );
		}
		for ( KeyValues *f = ( k = pKV->FindKey( "flags" ) ) ? k->GetFirstValue() : NULL; f; f = f->GetNextValue() )
			RPMState::SetFlag( f->GetName(), f->GetInt() != 0 );
		for ( KeyValues *v = ( k = pKV->FindKey( "vars" ) ) ? k->GetFirstValue() : NULL; v; v = v->GetNextValue() )
			RPMState::SetVar( v->GetName(), v->GetInt() );
		// attributes first: skills grow from them; a skill of the test state is its total
		for ( KeyValues *s = ( k = pKV->FindKey( "stats" ) ) ? k->GetFirstValue() : NULL; s; s = s->GetNextValue() )
			RPMState::Section( "player" )->FindKey( "stats", true )->SetInt( s->GetName(), s->GetInt() );
		for ( KeyValues *s = ( k = pKV->FindKey( "skills" ) ) ? k->GetFirstValue() : NULL; s; s = s->GetNextValue() )
			RPMChar::SetSkillTotal( s->GetName(), s->GetInt() );
		if ( pKV->FindKey( "level" ) )
		{
			RPMState::Section( "player" )->SetInt( "level", pKV->GetInt( "level", 1 ) );
			RPMState::Section( "player" )->SetInt( "xp", RPMRPG::XPForLevel( pKV->GetInt( "level", 1 ) ) );
		}
		if ( pKV->GetInt( "created", 1 ) )
			RPMState::Section( "player" )->SetInt( "created", 1 );	// the test starts without the creation window
		if ( pKV->FindKey( "rep" ) )
			QuestSystem().AddReputation( pPlayer, pKV->GetInt( "rep" ) - QuestSystem().GetReputation( pPlayer ) );
		for ( KeyValues *it = ( k = pKV->FindKey( "items" ) ) ? k->GetFirstValue() : NULL; it; it = it->GetNextValue() )
		{
			int n = it->GetInt();
			if ( RPMItems::IsStackable( it->GetName() ) || InventorySystem::IsAmmoItem( it->GetName() ) )
				InventorySystem::AddItemToPlayerWithBullets( pPlayer, it->GetName(), n );
			else
				for ( int i = 0; i < n; i++ )
					InventorySystem::AddItemToPlayer( pPlayer, it->GetName() );
		}
		InventorySystem::SendInventoryToPlayer( pPlayer );
		if ( pKV->GetString( "hour" )[0] )
			engine->ServerCommand( CFmtStr( "sv_timecycle_set_time %.2f\n", pKV->GetFloat( "hour" ) ) );
		if ( pKV->GetString( "weather" )[0] )
			engine->ServerCommand( CFmtStr( "sv_weather %s 0\n", pKV->GetString( "weather" ) ) );
		QuestSystem().SendQuestLogToPlayer( pPlayer );
		Msg( "rpm_test_state: applied %s\n", rpm_test_state.GetString() );
		pKV->deleteThis();
	}

	void UpdateZones( CBasePlayer *pPlayer )
	{
		int n = RPMWorld::ZoneCount();
		if ( m_ZoneInside.Count() != n )
		{
			m_ZoneInside.SetCount( n );
			m_bZonesKnown = false;
		}
		Vector pos = pPlayer->GetAbsOrigin();
		for ( int i = 0; i < n; i++ )
		{
			bool bIn = RPMWorld::IsInZone( i, pos );
			// the first look after a load only learns where the player is
			if ( m_bZonesKnown && bIn != m_ZoneInside[i] )
			{
				Ctx_t ctx;
				ctx.pPlayer = pPlayer;
				Fire( bIn ? "zone_enter" : "zone_leave", NULL, RPMWorld::ZoneName( i ), INT_MIN, ctx );
			}
			if ( bIn && ( !m_bZonesKnown || !m_ZoneInside[i] ) )
				RPMAudio::ZoneEnter( i );		// locations: discovered on the first visit (also arriving inside)
			m_ZoneInside[i] = bIn;
		}
		m_bZonesKnown = true;
	}

	void UpdateWhen( CBasePlayer *pPlayer )
	{
		if ( !s_pEvents )
			return;
		KeyValues *pWas = RPMState::Section( "when" );
		Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		for ( KeyValues *e = s_pEvents->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
		{
			if ( Q_stricmp( e->GetString( "on" ), "when" ) )
				continue;
			const char *pszMap = e->GetString( "map" );
			if ( pszMap[0] && !AnyPatternMatches( pszMap, STRING( gpGlobals->mapname ) ) )
				continue;
			bool bNow = Check( e->FindKey( "conditions" ), ctx );
			bool bWas = pWas->GetInt( e->GetName(), 0 ) != 0;
			pWas->SetInt( e->GetName(), bNow ? 1 : 0 );
			if ( !bNow || bWas )
				continue;
			const char *id = e->GetName();
			KeyValues *pFired = RPMState::Section( "events" );
			if ( e->GetBool( "once" ) && pFired->GetInt( id, 0 ) > 0 )
				continue;
			pFired->SetInt( id, pFired->GetInt( id, 0 ) + 1 );
			if ( rpm_logic_debug.GetBool() )
				Msg( "[logic] event %s (when)\n", id );
			KeyValues *pActions = e->FindKey( "actions" );
			float flDelay = e->GetFloat( "delay", 0.0f );
			for ( KeyValues *a = pActions ? pActions->GetFirstTrueSubKey() : NULL; a && flDelay > 0.0f; a = a->GetNextTrueSubKey() )
				Queue( a, ctx, flDelay + a->GetFloat( "delay", 0.0f ), false );
			if ( flDelay <= 0.0f )
				Run( pActions, ctx );
		}
	}

	void UpdateHours( CBasePlayer *pPlayer )
	{
		float h = RPMWorld::GetHours();
		if ( m_flLastHours >= 0 && h != m_flLastHours )
		{
			// whole hours passed since the last tick (over midnight too; a jump of the clock by a command fires nothing)
			float from = m_flLastHours, to = h < from ? h + 24.0f : h;
			if ( to - from < 2.0f )
			{
				for ( int hour = (int)floorf( from ) + 1; hour <= (int)floorf( to ); hour++ )
				{
					Ctx_t ctx;
					ctx.pPlayer = pPlayer;
					Fire( "hour", NULL, "", hour % 24, ctx );
				}
			}
		}
		m_flLastHours = h;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		RunPending( false );
		// a dialog window the client never reported closed (level change, crash of the panel)
		if ( s_bDialogOpen && gpGlobals->curtime - s_flDialogOpenSince > 600.0f )
			DialogClosed( NULL );
		if ( gpGlobals->curtime < m_flNextTick )
			return;
		m_flNextTick = gpGlobals->curtime + 0.25f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer || !pPlayer->IsAlive() )
			return;
		if ( rpm_test_state.GetString()[0] && gpGlobals->curtime > 1.0f )
		{
			ApplyTestState( pPlayer );
			rpm_test_state.SetValue( "" );
		}
		if ( m_bMapEnter )
		{
			m_bMapEnter = false;
			Ctx_t ctx;
			ctx.pPlayer = pPlayer;
			Fire( "map_enter", NULL, STRING( gpGlobals->mapname ), INT_MIN, ctx );
		}
		UpdateZones( pPlayer );
		UpdateHours( pPlayer );
		UpdateWhen( pPlayer );
		QuestSystem().Think( pPlayer );
	}

	bool m_bNewGame = false;
	bool m_bMapEnter = false;
	bool m_bZonesKnown = false;
	float m_flNextTick = 0;
	float m_flLastHours = -1;
	CUtlVector<bool> m_ZoneInside;
};

static CRPMLogicSystem g_RPMLogicSystem;

//-----------------------------------------------------------------------------
// Console
//-----------------------------------------------------------------------------
CON_COMMAND( dialog_closed, "HL2RPM: the client closed its dialog window (runs the actions that waited for it)" )
{
	RPMLogic::DialogClosed( UTIL_GetCommandClient() );
}

CON_COMMAND_F( rpm_skill, "HL2RPM: rpm_skill <id> [value] - show or set a skill (speech, lockpick, ...) or an attribute (str, cha, ...)", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
		return;
	static const char *s_pszStats[] = { "str", "per", "end", "cha", "int", "agi", "lck" };
	bool bStat = false;
	for ( int i = 0; i < ARRAYSIZE( s_pszStats ); i++ )
		bStat = bStat || !Q_stricmp( args[1], s_pszStats[i] );
	KeyValues *k = RPMState::Section( "player" )->FindKey( "stats", true );
	if ( args.ArgC() > 2 )
	{
		if ( bStat )
			k->SetInt( args[1], atoi( args[2] ) );
		else
			RPMChar::SetSkillTotal( args[1], atoi( args[2] ) );
	}
	Msg( "%s %s = %d\n", bStat ? "stat" : "skill", args[1], bStat ? RPMChar::Stat( args[1] ) : RPMChar::Skill( args[1] ) );
}

CON_COMMAND( rpm_events_dump, "HL2RPM: the events of scripts/rpg/events.txt and how many times each fired" )
{
	if ( !s_pEvents )
		ReloadEvents();
	KeyValues *pFired = RPMState::Section( "events" );
	for ( KeyValues *e = s_pEvents->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
		Msg( "  %-24s on %-14s target '%s' value '%s'%s - fired %d\n", e->GetName(), e->GetString( "on" ), e->GetString( "target" ),
			e->GetString( "value" ), e->GetBool( "once" ) ? " once" : "", pFired->GetInt( e->GetName(), 0 ) );
	Msg( "%d zone(s) on this map\n", RPMWorld::ZoneCount() );
}

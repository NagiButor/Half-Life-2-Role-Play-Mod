//========= HL2RPM ============//
//
// RPG-система (сервер): консольные команды (RPG_SPEC §1 «Команды») и команды интерфейса.
//   тест:       rpg_dump, rpg_set, rpg_xp, rpg_flag, rpg_unflag, rpg_rep, rpg_chargen, rpg_levelup,
//               rpg_reload, rpg_effects, rpg_test, rpg_check
//   интерфейс:  rpg_request_state, rpg_chargen_commit, rpg_skill_add, rpg_perk_pick, rpg_sleep,
//               rpg_barter_buy, rpg_barter_sell, rpg_travel
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_entities.h"
#include "hl2rpm/rpg_formulas.h"
#include "player.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CBasePlayer *CommandPlayer()
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	return pPlayer ? pPlayer : RPG_GetPlayer();
}

// ---------------------------------------------------------------------------
// Тест
// ---------------------------------------------------------------------------
CON_COMMAND_F( rpg_dump, "RPG: print the character (attributes, skills, factions, flags...)", FCVAR_GAMEDLL )
{
	CRPGCharacter *pChar = RPG_GetCharacter();
	pChar->Data().Print( "[rpg]" );
	CBasePlayer *pPlayer = CommandPlayer();
	if ( pPlayer )
		Msg( "[rpg] player HP %d/%d  suit type %d (%s)\n", pPlayer->GetHealth(), pPlayer->GetMaxHealth(),
			pPlayer->m_Local.m_iSuitType, ( pPlayer->m_Local.m_iSuitType >= 0 && pPlayer->m_Local.m_iSuitType < RPG_SUIT_COUNT ) ? g_pszRPGSuitTypeKeys[pPlayer->m_Local.m_iSuitType] : "?" );
}

CON_COMMAND_F( rpg_set, "RPG cheat: rpg_set <field> <value>. Fields: name, level, xp, skillpoints, perkpicks, credits, civil, "
	"suspicion, humanity, resonance, influence, chargen, act, LOV/ANA/MIG/BDI/DUH/AVT (or agility...), <skill> (total value), "
	"tag:<skill> 0/1, <faction>, PK-xx <rank>, TR-xx 0/1, disposition:<npc>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() < 3 )
	{
		Msg( "usage: rpg_set <field> <value>\n" );
		return;
	}
	CRPGCharacter *pChar = RPG_GetCharacter();
	const CRPGData &data = RPGData();
	const char *pszField = args[1];
	const char *pszValue = args[2];
	const int iValue = atoi( pszValue );

	if ( !Q_stricmp( pszField, "name" ) ) pChar->SetName( pszValue );
	else if ( !Q_stricmp( pszField, "level" ) ) pChar->SetLevel( iValue );
	else if ( !Q_stricmp( pszField, "xp" ) )
	{
		int cur = pChar->Data().m_iXP;
		if ( iValue > cur )
			pChar->AddXP( iValue - cur, "cheat" );
	}
	else if ( !Q_stricmp( pszField, "skillpoints" ) ) pChar->SetSkillPoints( iValue );
	else if ( !Q_stricmp( pszField, "perkpicks" ) ) pChar->SetPerkPicks( iValue );
	else if ( !Q_stricmp( pszField, "credits" ) ) pChar->SetCredits( iValue );
	else if ( !Q_stricmp( pszField, "civil" ) ) pChar->SetCivil( iValue );
	else if ( !Q_stricmp( pszField, "suspicion" ) ) pChar->SetSuspicion( (float)atof( pszValue ) );
	else if ( !Q_stricmp( pszField, "humanity" ) ) pChar->SetHumanity( iValue );
	else if ( !Q_stricmp( pszField, "resonance" ) ) pChar->SetResonance( iValue );
	else if ( !Q_stricmp( pszField, "influence" ) ) pChar->SetInfluence( iValue );
	else if ( !Q_stricmp( pszField, "chargen" ) ) pChar->SetChargenDone( iValue != 0 );
	else if ( !Q_stricmp( pszField, "act" ) ) pChar->SetAct( iValue );
	else if ( data.FindAttr( pszField ) >= 0 ) pChar->SetAttr( data.FindAttr( pszField ), iValue );
	else if ( data.FindSkill( pszField ) >= 0 )
	{
		// итоговое значение навыка: вложенные очки подгоняются
		int s = data.FindSkill( pszField );
		const CRPGCharacterData &d = pChar->Data();
		int without = d.GetSkill( s ) - d.GetSkillInvested( s );
		pChar->SetSkillInvested( s, Max( 0, iValue - without ) );
	}
	else if ( !Q_strnicmp( pszField, "tag:", 4 ) && data.FindSkill( pszField + 4 ) >= 0 ) pChar->SetSkillTagged( data.FindSkill( pszField + 4 ), iValue != 0 );
	else if ( !Q_strnicmp( pszField, "disposition:", 12 ) ) pChar->SetDisposition( pszField + 12, iValue );
	else if ( data.FindFaction( pszField ) ) pChar->SetRep( data.FindFaction( pszField )->id.Get(), iValue );
	else if ( data.FindPerk( pszField ) ) pChar->SetPerkRank( data.FindPerk( pszField )->id.Get(), iValue );
	else if ( data.FindTrait( pszField ) ) pChar->SetTrait( pszField, iValue != 0 );
	else
	{
		Msg( "rpg_set: unknown field '%s'\n", pszField );
		return;
	}
	Msg( "rpg_set %s %s: ok\n", pszField, pszValue );
}

CON_COMMAND_F( rpg_xp, "RPG cheat: add experience: rpg_xp <n>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() > 1 )
		RPG_GetCharacter()->AddXP( atoi( args[1] ), "cheat" );
}

CON_COMMAND_F( rpg_flag, "RPG cheat: set a flag: rpg_flag <name> | <name=value> (no value: print the flag)", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
		return;
	CUtlString arg = args.ArgS();
	const char *pEq = Q_strstr( arg.Get(), "=" );
	if ( !pEq )
	{
		RPG_GetCharacter()->SetFlag( args[1], 1 );
		Msg( "[rpg] flag %s = 1\n", args[1] );
		return;
	}
	RPGEffectList_t list;
	list.AddKey( "set_flag", arg.Get() );
	list.Apply( CommandPlayer() );
	CUtlString name;
	name.SetDirect( arg.Get(), (int)( pEq - arg.Get() ) );
	Msg( "[rpg] flag %s = \"%s\"\n", name.Get(), RPG_GetCharacter()->Data().GetFlagString( name.Get() ) );
}

CON_COMMAND_F( rpg_unflag, "RPG cheat: clear a flag: rpg_unflag <name>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() > 1 )
		RPG_GetCharacter()->ClearFlag( args[1] );
}

CON_COMMAND_F( rpg_rep, "RPG cheat: change faction reputation: rpg_rep <faction> <delta>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() > 2 )
		RPG_GetCharacter()->AddRep( args[1], atoi( args[2] ) );
}

CON_COMMAND_F( rpg_chargen, "RPG cheat: open character creation", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	RPG_OpenUI( CommandPlayer(), "chargen" );
}

CON_COMMAND_F( rpg_levelup, "RPG: open the level-up screen (skill points / perk picks)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	RPG_OpenUI( CommandPlayer(), "levelup" );
}

CON_COMMAND_F( rpg_reload, "RPG cheat: reload scripts/rpg/*.txt", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	RPGData().Load();
	RPG_GetCharacter()->QueueFullSync();
}

CON_COMMAND_F( rpg_effects, "RPG cheat: apply effects: rpg_effects \"xp=25, rep=lambda:+10, set_flag=x\"", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
		return;
	CBasePlayer *pPlayer = CommandPlayer();
	CBaseEntity *pNPC = NULL;
	// собеседник для disposition/start_combat - NPC под прицелом
	if ( pPlayer )
	{
		trace_t tr;
		Vector vecFwd;
		pPlayer->EyeVectors( &vecFwd );
		UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyePosition() + vecFwd * 512.0f, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );
		if ( tr.m_pEnt && tr.m_pEnt->MyNPCPointer() )
			pNPC = tr.m_pEnt;
	}
	RPG_ApplyEffects( args.ArgS(), pPlayer, pNPC, pPlayer );
}

CON_COMMAND_F( rpg_test, "RPG: test a condition: rpg_test \"mq00_done, ANA>=5 | speech>=40\"", FCVAR_GAMEDLL )
{
	if ( args.ArgC() < 2 )
		return;
	CUtlString reason;
	bool bMet = RPG_TestCondition( args.ArgS(), CommandPlayer(), NULL, &reason );
	Msg( "[rpg] test \"%s\": %s%s%s\n", args.ArgS(), bMet ? "TRUE" : "FALSE", reason.Length() ? " - " : "", reason.Get() );
}

CON_COMMAND_F( rpg_check, "RPG: evaluate a dialog check without applying it: rpg_check skill:speech:50", FCVAR_GAMEDLL )
{
	if ( args.ArgC() < 2 )
		return;
	RPGCheck_t check;
	CUtlString err;
	if ( !check.Parse( args.ArgS(), &err ) )
	{
		Msg( "[rpg] check: %s\n", err.Get() );
		return;
	}
	RPGCheckResult_t res = RPG_EvaluateCheck( check, RPG_MakeEvalContext( CommandPlayer() ), false );
	CUtlString desc;
	check.Describe( desc );
	Msg( "[rpg] check %s: %s (value %d, threshold %d, xp %d, cost %d)\n", desc.Get(), res.pass ? "PASS" : "FAIL", res.value, res.threshold, res.xp, res.cost );
}

// ---------------------------------------------------------------------------
// Интерфейс (клиент -> сервер; всё проверяется здесь)
// ---------------------------------------------------------------------------
CON_COMMAND_F( rpg_request_state, "RPG: client asks for the full character state", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	RPG_GetCharacter()->QueueFullSync();
}

// rpg_chargen_commit "<имя>" LOV ANA MIG BDI DUH AVT <тег1> <тег2> <тег3> [черта1] [черта2]
CON_COMMAND_F( rpg_chargen_commit, "RPG UI: create the character: rpg_chargen_commit \"<name>\" LOV ANA MIG BDI DUH AVT <tag1> <tag2> <tag3> [TR-xx] [TR-xx]",
	FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() < 11 )
	{
		Msg( "usage: rpg_chargen_commit \"<name>\" LOV ANA MIG BDI DUH AVT <tag1> <tag2> <tag3> [trait] [trait]\n" );
		return;
	}
	int attrs[RPG_ATTR_COUNT];
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
		attrs[i] = atoi( args[2 + i] );
	const char *tags[RPG_TAG_COUNT] = { args[8], args[9], args[10] };
	const char *traits[RPG_MAX_TRAITS] = { NULL, NULL };
	int nTraits = 0;
	for ( int i = 11; i < args.ArgC() && nTraits < RPG_MAX_TRAITS; ++i )
	{
		if ( args[i][0] )
			traits[nTraits++] = args[i];
	}
	static ConVarRef sv_cheats( "sv_cheats" );
	CUtlString err;
	CRPGCharacter *pChar = RPG_GetCharacter();
	if ( pChar->CommitChargen( args[1], attrs, tags, RPG_TAG_COUNT, traits, nTraits, sv_cheats.GetBool(), &err ) )
		Msg( "[rpg] character created\n" );
	else
	{
		Msg( "[rpg] chargen rejected: %s\n", err.Get() );
		pChar->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_Chargen", 0, err.Get() );
	}
}

CON_COMMAND_F( rpg_skill_add, "RPG UI: spend skill points: rpg_skill_add <skill> <points>", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() < 3 )
		return;
	CUtlString err;
	CRPGCharacter *pChar = RPG_GetCharacter();
	if ( !pChar->InvestSkill( RPGData().FindSkill( args[1] ), atoi( args[2] ), &err ) )
	{
		Msg( "[rpg] rpg_skill_add %s %s rejected: %s\n", args[1], args[2], err.Get() );
		pChar->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_Skill", 0, err.Get() );
	}
}

CON_COMMAND_F( rpg_perk_pick, "RPG UI: take a perk: rpg_perk_pick <PK-xx>", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() < 2 )
		return;
	CUtlString err;
	CRPGCharacter *pChar = RPG_GetCharacter();
	if ( !pChar->PickPerk( args[1], &err ) )
	{
		Msg( "[rpg] rpg_perk_pick %s rejected: %s\n", args[1], err.Get() );
		pChar->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_Perk", 0, err.Get() );
	}
}

CON_COMMAND_F( rpg_sleep, "RPG UI: sleep N hours in the bed just used (prop_rpg_usable use_action sleep): rpg_sleep <hours>", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() < 2 )
		return;
	CUtlString err;
	if ( !RPG_Sleep( CommandPlayer(), (float)atof( args[1] ), &err ) )
		Msg( "[rpg] rpg_sleep rejected: %s\n", err.Get() );
}

// ---------------------------------------------------------------------------
// Торговля (экран «barter» клиента): rpg_barter_buy / rpg_barter_sell <classname> [количество]
// ---------------------------------------------------------------------------
static void BarterCommand( const CCommand &args, bool bBuy )
{
	if ( args.ArgC() < 2 )
		return;
	CBasePlayer *pPlayer = CommandPlayer();
	const int count = args.ArgC() > 2 ? clamp( atoi( args[2] ), 1, 100 ) : 1;
	CUtlString err;
	const bool bOk = bBuy ? RPG_BarterBuy( pPlayer, args[1], count, &err ) : RPG_BarterSell( pPlayer, args[1], count, &err );
	if ( !bOk && pPlayer )
		RPG_GetCharacter( pPlayer )->Notify( RPG_NOTIFY_FAIL, err.IsEmpty() ? "#HL2RPM_UI_Fail_Condition" : err.Get(), 0, args[1] );
}

CON_COMMAND_F( rpg_barter_buy, "RPG UI: buy from the trader whose barter screen is open: rpg_barter_buy <classname> [count]", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	BarterCommand( args, true );
}

CON_COMMAND_F( rpg_barter_sell, "RPG UI: sell to the trader whose barter screen is open: rpg_barter_sell <classname> [count]", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	BarterCommand( args, false );
}

// ---------------------------------------------------------------------------
// Быстрое перемещение - трамвай (RPG_SPEC §11.2): rpg_travel <локация>. На карте с остановкой -
// info_landmark lm_travel (переход changelevel2) и info_target travel_arrive (где игрок выходит)
// ---------------------------------------------------------------------------
static ConVar rpg_travel_hours( "rpg_travel_hours", "0.5", FCVAR_GAMEDLL, "RPG: game hours a tram ride takes" );
static bool s_bTravelArrive = false;
static float s_flTravelArriveUntil = 0.0f;

CON_COMMAND_F( rpg_travel, "RPG UI: ride the tram to a discovered stop: rpg_travel <location id>", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() < 2 )
		return;
	CBasePlayer *pPlayer = CommandPlayer();
	if ( !pPlayer )
		return;
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	const RPGLocationDef_t *pLoc = RPGData().FindLocation( args[1] );
	const char *pszFail = NULL;
	if ( !pLoc || !pLoc->travel || pLoc->map.IsEmpty() || !pChar->Data().HasLocation( pLoc->id.Get() ) )
		pszFail = "#HL2RPM_UI_Fail_TravelUnknown";
	else if ( !Q_stricmp( pLoc->map.Get(), STRING( gpGlobals->mapname ) ) )
		pszFail = "#HL2RPM_UI_Fail_TravelHere";
	else if ( pChar->Data().m_flSuspicion >= 50.0f || pChar->Data().m_bAlarm )
		pszFail = "#HL2RPM_UI_Fail_TravelSuspicion";
	else if ( !gEntList.FindEntityByName( NULL, "lm_travel" ) )
		pszFail = "#HL2RPM_UI_Fail_TravelNoStop";
	if ( pszFail )
	{
		pChar->Notify( RPG_NOTIFY_FAIL, pszFail, 0, args[1] );
		return;
	}
	RPG_AdvanceClock( rpg_travel_hours.GetFloat() );
	s_bTravelArrive = true;
	s_flTravelArriveUntil = 0.0f;
	engine->ChangeLevel( pLoc->map.Get(), "lm_travel" );
}

// после поездки игрок выходит у остановки (а не в том же смещении от landmark, что на прошлой карте)
class CRPGTravelArrival : public CAutoGameSystemPerFrame
{
public:
	CRPGTravelArrival() : CAutoGameSystemPerFrame( "CRPGTravelArrival" ) {}
	virtual void LevelInitPostEntity()
	{
		if ( s_bTravelArrive )
			s_flTravelArriveUntil = gpGlobals->curtime + 5.0f;
	}
	virtual void FrameUpdatePostEntityThink()
	{
		if ( !s_bTravelArrive || s_flTravelArriveUntil <= 0.0f )
			return;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		CBaseEntity *pSpot = gEntList.FindEntityByName( NULL, "travel_arrive" );
		if ( pPlayer && pPlayer->IsAlive() && pSpot )
		{
			const QAngle ang = pSpot->GetAbsAngles();
			pPlayer->Teleport( &pSpot->GetAbsOrigin(), &ang, &vec3_origin );
			pPlayer->SnapEyeAngles( ang );
			s_bTravelArrive = false;
			const Vector &at = pSpot->GetAbsOrigin();
			Msg( "[rpg] tram: arrived at the stop of %s (%.0f %.0f %.0f)\n", STRING( gpGlobals->mapname ), at.x, at.y, at.z );
		}
		else if ( gpGlobals->curtime > s_flTravelArriveUntil )
			s_bTravelArrive = false;
	}
};
static CRPGTravelArrival g_RPGTravelArrival;

//========= HL2RPM ============//
//
// RPG-система (сервер): применение эффектов §4 и итогов проверок (см. rpg_conditions.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_conditions.h"
#include "ai_basenpc.h"
#include "player.h"
#include "quest_system.h"
#include "hl2rpm/rpg_quests.h"
#include "eventqueue.h"
#include "world.h"
#include "recipientfilter.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpg_debug;

static void RPG_PlaySoundToPlayer( CBasePlayer *pPlayer, const char *pszSound )
{
	if ( !pPlayer || !pszSound || !pszSound[0] )
		return;
	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	const bool bWave = Q_stristr( pszSound, ".wav" ) || Q_stristr( pszSound, ".mp3" );
	if ( bWave )
		CBaseEntity::PrecacheSound( pszSound );
	else
		CBaseEntity::PrecacheScriptSound( pszSound );
	EmitSound_t ep;
	ep.m_nChannel = CHAN_STATIC;
	ep.m_pSoundName = pszSound;
	ep.m_flVolume = 1.0f;
	ep.m_SoundLevel = SNDLVL_NONE;
	CBaseEntity::EmitSound( filter, pPlayer->entindex(), ep );
}

void RPGEffectList_t::Apply( CBasePlayer *pPlayer, CBaseEntity *pNPC, CBaseEntity *pCaller ) const
{
	if ( !pPlayer )
		pPlayer = RPG_GetPlayer();
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	if ( !pChar )
		return;

	for ( int i = 0; i < m_Effects.Count(); ++i )
	{
		const RPGEffect_t &e = m_Effects[i];
		if ( rpg_debug.GetBool() )
		{
			CUtlString desc;
			e.Describe( desc );
			Msg( "[rpg] effect %s\n", desc.Get() );
		}
		switch ( e.type )
		{
		case RPG_EFF_XP:			pChar->AddXP( e.amount, "effect" ); break;
		case RPG_EFF_REP:			pChar->AddRep( e.id.Get(), e.amount ); break;
		case RPG_EFF_CIVIL:			pChar->AddCivil( e.amount ); break;
		case RPG_EFF_SUSPICION:		pChar->AddSuspicion( (float)e.amount, true ); break;
		case RPG_EFF_HUMANITY:		pChar->AddHumanity( e.amount ); break;
		case RPG_EFF_RESONANCE:		pChar->AddResonance( e.amount ); break;
		case RPG_EFF_INFLUENCE:		pChar->AddInfluence( e.amount ); break;
		case RPG_EFF_CREDITS:		pChar->AddCredits( e.amount ); break;
		case RPG_EFF_GIVE_ITEM:		if ( pPlayer ) RPG_GiveItem( pPlayer, e.id.Get(), e.amount ); break;
		case RPG_EFF_TAKE_ITEM:		if ( pPlayer ) RPG_TakeItem( pPlayer, e.id.Get(), e.amount ); break;
		case RPG_EFF_SET_FLAG:
			if ( e.bString )
				pChar->SetFlagString( e.id.Get(), e.str.Get() );
			else
				pChar->SetFlag( e.id.Get(), e.amount );
			break;
		case RPG_EFF_CLEAR_FLAG:	pChar->ClearFlag( e.id.Get() ); break;
		case RPG_EFF_ADD_FLAG:		pChar->AddFlag( e.id.Get(), e.amount ); break;
		case RPG_EFF_DISPOSITION:
		{
			CAI_BaseNPC *pAI = pNPC ? pNPC->MyNPCPointer() : NULL;
			if ( !pNPC || pNPC->GetEntityName() == NULL_STRING )
			{
				DevWarning( "[rpg] effect disposition: no named interlocutor\n" );
				break;
			}
			pChar->AddDisposition( STRING( pNPC->GetEntityName() ), e.amount, pAI ? pAI->GetRPGDisposition() : 0 );
			break;
		}
		case RPG_EFF_DISPOSITION_NPC:
		{
			CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, e.id.Get() );
			CAI_BaseNPC *pAI = pEnt ? pEnt->MyNPCPointer() : NULL;
			pChar->AddDisposition( e.id.Get(), e.amount, pAI ? pAI->GetRPGDisposition() : 0 );
			break;
		}
		// квесты v2 (scripts/quests) - RPGQuests, старые (блоки "quests" диалогов) - CQuestSystem
		case RPG_EFF_GRANT_QUEST:
			if ( pPlayer && !( RPGQuests::IsV2( e.id.Get() ) ? RPGQuests::Grant( pPlayer, e.id.Get() ) : QuestSystem().GrantQuest( pPlayer, e.id.Get() ) ) )
				DevWarning( "[rpg] grant_quest %s failed\n", e.id.Get() );
			break;
		case RPG_EFF_COMPLETE_QUEST:
			if ( pPlayer && !( RPGQuests::IsV2( e.id.Get() ) ? RPGQuests::Complete( pPlayer, e.id.Get() ) : QuestSystem().CompleteQuest( pPlayer, e.id.Get() ) ) )
				DevWarning( "[rpg] complete_quest %s failed\n", e.id.Get() );
			break;
		case RPG_EFF_FAIL_QUEST:
			if ( pPlayer )
			{
				if ( RPGQuests::IsV2( e.id.Get() ) )
					RPGQuests::Fail( pPlayer, e.id.Get() );
				else
					QuestSystem().FailQuest( pPlayer, e.id.Get(), 0 );
			}
			break;
		case RPG_EFF_SET_STAGE:
			if ( pPlayer && !( RPGQuests::IsV2( e.id.Get() ) ? RPGQuests::SetStage( pPlayer, e.id.Get(), e.amount ) : QuestSystem().SetQuestStage( pPlayer, e.id.Get(), e.amount ) ) )
				DevWarning( "[rpg] set_stage %s:%d failed\n", e.id.Get(), e.amount );
			break;
		case RPG_EFF_QUEST_EVENT:	RPG_QuestEvent( pPlayer, e.str.Get(), e.id.Get() ); break;
		case RPG_EFF_DISCOVER:		pChar->Discover( e.id.Get() ); break;
		case RPG_EFF_NOTE:			pChar->AddNote( e.id.Get() ); break;
		case RPG_EFF_ACT:			pChar->SetAct( e.amount ); break;
		case RPG_EFF_HAGGLE:
		{
			const char *pszTrader = e.id.Length() ? e.id.Get() : RPG_GetNPCTrader( pNPC );
			if ( !pszTrader || !pszTrader[0] )
			{
				DevWarning( "[rpg] haggle: no trader id (rpg_trader of the NPC)\n" );
				break;
			}
			CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
			char szFlag[96];
			CRPGCharacterData::TraderHaggleFlag( pszTrader, false, szFlag, sizeof( szFlag ) );
			pChar->SetFlag( szFlag, clamp( e.amount, -50, 50 ) );
			CRPGCharacterData::TraderHaggleFlag( pszTrader, true, szFlag, sizeof( szFlag ) );
			pChar->SetFlag( szFlag, Max( 1, pChar->Data().GetFlagInt( RPG_FLAG_DAY, 1 ) ) );
			break;
		}
		case RPG_EFF_WAIT:
			if ( pPlayer && e.amount > 0 )
			{
				color32 black = { 0, 0, 0, 255 };
				UTIL_ScreenFade( pPlayer, black, 1.2f, 0.6f, FFADE_IN );
				RPG_AdvanceClock( (float)e.amount );
			}
			break;
		case RPG_EFF_DAMAGE:
			if ( pPlayer && e.amount > 0 )
			{
				CBaseEntity *pAttacker = pNPC ? pNPC : ( pCaller ? pCaller : GetWorldEntity() );
				CTakeDamageInfo info( pAttacker, pAttacker, (float)e.amount, DMG_GENERIC );
				pPlayer->TakeDamage( info );
			}
			break;
		case RPG_EFF_HEAL:
			if ( pPlayer && e.amount > 0 )
				pPlayer->TakeHealth( (float)e.amount, DMG_GENERIC );
			break;
		case RPG_EFF_START_COMBAT:
			// собеседник (и все NPC с его ftag) враждебен игроку - штатные входы NPC
			if ( pNPC && pNPC->MyNPCPointer() )
			{
				variant_t empty;
				empty.SetString( NULL_STRING );
				pNPC->AcceptInput( "HatePlayer", pPlayer, pCaller ? pCaller : pPlayer, empty, 0 );
				pNPC->AcceptInput( "HateFaction", pPlayer, pCaller ? pCaller : pPlayer, empty, 0 );
			}
			else
				DevWarning( "[rpg] start_combat: no NPC interlocutor\n" );
			break;
		case RPG_EFF_OPEN_BARTER:
		{
			const char *pszTrader = e.id.Length() ? e.id.Get() : RPG_GetNPCTrader( pNPC );
			if ( !pszTrader || !pszTrader[0] )
			{
				DevWarning( "[rpg] open_barter: no trader id (rpg_trader of the NPC)\n" );
				break;
			}
			RPG_OpenUI( pPlayer, "barter", pszTrader, pNPC );
			break;
		}
		case RPG_EFF_OPEN_CHARGEN:	RPG_OpenUI( pPlayer, "chargen", NULL, pNPC ); break;
		case RPG_EFF_OPEN_LEVELUP:	RPG_OpenUI( pPlayer, "levelup", NULL, pNPC ); break;
		case RPG_EFF_FIRE:
		{
			variant_t value;
			value.SetString( e.param.Length() ? AllocPooledString( e.param.Get() ) : NULL_STRING );
			g_EventQueue.AddEvent( e.id.Get(), e.str.Get(), value, e.delay, pPlayer, pCaller ? pCaller : pPlayer );
			break;
		}
		case RPG_EFF_NOTIFY:		pChar->Notify( RPG_NOTIFY_GENERIC, e.id.Get(), 0, e.param.Get() ); break;
		case RPG_EFF_SOUND:			RPG_PlaySoundToPlayer( pPlayer, e.id.Get() ); break;
		default:
			break;
		}
	}
}

void RPG_ApplyCheckOutcome( CBasePlayer *pPlayer, CBaseEntity *pNPC, const RPGCheck_t &check, const RPGCheckResult_t &result )
{
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	if ( !pChar || !check.IsValid() )
		return;
	if ( result.pass )
	{
		if ( check.kind == RPG_CHECK_BRIBE && result.cost > 0 )
			pChar->AddCredits( -result.cost );
		if ( result.xp > 0 )
			pChar->AddXP( result.xp, "check" );
	}
	else if ( pNPC && RPG_IsWatcher( pNPC ) )
	{
		// проваленная ложь ГО +15, провал подкупа +20 (PK-16 «Тонкий подкуп» - без подозрения)
		if ( check.kind == RPG_CHECK_LIE )
			pChar->AddSuspicion( 15.0f, true );
		else if ( check.kind == RPG_CHECK_BRIBE && pChar->Data().GetPerkRank( "PK-16" ) <= 0 )
			pChar->AddSuspicion( 20.0f, true );
	}
	CUtlString label;
	check.Describe( label );
	pChar->Notify( RPG_NOTIFY_CHECK, label.Get(), result.pass ? 1 : 0, RPGCheck_t::KindName( check.kind ) );
}

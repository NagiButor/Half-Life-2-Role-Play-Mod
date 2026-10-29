//========= HL2RPM ============//
//
// RPG-система (сервер): закон Альянса. Подозрение (rpg_character.cpp) - это то, что ГО делает:
//   25+ «Внимание»     юнит, который давно видит героя, подходит проверить документы
//   50+ «Досмотр»      подходит сразу: документы и досмотр - контрабанда изымается, штраф
//   75+ «Задержание»   подходит задерживать: сдаться, уговорить, откупиться или бежать (-> 100)
//   100 «Тревога»      ГО бьёт и стреляет (gordon_precriminal для них снят - rpg_entities.cpp),
//                      голос Надзора; уйти из виду до спада - или сдаться: присесть без оружия
//                      рядом с юнитом. Удар ГО, который уложил бы героя, - задержание.
// Задержание: затемнение, штраф (нет денег - долг law_debt), изъятие контрабанды, ГС, +часы,
// герой приходит в себя у info_target rpg_arrest_release карты (дежурка, участок) или на месте.
// Разговор с юнитом - диалог _law_stop (content/dialogs), его эффект law=... и условие law ....
// Part I/06 §3, Part I/07 §3, RPG_SPEC §2.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_law.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_dialog.h"
#include "hl2rpm/rpg_entities.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_formulas.h"
#include "ai_basenpc.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "npc_metropolice.h"
#include "player.h"
#include "inventory_system.h"
#include "engine/IEngineSound.h"
#include "recipientfilter.h"
#include "takedamageinfo.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar rpg_law_enable( "rpg_law_enable", "1", FCVAR_GAMEDLL, "RPG: Civil Protection reacts to suspicion - papers checks, searches, arrests" );
static ConVar rpg_law_check_dist( "rpg_law_check_dist", "900", FCVAR_GAMEDLL, "RPG: a CP unit that sees the player this close comes for a check" );
static ConVar rpg_law_attention_interval( "rpg_law_attention_interval", "75", FCVAR_GAMEDLL, "RPG: at Attention (25+) a unit asks for papers at most this often (seconds)" );
static ConVar rpg_law_debug( "rpg_law_debug", "0", FCVAR_GAMEDLL, "RPG: print the law system decisions" );

#define LAW_MSG( ... ) do { if ( rpg_law_debug.GetBool() ) Msg( "[law] " __VA_ARGS__ ); } while ( 0 )

#define LAW_DIALOG			"_law_stop"
#define LAW_TALK_DIST		110.0f
#define LAW_SURRENDER_DIST	240.0f
#define LAW_SHOVE_SUSPICION	6.0f		// юнит оттолкнул героя / сделал замечание (не чаще раза в 3 с)

enum LawArrestKind_t
{
	LAW_ARREST_DIALOG = 0,		// сдался юниту в разговоре
	LAW_ARREST_SURRENDER,		// сдался во время тревоги (присел без оружия)
	LAW_ARREST_KNOCKOUT,		// уложили дубинкой
};

enum LawOutcome_t
{
	LAW_OUT_NONE = 0,
	LAW_OUT_RELEASE,
	LAW_OUT_ARREST,
	LAW_OUT_FLEE,
};

static const char *s_pszLawSounds[] =
{
	"npc/metropolice/vo/citizen.wav", "npc/metropolice/vo/holdit.wav", "npc/metropolice/vo/holditrightthere.wav",
	"npc/metropolice/vo/dontmove.wav", "npc/metropolice/vo/search.wav", "npc/metropolice/vo/prepareforjudgement.wav",
	"npc/metropolice/vo/movealong.wav", "npc/metropolice/vo/suspectlocationunknown.wav",
	"npc/overwatch/cityvoice/f_evasionbehavior_2_spkr.wav", "npc/overwatch/cityvoice/f_anticitizenreport_spkr.wav",
	"weapons/stunstick/stunstick_impact1.wav", NULL
};

static bool IsCP( CBaseEntity *pEnt )
{
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	if ( !pNPC || !pNPC->IsAlive() )
		return false;
	const char *pszFaction = pNPC->GetRPGFaction();
	if ( pszFaction && pszFaction[0] )
		return !Q_stricmp( pszFaction, "cp" );
	return pNPC->ClassMatches( "npc_metropolice" );
}

static void Say( CBasePlayer *pPlayer, CBaseEntity *pFrom, const char *pszWav )
{
	if ( !pPlayer || !pszWav )
		return;
	CSingleUserRecipientFilter filter( pPlayer );
	EmitSound_t ep;
	const bool bWorld = pFrom && pFrom != pPlayer;
	ep.m_nChannel = bWorld ? CHAN_VOICE : CHAN_STATIC;
	ep.m_pSoundName = pszWav;
	ep.m_flVolume = 1.0f;
	ep.m_SoundLevel = bWorld ? SNDLVL_80dB : SNDLVL_NONE;
	CBaseEntity::EmitSound( filter, ( bWorld ? pFrom : pPlayer )->entindex(), ep );
}

static bool IsContrabandClass( const char *pszClass )
{
	if ( !pszClass || !pszClass[0] || !Q_strnicmp( pszClass, "item_suit", 9 ) )
		return false;
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
	if ( pDef )
		return pDef->contraband > 0;
	return !Q_strnicmp( pszClass, "weapon_", 7 );		// неизвестное оружие - запрещено
}

// контрабанда у героя: в инвентаре и в руках (без повторов)
static void FindContraband( CBasePlayer *pPlayer, CUtlVector<CUtlString> &out )
{
	CUtlVector<CUtlString> items;
	InventorySystem::GetInventoryItemStringsForPlayer( pPlayer, items );
	for ( int i = 0; i < items.Count(); ++i )
	{
		char sz[128];
		Q_strncpy( sz, items[i].Get(), sizeof( sz ) );
		char *pColon = strchr( sz, ':' );
		if ( pColon )
			*pColon = '\0';
		if ( IsContrabandClass( sz ) && out.Find( CUtlString( sz ) ) == out.InvalidIndex() )
			out.AddToTail( CUtlString( sz ) );
	}
	for ( int i = 0; i < pPlayer->WeaponCount(); ++i )
	{
		CBaseCombatWeapon *pWeapon = pPlayer->GetWeapon( i );
		if ( pWeapon && IsContrabandClass( pWeapon->GetClassname() ) && out.Find( CUtlString( pWeapon->GetClassname() ) ) == out.InvalidIndex() )
			out.AddToTail( CUtlString( pWeapon->GetClassname() ) );
	}
}

// в форме ГО / Надзора свои не останавливают и замечаний не считают (тревога - другое дело)
static bool IsUniformed( CBasePlayer *pPlayer )
{
	return pPlayer->m_Local.m_iSuitType == RPG_SUIT_CP || pPlayer->m_Local.m_iSuitType == RPG_SUIT_OVERWATCH;
}

static bool IsArmed( CBasePlayer *pPlayer )
{
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	return pWeapon && pWeapon->IsWeaponVisible() && IsContrabandClass( pWeapon->GetClassname() );
}

// изъять всю контрабанду; список классов через запятую - для уведомления
static int Confiscate( CBasePlayer *pPlayer, CUtlString &list )
{
	CUtlVector<CUtlString> found;
	FindContraband( pPlayer, found );
	int n = 0;
	for ( int i = 0; i < found.Count(); ++i )
	{
		if ( RPG_TakeItem( pPlayer, found[i].Get(), 999, false ) <= 0 )
			continue;
		if ( n++ )
			list.Append( "," );
		list.Append( found[i].Get() );
	}
	return n;
}

// штраф: что есть - сразу, остальное - долг на учётной карте
static int PayFine( int iFine )
{
	CRPGCharacter *pChar = RPG_GetCharacter();
	const int iPaid = Min( Max( 0, pChar->Data().m_iCredits ), iFine );
	if ( iPaid > 0 )
		pChar->AddCredits( -iPaid, true );
	if ( iFine > iPaid )
		pChar->AddFlag( "law_debt", iFine - iPaid );
	return iPaid;
}

static unsigned int NameHash( CBaseEntity *pEnt )
{
	const char *psz = pEnt ? STRING( pEnt->GetEntityName() ) : "";
	unsigned int h = 2166136261u;
	if ( !psz || !psz[0] )
		return pEnt ? (unsigned int)pEnt->entindex() * 2654435761u : 0;
	for ( ; *psz; ++psz )
		h = ( h ^ (unsigned char)( *psz | 0x20 ) ) * 16777619u;
	return h;
}

// «берёт»: каждый генерик-юнит со скрытым флагом (Акт I - ~30 %, Part I/07 §5); Гэннон берёт всегда
static bool IsCorrupt( CBaseEntity *pNPC )
{
	if ( !pNPC )
		return false;
	if ( !Q_strnicmp( STRING( pNPC->GetEntityName() ), "npc_gannon", 10 ) )
		return true;
	return ( NameHash( pNPC ) % 100u ) < 30u;
}

// ===========================================================================
// Система
// ===========================================================================
class CRPGLawSystem : public CAutoGameSystemPerFrame
{
public:
	CRPGLawSystem() : CAutoGameSystemPerFrame( "CRPGLawSystem" ) { Reset(); }

	virtual void LevelInitPostEntity()
	{
		Reset();
		for ( int i = 0; s_pszLawSounds[i]; ++i )
			enginesound->PrecacheSound( s_pszLawSounds[i], true );
	}
	virtual void LevelShutdownPreEntity() { Reset(); }
	virtual void FrameUpdatePostEntityThink();

	void Reset()
	{
		m_hEngager = NULL;
		m_bTalking = false;
		m_iOutcome = LAW_OUT_NONE;
		m_bArrestPending = false;
		m_bArrestQueued = false;
		m_flNextThink = 0.0f;
		m_flNextStop = 0.0f;
		m_flNextAttention = 0.0f;
		m_flSeenSince = -1.0f;
		m_flSurrender = 0.0f;
		m_flNextCopNote = 0.0f;
		m_bForgivePending = false;
		m_Returning.RemoveAll();
	}

	void BeginEngage( CBasePlayer *pPlayer, CAI_BaseNPC *pCP, int tier );
	void Disengage( bool bReturn );
	void BeginArrest( CBasePlayer *pPlayer, int kind );
	void QueueArrest( int kind ) { m_bArrestQueued = true; m_iQueuedKind = kind; }
	bool IsArrestPending() const { return m_bArrestPending || m_bArrestQueued; }
	void SetOutcome( int iOutcome ) { m_iOutcome = iOutcome; }
	CBaseEntity *GetEngager() { return m_hEngager.Get(); }
	bool OnCopWarned( CBasePlayer *pPlayer, CBaseEntity *pCop );
	// юниты забывают прежнее - в следующем кадре: просьба приходит и из удара юнита (урон игроку)
	void ForgiveLater() { m_bForgivePending = true; }

private:
	void ForgiveNow();
	CAI_BaseNPC *FindChecker( CBasePlayer *pPlayer );
	void Chase( CBasePlayer *pPlayer, CAI_BaseNPC *pCP );
	void UpdateEngage( CBasePlayer *pPlayer );
	void UpdateSurrender( CBasePlayer *pPlayer, float dt );
	void UpdateReturning();
	void FinishArrest( CBasePlayer *pPlayer );

	CHandle<CBaseEntity> m_hEngager;
	int m_iEngageTier;
	float m_flEngageStart, m_flLastSeen, m_flNextRepath;
	Vector m_vecPost;
	float m_flPostYaw;
	bool m_bTalking;
	int m_iOutcome;

	bool m_bArrestPending, m_bArrestQueued;
	int m_iArrestKind, m_iQueuedKind;
	float m_flArrestTime;

	float m_flNextThink, m_flNextStop, m_flNextAttention, m_flSeenSince, m_flSurrender;
	float m_flNextCopNote;
	bool m_bForgivePending;

	struct Returning_t { CHandle<CBaseEntity> h; Vector pos; float yaw; float until; };
	CUtlVector<Returning_t> m_Returning;
};

static CRPGLawSystem g_RPGLaw;

CAI_BaseNPC *CRPGLawSystem::FindChecker( CBasePlayer *pPlayer )
{
	const float flMax = rpg_law_check_dist.GetFloat();
	CAI_BaseNPC *pBest = NULL;
	float flBest = flMax * flMax;
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); ++i )
	{
		CAI_BaseNPC *pNPC = ppAIs[i];
		if ( !IsCP( pNPC ) || pNPC->IRelationType( pPlayer ) == D_HT )
			continue;
		const float d2 = pNPC->GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() );
		if ( d2 >= flBest )
			continue;
		if ( !pNPC->FInViewCone( pPlayer ) || !pNPC->FVisible( pPlayer ) )
			continue;
		pBest = pNPC;
		flBest = d2;
	}
	return pBest;
}

void CRPGLawSystem::Chase( CBasePlayer *pPlayer, CAI_BaseNPC *pCP )
{
	Vector pos = pPlayer->GetAbsOrigin();
	pCP->TranslateNavGoal( pPlayer, pos );
	pCP->m_vecLastPosition = pos;
	const int iSched = m_iEngageTier >= RPG_STIER_DETAIN ? SCHED_FORCED_GO_RUN : SCHED_FORCED_GO;
	if ( pCP->IsCurSchedule( iSched, false ) && pCP->GetNavigator()->IsGoalActive() )
	{
		pCP->GetNavigator()->UpdateGoalPos( pos );
		return;
	}
	pCP->m_flMoveWaitFinished = gpGlobals->curtime;
	pCP->SetSchedule( iSched );
}

void CRPGLawSystem::BeginEngage( CBasePlayer *pPlayer, CAI_BaseNPC *pCP, int tier )
{
	m_hEngager = pCP;
	m_iEngageTier = tier;
	m_flEngageStart = m_flLastSeen = gpGlobals->curtime;
	m_flNextRepath = 0.0f;
	m_bTalking = false;
	m_iOutcome = LAW_OUT_NONE;
	m_vecPost = pCP->GetAbsOrigin();
	m_flPostYaw = pCP->GetAbsAngles().y;
	if ( pCP->m_hCine != NULL )
		pCP->ExitScriptedSequence();
	Chase( pPlayer, pCP );
	Say( pPlayer, pCP, tier >= RPG_STIER_DETAIN ? "npc/metropolice/vo/holditrightthere.wav" :
		( tier >= RPG_STIER_SEARCH ? "npc/metropolice/vo/holdit.wav" : "npc/metropolice/vo/citizen.wav" ) );
	RPG_GetCharacter()->Notify( RPG_NOTIFY_LAW, tier >= RPG_STIER_DETAIN ? "detain" : ( tier >= RPG_STIER_SEARCH ? "search" : "check" ) );
	LAW_MSG( "%s comes for a %s (suspicion %.0f)\n", STRING( pCP->GetEntityName() ), RPGFormulas::SuspicionTierKey( tier ),
		RPG_GetCharacter()->Data().m_flSuspicion );
}

void CRPGLawSystem::Disengage( bool bReturn )
{
	CAI_BaseNPC *pCP = m_hEngager.Get() ? m_hEngager->MyNPCPointer() : NULL;
	m_hEngager = NULL;
	m_bTalking = false;
	m_iOutcome = LAW_OUT_NONE;
	if ( !pCP || !pCP->IsAlive() || !bReturn )
		return;
	if ( pCP->GetAbsOrigin().DistToSqr( m_vecPost ) > 32.0f * 32.0f )
	{
		pCP->m_vecLastPosition = m_vecPost;
		pCP->m_flMoveWaitFinished = gpGlobals->curtime;
		pCP->SetSchedule( SCHED_FORCED_GO );
	}
	Returning_t &r = m_Returning[m_Returning.AddToTail()];
	r.h = pCP;
	r.pos = m_vecPost;
	r.yaw = m_flPostYaw;
	r.until = gpGlobals->curtime + 20.0f;
}

void CRPGLawSystem::UpdateReturning()
{
	for ( int i = m_Returning.Count() - 1; i >= 0; --i )
	{
		Returning_t &r = m_Returning[i];
		CAI_BaseNPC *pCP = r.h.Get() ? r.h->MyNPCPointer() : NULL;
		if ( !pCP || !pCP->IsAlive() || pCP == m_hEngager.Get() )
		{
			m_Returning.Remove( i );
			continue;
		}
		if ( ( pCP->GetAbsOrigin() - r.pos ).Length2DSqr() < 40.0f * 40.0f || gpGlobals->curtime > r.until )
		{
			pCP->GetMotor()->SetIdealYaw( r.yaw );
			m_Returning.Remove( i );
		}
	}
}

void CRPGLawSystem::UpdateEngage( CBasePlayer *pPlayer )
{
	CAI_BaseNPC *pCP = m_hEngager.Get() ? m_hEngager->MyNPCPointer() : NULL;
	if ( !pCP || !pCP->IsAlive() || pCP->IRelationType( pPlayer ) == D_HT )
	{
		Disengage( false );
		return;
	}
	CRPGCharacter *pChar = RPG_GetCharacter();
	if ( m_bTalking )
	{
		if ( RPGDialog::IsActive() && RPGDialog::GetActiveNPC() == pCP )
			return;
		// разговор окончен: чем
		switch ( m_iOutcome )
		{
		case LAW_OUT_NONE:
			// ушёл, не дослушав
			pChar->AddSuspicion( 15.0f, false );
			pChar->Notify( RPG_NOTIFY_LAW, "walked_off" );
			m_flNextStop = gpGlobals->curtime + 15.0f;
			Disengage( true );
			break;
		case LAW_OUT_FLEE:
			pChar->AddSuspicion( m_iEngageTier >= RPG_STIER_DETAIN ? 100.0f : 35.0f, false );
			pChar->Notify( RPG_NOTIFY_LAW, "fled" );
			Disengage( false );
			break;
		case LAW_OUT_ARREST:
			Disengage( false );
			break;
		default:
			m_flNextStop = gpGlobals->curtime + 60.0f;
			m_flNextAttention = gpGlobals->curtime + rpg_law_attention_interval.GetFloat() * 2.0f;
			Disengage( true );
			break;
		}
		return;
	}
	if ( RPGDialog::IsActive() )
		return;		// герой говорит с кем-то другим - юнит ждёт рядом

	const float dist = ( pPlayer->WorldSpaceCenter() - pCP->WorldSpaceCenter() ).Length();
	const bool bSeen = pCP->FVisible( pPlayer );
	if ( bSeen )
		m_flLastSeen = gpGlobals->curtime;
	// скрылся: из виду дольше 6 с или далеко
	if ( gpGlobals->curtime - m_flLastSeen > 6.0f || dist > 1400.0f )
	{
		pChar->AddSuspicion( m_iEngageTier >= RPG_STIER_DETAIN ? 100.0f : 20.0f, false );
		pChar->Notify( RPG_NOTIFY_LAW, "fled" );
		Say( pPlayer, pCP, "npc/metropolice/vo/suspectlocationunknown.wav" );
		LAW_MSG( "the player got away from %s\n", STRING( pCP->GetEntityName() ) );
		m_flNextStop = gpGlobals->curtime + 10.0f;
		Disengage( true );
		return;
	}
	if ( gpGlobals->curtime - m_flEngageStart > 30.0f )
	{
		LAW_MSG( "%s could not reach the player\n", STRING( pCP->GetEntityName() ) );
		m_flNextStop = gpGlobals->curtime + 20.0f;
		Disengage( true );
		return;
	}
	if ( dist <= LAW_TALK_DIST && bSeen )
	{
		pCP->GetNavigator()->StopMoving( false );
		pCP->ClearSchedule( "law: talking to the player" );
		pCP->GetMotor()->SetIdealYawToTarget( pPlayer->GetAbsOrigin() );
		pCP->SetTurnActivity();
		if ( RPGDialog::Start( pPlayer, pCP, -1, LAW_DIALOG ) )
		{
			m_bTalking = true;
			m_iOutcome = LAW_OUT_NONE;
		}
		else
		{
			m_flNextStop = gpGlobals->curtime + 20.0f;
			Disengage( true );
		}
		return;
	}
	if ( gpGlobals->curtime >= m_flNextRepath )
	{
		m_flNextRepath = gpGlobals->curtime + 0.4f;
		Chase( pPlayer, pCP );
	}
}

void CRPGLawSystem::UpdateSurrender( CBasePlayer *pPlayer, float dt )
{
	const bool bCrouched = ( pPlayer->GetFlags() & FL_DUCKING ) != 0;
	bool bNear = false;
	if ( bCrouched && !IsArmed( pPlayer ) )
	{
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs() && !bNear; ++i )
		{
			CAI_BaseNPC *pNPC = ppAIs[i];
			if ( IsCP( pNPC ) && pNPC->GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() ) < LAW_SURRENDER_DIST * LAW_SURRENDER_DIST &&
				pNPC->FVisible( pPlayer ) )
				bNear = true;
		}
	}
	m_flSurrender = bNear ? m_flSurrender + dt : 0.0f;
	if ( m_flSurrender >= 1.2f )
	{
		m_flSurrender = 0.0f;
		LAW_MSG( "the player surrenders\n" );
		BeginArrest( pPlayer, LAW_ARREST_SURRENDER );
	}
}

// юнит оттолкнул героя или сделал замечание (CNPC_MetroPolice::IncrementPlayerCriminalStatus):
// это подозрение, дальше - обычная лестница закона. false - закон выключен (стоковый счёт юнита)
bool CRPGLawSystem::OnCopWarned( CBasePlayer *pPlayer, CBaseEntity *pCop )
{
	if ( !rpg_law_enable.GetBool() || !pPlayer )
		return false;
	// задержание, разговор, своя форма - не в счёт
	if ( IsArrestPending() || RPGDialog::IsActive() || IsUniformed( pPlayer ) )
		return true;
	if ( gpGlobals->curtime < m_flNextCopNote )
		return true;
	m_flNextCopNote = gpGlobals->curtime + 3.0f;
	RPG_GetCharacter()->AddSuspicion( LAW_SHOVE_SUSPICION, false );
	LAW_MSG( "%s pushed the player away: +%.0f suspicion\n", pCop ? STRING( pCop->GetEntityName() ) : "?", LAW_SHOVE_SUSPICION );
	return true;
}

void CRPGLawSystem::ForgiveNow()
{
	m_bForgivePending = false;
	CBasePlayer *pPlayer = RPG_GetPlayer();
	if ( !pPlayer )
		return;
	int n = 0;
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); ++i )
	{
		CNPC_MetroPolice *pCop = dynamic_cast< CNPC_MetroPolice * >( ppAIs[i] );
		if ( !pCop || !pCop->IsAlive() || pCop->IRelationType( pPlayer ) == D_HT )
			continue;
		pCop->RPG_ForgivePlayer();
		++n;
	}
	LAW_MSG( "%d units forget the player's offences\n", n );
}

void CRPGLawSystem::BeginArrest( CBasePlayer *pPlayer, int kind )
{
	if ( m_bArrestPending || !pPlayer )
		return;
	m_bArrestPending = true;
	m_bArrestQueued = false;
	m_iArrestKind = kind;
	m_flArrestTime = gpGlobals->curtime + 2.2f;
	CBaseEntity *pCP = m_hEngager.Get();
	Disengage( false );
	if ( RPGDialog::IsActive() )
		RPGDialog::Stop();
	color32 black = { 0, 0, 0, 255 };
	UTIL_ScreenFade( pPlayer, black, kind == LAW_ARREST_KNOCKOUT ? 0.6f : 1.6f, 1.2f, FFADE_OUT | FFADE_STAYOUT );
	pPlayer->AddFlag( FL_FROZEN );
	if ( kind == LAW_ARREST_KNOCKOUT )
		Say( pPlayer, pPlayer, "weapons/stunstick/stunstick_impact1.wav" );
	else
		Say( pPlayer, pCP ? pCP : pPlayer, "npc/metropolice/vo/prepareforjudgement.wav" );
	// ГО сразу перестаёт бить: тревога снимается вместе с подозрением, юниты забывают замечания
	RPG_GetCharacter()->SetSuspicion( 0.0f );
	ForgiveLater();
	LAW_MSG( "arrest (%d)\n", kind );
}

void CRPGLawSystem::FinishArrest( CBasePlayer *pPlayer )
{
	m_bArrestPending = false;
	CRPGCharacter *pChar = RPG_GetCharacter();
	const int kind = m_iArrestKind;
	// Part I/07 §1, §3: штраф, ГС, изъятие; сопротивление - строже
	const int iFine = kind == LAW_ARREST_KNOCKOUT ? 80 : ( kind == LAW_ARREST_SURRENDER ? 40 : 30 );
	const int iCivil = kind == LAW_ARREST_KNOCKOUT ? -15 : -5;
	CUtlString taken;
	Confiscate( pPlayer, taken );
	PayFine( iFine );
	pChar->AddCivil( iCivil, true );
	pChar->AddFlag( "law_arrests", 1 );
	pChar->SetSuspicion( 0.0f );
	RPG_AdvanceClock( kind == LAW_ARREST_KNOCKOUT ? 4.0f : 2.0f );
	// за часы в участке герой приходит в себя: не меньше 60 % здоровья - ниже без костюма весь экран
	// пульсирует красным с дыханием (m_flSuitlessHealthOverlay, player.cpp), и задержанный «просыпался» в нём
	const int iMinHealth = (int)ceilf( pPlayer->GetMaxHealth() * 0.6f );
	if ( pPlayer->GetHealth() < iMinHealth )
		pPlayer->SetHealth( iMinHealth );
	CBaseEntity *pDest = gEntList.FindEntityByName( NULL, "rpg_arrest_release" );
	if ( pDest )
	{
		QAngle ang = pDest->GetAbsAngles();
		Vector pos = pDest->GetAbsOrigin();
		pPlayer->Teleport( &pos, &ang, &vec3_origin );
		pPlayer->SnapEyeAngles( ang );
	}
	pPlayer->RemoveFlag( FL_FROZEN );
	color32 black = { 0, 0, 0, 255 };
	UTIL_ScreenFade( pPlayer, black, 2.5f, 0.0f, FFADE_IN | FFADE_PURGE );
	pChar->Notify( RPG_NOTIFY_LAW, "arrested", iFine, taken.Get() );
	ForgiveNow();
	m_flNextStop = gpGlobals->curtime + 90.0f;
	m_flNextAttention = gpGlobals->curtime + 180.0f;
	LAW_MSG( "released: fine %d, civil %d, confiscated '%s'%s\n", iFine, iCivil, taken.Get(), pDest ? ", at rpg_arrest_release" : "" );
}

void CRPGLawSystem::FrameUpdatePostEntityThink()
{
	CBasePlayer *pPlayer = RPG_GetPlayer();
	if ( !pPlayer || !pPlayer->IsConnected() )
		return;
	if ( m_bForgivePending )
		ForgiveNow();
	if ( m_bArrestPending )
	{
		if ( gpGlobals->curtime >= m_flArrestTime )
			FinishArrest( pPlayer );
		return;
	}
	if ( m_bArrestQueued )
	{
		m_bArrestQueued = false;
		BeginArrest( pPlayer, m_iQueuedKind );
		return;
	}
	if ( gpGlobals->curtime < m_flNextThink )
		return;
	const float dt = ( m_flNextThink > 0.0f ) ? Min( 0.5f, gpGlobals->curtime - ( m_flNextThink - 0.1f ) ) : 0.1f;
	m_flNextThink = gpGlobals->curtime + 0.1f;
	UpdateReturning();
	if ( !rpg_law_enable.GetBool() || !pPlayer->IsAlive() )
	{
		if ( m_hEngager.Get() )
			Disengage( true );
		return;
	}

	const CRPGCharacterData &d = RPG_GetCharacter()->Data();
	if ( d.m_bAlarm )
	{
		if ( m_hEngager.Get() )
			Disengage( false );
		UpdateSurrender( pPlayer, dt );
		return;
	}
	m_flSurrender = 0.0f;
	if ( m_hEngager.Get() )
	{
		UpdateEngage( pPlayer );
		return;
	}
	if ( RPGDialog::IsActive() || ( pPlayer->GetFlags() & FL_FROZEN ) || pPlayer->IsInAVehicle() )
	{
		m_flSeenSince = -1.0f;
		return;
	}
	if ( IsUniformed( pPlayer ) )
	{
		m_flSeenSince = -1.0f;
		return;
	}
	const int tier = d.GetSuspicionTier();
	if ( tier < RPG_STIER_ATTENTION || gpGlobals->curtime < m_flNextStop )
	{
		m_flSeenSince = -1.0f;
		return;
	}
	CAI_BaseNPC *pCP = FindChecker( pPlayer );
	if ( !pCP )
	{
		m_flSeenSince = -1.0f;
		return;
	}
	if ( m_flSeenSince < 0.0f )
		m_flSeenSince = gpGlobals->curtime;
	// «Внимание»: проверка не чаще раза в rpg_law_attention_interval, и только если юнит смотрит уже 4 с
	if ( tier == RPG_STIER_ATTENTION && ( gpGlobals->curtime < m_flNextAttention || gpGlobals->curtime - m_flSeenSince < 4.0f ) )
		return;
	if ( tier >= RPG_STIER_SEARCH && gpGlobals->curtime - m_flSeenSince < 0.8f )
		return;
	m_flNextAttention = gpGlobals->curtime + rpg_law_attention_interval.GetFloat();
	m_flSeenSince = -1.0f;
	BeginEngage( pPlayer, pCP, tier );
}

// ===========================================================================
// Внешний интерфейс
// ===========================================================================
void RPG_Law_OnAlarm( bool bOn )
{
	CBasePlayer *pPlayer = RPG_GetPlayer();
	if ( !pPlayer )
		return;
	if ( bOn )
	{
		Say( pPlayer, pPlayer, "npc/overwatch/cityvoice/f_evasionbehavior_2_spkr.wav" );
		RPG_GetCharacter()->Notify( RPG_NOTIFY_LAW, "alarm" );
	}
	else
	{
		if ( !g_RPGLaw.IsArrestPending() )
			RPG_GetCharacter()->Notify( RPG_NOTIFY_LAW, "alarm_end" );
		g_RPGLaw.ForgiveLater();
	}
}

bool RPG_Law_CopWarned( CBaseEntity *pCop, CBasePlayer *pPlayer )
{
	return g_RPGLaw.OnCopWarned( pPlayer, pCop );
}

bool RPG_Law_FilterPlayerDamage( CBasePlayer *pPlayer, CTakeDamageInfo &info )
{
	if ( !pPlayer || !rpg_law_enable.GetBool() )
		return false;
	CBaseEntity *pAttacker = info.GetAttacker();
	if ( !IsCP( pAttacker ) )
		return false;
	if ( g_RPGLaw.IsArrestPending() )
	{
		info.SetDamage( 0.0f );
		return true;
	}
	// город: ГО не убивает, а укладывает - и задерживает (Part I/07 §3 «Схвачен без сознания»)
	if ( (float)pPlayer->GetHealth() - info.GetDamage() > 10.0f )
		return false;
	info.SetDamage( Max( 0.0f, (float)pPlayer->GetHealth() - 10.0f ) );
	g_RPGLaw.BeginArrest( pPlayer, LAW_ARREST_KNOCKOUT );
	return true;
}

void RPG_Law_Effect( CBasePlayer *pPlayer, CBaseEntity *pNPC, const char *pszAction )
{
	if ( !pPlayer || !pszAction || !pszAction[0] )
		return;
	CRPGCharacter *pChar = RPG_GetCharacter();
	if ( !Q_stricmp( pszAction, "release" ) )
	{
		g_RPGLaw.SetOutcome( LAW_OUT_RELEASE );
	}
	else if ( !Q_stricmp( pszAction, "frisk" ) )
	{
		CUtlString taken;
		if ( Confiscate( pPlayer, taken ) > 0 )
		{
			PayFine( 30 );
			pChar->AddCivil( -5, true );
			pChar->Notify( RPG_NOTIFY_LAW, "frisk_found", 30, taken.Get() );
		}
		else
			pChar->Notify( RPG_NOTIFY_LAW, "frisk_clean" );
		g_RPGLaw.SetOutcome( LAW_OUT_RELEASE );
	}
	else if ( !Q_stricmp( pszAction, "arrest" ) )
	{
		g_RPGLaw.SetOutcome( LAW_OUT_ARREST );
		g_RPGLaw.QueueArrest( LAW_ARREST_DIALOG );		// не из середины разговора: в следующем кадре
	}
	else if ( !Q_stricmp( pszAction, "flee" ) )
	{
		g_RPGLaw.SetOutcome( LAW_OUT_FLEE );
	}
	else if ( !Q_strnicmp( pszAction, "fine:", 5 ) )
	{
		PayFine( Max( 0, atoi( pszAction + 5 ) ) );
	}
	else if ( !Q_stricmp( pszAction, "pay_debt" ) )
	{
		const int iDebt = pChar->Data().GetFlagInt( "law_debt" );
		const int iPay = Min( iDebt, Max( 0, pChar->Data().m_iCredits ) );
		if ( iPay > 0 )
		{
			pChar->AddCredits( -iPay, true );
			if ( iDebt - iPay > 0 )
				pChar->SetFlag( "law_debt", iDebt - iPay );
			else
				pChar->ClearFlag( "law_debt" );
		}
	}
	else
		DevWarning( "[law] unknown action law=%s\n", pszAction );
}

int RPG_Law_Query( CBasePlayer *pPlayer, CBaseEntity *pNPC, const char *pszWhat )
{
	if ( !pszWhat )
		return -1;
	if ( !pPlayer )
		pPlayer = RPG_GetPlayer();
	if ( !pPlayer )
		return -1;
	const CRPGCharacterData &d = RPG_GetCharacter()->Data();
	if ( !Q_stricmp( pszWhat, "contraband" ) )
	{
		CUtlVector<CUtlString> found;
		FindContraband( pPlayer, found );
		return found.Count() > 0 ? 1 : 0;
	}
	if ( !Q_stricmp( pszWhat, "armed" ) )
		return IsArmed( pPlayer ) ? 1 : 0;
	if ( !Q_stricmp( pszWhat, "corrupt" ) )
		return IsCorrupt( pNPC ) ? 1 : 0;
	if ( !Q_stricmp( pszWhat, "debt" ) )
		return d.GetFlagInt( "law_debt" ) > 0 ? 1 : 0;
	if ( !Q_stricmp( pszWhat, "record" ) )
		return d.GetFlagInt( "law_arrests" ) > 0 ? 1 : 0;
	if ( !Q_stricmp( pszWhat, "papers" ) )
		return ( RPG_CountItem( pPlayer, "item_temp_pass" ) > 0 || RPG_CountItem( pPlayer, "item_account_card" ) > 0 ) ? 1 : 0;
	if ( !Q_stricmp( pszWhat, "stop" ) )
		return ( pNPC && pNPC == g_RPGLaw.GetEngager() ) ? 1 : 0;
	return -1;
}

// ---------------------------------------------------------------------------
// Отладка
// ---------------------------------------------------------------------------
CON_COMMAND_F( rpg_law_test, "RPG: rpg_law_test <suspicion> - set suspicion; the nearest CP unit that sees you reacts", FCVAR_CHEAT )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	const float v = args.ArgC() > 1 ? atof( args[1] ) : 60.0f;
	g_RPGLaw.Reset();
	RPG_GetCharacter()->SetSuspicion( v );
	Msg( "[law] suspicion %.0f\n", v );
}

CON_COMMAND_F( rpg_law_arrest, "RPG: rpg_law_arrest [0 dialog|1 surrender|2 knockout] - arrest the player now", FCVAR_CHEAT )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	g_RPGLaw.BeginArrest( RPG_GetPlayer(), args.ArgC() > 1 ? clamp( atoi( args[1] ), 0, 2 ) : LAW_ARREST_SURRENDER );
}

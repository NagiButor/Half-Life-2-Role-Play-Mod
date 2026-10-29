//========= HL2RPM ============//
//
// RPG-система: энтити карт (RPG_SPEC §7, §11).
//   logic_rpg        - эффекты/условия по входам, OnTrue/OnFalse, слои карты по флагам
//   logic_rpg_watch  - выходы по уровням подозрения
//   trigger_rpg      - браш: игрок вошёл -> условие, эффекты, событие квеста, открытие локации
//   trigger_rpg_zone - браш: зона контроля Альянса (1 обычная, 2 запретная)
//   prop_rpg_usable  - модель с действием по +use: записка, терминал, кровать (сон), эффекты
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_entities.h"
#include "hl2rpm/rpg_dialog.h"
#include "hl2rpm/rpg_formulas.h"
#include "triggers.h"
#include "props.h"
#include "ai_basenpc.h"
#include "ai_basenpc.h"
#include "ai_memory.h"
#include "player.h"
#include "shake.h"
#include "timecycle/env_timecycle.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpg_debug;

static CBasePlayer *ActivatorPlayer( CBaseEntity *pActivator )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );
	return pPlayer ? pPlayer : RPG_GetPlayer();
}

// ===========================================================================
// logic_rpg
// ===========================================================================
class CLogicRPG : public CLogicalEntity
{
public:
	DECLARE_CLASS( CLogicRPG, CLogicalEntity );
	DECLARE_DATADESC();

	CLogicRPG() : m_bTestOnSpawn( false ), m_iLastResult( -1 ) {}

	void ScheduleSpawnTest()
	{
		if ( !m_bTestOnSpawn )
			return;
		SetThink( &CLogicRPG::SpawnTestThink );
		SetNextThink( gpGlobals->curtime + 0.1f );
	}

	void SpawnTestThink()
	{
		SetThink( NULL );
		RunTest( STRING( m_iszCondition ), RPG_GetPlayer() );
	}

	void RunTest( const char *pszExpr, CBaseEntity *pActivator )
	{
		CBasePlayer *pPlayer = ActivatorPlayer( pActivator );
		CUtlString reason;
		bool bMet = RPG_TestCondition( pszExpr, pPlayer, NULL, &reason );
		if ( rpg_debug.GetBool() )
			Msg( "[rpg] %s test \"%s\": %s %s\n", GetDebugName(), pszExpr, bMet ? "true" : "false", reason.Get() );
		if ( pszExpr == STRING( m_iszCondition ) )
			m_iLastResult = bMet ? 1 : 0;
		if ( bMet )
			m_OnTrue.FireOutput( pActivator ? pActivator : pPlayer, this );
		else
			m_OnFalse.FireOutput( pActivator ? pActivator : pPlayer, this );
	}

	// test_on_spawn - это «слой карты по флагам»: он держится в согласии с персонажем и во время игры.
	// Флаги/предметы изменились - условие проверяется заново, выходы - только когда результат сменился
	// (КПП вокзала: диалог ставит checkpoint_trainstation_passed - барьер на выходе снимается сразу,
	// а не после перезагрузки карты)
	void FlagRetest()
	{
		if ( !m_bTestOnSpawn || m_iszCondition == NULL_STRING || !STRING( m_iszCondition )[0] )
			return;
		CBasePlayer *pPlayer = RPG_GetPlayer();
		if ( !pPlayer )
			return;
		const int iNow = RPG_TestCondition( STRING( m_iszCondition ), pPlayer, NULL ) ? 1 : 0;
		if ( iNow == m_iLastResult )
			return;
		RunTest( STRING( m_iszCondition ), pPlayer );
	}

	// вход = один ключ эффекта (§4)
	void ApplyKey( const char *pszKey, inputdata_t &inputdata, const char *pszDefault = NULL )
	{
		const char *pszValue = inputdata.value.String();
		if ( ( !pszValue || !pszValue[0] ) && pszDefault )
			pszValue = pszDefault;
		RPGEffectList_t list;
		if ( !list.AddKey( pszKey, pszValue ) )
			Warning( "%s: bad %s value '%s'\n", GetDebugName(), pszKey, pszValue ? pszValue : "" );
		list.Apply( ActivatorPlayer( inputdata.pActivator ), NULL, this );
	}

	void InputApplyEffects( inputdata_t &inputdata )
	{
		const char *pszParam = inputdata.value.String();
		const char *pszEffects = ( pszParam && pszParam[0] ) ? pszParam : STRING( m_iszEffects );
		RPGEffectList_t list;
		list.ParseString( pszEffects );
		list.Apply( ActivatorPlayer( inputdata.pActivator ), NULL, this );
	}

	void InputAddXP( inputdata_t &d ) { ApplyKey( "xp", d ); }
	void InputSetFlag( inputdata_t &d ) { ApplyKey( "set_flag", d ); }
	void InputClearFlag( inputdata_t &d ) { ApplyKey( "clear_flag", d ); }
	void InputAddFlag( inputdata_t &d ) { ApplyKey( "add_flag", d ); }
	void InputAddRep( inputdata_t &d ) { ApplyKey( "rep", d ); }
	void InputAddCivil( inputdata_t &d ) { ApplyKey( "civil", d ); }
	void InputAddSuspicion( inputdata_t &d ) { ApplyKey( "suspicion", d ); }
	void InputAddCredits( inputdata_t &d ) { ApplyKey( "credits", d ); }
	void InputAddHumanity( inputdata_t &d ) { ApplyKey( "humanity", d ); }
	void InputAddResonance( inputdata_t &d ) { ApplyKey( "resonance", d ); }
	void InputGiveItem( inputdata_t &d ) { ApplyKey( "give_item", d ); }
	void InputTakeItem( inputdata_t &d ) { ApplyKey( "take_item", d ); }
	void InputGrantQuest( inputdata_t &d ) { ApplyKey( "grant_quest", d ); }
	void InputCompleteQuest( inputdata_t &d ) { ApplyKey( "complete_quest", d ); }
	void InputFailQuest( inputdata_t &d ) { ApplyKey( "fail_quest", d ); }
	void InputSetQuestStage( inputdata_t &d ) { ApplyKey( "set_stage", d ); }
	void InputDiscover( inputdata_t &d ) { ApplyKey( "discover", d ); }
	void InputSetAct( inputdata_t &d ) { ApplyKey( "act", d ); }
	void InputOpenCharGen( inputdata_t &d ) { ApplyKey( "open_chargen", d, "1" ); }
	void InputOpenBarter( inputdata_t &d ) { ApplyKey( "open_barter", d, "1" ); }
	void InputNotify( inputdata_t &d ) { ApplyKey( "notify", d ); }

	void InputQuestEvent( inputdata_t &inputdata )
	{
		// "name" -> event:name; "type:target" как есть
		const char *pszValue = inputdata.value.String();
		if ( !pszValue || !pszValue[0] )
			return;
		const char *pColon = Q_strstr( pszValue, ":" );
		if ( pColon )
		{
			CUtlString type;
			type.SetDirect( pszValue, (int)( pColon - pszValue ) );
			RPG_QuestEvent( ActivatorPlayer( inputdata.pActivator ), type.Get(), pColon + 1 );
		}
		else
			RPG_QuestEvent( ActivatorPlayer( inputdata.pActivator ), "event", pszValue );
	}

	void InputTest( inputdata_t &inputdata )
	{
		RunTest( STRING( m_iszCondition ), inputdata.pActivator );
	}

	// разговор (диалог v2) с энтити по targetname: сцены, «голос в голове», объявления с ответами.
	// Ключ condition, если задан, должен выполняться.
	void InputStartDialog( inputdata_t &inputdata )
	{
		const char *pszName = inputdata.value.String();
		CBaseEntity *pTarget = ( pszName && pszName[0] ) ? gEntList.FindEntityByName( NULL, pszName, this, inputdata.pActivator, inputdata.pCaller ) : NULL;
		CBasePlayer *pPlayer = ActivatorPlayer( inputdata.pActivator );
		if ( !pTarget || !pPlayer )
		{
			Warning( "[rpg] logic_rpg %s: StartDialog - no entity '%s'\n", GetDebugName(), pszName ? pszName : "" );
			return;
		}
		if ( m_iszCondition != NULL_STRING && STRING( m_iszCondition )[0] && !RPG_TestCondition( STRING( m_iszCondition ), pPlayer, pTarget ) )
			return;
		RPGDialog::Start( pPlayer, pTarget );
	}

	void InputTestCondition( inputdata_t &inputdata )
	{
		RunTest( inputdata.value.String(), inputdata.pActivator );
	}

private:
	string_t m_iszEffects;
	string_t m_iszCondition;
	bool m_bTestOnSpawn;
	int m_iLastResult;		// последний результат условия (-1 - ещё не проверялось)
	COutputEvent m_OnTrue;
	COutputEvent m_OnFalse;
};

LINK_ENTITY_TO_CLASS( logic_rpg, CLogicRPG );

// флаги и предметы персонажа изменились -> логика слоёв (test_on_spawn) проверяется в конце кадра
class CRPGLogicLayerWatcher : public CAutoGameSystemPerFrame, public IRPGListener
{
public:
	CRPGLogicLayerWatcher() : CAutoGameSystemPerFrame( "CRPGLogicLayerWatcher" ), m_bDirty( false ) {}
	virtual bool Init() { RPG_AddListener( this ); return true; }
	virtual void LevelInitPostEntity() { m_bDirty = false; }
	virtual void OnRPGFlagChanged( CBasePlayer *pPlayer, const char *pszFlag ) { m_bDirty = true; }
	virtual void OnRPGItemsChanged( CBasePlayer *pPlayer, const char *pszClassname, int iDelta ) { m_bDirty = true; }
	virtual void FrameUpdatePostEntityThink()
	{
		if ( !m_bDirty )
			return;
		m_bDirty = false;
		for ( CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "logic_rpg" ); pEnt; pEnt = gEntList.FindEntityByClassname( pEnt, "logic_rpg" ) )
			static_cast<CLogicRPG *>( pEnt )->FlagRetest();
	}
private:
	bool m_bDirty;
};
static CRPGLogicLayerWatcher g_RPGLogicLayerWatcher;

BEGIN_DATADESC( CLogicRPG )
	DEFINE_KEYFIELD( m_iszEffects, FIELD_STRING, "effects" ),
	DEFINE_KEYFIELD( m_iszCondition, FIELD_STRING, "condition" ),
	DEFINE_KEYFIELD( m_bTestOnSpawn, FIELD_BOOLEAN, "test_on_spawn" ),
	DEFINE_FIELD( m_iLastResult, FIELD_INTEGER ),
	DEFINE_THINKFUNC( SpawnTestThink ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ApplyEffects", InputApplyEffects ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddXP", InputAddXP ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetFlag", InputSetFlag ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ClearFlag", InputClearFlag ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddFlag", InputAddFlag ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddRep", InputAddRep ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddCivil", InputAddCivil ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddSuspicion", InputAddSuspicion ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddCredits", InputAddCredits ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddHumanity", InputAddHumanity ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddResonance", InputAddResonance ),
	DEFINE_INPUTFUNC( FIELD_STRING, "GiveItem", InputGiveItem ),
	DEFINE_INPUTFUNC( FIELD_STRING, "TakeItem", InputTakeItem ),
	DEFINE_INPUTFUNC( FIELD_STRING, "GrantQuest", InputGrantQuest ),
	DEFINE_INPUTFUNC( FIELD_STRING, "CompleteQuest", InputCompleteQuest ),
	DEFINE_INPUTFUNC( FIELD_STRING, "FailQuest", InputFailQuest ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetQuestStage", InputSetQuestStage ),
	DEFINE_INPUTFUNC( FIELD_STRING, "QuestEvent", InputQuestEvent ),
	DEFINE_INPUTFUNC( FIELD_STRING, "Discover", InputDiscover ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetAct", InputSetAct ),
	DEFINE_INPUTFUNC( FIELD_STRING, "OpenCharGen", InputOpenCharGen ),
	DEFINE_INPUTFUNC( FIELD_STRING, "OpenBarter", InputOpenBarter ),
	DEFINE_INPUTFUNC( FIELD_STRING, "Notify", InputNotify ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Test", InputTest ),
	DEFINE_INPUTFUNC( FIELD_STRING, "TestCondition", InputTestCondition ),
	DEFINE_INPUTFUNC( FIELD_STRING, "StartDialog", InputStartDialog ),
	DEFINE_OUTPUT( m_OnTrue, "OnTrue" ),
	DEFINE_OUTPUT( m_OnFalse, "OnFalse" ),
END_DATADESC()

void RPG_RunSpawnTests()
{
	for ( CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "logic_rpg" ); pEnt; pEnt = gEntList.FindEntityByClassname( pEnt, "logic_rpg" ) )
		static_cast<CLogicRPG *>( pEnt )->ScheduleSpawnTest();
}

// ===========================================================================
// logic_rpg_watch
// ===========================================================================
class CLogicRPGWatch : public CLogicalEntity
{
public:
	DECLARE_CLASS( CLogicRPGWatch, CLogicalEntity );
	DECLARE_DATADESC();

	void OnTier( int iOldTier, int iNewTier )
	{
		CBasePlayer *pPlayer = RPG_GetPlayer();
		for ( int t = iOldTier + 1; t <= iNewTier; ++t )
		{
			switch ( t )
			{
			case RPG_STIER_ATTENTION: m_OnSuspicion25.FireOutput( pPlayer, this ); break;
			case RPG_STIER_SEARCH: m_OnSuspicion50.FireOutput( pPlayer, this ); break;
			case RPG_STIER_DETAIN: m_OnSuspicion75.FireOutput( pPlayer, this ); break;
			case RPG_STIER_ALARM: m_OnSuspicion100.FireOutput( pPlayer, this ); break;
			}
		}
		if ( iNewTier == RPG_STIER_CALM && iOldTier > RPG_STIER_CALM )
			m_OnSuspicionCalm.FireOutput( pPlayer, this );
	}

	void InputAddSuspicion( inputdata_t &inputdata )
	{
		CRPGCharacter *pChar = RPG_GetCharacter();
		pChar->AddSuspicion( (float)atof( inputdata.value.String() ), true );
	}

	void InputResetSuspicion( inputdata_t &inputdata )
	{
		RPG_GetCharacter()->SetSuspicion( 0.0f );
	}

private:
	COutputEvent m_OnSuspicion25;
	COutputEvent m_OnSuspicion50;
	COutputEvent m_OnSuspicion75;
	COutputEvent m_OnSuspicion100;
	COutputEvent m_OnSuspicionCalm;
};

LINK_ENTITY_TO_CLASS( logic_rpg_watch, CLogicRPGWatch );

BEGIN_DATADESC( CLogicRPGWatch )
	DEFINE_INPUTFUNC( FIELD_STRING, "AddSuspicion", InputAddSuspicion ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ResetSuspicion", InputResetSuspicion ),
	DEFINE_OUTPUT( m_OnSuspicion25, "OnSuspicion25" ),
	DEFINE_OUTPUT( m_OnSuspicion50, "OnSuspicion50" ),
	DEFINE_OUTPUT( m_OnSuspicion75, "OnSuspicion75" ),
	DEFINE_OUTPUT( m_OnSuspicion100, "OnSuspicion100" ),
	DEFINE_OUTPUT( m_OnSuspicionCalm, "OnSuspicionCalm" ),
END_DATADESC()

void RPG_FireWatchOutputs( int iOldTier, int iNewTier )
{
	for ( CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "logic_rpg_watch" ); pEnt; pEnt = gEntList.FindEntityByClassname( pEnt, "logic_rpg_watch" ) )
		static_cast<CLogicRPGWatch *>( pEnt )->OnTier( iOldTier, iNewTier );
}

// ===========================================================================
// trigger_rpg
// ===========================================================================
class CTriggerRPG : public CBaseTrigger
{
public:
	DECLARE_CLASS( CTriggerRPG, CBaseTrigger );
	DECLARE_DATADESC();

	CTriggerRPG() : m_bOnce( false ), m_bPassed( false ) {}

	void Spawn()
	{
		BaseClass::Spawn();
		AddSpawnFlags( SF_TRIGGER_ALLOW_CLIENTS );
		InitTrigger();
	}

	void StartTouch( CBaseEntity *pOther )
	{
		BaseClass::StartTouch( pOther );
		if ( m_bDisabled || !pOther || !pOther->IsPlayer() || !PassesTriggerFilters( pOther ) )
			return;
		if ( m_bOnce && m_bPassed )
			return;
		CBasePlayer *pPlayer = ToBasePlayer( pOther );

		CUtlString reason;
		if ( m_iszCondition != NULL_STRING && !RPG_TestCondition( STRING( m_iszCondition ), pPlayer, NULL, &reason ) )
		{
			if ( rpg_debug.GetBool() )
				Msg( "[rpg] %s: condition failed: %s\n", GetDebugName(), reason.Get() );
			m_OnFail.FireOutput( pPlayer, this );
			return;
		}

		m_bPassed = true;
		if ( m_iszEffects != NULL_STRING )
			RPG_ApplyEffects( STRING( m_iszEffects ), pPlayer, NULL, this );
		if ( m_iszDiscover != NULL_STRING )
			RPG_GetCharacter()->Discover( STRING( m_iszDiscover ) );
		if ( m_iszQuestEvent != NULL_STRING )
		{
			// "name" -> reach:name; "type:target" как есть
			const char *pszEvent = STRING( m_iszQuestEvent );
			const char *pColon = Q_strstr( pszEvent, ":" );
			if ( pColon )
			{
				CUtlString type;
				type.SetDirect( pszEvent, (int)( pColon - pszEvent ) );
				RPG_QuestEvent( pPlayer, type.Get(), pColon + 1 );
			}
			else
				RPG_QuestEvent( pPlayer, "reach", pszEvent );
		}
		m_OnPass.FireOutput( pPlayer, this );
		if ( m_bOnce )
			Disable();
	}

private:
	string_t m_iszCondition;
	string_t m_iszEffects;
	string_t m_iszQuestEvent;
	string_t m_iszDiscover;
	bool m_bOnce;
	bool m_bPassed;
	COutputEvent m_OnPass;
	COutputEvent m_OnFail;
};

LINK_ENTITY_TO_CLASS( trigger_rpg, CTriggerRPG );

BEGIN_DATADESC( CTriggerRPG )
	DEFINE_KEYFIELD( m_iszCondition, FIELD_STRING, "condition" ),
	DEFINE_KEYFIELD( m_iszEffects, FIELD_STRING, "effects" ),
	DEFINE_KEYFIELD( m_iszQuestEvent, FIELD_STRING, "quest_event" ),
	DEFINE_KEYFIELD( m_iszDiscover, FIELD_STRING, "discover" ),
	DEFINE_KEYFIELD( m_bOnce, FIELD_BOOLEAN, "once" ),
	DEFINE_FIELD( m_bPassed, FIELD_BOOLEAN ),
	DEFINE_OUTPUT( m_OnPass, "OnPass" ),
	DEFINE_OUTPUT( m_OnFail, "OnFail" ),
END_DATADESC()

// ===========================================================================
// trigger_rpg_zone
// ===========================================================================
class CTriggerRPGZone;
static CUtlVector<CTriggerRPGZone *> s_RPGZones;

class CTriggerRPGZone : public CBaseTrigger
{
public:
	DECLARE_CLASS( CTriggerRPGZone, CBaseTrigger );
	DECLARE_DATADESC();

	CTriggerRPGZone() : m_iZone( RPG_ZONE_CONTROL ) { s_RPGZones.AddToTail( this ); }
	~CTriggerRPGZone() { s_RPGZones.FindAndRemove( this ); }

	void Spawn()
	{
		BaseClass::Spawn();
		AddSpawnFlags( SF_TRIGGER_ALLOW_CLIENTS );
		m_iZone = clamp( m_iZone, (int)RPG_ZONE_CONTROL, (int)RPG_ZONE_RESTRICTED );
		InitTrigger();
	}

	int GetZoneFor( CBasePlayer *pPlayer )
	{
		if ( m_bDisabled || !pPlayer )
			return RPG_ZONE_NONE;
		return IsTouching( pPlayer ) ? m_iZone : RPG_ZONE_NONE;
	}

	void InputSetZone( inputdata_t &inputdata )
	{
		m_iZone = clamp( inputdata.value.Int(), (int)RPG_ZONE_CONTROL, (int)RPG_ZONE_RESTRICTED );
	}

private:
	int m_iZone;
};

LINK_ENTITY_TO_CLASS( trigger_rpg_zone, CTriggerRPGZone );

BEGIN_DATADESC( CTriggerRPGZone )
	DEFINE_KEYFIELD( m_iZone, FIELD_INTEGER, "zone" ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetZone", InputSetZone ),
END_DATADESC()

int RPG_GetPlayerZone( CBasePlayer *pPlayer )
{
	int zone = RPG_ZONE_NONE;
	for ( int i = 0; i < s_RPGZones.Count(); ++i )
		zone = Max( zone, s_RPGZones[i]->GetZoneFor( pPlayer ) );
	return zone;
}

// ===========================================================================
// Тревога: ГО враждебны до сброса
// ===========================================================================
void RPG_ApplyAlarm( bool bOn )
{
	CBasePlayer *pPlayer = RPG_GetPlayer();
	if ( !pPlayer )
		return;
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); ++i )
	{
		CAI_BaseNPC *pNPC = ppAIs[i];
		if ( !pNPC || !pNPC->IsAlive() )
			continue;
		const char *pszFaction = pNPC->GetRPGFaction();
		if ( !pszFaction[0] || ( Q_stricmp( pszFaction, "cp" ) && Q_stricmp( pszFaction, "overwatch" ) ) )
			continue;
		if ( bOn )
		{
			if ( pNPC->m_iRPGAlarmPrevDisp >= 0 )
				continue;
			pNPC->m_iRPGAlarmPrevDisp = (int)pNPC->IRelationType( pPlayer );
			pNPC->m_iRPGAlarmPrevPriority = pNPC->IRelationPriority( pPlayer );
			// gordon_precriminal (город) делает ГО к игроку нейтральной при любом отношении
			// (CNPC_MetroPolice::IRelationType): на время тревоги - снять его для юнита
			pNPC->m_iRPGAlarmPrevGpc = pNPC->GetGordonPrecriminalOverride();
			pNPC->SetGordonPrecriminalOverride( 2 );
			pNPC->AddEntityRelationship( pPlayer, D_HT, 99 );
			// юниты поблизости знают, где нарушитель (подкрепление идёт на голос Надзора)
			if ( pNPC->GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() ) < 2000.0f * 2000.0f )
				pNPC->UpdateEnemyMemory( pPlayer, pPlayer->GetAbsOrigin() );
		}
		else
		{
			if ( pNPC->m_iRPGAlarmPrevDisp < 0 )
				continue;
			const Disposition_t prev = (Disposition_t)pNPC->m_iRPGAlarmPrevDisp;
			pNPC->AddEntityRelationship( pPlayer, prev, pNPC->m_iRPGAlarmPrevPriority );
			pNPC->m_iRPGAlarmPrevDisp = -1;
			if ( pNPC->m_iRPGAlarmPrevGpc >= 0 )
				pNPC->SetGordonPrecriminalOverride( pNPC->m_iRPGAlarmPrevGpc );
			pNPC->m_iRPGAlarmPrevGpc = -1;
			if ( prev != D_HT && pNPC->GetEnemy() == pPlayer )
			{
				pNPC->SetEnemy( NULL );
				if ( pNPC->GetEnemies() )
					pNPC->GetEnemies()->ClearMemory( pPlayer );
			}
		}
	}
}

// ===========================================================================
// prop_rpg_usable
// ===========================================================================
static EHANDLE s_hSleepBed;
static float s_flSleepRequestTime = -1.0f;

class CPropRPGUsable : public CDynamicProp
{
public:
	DECLARE_CLASS( CPropRPGUsable, CDynamicProp );
	DECLARE_DATADESC();

	CPropRPGUsable() : m_bOnce( false ), m_bUsed( false ), m_bUseDisabled( false ), m_bHome( false ), m_flSleepHours( 8.0f ) {}

	virtual int ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; }
	// записка, кровать, остановка - не ломаются и не падают, даже если у модели есть propdata
	// (иначе Mapbase на каждой загрузке пишет «should be used on a prop_physics»)
	virtual bool OverridePropdata() { return true; }

	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
	{
		CBasePlayer *pPlayer = ToBasePlayer( pActivator );
		if ( !pPlayer )
		{
			BaseClass::Use( pActivator, pCaller, useType, value );
			return;
		}
		if ( m_bUseDisabled || ( m_bOnce && m_bUsed ) )
			return;

		CUtlString reason;
		if ( m_iszCondition != NULL_STRING && !RPG_TestCondition( STRING( m_iszCondition ), pPlayer, NULL, &reason ) )
		{
			RPG_GetCharacter()->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_Condition", 0, reason.Get() );
			m_OnUseDenied.FireOutput( pPlayer, this );
			return;
		}

		const char *pszAction = STRING( m_iszUseAction );
		if ( !Q_stricmp( pszAction, "note" ) || !Q_stricmp( pszAction, "terminal" ) )
		{
			const char *pszNote = m_iszNote != NULL_STRING ? STRING( m_iszNote ) : STRING( GetEntityName() );
			if ( !Q_stricmp( pszAction, "note" ) )
				RPG_GetCharacter()->AddNote( pszNote );
			RPG_OpenUI( pPlayer, pszAction, pszNote, this );
		}
		else if ( !Q_stricmp( pszAction, "travel" ) )
		{
			// остановка трамвая: экран маршрута -> rpg_travel <локация>
			RPG_OpenUI( pPlayer, "travel", STRING( GetEntityName() ), this );
			m_OnUsed.FireOutput( pPlayer, this );
			return;
		}
		else if ( !Q_stricmp( pszAction, "sleep" ) )
		{
			// часы выбирает интерфейс -> rpg_sleep <часы>; эффекты - после сна
			s_hSleepBed = this;
			s_flSleepRequestTime = gpGlobals->curtime;
			RPG_OpenUI( pPlayer, "sleep", CFmtStr( "%d", (int)m_flSleepHours ), this );
			m_OnUsed.FireOutput( pPlayer, this );
			return;
		}

		if ( m_iszEffects != NULL_STRING )
			RPG_ApplyEffects( STRING( m_iszEffects ), pPlayer, NULL, this );
		m_bUsed = true;
		m_OnUsed.FireOutput( pPlayer, this );
	}

	bool Sleep( CBasePlayer *pPlayer, float flHours )
	{
		flHours = clamp( flHours, 1.0f, 24.0f );
		CRPGCharacter *pChar = RPG_GetCharacter();

		color32 black = { 0, 0, 0, 255 };
		UTIL_ScreenFade( pPlayer, black, 1.5f, 1.0f, FFADE_IN | FFADE_PURGE );

		RPG_AdvanceClock( flHours );
		pChar->SetSuspicion( 0.0f );
		if ( m_bHome )
			pPlayer->SetHealth( pPlayer->GetMaxHealth() );	// своя кровать: полное лечение (без травм)
		if ( m_iszEffects != NULL_STRING )
			RPG_ApplyEffects( STRING( m_iszEffects ), pPlayer, NULL, this );

		// проснулся в комендантский час - предупреждение
		if ( RPG_TestCondition( "time curfew", pPlayer ) )
			pChar->Notify( RPG_NOTIFY_GENERIC, "#HL2RPM_UI_CurfewWake", 0 );

		m_bUsed = true;
		m_OnSlept.FireOutput( pPlayer, this );
		if ( rpg_debug.GetBool() )
			Msg( "[rpg] slept %.0f h on %s (home %d)\n", flHours, GetDebugName(), m_bHome ? 1 : 0 );
		return true;
	}

	void InputEnableUse( inputdata_t &inputdata ) { m_bUseDisabled = false; }
	void InputDisableUse( inputdata_t &inputdata ) { m_bUseDisabled = true; }

	const char *GetHintName() const { return STRING( m_iszHint ); }
	const char *GetUseAction() const { return STRING( m_iszUseAction ); }
	bool IsUsableNow() const { return !m_bUseDisabled && !( m_bOnce && m_bUsed ); }

private:
	string_t m_iszUseAction;
	string_t m_iszNote;
	string_t m_iszCondition;
	string_t m_iszEffects;
	string_t m_iszHint;
	bool m_bOnce;
	bool m_bUsed;
	bool m_bUseDisabled;
	bool m_bHome;
	float m_flSleepHours;
	COutputEvent m_OnUsed;
	COutputEvent m_OnUseDenied;
	COutputEvent m_OnSlept;
};

LINK_ENTITY_TO_CLASS( prop_rpg_usable, CPropRPGUsable );

BEGIN_DATADESC( CPropRPGUsable )
	DEFINE_KEYFIELD( m_iszUseAction, FIELD_STRING, "use_action" ),
	DEFINE_KEYFIELD( m_iszNote, FIELD_STRING, "note" ),
	DEFINE_KEYFIELD( m_iszCondition, FIELD_STRING, "condition" ),
	DEFINE_KEYFIELD( m_iszEffects, FIELD_STRING, "effects" ),
	DEFINE_KEYFIELD( m_iszHint, FIELD_STRING, "name" ),
	DEFINE_KEYFIELD( m_bOnce, FIELD_BOOLEAN, "once" ),
	DEFINE_KEYFIELD( m_bHome, FIELD_BOOLEAN, "home" ),
	DEFINE_KEYFIELD( m_flSleepHours, FIELD_FLOAT, "sleep_hours" ),
	DEFINE_KEYFIELD( m_bUseDisabled, FIELD_BOOLEAN, "use_disabled" ),
	DEFINE_FIELD( m_bUsed, FIELD_BOOLEAN ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableUse", InputEnableUse ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableUse", InputDisableUse ),
	DEFINE_OUTPUT( m_OnUsed, "OnUsed" ),
	DEFINE_OUTPUT( m_OnUseDenied, "OnUseDenied" ),
	DEFINE_OUTPUT( m_OnSlept, "OnSlept" ),
END_DATADESC()

bool RPG_Sleep( CBasePlayer *pPlayer, float flHours, CUtlString *pError )
{
	CPropRPGUsable *pBed = dynamic_cast<CPropRPGUsable *>( s_hSleepBed.Get() );
	if ( !pPlayer || !pBed || s_flSleepRequestTime < 0.0f || gpGlobals->curtime - s_flSleepRequestTime > 120.0f )
	{
		if ( pError ) *pError = "no bed";
		return false;
	}
	if ( pBed->WorldSpaceCenter().DistToSqr( pPlayer->WorldSpaceCenter() ) > 256.0f * 256.0f )
	{
		if ( pError ) *pError = "too far from the bed";
		return false;
	}
	s_hSleepBed = NULL;
	s_flSleepRequestTime = -1.0f;
	return pBed->Sleep( pPlayer, flHours );
}

// подсказка у прицела (rpg_usehint.cpp)
bool RPG_GetUsableHint( CBaseEntity *pEnt, const char *&pszAction, const char *&pszName )
{
	CPropRPGUsable *pUsable = dynamic_cast<CPropRPGUsable *>( pEnt );
	if ( !pUsable || !pUsable->IsUsableNow() )
		return false;
	pszAction = pUsable->GetUseAction();
	pszName = pUsable->GetHintName();
	return true;
}

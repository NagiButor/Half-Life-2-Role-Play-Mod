//========= HL2RPM ============//
//
// RPG-система (сервер): квесты v2 - выдача, стадии, цели, награды, метка на компасе.
// См. rpg_quests.h и RPG_SPEC §6.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_quests.h"
#include "hl2rpm/rpg_character.h"
#include "hl2_player.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpg_debug;
// triggers.cpp: карта перехода trigger_changelevel
const char *ChangeLevel_GetMapName( CBaseEntity *pEnt );

static ConVar rpg_quest_marker_update( "rpg_quest_marker_update", "0.5", FCVAR_GAMEDLL, "RPG: seconds between quest marker updates" );

// ---------------------------------------------------------------------------
// Флаги состояния
// ---------------------------------------------------------------------------
static CRPGCharacter *Char()
{
	return RPG_GetCharacter();
}

static void SetQFlag( const char *pszId, const char *pszSuffix, int value )
{
	char sz[128];
	RPGQuest_FlagName( sz, sizeof( sz ), pszId, pszSuffix );
	Char()->SetFlag( sz, value );
}

static void ClearQFlag( const char *pszId, const char *pszSuffix )
{
	char sz[128];
	RPGQuest_FlagName( sz, sizeof( sz ), pszId, pszSuffix );
	if ( Char()->Data().FindFlag( sz ) )
		Char()->ClearFlag( sz );
}

static void ClearStageProgress( const RPGQuestDef_t &def, int iStage )
{
	if ( iStage < 0 || iStage >= def.stages.Count() )
		return;
	for ( int i = 0; i < def.stages[iStage].objectives.Count(); ++i )
		ClearQFlag( def.id.Get(), CFmtStr( "o%d", i ) );
}

static bool s_bInQuestUpdate = false;	// флаги q_* меняются внутри - не реагировать на себя

// ---------------------------------------------------------------------------
// Цели
// ---------------------------------------------------------------------------
static bool TargetMatches( const RPGQuestObjective_t &o, const char *pszName )
{
	if ( o.targets.Count() == 0 )
		return true;	// без цели - любое событие этого типа
	if ( !pszName || !pszName[0] )
		return false;
	for ( int i = 0; i < o.targets.Count(); ++i )
	{
		const char *t = o.targets[i].Get();
		const int len = Q_strlen( t );
		if ( len && t[len - 1] == '*' )
		{
			if ( !Q_strnicmp( t, pszName, len - 1 ) )
				return true;
		}
		else if ( !Q_stricmp( t, pszName ) )
			return true;
	}
	return false;
}

static bool IsObjectiveDone( CBasePlayer *pPlayer, const RPGQuestDef_t &def, const RPGQuestObjective_t &o, int iObj )
{
	const CRPGCharacterData &d = Char()->Data();
	switch ( o.type )
	{
	case RPG_OBJ_FLAG:
	{
		RPGEvalResult_t r = o.cond.Evaluate( RPG_MakeEvalContext( pPlayer ) );
		return r.met;
	}
	case RPG_OBJ_ITEM:
	{
		int have = 0;
		for ( int i = 0; i < o.targets.Count(); ++i )
			have += RPG_CountItem( pPlayer, o.targets[i].Get() );
		return have >= o.count;
	}
	default:
		return RPGQuest_GetProgress( d, def.id.Get(), iObj ) >= o.count;
	}
}

static void CompleteInternal( CBasePlayer *pPlayer, const RPGQuestDef_t &def );
static void SetStageInternal( CBasePlayer *pPlayer, const RPGQuestDef_t &def, int iStage, bool bNotify );

// все обязательные цели текущей стадии выполнены - стадия закрывается
static void EvaluateQuest( CBasePlayer *pPlayer, const RPGQuestDef_t &def )
{
	const CRPGCharacterData &d = Char()->Data();
	if ( RPGQuest_GetState( d, def.id.Get() ) != RPG_QUEST_ACTIVE )
		return;
	const int iStage = RPGQuest_GetStage( d, def.id.Get() );
	if ( iStage < 0 || iStage >= def.stages.Count() )
		return;
	const RPGQuestStage_t &st = def.stages[iStage];
	int nRequired = 0;
	for ( int i = 0; i < st.objectives.Count(); ++i )
	{
		if ( st.objectives[i].optional )
			continue;
		++nRequired;
		if ( !IsObjectiveDone( pPlayer, def, st.objectives[i], i ) )
			return;
	}
	if ( nRequired == 0 )
		return;		// стадию закрывает только эффект set_stage / complete_quest

	// стадия выполнена
	if ( st.xp > 0 )
		Char()->AddXP( st.xp, "quest" );
	st.onComplete.Apply( pPlayer );
	if ( RPGQuest_GetState( Char()->Data(), def.id.Get() ) != RPG_QUEST_ACTIVE )
		return;		// эффекты уже завершили/провалили квест
	if ( RPGQuest_GetStage( Char()->Data(), def.id.Get() ) != iStage )
		return;		// эффекты сами перевели стадию

	if ( iStage + 1 < def.stages.Count() )
		SetStageInternal( pPlayer, def, iStage + 1, true );
	else if ( def.completeOnLastStage )
		CompleteInternal( pPlayer, def );
}

static void SetStageInternal( CBasePlayer *pPlayer, const RPGQuestDef_t &def, int iStage, bool bNotify )
{
	s_bInQuestUpdate = true;
	const int iOld = RPGQuest_GetStage( Char()->Data(), def.id.Get() );
	ClearStageProgress( def, iOld );
	SetQFlag( def.id.Get(), "s", iStage );
	ClearStageProgress( def, iStage );
	s_bInQuestUpdate = false;
	if ( bNotify )
		Char()->Notify( RPG_NOTIFY_QUEST, def.id.Get(), iStage, "stage" );
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] quest %s: stage %d\n", def.id.Get(), iStage );
	// цели «флаг»/«предмет» могут быть уже выполнены
	EvaluateQuest( pPlayer, def );
}

// отслеживать первый активный квест: главный, затем любой
static void RetrackAfter( const char *pszFinishedId )
{
	const CRPGCharacterData &d = Char()->Data();
	if ( Q_stricmp( RPGQuest_GetTracked( d ), pszFinishedId ) )
		return;
	const char *pszBest = "";
	for ( int pass = 0; pass < 2 && !pszBest[0]; ++pass )
	{
		for ( int i = 0; i < RPGQuestData().Count(); ++i )
		{
			const RPGQuestDef_t &q = RPGQuestData().Get( i );
			if ( pass == 0 && q.category != RPG_QCAT_MAIN )
				continue;
			if ( RPGQuest_GetState( d, q.id.Get() ) == RPG_QUEST_ACTIVE )
			{
				pszBest = q.id.Get();
				break;
			}
		}
	}
	Char()->SetFlagString( RPG_QFLAG_TRACK, pszBest );
}

static void CompleteInternal( CBasePlayer *pPlayer, const RPGQuestDef_t &def )
{
	s_bInQuestUpdate = true;
	const int iStage = RPGQuest_GetStage( Char()->Data(), def.id.Get() );
	ClearStageProgress( def, iStage );
	SetQFlag( def.id.Get(), NULL, RPG_QUEST_COMPLETED );
	s_bInQuestUpdate = false;

	Char()->Notify( RPG_NOTIFY_QUEST, def.id.Get(), iStage, "completed" );
	for ( int i = 0; i < def.rewards.Count(); ++i )
		RPG_GiveItem( pPlayer, def.rewards[i].classname.Get(), def.rewards[i].count );
	if ( def.xp > 0 )
		Char()->AddXP( def.xp, "quest" );
	def.onComplete.Apply( pPlayer );
	RetrackAfter( def.id.Get() );
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] quest %s completed\n", def.id.Get() );
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
namespace RPGQuests
{
	bool IsV2( const char *pszId )
	{
		RPGQuestData().EnsureLoaded();
		return RPGQuestData().Find( pszId ) != NULL;
	}

	int GetState( const char *pszId )
	{
		return RPGQuest_GetState( Char()->Data(), pszId );
	}

	int GetStage( const char *pszId )
	{
		return RPGQuest_GetStage( Char()->Data(), pszId );
	}

	bool Grant( CBasePlayer *pPlayer, const char *pszId )
	{
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszId );
		if ( !pDef )
			return false;
		const int state = GetState( pszId );
		if ( state == RPG_QUEST_ACTIVE || state == RPG_QUEST_COMPLETED )
			return false;

		s_bInQuestUpdate = true;
		SetQFlag( pszId, NULL, RPG_QUEST_ACTIVE );
		SetQFlag( pszId, "s", 0 );
		ClearStageProgress( *pDef, 0 );
		s_bInQuestUpdate = false;

		Char()->Notify( RPG_NOTIFY_QUEST, pszId, 0, "started" );
		const char *pszTracked = RPGQuest_GetTracked( Char()->Data() );
		const RPGQuestDef_t *pTracked = RPGQuestData().Find( pszTracked );
		const bool bTrackedActive = pTracked && GetState( pszTracked ) == RPG_QUEST_ACTIVE;
		if ( !bTrackedActive || ( pDef->autoTrack && pDef->category == RPG_QCAT_MAIN && pTracked->category != RPG_QCAT_MAIN ) || ( pDef->autoTrack && !pszTracked[0] ) )
			Char()->SetFlagString( RPG_QFLAG_TRACK, pszId );
		if ( rpg_debug.GetBool() )
			Msg( "[rpg] quest %s granted\n", pszId );

		pDef->onGrant.Apply( pPlayer );
		EvaluateQuest( pPlayer, *pDef );
		return true;
	}

	bool Complete( CBasePlayer *pPlayer, const char *pszId )
	{
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszId );
		if ( !pDef )
			return false;
		const int state = GetState( pszId );
		if ( state == RPG_QUEST_COMPLETED )
			return false;
		if ( state != RPG_QUEST_ACTIVE )
		{
			// выполнен, не будучи выданным (решено заранее) - выдаём молча и сразу закрываем
			s_bInQuestUpdate = true;
			SetQFlag( pszId, NULL, RPG_QUEST_ACTIVE );
			SetQFlag( pszId, "s", Max( 0, pDef->stages.Count() - 1 ) );
			s_bInQuestUpdate = false;
		}
		CompleteInternal( pPlayer, *pDef );
		return true;
	}

	bool Fail( CBasePlayer *pPlayer, const char *pszId )
	{
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszId );
		if ( !pDef )
			return false;
		const int state = GetState( pszId );
		if ( state == RPG_QUEST_COMPLETED || state == RPG_QUEST_FAILED )
			return false;
		s_bInQuestUpdate = true;
		ClearStageProgress( *pDef, GetStage( pszId ) );
		SetQFlag( pszId, NULL, RPG_QUEST_FAILED );
		s_bInQuestUpdate = false;
		Char()->Notify( RPG_NOTIFY_QUEST, pszId, GetStage( pszId ), "failed" );
		pDef->onFail.Apply( pPlayer );
		RetrackAfter( pszId );
		return true;
	}

	bool SetStage( CBasePlayer *pPlayer, const char *pszId, int iStage )
	{
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszId );
		if ( !pDef || iStage < 0 )
			return false;
		if ( GetState( pszId ) != RPG_QUEST_ACTIVE )
		{
			if ( GetState( pszId ) == RPG_QUEST_COMPLETED )
				return false;
			Grant( pPlayer, pszId );
		}
		if ( iStage >= pDef->stages.Count() )
		{
			CompleteInternal( pPlayer, *pDef );
			return true;
		}
		if ( GetStage( pszId ) == iStage )
			return true;
		SetStageInternal( pPlayer, *pDef, iStage, true );
		return true;
	}

	bool Track( CBasePlayer *pPlayer, const char *pszId )
	{
		if ( pszId && pszId[0] && ( !RPGQuestData().Find( pszId ) || GetState( pszId ) != RPG_QUEST_ACTIVE ) )
			return false;
		Char()->SetFlagString( RPG_QFLAG_TRACK, pszId ? pszId : "" );
		return true;
	}

	void Reload()
	{
		RPGQuestData().Load();
	}
}

// ---------------------------------------------------------------------------
// События RPG -> цели
// ---------------------------------------------------------------------------
class CRPGQuestListener : public IRPGListener, public CAutoGameSystemPerFrame
{
public:
	CRPGQuestListener() : CAutoGameSystemPerFrame( "CRPGQuestListener" ), m_flNextMarker( 0.0f ) {}

	virtual bool Init()
	{
		RPG_AddListener( this );
		return true;
	}

	virtual void Shutdown()
	{
		RPG_RemoveListener( this );
	}

	virtual void LevelInitPreEntity()
	{
		RPGQuestData().Load();
		m_flNextMarker = 0.0f;
	}

	// счётчик цели +1 у всех активных квестов, текущая стадия которых ждёт этого события
	void Bump( CBasePlayer *pPlayer, int type, const char *pszTarget, const char *pszTarget2 = NULL )
	{
		const CRPGCharacterData &d = Char()->Data();
		for ( int q = 0; q < RPGQuestData().Count(); ++q )
		{
			const RPGQuestDef_t &def = RPGQuestData().Get( q );
			if ( RPGQuest_GetState( d, def.id.Get() ) != RPG_QUEST_ACTIVE )
				continue;
			const int iStage = RPGQuest_GetStage( d, def.id.Get() );
			if ( iStage < 0 || iStage >= def.stages.Count() )
				continue;
			const RPGQuestStage_t &st = def.stages[iStage];
			bool bChanged = false;
			for ( int i = 0; i < st.objectives.Count(); ++i )
			{
				const RPGQuestObjective_t &o = st.objectives[i];
				if ( o.type != type )
					continue;
				if ( !TargetMatches( o, pszTarget ) && !( pszTarget2 && TargetMatches( o, pszTarget2 ) ) )
					continue;
				const int cur = RPGQuest_GetProgress( d, def.id.Get(), i );
				if ( cur >= o.count )
					continue;
				s_bInQuestUpdate = true;
				SetQFlag( def.id.Get(), CFmtStr( "o%d", i ), cur + 1 );
				s_bInQuestUpdate = false;
				if ( o.count > 1 )
					Char()->Notify( RPG_NOTIFY_QUEST, def.id.Get(), cur + 1, CFmtStr( "progress %d %d %d", i, cur + 1, o.count ) );
				bChanged = true;
			}
			if ( bChanged )
				EvaluateQuest( pPlayer, def );
		}
	}

	void EvaluateAll( CBasePlayer *pPlayer, int typeFilter )
	{
		const CRPGCharacterData &d = Char()->Data();
		for ( int q = 0; q < RPGQuestData().Count(); ++q )
		{
			const RPGQuestDef_t &def = RPGQuestData().Get( q );
			if ( RPGQuest_GetState( d, def.id.Get() ) != RPG_QUEST_ACTIVE )
				continue;
			const int iStage = RPGQuest_GetStage( d, def.id.Get() );
			if ( iStage < 0 || iStage >= def.stages.Count() )
				continue;
			bool bHas = false;
			for ( int i = 0; i < def.stages[iStage].objectives.Count() && !bHas; ++i )
				bHas = def.stages[iStage].objectives[i].type == typeFilter;
			if ( bHas )
				EvaluateQuest( pPlayer, def );
		}
	}

	virtual void OnRPGQuestEvent( CBasePlayer *pPlayer, const char *pszType, const char *pszTarget )
	{
		const int type = CRPGQuestData::ObjectiveTypeFromKey( pszType );
		if ( type == RPG_OBJ_TALK || type == RPG_OBJ_REACH || type == RPG_OBJ_EVENT )
			Bump( pPlayer, type, pszTarget );
	}

	virtual void OnRPGFlagChanged( CBasePlayer *pPlayer, const char *pszFlag )
	{
		if ( s_bInQuestUpdate || !pszFlag || !Q_strnicmp( pszFlag, "q_", 2 ) )
			return;
		s_bInQuestUpdate = true;		// эффекты внутри не должны снова звать нас по кругу
		s_bInQuestUpdate = false;
		EvaluateAll( pPlayer, RPG_OBJ_FLAG );
	}

	virtual void OnRPGItemsChanged( CBasePlayer *pPlayer, const char *pszClassname, int iDelta )
	{
		EvaluateAll( pPlayer, RPG_OBJ_ITEM );
	}

	virtual void OnRPGKill( CBasePlayer *pPlayer, CBaseEntity *pVictim )
	{
		if ( !pVictim )
			return;
		Bump( pPlayer, RPG_OBJ_KILL, STRING( pVictim->GetEntityName() ), pVictim->GetClassname() );
	}

	// ---- метка отслеживаемого квеста: локатор HL2 (CHL2_Player::m_hLocatorTargetEntity) ----
	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNextMarker )
			return;
		m_flNextMarker = gpGlobals->curtime + rpg_quest_marker_update.GetFloat();

		CHL2_Player *pPlayer = dynamic_cast<CHL2_Player *>( RPG_GetPlayer() );
		if ( !pPlayer )
			return;
		CBaseEntity *pTarget = NULL;
		const CRPGCharacterData &d = Char()->Data();
		const char *pszTracked = RPGQuest_GetTracked( d );
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszTracked );
		if ( pDef && RPGQuest_GetState( d, pszTracked ) == RPG_QUEST_ACTIVE )
		{
			const int iStage = RPGQuest_GetStage( d, pszTracked );
			if ( iStage >= 0 && iStage < pDef->stages.Count() )
			{
				const RPGQuestStage_t &st = pDef->stages[iStage];
				const char *pszMap = STRING( gpGlobals->mapname );
				if ( st.markerMap.IsEmpty() || !Q_stricmp( st.markerMap.Get(), pszMap ) )
				{
					if ( st.marker.Length() )
						pTarget = gEntList.FindEntityByName( NULL, st.marker.Get() );
				}
				else
				{
					// метка на другой карте: ближайший переход туда
					for ( CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "trigger_changelevel" ); pEnt; pEnt = gEntList.FindEntityByClassname( pEnt, "trigger_changelevel" ) )
					{
						const char *pszTo = ChangeLevel_GetMapName( pEnt );
						if ( pszTo && !Q_stricmp( pszTo, st.markerMap.Get() ) )
						{
							pTarget = pEnt;
							break;
						}
					}
				}
			}
		}
		if ( pPlayer->GetLocatorTargetEntity() != pTarget )
			pPlayer->SetLocatorTargetEntity( pTarget );
	}

private:
	float m_flNextMarker;
};
static CRPGQuestListener g_RPGQuestListener;

// ---------------------------------------------------------------------------
// Команды
// ---------------------------------------------------------------------------
static CBasePlayer *CmdPlayer()
{
	CBasePlayer *p = UTIL_GetCommandClient();
	return p ? p : UTIL_PlayerByIndex( 1 );
}

CON_COMMAND_F( rpg_quest_grant, "RPG: grant a v2 quest: rpg_quest_grant <id>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() >= 2 && !RPGQuests::Grant( CmdPlayer(), args[1] ) )
		Msg( "[rpg] can't grant quest '%s'\n", args[1] );
}

CON_COMMAND_F( rpg_quest_complete, "RPG: complete a v2 quest: rpg_quest_complete <id>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() >= 2 && !RPGQuests::Complete( CmdPlayer(), args[1] ) )
		Msg( "[rpg] can't complete quest '%s'\n", args[1] );
}

CON_COMMAND_F( rpg_quest_fail, "RPG: fail a v2 quest: rpg_quest_fail <id>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() >= 2 && !RPGQuests::Fail( CmdPlayer(), args[1] ) )
		Msg( "[rpg] can't fail quest '%s'\n", args[1] );
}

CON_COMMAND_F( rpg_quest_stage, "RPG: set a v2 quest stage: rpg_quest_stage <id> <n>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() >= 3 && !RPGQuests::SetStage( CmdPlayer(), args[1], atoi( args[2] ) ) )
		Msg( "[rpg] can't set stage of '%s'\n", args[1] );
}

// без cheat: журнал браслета шлёт «отслеживать»
CON_COMMAND_F( rpg_quest_track, "RPG: track an active quest on the compass: rpg_quest_track <id|\"\">", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	RPGQuests::Track( CmdPlayer(), args.ArgC() >= 2 ? args[1] : "" );
}

CON_COMMAND_F( rpg_quest_reload, "RPG: reload scripts/quests", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	RPGQuests::Reload();
}

CON_COMMAND_F( rpg_quest_dump, "RPG: print v2 quests and their state", FCVAR_GAMEDLL )
{
	const CRPGCharacterData &d = Char()->Data();
	Msg( "[rpg] %d quest definition(s), tracked '%s'\n", RPGQuestData().Count(), RPGQuest_GetTracked( d ) );
	static const char *s_States[] = { "inactive", "ACTIVE", "completed", "failed" };
	for ( int i = 0; i < RPGQuestData().Count(); ++i )
	{
		const RPGQuestDef_t &q = RPGQuestData().Get( i );
		const int st = RPGQuest_GetState( d, q.id.Get() );
		CUtlString prog;
		const int iStage = RPGQuest_GetStage( d, q.id.Get() );
		if ( st == RPG_QUEST_ACTIVE && iStage >= 0 && iStage < q.stages.Count() )
		{
			for ( int o = 0; o < q.stages[iStage].objectives.Count(); ++o )
				prog.Append( CFmtStr( " [%d %s %d/%d]", o, q.stages[iStage].objectives[o].target.Get(), RPGQuest_GetProgress( d, q.id.Get(), o ), q.stages[iStage].objectives[o].count ) );
		}
		Msg( "[rpg]   %-22s %-9s %-9s stage %d/%d%s\n", q.id.Get(), CRPGQuestData::CategoryKey( q.category ), s_States[clamp( st, 0, 3 )], iStage, q.stages.Count(), prog.Get() );
	}
}

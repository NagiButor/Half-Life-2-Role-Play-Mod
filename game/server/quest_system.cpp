#include "cbase.h"
#include "dialog_definitions.h"
#include "quest_system.h"
#include "isaverestore.h"

#include "inventory_system.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_world.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

static CQuestSystem g_QuestSystem;
ConVar quest_reward_debug( "quest_reward_debug", "0", FCVAR_GAMEDLL, "Debug quest reward granting with DevMsg (requires developer 1)" );
static void CC_Quest_Dump_Def( const CCommand &args );
static ConCommand quest_dump_def( "quest_dump_def", CC_Quest_Dump_Def, "Print quest def info: quest_dump_def <id>", FCVAR_GAMEDLL );

static CUtlString EscapeForQuotedArg(const char *s)
{
	CUtlString out;
	if (!s)
		return out;
	for (const char *p = s; *p; ++p)
	{
		if (*p == '\\' || *p == '"')
			out.Append("\\");
		char tmp[2] = { *p, '\0' };
		out.Append(tmp);
	}
	return out;
}

static void SendToastToPlayer( CBasePlayer *player, const char *category, const char *message, const char *sound )
{
	if ( !player || !message )
		return;
	CUtlString safeMsg = EscapeForQuotedArg( message );
	CUtlString safeSound = EscapeForQuotedArg( sound ? sound : "" );
	engine->ClientCommand( player->edict(), UTIL_VarArgs( "ui_toast \"%s\" \"%s\" \"%s\"\n", category ? category : "ui", safeMsg.Get(), safeSound.Get() ) );
}

static bool MatchesTargetNameList( const CUtlVector<CUtlString> &targets, const char *victimName )
{
	if ( !victimName || !victimName[0] )
		return false;
	for ( int i = 0; i < targets.Count(); ++i )
	{
		if ( !Q_stricmp( victimName, targets[i].Get() ) )
			return true;
	}
	return false;
}

static void CopyQuestDef( QuestDef &dst, const QuestDef &src )
{
	dst.id = src.id;
	dst.title = src.title;
	dst.description = src.description;
	dst.minReputation = src.minReputation;
	dst.killTarget = src.killTarget;
	dst.killCount = src.killCount;
	dst.failOnKillTarget = src.failOnKillTarget;
	dst.failOnKillRepPenalty = src.failOnKillRepPenalty;

	dst.killTargets.RemoveAll();
	for ( int i = 0; i < src.killTargets.Count(); ++i )
		dst.killTargets.AddToTail( src.killTargets[i] );

	dst.failOnKillTargets.RemoveAll();
	for ( int i = 0; i < src.failOnKillTargets.Count(); ++i )
		dst.failOnKillTargets.AddToTail( src.failOnKillTargets[i] );

	dst.stages.RemoveAll();
	for ( int i = 0; i < src.stages.Count(); ++i )
	{
		dst.stages.AddToTail();
		QuestStageDef &outStage = dst.stages.Tail();
		const QuestStageDef &inStage = src.stages[i];

		outStage.title = inStage.title;
		outStage.description = inStage.description;
		outStage.killTarget = inStage.killTarget;
		outStage.killCount = inStage.killCount;

		outStage.killTargets.RemoveAll();
		for ( int j = 0; j < inStage.killTargets.Count(); ++j )
			outStage.killTargets.AddToTail( inStage.killTargets[j] );

		outStage.objectives.RemoveAll();
		for ( int j = 0; j < inStage.objectives.Count(); ++j )
			outStage.objectives.AddToTail( inStage.objectives[j] );
		outStage.pActions = inStage.pActions;
		outStage.next = inStage.next;
		outStage.final = inStage.final;
	}
	dst.xp = src.xp;
	dst.pOnComplete = src.pOnComplete;
	dst.pOnFail = src.pOnFail;
	dst.pFailConds = src.pFailConds;

	dst.rewards.RemoveAll();
	for ( int i = 0; i < src.rewards.Count(); ++i )
	{
		dst.rewards.AddToTail();
		QuestRewardDef &outReward = dst.rewards.Tail();
		outReward.itemName = src.rewards[i].itemName;
		outReward.amount = src.rewards[i].amount;
	}
}

// the longest start of a UTF-8 string that fits in maxBytes without cutting a character
static CUtlString Utf8Prefix( const char *s, int maxBytes )
{
	int n = 0, len = s ? Q_strlen( s ) : 0;
	if ( len <= maxBytes )
		return CUtlString( s ? s : "" );
	n = maxBytes;
	while ( n > 0 && ( (unsigned char)s[n] & 0xC0 ) == 0x80 )
		--n;
	CUtlString out;
	out.SetDirect( s, n );
	return out;
}

static const QuestStageDef *GetQuestStageDef( const QuestDef &def, int stage )
{
	if ( stage < 0 || stage >= def.stages.Count() )
		return NULL;
	return &def.stages[stage];
}

static const char *GetQuestTitleForStage( const QuestDef &def, int stage )
{
	const QuestStageDef *st = GetQuestStageDef( def, stage );
	if ( st && st->title.Length() > 0 )
		return st->title.Get();
	if ( def.title.Length() > 0 )
		return def.title.Get();
	return def.id.Get();
}

static const char *GetQuestDescForStage( const QuestDef &def, int stage )
{
	const QuestStageDef *st = GetQuestStageDef( def, stage );
	if ( st && st->description.Length() > 0 )
		return st->description.Get();
	return def.description.Get();
}

CQuestSystem::CQuestSystem()
{
}

void CQuestSystem::LevelInitPreEntity()
{
	m_LastMarkers = "-";		// the client forgets its markers on a new level
	m_LastTracked = "-";
	m_flNextMarkers = 0.0f;
	// HL2RPM: a new game starts without quests; a changelevel keeps them, a loaded
	// game gets them from the save (see the save/restore block below)
	if ( gpGlobals->eLoadType == MapLoad_NewGame || gpGlobals->eLoadType == MapLoad_Background )
	{
		ClearPlayerData();
		DialogDefinitions::ClearDialogProgress();
	}
}

void CQuestSystem::LevelInitPostEntity()
{
	ListenForGameEvent( "entity_killed" );
}

void CQuestSystem::LevelShutdownPostEntity()
{
	// HL2RPM: the player data used to be dropped here, i.e. on every changelevel
}

void CQuestSystem::ClearPlayerData()
{
	m_PlayerData.RemoveAll();
}

void CQuestSystem::PrintState() const
{
	for ( int i = 0; i < m_PlayerData.Count(); ++i )
	{
		const PlayerData &data = m_PlayerData[i];
		Msg( "[quests] player %d reputation %d, %d quest(s)\n", data.entIndex, data.reputation, data.quests.Count() );
		for ( int q = 0; q < data.quests.Count(); ++q )
			Msg( "[quests]   %s  state %d  stage %d  progress %d\n", data.quests[q].id.Get(), (int)data.quests[q].state, data.quests[q].stage, data.quests[q].progress );
	}
}

CON_COMMAND_F( quest_dump, "Print the quests and reputation of the player", FCVAR_GAMEDLL )
{
	QuestSystem().PrintState();
}

// Tab menu - Quests - Enter: track this quest (again - track none: the markers of all quests)
CON_COMMAND_F( rpm_quest_track, "HL2RPM: rpm_quest_track <quest id> - its markers on the HUD (the same again - all quests)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer || args.ArgC() < 2 )
		return;
	KeyValues *pPl = RPMState::Section( "player" );
	pPl->SetString( "tracked", Q_stricmp( pPl->GetString( "tracked", "" ), args[1] ) ? args[1] : "" );
	QuestSystem().ForceMarkers();
	QuestSystem().Think( pPlayer );
}

CON_COMMAND_F( quest_grant, "Test: give the player a quest: quest_grant <id>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient() ? UTIL_GetCommandClient() : UTIL_PlayerByIndex( 1 );
	if ( pPlayer && args.ArgC() > 1 )
		Msg( "quest_grant %s: %s\n", args[1], QuestSystem().GrantQuest( pPlayer, args[1] ) ? "ok" : "failed" );
}

CON_COMMAND_F( quest_rep, "Test: change the player's reputation: quest_rep <delta>", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient() ? UTIL_GetCommandClient() : UTIL_PlayerByIndex( 1 );
	if ( pPlayer && args.ArgC() > 1 )
		QuestSystem().AddReputation( pPlayer, atoi( args[1] ) );
}

static const int QUEST_SAVE_VERSION = 1;

void CQuestSystem::SaveState( ISave *pSave )
{
	int nVersion = QUEST_SAVE_VERSION;
	pSave->WriteInt( &nVersion );
	int nPlayers = m_PlayerData.Count();
	pSave->WriteInt( &nPlayers );
	for ( int i = 0; i < nPlayers; ++i )
	{
		PlayerData &data = m_PlayerData[i];
		pSave->WriteInt( &data.entIndex );
		pSave->WriteInt( &data.reputation );
		int nQuests = data.quests.Count();
		pSave->WriteInt( &nQuests );
		for ( int q = 0; q < nQuests; ++q )
		{
			PlayerQuestState &st = data.quests[q];
			pSave->WriteString( st.id.Get() ? st.id.Get() : "" );
			int iState = (int)st.state;
			pSave->WriteInt( &iState );
			pSave->WriteInt( &st.stage );
			pSave->WriteInt( &st.progress );
		}
	}
}

void CQuestSystem::RestoreState( IRestore *pRestore )
{
	m_PlayerData.RemoveAll();
	const int nVersion = pRestore->ReadInt();
	if ( nVersion != QUEST_SAVE_VERSION )
		return;
	const int nPlayers = pRestore->ReadInt();
	for ( int i = 0; i < nPlayers; ++i )
	{
		m_PlayerData.AddToTail();
		PlayerData &data = m_PlayerData.Tail();
		data.entIndex = pRestore->ReadInt();
		data.reputation = pRestore->ReadInt();
		const int nQuests = pRestore->ReadInt();
		for ( int q = 0; q < nQuests; ++q )
		{
			char szId[256];
			pRestore->ReadString( szId, sizeof( szId ), 0 );
			data.quests.AddToTail();
			PlayerQuestState &st = data.quests.Tail();
			st.id = szId;
			st.state = (QuestState)pRestore->ReadInt();
			st.stage = pRestore->ReadInt();
			st.progress = pRestore->ReadInt();
		}
	}
}

void CQuestSystem::FireGameEvent( IGameEvent *event )
{
	if ( !event )
		return;

	if ( Q_stricmp( event->GetName(), "entity_killed" ) != 0 )
		return;

	CBasePlayer *attacker = NULL;
	int attackerEntIndex = event->GetInt( "entindex_attacker", -1 );
	if ( attackerEntIndex > 0 )
		attacker = ToBasePlayer( UTIL_EntityByIndex( attackerEntIndex ) );
	if ( !attacker )
	{
		int attackerUserId = event->GetInt( "attacker", 0 );
		if ( attackerUserId > 0 )
			attacker = UTIL_PlayerByUserId( attackerUserId );
	}

	int victimEntIndex = event->GetInt( "entindex_killed", -1 );
	if ( victimEntIndex <= 0 )
		return;

	CBaseEntity *victim = UTIL_EntityByIndex( victimEntIndex );
	if ( !victim )
		return;

	// HL2RPM: "kill" events of the Creation Kit (whoever killed), XP for NPCs of the kit
	RPMLogic::NoteKill( victim, attacker ? (CBaseEntity *)attacker : ( attackerEntIndex > 0 ? UTIL_EntityByIndex( attackerEntIndex ) : NULL ) );
	if ( !attacker )
		return;

	PlayerData *data = FindPlayerDataByEntIndex( attacker->entindex() );
	if ( !data )
		return;

	const char *victimName = victim->GetEntityName().ToCStr();
	const char *victimClass = victim->GetClassname();

	for ( int i = 0; i < data->quests.Count(); ++i )
	{
		PlayerQuestState &st = data->quests[i];
		if ( st.state != kQuestState_Active )
			continue;

		const QuestDef *def = FindQuestDef( st.id.Get() );
		if ( !def )
			continue;

		if ( def->failOnKillTarget.Length() > 0 )
		{
			const char *failId = def->failOnKillTarget.Get();
			if ( ( victimName && Q_stricmp( victimName, failId ) == 0 ) || ( victimClass && Q_stricmp( victimClass, failId ) == 0 ) )
			{
				FailQuest( attacker, def->id.Get(), def->failOnKillRepPenalty );
				continue;
			}
		}
		if ( def->failOnKillTargets.Count() > 0 )
		{
			if ( MatchesTargetNameList( def->failOnKillTargets, victimName ) )
			{
				FailQuest( attacker, def->id.Get(), def->failOnKillRepPenalty );
				continue;
			}
		}

		const QuestStageDef *stageDef = GetQuestStageDef( *def, st.stage );
		const CUtlVector<CUtlString> *targets = NULL;
		const char *singleTarget = NULL;
		int killCount = 0;

		if ( stageDef && stageDef->killCount > 0 )
		{
			killCount = stageDef->killCount;
			if ( stageDef->killTargets.Count() > 0 )
				targets = &stageDef->killTargets;
			else if ( stageDef->killTarget.Length() > 0 )
				singleTarget = stageDef->killTarget.Get();
		}
		else if ( st.stage == 0 && def->killCount > 0 )
		{
			killCount = def->killCount;
			if ( def->killTargets.Count() > 0 )
				targets = &def->killTargets;
			else if ( def->killTarget.Length() > 0 )
				singleTarget = def->killTarget.Get();
		}

		bool killMatch = false;
		if ( targets )
		{
			killMatch = MatchesTargetNameList( *targets, victimName );
		}
		else if ( singleTarget )
		{
			killMatch = ( ( victimName && Q_stricmp( victimName, singleTarget ) == 0 ) || ( victimClass && Q_stricmp( victimClass, singleTarget ) == 0 ) );
		}

		if ( killMatch && killCount > 0 )
		{
			st.progress++;
			SendToastToPlayer( attacker, "quest", UTIL_VarArgs( "Quest progress: %s", GetQuestTitleForStage( *def, st.stage ) ), "friends/friend_join.wav" );
			if ( st.progress >= killCount )
			{
				SendToastToPlayer( attacker, "quest", UTIL_VarArgs( "Objective complete: %s", GetQuestTitleForStage( *def, st.stage ) ), "friends/friend_online.wav" );
				st.stage++;
				st.progress = 0;
			}
			SendQuestLogToPlayer( attacker );
		}

		// HL2RPM: "kill" objectives of the stage
		if ( stageDef )
		{
			CUtlString questId = st.id;		// st may move: the actions of a new stage can add quests
			for ( int o = 0; o < stageDef->objectives.Count(); ++o )
			{
				const QuestObjDef &obj = stageDef->objectives[o];
				if ( Q_stricmp( obj.type, "kill" ) || !RPMLogic::NameMatches( obj.target, victim ) )
					continue;
				int have = ObjectiveProgress( questId, obj.id );
				if ( have < obj.count )
					SetObjectiveProgress( attacker, *def, obj, have + 1 );
			}
			AdvanceIfDone( attacker, questId );
		}
	}
}

void CQuestSystem::RegisterQuestDef( const QuestDef &def )
{
	if ( quest_reward_debug.GetBool() )
	{
		DevMsg( "QuestSystem: RegisterQuestDef id='%s' title='%s' rewards=%d stages=%d\n",
			def.id.Get(), def.title.Get(), def.rewards.Count(), def.stages.Count() );
		for ( int i = 0; i < def.rewards.Count(); ++i )
		{
			DevMsg( "  reward[%d]: '%s' x%d\n", i, def.rewards[i].itemName.Get(), def.rewards[i].amount );
		}
	}
	for ( int i = 0; i < m_QuestDefs.Count(); ++i )
	{
		if ( m_QuestDefs[i].id == def.id )
		{
			CopyQuestDef( m_QuestDefs[i], def );
			return;
		}
	}
	m_QuestDefs.AddToTail();
	CopyQuestDef( m_QuestDefs.Tail(), def );
}

void CQuestSystem::ClearQuestDefs()
{
	m_QuestDefs.RemoveAll();
	RPMLogic::FreePool( RPMLogic::POOL_QUESTS );
}

const QuestDef *CQuestSystem::FindQuestDef( const char *questId ) const
{
	if ( !questId || !questId[0] )
		return NULL;

	for ( int i = 0; i < m_QuestDefs.Count(); ++i )
	{
		if ( m_QuestDefs[i].id == questId )
			return &m_QuestDefs[i];
	}
	return NULL;
}

int CQuestSystem::GetReputation( CBasePlayer *player ) const
{
	if ( !player )
		return 0;

	const PlayerData *data = FindPlayerDataByEntIndex( player->entindex() );
	return data ? data->reputation : 0;
}

void CQuestSystem::AddReputation( CBasePlayer *player, int delta )
{
	PlayerData *data = FindOrCreatePlayerData( player );
	if ( !data )
		return;

	data->reputation += delta;
}

const PlayerQuestState *CQuestSystem::FindPlayerQuest( CBasePlayer *player, const char *questId ) const
{
	if ( !player || !questId || !questId[0] )
		return NULL;

	const PlayerData *data = FindPlayerDataByEntIndex( player->entindex() );
	if ( !data )
		return NULL;

	for ( int i = 0; i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].id == questId )
			return &data->quests[i];
	}

	return NULL;
}

PlayerQuestState *CQuestSystem::FindOrCreatePlayerQuest( CBasePlayer *player, const char *questId )
{
	if ( !player || !questId || !questId[0] )
		return NULL;

	PlayerData *data = FindOrCreatePlayerData( player );
	if ( !data )
		return NULL;

	for ( int i = 0; i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].id == questId )
			return &data->quests[i];
	}

	PlayerQuestState st;
	st.id = questId;
	st.state = kQuestState_Inactive;
	st.stage = 0;
	st.progress = 0;
	data->quests.AddToTail( st );
	return &data->quests.Tail();
}

bool CQuestSystem::GrantQuest( CBasePlayer *player, const char *questId )
{
	if ( !player )
		return false;

	const QuestDef *def = FindQuestDef( questId );
	if ( !def )
		return false;

	if ( GetReputation( player ) < def->minReputation )
		return false;

	PlayerQuestState *st = FindOrCreatePlayerQuest( player, questId );
	if ( !st )
		return false;

	if ( st->state == kQuestState_Active || st->state == kQuestState_Completed )
		return false;

	st->state = kQuestState_Active;
	st->stage = 0;
	st->progress = 0;
	ClearObjectives( questId );
	NotifyQuestStateChanged( player, questId, kQuestState_Active );
	CUtlString id = questId;		// the actions may change the quest list
	// a new quest becomes the tracked one (New Vegas): its markers on the HUD
	RPMState::Section( "player" )->SetString( "tracked", id.Get() );
	m_flNextMarkers = 0.0f;
	RPMLogic::NoteQuest( player, id, "quest_start", INT_MIN );
	EnterStage( player, id, 0 );
	SendQuestLogToPlayer( player );
	return true;
}

bool CQuestSystem::CompleteQuest( CBasePlayer *player, const char *questId )
{
	PlayerQuestState *st = FindOrCreatePlayerQuest( player, questId );
	if ( !st || st->state != kQuestState_Active )
	{
		if ( quest_reward_debug.GetBool() )
			DevMsg( "QuestSystem: CompleteQuest FAILED id='%s' (no state/!active)\n", questId ? questId : "" );
		return false;
	}

	st->state = kQuestState_Completed;
	NotifyQuestStateChanged( player, questId, st->state );
	const QuestDef *def = FindQuestDef( questId );
	if ( def )
	{
		RPMLogic::AddXP( player, def->xp );
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = player;
		ctx.pszQuest = def->id.Get();
		RPMLogic::Run( def->pOnComplete, ctx );
		RPMLogic::NoteQuest( player, def->id.Get(), "quest_complete", INT_MIN );
	}
	if ( quest_reward_debug.GetBool() )
	{
		SendToastToPlayer( player, "quest", UTIL_VarArgs( "Quest completed: %s", questId ? questId : "" ), "friends/friend_online.wav" );
		DevMsg( "QuestSystem: CompleteQuest id='%s' def=%p\n", questId ? questId : "", def );
	}
	if ( def )
	{
		if ( quest_reward_debug.GetBool() )
		{
			SendToastToPlayer( player, "quest", UTIL_VarArgs( "Rewards configured: %d", def->rewards.Count() ), "friends/friend_join.wav" );
			DevMsg( "QuestSystem: rewards.Count=%d\n", def->rewards.Count() );
		}
		for ( int i = 0; i < def->rewards.Count(); ++i )
		{
			const QuestRewardDef &r = def->rewards[i];
			if ( r.itemName.Length() <= 0 || r.amount <= 0 )
				continue;
			if ( quest_reward_debug.GetBool() )
				DevMsg( "QuestSystem: grant reward '%s' x%d (ammo=%d)\n", r.itemName.Get(), r.amount, InventorySystem::IsAmmoItem( r.itemName.Get() ) ? 1 : 0 );
			if ( InventorySystem::IsAmmoItem( r.itemName.Get() ) )
			{
				InventorySystem::AddItemToPlayerWithBullets( player, r.itemName.Get(), r.amount );
			}
			else
			{
				for ( int j = 0; j < r.amount; ++j )
					InventorySystem::AddItemToPlayer( player, r.itemName.Get() );
			}
			InventorySystem::NotifyPlayerReward( player, r.itemName.Get(), r.amount );
			if ( quest_reward_debug.GetBool() )
				SendToastToPlayer( player, "quest", UTIL_VarArgs( "Reward: %s x%d", r.itemName.Get(), r.amount ), "" );
		}
		if ( quest_reward_debug.GetBool() )
			DevMsg( "QuestSystem: SendInventoryToPlayer\n" );
		InventorySystem::SendInventoryToPlayer( player );
	}
	SendQuestLogToPlayer( player );
	return true;
}

bool CQuestSystem::FailQuest( CBasePlayer *player, const char *questId, int repPenalty )
{
	PlayerQuestState *st = FindOrCreatePlayerQuest( player, questId );
	if ( !st || ( st->state != kQuestState_Active && st->state != kQuestState_Inactive ) )
		return false;

	st->state = kQuestState_Failed;
	NotifyQuestStateChanged( player, questId, st->state );
	if ( repPenalty != 0 )
		AddReputation( player, repPenalty );
	const QuestDef *def = FindQuestDef( questId );
	if ( def )
	{
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = player;
		ctx.pszQuest = def->id.Get();
		RPMLogic::Run( def->pOnFail, ctx );
		RPMLogic::NoteQuest( player, def->id.Get(), "quest_fail", INT_MIN );
	}
	SendQuestLogToPlayer( player );
	return true;
}

bool CQuestSystem::SetQuestStage( CBasePlayer *player, const char *questId, int stage )
{
	PlayerQuestState *st = FindOrCreatePlayerQuest( player, questId );
	if ( !st || st->state != kQuestState_Active )
		return false;

	st->stage = stage;
	st->progress = 0;
	CUtlString id = questId;
	EnterStage( player, id, stage );
	SendQuestLogToPlayer( player );
	return true;
}

//-----------------------------------------------------------------------------
// HL2RPM: objectives of the stages (Creation Kit)
//-----------------------------------------------------------------------------
static KeyValues *ObjSection()
{
	return RPMState::Section( "qobj" );
}

static CUtlString ObjKey( const char *questId, const char *objId )
{
	CUtlString k;
	k.Format( "%s.%s", questId ? questId : "", objId ? objId : "" );
	return k;
}

static const QuestObjDef *FindObjective( const QuestDef &def, const char *objId, int *pStage = NULL )
{
	for ( int s = 0; s < def.stages.Count(); ++s )
		for ( int o = 0; o < def.stages[s].objectives.Count(); ++o )
			if ( !Q_stricmp( def.stages[s].objectives[o].id, objId ) )
			{
				if ( pStage )
					*pStage = s;
				return &def.stages[s].objectives[o];
			}
	return NULL;
}

int CQuestSystem::ObjectiveProgress( const char *questId, const char *objId ) const
{
	return ObjSection()->GetInt( ObjKey( questId, objId ), 0 );
}

bool CQuestSystem::IsObjectiveDone( const char *questId, const char *objId ) const
{
	const QuestDef *def = FindQuestDef( questId );
	const QuestObjDef *obj = def ? FindObjective( *def, objId ) : NULL;
	return obj && ObjectiveProgress( questId, objId ) >= Max( 1, obj->count );
}

void CQuestSystem::ClearObjectives( const char *questId )
{
	KeyValues *s = ObjSection();
	CUtlString prefix;
	prefix.Format( "%s.", questId );
	for ( KeyValues *k = s->GetFirstSubKey(); k; )
	{
		KeyValues *next = k->GetNextKey();
		if ( !Q_strnicmp( k->GetName(), prefix, prefix.Length() ) )
		{
			s->RemoveSubKey( k );
			k->deleteThis();
		}
		k = next;
	}
}

void CQuestSystem::SetObjectiveProgress( CBasePlayer *player, const QuestDef &def, const QuestObjDef &obj, int progress )
{
	int need = Max( 1, obj.count );
	int was = ObjectiveProgress( def.id, obj.id );
	progress = clamp( progress, 0, need );
	if ( progress == was )
		return;
	ObjSection()->SetInt( ObjKey( def.id, obj.id ), progress );
	bool ru = RPMLogic::IsRussian();
	const char *text = obj.text.IsEmpty() ? def.title.Get() : obj.text.Get();
	if ( progress >= need )
		SendToastToPlayer( player, "quest", CFmtStr( ru ? "Цель выполнена: %s" : "Objective complete: %s", text ), "friends/friend_online.wav" );
	else if ( need > 1 && progress > was )
		SendToastToPlayer( player, "quest", CFmtStr( "%s (%d/%d)", text, progress, need ), "friends/friend_join.wav" );
	SendQuestLogToPlayer( player );
}

bool CQuestSystem::CompleteObjective( CBasePlayer *player, const char *questId, const char *objId )
{
	const QuestDef *def = FindQuestDef( questId );
	const QuestObjDef *obj = def ? FindObjective( *def, objId ) : NULL;
	if ( !obj )
	{
		Warning( "QuestSystem: no objective '%s' in quest '%s'\n", objId ? objId : "", questId ? questId : "" );
		return false;
	}
	SetObjectiveProgress( player, *def, *obj, Max( 1, obj->count ) );
	CUtlString id = questId;
	AdvanceIfDone( player, id );
	return true;
}

bool CQuestSystem::StageDone( const QuestDef &def, int stage ) const
{
	const QuestStageDef *st = GetQuestStageDef( def, stage );
	if ( !st || st->objectives.Count() == 0 )
		return false;		// a stage without objectives waits for the actions of dialogs / events
	for ( int o = 0; o < st->objectives.Count(); ++o )
	{
		const QuestObjDef &obj = st->objectives[o];
		if ( !obj.optional && ObjectiveProgress( def.id, obj.id ) < Max( 1, obj.count ) )
			return false;
	}
	return true;
}

void CQuestSystem::AdvanceIfDone( CBasePlayer *player, const char *questId )
{
	if ( m_iThinkDepth > 8 )
		return;
	const PlayerQuestState *st = FindPlayerQuest( player, questId );
	const QuestDef *def = FindQuestDef( questId );
	if ( !st || !def || st->state != kQuestState_Active || !StageDone( *def, st->stage ) )
		return;
	m_iThinkDepth++;
	const QuestStageDef &stage = def->stages[st->stage];
	int next = stage.next >= 0 ? stage.next : st->stage + 1;
	CUtlString id = questId;
	if ( stage.final || next >= def->stages.Count() )
		CompleteQuest( player, id );
	else
		SetQuestStage( player, id, next );
	m_iThinkDepth--;
}

void CQuestSystem::EnterStage( CBasePlayer *player, const char *questId, int stage )
{
	const QuestDef *def = FindQuestDef( questId );
	const QuestStageDef *st = def ? GetQuestStageDef( *def, stage ) : NULL;
	if ( !st )
		return;
	bool ru = RPMLogic::IsRussian();
	if ( stage > 0 || st->objectives.Count() )
	{
		const char *text = st->objectives.Count() && !st->objectives[0].text.IsEmpty() ? st->objectives[0].text.Get() : st->title.Get();
		if ( text && text[0] )
			SendToastToPlayer( player, "quest", CFmtStr( ru ? "Новая цель: %s" : "New objective: %s", text ), "friends/friend_join.wav" );
	}
	m_LastMarkers = "-";		// the markers of the new objectives at once
	m_flNextMarkers = 0.0f;
	RPMLogic::Ctx_t ctx;
	ctx.pPlayer = player;
	ctx.pszQuest = def->id.Get();
	RPMLogic::NoteQuest( player, def->id.Get(), "quest_stage", stage );
	m_iThinkDepth++;
	RPMLogic::Run( st->pActions, ctx );
	m_iThinkDepth--;
}

void CQuestSystem::OnTalk( CBasePlayer *player, CBaseEntity *npc )
{
	PlayerData *data = player ? FindPlayerDataByEntIndex( player->entindex() ) : NULL;
	for ( int i = 0; data && i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].state != kQuestState_Active )
			continue;
		CUtlString id = data->quests[i].id;
		const QuestDef *def = FindQuestDef( id );
		const QuestStageDef *st = def ? GetQuestStageDef( *def, data->quests[i].stage ) : NULL;
		for ( int o = 0; st && o < st->objectives.Count(); ++o )
			if ( !Q_stricmp( st->objectives[o].type, "talk" ) && RPMLogic::NameMatches( st->objectives[o].target, npc ) )
				SetObjectiveProgress( player, *def, st->objectives[o], st->objectives[o].count );
		AdvanceIfDone( player, id );
	}
}

void CQuestSystem::OnUse( CBasePlayer *player, CBaseEntity *ent )
{
	PlayerData *data = player ? FindPlayerDataByEntIndex( player->entindex() ) : NULL;
	for ( int i = 0; data && i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].state != kQuestState_Active )
			continue;
		CUtlString id = data->quests[i].id;
		const QuestDef *def = FindQuestDef( id );
		const QuestStageDef *st = def ? GetQuestStageDef( *def, data->quests[i].stage ) : NULL;
		for ( int o = 0; st && o < st->objectives.Count(); ++o )
			if ( !Q_stricmp( st->objectives[o].type, "use" ) && RPMLogic::NameMatches( st->objectives[o].target, ent ) )
				SetObjectiveProgress( player, *def, st->objectives[o], st->objectives[o].count );
		AdvanceIfDone( player, id );
	}
}

void CQuestSystem::Think( CBasePlayer *player )
{
	PlayerData *data = player ? FindPlayerDataByEntIndex( player->entindex() ) : NULL;
	for ( int i = 0; data && i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].state != kQuestState_Active )
			continue;
		CUtlString id = data->quests[i].id;
		const QuestDef *def = FindQuestDef( id );
		if ( !def )
			continue;
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = player;
		ctx.pszQuest = def->id.Get();
		if ( def->pFailConds && RPMLogic::Check( def->pFailConds, ctx ) )
		{
			FailQuest( player, id, 0 );
			continue;
		}
		const QuestStageDef *st = GetQuestStageDef( *def, data->quests[i].stage );
		for ( int o = 0; st && o < st->objectives.Count(); ++o )
		{
			const QuestObjDef &obj = st->objectives[o];
			int need = Max( 1, obj.count );
			int have = ObjectiveProgress( id, obj.id );
			if ( have >= need )
				continue;
			if ( !Q_stricmp( obj.type, "collect" ) )
				SetObjectiveProgress( player, *def, obj, InventorySystem::CountItem( player, obj.item ) );
			else if ( !Q_stricmp( obj.type, "go" ) )
			{
				Vector pos;
				int zone = RPMWorld::FindZone( obj.target );
				bool bThere = zone >= 0 ? RPMWorld::IsInZone( zone, player->GetAbsOrigin() ) :
					( RPMLogic::TargetPos( obj.target, ctx, pos ) && ( pos - player->GetAbsOrigin() ).Length() <= obj.radius );
				if ( bThere )
					SetObjectiveProgress( player, *def, obj, need );
			}
			else if ( !Q_stricmp( obj.type, "cond" ) && RPMLogic::Check( obj.pConds, ctx ) )
				SetObjectiveProgress( player, *def, obj, need );
		}
		AdvanceIfDone( player, id );
	}
	SendMarkers( player );
}

static ConVar rpm_markers_debug( "rpm_markers_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print the quest markers sent to the HUD and the tracked quest" );

void CQuestSystem::SendMarkers( CBasePlayer *player )
{
	if ( gpGlobals->curtime < m_flNextMarkers )
		return;
	m_flNextMarkers = gpGlobals->curtime + 0.5f;
	CUtlString out;
	PlayerData *data = player ? FindPlayerDataByEntIndex( player->entindex() ) : NULL;
	// the tracked quest (Tab menu - Quests, Enter): markers only of it; none or not active - of all
	const char *tracked = RPMState::Section( "player" )->GetString( "tracked", "" );
	bool bTrackedActive = false;
	for ( int i = 0; data && tracked[0] && i < data->quests.Count(); ++i )
		if ( data->quests[i].state == kQuestState_Active && !Q_stricmp( data->quests[i].id, tracked ) )
			bTrackedActive = true;
	if ( !bTrackedActive )
		tracked = "";
	if ( m_LastTracked != tracked )
	{
		m_LastTracked = tracked;
		engine->ClientCommand( player->edict(), "rpm_quest_tracked \"%s\"\n", tracked );
	}
	for ( int i = 0; data && i < data->quests.Count(); ++i )
	{
		if ( data->quests[i].state != kQuestState_Active )
			continue;
		if ( tracked[0] && Q_stricmp( data->quests[i].id, tracked ) )
			continue;
		const QuestDef *def = FindQuestDef( data->quests[i].id );
		const QuestStageDef *st = def ? GetQuestStageDef( *def, data->quests[i].stage ) : NULL;
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = player;
		for ( int o = 0; st && o < st->objectives.Count(); ++o )
		{
			const QuestObjDef &obj = st->objectives[o];
			Vector pos;
			if ( !obj.marker || obj.target.IsEmpty() || ObjectiveProgress( def->id, obj.id ) >= Max( 1, obj.count ) )
				continue;
			if ( !Q_stricmp( obj.type, "kill" ) )
			{
				// the nearest living one that matches
				CBaseEntity *pBest = NULL;
				float flBest = FLT_MAX;
				for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
				{
					if ( !pEnt->MyNPCPointer() || !pEnt->IsAlive() || !RPMLogic::NameMatches( obj.target, pEnt ) )
						continue;
					float d = ( pEnt->GetAbsOrigin() - player->GetAbsOrigin() ).LengthSqr();
					if ( d < flBest )
					{
						flBest = d;
						pBest = pEnt;
					}
				}
				if ( !pBest )
					continue;
				pos = pBest->WorldSpaceCenter();
			}
			else if ( !RPMLogic::TargetPos( obj.target, ctx, pos ) )
				continue;
			char label[128];
			Q_strncpy( label, obj.text.IsEmpty() ? def->title.Get() : obj.text.Get(), sizeof( label ) );
			for ( char *c = label; *c; ++c )
				if ( *c == ';' || *c == '~' || *c == '"' )
					*c = ' ';
			if ( !out.IsEmpty() )
				out.Append( ";" );
			out.Append( CFmtStr( "%.0f %.0f %.0f~%s", pos.x, pos.y, pos.z + 12.0f, label ) );
		}
	}
	if ( out == m_LastMarkers )
		return;
	m_LastMarkers = out;
	if ( rpm_markers_debug.GetBool() )
		Msg( "[quests] markers (tracked '%s'): %s\n", tracked, out.IsEmpty() ? "none" : out.Get() );
	engine->ClientCommand( player->edict(), "rpm_markers \"%s\"\n", out.Get() );
}

void CQuestSystem::DebugSetQuest( CBasePlayer *player, const char *questId, QuestState state, int stage )
{
	PlayerQuestState *st = FindOrCreatePlayerQuest( player, questId );
	if ( !st )
		return;
	st->state = state;
	st->stage = stage;
	st->progress = 0;
	m_LastMarkers = "-";
}

void CQuestSystem::SendQuestLogToPlayer( CBasePlayer *player ) const
{
	if ( !player )
		return;

	const PlayerData *data = FindPlayerDataByEntIndex( player->entindex() );
	if ( !data )
		return;

	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();

	UserMessageBegin( filter, "Quests_Update" );
	WRITE_BYTE( kQuestsUpdate_QuestsBegin );
	MessageEnd();

	for ( int i = 0; i < data->quests.Count(); ++i )
	{
		const PlayerQuestState &st = data->quests[i];
		const QuestDef *def = FindQuestDef( st.id.Get() );
		if ( !def )
			continue;

		CUtlString stageTitle = GetQuestTitleForStage( *def, st.stage );
		CUtlString stageDesc = GetQuestDescForStage( *def, st.stage );
		// HL2RPM: the objectives of the current stage under its description
		const QuestStageDef *pStage = GetQuestStageDef( *def, st.stage );
		if ( st.state == kQuestState_Active && pStage )
		{
			for ( int o = 0; o < pStage->objectives.Count(); ++o )
			{
				const QuestObjDef &obj = pStage->objectives[o];
				if ( obj.text.IsEmpty() )
					continue;
				int need = Max( 1, obj.count ), have = ObjectiveProgress( def->id, obj.id );
				if ( !stageDesc.IsEmpty() )
					stageDesc.Append( "\n" );
				stageDesc.Append( have >= need ? "[x] " : "[  ] " );
				stageDesc.Append( obj.text );
				if ( need > 1 )
					stageDesc.Append( CFmtStr( " (%d/%d)", Min( have, need ), need ) );
				if ( obj.optional )
					stageDesc.Append( RPMLogic::IsRussian() ? " (необязательно)" : " (optional)" );
			}
		}
		if ( st.state == kQuestState_Completed && def->stages.Count() > 0 )
		{
			stageTitle = "Done: ";
			for ( int s = 0; s < def->stages.Count(); ++s )
			{
				if ( s > 0 ) stageTitle.Append( " -> " );
				const char *t = def->stages[s].title.Length() > 0 ? def->stages[s].title.Get() : def->title.Get();
				stageTitle.Append( t ? t : "" );
			}

			stageDesc.Clear();
			for ( int s = 0; s < def->stages.Count(); ++s )
			{
				if ( s > 0 ) stageDesc.Append( "\n" );
				const char *d = def->stages[s].description.Get();
				stageDesc.Append( d ? d : "" );
			}
		}

		// HL2RPM: a user message holds 255 bytes - the titles are cut, the description goes in parts
		// (a Russian description of 120 letters alone is 240 bytes)
		CUtlString title = Utf8Prefix( def->title.Get(), 60 );
		stageTitle = Utf8Prefix( stageTitle.Get(), 60 );
		int used = 1 + st.id.Length() + 1 + 1 + 2 + 2 + title.Length() + 1 + stageTitle.Length() + 1 + 1;
		CUtlString first = Utf8Prefix( stageDesc.Get(), Max( 0, 240 - used ) );
		UserMessageBegin( filter, "Quests_Update" );
		WRITE_BYTE( kQuestsUpdate_QuestAdd );
		WRITE_STRING( st.id.Get() );
		WRITE_BYTE( (int)st.state );
		WRITE_SHORT( st.stage );
		WRITE_SHORT( st.progress );
		WRITE_STRING( title.Get() );
		WRITE_STRING( stageTitle.Get() );
		WRITE_STRING( first.Get() );
		MessageEnd();
		for ( const char *rest = stageDesc.Get() + first.Length(); *rest; )
		{
			CUtlString part = Utf8Prefix( rest, 240 );
			if ( part.IsEmpty() )
				break;
			UserMessageBegin( filter, "Quests_Update" );
			WRITE_BYTE( kQuestsUpdate_QuestDescMore );
			WRITE_STRING( part.Get() );
			MessageEnd();
			rest += part.Length();
		}
	}

	UserMessageBegin( filter, "Quests_Update" );
	WRITE_BYTE( kQuestsUpdate_QuestsEnd );
	MessageEnd();
}

CQuestSystem::PlayerData *CQuestSystem::FindOrCreatePlayerData( CBasePlayer *player )
{
	if ( !player )
		return NULL;

	const int entIndex = player->entindex();

	for ( int i = 0; i < m_PlayerData.Count(); ++i )
	{
		if ( m_PlayerData[i].entIndex == entIndex )
			return &m_PlayerData[i];
	}

	m_PlayerData.AddToTail();
	PlayerData &data = m_PlayerData.Tail();
	data.entIndex = entIndex;
	data.reputation = 0;
	data.quests.RemoveAll();
	return &data;
}

CQuestSystem::PlayerData *CQuestSystem::FindPlayerDataByEntIndex( int entIndex )
{
	for ( int i = 0; i < m_PlayerData.Count(); ++i )
	{
		if ( m_PlayerData[i].entIndex == entIndex )
			return &m_PlayerData[i];
	}
	return NULL;
}

const CQuestSystem::PlayerData *CQuestSystem::FindPlayerDataByEntIndex( int entIndex ) const
{
	for ( int i = 0; i < m_PlayerData.Count(); ++i )
	{
		if ( m_PlayerData[i].entIndex == entIndex )
			return &m_PlayerData[i];
	}
	return NULL;
}

void CQuestSystem::NotifyQuestStateChanged( CBasePlayer *player, const char *questId, QuestState newState ) const
{
	if ( !player || !questId || !questId[0] )
		return;

	const QuestDef *def = FindQuestDef( questId );
	if ( !def )
		return;

	const char *sound = "";
	const char *prefix = "";
	const bool ru = RPMLogic::IsRussian();
	if ( newState == kQuestState_Active )
	{
		prefix = ru ? "Новое задание: " : "Quest started: ";
		sound = "friends/friend_join.wav";
	}
	else if ( newState == kQuestState_Completed )
	{
		prefix = ru ? "Задание выполнено: " : "Quest completed: ";
		sound = "friends/friend_online.wav";
	}
	else if ( newState == kQuestState_Failed )
	{
		prefix = ru ? "Задание провалено: " : "Quest failed: ";
		sound = "friends/friend_online.wav";
	}

	char msg[512];
	Q_snprintf( msg, sizeof( msg ), "%s%s", prefix, def->title.Get() );

	CUtlString safeMsg = EscapeForQuotedArg(msg);
	CUtlString safeSound = EscapeForQuotedArg(sound);
	engine->ClientCommand( player->edict(), UTIL_VarArgs("ui_toast \"quest\" \"%s\" \"%s\"\n", safeMsg.Get(), safeSound.Get()) );
}

CQuestSystem &QuestSystem()
{
	return g_QuestSystem;
}

static void CC_Quest_Dump_Def( const CCommand &args )
{
	if ( args.ArgC() < 2 )
	{
		DevMsg( "quest_dump_def <id>\n" );
		return;
	}
	const char *id = args.Arg( 1 );
	const QuestDef *def = QuestSystem().FindQuestDef( id );
	if ( !def )
	{
		DevMsg( "QuestSystem: no def for '%s'\n", id ? id : "" );
		return;
	}
	DevMsg( "QuestSystem: def '%s' title='%s' stages=%d rewards=%d\n",
		def->id.Get(), def->title.Get(), def->stages.Count(), def->rewards.Count() );
	for ( int i = 0; i < def->rewards.Count(); ++i )
	{
		DevMsg( "  reward[%d]: '%s' x%d\n", i, def->rewards[i].itemName.Get(), def->rewards[i].amount );
	}
	for ( int s = 0; s < def->stages.Count(); ++s )
	{
		DevMsg( "  stage[%d]: title='%s' desc='%s'\n", s, def->stages[s].title.Get(), def->stages[s].description.Get() );
	}
}

static void CC_Quest_Request( const CCommand &args )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	QuestSystem().SendQuestLogToPlayer( pPlayer );
}

static ConCommand quest_request( "quest_request", CC_Quest_Request, "Request quest list." );

// ---------------------------------------------------------------------------
// HL2RPM: save/restore block for quests and reputation
// ---------------------------------------------------------------------------
#include "isaverestore.h"

class CQuestSaveRestoreBlockHandler : public CDefSaveRestoreBlockHandler
{
public:
	const char *GetBlockName() { return "HL2RPMQuests"; }

	void Save( ISave *pSave )
	{
		pSave->StartBlock( "Quests" );
		QuestSystem().SaveState( pSave );
		pSave->EndBlock();
	}

	void PreRestore()
	{
		m_bDoLoad = false;
		// a save without this block (older build) must not keep the quests of the
		// session that was running before the load
		if ( gpGlobals->eLoadType == MapLoad_LoadGame )
			QuestSystem().ClearPlayerData();
	}

	void WriteSaveHeaders( ISave *pSave )
	{
		short nVersion = 1;
		pSave->WriteShort( &nVersion );
	}

	void ReadRestoreHeaders( IRestore *pRestore )
	{
		short nVersion;
		pRestore->ReadShort( &nVersion );
		// a changelevel restores the level's old data: the quests in memory are newer
		m_bDoLoad = ( nVersion == 1 ) && ( gpGlobals->eLoadType == MapLoad_LoadGame );
	}

	void Restore( IRestore *pRestore, bool fCreatePlayers )
	{
		if ( !m_bDoLoad )
			return;
		pRestore->StartBlock();
		QuestSystem().RestoreState( pRestore );
		pRestore->EndBlock();
	}

	// (the client asks for the quest log itself when the inventory window opens)

private:
	bool m_bDoLoad;
};

static CQuestSaveRestoreBlockHandler g_QuestSaveRestoreBlockHandler;

ISaveRestoreBlockHandler *GetQuestSaveRestoreBlockHandler()
{
	return &g_QuestSaveRestoreBlockHandler;
}

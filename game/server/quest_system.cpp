#include "cbase.h"
#include "quest_system.h"

#include "inventory_system.h"
#include "recipientfilter.h"
#include "usermessages.h"

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
	}

	dst.rewards.RemoveAll();
	for ( int i = 0; i < src.rewards.Count(); ++i )
	{
		dst.rewards.AddToTail();
		QuestRewardDef &outReward = dst.rewards.Tail();
		outReward.itemName = src.rewards[i].itemName;
		outReward.amount = src.rewards[i].amount;
	}
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

void CQuestSystem::LevelInitPostEntity()
{
	ListenForGameEvent( "entity_killed" );
}

void CQuestSystem::LevelShutdownPostEntity()
{
	m_PlayerData.RemoveAll();
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
	if ( !attacker )
		return;

	int victimEntIndex = event->GetInt( "entindex_killed", -1 );
	if ( victimEntIndex <= 0 )
		return;

	CBaseEntity *victim = UTIL_EntityByIndex( victimEntIndex );
	if ( !victim )
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
	NotifyQuestStateChanged( player, questId, st->state );
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
	SendQuestLogToPlayer( player );
	return true;
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

		UserMessageBegin( filter, "Quests_Update" );
		WRITE_BYTE( kQuestsUpdate_QuestAdd );
		WRITE_STRING( st.id.Get() );
		WRITE_BYTE( (int)st.state );
		WRITE_SHORT( st.stage );
		WRITE_SHORT( st.progress );
		WRITE_STRING( def->title.Get() );
		WRITE_STRING( stageTitle.Get() );
		WRITE_STRING( stageDesc.Get() );
		MessageEnd();
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
	if ( newState == kQuestState_Active )
	{
		prefix = "Quest started: ";
		sound = "friends/friend_join.wav";
	}
	else if ( newState == kQuestState_Completed )
	{
		prefix = "Quest completed: ";
		sound = "friends/friend_online.wav";
	}
	else if ( newState == kQuestState_Failed )
	{
		prefix = "Quest failed: ";
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

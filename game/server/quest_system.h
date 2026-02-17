#pragma once

#include "GameEventListener.h"
#include "quest_netmessages.h"
#include "utlvector.h"
#include "utlstring.h"

class CBasePlayer;
class CBaseEntity;

struct QuestStageDef
{
	CUtlString title;
	CUtlString description;
	CUtlString killTarget;
	CUtlVector<CUtlString> killTargets;
	int killCount;
};

struct QuestRewardDef
{
	CUtlString itemName;
	int amount;
};

struct QuestDef
{
	CUtlString id;
	CUtlString title;
	CUtlString description;
	int minReputation;
	CUtlString killTarget;
	CUtlVector<CUtlString> killTargets;
	int killCount;
	CUtlString failOnKillTarget;
	CUtlVector<CUtlString> failOnKillTargets;
	int failOnKillRepPenalty;
	CUtlVector<QuestStageDef> stages;
	CUtlVector<QuestRewardDef> rewards;
};

struct PlayerQuestState
{
	CUtlString id;
	QuestState state;
	int stage;
	int progress;
};

class CQuestSystem : public CAutoGameSystem, public CGameEventListener
{
public:
	CQuestSystem();

	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPostEntity();
	virtual void FireGameEvent( IGameEvent *event );

	void RegisterQuestDef( const QuestDef &def );
	const QuestDef *FindQuestDef( const char *questId ) const;

	int GetReputation( CBasePlayer *player ) const;
	void AddReputation( CBasePlayer *player, int delta );

	bool GrantQuest( CBasePlayer *player, const char *questId );
	bool CompleteQuest( CBasePlayer *player, const char *questId );
	bool FailQuest( CBasePlayer *player, const char *questId, int repPenalty );
	bool SetQuestStage( CBasePlayer *player, const char *questId, int stage );
	void ClearQuestDefs();

	const PlayerQuestState *FindPlayerQuest( CBasePlayer *player, const char *questId ) const;
	PlayerQuestState *FindOrCreatePlayerQuest( CBasePlayer *player, const char *questId );

	void SendQuestLogToPlayer( CBasePlayer *player ) const;

private:
	struct PlayerData
	{
		int entIndex;
		int reputation;
		CUtlVector<PlayerQuestState> quests;
	};

	PlayerData *FindOrCreatePlayerData( CBasePlayer *player );
	PlayerData *FindPlayerDataByEntIndex( int entIndex );
	const PlayerData *FindPlayerDataByEntIndex( int entIndex ) const;

	void NotifyQuestStateChanged( CBasePlayer *player, const char *questId, QuestState newState ) const;

	CUtlVector<QuestDef> m_QuestDefs;
	CUtlVector<PlayerData> m_PlayerData;
};

CQuestSystem &QuestSystem();

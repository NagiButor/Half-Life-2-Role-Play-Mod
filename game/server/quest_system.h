#pragma once

#include "GameEventListener.h"
#include "quest_netmessages.h"
#include "utlvector.h"
#include "utlstring.h"

class CBasePlayer;
class CBaseEntity;
class KeyValues;

// HL2RPM (Creation Kit): an objective of a stage - "kill" (target: names / classes, '*'), "collect"
// (item, count), "talk" (target NPC), "go" (target: zone / entity, radius), "use" (target), "cond"
// (conditions), "custom" (done only by the action "objective"). Progress: RPMState "qobj" <quest>.<id>.
struct QuestObjDef
{
	QuestObjDef() : count( 1 ), radius( 128.0f ), optional( false ), marker( true ), pConds( NULL ) {}
	CUtlString id;
	CUtlString text;
	CUtlString type;
	CUtlString target;
	CUtlString item;
	int count;
	float radius;
	bool optional;
	bool marker;			// a marker on the HUD at the target
	KeyValues *pConds;		// "cond" (owned by RPMLogic::POOL_QUESTS)
};

struct QuestStageDef
{
	QuestStageDef() : killCount( 0 ), pActions( NULL ), next( -1 ), final( false ) {}
	CUtlString title;
	CUtlString description;
	CUtlString killTarget;
	CUtlVector<CUtlString> killTargets;
	int killCount;
	// HL2RPM: objectives (all required done -> the next stage, or the quest is completed after the last /
	// a "final" stage), actions run when the stage begins, the stage that follows (-1: the next one)
	CUtlVector<QuestObjDef> objectives;
	KeyValues *pActions;
	int next;
	bool final;
};

struct QuestRewardDef
{
	CUtlString itemName;
	int amount;
};

struct QuestDef
{
	QuestDef() : minReputation( 0 ), killCount( 0 ), failOnKillRepPenalty( 0 ), xp( 0 ), pOnComplete( NULL ), pOnFail( NULL ), pFailConds( NULL ) {}
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
	// HL2RPM: experience for completing, actions on completion / failure, conditions that fail
	// the active quest (blocks owned by RPMLogic::POOL_QUESTS)
	int xp;
	KeyValues *pOnComplete;
	KeyValues *pOnFail;
	KeyValues *pFailConds;
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

	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPostEntity();
	virtual void FireGameEvent( IGameEvent *event );

	// HL2RPM: quests and reputation are part of the save game and survive level
	// transitions; a new game clears them
	void ClearPlayerData();
	void PrintState() const;
	void SaveState( class ISave *pSave );
	void RestoreState( class IRestore *pRestore );

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

	// HL2RPM: objectives of the stages (Creation Kit)
	void ForceMarkers() { m_LastMarkers = "-"; m_flNextMarkers = 0.0f; }	// send the HUD markers again now
	void Think( CBasePlayer *player );		// 4 times a second: collect / go / cond objectives, fail conditions, HUD markers
	void OnTalk( CBasePlayer *player, CBaseEntity *npc );
	void OnUse( CBasePlayer *player, CBaseEntity *ent );
	bool CompleteObjective( CBasePlayer *player, const char *questId, const char *objId );
	bool IsObjectiveDone( const char *questId, const char *objId ) const;
	int ObjectiveProgress( const char *questId, const char *objId ) const;
	// tests (rpm_test_state): set a quest without running its actions
	void DebugSetQuest( CBasePlayer *player, const char *questId, QuestState state, int stage );

private:
	void EnterStage( CBasePlayer *player, const char *questId, int stage );
	void SetObjectiveProgress( CBasePlayer *player, const QuestDef &def, const QuestObjDef &obj, int progress );
	void ClearObjectives( const char *questId );
	bool StageDone( const QuestDef &def, int stage ) const;
	void AdvanceIfDone( CBasePlayer *player, const char *questId );
	void SendMarkers( CBasePlayer *player );
	CUtlString m_LastMarkers;
	CUtlString m_LastTracked;		// what the client was told is tracked (rpm_quest_tracked)
	float m_flNextMarkers = 0.0f;
	int m_iThinkDepth = 0;

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

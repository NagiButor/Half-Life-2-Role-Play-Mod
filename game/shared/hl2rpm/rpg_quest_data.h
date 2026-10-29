//========= HL2RPM ============//
//
// RPG-система: квесты v2 (RPG_SPEC §6) - определения из MOD/scripts/quests/*.txt.
// Грузятся на сервере (логика) и на клиенте (журнал браслета, компас).
//
// Состояние квеста живёт во флагах персонажа (сохраняется, переживает переходы, уходит клиенту
// через RPG_State сам собой):
//   q_<id>        состояние: нет флага/0 - не выдан, 1 активен, 2 выполнен, 3 провален
//   q_<id>_s      текущая стадия
//   q_<id>_o<n>   прогресс цели n текущей стадии (для kill/talk/reach/event - счётчик)
//   q_track       отслеживаемый квест (строка)
//
//=============================//

#ifndef RPG_QUEST_DATA_H
#define RPG_QUEST_DATA_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_conditions.h"

class CRPGCharacterData;

enum RPGQuestState_t
{
	RPG_QUEST_INACTIVE = 0,
	RPG_QUEST_ACTIVE = 1,
	RPG_QUEST_COMPLETED = 2,
	RPG_QUEST_FAILED = 3,
};

enum RPGQuestCategory_t
{
	RPG_QCAT_MAIN = 0,
	RPG_QCAT_FACTION,
	RPG_QCAT_SIDE,
	RPG_QCAT_COMPANION,
	RPG_QCAT_COUNT
};

enum RPGObjectiveType_t
{
	RPG_OBJ_TALK = 0,	// конец разговора с NPC (target - targetname)
	RPG_OBJ_REACH,		// trigger_rpg quest_event reach:<target>
	RPG_OBJ_EVENT,		// logic_rpg QuestEvent / эффект quest_event event:<target>
	RPG_OBJ_KILL,		// убить NPC (target - имена через ';' или classname), count
	RPG_OBJ_ITEM,		// иметь предмет (target - classname), count
	RPG_OBJ_FLAG,		// выражение условий §3 (обычно имя флага)
	RPG_OBJ_COUNT
};

struct RPGQuestObjective_t
{
	int type;					// RPGObjectiveType_t
	CUtlString target;
	CUtlVector<CUtlString> targets;	// target, разобранный по ';'
	int count;
	bool optional;
	CUtlString title;			// подпись цели в журнале (токен; может быть пустой)
	RPGConditionList_t cond;	// для flag
};

struct RPGQuestStage_t
{
	CUtlString title, desc;
	CUtlString marker;			// targetname для компаса
	CUtlString markerMap;		// карта, на которой стоит метка
	CUtlVector<RPGQuestObjective_t> objectives;
	RPGEffectList_t onComplete;
	int xp;
};

struct RPGQuestDef_t
{
	CUtlString id, title, desc, faction;
	int category;				// RPGQuestCategory_t
	int xp;
	int act;
	bool autoTrack;
	bool completeOnLastStage;
	struct Reward_t { CUtlString classname; int count; };
	CUtlVector<Reward_t> rewards;
	RPGEffectList_t onGrant, onComplete, onFail;
	CUtlVector<RPGQuestStage_t> stages;
};

class CRPGQuestData
{
public:
	void Load();
	void EnsureLoaded() { if ( !m_bLoaded ) Load(); }

	int Count() const { return m_Quests.Count(); }
	const RPGQuestDef_t &Get( int i ) const { return m_Quests[i]; }
	const RPGQuestDef_t *Find( const char *pszId ) const;

	static const char *CategoryKey( int cat );
	static int CategoryFromKey( const char *pszKey );
	static int ObjectiveTypeFromKey( const char *pszKey );

private:
	bool ParseQuest( KeyValues *pKV, const char *pszFile );

	bool m_bLoaded = false;
	CUtlVector<RPGQuestDef_t> m_Quests;
};

CRPGQuestData &RPGQuestData();

// --- состояние из флагов (сервер и клиент) ---
int RPGQuest_GetState( const CRPGCharacterData &d, const char *pszId );
int RPGQuest_GetStage( const CRPGCharacterData &d, const char *pszId );
int RPGQuest_GetProgress( const CRPGCharacterData &d, const char *pszId, int iObjective );
const char *RPGQuest_GetTracked( const CRPGCharacterData &d );

#define RPG_QFLAG_TRACK "q_track"
void RPGQuest_FlagName( char *pszOut, int nOut, const char *pszId, const char *pszSuffix = NULL );

#endif // RPG_QUEST_DATA_H

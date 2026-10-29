//========= HL2RPM ============//
//
// RPG-система (сервер): персонаж игрока - источник истины (RPG_SPEC §0, §1).
// Состояние живёт в памяти server.dll (переживает trigger_changelevel), в сохранении -
// блок HL2RPMCharacter, сбрасывается только на MapLoad_NewGame. Клиенту - RPG_State/RPG_Notify.
//
// API для диалогов/квестов/интерфейса - см. game/server/hl2rpm/README.md.
//
//=============================//

#ifndef RPG_CHARACTER_H
#define RPG_CHARACTER_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_character_data.h"
#include "hl2rpm/rpg_conditions.h"

class CBasePlayer;
class CBaseEntity;
class CAI_BaseNPC;
class ISave;
class IRestore;
class ISaveRestoreBlockHandler;

// ---------------------------------------------------------------------------
// Слушатель событий персонажа (квесты v2, достижения, HUD-логика сервера)
// ---------------------------------------------------------------------------
class IRPGListener
{
public:
	// quest_event эффекта / trigger_rpg / logic_rpg QuestEvent: type talk/reach/event/..., target
	virtual void OnRPGQuestEvent( CBasePlayer *pPlayer, const char *pszType, const char *pszTarget ) {}
	// флаг установлен/изменён/снят
	virtual void OnRPGFlagChanged( CBasePlayer *pPlayer, const char *pszFlag ) {}
	// предметы выданы/забраны эффектами give_item/take_item (инвентарь изменился)
	virtual void OnRPGItemsChanged( CBasePlayer *pPlayer, const char *pszClassname, int iDelta ) {}
	virtual void OnRPGLevelUp( CBasePlayer *pPlayer, int iNewLevel ) {}
	virtual void OnRPGSuspicionTier( CBasePlayer *pPlayer, int iOldTier, int iNewTier ) {}
	virtual void OnRPGLocationDiscovered( CBasePlayer *pPlayer, const char *pszLocation ) {}
	virtual void OnRPGChargenDone( CBasePlayer *pPlayer ) {}
	// убийство (после начисления XP): victim - энтити
	virtual void OnRPGKill( CBasePlayer *pPlayer, CBaseEntity *pVictim ) {}
};

void RPG_AddListener( IRPGListener *pListener );
void RPG_RemoveListener( IRPGListener *pListener );

// ---------------------------------------------------------------------------
// Персонаж
// ---------------------------------------------------------------------------
class CRPGCharacter
{
public:
	CRPGCharacter();

	const CRPGCharacterData &Data() const { return m_Data; }

	// новая игра: начальный персонаж (данные уже загружены)
	void Reset();

	// ---- изменения (значения ограничиваются, клиенту уходят записи, уведомления) ----
	void SetName( const char *pszName );
	void SetAttr( int attr, int value );
	void AddXP( int amount, const char *pszSource = NULL, bool bNotify = true );
	void SetLevel( int level );						// rpg_set level: XP подтягивается к уровню
	void SetSkillPoints( int value );
	void SetPerkPicks( int value );
	bool InvestSkill( int skill, int points, CUtlString *pError = NULL );	// тратит skillPoints
	void SetSkillInvested( int skill, int value );	// без проверок (rpg_set)
	void SetSkillTagged( int skill, bool bTagged );
	bool CanPickPerk( const char *pszPerk, CUtlString *pReason = NULL ) const;
	bool PickPerk( const char *pszPerk, CUtlString *pError = NULL );		// тратит perkPicks
	void SetPerkRank( const char *pszPerk, int rank );						// без проверок
	void SetTrait( const char *pszTrait, bool bOn );
	void AddCredits( int delta, bool bNotify = true );
	void SetCredits( int value );
	void AddCivil( int delta, bool bNotify = true );
	void SetCivil( int value );
	// bScaled: умножается на suspicion_gain (TR-04, PK-25)
	void AddSuspicion( float delta, bool bScaled = true );
	void SetSuspicion( float value );
	void AddHumanity( int delta, bool bNotify = true );
	void AddResonance( int delta );
	void AddInfluence( int delta );
	void SetHumanity( int value );
	void SetResonance( int value );
	void SetInfluence( int value );
	void AddRep( const char *pszFaction, int delta, bool bNotify = true );
	void SetRep( const char *pszFaction, int value );
	void SetFlag( const char *pszFlag, int value );
	void SetFlagString( const char *pszFlag, const char *pszValue );
	void ClearFlag( const char *pszFlag );
	void AddFlag( const char *pszFlag, int delta );
	void SetDisposition( const char *pszNPC, int value );
	void AddDisposition( const char *pszNPC, int delta, int iStart = 0 );
	bool AddNote( const char *pszNote, bool bNotify = true );		// true - новая (+XP)
	bool Discover( const char *pszLocation, bool bNotify = true );	// true - новая (+XP)
	void SetAct( int act );
	void SetChargenDone( bool bDone );
	// создание персонажа: характеристики, теги, черты; стартовые модификаторы черт
	bool CommitChargen( const char *pszName, const int *pAttrs, const char *const *ppTags, int nTags,
		const char *const *ppTraits, int nTraits, bool bForce, CUtlString *pError );

	// состояние мира (ведёт система)
	void SetZone( int zone );
	void SetAlarm( bool bAlarm );
	void SetArmorClass( const char *pszClass );

	// ---- сеть ----
	void QueueFullSync();
	bool IsFullSyncPending() const { return m_bFullSyncPending; }
	void Flush( CBasePlayer *pPlayer );		// раз в кадр
	void Notify( int kind, const char *pszToken, int iValue = 0, const char *pszExtra = NULL );

	// ---- сохранение ----
	void Save( ISave *pSave );
	void Restore( IRestore *pRestore );

	// ---- служебное ----
	int CountKill( const char *pszClass, int iDay );	// убийств этого класса сегодня (после учёта этого)
	float m_flLastClockHours;							// для счёта игровых дней
	bool m_bHaveClock;

private:
	void QueueRecord( const RPGRecord_t &rec );
	void QueueStat( const char *pszStat );
	void CheckLevelUp();
	void ApplySuspicionTier( int oldTier );
	void FlagChanged( const char *pszFlag );

	CRPGCharacterData m_Data;

	CUtlVector<RPGRecord_t> m_Pending;
	struct PendingNotify_t
	{
		int kind;
		CUtlString token, extra;
		int value;
	};
	CUtlVector<PendingNotify_t> m_PendingNotify;
	bool m_bFullSyncPending;
	int m_iLastSentSuspicion;

	CUtlDict<int, int> m_KillCounts;	// classname -> убито за день m_iKillDay
	int m_iKillDay;
};

// персонаж игрока (одиночная игра - один)
CRPGCharacter *RPG_GetCharacter( CBasePlayer *pPlayer = NULL );
CBasePlayer *RPG_GetPlayer();

// ---------------------------------------------------------------------------
// Удобные функции для диалогов/квестов/энтити
// ---------------------------------------------------------------------------
// проверить выражение условий §3
bool RPG_TestCondition( const char *pszExpr, CBasePlayer *pPlayer, CBaseEntity *pNPC = NULL, CUtlString *pReason = NULL );
// применить строку эффектов §4 ("xp=25, set_flag=x")
void RPG_ApplyEffects( const char *pszEffects, CBasePlayer *pPlayer, CBaseEntity *pNPC = NULL, CBaseEntity *pCaller = NULL );
// событие для квестов (talk:<npc>, reach:<name>, event:<name>) - уходит слушателям
void RPG_QuestEvent( CBasePlayer *pPlayer, const char *pszType, const char *pszTarget );
// открыть экран интерфейса (RPG_NOTIFY_OPEN_UI): chargen, levelup, barter, note, terminal, read, sleep
void RPG_OpenUI( CBasePlayer *pPlayer, const char *pszScreen, const char *pszParam = NULL, CBaseEntity *pSource = NULL );
// торговля: открытый экран «barter» (id торговца, NPC; NULL - сессии нет или торговец ушёл/умер)
const char *RPG_GetBarterTrader( CBasePlayer *pPlayer, CBaseEntity **ppNPC = NULL );
// купить у открытого торговца / продать ему; false + причина (токен) - отказ
bool RPG_BarterBuy( CBasePlayer *pPlayer, const char *pszClass, int count, CUtlString *pError );
bool RPG_BarterSell( CBasePlayer *pPlayer, const char *pszClass, int count, CUtlString *pError );
// выдать/забрать предметы (инвентарь + уведомление + событие слушателям)
void RPG_GiveItem( CBasePlayer *pPlayer, const char *pszClassname, int count, bool bNotify = true );
int RPG_TakeItem( CBasePlayer *pPlayer, const char *pszClassname, int count, bool bNotify = true );	// сколько забрано
int RPG_CountItem( CBasePlayer *pPlayer, const char *pszClassname );
// оповестить слушателей об изменении инвентаря (если инвентарь меняли в обход RPG_GiveItem)
void RPG_FireItemsChanged( CBasePlayer *pPlayer, const char *pszClassname, int iDelta );
// классы предметов из items.txt и прекэш их моделей (на загрузке карты)
void RPG_InstallItemFactories();
void RPG_PrecacheItems();

// NPC: ключи rpg_faction / rpg_trader / rpg_disposition
const char *RPG_GetNPCFaction( CBaseEntity *pEnt );
const char *RPG_GetNPCTrader( CBaseEntity *pEnt );
// ГО/Надзор/сканеры - «наблюдатели» для подозрения
bool RPG_IsWatcher( CBaseEntity *pEnt );

// тип костюма по classname надетого костюма
int RPG_SuitTypeForClass( const char *pszSuitClass, bool bWearingSuit );
// синхронизировать m_iSuitType игрока с надетым костюмом
void RPG_UpdatePlayerSuitType( CBasePlayer *pPlayer );

// предел веса инвентаря (InventorySystem::GetPlayerMaxCarryWeight)
float RPG_GetCarryWeight( CBasePlayer *pPlayer );
// вес единицы предмета из items.txt; < 0 - не задан
float RPG_GetItemWeight( const char *pszClassname );
// inventory_use: предмет RPG (еда, записка, костюм...) - true, если обработано здесь
bool RPG_HandleInventoryUse( CBasePlayer *pPlayer, const char *pszClassname );

// время: сдвинуть часы мира (сон)
void RPG_AdvanceClock( float flHours );

ISaveRestoreBlockHandler *GetRPGSaveRestoreBlockHandler();

#endif // RPG_CHARACTER_H

//========= HL2RPM ============//
//
// RPG-система (клиент): зеркало персонажа для интерфейса. Заполняется сообщениями
// RPG_State (полная синхронизация + изменения), уведомления - RPG_Notify.
// Сервер - источник истины: интерфейс только читает отсюда и шлёт команды
// (rpg_chargen_commit, rpg_skill_add, rpg_perk_pick, rpg_sleep - см. server/hl2rpm/README.md).
//
//=============================//

#ifndef C_RPG_CHARACTER_H
#define C_RPG_CHARACTER_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_character_data.h"
#include "hl2rpm/rpg_conditions.h"

// Слушатель для экранов интерфейса (HUD, браслет, создание персонажа, диалог...)
class IRPGClientListener
{
public:
	// одна запись изменилась (RPGRecordType_t, ключ записи)
	virtual void OnRPGStateChanged( int iRecordType, const char *pszKey ) {}
	// пришло полное состояние (загрузка, переход, спавн, rpg_request_state)
	virtual void OnRPGFullUpdate() {}
	// уведомление (RPGNotifyKind_t), кроме RPG_NOTIFY_OPEN_UI
	virtual void OnRPGNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra ) {}
	// сервер просит открыть экран: chargen, levelup, barter, note, terminal, read, sleep.
	// true - экран открыт этим слушателем (временная обработка по умолчанию не нужна)
	virtual bool OnRPGOpenUI( const char *pszScreen, const char *pszParam, int iSourceEntIndex ) { return false; }
};

class C_RPGCharacter
{
public:
	C_RPGCharacter();

	// все поля и производные значения (формулы те же, что на сервере)
	const CRPGCharacterData &Data() const { return m_Data; }
	// было ли хоть одно полное состояние с сервера
	bool HasState() const { return m_bHasState; }

	// ---- удобные геттеры ----
	const char *GetName() const { return m_Data.m_Name.Get(); }		// может быть токеном "#RPG_Name_Default"
	int GetLevel() const { return m_Data.m_iLevel; }
	int GetXP() const { return m_Data.m_iXP; }
	int GetXPForNextLevel() const { return m_Data.GetXPForNextLevel(); }
	int GetSkillPoints() const { return m_Data.m_iSkillPoints; }
	int GetPerkPicks() const { return m_Data.m_iPerkPicks; }
	int GetCredits() const { return m_Data.m_iCredits; }
	int GetCivil() const { return m_Data.m_iCivil; }
	int GetCivilTier() const { return m_Data.GetCivilTier(); }
	bool HasTempPass() const { return m_Data.HasTempPass(); }
	float GetSuspicion() const { return m_Data.m_flSuspicion; }
	int GetSuspicionTier() const { return m_Data.GetSuspicionTier(); }
	int GetHumanity() const { return m_Data.m_iHumanity; }
	int GetHumanityTier() const { return m_Data.GetHumanityTier(); }
	int GetResonance() const { return m_Data.m_iResonance; }
	int GetInfluence() const { return m_Data.m_iInfluence; }
	int GetAttr( int attr ) const { return m_Data.GetAttr( attr ); }
	int GetAttrBase( int attr ) const { return m_Data.GetAttrBase( attr ); }
	int GetSkill( int skill ) const { return m_Data.GetSkill( skill ); }
	int GetSkill( const char *pszSkill ) const { return m_Data.GetSkill( pszSkill ); }
	int GetSkillBase( int skill ) const { return m_Data.GetSkillBase( skill ); }
	int GetSkillInvested( int skill ) const { return m_Data.GetSkillInvested( skill ); }
	bool IsSkillTagged( int skill ) const { return m_Data.IsSkillTagged( skill ); }
	bool HasTrait( const char *pszId ) const { return m_Data.HasTrait( pszId ); }
	int GetPerkRank( const char *pszId ) const { return m_Data.GetPerkRank( pszId ); }
	int GetFaction( const char *pszId ) const { return m_Data.GetFaction( pszId ); }
	int GetFactionTier( const char *pszId ) const { return m_Data.GetFactionTier( pszId ); }
	bool IsFlagSet( const char *pszFlag ) const { return m_Data.IsFlagSet( pszFlag ); }
	int GetFlagInt( const char *pszFlag, int iDefault = 0 ) const { return m_Data.GetFlagInt( pszFlag, iDefault ); }
	const char *GetFlagString( const char *pszFlag ) const { return m_Data.GetFlagString( pszFlag ); }
	int GetDisposition( const char *pszNPC, int iDefault = 0 ) const { return m_Data.GetDisposition( pszNPC, iDefault ); }
	bool HasNote( const char *pszNote ) const { return m_Data.HasNote( pszNote ); }
	bool HasLocation( const char *pszLocation ) const { return m_Data.HasLocation( pszLocation ); }
	bool IsChargenDone() const { return m_Data.m_bChargenDone; }
	int GetZone() const { return m_Data.m_iZone; }					// RPGZone_t: «глаз» на HUD
	bool IsAlarm() const { return m_Data.m_bAlarm; }
	const char *GetArmorClass() const { return m_Data.m_ArmorClass.Get(); }
	int GetSuitType() const;										// RPGSuitType_t (сетевая переменная игрока)
	int GetMaxHealth() const { return m_Data.GetMaxHealth(); }
	float GetCarryWeight() const { return m_Data.GetCarryWeight(); }
	int GetArmor() const { return m_Data.GetArmor(); }
	int GetAct() const { return m_Data.GetAct(); }

	// ---- слушатели ----
	void AddListener( IRPGClientListener *pListener );
	void RemoveListener( IRPGClientListener *pListener );

	// ---- служебное ----
	// экран интерфейса: первый слушатель, который его открыл, иначе - подсказка в консоль
	void OpenUI( const char *pszScreen, const char *pszParam, int iSource );
	void OnStateMessage( bf_read &msg );
	void OnNotifyMessage( bf_read &msg );
	void Reset();

private:
	void ShowTemporaryNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra );
	void DefaultOpenUI( const char *pszScreen, const char *pszParam, int iSource );

	CRPGCharacterData m_Data;
	bool m_bHasState;
	bool m_bInFullUpdate;
	CUtlVector<IRPGClientListener *> m_Listeners;
};

C_RPGCharacter &RPGClient();

// Текст уведомления RPG_Notify (UTF-8) для ленты HUD и консоли; false - показывать нечего
bool RPG_FormatNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra, char *pszOut, int nOut );

// Локализованный текст токена в UTF-8 (нет токена - pszFallback или сам токен без '#')
const char *RPG_LocalizeUTF8( const char *pszToken, char *pszOut, int nOutSize, const char *pszFallback = NULL );

#endif // C_RPG_CHARACTER_H

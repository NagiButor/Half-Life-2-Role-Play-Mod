//========= HL2RPM ============//
//
// RPG-система: данные персонажа (RPG_SPEC §1) и производные значения по формулам.
// Один класс на обеих сторонах: на сервере его меняет CRPGCharacter, на клиенте
// C_RPGCharacter заполняет его записями RPG_State. Сохранение - те же записи.
//
//=============================//

#ifndef RPG_CHARACTER_DATA_H
#define RPG_CHARACTER_DATA_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_shared.h"
#include "hl2rpm/rpg_data.h"
#include "utldict.h"

// флаг: число или строка (mq02_path=lambda)
struct RPGFlagValue_t
{
	int iValue;
	CUtlString sValue;

	RPGFlagValue_t() : iValue( 0 ) {}
	bool IsString() const { return !sValue.IsEmpty(); }
	bool IsSet() const { return IsString() || iValue != 0; }
};

class CRPGCharacterData
{
public:
	CRPGCharacterData();

	// новый персонаж (новая игра): характеристики 5, фракции из factions.txt...
	void Reset();

	// ---- поля (RPG_SPEC §1) ----
	CUtlString m_Name;							// "#RPG_Name_Default" до создания персонажа
	int m_Attr[RPG_ATTR_COUNT];					// база 1..10
	int m_SkillInvested[RPG_MAX_SKILLS];		// индекс = RPGData().FindSkill
	bool m_SkillTagged[RPG_MAX_SKILLS];
	CUtlVector<CUtlString> m_Traits;			// TR-xx
	CUtlDict<int, int> m_Perks;					// PK-xx -> ранг
	int m_iLevel, m_iXP, m_iSkillPoints, m_iPerkPicks;
	int m_iCredits;
	int m_iCivil;								// -100..100
	float m_flSuspicion;						// 0..100
	int m_iHumanity, m_iResonance, m_iInfluence;
	CUtlDict<int, int> m_Factions;				// id -> -100..100
	CUtlDict<RPGFlagValue_t, int> m_Flags;
	CUtlDict<int, int> m_Dispositions;			// targetname NPC -> значение
	CUtlVector<CUtlString> m_Notes;
	CUtlVector<CUtlString> m_Locations;
	bool m_bChargenDone;

	// состояние мира, которое ведёт сервер (тоже в сети - для HUD)
	int m_iZone;								// RPGZone_t
	bool m_bAlarm;								// тревога (подозрение достигло 100)
	CUtlString m_ArmorClass;					// classname надетого костюма

	// ---- производные (формулы) ----
	int GetAttrBase( int attr ) const;
	int GetAttr( int attr ) const;				// с модификаторами, 1..12
	int GetSkillBase( int skill ) const;		// по характеристикам
	int GetSkill( int skill, bool bForCheck = false ) const;	// итог, до 100 (110 для проверок)
	int GetSkill( const char *pszSkill, bool bForCheck = false ) const;
	int GetSkillInvested( int skill ) const;
	bool IsSkillTagged( int skill ) const;
	int GetTagCount() const;
	int GetMaxHealth() const;
	float GetCarryWeight() const;
	int GetArmor() const;
	int GetXPForNextLevel() const;
	int GetCivilTier() const;					// RPGCivilTier_t
	bool HasTempPass() const;
	int GetSuspicionTier() const;
	int GetHumanityTier() const;
	int GetAct() const;							// флаг world_act (1 по умолчанию)

	// сумма прибавок / произведение множителей по всем источникам (черты, перки, броня)
	float GetModAdd( const char *pszKey ) const;
	float GetModMult( const char *pszKey ) const;

	// ---- доступ ----
	bool HasTrait( const char *pszId ) const;
	int GetPerkRank( const char *pszId ) const;
	int GetFaction( const char *pszId ) const;		// нет в словаре - старт из factions.txt
	int GetFactionTier( const char *pszId ) const;
	bool HasDisposition( const char *pszNPC ) const;
	int GetDisposition( const char *pszNPC, int iDefault = 0 ) const;
	bool HasNote( const char *pszId ) const;
	bool HasLocation( const char *pszId ) const;

	const RPGFlagValue_t *FindFlag( const char *pszName ) const;
	bool IsFlagSet( const char *pszName ) const;
	int GetFlagInt( const char *pszName, int iDefault = 0 ) const;
	const char *GetFlagString( const char *pszName ) const;	// число - как текст

	// ---- торговля (RPG_SPEC §1 «Цены»): касса и склад торговца живут во флагах персонажа -
	// сохраняются и доходят до клиента вместе с ним. trd_<id>_cr = касса + 1,
	// trd_<id>.<classname> = количество + 1; 0 (флага нет) - как в traders.txt ----
	int GetTraderCredits( const RPGTraderDef_t &t ) const;
	int GetTraderStock( const RPGTraderDef_t &t, const char *pszClass ) const;
	void GetTraderStockList( const RPGTraderDef_t &t, CUtlVector<RPGTraderItem_t> &out ) const;	// count > 0
	int GetTraderBuyPrice( const RPGTraderDef_t &t, const char *pszClass ) const;	// игрок платит; -1 - не продаётся
	int GetTraderSellPrice( const RPGTraderDef_t &t, const char *pszClass ) const;	// торговец платит; -1 - не купит
	static void TraderCreditsFlag( const char *pszTrader, char *pszOut, int nOut );
	static void TraderStockFlag( const char *pszTrader, const char *pszClass, char *pszOut, int nOut );
	// торг (эффект haggle): haggle_<id> = % скидки, haggle_<id>_day = день торга (действует до конца суток)
	static void TraderHaggleFlag( const char *pszTrader, bool bDay, char *pszOut, int nOut );
	int GetTraderHaggle( const RPGTraderDef_t &t ) const;

	// ---- записи (сеть / сохранение) ----
	void BuildRecords( CUtlVector<RPGRecord_t> &out ) const;
	// применить одну запись; false - неизвестный тип
	bool ApplyRecord( const RPGRecord_t &rec );
	// значение stat по имени (для rpg_set / записей); false - нет такого
	bool GetStat( const char *pszStat, int &iValue ) const;
	bool SetStatRaw( const char *pszStat, int iValue );

	void Print( const char *pszPrefix ) const;

private:
	void AccumulateMods( const char *pszKey, float &flAdd, float &flMul ) const;
};

static const char *const g_pszRPGStatNames[] =
{
	"level", "xp", "skillpoints", "perkpicks", "credits", "civil", "humanity",
	"resonance", "influence", "chargen", "suspicion", "zone", "alarm", NULL
};

#endif // RPG_CHARACTER_DATA_H

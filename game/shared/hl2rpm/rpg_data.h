//========= HL2RPM ============//
//
// RPG-система: данные из MOD/scripts/rpg/*.txt (KeyValues, UTF-8 без BOM).
// Грузятся и на сервере, и на клиенте (интерфейс берёт отсюда списки и тексты).
//   attributes.txt skills.txt traits.txt perks.txt factions.txt items.txt
//   traders.txt locations.txt kills.txt
//
//=============================//

#ifndef RPG_DATA_H
#define RPG_DATA_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_shared.h"
#include "utldict.h"

class KeyValues;

// Модификатор черты/перка/брони: "skill:speech" "+10", "suspicion_gain" "x0.75"
struct RPGMod_t
{
	CUtlString key;
	bool bMul;		// множитель (xK) или прибавка (±N)
	float value;
};
typedef CUtlVector<RPGMod_t> RPGModList_t;

struct RPGAttrDef_t
{
	CUtlString id, abbr, name, desc, icon;
};

struct RPGSkillDef_t
{
	CUtlString id, name, desc, icon;
	int primary;	// RPGAttribute_t
	int secondary;
};

struct RPGTraitDef_t
{
	CUtlString id, slug, name, desc, icon;
	RPGModList_t mods;
};

struct RPGPerkDef_t
{
	CUtlString id, slug, name, desc, flavor, icon;
	CUtlString require;		// условия RPG_SPEC §3 (выражение)
	int level;				// минимальный уровень
	int ranks;
	RPGModList_t mods;		// на каждый ранг
};

struct RPGFactionDef_t
{
	CUtlString id, name, icon;
	int start;
	bool hidden;		// не показывать, пока не было изменений
	bool service;		// служебная (cp, overwatch...): без шкалы
};

struct RPGItemDef_t
{
	CUtlString classname, category, name, desc, icon, model, use, armor;
	float kg;			// < 0 - не задан (берётся scripts/inventory_weights.txt)
	int value;			// базовая цена, кр.
	int contraband;		// 0..3
	int skin;
	RPGModList_t mods;	// для брони - пока надета
};

struct RPGTraderItem_t
{
	CUtlString classname;
	int count;
};

struct RPGTraderDef_t
{
	CUtlString id, name, faction;
	int credits;
	float buyMult, sellMult, restockHours;
	bool acceptsContraband;
	CUtlVector<RPGTraderItem_t> items;
};

struct RPGLocationDef_t
{
	CUtlString id, name, map;
	int xp;
};

struct RPGKillDef_t
{
	CUtlString classname;
	int xp;
	int humanity;
};

class CRPGData
{
public:
	CRPGData();

	// перечитать все файлы (вызывается на каждой загрузке карты и rpg_reload)
	void Load();
	void EnsureLoaded() { if ( !m_bLoaded ) Load(); }
	bool IsLoaded() const { return m_bLoaded; }

	// характеристики: индекс = RPGAttribute_t
	const RPGAttrDef_t &GetAttr( int i ) const { return m_Attrs[i]; }
	int FindAttr( const char *pszIdOrAbbr ) const;

	int GetSkillCount() const { return m_Skills.Count(); }
	const RPGSkillDef_t &GetSkill( int i ) const { return m_Skills[i]; }
	int FindSkill( const char *pszId ) const;

	int GetTraitCount() const { return m_Traits.Count(); }
	const RPGTraitDef_t &GetTrait( int i ) const { return m_Traits[i]; }
	const RPGTraitDef_t *FindTrait( const char *pszId ) const;

	int GetPerkCount() const { return m_Perks.Count(); }
	const RPGPerkDef_t &GetPerk( int i ) const { return m_Perks[i]; }
	const RPGPerkDef_t *FindPerk( const char *pszId ) const;

	int GetFactionCount() const { return m_Factions.Count(); }
	const RPGFactionDef_t &GetFaction( int i ) const { return m_Factions[i]; }
	const RPGFactionDef_t *FindFaction( const char *pszId ) const;

	int GetItemCount() const { return m_Items.Count(); }
	const RPGItemDef_t &GetItem( int i ) const { return m_Items[i]; }
	const RPGItemDef_t *FindItem( const char *pszClassname ) const;

	int GetTraderCount() const { return m_Traders.Count(); }
	const RPGTraderDef_t &GetTrader( int i ) const { return m_Traders[i]; }
	const RPGTraderDef_t *FindTrader( const char *pszId ) const;

	int GetLocationCount() const { return m_Locations.Count(); }
	const RPGLocationDef_t &GetLocation( int i ) const { return m_Locations[i]; }
	const RPGLocationDef_t *FindLocation( const char *pszId ) const;

	const RPGKillDef_t *FindKill( const char *pszClassname ) const;
	int GetKillDailyLimit() const { return m_iKillDailyLimit; }

	// разбор блока mods; bWarn - предупреждать о неизвестных ключах
	static void ParseMods( KeyValues *pMods, RPGModList_t &out, const char *pszOwner );
	static bool IsKnownModKey( const char *pszKey );

private:
	void SetDefaults();
	void LoadAttributes();
	void LoadSkills();
	void LoadTraits();
	void LoadPerks();
	void LoadFactions();
	void LoadItems();
	void LoadTraders();
	void LoadLocations();
	void LoadKills();

	bool m_bLoaded;
	RPGAttrDef_t m_Attrs[RPG_ATTR_COUNT];
	CUtlVector<RPGSkillDef_t> m_Skills;
	CUtlVector<RPGTraitDef_t> m_Traits;
	CUtlVector<RPGPerkDef_t> m_Perks;
	CUtlVector<RPGFactionDef_t> m_Factions;
	CUtlVector<RPGItemDef_t> m_Items;
	CUtlDict<int, int> m_ItemIndex;
	CUtlVector<RPGTraderDef_t> m_Traders;
	CUtlVector<RPGLocationDef_t> m_Locations;
	CUtlVector<RPGKillDef_t> m_Kills;
	int m_iKillDailyLimit;
};

CRPGData &RPGData();

#endif // RPG_DATA_H

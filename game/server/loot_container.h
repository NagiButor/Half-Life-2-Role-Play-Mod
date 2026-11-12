#ifndef LOOT_CONTAINER_H
#define LOOT_CONTAINER_H

#include "cbase.h"
#include "triggers.h"
#include "props.h"

// HL2RPM: anything the loot window can open (func_loot_container, prop_loot_container)
class ILootContainer
{
public:
	virtual const char *GetLootItems() const = 0;
	virtual void SetLootItems(const char *items) = 0;
	virtual const char *GetLootDisplayName() const = 0;
	virtual const char *GetLootSoundOpen() const = 0;
	virtual const char *GetLootSoundClose() const = 0;
	// HL2RPM: whose it is ("" - nobody's): an NPC targetname or a faction - taking is theft (RPMTheft)
	virtual const char *GetLootOwner() const { return ""; }
};

class CFuncLootContainer : public CBaseTrigger, public ILootContainer
{
public:
	DECLARE_CLASS(CFuncLootContainer, CBaseTrigger);
	DECLARE_DATADESC();

	void Spawn(void);
	void Precache(void);

	const char *GetLootItems() const;
	void SetLootItems(const char *items);

	const char *GetLootDisplayName() const;
	const char *GetLootSoundOpen() const;
	const char *GetLootSoundClose() const;
	const char *GetLootOwner() const { return STRING(m_strLootOwner); }

private:
	void LootThink();

	float m_flNextUseAllowed[MAX_PLAYERS + 1];

	string_t m_strLootItems;
	string_t m_strLootDisplayName;
	string_t m_strLootSoundOpen;
	string_t m_strLootSoundClose;
	string_t m_strLootOwner;
};

// HL2RPM: a lootable model (crate, locker, fridge - containers of the Creation Kit):
// the player opens it with USE
class CPropLootContainer : public CDynamicProp, public ILootContainer
{
public:
	DECLARE_CLASS(CPropLootContainer, CDynamicProp);
	DECLARE_DATADESC();

	void Spawn(void);
	void Precache(void);
	virtual int ObjectCaps(void) { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; }
	// HL2RPM (M6): the lock for the use prompt (hl2rpm_prompt.cpp)
	bool IsLootLocked() const { return m_bLocked; }
	int GetLockLevel() const { return m_iLockLevel; }
	const char *GetLockKey() const { return STRING(m_strLockKey); }
	// any model, with or without prop_data (crates, lockers, fridges)
	bool OverridePropdata(void) { return true; }
	void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

	const char *GetLootItems() const { return STRING(m_strLootItems); }
	void SetLootItems(const char *items) { m_strLootItems = (items && items[0]) ? AllocPooledString(items) : NULL_STRING; }
	const char *GetLootDisplayName() const;
	const char *GetLootSoundOpen() const { return STRING(m_strLootSoundOpen); }
	const char *GetLootSoundClose() const { return STRING(m_strLootSoundClose); }
	const char *GetLootOwner() const { return STRING(m_strLootOwner); }

	void InputLock(inputdata_t &inputdata);
	void InputUnlock(inputdata_t &inputdata);

private:
	string_t m_strLootItems;
	string_t m_strLootDisplayName;
	string_t m_strLootSoundOpen;
	string_t m_strLootSoundClose;
	// HL2RPM: a lock (key item and / or lockpick skill) and restocking (Creation Kit)
	string_t m_strLockKey;
	string_t m_strLootOwner;
	int m_iLockLevel;
	bool m_bLocked;
	float m_flRestockHours;
	string_t m_strOriginalLoot;
	float m_flLastRestock;
	COutputEvent m_OnUnlocked;
	COutputEvent m_OnLockedUse;
};

#endif


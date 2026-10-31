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

private:
	void LootThink();

	float m_flNextUseAllowed[MAX_PLAYERS + 1];

	string_t m_strLootItems;
	string_t m_strLootDisplayName;
	string_t m_strLootSoundOpen;
	string_t m_strLootSoundClose;
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
	// any model, with or without prop_data (crates, lockers, fridges)
	bool OverridePropdata(void) { return true; }
	void Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value);

	const char *GetLootItems() const { return STRING(m_strLootItems); }
	void SetLootItems(const char *items) { m_strLootItems = (items && items[0]) ? AllocPooledString(items) : NULL_STRING; }
	const char *GetLootDisplayName() const;
	const char *GetLootSoundOpen() const { return STRING(m_strLootSoundOpen); }
	const char *GetLootSoundClose() const { return STRING(m_strLootSoundClose); }

private:
	string_t m_strLootItems;
	string_t m_strLootDisplayName;
	string_t m_strLootSoundOpen;
	string_t m_strLootSoundClose;
};

#endif


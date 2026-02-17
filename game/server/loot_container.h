#ifndef LOOT_CONTAINER_H
#define LOOT_CONTAINER_H

#include "cbase.h"
#include "triggers.h"

class CFuncLootContainer : public CBaseTrigger
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

#endif


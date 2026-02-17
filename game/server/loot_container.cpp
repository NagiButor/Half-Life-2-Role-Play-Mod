#include "cbase.h"
#include "loot_container.h"
#include "loot_system.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS(func_loot_container, CFuncLootContainer);

BEGIN_DATADESC(CFuncLootContainer)
	DEFINE_KEYFIELD(m_strLootItems, FIELD_STRING, "loot_items"),
	DEFINE_KEYFIELD(m_strLootDisplayName, FIELD_STRING, "loot_displayname"),
	DEFINE_KEYFIELD(m_strLootSoundOpen, FIELD_STRING, "loot_sound_open"),
	DEFINE_KEYFIELD(m_strLootSoundClose, FIELD_STRING, "loot_sound_close"),
END_DATADESC()

void CFuncLootContainer::Spawn(void)
{
	BaseClass::Spawn();
	InitTrigger();
	Precache();
	for (int i = 0; i <= MAX_PLAYERS; ++i)
		m_flNextUseAllowed[i] = 0.0f;
}

void CFuncLootContainer::Precache(void)
{
	BaseClass::Precache();
	const char *openSnd = GetLootSoundOpen();
	const char *closeSnd = GetLootSoundClose();
	if (openSnd && openSnd[0])
		PrecacheScriptSound(openSnd);
	if (closeSnd && closeSnd[0])
		PrecacheScriptSound(closeSnd);
}

const char *CFuncLootContainer::GetLootItems() const
{
	return STRING(m_strLootItems);
}

void CFuncLootContainer::SetLootItems(const char *items)
{
	if (!items || !items[0])
	{
		m_strLootItems = NULL_STRING;
		return;
	}
	m_strLootItems = AllocPooledString(items);
}

const char *CFuncLootContainer::GetLootDisplayName() const
{
	return STRING(m_strLootDisplayName);
}

const char *CFuncLootContainer::GetLootSoundOpen() const
{
	return STRING(m_strLootSoundOpen);
}

const char *CFuncLootContainer::GetLootSoundClose() const
{
	return STRING(m_strLootSoundClose);
}

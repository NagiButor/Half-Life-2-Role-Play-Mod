#include "cbase.h"
#include "loot_container.h"
#include "loot_system.h"
#include "in_buttons.h"
#include "player.h"
#include "hl2rpm_world.h"
#include "hl2rpm_rpg.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS(func_loot_container, CFuncLootContainer);
LINK_ENTITY_TO_CLASS(prop_loot_container, CPropLootContainer);

BEGIN_DATADESC(CPropLootContainer)
	DEFINE_KEYFIELD(m_strLootItems, FIELD_STRING, "loot_items"),
	DEFINE_KEYFIELD(m_strLootDisplayName, FIELD_STRING, "loot_displayname"),
	DEFINE_KEYFIELD(m_strLootSoundOpen, FIELD_STRING, "loot_sound_open"),
	DEFINE_KEYFIELD(m_strLootSoundClose, FIELD_STRING, "loot_sound_close"),
	DEFINE_KEYFIELD(m_strLockKey, FIELD_STRING, "lock_key"),
	DEFINE_KEYFIELD(m_strLootOwner, FIELD_STRING, "owner"),
	DEFINE_KEYFIELD(m_iLockLevel, FIELD_INTEGER, "lock_level"),
	DEFINE_KEYFIELD(m_flRestockHours, FIELD_FLOAT, "restock_hours"),
	DEFINE_FIELD(m_bLocked, FIELD_BOOLEAN),
	DEFINE_FIELD(m_strOriginalLoot, FIELD_STRING),
	DEFINE_FIELD(m_flLastRestock, FIELD_FLOAT),
	DEFINE_INPUTFUNC(FIELD_VOID, "Lock", InputLock),
	DEFINE_INPUTFUNC(FIELD_VOID, "Unlock", InputUnlock),
	DEFINE_OUTPUT(m_OnUnlocked, "OnUnlocked"),
	DEFINE_OUTPUT(m_OnLockedUse, "OnLockedUse"),
END_DATADESC()

void CPropLootContainer::InputLock(inputdata_t &inputdata)
{
	m_bLocked = true;
}

void CPropLootContainer::InputUnlock(inputdata_t &inputdata)
{
	m_bLocked = false;
}

void CPropLootContainer::Precache(void)
{
	BaseClass::Precache();
	if (m_strLootSoundOpen != NULL_STRING)
		PrecacheScriptSound(STRING(m_strLootSoundOpen));
	if (m_strLootSoundClose != NULL_STRING)
		PrecacheScriptSound(STRING(m_strLootSoundClose));
}

void CPropLootContainer::Spawn(void)
{
	Precache();
	// solid by its collision model unless the mapper said otherwise
	if (GetSolid() == SOLID_NONE)
		SetSolid(SOLID_VPHYSICS);
	BaseClass::Spawn();
	m_bLocked = m_strLockKey != NULL_STRING || m_iLockLevel > 0;
	m_strOriginalLoot = m_strLootItems;
	m_flLastRestock = -1.0f;
}

// a container of the Creation Kit layer: its name in the game's language
const char *CPropLootContainer::GetLootDisplayName() const
{
	const char *psz = RPMWorld::GetRefString(const_cast<CPropLootContainer *>(this), "loot_displayname");
	if (psz && psz[0])
		return psz;
	return STRING(m_strLootDisplayName);
}

void CPropLootContainer::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	CBasePlayer *pPlayer = ToBasePlayer(pActivator);
	if (!pPlayer)
		return;
	if (m_bLocked)
	{
		if (!RPMLock::TryOpen(this, pPlayer, STRING(m_strLockKey), m_iLockLevel))
		{
			m_OnLockedUse.FireOutput(pActivator, this);
			return;
		}
		m_bLocked = false;
		m_OnUnlocked.FireOutput(pActivator, this);
	}
	// restock: back to the original contents (lists rolled again) after the given game hours
	if (m_flRestockHours > 0.0f)
	{
		float now = (float)RPMState::GameHours();
		if (m_flLastRestock < 0.0f)
			m_flLastRestock = now;
		else if (now - m_flLastRestock >= m_flRestockHours)
		{
			SetLootItems(STRING(m_strOriginalLoot));
			m_flLastRestock = now;
		}
	}
	EmitSound("HL2Player.Use");
	OpenLootForPlayer(pPlayer, this);
}


BEGIN_DATADESC(CFuncLootContainer)
	DEFINE_KEYFIELD(m_strLootOwner, FIELD_STRING, "owner"),
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

// loot_system.h -- simple server-side helper to trigger client loot panel
#ifndef LOOT_SYSTEM_H
#define LOOT_SYSTEM_H

#include "cbase.h"

class CRagdollProp;

void OpenLootForPlayer(CBasePlayer *pPlayer, CBaseEntity *pLootEnt);

#endif // LOOT_SYSTEM_H

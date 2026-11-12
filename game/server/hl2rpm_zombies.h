//========= HL2RPM ============================================================//
//
// Zombies that do not all look the same: hl2rpm_zombies.cpp
//
//=============================================================================//
#ifndef HL2RPM_ZOMBIES_H
#define HL2RPM_ZOMBIES_H
#pragma once

class CBaseAnimating;
class CBaseEntity;

enum RPMZombieKind_t
{
	RPMZ_CLASSIC,		// npc_zombie, npc_zombie_torso
	RPMZ_ZOMBINE,		// npc_zombine
	RPMZ_POISON,		// npc_poisonzombie
};

// Precache: every model of the kind (a zombie may be precached before it chooses)
void RPMZombie_Precache( int kind );
// Spawn, before Precache / SetZombieModel: a zombie with the stock model takes a variant and a skin
void RPMZombie_Choose( CBaseAnimating *pZombie, int kind );
// the torso / legs of the zombie's variant (pszStock without one)
const char *RPMZombie_Torso( CBaseAnimating *pZombie, const char *pszStock );
const char *RPMZombie_Legs( CBaseAnimating *pZombie, const char *pszStock );
// the legs that fall off wear the clothes of the body
void RPMZombie_LegsGib( CBaseAnimating *pZombie, CBaseEntity *pGib );

#endif // HL2RPM_ZOMBIES_H

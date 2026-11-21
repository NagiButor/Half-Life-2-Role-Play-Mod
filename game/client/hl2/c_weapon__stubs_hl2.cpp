//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "c_weapon__stubs.h"
#include "basehlcombatweapon_shared.h"
#include "c_basehlcombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

STUB_WEAPON_CLASS( cycler_weapon, WeaponCycler, C_BaseCombatWeapon );

STUB_WEAPON_CLASS( weapon_binoculars, WeaponBinoculars, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_bugbait, WeaponBugBait, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_flaregun, Flaregun, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_annabelle, WeaponAnnabelle, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_gauss, WeaponGaussGun, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_cubemap, WeaponCubemap, C_BaseCombatWeapon );
STUB_WEAPON_CLASS( weapon_alyxgun, WeaponAlyxGun, C_HLSelectFireMachineGun );
STUB_WEAPON_CLASS( weapon_citizenpackage, WeaponCitizenPackage, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_citizensuitcase, WeaponCitizenSuitcase, C_WeaponCitizenPackage );
//NewWeapons #Правка№2
STUB_WEAPON_CLASS(weapon_xm1014, WeaponXm1014, C_BaseHLCombatWeapon);

#ifndef HL2MP
STUB_WEAPON_CLASS( weapon_ar2, WeaponAR2, C_HLMachineGun );
STUB_WEAPON_CLASS( weapon_frag, WeaponFrag, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_rpg, WeaponRPG, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_pistol, WeaponPistol, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_shotgun, WeaponShotgun, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_smg1, WeaponSMG1, C_HLSelectFireMachineGun );
STUB_WEAPON_CLASS( weapon_357, Weapon357, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_crossbow, WeaponCrossbow, C_BaseHLCombatWeapon );
#ifndef MAPBASE
STUB_WEAPON_CLASS( weapon_slam, Weapon_SLAM, C_BaseHLCombatWeapon );
#endif
STUB_WEAPON_CLASS( weapon_crowbar, WeaponCrowbar, C_BaseHLBludgeonWeapon );
// HL2RPM: the melee weapons of the HL2 Melee Pack (server: hl2rpm_melee.cpp)
STUB_WEAPON_CLASS( weapon_rpm_axe, WeaponRPMAxe, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_bottle, WeaponRPMBottle, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_brokenbottle, WeaponRPMBrokenBottle, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_hook, WeaponRPMHook, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_pan, WeaponRPMPan, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_pickaxe, WeaponRPMPickaxe, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_pipe, WeaponRPMPipe, C_BaseHLBludgeonWeapon );
STUB_WEAPON_CLASS( weapon_rpm_pot, WeaponRPMPot, C_BaseHLBludgeonWeapon );
// HL2RPM: the Beretta 92 (server: hl2rpm_beretta.cpp)
STUB_WEAPON_CLASS( weapon_rpm_beretta, WeaponRPMBeretta, C_BaseHLCombatWeapon );
STUB_WEAPON_CLASS( weapon_rpm_shovel, WeaponRPMShovel, C_BaseHLBludgeonWeapon );
#ifdef HL2_EPISODIC
STUB_WEAPON_CLASS( weapon_hopwire, WeaponHopwire, C_BaseHLCombatWeapon );
//STUB_WEAPON_CLASS( weapon_proto1, WeaponProto1, C_BaseHLCombatWeapon );
#endif
#ifdef HL2_LOSTCOAST
STUB_WEAPON_CLASS( weapon_oldmanharpoon, WeaponOldManHarpoon, C_WeaponCitizenPackage );
#endif
#endif



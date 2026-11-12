// HL2RPM: one equipped weapon, out in the hands or put away (hl2rpm_weapons.cpp)
#ifndef HL2RPM_WEAPONS_H
#define HL2RPM_WEAPONS_H
#pragma once

class CBasePlayer;
class CBaseCombatWeapon;

namespace RPMWeapons
{
	CBaseCombatWeapon *Equipped( CBasePlayer *pPlayer );	// the weapon on the player, in the hands or away
	bool IsAway( CBasePlayer *pPlayer );
	void Stow( CBasePlayer *pPlayer, CBaseCombatWeapon *pWeapon );		// back into the inventory, magazine and all
	void StowAll( CBasePlayer *pPlayer, CBaseCombatWeapon *pKeep );
	void TakeOut( CBasePlayer *pPlayer );
	void PutAway( CBasePlayer *pPlayer );
	void Toggle( CBasePlayer *pPlayer );
	void SetBusy( bool bBusy );		// the inventory gives and drops a weapon for a moment
	void PlayerFrame( CBasePlayer *pPlayer );
}

#endif // HL2RPM_WEAPONS_H

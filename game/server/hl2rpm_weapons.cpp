//========= HL2RPM ============================================================//
//
// The player's weapon, as in New Vegas: one weapon is equipped, everything else stays
// in the inventory with what is in its magazine. The equipped one is out in the hands
// or put away:
//
//   H (rpm_holster) or holding R - put away / take out;
//   the attack button or R take it out when it is away;
//   the inventory: E equips (the one in the hands goes back) or takes it off.
//
// "Away" is simply no active weapon: the weapon stays in m_hMyWeapons, in the save too.
// Stock HL2 keeps every weapon on the player; anything that still gives one (give, map
// equipment, old saves) has the rest put back into the inventory.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_weapons.h"
#include "inventory_system.h"
#include "player.h"
#include "basecombatweapon.h"
#include "baseviewmodel.h"
#include "in_buttons.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define HOLSTER_HOLD_TIME 0.6f		// seconds of R

extern bool RPMSeat_IsSeated( CBasePlayer *pPlayer );

static bool s_bBusy = false;		// the inventory is handling a weapon for a moment: nothing is put away
static float s_flReloadDown = 0.0f;
static bool s_bReloadUsed = false;
static float s_flNextCheck = 0.0f;
static float s_flHolsterEnd = 0.0f;		// the animation of putting it away is over

namespace RPMWeapons
{
	CBaseCombatWeapon *Equipped( CBasePlayer *pPlayer )
	{
		if ( !pPlayer )
			return NULL;
		if ( pPlayer->GetActiveWeapon() )
			return pPlayer->GetActiveWeapon();
		for ( int i = 0; i < pPlayer->WeaponCount(); i++ )
		{
			if ( pPlayer->GetWeapon( i ) )
				return pPlayer->GetWeapon( i );
		}
		return NULL;
	}

	bool IsAway( CBasePlayer *pPlayer )
	{
		return pPlayer && !pPlayer->GetActiveWeapon() && Equipped( pPlayer );
	}

	void SetBusy( bool bBusy )
	{
		s_bBusy = bBusy;
	}

	void Stow( CBasePlayer *pPlayer, CBaseCombatWeapon *pWeapon )
	{
		if ( !pPlayer || !pWeapon || pWeapon->GetOwner() != pPlayer )
			return;
		// a weapon without a magazine (grenades, the RPG) has its ammo on the player: it is written down
		// so that equipping it again doesn't add the default amount once more
		int iAmmo = -1, nAmmo = 0;
		if ( !pWeapon->UsesClipsForAmmo1() && pWeapon->GetPrimaryAmmoType() >= 0 )
		{
			iAmmo = pWeapon->GetPrimaryAmmoType();
			nAmmo = pPlayer->GetAmmoCount( iAmmo );
		}
		InventorySystem::AddWeaponToPlayer( pPlayer, pWeapon->GetClassname(), Max( pWeapon->m_iClip1.Get(), 0 ), Max( pWeapon->m_iClip2.Get(), 0 ), iAmmo, nAmmo );

		const bool bActive = pPlayer->GetActiveWeapon() == pWeapon;
		pPlayer->RemovePlayerItem( pWeapon );
		UTIL_Remove( pWeapon );
		if ( bActive )
		{
			CBaseViewModel *pVM = pPlayer->GetViewModel();
			if ( pVM )
				pVM->AddEffects( EF_NODRAW );
		}
	}

	void StowAll( CBasePlayer *pPlayer, CBaseCombatWeapon *pKeep )
	{
		if ( !pPlayer || s_bBusy )
			return;
		for ( int i = pPlayer->WeaponCount() - 1; i >= 0; i-- )
		{
			CBaseCombatWeapon *pWeapon = pPlayer->GetWeapon( i );
			if ( pWeapon && pWeapon != pKeep )
				Stow( pPlayer, pWeapon );
		}
	}

	static bool CanChange( CBasePlayer *pPlayer )
	{
		return pPlayer->IsAlive() && !pPlayer->IsInAVehicle() && !pPlayer->GetUseEntity() && !RPMSeat_IsSeated( pPlayer );
	}

	void TakeOut( CBasePlayer *pPlayer )
	{
		CBaseCombatWeapon *pWeapon = Equipped( pPlayer );
		if ( !pWeapon || pPlayer->GetActiveWeapon() || !CanChange( pPlayer ) || !pWeapon->CanDeploy() )
			return;
		// (not Weapon_Switch: it refuses a weapon with no ammo, and an empty gun can be held)
		pPlayer->SetActiveWeapon( pWeapon );
		pWeapon->Deploy();
		CBaseViewModel *pVM = pPlayer->GetViewModel();
		if ( pVM )
			pVM->RemoveEffects( EF_NODRAW );
	}

	void PutAway( CBasePlayer *pPlayer )
	{
		CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : NULL;
		if ( !pWeapon || !CanChange( pPlayer ) || !pWeapon->CanHolster() )
			return;
		if ( !pWeapon->Holster( NULL ) )
			return;
		s_flHolsterEnd = pPlayer->GetNextAttack();
		pPlayer->ClearActiveWeapon();
		pPlayer->SetNextAttack( gpGlobals->curtime );
	}

	void Toggle( CBasePlayer *pPlayer )
	{
		if ( !pPlayer )
			return;
		if ( pPlayer->GetActiveWeapon() )
			PutAway( pPlayer );
		else
			TakeOut( pPlayer );
	}

	void PlayerFrame( CBasePlayer *pPlayer )
	{
		// old saves, and whatever gave a weapon past Weapon_Equip
		if ( gpGlobals->curtime >= s_flNextCheck || gpGlobals->curtime < s_flNextCheck - 2.0f )
		{
			s_flNextCheck = gpGlobals->curtime + 1.0f;
			if ( pPlayer->WeaponCount() && !s_bBusy )
			{
				int n = 0;
				for ( int i = 0; i < pPlayer->WeaponCount(); i++ )
					n += pPlayer->GetWeapon( i ) ? 1 : 0;
				if ( n > 1 )
					StowAll( pPlayer, Equipped( pPlayer ) );
			}
		}

		CBaseCombatWeapon *pActive = pPlayer->GetActiveWeapon();

		// a weapon that is away is not in the hands: the view model doesn't stay (a loaded game showed it)
		if ( !pActive && Equipped( pPlayer ) && ( gpGlobals->curtime >= s_flHolsterEnd || gpGlobals->curtime < s_flHolsterEnd - 5.0f ) )
		{
			CBaseViewModel *pVM = pPlayer->GetViewModel();
			if ( pVM && !pVM->IsEffectActive( EF_NODRAW ) )
				pVM->AddEffects( EF_NODRAW );
		}

		// the last grenade is gone: nothing is left in the hands
		if ( pActive && !pActive->UsesClipsForAmmo1() && pActive->GetPrimaryAmmoType() >= 0 && !pPlayer->GetAmmoCount( pActive->GetPrimaryAmmoType() )
			&& !pActive->HasAnyAmmo() && gpGlobals->curtime >= pActive->m_flNextPrimaryAttack && !Q_stricmp( pActive->GetClassname(), "weapon_frag" ) )
		{
			pPlayer->RemovePlayerItem( pActive );
			UTIL_Remove( pActive );
			CBaseViewModel *pVM = pPlayer->GetViewModel();
			if ( pVM )
				pVM->AddEffects( EF_NODRAW );
			InventorySystem::SendInventoryToPlayer( pPlayer );
			return;
		}

		// R: a tap reloads (or takes the weapon out), held - puts it away, once the reload is over
		const bool bReload = ( pPlayer->m_nButtons & IN_RELOAD ) != 0;
		if ( bReload )
		{
			if ( !s_flReloadDown )
			{
				s_flReloadDown = gpGlobals->curtime;
				s_bReloadUsed = false;
				if ( !pActive && Equipped( pPlayer ) )
				{
					TakeOut( pPlayer );
					s_bReloadUsed = true;
				}
			}
			else if ( !s_bReloadUsed && pActive && gpGlobals->curtime - s_flReloadDown >= HOLSTER_HOLD_TIME && !pActive->m_bInReload )
			{
				PutAway( pPlayer );
				s_bReloadUsed = true;
			}
		}
		else
		{
			s_flReloadDown = 0.0f;
		}

		// the attack button takes it out and doesn't fire
		if ( !pActive && ( pPlayer->m_afButtonPressed & IN_ATTACK ) && Equipped( pPlayer ) )
			TakeOut( pPlayer );
	}
}

void RPMWeapons_OnEquip( CBasePlayer *pPlayer, CBaseCombatWeapon *pWeapon )
{
	RPMWeapons::StowAll( pPlayer, pWeapon );
}

void RPMWeapons_PlayerFrame( CBasePlayer *pPlayer )
{
	RPMWeapons::PlayerFrame( pPlayer );
}

CON_COMMAND( weapon_dump, "HL2RPM: the weapon on the player and the ammo he carries" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	Msg( "[weapon] active '%s', vm drawn %d\n", pPlayer->GetActiveWeapon() ? pPlayer->GetActiveWeapon()->GetClassname() : "", pPlayer->GetViewModel() ? !pPlayer->GetViewModel()->IsEffectActive( EF_NODRAW ) : -1 );
	for ( int i = 0; i < pPlayer->WeaponCount(); i++ )
	{
		CBaseCombatWeapon *pWeapon = pPlayer->GetWeapon( i );
		if ( pWeapon )
			Msg( "[weapon]   on the player: %s clip %d ammo %d\n", pWeapon->GetClassname(), pWeapon->m_iClip1.Get(), pWeapon->GetPrimaryAmmoType() >= 0 ? pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() ) : -1 );
	}
	InventorySystem::PrintInventory( pPlayer );
}

CON_COMMAND_F( rpm_holster,"HL2RPM: put the weapon away / take it out", FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	RPMWeapons::Toggle( UTIL_GetCommandClient() );
}

CON_COMMAND_F( inventory_unequip_weapon, "HL2RPM: the weapon goes back into the inventory", FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	RPMWeapons::Stow( pPlayer, RPMWeapons::Equipped( pPlayer ) );
	InventorySystem::SendInventoryToPlayer( pPlayer );
}

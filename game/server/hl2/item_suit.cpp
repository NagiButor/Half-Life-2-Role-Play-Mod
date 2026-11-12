//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
/*

===== item_suit.cpp ========================================================

  handling for the player's suit.
*/

#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "items.h"
#include "inventory_system.h"
#include "hl2rpm_items.h"
#include "filesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define SF_SUIT_SHORTLOGON		0x0001

class CItemSuit : public CItem
{
public:
	DECLARE_CLASS( CItemSuit, CItem );

	// HL2RPM: the suit on a stand, as the other suits (hl2rpm_items.cpp: hl2rpm/items/suit_hev)
	static const char *SuitModel()
	{
		const char *m = RPMItems::Model( "item_suit" );
		return ( m && m[0] && filesystem->FileExists( m, "GAME" ) ) ? m : "models/items/hevsuit.mdl";
	}
	void Spawn( void )
	{ 
		Precache( );
		SetModel( SuitModel() );
		BaseClass::Spawn( );
		
		CollisionProp()->UseTriggerBounds( false, 0 );
	}
	void Precache( void )
	{
		PrecacheModel( SuitModel() );
	}
	bool MyTouch( CBasePlayer *pPlayer )
	{
		if ( pPlayer->IsSuitEquipped() )
			return FALSE;

		if ( m_spawnflags & SF_SUIT_SHORTLOGON )
			UTIL_EmitSoundSuit(pPlayer->edict(), "!HEV_A0");		// short version of suit logon,
		else
			UTIL_EmitSoundSuit(pPlayer->edict(), "!HEV_AAx");	// long version of suit logon

#ifdef MAPBASE
		pPlayer->EquipSuit(!HasSpawnFlags(SF_SUIT_SHORTLOGON));
#else
		pPlayer->EquipSuit();
#endif
		InventorySystem::SetEquippedSuitClass(pPlayer, "item_suit");

		// Ensure client HUD is restored to default (not combine blue) when picking normal suit
		{
			CSingleUserRecipientFilter filter( pPlayer );
			UserMessageBegin( filter, "CombineSuit" );
			WRITE_BYTE( 0 );
			MessageEnd();
		}
				
		return true;
	}
};

LINK_ENTITY_TO_CLASS(item_suit, CItemSuit);

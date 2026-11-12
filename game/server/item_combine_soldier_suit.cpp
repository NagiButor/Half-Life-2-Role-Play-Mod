// item_combine_soldier_suit.cpp
#include "cbase.h"
#include "item_combine_soldier_suit.h"
#include "player.h"
#include "usermessages.h"
#include "baseplayer_shared.h"
#include "inventory_system.h"
#include "hl2rpm_items.h"

LINK_ENTITY_TO_CLASS( item_combine_soldier_suit, CItemCombineSoldierSuit );

BEGIN_DATADESC( CItemCombineSoldierSuit )
END_DATADESC()

void CItemCombineSoldierSuit::Precache( void )
{
    PrecacheModel( RPMItems::Model( "item_combine_soldier_suit" ) );
    BaseClass::Precache();
}

void CItemCombineSoldierSuit::Spawn( void )
{
    Precache();
    SetModel( RPMItems::Model( "item_combine_soldier_suit" ) );
    BaseClass::Spawn();
}

bool CItemCombineSoldierSuit::MyTouch( CBasePlayer *pPlayer )
{
    if ( !pPlayer )
        return false;

    // Mark player as wearing a suit (networked local data).
    pPlayer->m_Local.m_bWearingSuit = true;
    InventorySystem::SetEquippedSuitClass(pPlayer, "item_combine_soldier_suit");

    // Send usermessage to this client to enable combine-suit HUD
    {
        CSingleUserRecipientFilter filter( pPlayer );
        UserMessageBegin( filter, "CombineSuit" );
        WRITE_BYTE( 1 );
        MessageEnd();
    }

    // Play suit equip sound using the standard suit helper so it's consistent with other suits
    UTIL_EmitSoundSuit( pPlayer->edict(), "!HEV_AAx" );

    // (Optional) gameplay effects (armor) can be set here if desired.

    // Play suit equip sound on server so that client hears it normally
    EmitSound( "player/suit_equip.wav" );

    // Remove the item from the world
    UTIL_Remove( this );

    return true;
}

//-----------------------------------------------------------------------------
// HL2RPM (M6): more suits to wear - the PCV, Civil Protection, the guards of Nova
// Prospekt and Overwatch elite. Their models are in RPMItems::Model, what they stop and
// the body of the player in RPMItems::SuitDefense / SuitBody; HUD / window colors and
// the boot screen come from the class (RPMChar_SuitTheme in hl2rpm_char.cpp).
//-----------------------------------------------------------------------------
class CItemRPMSuit : public CItem
{
public:
	DECLARE_CLASS( CItemRPMSuit, CItem );

	const char *SuitModel()
	{
		return RPMItems::Model( GetClassname() );
	}

	virtual void Precache( void )
	{
		PrecacheModel( SuitModel() );
		BaseClass::Precache();
	}

	virtual void Spawn( void )
	{
		Precache();
		SetModel( SuitModel() );
		BaseClass::Spawn();
	}

	virtual bool MyTouch( CBasePlayer *pPlayer )
	{
		if ( !pPlayer )
			return false;
		pPlayer->m_Local.m_bWearingSuit = true;
		InventorySystem::SetEquippedSuitClass( pPlayer, GetClassname() );
		EmitSound( "player/suit_equip.wav" );
		UTIL_Remove( this );
		return true;
	}
};

LINK_ENTITY_TO_CLASS( item_suit_pcv, CItemRPMSuit );
LINK_ENTITY_TO_CLASS( item_suit_cp, CItemRPMSuit );
LINK_ENTITY_TO_CLASS( item_suit_elite, CItemRPMSuit );
LINK_ENTITY_TO_CLASS( item_suit_prison, CItemRPMSuit );
// (2026-11-02: not combat suits - the coverall of a Combine worker and a hazmat suit, with a HUD of
// their own: themes "worker" / "hazmat")
LINK_ENTITY_TO_CLASS( item_suit_worker, CItemRPMSuit );
LINK_ENTITY_TO_CLASS( item_suit_hazmat, CItemRPMSuit );

// item_combine_soldier_suit.cpp
#include "cbase.h"
#include "item_combine_soldier_suit.h"
#include "player.h"
#include "usermessages.h"
#include "baseplayer_shared.h"
#include "inventory_system.h"
#include "hl2rpm/rpg_shared.h"

LINK_ENTITY_TO_CLASS( item_combine_soldier_suit, CItemCombineSoldierSuit );

BEGIN_DATADESC( CItemCombineSoldierSuit )
END_DATADESC()

void CItemCombineSoldierSuit::Precache( void )
{
    PrecacheModel( "models/combine_vests/militaryvest.mdl" );
    BaseClass::Precache();
}

void CItemCombineSoldierSuit::Spawn( void )
{
    Precache();
    SetModel( "models/combine_vests/militaryvest.mdl" );
    BaseClass::Spawn();
}

bool CItemCombineSoldierSuit::MyTouch( CBasePlayer *pPlayer )
{
    if ( !pPlayer )
        return false;

    // HL2RPM: a suit already worn goes back into the inventory (this used to overwrite the HEV suit)
    if ( pPlayer->IsSuitEquipped() )
    {
        const char *pszOld = InventorySystem::GetEquippedSuitClass( pPlayer );
        CUtlString old = ( pszOld && pszOld[0] ) ? pszOld : "item_suit";
        if ( !Q_stricmp( old.Get(), "item_combine_soldier_suit" ) )
            return false;
        pPlayer->RemoveSuit();
        InventorySystem::AddItemToPlayer( pPlayer, old.Get() );
    }

    // Mark player as wearing a suit (networked local data).
    pPlayer->m_Local.m_bWearingSuit = true;
    pPlayer->m_Local.m_iSuitType = RPG_SUIT_OVERWATCH;	// HL2RPM: HUD theme, saved with the player
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

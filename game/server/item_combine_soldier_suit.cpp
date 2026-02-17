// item_combine_soldier_suit.cpp
#include "cbase.h"
#include "item_combine_soldier_suit.h"
#include "player.h"
#include "usermessages.h"
#include "baseplayer_shared.h"
#include "inventory_system.h"

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

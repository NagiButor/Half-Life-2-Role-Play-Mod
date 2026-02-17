#include "cbase.h"
#include "entitylist.h"

#if defined(GAME_DLL)

class CInventoryEntityListener : public IEntityListener
{
public:
    void LevelInitPreEntity()
    {
        gEntList.AddListenerEntity( this );
    }

    void LevelShutdownPostEntity()
    {
        gEntList.RemoveListenerEntity( this );
    }

    // Called when an entity has finished spawning
    virtual void OnEntitySpawned( CBaseEntity *pEntity )
    {
        if ( !pEntity )
            return;

        const char *pszClass = pEntity->GetClassname();
        if ( !pszClass || !pszClass[0] )
            return;

        // Normalize weapons: ensure they use the weapon collision group and
        // do not accidentally carry a "no player pickup" spawnflag that would
        // block manual pickup. We don't force-disable scripted behavior, just
        // remove stray flags that would permanently prevent pickup.
        if ( Q_strnicmp( pszClass, "weapon_", 7 ) == 0 )
        {
            // Remove any server-side no-pickup flags so players can still pick up
            // weapons via normal routes. The movehelper already prevents auto-touch pickups.
#if defined(SF_WEAPON_NO_PLAYER_PICKUP)
            if ( pEntity->HasSpawnFlags( SF_WEAPON_NO_PLAYER_PICKUP ) )
                pEntity->RemoveSpawnFlags( SF_WEAPON_NO_PLAYER_PICKUP );
#endif
            pEntity->SetCollisionGroup( COLLISION_GROUP_WEAPON );
        }
        else if ( Q_strnicmp( pszClass, "item_", 5 ) == 0 )
        {
            // Leave collision group default for items; ensure they can be touched normally.
            // Do NOT remove SF_ITEM_NO_PLAYER_PICKUP here: items dropped from inventory
            // may intentionally set that flag to avoid immediate auto-pickup.
        }
    }
};

static CInventoryEntityListener g_InventoryEntityListener;

#endif // GAME_DLL

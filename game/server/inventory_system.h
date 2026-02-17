#ifndef INVENTORY_SYSTEM_H
#define INVENTORY_SYSTEM_H

#include "cbase.h"
#include "utlstring.h"
#include "utlvector.h"

class CBasePlayer;

namespace InventorySystem
{
    struct InvItem
    {
        CUtlString classname;
        int bullets;
        bool isWeapon;
        int weaponClip1;
        int weaponClip2;
        int reserveAmmoIndex;
        int reserveAmmoCount;
    };

    bool PopInventoryEntryAtIndex(CBasePlayer *pPlayer, int slotIndex, InvItem &outItem);

    void AddWeaponToPlayer(CBasePlayer *pPlayer, const char *itemName, int clip1, int clip2, int reserveAmmoIndex, int reserveAmmoCount);

    // Add/remove simple named item for a player
    void AddItemToPlayer(CBasePlayer *pPlayer, const char *itemName);
    // Remove one instance of `itemName` from the player's inventory.
    // Returns remaining bullets stored in that removed item if it was an ammo item,
    // or 0 for non-ammo items, or -1 if nothing was removed.
    int RemoveItemFromPlayer(CBasePlayer *pPlayer, const char *itemName);

    // Send the player's inventory to their client (opens the panel)
    void SendInventoryToPlayer(CBasePlayer *pPlayer);
    void GetInventoryItemStringsForPlayer(CBasePlayer *pPlayer, CUtlVector<CUtlString> &outItems);
    // Return a serialized inventory payload string for the given player
    CUtlString GetInventoryPayloadForPlayer(CBasePlayer *pPlayer);

    // Add an item to a player's inventory with an explicit bullet count
    void AddItemToPlayerWithBullets(CBasePlayer *pPlayer, const char *itemName, int bullets);

    // Check if an item class name is an ammo type
    bool IsAmmoItem(const char *classname);

    // Get the ammo type name (e.g., "Pistol", "SMG1") for an item class
    const char *GetAmmoTypeForItem(const char *classname);

    // Get the bullet count for an ammo item class (how many bullets per pickup)
    int GetBulletsPerAmmoItem(const char *classname);

    // Get total ammo count of a specific ammo type in player's inventory
    int GetInventoryAmmoCount(CBasePlayer *pPlayer, const char *ammoTypeName);

    // Consume ammo from inventory (returns how many bullets were actually consumed)
    int ConsumeInventoryAmmo(CBasePlayer *pPlayer, const char *ammoTypeName, int amount);

    // Sync player's engine ammo counts with inventory ammo
    void SyncPlayerAmmoWithInventory(CBasePlayer *pPlayer);

    float GetPlayerInventoryWeight(CBasePlayer *pPlayer);
    float GetPlayerMaxCarryWeight(CBasePlayer *pPlayer);
    bool IsPlayerOverencumbered(CBasePlayer *pPlayer);
    bool UpdateOverencumberedState(CBasePlayer *pPlayer, bool bOverencumbered);

    // Get friendly display name for an item classname
    const char *GetFriendlyItemName(const char *classname);

    // Display a pickup notification to the player
    void NotifyPlayerPickup(CBasePlayer *pPlayer, const char *itemName);
    void NotifyPlayerPickup(CBasePlayer *pPlayer, const char *itemName, int count);
    void NotifyPlayerReward(CBasePlayer *pPlayer, const char *itemName, int count);

    // If a world entity has stored dropped ammo, return that value and remove
    // the stored mapping. Returns 0 if no stored value was present.
    int ConsumeDroppedAmmoForEntity(CBaseEntity *pEnt);

    // Record a dropped weapon entity along with its clip and reserve state
    // so pickups can restore clips/reserve when the entity is picked up.
    void RecordDroppedWeapon(CBaseEntity *pEnt, int clip1, int clip2, int reserveAmmoIndex, int reserveAmmoCount);

    // Helper: add the dropped weapon's magazine bullets into the dropper's
    // inventory as an ammo item so magazine rounds are not lost when G-dropping.
    void AddDroppedWeaponClipToInventory(CBasePlayer *pPlayer, int ammoIndex, int clipBullets);

    void SetEquippedSuitClass(CBasePlayer *pPlayer, const char *suitClassname);
    const char *GetEquippedSuitClass(CBasePlayer *pPlayer);
    void ClearEquippedSuitClass(CBasePlayer *pPlayer);

}

extern ConVar sv_inventory_enable;
extern ConVar sv_inventory_move_clip_to_inventory;
extern ConVar sv_inventory_weight_enable;
extern ConVar sv_inventory_weight_max;
extern ConVar sv_inventory_weight_over_speed;
extern ConVar sv_inventory_weight_over_toast;
extern ConVar sv_inventory_weight_over_toast_sound;
extern ConVar sv_inventory_weight_file;
extern ConVar sv_inventory_weight_default_item;
extern ConVar sv_inventory_weight_default_weapon;
extern ConVar sv_inventory_weight_default_other;
extern ConVar sv_inventory_weight_default_bullet;
extern ConVar sv_inventory_weight_include_equipped_weapons;
extern ConVar sv_inventory_weight_include_equipped_suit;

#endif // INVENTORY_SYSTEM_H

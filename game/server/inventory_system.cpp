#include "cbase.h"
#include "inventory_system.h"
#include "player.h"
#include "util.h"
#include "ammodef.h"
#include "items.h"
#include "baseviewmodel.h"
#include "recipientfilter.h"
#include "inventory_netmessages.h"
#include "usermessages.h"
#include "hl2_player.h"
#include "filesystem.h"
#include "KeyValues.h"

extern ConVar quest_reward_debug;

// Wrapper entity for dropped inventory ammo: stores an explicit bullet count
// and model so pickups always grant the stored amount instead of the
// class-default amount.
class CInventoryDroppedAmmo : public CBaseAnimating
{
public:
    DECLARE_CLASS( CInventoryDroppedAmmo, CBaseAnimating );
    DECLARE_DATADESC();

    void Spawn( void )
    {
        Precache();
        if ( m_pszModel && m_pszModel[0] )
        {
            SetModel( m_pszModel );
        }
        BaseClass::Spawn();
        // Prefer using VPhysics so the dropped ammo behaves like a physical prop.
        // If vphysics initialization fails for any reason, fall back to the
        // simple fly-gravity behaviour used previously.
        SetMoveType( MOVETYPE_VPHYSICS );
        SetSolid( SOLID_VPHYSICS );
        if ( !VPhysicsInitNormal( SOLID_VPHYSICS, 0, false ) )
        {
            // Fallback to previous behaviour
            SetMoveType( MOVETYPE_FLYGRAVITY );
            SetSolid( SOLID_BBOX );
        }
    }

    int ObjectCaps( void ) { return (BaseClass::ObjectCaps() | FCAP_IMPULSE_USE); }

    void Precache( void )
    {
        if ( m_pszModel && m_pszModel[0] )
            PrecacheModel( m_pszModel );
    }

    bool MyTouch( CBasePlayer *pPlayer )
    {
        if ( !pPlayer ) return false;
        // Add to player's inventory rather than directly giving ammo so the
        // inventory system remains authoritative.
        if ( m_iBullets > 0 && m_szClassname[0] )
        {
            InventorySystem::AddItemToPlayerWithBullets(pPlayer, m_szClassname, m_iBullets);
            // Notify the player client that they picked up this item
            InventorySystem::NotifyPlayerPickup(pPlayer, m_szClassname);
            DevMsg("Inventory: CInventoryDroppedAmmo added %d bullets of '%s' to player %s inventory\n", m_iBullets, m_szClassname, pPlayer->GetPlayerName());
        }
        UTIL_Remove( this );
        return true;
    }

    void Touch( CBaseEntity *pOther )
    {
        // Prevent automatic pickup on touch; pickups should occur via +use (E).
        // This avoids the engine auto-granting default amounts and breaking
        // the inventory-authoritative stored counts.
        return;
    }

    void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
    {
        CBasePlayer *pPlayer = ToBasePlayer( pActivator );
        MyTouch( pPlayer );
    }

    int m_iBullets = 0;
    int m_iAmmoIndex = -1;
    const char *m_pszModel = "";
    char m_szClassname[64];
};

LINK_ENTITY_TO_CLASS( item_inventory_dropped_ammo, CInventoryDroppedAmmo );

BEGIN_DATADESC( CInventoryDroppedAmmo )
    DEFINE_FIELD( m_iBullets, FIELD_INTEGER ),
    DEFINE_FIELD( m_iAmmoIndex, FIELD_INTEGER ),
    // model is not persisted via datadesc here
END_DATADESC()

// Helper: choose a model string for an ammo classname so wrapper looks correct
static const char *GetModelForAmmoItem(const char *classname)
{
    if (!classname) return "";
    if (!Q_stricmp(classname, "item_ammo_smg1") || !Q_stricmp(classname, "item_box_mrounds"))
        return "models/items/boxmrounds.mdl";
    if (!Q_stricmp(classname, "item_ammo_smg1_large") || !Q_stricmp(classname, "item_large_box_mrounds"))
        return "models/items/boxmrounds.mdl";
    if (!Q_stricmp(classname, "item_ammo_pistol") || !Q_stricmp(classname, "item_box_srounds"))
        return "models/items/boxsrounds.mdl";
    if (!Q_stricmp(classname, "item_ammo_ar2") || !Q_stricmp(classname, "item_box_lrounds"))
        return "models/items/combine_rifle_cartridge01.mdl";
    if (!Q_stricmp(classname, "item_ammo_357") )
        return "models/items/357ammo.mdl";
    if (!Q_stricmp(classname, "item_ammo_357_large") )
        return "models/items/357ammobox.mdl";
    if (!Q_stricmp(classname, "item_ammo_crossbow"))
        return "models/items/crossbowrounds.mdl";
    if (!Q_stricmp(classname, "item_box_buckshot"))
        return "models/items/boxbuckshot.mdl";
    if (!Q_stricmp(classname, "item_rpg_round") || !Q_stricmp(classname, "item_ml_grenade"))
        return "models/weapons/w_missile_closed.mdl";
    return "";
}

// Server toggle: enable pickup into inventory via +use instead of default pickup
ConVar sv_inventory_enable("sv_inventory_enable", "1", FCVAR_NOTIFY, "Enable picking up weapons/items into inventory via +use");

// Toggle: when true, add dropped weapon magazine bullets into the dropper's
// inventory as ammo items. Default is OFF to avoid accidentally moving
// magazine rounds into inventory when players press G to drop weapons.
ConVar sv_inventory_move_clip_to_inventory("sv_inventory_move_clip_to_inventory", "1", FCVAR_NONE, "When 1, moved dropped weapon clip bullets into player's inventory (legacy behavior)");

ConVar sv_inventory_weight_enable("sv_inventory_weight_enable", "1", FCVAR_NOTIFY, "Enable inventory weight/encumbrance system");
ConVar sv_inventory_weight_max("sv_inventory_weight_max", "40.0", FCVAR_NOTIFY, "Max carry weight (kg)");
ConVar sv_inventory_weight_over_speed("sv_inventory_weight_over_speed", "60.0", FCVAR_NOTIFY, "Max speed when overencumbered");
ConVar sv_inventory_weight_over_toast("sv_inventory_weight_over_toast", "1", FCVAR_NONE, "Show toast when becoming overencumbered");
ConVar sv_inventory_weight_over_toast_sound("sv_inventory_weight_over_toast_sound", "friends/message.wav", FCVAR_NONE, "Toast sound when becoming overencumbered");
ConVar sv_inventory_weight_file("sv_inventory_weight_file", "scripts/inventory_weights.txt", FCVAR_NONE, "KeyValues weight table file (GAME path)");
ConVar sv_inventory_weight_default_item("sv_inventory_weight_default_item", "1.0", FCVAR_NONE, "Default kg per item_* entry");
ConVar sv_inventory_weight_default_weapon("sv_inventory_weight_default_weapon", "3.0", FCVAR_NONE, "Default kg per weapon_* entry");
ConVar sv_inventory_weight_default_other("sv_inventory_weight_default_other", "0.5", FCVAR_NONE, "Default kg per unknown classname entry");
ConVar sv_inventory_weight_default_bullet("sv_inventory_weight_default_bullet", "0.01", FCVAR_NONE, "Default kg per bullet for ammo items");
ConVar sv_inventory_weight_include_equipped_weapons("sv_inventory_weight_include_equipped_weapons", "1", FCVAR_NONE, "Include currently owned weapons in weight calculation");
ConVar sv_inventory_weight_include_equipped_suit("sv_inventory_weight_include_equipped_suit", "1", FCVAR_NONE, "Include equipped suit in weight calculation");

namespace InventorySystem
{

    // Forward-declare index-based pop helper (definition below)
    bool PopInventoryEntryAtIndex(CBasePlayer *pPlayer, int slotIndex, InvItem &outItem);
    static CUtlString EscapeForQuotedArg(const char *s);

    struct PlayerInv
    {
        int entIndex;
        CUtlVector<InvItem> items;
    };

    static CUtlVector<PlayerInv> g_PlayerInventories;

    struct EquippedSuitEntry
    {
        int entIndex;
        string_t suitClass;
    };
    static CUtlVector<EquippedSuitEntry> g_EquippedSuitByPlayer;

    static CUtlDict<float, int> g_WeightKgByClass;
    static bool g_bWeightTableLoaded = false;
    static void LoadInventoryWeightTable()
    {
        g_WeightKgByClass.RemoveAll();
        g_bWeightTableLoaded = true;

        KeyValues *pKV = new KeyValues("InventoryWeights");
        if (!pKV->LoadFromFile(filesystem, sv_inventory_weight_file.GetString(), "GAME"))
        {
            pKV->deleteThis();
            return;
        }

        for (KeyValues *pKey = pKV->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
        {
            const float kg = pKey->GetFloat("kg", 0.0f);
            if (kg <= 0.0f)
                continue;
            g_WeightKgByClass.Insert(pKey->GetName(), kg);
        }

        pKV->deleteThis();
    }

    // Track information for dropped world entities so pickups can restore
    // partial packs or weapon state.
    struct DroppedEntityInfo
    {
        CBaseEntity *pEnt;
        // For ammo packs
        int bullets;
        // For weapons
        bool isWeapon;
        int weaponClip1;
        int weaponClip2;
        int reserveAmmoIndex;
        int reserveAmmoCount;
    };
    static CUtlVector<DroppedEntityInfo> g_DroppedAmmoMap;

    // Forward-declare helper used below
    static PlayerInv *FindOrCreateInventory(CBasePlayer *pPlayer);

    // Ammo item mapping structure
    struct AmmoItemInfo
    {
        const char *classname;
        const char *ammoType;
        int bulletsPerPickup;
    };

    // Table mapping item class names to ammo types and bullet counts
    static const AmmoItemInfo g_AmmoItemTable[] = {
        // Pistol ammo
        {"item_ammo_pistol", "Pistol", SIZE_AMMO_PISTOL},
        {"item_box_srounds", "Pistol", SIZE_AMMO_PISTOL},
        {"item_ammo_pistol_large", "Pistol", SIZE_AMMO_PISTOL_LARGE},
        {"item_large_box_srounds", "Pistol", SIZE_AMMO_PISTOL_LARGE},
        // SMG ammo
        {"item_ammo_smg1", "SMG1", SIZE_AMMO_SMG1},
        {"item_box_mrounds", "SMG1", SIZE_AMMO_SMG1},
        {"item_ammo_smg1_large", "SMG1", SIZE_AMMO_SMG1_LARGE},
        {"item_large_box_mrounds", "SMG1", SIZE_AMMO_SMG1_LARGE},
        // AR2 ammo
        {"item_ammo_ar2", "AR2", SIZE_AMMO_AR2},
        {"item_box_lrounds", "AR2", SIZE_AMMO_AR2},
        {"item_ammo_ar2_large", "AR2", SIZE_AMMO_AR2_LARGE},
        {"item_large_box_lrounds", "AR2", SIZE_AMMO_AR2_LARGE},
        // 357 ammo
        {"item_ammo_357", "357", SIZE_AMMO_357},
        {"item_ammo_357_large", "357", SIZE_AMMO_357_LARGE},
        // Crossbow ammo
        {"item_ammo_crossbow", "XBowBolt", SIZE_AMMO_CROSSBOW},
        // Buckshot (shotgun) ammo
        {"item_box_buckshot", "Buckshot", SIZE_AMMO_BUCKSHOT},
        // RPG ammo
        {"item_rpg_round", "RPG_Round", SIZE_AMMO_RPG_ROUND},
        {"item_ml_grenade", "RPG_Round", SIZE_AMMO_RPG_ROUND},
        // SMG grenade
        {"item_ammo_smg1_grenade", "SMG1_Grenade", SIZE_AMMO_SMG1_GRENADE},
        {"item_ar2_grenade", "SMG1_Grenade", SIZE_AMMO_SMG1_GRENADE},
        // AR2 alt fire
        {"item_ammo_ar2_altfire", "AR2AltFire", SIZE_AMMO_AR2_ALTFIRE},
        // Terminator
        {NULL, NULL, 0}
    };

    bool IsAmmoItem(const char *classname)
    {
        if (!classname) return false;
        for (int i = 0; g_AmmoItemTable[i].classname != NULL; ++i)
        {
            if (!Q_stricmp(classname, g_AmmoItemTable[i].classname))
                return true;
        }
        return false;
    }

    const char *GetAmmoTypeForItem(const char *classname)
    {
        if (!classname) return NULL;
        for (int i = 0; g_AmmoItemTable[i].classname != NULL; ++i)
        {
            if (!Q_stricmp(classname, g_AmmoItemTable[i].classname))
                return g_AmmoItemTable[i].ammoType;
        }
        return NULL;
    }

    int GetBulletsPerAmmoItem(const char *classname)
    {
        if (!classname) return 0;
        for (int i = 0; g_AmmoItemTable[i].classname != NULL; ++i)
        {
            if (!Q_stricmp(classname, g_AmmoItemTable[i].classname))
                return g_AmmoItemTable[i].bulletsPerPickup;
        }
        return 0;
    }

    int GetInventoryAmmoCount(CBasePlayer *pPlayer, const char *ammoTypeName)
    {
        if (!pPlayer || !ammoTypeName) return 0;
        int idx = pPlayer->entindex();
        int totalBullets = 0;

        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
            for (int j = 0; j < g_PlayerInventories[i].items.Count(); ++j)
            {
                const char *itemClass = g_PlayerInventories[i].items[j].classname.Get();
                const char *itemAmmoType = GetAmmoTypeForItem(itemClass);
                if (itemAmmoType && !Q_stricmp(itemAmmoType, ammoTypeName))
                {
                    totalBullets += g_PlayerInventories[i].items[j].bullets;
                }
            }
            break;
        }
        return totalBullets;
    }

    int ConsumeInventoryAmmo(CBasePlayer *pPlayer, const char *ammoTypeName, int amount)
    {
        if (!pPlayer || !ammoTypeName || amount <= 0) return 0;
        int idx = pPlayer->entindex();
        int consumed = 0;

        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;

            // Process items in reverse order so we can safely remove/modify
            for (int j = g_PlayerInventories[i].items.Count() - 1; j >= 0 && consumed < amount; --j)
            {
                const char *itemClass = g_PlayerInventories[i].items[j].classname.Get();
                const char *itemAmmoType = GetAmmoTypeForItem(itemClass);
                if (itemAmmoType && !Q_stricmp(itemAmmoType, ammoTypeName))
                {
                    int &bulletsInItem = g_PlayerInventories[i].items[j].bullets;
                    int need = amount - consumed;
                    if (bulletsInItem <= need)
                    {
                        DevMsg("Inventory: consuming %d bullets from pack '%s' (had %d) -> removing pack\n", bulletsInItem, itemClass, bulletsInItem);
                        consumed += bulletsInItem;
                        // remove this empty pack
                        g_PlayerInventories[i].items.Remove(j);
                    }
                    else
                    {
                        DevMsg("Inventory: consuming %d bullets from pack '%s' (had %d) -> remaining %d\n", need, itemClass, bulletsInItem, bulletsInItem - need);
                        bulletsInItem -= need;
                        consumed += need;
                        break;
                    }
                }
            }
            break;
        }
        DevMsg("Inventory: total consumed %d bullets of type %s for player %d\n", consumed, ammoTypeName ? ammoTypeName : "(null)", idx);
        return consumed;
    }

    void SyncPlayerAmmoWithInventory(CBasePlayer *pPlayer)
    {
        if (!pPlayer) return;
        
        // Get ammo counts for each ammo type from inventory
        static const char *ammoTypes[] = {
            "Pistol", "SMG1", "AR2", "357", "XBowBolt", 
            "Buckshot", "RPG_Round", "SMG1_Grenade", "AR2AltFire", NULL
        };

        for (int i = 0; ammoTypes[i] != NULL; ++i)
        {
                int invCount = GetInventoryAmmoCount(pPlayer, ammoTypes[i]);
            int ammoIndex = GetAmmoDef()->Index(ammoTypes[i]);
            if (ammoIndex >= 0)
            {
                // Set player's ammo reserve to match inventory bullets
                pPlayer->SetAmmoCount(invCount, ammoIndex);
            }
        }
    }

    // Add a weapon entry into the player's inventory with explicit clip/reserve state
    void AddWeaponToPlayer(CBasePlayer *pPlayer, const char *itemName, int clip1, int clip2, int reserveAmmoIndex, int reserveAmmoCount)
    {
        if (!pPlayer || !itemName || !itemName[0]) return;
        PlayerInv *inv = FindOrCreateInventory(pPlayer);
        if (!inv) return;
        InvItem it;
        it.classname = itemName;
        it.bullets = 0;
        it.isWeapon = true;
        it.weaponClip1 = clip1;
        it.weaponClip2 = clip2;
        it.reserveAmmoIndex = reserveAmmoIndex;
        it.reserveAmmoCount = reserveAmmoCount;
        inv->items.AddToTail(it);
    }

    static PlayerInv *FindOrCreateInventory(CBasePlayer *pPlayer)
    {
        if (!pPlayer) return NULL;
        int pidx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex == pidx)
                return &g_PlayerInventories[i];
        }
        // Add a default element and initialize in-place to avoid copying
        int newIndex = g_PlayerInventories.AddToTail();
        g_PlayerInventories[newIndex].entIndex = pidx;
        return &g_PlayerInventories[newIndex];
    }

    void SetEquippedSuitClass(CBasePlayer *pPlayer, const char *suitClassname)
    {
        if (!pPlayer)
            return;

        const int idx = pPlayer->entindex();
        for (int i = 0; i < g_EquippedSuitByPlayer.Count(); ++i)
        {
            if (g_EquippedSuitByPlayer[i].entIndex == idx)
            {
                g_EquippedSuitByPlayer[i].suitClass = suitClassname && suitClassname[0] ? AllocPooledString(suitClassname) : NULL_STRING;
                return;
            }
        }

        int newIndex = g_EquippedSuitByPlayer.AddToTail();
        g_EquippedSuitByPlayer[newIndex].entIndex = idx;
        g_EquippedSuitByPlayer[newIndex].suitClass = suitClassname && suitClassname[0] ? AllocPooledString(suitClassname) : NULL_STRING;
    }

    const char *GetEquippedSuitClass(CBasePlayer *pPlayer)
    {
        if (!pPlayer)
            return "";

        const int idx = pPlayer->entindex();
        for (int i = 0; i < g_EquippedSuitByPlayer.Count(); ++i)
        {
            if (g_EquippedSuitByPlayer[i].entIndex == idx)
            {
                return g_EquippedSuitByPlayer[i].suitClass != NULL_STRING ? STRING(g_EquippedSuitByPlayer[i].suitClass) : "";
            }
        }

        return "";
    }

    void ClearEquippedSuitClass(CBasePlayer *pPlayer)
    {
        SetEquippedSuitClass(pPlayer, NULL);
    }

    static float GetClassUnitWeightKg(const char *classname, bool bAmmoUnit)
    {
        if (!classname || !classname[0])
            return 0.0f;

        if (!g_bWeightTableLoaded)
            LoadInventoryWeightTable();

        int found = g_WeightKgByClass.Find(classname);
        if (found != g_WeightKgByClass.InvalidIndex())
            return g_WeightKgByClass[found];

        if (bAmmoUnit)
            return sv_inventory_weight_default_bullet.GetFloat();

        if (!Q_strnicmp(classname, "weapon_", 7))
            return sv_inventory_weight_default_weapon.GetFloat();
        if (!Q_strnicmp(classname, "item_", 5))
            return sv_inventory_weight_default_item.GetFloat();
        return sv_inventory_weight_default_other.GetFloat();
    }

    float GetPlayerInventoryWeight(CBasePlayer *pPlayer)
    {
        if (!pPlayer)
            return 0.0f;

        float total = 0.0f;
        PlayerInv *inv = FindOrCreateInventory(pPlayer);
        if (inv)
        {
            for (int i = 0; i < inv->items.Count(); ++i)
            {
                const InvItem &it = inv->items[i];
                const char *cls = it.classname.Get();
                if (IsAmmoItem(cls))
                {
                    const int bullets = Max(0, it.bullets);
                    total += GetClassUnitWeightKg(cls, true) * (float)bullets;
                }
                else
                {
                    total += GetClassUnitWeightKg(cls, false);
                }
            }
        }

        if (sv_inventory_weight_include_equipped_weapons.GetBool())
        {
            for (int i = 0; i < pPlayer->WeaponCount(); ++i)
            {
                CBaseCombatWeapon *pWeap = pPlayer->GetWeapon(i);
                if (!pWeap)
                    continue;
                total += GetClassUnitWeightKg(pWeap->GetClassname(), false);
            }
        }

        if (sv_inventory_weight_include_equipped_suit.GetBool() && pPlayer->IsSuitEquipped())
        {
            const char *suitClass = GetEquippedSuitClass(pPlayer);
            if (!suitClass || !suitClass[0])
                suitClass = "item_suit";
            total += GetClassUnitWeightKg(suitClass, false);
        }

        return total;
    }

    float GetPlayerMaxCarryWeight(CBasePlayer *pPlayer)
    {
        return sv_inventory_weight_max.GetFloat();
    }

    bool IsPlayerOverencumbered(CBasePlayer *pPlayer)
    {
        if (!sv_inventory_weight_enable.GetBool())
            return false;
        const float maxW = GetPlayerMaxCarryWeight(pPlayer);
        if (maxW <= 0.0f)
            return false;
        return GetPlayerInventoryWeight(pPlayer) > maxW;
    }

    struct OverencumberedEntry
    {
        int entIndex;
        bool over;
    };
    static CUtlVector<OverencumberedEntry> g_OverencumberedState;

    bool UpdateOverencumberedState(CBasePlayer *pPlayer, bool bOverencumbered)
    {
        if (!pPlayer)
            return false;

        bool bPrev = false;
        const int idx = pPlayer->entindex();
        for (int i = 0; i < g_OverencumberedState.Count(); ++i)
        {
            if (g_OverencumberedState[i].entIndex != idx)
                continue;
            bPrev = g_OverencumberedState[i].over;
            g_OverencumberedState[i].over = bOverencumbered;
            goto done_update;
        }
        {
            int newIndex = g_OverencumberedState.AddToTail();
            g_OverencumberedState[newIndex].entIndex = idx;
            g_OverencumberedState[newIndex].over = bOverencumbered;
        }
    done_update:

        if (bOverencumbered && !bPrev && sv_inventory_weight_over_toast.GetBool())
        {
            CUtlString msg = EscapeForQuotedArg("You are overencumbered");
            engine->ClientCommand(pPlayer->edict(), UTIL_VarArgs("ui_toast \"inventory\" \"%s\" \"%s\"\n", msg.Get(), sv_inventory_weight_over_toast_sound.GetString()));
        }
        return bPrev;
    }

    void AddItemToPlayer(CBasePlayer *pPlayer, const char *itemName)
    {
        if (!pPlayer || !itemName || !itemName[0]) return;
        if (quest_reward_debug.GetBool())
            DevMsg("Inventory: AddItemToPlayer player=%d item='%s'\n", pPlayer->entindex(), itemName);
        PlayerInv *inv = FindOrCreateInventory(pPlayer);
        if (!inv) return;
        InvItem it;
        it.classname = itemName;
        if (IsAmmoItem(itemName))
            it.bullets = GetBulletsPerAmmoItem(itemName);
        else
            it.bullets = 0;
        it.isWeapon = false;
        it.weaponClip1 = 0;
        it.weaponClip2 = 0;
        it.reserveAmmoIndex = -1;
        it.reserveAmmoCount = 0;
        inv->items.AddToTail(it);
        
        // If an ammo item was added, sync player's ammo counts
        if (IsAmmoItem(itemName))
        {
            SyncPlayerAmmoWithInventory(pPlayer);
        }
    }

    void DebugDumpPlayerInventory(CBasePlayer *pPlayer)
    {
        if (!pPlayer) return;
        int idx = pPlayer->entindex();
        DevMsg("Inventory: DebugDump for player %s (ent=%d):\n", pPlayer->GetPlayerName(), idx);
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
            for (int j = 0; j < g_PlayerInventories[i].items.Count(); ++j)
            {
                DevMsg("  - %s : %d bullets\n", g_PlayerInventories[i].items[j].classname.Get(), g_PlayerInventories[i].items[j].bullets);
            }
            return;
        }
        DevMsg("  (no inventory entries)\n");
    }

    // Add an item with an explicit bullets count (used when picking up dropped packs)
    void AddItemToPlayerWithBullets(CBasePlayer *pPlayer, const char *itemName, int bullets)
    {
        if (!pPlayer || !itemName || !itemName[0]) return;
        if (quest_reward_debug.GetBool())
            DevMsg("Inventory: AddItemToPlayerWithBullets player=%d item='%s' bullets=%d\n", pPlayer->entindex(), itemName, bullets);
        PlayerInv *inv = FindOrCreateInventory(pPlayer);
        if (!inv) return;
        // If an existing ammo entry of the same classname exists, stack bullets
        if (IsAmmoItem(itemName))
        {
            for (int i = 0; i < inv->items.Count(); ++i)
            {
                if (!Q_stricmp(inv->items[i].classname.Get(), itemName))
                {
                    inv->items[i].bullets += bullets;
                    DevMsg("Inventory: stacked %d bullets into existing '%s' for player %d (new total=%d)\n", bullets, itemName, pPlayer->entindex(), inv->items[i].bullets);
                    // Sync player's ammo display
                    SyncPlayerAmmoWithInventory(pPlayer);
                    return;
                }
            }
        }

        // No existing entry found (or not an ammo item) - create a new entry
        InvItem it;
        it.classname = itemName;
        if (IsAmmoItem(itemName))
            it.bullets = bullets;
        else
            it.bullets = 0;
        it.isWeapon = false;
        it.weaponClip1 = 0;
        it.weaponClip2 = 0;
        it.reserveAmmoIndex = -1;
        it.reserveAmmoCount = 0;
        inv->items.AddToTail(it);
        DevMsg("Inventory: AddItemToPlayerWithBullets added '%s' with %d bullets for player %d\n", itemName, it.bullets, pPlayer->entindex());
        if (IsAmmoItem(itemName))
        {
            SyncPlayerAmmoWithInventory(pPlayer);
        }
    }

    int RemoveItemFromPlayer(CBasePlayer *pPlayer, const char *itemName)
    {
        if (!pPlayer || !itemName || !itemName[0]) return -1;
        bool wasAmmo = IsAmmoItem(itemName);
        int idx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
                // Iterate in reverse so we remove the most-recently added matching pack
                for (int j = g_PlayerInventories[i].items.Count() - 1; j >= 0; --j)
                {
                    if (!Q_stricmp(g_PlayerInventories[i].items[j].classname.Get(), itemName))
                    {
                        int bullets = g_PlayerInventories[i].items[j].bullets;
                        DevMsg("Inventory: RemoveItemFromPlayer removing '%s' with %d bullets for player %d\n", itemName, bullets, idx);
                        g_PlayerInventories[i].items.Remove(j);
                        // If ammo was removed, sync player's ammo counts
                        if (wasAmmo)
                        {
                            SyncPlayerAmmoWithInventory(pPlayer);
                        }
                        // Return remaining bullets stored in removed item (or 0 for non-ammo)
                        return bullets;
                    }
                }
        }
        return -1;
    }

    // Remove a matching inventory entry and return the full InvItem data.
    // Returns true if an entry was removed and filled into outItem.
    static bool PopInventoryEntry(CBasePlayer *pPlayer, const char *itemName, InvItem &outItem)
    {
        if (!pPlayer || !itemName || !itemName[0]) return false;
        int idx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
            for (int j = g_PlayerInventories[i].items.Count() - 1; j >= 0; --j)
            {
                if (!Q_stricmp(g_PlayerInventories[i].items[j].classname.Get(), itemName))
                {
                    outItem = g_PlayerInventories[i].items[j];
                    g_PlayerInventories[i].items.Remove(j);
                    // If it was ammo, sync ammo counts
                    if (IsAmmoItem(itemName))
                        SyncPlayerAmmoWithInventory(pPlayer);
                    return true;
                }
            }
        }
        return false;
    }

    // Helper to escape for quoted arg (small duplicate of dialog helper)
    static CUtlString EscapeForQuotedArg(const char *s)
    {
        CUtlString out;
        if (!s) return out;
        for (const char *p = s; *p; ++p)
        {
            if (*p == '\\' || *p == '"')
            {
                out.Append("\\");
            }
            char tmp[2] = { *p, '\0' };
            out.Append(tmp);
        }
        return out;
    }

    void SendInventoryToPlayer(CBasePlayer *pPlayer)
    {
        if (!pPlayer)
            return;

        CUtlVector<CUtlString> items;
        GetInventoryItemStringsForPlayer(pPlayer, items);
        if (quest_reward_debug.GetBool())
            DevMsg("Inventory: SendInventoryToPlayer player=%d itemStrings=%d\n", pPlayer->entindex(), items.Count());

        CSingleUserRecipientFilter user(pPlayer);
        user.MakeReliable();

        UserMessageBegin(user, "Inventory_Update");
        WRITE_BYTE(kInventoryUpdate_InventoryBegin);
        MessageEnd();

        for (int i = 0; i < items.Count(); ++i)
        {
            UserMessageBegin(user, "Inventory_Update");
            WRITE_BYTE(kInventoryUpdate_InventoryAdd);
            WRITE_STRING(items[i].Get());
            MessageEnd();
        }

        UserMessageBegin(user, "Inventory_Update");
        WRITE_BYTE(kInventoryUpdate_InventoryEnd);
        MessageEnd();

        UserMessageBegin(user, "Inventory_Update");
        WRITE_BYTE(kInventoryUpdate_InventoryWeight);
        WRITE_FLOAT(GetPlayerInventoryWeight(pPlayer));
        WRITE_FLOAT(GetPlayerMaxCarryWeight(pPlayer));
        WRITE_BYTE(IsPlayerOverencumbered(pPlayer) ? 1 : 0);
        MessageEnd();
    }

    void GetInventoryItemStringsForPlayer(CBasePlayer *pPlayer, CUtlVector<CUtlString> &outItems)
    {
        outItems.RemoveAll();
        if (!pPlayer)
            return;
        int idx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx)
                continue;
            for (int j = 0; j < g_PlayerInventories[i].items.Count(); ++j)
            {
                const InvItem &it = g_PlayerInventories[i].items[j];
                char buf[256];
                if (it.isWeapon)
                {
                    Q_snprintf(buf, sizeof(buf), "%s:%d:%d:%d:%d:%d",
                        it.classname.Get(),
                        it.bullets,
                        it.weaponClip1,
                        it.weaponClip2,
                        it.reserveAmmoIndex,
                        it.reserveAmmoCount);
                }
                else
                {
                    Q_snprintf(buf, sizeof(buf), "%s:%d", it.classname.Get(), it.bullets);
                }
                outItems.AddToTail(CUtlString(buf));
            }
            break;
        }
    }

    CUtlString GetInventoryPayloadForPlayer(CBasePlayer *pPlayer)
    {
        CUtlString payload;
        if (!pPlayer) return payload;
        int idx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
            for (int j = 0; j < g_PlayerInventories[i].items.Count(); ++j)
            {
                if (payload.Get()[0]) payload.Append(";");
                payload.Append(g_PlayerInventories[i].items[j].classname.Get());
                char numbuf[128];
                Q_snprintf(numbuf, sizeof(numbuf), ":%d", g_PlayerInventories[i].items[j].bullets);
                payload.Append(numbuf);
                if (g_PlayerInventories[i].items[j].isWeapon)
                {
                    Q_snprintf(numbuf, sizeof(numbuf), ":%d:%d:%d:%d",
                              g_PlayerInventories[i].items[j].weaponClip1,
                              g_PlayerInventories[i].items[j].weaponClip2,
                              g_PlayerInventories[i].items[j].reserveAmmoIndex,
                              g_PlayerInventories[i].items[j].reserveAmmoCount);
                    payload.Append(numbuf);
                }
            }
            break;
        }
        return payload;
    }

    int ConsumeDroppedAmmoForEntity(CBaseEntity *pEnt)
    {
        if (!pEnt) return 0;
        // If this is our wrapper entity, return its stored bullets directly
        CInventoryDroppedAmmo *pDrop = dynamic_cast<CInventoryDroppedAmmo*>(pEnt);
        if (pDrop)
        {
            int b = pDrop->m_iBullets;
            DevMsg("Inventory: ConsumeDroppedAmmoForEntity (wrapper) found %d bullets for ent %d\n", b, pEnt->entindex());
            pDrop->m_iBullets = 0;
            return b;
        }
        for (int i = 0; i < g_DroppedAmmoMap.Count(); ++i)
        {
            if (g_DroppedAmmoMap[i].pEnt == pEnt)
            {
                int bullets = g_DroppedAmmoMap[i].bullets;
                DevMsg("Inventory: ConsumeDroppedAmmoForEntity found %d bullets for ent %d\n", bullets, pEnt->entindex());
                g_DroppedAmmoMap.Remove(i);
                return bullets;
            }
        }
        return 0;
    }

    void RecordDroppedWeapon(CBaseEntity *pEnt, int clip1, int clip2, int reserveAmmoIndex, int reserveAmmoCount)
    {
        if (!pEnt) return;
        DroppedEntityInfo e;
        e.pEnt = pEnt;
        e.bullets = 0;
        e.isWeapon = true;
        e.weaponClip1 = clip1;
        e.weaponClip2 = clip2;
        e.reserveAmmoIndex = reserveAmmoIndex;
        e.reserveAmmoCount = reserveAmmoCount;
        g_DroppedAmmoMap.AddToTail(e);
        DevMsg("Inventory: RecordDroppedWeapon stored ent %d clip1=%d clip2=%d reserveIndex=%d reserveCount=%d\n", pEnt->entindex(), clip1, clip2, reserveAmmoIndex, reserveAmmoCount);
    }

    // Helper: add the dropped weapon's magazine bullets into the dropper's
    // inventory as an ammo item so magazine rounds are not lost when G-dropping.
    void AddDroppedWeaponClipToInventory(CBasePlayer *pPlayer, int ammoIndex, int clipBullets)
    {
        // Respect server toggle: if disabled, do nothing here to keep
        // magazine bullets with the world weapon instead of moving them
        // to the player's inventory.
        if (!sv_inventory_move_clip_to_inventory.GetBool())
            return;
        if (!pPlayer || clipBullets <= 0 || ammoIndex < 0)
            return;

        // Map ammo index to a reasonable small ammo item classname.
        const char *classname = NULL;
        if (ammoIndex == GetAmmoDef()->Index("Pistol"))
            classname = "item_ammo_pistol";
        else if (ammoIndex == GetAmmoDef()->Index("SMG1"))
            classname = "item_ammo_smg1";
        else if (ammoIndex == GetAmmoDef()->Index("AR2"))
            classname = "item_ammo_ar2";
        else if (ammoIndex == GetAmmoDef()->Index("357"))
            classname = "item_ammo_357";
        else if (ammoIndex == GetAmmoDef()->Index("XBowBolt"))
            classname = "item_ammo_crossbow";
        else if (ammoIndex == GetAmmoDef()->Index("Buckshot"))
            classname = "item_box_buckshot";
        else if (ammoIndex == GetAmmoDef()->Index("RPG_Round"))
            classname = "item_rpg_round";
        else if (ammoIndex == GetAmmoDef()->Index("SMG1_Grenade"))
            classname = "item_ammo_smg1_grenade";
        else if (ammoIndex == GetAmmoDef()->Index("AR2AltFire"))
            classname = "item_ammo_ar2_altfire";

        if (!classname)
        {
            DevMsg("Inventory: unknown ammo index %d; not adding dropped clip to inventory\n", ammoIndex);
            return;
        }

        DevMsg("Inventory: adding dropped weapon clip to player %d as %s (%d bullets)\n", pPlayer->entindex(), classname, clipBullets);
        AddItemToPlayerWithBullets(pPlayer, classname, clipBullets);
        // Sync player's ammo counts so the HUD reflects inventory state
        SyncPlayerAmmoWithInventory(pPlayer);
    }

    const char *GetFriendlyItemName(const char *classname)
    {
        static char buf[256];
        if (!classname || !classname[0])
            return "(unknown)";

        // Weapons: strip "weapon_" and prettify
        if (!Q_strnicmp(classname, "weapon_", 7))
        {
            const char *name = classname + 7;
            Q_strncpy(buf, name, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            // Capitalize first letter
            if (buf[0] >= 'a' && buf[0] <= 'z')
                buf[0] = buf[0] - 'a' + 'A';
            return buf;
        }

        // Items: strip "item_" and prettify
        if (!Q_strnicmp(classname, "item_", 5))
        {
            const char *name = classname + 5;
            Q_strncpy(buf, name, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            // Capitalize first letter
            if (buf[0] >= 'a' && buf[0] <= 'z')
                buf[0] = buf[0] - 'a' + 'A';
            return buf;
        }

        // Default: return as-is
        Q_strncpy(buf, classname, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }

    void NotifyPlayerPickup(CBasePlayer *pPlayer, const char *itemName)
    {
        NotifyPlayerPickup(pPlayer, itemName, 1);
    }

    void NotifyPlayerPickup(CBasePlayer *pPlayer, const char *itemName, int count)
    {
        if (!pPlayer || !itemName) 
        {
            DevMsg("Inventory: NotifyPlayerPickup - invalid player or item\n");
            return;
        }

        if (count < 1) count = 1;
        
        DevMsg("Inventory: NotifyPlayerPickup called for player %s, item %s\n", pPlayer->GetPlayerName(), itemName);
        
        // Get friendly display name for the item
        const char *friendlyName = GetFriendlyItemName(itemName);
        if (!friendlyName || !friendlyName[0])
            friendlyName = itemName;
        
        DevMsg("Inventory: sending pickup notification '%s'\n", friendlyName);

        CUtlString msg = EscapeForQuotedArg(UTIL_VarArgs("PICKUP|%s|%d", friendlyName, count));
        engine->ClientCommand(pPlayer->edict(), UTIL_VarArgs("ui_toast \"pickup\" \"%s\" \"\"", msg.Get()));
    }

    void NotifyPlayerReward(CBasePlayer *pPlayer, const char *itemName, int count)
    {
        if (!pPlayer || !itemName) 
        {
            DevMsg("Inventory: NotifyPlayerReward - invalid player or item\n");
            return;
        }

        if (count < 1) count = 1;

        const char *friendlyName = GetFriendlyItemName(itemName);
        if (!friendlyName || !friendlyName[0])
            friendlyName = itemName;

        CUtlString msg = EscapeForQuotedArg(UTIL_VarArgs("REWARD|%s|%d", friendlyName, count));
        engine->ClientCommand(pPlayer->edict(), UTIL_VarArgs("ui_toast \"reward\" \"%s\" \"\"", msg.Get()));
    }

}

// Server-side command handlers
static void CC_Inventory_Request(const CCommand &args)
{
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_request_cc("inventory_request", CC_Inventory_Request, "Request inventory (client->server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_UIOpen(const CCommand &args)
{
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;
    const bool bOpen = (args.ArgC() >= 2) ? (atoi(args.Arg(1)) != 0) : false;
    pPlayer->SetInventoryUIOpen(bOpen);
}
static ConCommand inventory_ui_open_cc("inventory_ui_open", CC_Inventory_UIOpen, "Set inventory UI open state (client->server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_Use(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *item = args.Arg(1);
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;
    
    // AMMO ITEMS CANNOT BE USED - they can only be picked up and dropped
    // Ammo is automatically synced with weapon ammo counts
    if (InventorySystem::IsAmmoItem(item))
    {
        DevMsg("Inventory: cannot use ammo item %s - ammo is automatically synced with weapons\n", item);
        // Just refresh the inventory panel, don't consume the item
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }
    
    DevMsg("Inventory: player %s used item %s\n", pPlayer->GetPlayerName(), item);
    // New behavior: 'Use' spawns the world entity (like dropping it) so the
    // player can pick it up immediately as a world item (gives armor/ammo/etc.).
    // Remove from inventory first, then spawn into the world. If removal fails
    // the player doesn't actually have the item (desync/consumed), so abort.
    // For weapons, prefer to pop the full inventory entry so we can restore
    // stored clip/reserve state when giving the weapon back to the player.
    InventorySystem::InvItem poppedItem;
    bool hadEntry = true;
    bool entryIsWeapon = (Q_strnicmp(item, "weapon_", 7) == 0);
    if (entryIsWeapon)
    {
        hadEntry = InventorySystem::PopInventoryEntry(pPlayer, item, poppedItem);
        if (!hadEntry)
        {
            DevMsg("Inventory: use failed - player %s does not have item %s\n", pPlayer->GetPlayerName(), item);
            InventorySystem::SendInventoryToPlayer(pPlayer);
            return;
        }
    }
    else
    {
        if (InventorySystem::RemoveItemFromPlayer(pPlayer, item) == -1)
        {
            DevMsg("Inventory: use failed - player %s does not have item %s\n", pPlayer->GetPlayerName(), item);
            InventorySystem::SendInventoryToPlayer(pPlayer);
            return;
        }
    }


    // Prefer giving the item/weapon directly to the player so it is added to
    // their loadout immediately. `GiveNamedItem` handles weapons and many
    // items correctly. For generic items that return a world entity, parent
    // it to the player so it follows them (and becomes usable/touchable).
    CBaseEntity *pGiven = pPlayer->GiveNamedItem(item);
    if (pGiven)
    {
        DevMsg("inventory_use: GiveNamedItem returned ent %d (classname=%s)\n", pGiven->entindex(), pGiven->GetClassname() ? pGiven->GetClassname() : "(null)");
        if (pGiven->IsBaseCombatWeapon())
        {
            // Weapon was created/given to the player. Ensure it's equipped.
            CBaseCombatWeapon *pWeap = dynamic_cast<CBaseCombatWeapon*>(pGiven);
            if (pWeap)
            {
                pPlayer->Weapon_Equip(pWeap);
                // If we popped a stored inventory weapon entry, restore its clips
                // and the player's reserve ammo that was recorded at drop time.
                if (entryIsWeapon && poppedItem.classname.Get()[0])
                {
                    pWeap->m_iClip1 = poppedItem.weaponClip1;
                    pWeap->m_iClip2 = poppedItem.weaponClip2;
                    if (poppedItem.reserveAmmoIndex >= 0)
                    {
                        pPlayer->SetAmmoCount(poppedItem.reserveAmmoCount, poppedItem.reserveAmmoIndex);
                    }
                }
                else
                {
                    // Ensure player's ammo reserve matches inventory (so reload uses inventory)
                    InventorySystem::SyncPlayerAmmoWithInventory(pPlayer);
                    // If weapon uses clips, keep the clip empty so reserve shows in inventory
                    if (pWeap->UsesClipsForAmmo1())
                    {
                        pWeap->m_iClip1 = 0;
                    }
                    if (pWeap->UsesClipsForAmmo2())
                    {
                        pWeap->m_iClip2 = 0;
                    }
                }
                // Ensure viewmodel is created and visible immediately after equip
                pPlayer->CreateViewModel();
                CBaseViewModel *vm = pPlayer->GetViewModel();
                if (vm)
                {
                    vm->RemoveEffects( EF_NODRAW );
                }
            }
        }
        else
        {
            // Non-weapon: for world "item_" entities (health, battery, etc.)
            // spawn them in front of the player and leave their touch enabled
            // so the player immediately picks them up and receives the effect.
            if (Q_strnicmp(item, "item_", 5) == 0)
            {
                Vector forward;
                AngleVectors(pPlayer->EyeAngles(), &forward);
                Vector spawnPos = pPlayer->EyePosition() + forward * 48.0f;
                QAngle ang = pPlayer->GetAbsAngles();
                pGiven->SetAbsOrigin(spawnPos);
                pGiven->SetAbsVelocity(pPlayer->GetAbsVelocity());
                pGiven->SetAbsAngles(ang);
                pGiven->AddSpawnFlags(SF_ITEM_ALWAYS_TOUCHABLE);
                // leave touch enabled so MyTouch() will trigger
            }
            else
            {
                // Fallback: parent to player but disable its touch so it won't be
                // automatically picked up by touching; inventory commands control pickup.
                pGiven->SetParent(pPlayer);
                pGiven->SetTouch(NULL);
                pGiven->SetUse(NULL);
            }
        }
    }
    else
    {
        // Fallback: create a world entity and parent it to the player so it
        // behaves like other inventory items if GiveNamedItem failed.
        DevMsg("inventory_use: GiveNamedItem failed for %s, falling back to spawn\n", item);
        Vector forward;
        AngleVectors(pPlayer->EyeAngles(), &forward);
        Vector spawnPos = pPlayer->EyePosition() + forward * 48.0f;
        QAngle ang = pPlayer->GetAbsAngles();

        CBaseEntity *pEnt = CBaseEntity::Create(item, spawnPos, ang);
        if (pEnt)
        {
            pEnt->SetAbsVelocity(pPlayer->GetAbsVelocity());
            // If this is an item_ entity, place it in front of the player
            // and leave touch enabled so it will immediately apply (use).
            if (Q_strnicmp(item, "item_", 5) == 0)
            {
                pEnt->SetAbsOrigin(spawnPos);
                pEnt->AddSpawnFlags(SF_ITEM_ALWAYS_TOUCHABLE);
                // leave touch enabled
            }
            else
            {
                pEnt->SetParent(pPlayer);
                pEnt->SetTouch(NULL);
                pEnt->SetUse(NULL);
            }
        }
    }

    // Update client view
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_use_cc("inventory_use", CC_Inventory_Use, "Use inventory item (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_Drop(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *item = args.Arg(1);
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;

    // Debug: dump player's inventory before removal so we can see stored bullets
    InventorySystem::DebugDumpPlayerInventory(pPlayer);

    // Remove from inventory; if the player doesn't actually have the item,
    // abort the drop to avoid spawning a fresh world entity and duplicating.
    int droppedBullets = 0;
    InventorySystem::InvItem poppedItem; // temporary
    bool hadEntry = true;
    bool entryIsWeapon = (Q_strnicmp(item, "weapon_", 7) == 0);
    if (entryIsWeapon)
    {
        hadEntry = InventorySystem::PopInventoryEntry(pPlayer, item, poppedItem);
        if (hadEntry)
        {
            // weapon entries don't use bullets field for ammo packs
            droppedBullets = 0;
        }
    }
    else
    {
        droppedBullets = InventorySystem::RemoveItemFromPlayer(pPlayer, item);
        if (droppedBullets == -1) hadEntry = false;
    }

    DevMsg("Inventory: CC_Inventory_Drop: player %s removed item %s -> droppedBullets=%d (hadEntry=%d)\n", pPlayer->GetPlayerName(), item, droppedBullets, hadEntry ? 1 : 0);
    if (!hadEntry)
    {
        DevMsg("Inventory: drop failed - player %s does not have item %s\n", pPlayer->GetPlayerName(), item);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }

    {
        const char *friendlyName = InventorySystem::GetFriendlyItemName(item);
        if (!friendlyName || !friendlyName[0])
            friendlyName = item;
        CUtlString msg = InventorySystem::EscapeForQuotedArg(UTIL_VarArgs("Removed: %s", friendlyName));
        engine->ClientCommand(pPlayer->edict(), UTIL_VarArgs("ui_toast \"inventory\" \"%s\" \"friends/message.wav\"", msg.Get()));
    }

    // Spawn the entity back into the world in front of the player
    Vector forward;
    AngleVectors(pPlayer->EyeAngles(), &forward);
    Vector spawnPos = pPlayer->EyePosition() + forward * 48.0f;
    QAngle ang = pPlayer->GetAbsAngles();

    // Create the entity by classname.
    // For weapons: use GiveNamedItem + Weapon_Drop for proper physics/placement.
    // For items (item_*): directly create in world to avoid triggering item effects
    // like health/armor auto-apply.
    const char *itemClass = item;
    bool isWeapon = (Q_strnicmp(itemClass, "weapon_", 7) == 0);
    
    if (isWeapon)
    {
        // Weapon: give it, then drop it using normal weapon drop logic
        CBaseEntity *pGiven = NULL;
        if (poppedItem.classname.Get()[0])
        {
            // If we popped a stored weapon entry, give the weapon and apply stored clips
            pGiven = pPlayer->GiveNamedItem(item);
            if (pGiven && pGiven->IsBaseCombatWeapon())
            {
                CBaseCombatWeapon *pWeap = dynamic_cast<CBaseCombatWeapon*>(pGiven);
                if (pWeap)
                {
                    // restore stored clip values onto the temporary given weapon
                    pWeap->m_iClip1 = poppedItem.weaponClip1;
                    pWeap->m_iClip2 = poppedItem.weaponClip2;
                    if (poppedItem.reserveAmmoIndex >= 0)
                    {
                        pPlayer->SetAmmoCount(poppedItem.reserveAmmoCount, poppedItem.reserveAmmoIndex);
                    }
                }
            }
        }
        else
        {
            pGiven = pPlayer->GiveNamedItem(item);
        }
        if (pGiven && pGiven->IsBaseCombatWeapon())
        {
            CBaseCombatWeapon *pWeap = dynamic_cast<CBaseCombatWeapon*>(pGiven);
            if (pWeap)
            {
                // Record weapon state (clip and player's reserve for its ammo) BEFORE calling Weapon_Drop
                InventorySystem::DroppedEntityInfo e;
                e.pEnt = pWeap; e.bullets = 0; e.isWeapon = true;
                e.weaponClip1 = pWeap->m_iClip1;
                e.weaponClip2 = pWeap->m_iClip2;
                int primType = pWeap->GetPrimaryAmmoType();
                e.reserveAmmoIndex = primType;
                e.reserveAmmoCount = (primType >= 0) ? pPlayer->GetAmmoCount(primType) : 0;
                // Now drop the weapon so the world entity is created
                pPlayer->Weapon_Drop(pWeap, NULL, &pPlayer->GetAbsVelocity());
                pWeap->SetTouch(NULL);
                pWeap->SetUse(NULL);

                // After Weapon_Drop, the weapon instance should refer to the world entity.
                e.pEnt = pWeap;
                InventorySystem::g_DroppedAmmoMap.AddToTail(e);
                // Legacy behavior: transfer the dropped weapon's magazine bullets
                // into the player's inventory as ammo items so they are not lost.
                // This is guarded by the server ConVar `sv_inventory_move_clip_to_inventory`.
                if (sv_inventory_move_clip_to_inventory.GetBool())
                {
                    if (e.reserveAmmoIndex >= 0 && e.weaponClip1 > 0)
                    {
                        InventorySystem::AddDroppedWeaponClipToInventory(pPlayer, e.reserveAmmoIndex, e.weaponClip1);
                    }
                    if (e.reserveAmmoIndex >= 0 && e.weaponClip2 > 0)
                    {
                        // Secondary clip (if present) is treated the same way
                        InventorySystem::AddDroppedWeaponClipToInventory(pPlayer, e.reserveAmmoIndex, e.weaponClip2);
                    }
                }
                DevMsg("Inventory: recorded weapon drop for ent %d ('%s') clip1=%d clip2=%d reserveIndex=%d reserveCount=%d\n", pWeap->entindex(), pWeap->GetClassname() ? pWeap->GetClassname() : "(null)", e.weaponClip1, e.weaponClip2, e.reserveAmmoIndex, e.reserveAmmoCount);
            }
        }
        else if (pGiven)
        {
            // Fallback: just place it in world without applying effects
            pGiven->SetAbsOrigin(spawnPos);
            pGiven->SetAbsVelocity(pPlayer->GetAbsVelocity());
            pGiven->SetTouch(NULL);
                pGiven->SetUse(NULL);
        }
        else
        {
            // Failed to give: create directly
            CBaseEntity *pEnt = CBaseEntity::Create(item, spawnPos, ang);
            if (pEnt)
            {
                pEnt->SetAbsVelocity(pPlayer->GetAbsVelocity());
                pEnt->SetTouch(NULL);
            }
        }
    }
    else
    {
        // Non-weapon item: directly spawn in world without GiveNamedItem
        // to avoid triggering item effects (health kit healing, battery charging armor)
        // If this is an ammo item with a specific stored count, spawn a
        // controlled wrapper entity which will use the stored bullet count
        // on pickup instead of the class-default amount.
        if (droppedBullets > 0 && InventorySystem::IsAmmoItem(itemClass))
        {
            CBaseEntity *pWrap = CBaseEntity::Create("item_inventory_dropped_ammo", spawnPos, ang);
            if (pWrap)
            {
                CInventoryDroppedAmmo *pDrop = dynamic_cast<CInventoryDroppedAmmo*>(pWrap);
                if (pDrop)
                {
                    pDrop->m_iBullets = droppedBullets;
                    Q_strncpy(pDrop->m_szClassname, itemClass ? itemClass : "", sizeof(pDrop->m_szClassname));
                    const char *atype = InventorySystem::GetAmmoTypeForItem(itemClass);
                    pDrop->m_iAmmoIndex = atype ? GetAmmoDef()->Index(atype) : -1;
                    pDrop->m_pszModel = GetModelForAmmoItem(itemClass);
                    pDrop->SetAbsVelocity(pPlayer->GetAbsVelocity());
                    pDrop->SetAbsOrigin(spawnPos);
                    pDrop->Spawn();
                    DevMsg("Inventory: spawned inventory-dropped wrapper ent %d for '%s' with %d bullets\n", pDrop->entindex(), itemClass, droppedBullets);
                    if (!Q_stricmp(itemClass, "item_box_buckshot"))
                    {
                        DevMsg("Inventory: buckshot wrapper created ent %d storing %d shells\n", pDrop->entindex(), droppedBullets);
                    }
                }
                else
                {
                    // fallback: just create raw entity
                    pWrap->SetAbsVelocity(pPlayer->GetAbsVelocity());
                }
            }
        }
        else
        {
            DevMsg("Inventory: spawning raw world entity for '%s' (droppedBullets=%d)\n", itemClass, droppedBullets);
            CBaseEntity *pEnt = CBaseEntity::Create(item, spawnPos, ang);
            if (pEnt)
            {
#if defined(SF_ITEM_NO_PLAYER_PICKUP)
                if (Q_strnicmp(itemClass, "item_", 5) == 0)
                    pEnt->AddSpawnFlags(SF_ITEM_NO_PLAYER_PICKUP);
#endif
                pEnt->SetAbsVelocity(pPlayer->GetAbsVelocity());
                pEnt->SetTouch(NULL);
                pEnt->SetUse(NULL);
            }
        }
    }

    // Update client
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_drop_cc("inventory_drop", CC_Inventory_Drop, "Drop inventory item into world (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

// Client requests server to pick up an entity into inventory
static void CC_Inventory_Pickup(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    int entIndex = atoi(args.Arg(1));
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;

    CBaseEntity *pEnt = UTIL_EntityByIndex(entIndex);
    if (!pEnt) return;

    DevMsg("Inventory: CC_Inventory_Pickup called for ent %d (classname=%s) by player %s\n", entIndex, pEnt->GetClassname() ? pEnt->GetClassname() : "(null)", pPlayer->GetPlayerName());

#if defined(SF_ITEM_NO_PLAYER_PICKUP)
    // Prevent picking up items that were intentionally marked no-player-pickup
    // (e.g., recently dropped inventory items). This avoids duplication exploits
    // where dropped ammo/items could be immediately re-added as full packs.
    if (pEnt->HasSpawnFlags(SF_ITEM_NO_PLAYER_PICKUP))
    {
        DevMsg("Inventory: pickup rejected for ent %d because SF_ITEM_NO_PLAYER_PICKUP set\n", entIndex);
        return;
    }
#endif

    const char *classname = pEnt->GetClassname();
    if (!classname) return;

    // Only accept entities that look like weapons or items
    if (Q_strnicmp(classname, "weapon_", 7) != 0 && Q_strnicmp(classname, "item_", 5) != 0)
    {
        return;
    }

    // First: if this is our dropped-wrapper entity, use its stored classname/bullets
    CInventoryDroppedAmmo *pDrop = dynamic_cast<CInventoryDroppedAmmo*>(pEnt);
    if (pDrop)
    {
        // Add stored bullets/class to player's inventory
        if (pDrop->m_szClassname[0])
        {
            InventorySystem::AddItemToPlayerWithBullets(pPlayer, pDrop->m_szClassname, pDrop->m_iBullets);
            // Notify the player about the pickup so client HUD shows message
            InventorySystem::NotifyPlayerPickup(pPlayer, pDrop->m_szClassname);
        }
        UTIL_Remove(pEnt);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }

    // Next: check if we have a recorded dropped-entity entry (we store this for
    // inventory-drop paths and some weapon drops). If found, restore stored data.
    int foundIndex = -1;
    for (int i = 0; i < InventorySystem::g_DroppedAmmoMap.Count(); ++i)
    {
        if (InventorySystem::g_DroppedAmmoMap[i].pEnt == pEnt)
        {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex != -1)
    {
        InventorySystem::DroppedEntityInfo entInfo = InventorySystem::g_DroppedAmmoMap[foundIndex];
        DevMsg("Inventory: picking up dropped ent %d for '%s' (isWeapon=%d)\n", entIndex, classname, entInfo.isWeapon ? 1 : 0);
        if (entInfo.isWeapon && Q_strnicmp(classname, "weapon_", 7) == 0)
        {
            // Give the weapon to the player directly and restore its clips and
            // the player's reserve ammo stored at drop time.
            CBaseEntity *pGiven = pPlayer->GiveNamedItem(classname);
            if (pGiven && pGiven->IsBaseCombatWeapon())
            {
                CBaseCombatWeapon *pWeap = dynamic_cast<CBaseCombatWeapon*>(pGiven);
                if (pWeap)
                {
                    pWeap->m_iClip1 = entInfo.weaponClip1;
                    pWeap->m_iClip2 = entInfo.weaponClip2;
                    if (entInfo.reserveAmmoIndex >= 0)
                    {
                        pPlayer->SetAmmoCount(entInfo.reserveAmmoCount, entInfo.reserveAmmoIndex);
                    }
                    DevMsg("Inventory: restored weapon clips and reserve for player %d (clip1=%d clip2=%d reserveIndex=%d reserveCount=%d)\n", pPlayer->entindex(), entInfo.weaponClip1, entInfo.weaponClip2, entInfo.reserveAmmoIndex, entInfo.reserveAmmoCount);
                }
                // Notify player they picked up the weapon
                InventorySystem::NotifyPlayerPickup(pPlayer, classname);
            }
        }
        else
        {
            // Ammo pack or generic item: restore stored bullets into inventory
            InventorySystem::AddItemToPlayerWithBullets(pPlayer, classname, entInfo.bullets);
            // Notify the player about the pickup so client HUD shows message
            InventorySystem::NotifyPlayerPickup(pPlayer, classname);
        }

        // remove map entry
        InventorySystem::g_DroppedAmmoMap.Remove(foundIndex);
        UTIL_Remove(pEnt);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }

    // If this is a plain world weapon entity dropped via G (no recorded entry),
    // try to copy its current clips into the given weapon when adding to the
    // player's inventory so the magazine isn't lost.
    if (!Q_strnicmp(classname, "weapon_", 7))
    {
        CBaseCombatWeapon *pWorldWeap = dynamic_cast<CBaseCombatWeapon*>(pEnt);
        int clip1 = -1, clip2 = -1;
        if (pWorldWeap)
        {
            clip1 = pWorldWeap->m_iClip1;
            clip2 = pWorldWeap->m_iClip2;
        }

        // Add the weapon to the player's inventory, preserving clip counts
        InventorySystem::AddWeaponToPlayer(pPlayer, classname, clip1 >= 0 ? clip1 : 0, clip2 >= 0 ? clip2 : 0, -1, 0);

        // Notify player about the weapon pickup
        InventorySystem::NotifyPlayerPickup(pPlayer, classname);

        UTIL_Remove(pEnt);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }

    // Default: add the class name to player's inventory
    InventorySystem::AddItemToPlayer(pPlayer, classname);
    UTIL_Remove(pEnt);

    // Notify player about the pickup
    InventorySystem::NotifyPlayerPickup(pPlayer, classname);

    // Send updated inventory to the player
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_pickup_cc("inventory_pickup", CC_Inventory_Pickup, "Pick up world entity into inventory (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

// Stack two matching ammo entries together (client requests stack via drag)
static void CC_Inventory_Stack(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *item = args.Arg(1);
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;

    // Only handle ammo-like items
    if (!InventorySystem::IsAmmoItem(item))
    {
        DevMsg("Inventory: stack request for non-ammo '%s' ignored\n", item);
        return;
    }

    InventorySystem::InvItem a;
    InventorySystem::InvItem b;

    // If client provided indices, use them to remove exact slots so we don't pop arbitrary entries
    if (args.ArgC() >= 4)
    {
        int src = atoi(args.Arg(2));
        int dst = atoi(args.Arg(3));
        if (src == dst) return;
        // Remove higher index first to avoid shifting
        bool aok = false, bok = false;
        if (src > dst)
        {
            aok = InventorySystem::PopInventoryEntryAtIndex(pPlayer, src, a);
            bok = InventorySystem::PopInventoryEntryAtIndex(pPlayer, dst, b);
        }
        else
        {
            aok = InventorySystem::PopInventoryEntryAtIndex(pPlayer, dst, a);
            bok = InventorySystem::PopInventoryEntryAtIndex(pPlayer, src, b);
        }

        if (aok && bok)
        {
            int total = a.bullets + b.bullets;
            InventorySystem::AddItemToPlayerWithBullets(pPlayer, item, total);
            InventorySystem::SyncPlayerAmmoWithInventory(pPlayer);
            InventorySystem::SendInventoryToPlayer(pPlayer);
            DevMsg("Inventory: stacked %s into %d bullets for player %s (indices %d,%d)\n", item, total, pPlayer->GetPlayerName(), src, dst);
            return;
        }
        else
        {
            if (aok) InventorySystem::AddItemToPlayerWithBullets(pPlayer, a.classname.Get(), a.bullets);
            if (bok) InventorySystem::AddItemToPlayerWithBullets(pPlayer, b.classname.Get(), b.bullets);
            InventorySystem::SendInventoryToPlayer(pPlayer);
            return;
        }
    }

    // Fallback: pop by classname twice (legacy behavior)
    bool hasA = InventorySystem::PopInventoryEntry(pPlayer, item, a);
    bool hasB = InventorySystem::PopInventoryEntry(pPlayer, item, b);
    if (hasA && hasB)
    {
        int total = a.bullets + b.bullets;
        InventorySystem::AddItemToPlayerWithBullets(pPlayer, item, total);
        InventorySystem::SyncPlayerAmmoWithInventory(pPlayer);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        DevMsg("Inventory: stacked %s into %d bullets for player %s\n", item, total, pPlayer->GetPlayerName());
    }
    else
    {
        // If we popped one or none, restore any popped entries
        if (hasA) InventorySystem::AddItemToPlayerWithBullets(pPlayer, a.classname.Get(), a.bullets);
        if (hasB) InventorySystem::AddItemToPlayerWithBullets(pPlayer, b.classname.Get(), b.bullets);
        InventorySystem::SendInventoryToPlayer(pPlayer);
    }
}

    // Remove inventory entry at a specific slot index (server-side ordering must match serialization order)
    bool InventorySystem::PopInventoryEntryAtIndex(CBasePlayer *pPlayer, int slotIndex, InvItem &outItem)
    {
        if (!pPlayer) return false;
        int idx = pPlayer->entindex();
        for (int i = 0; i < g_PlayerInventories.Count(); ++i)
        {
            if (g_PlayerInventories[i].entIndex != idx) continue;
            if (slotIndex < 0 || slotIndex >= g_PlayerInventories[i].items.Count()) return false;
            outItem = g_PlayerInventories[i].items[slotIndex];
            g_PlayerInventories[i].items.Remove(slotIndex);
            if (IsAmmoItem(outItem.classname.Get()))
                SyncPlayerAmmoWithInventory(pPlayer);
            return true;
        }
        return false;
    }
static ConCommand inventory_stack_cc("inventory_stack", CC_Inventory_Stack, "Stack two matching ammo inventory entries (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static bool IsAnyStackableNonWeapon(const InventorySystem::InvItem &it)
{
    return !it.isWeapon && InventorySystem::IsAmmoItem(it.classname.Get());
}

static int GetSortGroupForInvItem(const InventorySystem::InvItem &it)
{
    if (it.isWeapon) return 0;
    if (InventorySystem::IsAmmoItem(it.classname.Get())) return 1;
    return 2;
}

static int __cdecl InventorySortComparator(const void *a, const void *b)
{
    const InventorySystem::InvItem *ia = (const InventorySystem::InvItem*)a;
    const InventorySystem::InvItem *ib = (const InventorySystem::InvItem*)b;
    int ga = GetSortGroupForInvItem(*ia);
    int gb = GetSortGroupForInvItem(*ib);
    if (ga != gb) return ga - gb;
    return Q_stricmp(ia->classname.Get(), ib->classname.Get());
}

static void CC_Inventory_Sort(const CCommand &args)
{
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;

    int idx = pPlayer->entindex();
    for (int i = 0; i < InventorySystem::g_PlayerInventories.Count(); ++i)
    {
        if (InventorySystem::g_PlayerInventories[i].entIndex != idx)
            continue;

        CUtlVector<InventorySystem::InvItem> out;
        CUtlDict<int, int> mapIndexByKey;

        for (int j = 0; j < InventorySystem::g_PlayerInventories[i].items.Count(); ++j)
        {
            const InventorySystem::InvItem &src = InventorySystem::g_PlayerInventories[i].items[j];
            if (IsAnyStackableNonWeapon(src))
            {
                CUtlString key = src.classname;
                int found = mapIndexByKey.Find(key.Get());
                if (found != mapIndexByKey.InvalidIndex())
                {
                    int outIndex = mapIndexByKey[found];
                    out[outIndex].bullets += src.bullets;
                    continue;
                }
                int outIndex = out.AddToTail(src);
                mapIndexByKey.Insert(key.Get(), outIndex);
                continue;
            }
            out.AddToTail(src);
        }

        if (out.Count() > 1)
            qsort(out.Base(), out.Count(), sizeof(InventorySystem::InvItem), InventorySortComparator);

        InventorySystem::g_PlayerInventories[i].items.RemoveAll();
        for (int k = 0; k < out.Count(); ++k)
            InventorySystem::g_PlayerInventories[i].items.AddToTail(out[k]);

        InventorySystem::SyncPlayerAmmoWithInventory(pPlayer);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }
}
static ConCommand inventory_sort_cc("inventory_sort", CC_Inventory_Sort, "Sort and stack inventory items (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_Weight_Reload(const CCommand &args)
{
    InventorySystem::LoadInventoryWeightTable();
}
static ConCommand inventory_weight_reload_cc("inventory_weight_reload", CC_Inventory_Weight_Reload, "Reload inventory weight table (server)", FCVAR_GAMEDLL);

static void CC_Inventory_Unequip_Suit(const CCommand &args)
{
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer)
        return;

    if (!pPlayer->IsSuitEquipped())
        return;

    const char *suitClass = InventorySystem::GetEquippedSuitClass(pPlayer);
    if (!suitClass || !suitClass[0])
        suitClass = "item_suit";

    CHL2_Player *pHL2Player = dynamic_cast<CHL2_Player*>(pPlayer);
    if (pHL2Player)
    {
        pHL2Player->FlashlightTurnOff();
        pHL2Player->StopZooming();
        pHL2Player->StopSprinting();
    }

    pPlayer->RemoveSuit();

    {
        CSingleUserRecipientFilter filter(pPlayer);
        UserMessageBegin(filter, "CombineSuit");
        WRITE_BYTE(0);
        MessageEnd();
    }

    InventorySystem::ClearEquippedSuitClass(pPlayer);
    InventorySystem::AddItemToPlayer(pPlayer, suitClass);
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_unequip_suit_cc("inventory_unequip_suit", CC_Inventory_Unequip_Suit, "Unequip current suit into inventory (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_Grab(const CCommand &args)
{
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer)
        return;

    CBaseEntity *pUseEntity = pPlayer->GetUseEntity();
    if (pUseEntity && pUseEntity->ClassMatches("player_pickup"))
    {
        pPlayer->ClearUseEntity();
        return;
    }

    CBaseEntity *pTarget = pPlayer->FindUseEntity();
    if (pTarget)
    {
        if (pTarget->ClassMatches("item_*") || pTarget->ClassMatches("weapon_*") || pTarget->ClassMatches("prop_physics*"))
            pPlayer->PickupObject(pTarget);
        else if (pTarget->ClassMatches("prop_ragdoll*"))
        {
            extern ConVar physcannon_maxmass;
            if (CBasePlayer::CanPickupObject(pTarget, physcannon_maxmass.GetFloat(), 0))
                pPlayer->PickupObject(pTarget, false);
        }
        return;
    }

    Vector forward;
    pPlayer->EyeVectors(&forward);
    trace_t tr;
    const int contentsMask = MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_PLAYERCLIP;
    UTIL_TraceHull(pPlayer->EyePosition(), pPlayer->EyePosition() + forward * PLAYER_USE_RADIUS,
        -Vector(16, 16, 16), Vector(16, 16, 16), contentsMask, pPlayer, COLLISION_GROUP_NONE, &tr);

    pTarget = tr.m_pEnt;
    if (!pTarget)
        return;

    if ((tr.endpos - tr.startpos).Length() > PLAYER_USE_RADIUS)
        return;

    if (!pTarget->ClassMatches("prop_physics*") && !pTarget->ClassMatches("prop_ragdoll*"))
        return;

    if (pTarget->ClassMatches("prop_physics*"))
    {
        pPlayer->PickupObject(pTarget);
        return;
    }

    extern ConVar physcannon_maxmass;
    if (!CBasePlayer::CanPickupObject(pTarget, physcannon_maxmass.GetFloat(), 0))
        return;
    pPlayer->PickupObject(pTarget, false);
}
static ConCommand inventory_grab_cc("inventory_grab", CC_Inventory_Grab, "Toggle carrying item/weapon in hands (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Inventory_Split(const CCommand &args)
{
    if (args.ArgC() < 3) return;
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer) return;

    int slotIndex = atoi(args.Arg(1));
    int amount = atoi(args.Arg(2));
    if (amount <= 0) return;

    int idx = pPlayer->entindex();
    for (int i = 0; i < InventorySystem::g_PlayerInventories.Count(); ++i)
    {
        if (InventorySystem::g_PlayerInventories[i].entIndex != idx)
            continue;

        if (slotIndex < 0 || slotIndex >= InventorySystem::g_PlayerInventories[i].items.Count())
            return;

        InventorySystem::InvItem &src = InventorySystem::g_PlayerInventories[i].items[slotIndex];
        if (src.isWeapon)
            return;
        if (!InventorySystem::IsAmmoItem(src.classname.Get()))
            return;

        if (src.bullets <= 1)
            return;

        if (amount >= src.bullets)
            amount = src.bullets - 1;
        if (amount <= 0)
            return;

        src.bullets -= amount;
        InventorySystem::InvItem out = src;
        out.bullets = amount;
        InventorySystem::g_PlayerInventories[i].items.InsertAfter(slotIndex, out);

        InventorySystem::SyncPlayerAmmoWithInventory(pPlayer);
        InventorySystem::SendInventoryToPlayer(pPlayer);
        return;
    }
}
static ConCommand inventory_split_cc("inventory_split", CC_Inventory_Split, "Split ammo stack at slot: inventory_split <slot> <amount>", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

// Server testing commands
static void CC_Inventory_Add(const CCommand &args)
{
    if (args.ArgC() < 2) { DevMsg("Usage: inventory_add <itemname>\n"); return; }
    const char *item = args.Arg(1);
    // find first player
    CBasePlayer *pPlayer = UTIL_PlayerByIndex(1);
    if (!pPlayer) { DevMsg("No player at index 1\n"); return; }
    InventorySystem::AddItemToPlayer(pPlayer, item);
    DevMsg("Added item %s to player %s\n", item, pPlayer->GetPlayerName());
}
static ConCommand inventory_add_cc("inventory_add", CC_Inventory_Add, "Add item to player 1 (server)", FCVAR_GAMEDLL);

static void CC_Inventory_Force(const CCommand &args)
{
    // Force open inventory for player 1
    CBasePlayer *pPlayer = UTIL_PlayerByIndex(1);
    if (!pPlayer) { DevMsg("No player at index 1\n"); return; }
    InventorySystem::SendInventoryToPlayer(pPlayer);
}
static ConCommand inventory_force_cc("inventory_force", CC_Inventory_Force, "Force open inventory for player 1 (server)", FCVAR_GAMEDLL);

// Scan the current map for `weapon_*` and `item_*` entities and disable their touch
// functions so they won't be automatically picked up by players walking over them.
static void Inventory_ScanMapEntities()
{
    int maxEdicts = gEntList.NumberOfEdicts();
    for (int i = 1; i <= maxEdicts; ++i)
    {
        CBaseEntity *pEnt = UTIL_EntityByIndex(i);
        if (!pEnt) continue;
        const char *classname = pEnt->GetClassname();
        if (!classname) continue;
        if (Q_strnicmp(classname, "weapon_", 7) == 0 || Q_strnicmp(classname, "item_", 5) == 0)
        {
            pEnt->SetTouch(NULL);
        }
    }
}

// Console command to run the scan manually
static void CC_Inventory_ScanMap(const CCommand &args)
{
    Inventory_ScanMapEntities();
    DevMsg("Inventory: scanned map and disabled automatic pickup for weapon_/item_ entities.\n");
}
static ConCommand inventory_scan_map_cc("inventory_scan_map", CC_Inventory_ScanMap, "Scan map and disable automatic pickup for weapon_/item_ entities (server)", FCVAR_GAMEDLL);

// Auto-run after entities are created on level init
class CInventoryMapScanner : public CAutoGameSystem
{
public:
    CInventoryMapScanner() : CAutoGameSystem("CInventoryMapScanner") {}
    virtual void LevelInitPostEntity() OVERRIDE
    {
        Inventory_ScanMapEntities();
    }
};

static CInventoryMapScanner g_InventoryMapScanner;

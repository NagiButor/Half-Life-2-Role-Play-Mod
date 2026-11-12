//========= HL2RPM ============================================================//
//
// Item database of the RPG: scripts/rpg/items.txt (written by the Creation Kit).
// Shared by client (names, descriptions, models in the inventory) and server
// (weights, prices, what using an item does, the generic item_rpm entity).
//
// "Items" { "<id>" { "name" "..." "name_russian" "..." "desc" ... "category" "aid"
//   "weight" "0.5" "value" "25" "model" "models/..." "base" "" ... } }
//
// id = the entity classname for the game's own items (weapon_pistol, item_healthkit);
// items with a "base" (note, key, currency, generic) have no entity class of their
// own: the world spawns them as item_rpm with "item" = id, the inventory stores the id.
//
//=============================================================================//
#ifndef HL2RPM_ITEMS_H
#define HL2RPM_ITEMS_H
#pragma once

class KeyValues;

// the item the lockpicking minigame spends (hl2rpm_lockpick.cpp)
#define RPM_ITEM_BOBBYPIN "bobby_pin"

struct RPMItemDef_t
{
	char id[64];
	char category[24];		// weapon ammo apparel aid food note key currency misc quest
	char base[24];			// "" = a real entity class; note / key / currency / generic -> item_rpm
	char model[160];
	float weight;			// kg (per unit for stackable)
	int value;				// price in currency units
	int heal;				// aid / food: health restored on use
	bool stackable;			// several in one inventory slot (currency, ammo-like)
	bool quest;				// can't be dropped or sold
	// weapons
	float damage;			// per shot / hit (overrides the sk_* convar when > 0)
	float fireRate;			// shots per second (information)
	int clip;
	char skill[24];			// governing skill id (guns, energy, melee, explosives)
	int skillReq;
	KeyValues *pKV;			// the whole block (localized texts)
};

namespace RPMItems
{
	void Load( bool bForce = false );
	const RPMItemDef_t *Find( const char *id );
	int Count();
	const RPMItemDef_t *Get( int i );
	// "<key>_<language>" with fallback to "<key>"; "" if neither
	const char *Text( const RPMItemDef_t *pDef, const char *key );
	// the localized name, or a readable form of the id (weapon_pistol -> Pistol)
	const char *Name( const char *id );
	bool IsGeneric( const char *id );		// spawned as item_rpm, stored by id
	bool IsStackable( const char *id );
	// rpm_credits and bobby_pin work without an item database (weight 0, stackable)
	bool IsBuiltin( const char *id );
	// price: the database value, else the built-in price of a stock HL2 item or a bobby pin
	// (ammo: per bullet, like the database); 0 = not traded
	int Value( const char *id );
	// the model of the item: the database, else the stock item's; "" = none (the card goes without)
	const char *Model( const char *id );
	// the localized description ("" if none; built-in items have their own)
	const char *Desc( const char *id );
	const char *Language();
	int SuitDefense( const char *id );		// percent of a hit a suit stops ("defense" in the database)
	const char *SuitBody( const char *id );	// the player's model in it, "" - his own
	int SuitBodySkin( const char *id );		// and its skin
	void SuitBodies( CUtlVector<const char *> &models );
	// first-person hands of the weapons made for separate hands (the melee weapons, "uses_hands" in the
	// weapon script - Mapbase's hand_viewmodel): those of the suit worn ("" = no suit: a citizen's);
	// a suit of the database: "suit_hands" (+ "suit_hands_skin"), else by the theme of its HUD
	const char *SuitHands( const char *id, int theme, int &skin );
	// the painting of the stock view models' arms in that suit (v_hand_sheet_<name>): "citizen",
	// "hev"... - a suit of the database: "suit_vmhands"
	const char *SuitViewHands( const char *id, int theme );
	void HandsModels( CUtlVector<const char *> &models );
}

#endif // HL2RPM_ITEMS_H

//========= HL2RPM ============================================================//
//
// RPG state of the game (server): world flags and variables, the absolute game
// clock (days), loot lists, locks. Kept in the save game and across level changes.
//
//=============================================================================//
#ifndef HL2RPM_RPG_H
#define HL2RPM_RPG_H
#pragma once

class CBaseEntity;
class CBasePlayer;
class KeyValues;

namespace RPMState
{
	// world flags / variables (quests, dialog effects, events of the Creation Kit)
	bool GetFlag( const char *name );
	void SetFlag( const char *name, bool on );
	int GetVar( const char *name, int def = 0 );
	void SetVar( const char *name, int value );

	// game time since the start of the game, in hours (the time of day keeps going over days)
	double GameHours();

	// a named part of the state tree (created on demand): "vendors", ...
	KeyValues *Section( const char *name );

	// the player's level and skills (the RPG system; 1 / 0 until it exists)
	int PlayerLevel( CBasePlayer *pPlayer );
	int PlayerSkill( CBasePlayer *pPlayer, const char *skill );
}

namespace RPMLoot
{
	// "list:<id>" entries of a loot string rolled into items (scripts/rpg/lootlists.txt);
	// other entries are kept. Returns true if anything was rolled.
	bool Resolve( const char *pszIn, CUtlString &out, CBasePlayer *pPlayer );
}

namespace RPMTheft
{
	// the owner of a container: an NPC (targetname) or a faction; what the prompt shows
	const char *OwnerName( const char *pszOwner );
	// the player took from an owned container: a witness (the owner or one of its faction who sees
	// the player) says a "theft" bark and turns hostile; the faction loses reputation (rule rep_theft)
	void OnTake( CBasePlayer *pPlayer, CBaseEntity *pContainer, const char *pszOwner );
}

namespace RPMLock
{
	// A locked container / door used by the player: opens with the key item or with the
	// lockpick skill (the minigame of hl2rpm_lockpick.cpp: it starts here and opens the
	// lock later by itself). Prints why not. Returns true if it may open now.
	bool TryOpen( CBaseEntity *pEnt, CBasePlayer *pPlayer, const char *pszKey, int iLevel );

	// hl2rpm_lockpick.cpp
	void LockId( CBaseEntity *pEnt, char *out, int len );	// "<map>.<name / ref / hammer id>" for the state tree
	bool IsJammed( CBaseEntity *pEnt );						// a failed "force": only the key opens it
	bool TakePass( CBaseEntity *pEnt );						// the minigame just opened this lock (once)
	void GrantPicked( CBaseEntity *pEnt, CBasePlayer *pPlayer, int iLevel );	// experience once per lock
	const char *DifficultyName( int iLevel );
	int RequiredSkill( int iLevel );
	bool StartMinigame( CBaseEntity *pEnt, CBasePlayer *pPlayer, int iLevel );
}

namespace RPMTrade
{
	// the "vendor" block of an NPC of the Creation Kit (scripts/npcs), NULL if it doesn't trade:
	// list (loot string, may hold list:<id>), money, markup (price factor when the player buys),
	// pays (share of the value the vendor pays), restock_hours, currency (item id, rpm_credits)
	KeyValues *VendorDef( CBaseEntity *pNPC );
	// opens the loot window in trade mode with this vendor (a moment later: after the dialog window closes)
	bool Open( CBasePlayer *pPlayer, CBaseEntity *pNPC );
}

#endif // HL2RPM_RPG_H

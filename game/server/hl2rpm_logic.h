//========= HL2RPM ============================================================//
//
// Conditions, actions and events of the Creation Kit: one language for dialog
// options, quest stages and objectives, AI packages, objects of the map layer
// ("presence"), spawners and the events of the "Logic" tab (scripts/rpg/events.txt).
//
// A block of conditions is KeyValues with one child per condition:
//   "conditions" { "c" { "type" "flag" "name" "door_open" } "c" { "type" "skill" "name" "speech" "op" ">=" "value" "50" "or" "1" } ... }
// all of them must hold; "or" 1 joins a condition with the next one (either of the two), "not" 1 inverts it.
// A block of actions runs its children in order: "actions" { "a" { "type" "give" "item" "rpm_credits" "count" "20" } ... }
//
//=============================================================================//
#ifndef HL2RPM_LOGIC_H
#define HL2RPM_LOGIC_H
#pragma once

class CBaseEntity;
class CBasePlayer;
class KeyValues;
class Vector;

namespace RPMLogic
{
	// the target weather preset of env_weather ("" - none): clear, rain, thunderstorm...
	const char *CurrentWeatherName();
	struct Ctx_t
	{
		Ctx_t() : pPlayer( NULL ), pSelf( NULL ), pOther( NULL ), pszQuest( NULL ) {}
		CBasePlayer	*pPlayer;		// NULL - the local player
		CBaseEntity	*pSelf;			// "this": the NPC of the dialog, the object of an event
		CBaseEntity	*pOther;		// the victim of a kill, the used object...
		const char	*pszQuest;		// "this quest" (actions of a quest stage)
	};

	bool Check( KeyValues *pConds, const Ctx_t &ctx );		// NULL / empty = true
	bool CheckOne( KeyValues *pCond, const Ctx_t &ctx );

	// Actions in order. While the player's dialog window is open the actions that need it closed
	// (the NPC attacks, teleport, trade, another dialog) wait until it closes.
	void Run( KeyValues *pActions, const Ctx_t &ctx );
	void NoteDialogOpen( CBasePlayer *pPlayer );
	void DialogClosed( CBasePlayer *pPlayer );
	bool IsDialogOpen();

	// skill / attribute check of a dialog option { "skill" "speech" "value" "50" } or { "stat" "cha" "value" "6" }:
	// whether the player passes, and the label "[Красноречие 50]" / "[Красноречие 25/50]"
	bool CheckPasses( KeyValues *pCheck, CBasePlayer *pPlayer, char *pszLabel, int nLabel );
	const char *SkillName( const char *pszId );		// skill or attribute id -> name in the dialog language

	// copies of blocks that live until their pool is freed (reload of dialogs / quests)
	enum { POOL_DIALOGS = 0, POOL_QUESTS, POOL_WORLD, POOL_COUNT };
	KeyValues *Keep( KeyValues *pBlock, int iPool );		// NULL / empty block -> NULL
	void FreePool( int iPool );

	// what happens in the game: events of the "Logic" tab and quest objectives
	void NoteKill( CBaseEntity *pVictim, CBaseEntity *pAttacker );
	void NoteUse( CBasePlayer *pPlayer, CBaseEntity *pUsed );
	void NoteTalk( CBasePlayer *pPlayer, CBaseEntity *pNPC );
	void NoteQuest( CBasePlayer *pPlayer, const char *pszQuest, const char *pszWhat, int iStage );	// quest_start / quest_stage / quest_complete / quest_fail
	void ReloadEvents();

	// helpers shared with quests and the world layer
	void Toast( CBasePlayer *pPlayer, const char *pszCategory, const char *pszText, const char *pszSound );
	void AddXP( CBasePlayer *pPlayer, int xp );
	bool IsRussian();
	const char *Localized( KeyValues *pKV, const char *pszKey );		// key_<dialog language>, else key
	// a target by name: targetname, name of a layer object, zone, "!player", "!self"
	CBaseEntity *FindTarget( const char *pszName, const Ctx_t &ctx );
	bool TargetPos( const char *pszName, const Ctx_t &ctx, Vector &pos );
	// "name;name2;zomb*" against the targetname or the classname
	bool NameMatches( const char *pszPatterns, CBaseEntity *pEnt );
}

#endif // HL2RPM_LOGIC_H

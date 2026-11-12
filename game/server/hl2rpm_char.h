//========= HL2RPM ============================================================//
//
// The player character of the RPG (server): attributes, skills, perks, traits, level and
// experience (rules: scripts/rpg/rpg.txt, see hl2rpm_rpgdefs.h), reputation with factions,
// the new game (scripts/rpg/newgame.txt). State lives in RPMState "player" (save game,
// level changes); the client gets it as the "RPMChar" user message.
//
//   "player" { "name" .. "created" "1" "level" "3" "xp" "450" "points" "0" "perks_pending" "1"
//              "stats" { "str" "6" .. } "skills" { "guns" "10" .. }   (points added to the formula)
//              "tags" "guns;speech;medicine" "traits" "a;b" "perks" { "<id>" "<rank>" } }
//   "rep" { "<faction>" "<value>" }
//
//=============================================================================//
#ifndef HL2RPM_CHAR_H
#define HL2RPM_CHAR_H
#pragma once

class CBaseEntity;
class CBasePlayer;
class CTakeDamageInfo;
class KeyValues;

namespace RPMChar
{
	int BaseStat( const char *id );		// as chosen (1..10)
	int Stat( const char *id );			// with perks and traits
	int SkillBase( const char *id );	// from the attributes and the tag
	int Skill( const char *id );		// all of it (0..skill_max)
	void SetSkillTotal( const char *id, int total );	// rpm_skill, test state: make it exactly this
	int Level();
	int XP();
	bool IsCreated();
	bool HasPerk( const char *id );
	// sum of the effects of perks (times rank) and traits; for "stat" / "skill" the id must
	// match, for the rest an effect without an id counts for every id
	float Effect( const char *type, const char *id = "" );

	int MaxHealth();
	float CarryWeight();
	float HealScale();
	float PriceScale( bool bPlayerBuys );

	void AddXP( CBasePlayer *pPlayer, int xp );
	void ModifyDamage( CTakeDamageInfo &info, CBaseEntity *pVictim );
	// bullet spread of the player's weapon: skill 0 .. 100 = rules spread_min .. spread_max,
	// below the weapon's requirement wider by skill_req_penalty
	float SpreadScale( CBasePlayer *pPlayer );
	void NoteKill( CBaseEntity *pVictim, CBaseEntity *pAttacker );

	int FactionRep( const char *faction );
	void AddFactionRep( CBasePlayer *pPlayer, const char *faction, int delta );

	// the character creation window (new game, action "char_create", console rpm_char_create)
	void OpenCreation( CBasePlayer *pPlayer );
}

#endif // HL2RPM_CHAR_H

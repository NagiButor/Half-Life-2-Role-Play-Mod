//========= HL2RPM ============================================================//
//
// Character rules of the RPG (like Fallout: New Vegas): 7 attributes, 12 skills, perks,
// traits, levels. scripts/rpg/rpg.txt (written by the Creation Kit, "RPG" tab); built-in
// attributes / skills / rules when the file is missing. Shared by the server (the rules)
// and the client (names and descriptions of the character windows).
//
// "RPG" { "rules" { "stat_total" "40" ... }
//         "stats"  { "str" { "name" .. "name_russian" .. "desc" .. } ... }
//         "skills" { "guns" { "stat" "agi" "name" .. "desc" .. } ... }
//         "perks"  { "<id>" { "name" .. "desc" .. "level" "2" "ranks" "1"
//                             "req" { "str" "6" "guns" "45" } "effects" { "e" { "type" "skill" "id" "guns" "value" "5" } } } }
//         "traits" { "<id>" { "name" .. "desc" .. "effects" { .. } } } }
//
// Effect types (per rank of a perk): stat (id), skill (id), skills (all skills), hp, carry (kg),
// damage (id = weapon skill or empty, %), crit (%), heal (%), xp (%), price (%), points (skill
// points per level), dr (damage resistance %).
//
//=============================================================================//
#ifndef HL2RPM_RPGDEFS_H
#define HL2RPM_RPGDEFS_H
#pragma once

class KeyValues;

#define RPM_STAT_COUNT 7
#define RPM_SKILL_COUNT 12

namespace RPMRPG
{
	void Load( bool bForce = false );
	// the "rules" values (built-in defaults when missing)
	float Rule( const char *key );

	int StatCount();
	const char *StatId( int i );
	int SkillCount();
	const char *SkillId( int i );
	int StatIndex( const char *id );		// -1 if not an attribute
	int SkillIndex( const char *id );		// -1 if not a skill
	const char *SkillStat( const char *skill );	// governing attribute of a skill

	// the localized name / description of an attribute, skill, perk or trait id
	const char *Name( const char *id );
	const char *Desc( const char *id );

	KeyValues *Perks();		// "<id>" blocks (never NULL)
	KeyValues *Traits();
	KeyValues *Perk( const char *id );
	KeyValues *Trait( const char *id );

	// experience needed to reach the level (FNV: 25 (3L + 2)(L - 1), times xp_scale)
	int XPForLevel( int level );
	// "<key>_<language>" with fallback to "<key>"
	const char *LocString( KeyValues *pKV, const char *key );
}

#endif // HL2RPM_RPGDEFS_H

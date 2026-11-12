//=============================================================================
// HL2RPM: the world content made with the HL2RPM Creation Kit.
//
// scripts/npcs/<targetname>.txt    "NPC"      - an actor: class, model, weapon, faction,
//                                                relation to the player, AI packages
// scripts/world/<map>.txt          "World"    - what the kit placed on a map: NPCs,
//                                                markers (beds, chairs...), items,
//                                                containers, props, patrol paths
// scripts/world/<map>_nav.txt      "Nav"      - AI nodes for maps without info_node
// scripts/world/factions.txt       "Factions" - faction relations (ftag of the NPCs)
//
// The layer is spawned once, on a new map (never on a loaded game or a revisited
// level: those entities come from the save). Definitions (NPCs, paths, factions,
// marker data) are read on every level init, so a save always runs the current ones.
// Console: rpm_world_reload, rpm_world_dump, rpm_ai_debug, rpm_nav_generate.
//=============================================================================
#ifndef HL2RPM_WORLD_H
#define HL2RPM_WORLD_H

#include "utlvector.h"
#include "utlstring.h"

class CBaseEntity;
class CBasePlayer;
class CAI_BaseNPC;
class CAI_Hint;
class CBaseAnimating;

// AI package types (GECK-like)
enum RPMPackageType_t
{
	RPMPKG_NONE = 0,
	RPMPKG_WAIT,		// stand at the location (current place by default)
	RPMPKG_TRAVEL,		// go to the location and stay there
	RPMPKG_GUARD,		// stand at the location facing yaw, return when pushed away
	RPMPKG_PATROL,		// walk a path of points
	RPMPKG_WANDER,		// walk to random places within the radius
	RPMPKG_SANDBOX,		// free time: markers (sit, lean, eat, work) and walks within the radius
	RPMPKG_SLEEP,		// lie on a bed marker (own bed first), on the ground without one
	RPMPKG_EAT,			// sit at an "eat" marker
	RPMPKG_USEMARKER,	// use one marker (by name) or any of the given kinds
	RPMPKG_FOLLOW,		// follow the player or a named entity
	RPMPKG_TALK,		// come to the player and start the dialog
	RPMPKG_COUNT
};

enum RPMLocation_t
{
	RPMLOC_SELF = 0,	// where the NPC was spawned / placed (editor location)
	RPMLOC_REF,			// an entity by targetname (marker, path, info_target...)
	RPMLOC_POINT,		// a point on the map
	RPMLOC_PLAYER,		// near the player
	RPMLOC_CURRENT,		// wherever the NPC is when the package starts
};

enum RPMPathMode_t
{
	RPMPATH_LOOP = 0,
	RPMPATH_PINGPONG,
	RPMPATH_ONCE,
};

struct RPMPackage_t
{
	CUtlString	name;
	int			type;
	float		start;			// hours 0..24
	float		duration;		// hours; >= 24 - always
	int			locKind;
	CUtlString	ref;
	Vector		point;
	float		yaw;
	bool		hasYaw;
	float		radius;
	bool		run;
	CUtlString	anim;			// actbusy name for the stand-style packages
	CUtlString	markerKinds;	// "sit;lean" - sandbox / use marker
	CUtlString	path;
	int			pathMode;
	CUtlString	target;			// follow
	bool		once;			// talk: once per game
	float		minUse, maxUse;	// sandbox: seconds per marker
	// conditions (same syntax as the dialogs)
	CUtlString	reqQuestStateId;
	int			reqQuestState;
	CUtlString	reqQuestStageId;
	int			reqQuestStage;
	KeyValues	*pConds;		// "conditions" of the Creation Kit (RPMLogic, owned by POOL_WORLD)
};

struct RPMNPCDef_t
{
	CUtlString	targetname;
	CUtlString	classname;
	CUtlString	model;
	CUtlString	weapon;
	CUtlString	squad;
	CUtlString	faction;
	int			relation;		// -1 default, else D_LI / D_NU / D_HT
	int			skin;
	int			health;
	bool		essential;
	bool		commandable;	// citizens: may join the player's squad (HL2 autosquad / USE)
	// variation (templates of the Creation Kit): a random pick per spawned NPC
	CUtlStringList models;		// "models" "a.mdl;b.mdl"
	CUtlStringList weapons;		// "weapons" "weapon_pistol;weapon_smg1;" (empty entry = no weapon)
	KeyValues	*pNameList;		// "names" { "0" { "english" ".." "russian" ".." } ... }
	int			skinMax;		// random skin skin..skinMax (0 = fixed)
	int			proficiency;	// -1 default, else WEAPON_PROFICIENCY_POOR..PERFECT (0..4)
	int			aggression;		// -1 by class / faction; 0 never attacks first, 1 attacks enemies, 2 all but friends, 3 frenzied
	int			confidence;		// -1 by class; 0 cowardly (flees at once), 1 cautious (< 50 % health), 2 average (< 25 %), 3 brave
	int			xp;				// experience for killing it (the RPG system)
	// behaviour (-1 = the rule of scripts/rpg/rpg.txt / by class): moves out of the player's way (0 / 1),
	// picks weapons up from the floor (0 never / 1 HL2 / 2 only in the player's squad), weapon ban
	// (0 not concerned / 1 civilian: may not carry weapons / 2 enforcer: warns, then attacks / 3 enforcer: attacks)
	int			yield;
	int			pickupWeapons;
	int			weaponLaw;
	KeyValues	*pKeys;			// extra keyvalues for the spawn (loot_items, citizentype...)
	KeyValues	*pNames;		// display_name, display_name_<language> (loot window of NPCs without a dialog)
	KeyValues	*pVendor;		// "vendor" { list money markup pays restock_hours currency } or NULL (RPMTrade)
	// sound (hl2rpm_audio.cpp): HL2 lines -1 default (kit NPCs: combat only) / 0 all / 1 combat / 2 none,
	// barks of the NPC (RPMLogic::POOL_WORLD) and the shared sets of scripts/rpg/barks.txt it uses
	int			stockSpeech;
	KeyValues	*pBarks;
	CUtlString	barkSets;
	CUtlVector<RPMPackage_t> packages;
};

struct RPMMarker_t
{
	CUtlString	name;
	CUtlString	kind;			// sleep sit eat lean work guard idle
	CUtlString	anim;
	CUtlString	owner;			// targetname of the NPC it belongs to, empty - anyone
	float		seatH;			// height of the seat / bed under it (pose parameter rpm_seat_h), -1 - not set
};

struct RPMPathPoint_t
{
	Vector		pos;
	float		wait;
	float		yaw;
	bool		hasYaw;
	CUtlString	anim;
};

struct RPMPath_t
{
	CUtlString	name;
	CUtlVector<RPMPathPoint_t> points;
};

namespace RPMWorld
{
	const RPMNPCDef_t	*FindNPCDef( const char *pszTargetname );
	const RPMMarker_t	*FindMarker( const char *pszName );
	const RPMPath_t		*FindPath( const char *pszName );
	// "<key>_<language>" (the game's dialog language) or "<key>" of an NPC definition / a layer reference
	const char	*GetNPCDisplayName( const char *pszTargetname );
	// the name of this NPC (a random name of a template if one was picked at spawn)
	const char	*GetNPCDisplayNameFor( CBaseEntity *pEnt );
	// spawn an NPC of a definition (spawn points, events); NULL if there is no such definition
	CBaseEntity	*SpawnNPC( const char *pszDef, const Vector &origin, const QAngle &angles, const char *pszTag );
	const char	*GetRefString( CBaseEntity *pLayerEntity, const char *pszKey );
	// the localized name of a faction of scripts/world/factions.txt (its id if none)
	const char	*FactionName( const char *pszFaction );
	// bumped on every reload of the definitions
	int		Generation();

	// transitions between maps (layer refs "transition"): spawned during a level change before the
	// engine looks for the landmark; the player arriving through one faces its arrival direction
	void	SpawnTransitionsEarly();
	void	NoteArrival( const char *pszLandmark );

	// world clock (env_timecycle or the world clock of maps without it)
	float	GetHours();
	bool	IsPackageTime( const RPMPackage_t &pkg, float flHours );
	bool	PackageConditionsMet( const RPMPackage_t &pkg, CBaseEntity *pNPC = NULL );

	// zones of the layer (refs "zone": a named box, rotated by its yaw) - events, quest objectives
	int		ZoneCount();
	const char *ZoneName( int i );
	int		FindZone( const char *pszName );		// -1 if none
	Vector	ZoneCenter( int i );
	Vector	ZoneSize( int i );
	bool	IsInZone( int i, const Vector &pos );
	KeyValues *ZoneKeys( int i );		// the zone's block of the layer file (location: display_name, music, soundscape...)

	// a free marker of one of the kinds ("sleep", "sit;lean") near the point;
	// owned markers are only for their owner, the NPC's own come first
	CAI_Hint *FindFreeMarker( CAI_BaseNPC *pNPC, const char *pszKinds, const Vector &center, float flRadius, bool bRandom );
	bool	MarkerMatchesKinds( const RPMMarker_t *pMarker, const char *pszKinds );

	// behaviour of NPCs (definition, else the rules of rpg.txt): moves out of the player's way, picks up
	// weapons, the weapon ban (RPMWeaponLaw: 0 not concerned, 1 civilian, 2 enforcer warns, 3 enforcer attacks)
	// a marker's pose for the animation library: the seat height (pose parameter rpm_seat_h)
	void	ApplyMarkerPose( CBaseAnimating *pAnim, const char *pszMarker );
	bool	NPCYieldsToPlayer( CAI_BaseNPC *pNPC );
	bool	NPCMayPickupWeapons( CAI_BaseNPC *pNPC );
	int		NPCWeaponLaw( CAI_BaseNPC *pNPC );

	// dialog activity: NPCs stand still while the player talks to them
	void	NoteDialogActivity( CBaseEntity *pNPC );
	bool	IsInDialog( CBaseEntity *pNPC );

	bool	IsDebugEnabled();
	int		DebugLevel();
	const char *PackageTypeName( int type );
}

#endif // HL2RPM_WORLD_H

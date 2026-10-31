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
	KeyValues	*pKeys;			// extra keyvalues for the spawn (loot_items, citizentype...)
	KeyValues	*pNames;		// display_name, display_name_<language> (loot window of NPCs without a dialog)
	CUtlVector<RPMPackage_t> packages;
};

struct RPMMarker_t
{
	CUtlString	name;
	CUtlString	kind;			// sleep sit eat lean work guard idle
	CUtlString	anim;
	CUtlString	owner;			// targetname of the NPC it belongs to, empty - anyone
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
	const char	*GetRefString( CBaseEntity *pLayerEntity, const char *pszKey );
	// bumped on every reload of the definitions
	int		Generation();

	// world clock (env_timecycle or the world clock of maps without it)
	float	GetHours();
	bool	IsPackageTime( const RPMPackage_t &pkg, float flHours );
	bool	PackageConditionsMet( const RPMPackage_t &pkg );

	// a free marker of one of the kinds ("sleep", "sit;lean") near the point;
	// owned markers are only for their owner, the NPC's own come first
	CAI_Hint *FindFreeMarker( CAI_BaseNPC *pNPC, const char *pszKinds, const Vector &center, float flRadius, bool bRandom );
	bool	MarkerMatchesKinds( const RPMMarker_t *pMarker, const char *pszKinds );

	// dialog activity: NPCs stand still while the player talks to them
	void	NoteDialogActivity( CBaseEntity *pNPC );
	bool	IsInDialog( CBaseEntity *pNPC );

	bool	IsDebugEnabled();
	int		DebugLevel();
	const char *PackageTypeName( int type );
}

#endif // HL2RPM_WORLD_H

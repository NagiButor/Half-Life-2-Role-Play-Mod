//=============================================================================
// HL2RPM: world content of the HL2RPM Creation Kit (see hl2rpm_world.h).
//=============================================================================
#include "cbase.h"
#include "hl2rpm_world.h"
#include "hl2rpm_items.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "utlbuffer.h"
#include "utldict.h"
#include "utlmap.h"
#include "checksum_crc.h"
#include "ai_basenpc.h"
#include "ai_hint.h"
#include "ai_network.h"
#include "ai_networkmanager.h"
#include "ai_node.h"
#include "ai_hull.h"
#include "quest_system.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_rpgdefs.h"

extern ConVar rpm_logic_debug;
#include "dialog_definitions.h"
#include "player.h"
#include "timecycle/env_timecycle.h"
#include "soundscape.h"
#include "soundscape_system.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpm_audio_debug;
ConVar rpm_world( "rpm_world", "1", FCVAR_GAMEDLL, "Spawn the HL2RPM Creation Kit layer (scripts/world/<map>.txt) on a new map" );
ConVar rpm_ai_debug( "rpm_ai_debug", "0", FCVAR_GAMEDLL, "HL2RPM AI packages: 1 - package and state above every NPC with a routine, 2 - also the goals" );
// set on the command line by the Creation Kit (commands after +map run before the map exists)
ConVar rpm_nav_autogen( "rpm_nav_autogen", "", FCVAR_GAMEDLL, "'<spacing> <max> <file>': generate nav nodes 3 s after the map is loaded, then quit (Creation Kit)" );
ConVar rpm_start( "rpm_start", "", FCVAR_GAMEDLL, "'x y z pitch yaw [hour]': put the player there once the map is loaded (Creation Kit: play from the camera)" );

extern bool g_bHL2RPMRestoringLevel;

static const char *s_pszPackageTypes[RPMPKG_COUNT] =
{
	"none", "wait", "travel", "guard", "patrol", "wander", "sandbox", "sleep", "eat", "use_marker", "follow", "talk",
};

static const char *s_pszLocationKinds[] = { "self", "ref", "point", "player", "current" };

const char *RPMWorld::PackageTypeName( int type )
{
	if ( type < 0 || type >= RPMPKG_COUNT )
		return "?";
	return s_pszPackageTypes[type];
}

static int ParseNamed( const char *psz, const char * const *ppNames, int nNames, int def )
{
	if ( !psz || !psz[0] )
		return def;
	for ( int i = 0; i < nNames; i++ )
	{
		if ( !Q_stricmp( psz, ppNames[i] ) )
			return i;
	}
	return def;
}

static int ParseDisposition( const char *psz )
{
	if ( !psz || !psz[0] )
		return -1;
	if ( !Q_stricmp( psz, "friend" ) || !Q_stricmp( psz, "like" ) || !Q_stricmp( psz, "ally" ) )
		return D_LI;
	if ( !Q_stricmp( psz, "neutral" ) )
		return D_NU;
	if ( !Q_stricmp( psz, "enemy" ) || !Q_stricmp( psz, "hate" ) || !Q_stricmp( psz, "hostile" ) )
		return D_HT;
	if ( !Q_stricmp( psz, "fear" ) )
		return D_FR;
	return -1;
}

static int ParseQuestState( const char *s )
{
	if ( !Q_stricmp( s, "active" ) )
		return kQuestState_Active;
	if ( !Q_stricmp( s, "completed" ) || !Q_stricmp( s, "done" ) )
		return kQuestState_Completed;
	if ( !Q_stricmp( s, "failed" ) )
		return kQuestState_Failed;
	if ( !Q_stricmp( s, "inactive" ) )
		return kQuestState_Inactive;
	return INT_MIN;
}

// "id:value" -> id, value
static void SplitIdValue( const char *psz, CUtlString &id, CUtlString &value )
{
	id = "";
	value = "";
	if ( !psz || !psz[0] )
		return;
	const char *colon = strchr( psz, ':' );
	if ( !colon )
	{
		id = psz;
		return;
	}
	id.SetDirect( psz, colon - psz );
	value = colon + 1;
}

static bool ParseVector( const char *psz, Vector &v )
{
	v.Init();
	if ( !psz || !psz[0] )
		return false;
	return sscanf( psz, "%f %f %f", &v.x, &v.y, &v.z ) == 3;
}

//-----------------------------------------------------------------------------
// Definitions
//-----------------------------------------------------------------------------
struct RPMFaction_t
{
	CUtlString	id;
	KeyValues	*pNames;		// name, name_<language>
	RPMFaction_t() : pNames( NULL ), playerDisp( -1 ) {}
	~RPMFaction_t() { if ( pNames ) pNames->deleteThis(); }
	int			playerDisp;		// -1 - class default
	CUtlVector<CUtlString> otherIds;
	CUtlVector<int> otherDisps;
};

struct RPMZone_t
{
	KeyValues	*pRef;			// its block in m_pLayer
	CUtlString	name;
	Vector		origin;			// the middle of the bottom
	Vector		size;
	float		yaw;
};

struct RPMNavFile_t
{
	CUtlVector<Vector> nodes;
	CRC32_t crc;
	bool bLoaded;
};

class CRPMWorldSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMWorldSystem() : CAutoGameSystemPerFrame( "CRPMWorldSystem" ) { m_iGeneration = 0; m_bSpawnLayer = false; m_flNextTick = 0; m_nRelationsAI = -1; m_nPlayerRelAI = -1; m_Nav.bLoaded = false; m_pLayer = NULL; }

	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPostEntity();
	virtual void FrameUpdatePostEntityThink();

	void LoadDefinitions();
	void ClearDefinitions();
	void SpawnLayer();
	CBaseEntity *SpawnRef( KeyValues *pRef, int &kind );
	void UpdatePresence();
	void SpawnTransitions();
	int RemoveLayerEntities();
	void ApplyRelations( bool bForce );
	void DrawDebug();

	const RPMNPCDef_t *FindNPCDef( const char *pszName ) const
	{
		int i = m_NPCDefs.Find( pszName );
		return ( i == m_NPCDefs.InvalidIndex() ) ? NULL : m_NPCDefs[i];
	}
	const RPMMarker_t *FindMarker( const char *pszName ) const
	{
		int i = m_Markers.Find( pszName );
		return ( i == m_Markers.InvalidIndex() ) ? NULL : m_Markers[i];
	}
	const RPMPath_t *FindPath( const char *pszName ) const
	{
		int i = m_Paths.Find( pszName );
		return ( i == m_Paths.InvalidIndex() ) ? NULL : m_Paths[i];
	}
	const RPMFaction_t *FindFaction( const char *pszId ) const
	{
		if ( !pszId || !pszId[0] )
			return NULL;
		for ( int i = 0; i < m_Factions.Count(); i++ )
		{
			if ( !Q_stricmp( m_Factions[i]->id, pszId ) )
				return m_Factions[i];
		}
		return NULL;
	}

	int m_iGeneration;
	bool m_bSpawnLayer;
	float m_flNextTick;
	float m_flNextCourage = 0.0f;
	void UpdateZoneSoundscapes();
	void UpdateConfidence();
	int m_nRelationsAI;
	int m_nPlayerRelAI;		// NPC count when the player relations were last applied (-1: not yet, or no player)

	CUtlDict<RPMNPCDef_t *, int> m_NPCDefs;
	CUtlDict<RPMMarker_t *, int> m_Markers;
	CUtlDict<RPMPath_t *, int> m_Paths;
	CUtlVector<RPMFaction_t *> m_Factions;
	CUtlVector<RPMZone_t> m_Zones;
	KeyValues *m_pLayer;		// scripts/world/<map>.txt (refs to spawn)
	float m_flNextPresence;
	RPMNavFile_t m_Nav;

	struct DialogNote_t { EHANDLE hEnt; float flTime; };
	CUtlVector<DialogNote_t> m_DialogNotes;
};

static CRPMWorldSystem g_RPMWorld;
static bool s_bTransitionsSpawned = false;	// the transitions of this level's layer exist (SpawnTransitions)

int RPMWorld::Generation() { return g_RPMWorld.m_iGeneration; }
const RPMNPCDef_t *RPMWorld::FindNPCDef( const char *pszTargetname ) { return ( pszTargetname && pszTargetname[0] ) ? g_RPMWorld.FindNPCDef( pszTargetname ) : NULL; }
const RPMMarker_t *RPMWorld::FindMarker( const char *pszName ) { return ( pszName && pszName[0] ) ? g_RPMWorld.FindMarker( pszName ) : NULL; }
const RPMPath_t *RPMWorld::FindPath( const char *pszName ) { return ( pszName && pszName[0] ) ? g_RPMWorld.FindPath( pszName ) : NULL; }
bool RPMWorld::IsDebugEnabled() { return rpm_ai_debug.GetBool(); }

// key_<language> wins over key (empty translations fall back)
static const char *LocalizedString( KeyValues *pKV, const char *pszKey )
{
	if ( !pKV )
		return "";
	char szKey[128];
	Q_snprintf( szKey, sizeof( szKey ), "%s_%s", pszKey, DialogDefinitions::GetDialogLanguage() );
	const char *psz = pKV->GetString( szKey, "" );
	if ( psz[0] )
		return psz;
	return pKV->GetString( pszKey, "" );
}

const char *RPMWorld::FactionName( const char *pszFaction )
{
	for ( int i = 0; pszFaction && i < g_RPMWorld.m_Factions.Count(); i++ )
	{
		RPMFaction_t *f = g_RPMWorld.m_Factions[i];
		if ( !Q_stricmp( f->id, pszFaction ) )
		{
			const char *n = LocalizedString( f->pNames, "name" );
			return n[0] ? n : f->id.Get();
		}
	}
	return pszFaction ? pszFaction : "";
}

//-----------------------------------------------------------------------------
// Behaviour of NPCs: the NPC's definition (Creation Kit), else the rules of
// scripts/rpg/rpg.txt; Hammer NPCs follow the rules
//-----------------------------------------------------------------------------
void RPMWorld::ApplyMarkerPose( CBaseAnimating *pAnim, const char *pszMarker )
{
	const RPMMarker_t *pM = FindMarker( pszMarker );
	if ( !pAnim || !pM )
		return;
	// a marker of a library animation without a measured seat: an ordinary chair
	float h = pM->seatH;
	if ( h < 0.0f )
	{
		if ( Q_strnicmp( pM->anim.Get(), "rpm_", 4 ) )
			return;
		h = 17.0f;
	}
	CStudioHdr *pHdr = pAnim->GetModelPtr();
	int i = pHdr ? pAnim->LookupPoseParameter( pHdr, "rpm_seat_h" ) : -1;
	if ( i >= 0 )
		pAnim->SetPoseParameter( pHdr, i, h );
	if ( rpm_ai_debug.GetBool() )
		Msg( "RPMWorld: %s on marker %s: rpm_seat_h %.1f (%s)\n", pAnim->GetDebugName(), pszMarker, h, i >= 0 ? "set" : "the model has no such pose parameter" );
}

bool RPMWorld::NPCYieldsToPlayer( CAI_BaseNPC *pNPC )
{
	if ( !pNPC )
		return false;
	const RPMNPCDef_t *pDef = FindNPCDef( pNPC->GetEntityName().ToCStr() );
	if ( pDef && pDef->yield >= 0 )
		return pDef->yield != 0;
	return RPMRPG::Rule( "npc_yield" ) != 0.0f;
}

bool RPMWorld::NPCMayPickupWeapons( CAI_BaseNPC *pNPC )
{
	if ( !pNPC )
		return false;
	const RPMNPCDef_t *pDef = FindNPCDef( pNPC->GetEntityName().ToCStr() );
	int mode = ( pDef && pDef->pickupWeapons >= 0 ) ? pDef->pickupWeapons : (int)RPMRPG::Rule( "npc_pickup_weapons" );
	if ( mode == 2 )
		return pNPC->IsInPlayerSquad();
	return mode != 0;
}

int RPMWorld::NPCWeaponLaw( CAI_BaseNPC *pNPC )
{
	if ( !pNPC )
		return 0;
	const RPMNPCDef_t *pDef = FindNPCDef( pNPC->GetEntityName().ToCStr() );
	if ( pDef && pDef->weaponLaw >= 0 )
		return pDef->weaponLaw;
	// by class: Civil Protection and soldiers enforce the ban (rule weapon_law: 0 off, 1 warn first,
	// 2 open fire at once), citizens may not carry weapons
	if ( FClassnameIs( pNPC, "npc_metropolice" ) || FClassnameIs( pNPC, "npc_combine_s" ) )
	{
		int rule = (int)RPMRPG::Rule( "weapon_law" );
		return rule <= 0 ? 0 : ( rule == 1 ? 2 : 3 );
	}
	if ( FClassnameIs( pNPC, "npc_citizen" ) )
		return 1;
	return 0;
}

const char *RPMWorld::GetNPCDisplayName( const char *pszTargetname )
{
	const RPMNPCDef_t *pDef = FindNPCDef( pszTargetname );
	return pDef ? LocalizedString( pDef->pNames, "display_name" ) : "";
}

static const char *LocalizedLang( KeyValues *pKV )
{
	// a name of the template list: { "english" ".." "russian" ".." }
	if ( !pKV )
		return "";
	const char *psz = pKV->GetString( DialogDefinitions::GetDialogLanguage(), "" );
	if ( psz[0] )
		return psz;
	KeyValues *pFirst = pKV->GetFirstValue();
	return pFirst ? pFirst->GetString() : "";
}

const char *RPMWorld::GetNPCDisplayNameFor( CBaseEntity *pEnt )
{
	if ( !pEnt )
		return "";
	const RPMNPCDef_t *pDef = FindNPCDef( pEnt->GetEntityName().ToCStr() );
	if ( !pDef )
		return "";
	const char *pszIdx = pEnt->GetContextValue( "rpmname" );
	if ( pszIdx && pszIdx[0] && pDef->pNameList )
	{
		int want = atoi( pszIdx ), i = 0;
		for ( KeyValues *k = pDef->pNameList->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey(), i++ )
			if ( i == want )
				return LocalizedLang( k );
	}
	return LocalizedString( pDef->pNames, "display_name" );
}

const char *RPMWorld::GetRefString( CBaseEntity *pEnt, const char *pszKey )
{
	if ( !pEnt || !g_RPMWorld.m_pLayer )
		return "";
	const char *pszRef = pEnt->GetContextValue( "rpmref" );
	if ( !pszRef || !pszRef[0] )
		return "";
	KeyValues *pRefs = g_RPMWorld.m_pLayer->FindKey( "refs" );
	return LocalizedString( pRefs ? pRefs->FindKey( pszRef ) : NULL, pszKey );
}
int RPMWorld::DebugLevel() { return rpm_ai_debug.GetInt(); }

static void ParsePackage( KeyValues *pKV, RPMPackage_t &p )
{
	p.name = pKV->GetString( "name", pKV->GetName() );
	p.type = ParseNamed( pKV->GetString( "type" ), s_pszPackageTypes, RPMPKG_COUNT, RPMPKG_NONE );
	p.start = pKV->GetFloat( "start", 0.0f );
	p.duration = pKV->GetFloat( "duration", 24.0f );
	p.locKind = ParseNamed( pKV->GetString( "location" ), s_pszLocationKinds, ARRAYSIZE( s_pszLocationKinds ), RPMLOC_SELF );
	p.ref = pKV->GetString( "ref" );
	ParseVector( pKV->GetString( "point" ), p.point );
	const char *pszYaw = pKV->GetString( "yaw" );
	p.hasYaw = pszYaw[0] != 0;
	p.yaw = p.hasYaw ? atof( pszYaw ) : 0.0f;
	p.radius = pKV->GetFloat( "radius", 0.0f );
	p.run = pKV->GetBool( "run" );
	p.anim = pKV->GetString( "anim" );
	p.markerKinds = pKV->GetString( "markers" );
	p.path = pKV->GetString( "path" );
	const char *pszMode = pKV->GetString( "path_mode" );
	p.pathMode = !Q_stricmp( pszMode, "pingpong" ) ? RPMPATH_PINGPONG : ( !Q_stricmp( pszMode, "once" ) ? RPMPATH_ONCE : RPMPATH_LOOP );
	p.target = pKV->GetString( "target" );
	p.once = pKV->GetBool( "once", p.type == RPMPKG_TALK );
	p.minUse = pKV->GetFloat( "min_use", 30.0f );
	p.maxUse = pKV->GetFloat( "max_use", 90.0f );

	CUtlString value;
	SplitIdValue( pKV->GetString( "require_quest_state" ), p.reqQuestStateId, value );
	p.reqQuestState = value.IsEmpty() ? INT_MIN : ParseQuestState( value );
	SplitIdValue( pKV->GetString( "require_quest_stage" ), p.reqQuestStageId, value );
	p.reqQuestStage = value.IsEmpty() ? INT_MIN : atoi( value );
	p.pConds = RPMLogic::Keep( pKV->FindKey( "conditions" ), RPMLogic::POOL_WORLD );
}

void CRPMWorldSystem::ClearDefinitions()
{
	for ( int i = m_NPCDefs.First(); i != m_NPCDefs.InvalidIndex(); i = m_NPCDefs.Next( i ) )
	{
		if ( m_NPCDefs[i]->pKeys )
			m_NPCDefs[i]->pKeys->deleteThis();
		if ( m_NPCDefs[i]->pNames )
			m_NPCDefs[i]->pNames->deleteThis();
		if ( m_NPCDefs[i]->pNameList )
			m_NPCDefs[i]->pNameList->deleteThis();
		if ( m_NPCDefs[i]->pVendor )
			m_NPCDefs[i]->pVendor->deleteThis();
		delete m_NPCDefs[i];
	}
	m_NPCDefs.RemoveAll();
	m_Markers.PurgeAndDeleteElements();
	m_Paths.PurgeAndDeleteElements();
	m_Factions.PurgeAndDeleteElements();
	m_Zones.RemoveAll();
	RPMLogic::FreePool( RPMLogic::POOL_WORLD );
	if ( m_pLayer )
	{
		m_pLayer->deleteThis();
		m_pLayer = NULL;
	}
	m_Nav.nodes.Purge();
	m_Nav.bLoaded = false;
}

void CRPMWorldSystem::LoadDefinitions()
{
	ClearDefinitions();
	m_iGeneration++;
	m_nRelationsAI = -1;
	m_nPlayerRelAI = -1;

	// NPCs
	FileFindHandle_t hFind;
	for ( const char *pszFile = filesystem->FindFirstEx( "scripts/npcs/*.txt", "MOD", &hFind ); pszFile; pszFile = filesystem->FindNext( hFind ) )
	{
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "scripts/npcs/%s", pszFile );
		KeyValues *pKV = new KeyValues( "NPC" );
		if ( !pKV->LoadFromFile( filesystem, szPath, "MOD" ) )
		{
			Warning( "RPMWorld: can't read %s\n", szPath );
			pKV->deleteThis();
			continue;
		}
		char szBase[MAX_PATH];
		Q_FileBase( pszFile, szBase, sizeof( szBase ) );
		RPMNPCDef_t *pDef = new RPMNPCDef_t;
		pDef->targetname = pKV->GetString( "targetname", szBase );
		pDef->classname = pKV->GetString( "classname", "npc_citizen" );
		pDef->model = pKV->GetString( "model" );
		pDef->weapon = pKV->GetString( "weapon" );
		pDef->squad = pKV->GetString( "squad" );
		pDef->faction = pKV->GetString( "faction" );
		pDef->relation = ParseDisposition( pKV->GetString( "player_relation" ) );
		pDef->skin = pKV->GetInt( "skin", 0 );
		pDef->health = pKV->GetInt( "health", 0 );
		pDef->essential = pKV->GetBool( "essential" );
		pDef->commandable = pKV->GetBool( "commandable" );
		V_SplitString( pKV->GetString( "models" ), ";", pDef->models );
		{
			// keep empty entries of weapons: "no weapon" is a valid pick
			const char *w = pKV->GetString( "weapons" );
			if ( w[0] )
			{
				CUtlString cur;
				for ( const char *c = w; ; ++c )
				{
					if ( *c == ';' || *c == 0 )
					{
						pDef->weapons.CopyAndAddToTail( cur.Get() );
						cur.Clear();
						if ( !*c )
							break;
					}
					else
					{
						char t[2] = { *c, 0 };
						cur.Append( t );
					}
				}
			}
		}
		KeyValues *pNameList = pKV->FindKey( "names" );
		pDef->pNameList = pNameList ? pNameList->MakeCopy() : NULL;
		pDef->skinMax = pKV->GetInt( "skin_max", 0 );
		pDef->proficiency = pKV->GetInt( "proficiency", -1 );
		pDef->aggression = pKV->GetInt( "aggression", -1 );
		pDef->confidence = pKV->GetInt( "confidence", -1 );
		pDef->xp = pKV->GetInt( "xp", 0 );
		pDef->yield = pKV->GetInt( "yield", -1 );
		pDef->pickupWeapons = pKV->GetInt( "pickup_weapons", -1 );
		pDef->weaponLaw = pKV->GetInt( "weapon_law", -1 );
		KeyValues *pKeys = pKV->FindKey( "keyvalues" );
		pDef->pKeys = pKeys ? pKeys->MakeCopy() : NULL;
		KeyValues *pVendor = pKV->FindKey( "vendor" );
		pDef->pVendor = pVendor ? pVendor->MakeCopy() : NULL;
		const char *pszStock = pKV->GetString( "stock_speech" );
		pDef->stockSpeech = !Q_stricmp( pszStock, "all" ) ? 0 : !Q_stricmp( pszStock, "combat" ) ? 1 : !Q_stricmp( pszStock, "none" ) ? 2 : -1;
		pDef->pBarks = RPMLogic::Keep( pKV->FindKey( "barks" ), RPMLogic::POOL_WORLD );
		pDef->barkSets = pKV->GetString( "bark_sets" );
		pDef->pNames = new KeyValues( "names" );
		for ( KeyValues *pV = pKV->GetFirstValue(); pV; pV = pV->GetNextValue() )
		{
			if ( !Q_strnicmp( pV->GetName(), "display_name", 12 ) )
				pDef->pNames->SetString( pV->GetName(), pV->GetString() );
		}
		KeyValues *pPackages = pKV->FindKey( "packages" );
		if ( pPackages )
		{
			for ( KeyValues *pPkg = pPackages->GetFirstTrueSubKey(); pPkg; pPkg = pPkg->GetNextTrueSubKey() )
			{
				int i = pDef->packages.AddToTail();
				ParsePackage( pPkg, pDef->packages[i] );
			}
		}
		pKV->deleteThis();

		int existing = m_NPCDefs.Find( pDef->targetname );
		if ( existing != m_NPCDefs.InvalidIndex() )
		{
			Warning( "RPMWorld: two NPC files for '%s', %s wins\n", pDef->targetname.Get(), szPath );
			if ( m_NPCDefs[existing]->pKeys )
				m_NPCDefs[existing]->pKeys->deleteThis();
			if ( m_NPCDefs[existing]->pNames )
				m_NPCDefs[existing]->pNames->deleteThis();
			if ( m_NPCDefs[existing]->pNameList )
				m_NPCDefs[existing]->pNameList->deleteThis();
			if ( m_NPCDefs[existing]->pVendor )
				m_NPCDefs[existing]->pVendor->deleteThis();
			delete m_NPCDefs[existing];
			m_NPCDefs.RemoveAt( existing );
		}
		m_NPCDefs.Insert( pDef->targetname, pDef );
	}
	filesystem->FindClose( hFind );

	// factions
	KeyValues *pFactions = new KeyValues( "Factions" );
	if ( pFactions->LoadFromFile( filesystem, "scripts/world/factions.txt", "MOD" ) )
	{
		for ( KeyValues *pF = pFactions->GetFirstTrueSubKey(); pF; pF = pF->GetNextTrueSubKey() )
		{
			RPMFaction_t *pFac = new RPMFaction_t;
			pFac->id = pF->GetName();
			pFac->pNames = pF->MakeCopy();
			pFac->playerDisp = ParseDisposition( pF->GetString( "player" ) );
			KeyValues *pRel = pF->FindKey( "relations" );
			for ( KeyValues *pR = pRel ? pRel->GetFirstValue() : NULL; pR; pR = pR->GetNextValue() )
			{
				int disp = ParseDisposition( pR->GetString() );
				if ( disp < 0 )
					continue;
				pFac->otherIds.AddToTail( CUtlString( pR->GetName() ) );
				pFac->otherDisps.AddToTail( disp );
			}
			m_Factions.AddToTail( pFac );
		}
	}
	pFactions->deleteThis();

	// the layer of this map: markers and paths are data, refs are spawned on a new map
	char szLayer[MAX_PATH];
	Q_snprintf( szLayer, sizeof( szLayer ), "scripts/world/%s.txt", STRING( gpGlobals->mapname ) );
	m_pLayer = new KeyValues( "World" );
	if ( !m_pLayer->LoadFromFile( filesystem, szLayer, "MOD" ) )
	{
		m_pLayer->deleteThis();
		m_pLayer = NULL;
	}
	KeyValues *pRefs = m_pLayer ? m_pLayer->FindKey( "refs" ) : NULL;
	for ( KeyValues *pRef = pRefs ? pRefs->GetFirstTrueSubKey() : NULL; pRef; pRef = pRef->GetNextTrueSubKey() )
	{
		const char *pszType = pRef->GetString( "type" );
		if ( !Q_stricmp( pszType, "marker" ) )
		{
			RPMMarker_t *pM = new RPMMarker_t;
			pM->name = pRef->GetString( "name" );
			if ( pM->name.IsEmpty() )
				pM->name.Format( "rpm_marker_%s", pRef->GetName() );
			pM->kind = pRef->GetString( "kind", "idle" );
			pM->anim = pRef->GetString( "anim" );
			pM->owner = pRef->GetString( "owner" );
			pM->seatH = pRef->GetFloat( "seat_h", -1.0f );
			if ( m_Markers.Find( pM->name ) != m_Markers.InvalidIndex() )
			{
				Warning( "RPMWorld: two markers named '%s'\n", pM->name.Get() );
				delete pM;
				continue;
			}
			m_Markers.Insert( pM->name, pM );
		}
		else if ( !Q_stricmp( pszType, "zone" ) )
		{
			RPMZone_t z;
			z.pRef = pRef;
			z.name = pRef->GetString( "name" );
			if ( z.name.IsEmpty() )
				z.name.Format( "zone_%s", pRef->GetName() );
			ParseVector( pRef->GetString( "origin" ), z.origin );
			z.size.Init( 256, 256, 128 );
			sscanf( pRef->GetString( "size", "256 256 128" ), "%f %f %f", &z.size.x, &z.size.y, &z.size.z );
			QAngle ang( 0, 0, 0 );
			sscanf( pRef->GetString( "angles", "0 0 0" ), "%f %f %f", &ang.x, &ang.y, &ang.z );
			z.yaw = ang.y;
			m_Zones.AddToTail( z );
		}
		else if ( !Q_stricmp( pszType, "path" ) )
		{
			RPMPath_t *pPath = new RPMPath_t;
			pPath->name = pRef->GetString( "name" );
			if ( pPath->name.IsEmpty() )
				pPath->name.Format( "rpm_path_%s", pRef->GetName() );
			KeyValues *pPoints = pRef->FindKey( "points" );
			for ( KeyValues *pPt = pPoints ? pPoints->GetFirstTrueSubKey() : NULL; pPt; pPt = pPt->GetNextTrueSubKey() )
			{
				RPMPathPoint_t pt;
				ParseVector( pPt->GetString( "origin" ), pt.pos );
				pt.wait = pPt->GetFloat( "wait", 0.0f );
				const char *pszYaw = pPt->GetString( "yaw" );
				pt.hasYaw = pszYaw[0] != 0;
				pt.yaw = pt.hasYaw ? atof( pszYaw ) : 0.0f;
				pt.anim = pPt->GetString( "anim" );
				pPath->points.AddToTail( pt );
			}
			if ( m_Paths.Find( pPath->name ) != m_Paths.InvalidIndex() || pPath->points.Count() == 0 )
			{
				delete pPath;
				continue;
			}
			m_Paths.Insert( pPath->name, pPath );
		}
	}

	DevMsg( "RPMWorld: %d NPC definitions, %d factions, layer %s (%d markers, %d paths, %d zones)\n",
		m_NPCDefs.Count(), m_Factions.Count(), m_pLayer ? szLayer : "none", m_Markers.Count(), m_Paths.Count(), m_Zones.Count() );
}

//-----------------------------------------------------------------------------
// Navigation nodes for maps without info_node: scripts/world/<map>_nav.txt,
// one "x y z" per line. The node graph (.ain) is rebuilt when that file changes.
//-----------------------------------------------------------------------------
static void NavPaths( char *pszNav, int nNav, char *pszStamp, int nStamp, char *pszAin, int nAin )
{
	const char *pszMap = STRING( gpGlobals->mapname );
	if ( pszNav )
		Q_snprintf( pszNav, nNav, "scripts/world/%s_nav.txt", pszMap );
	if ( pszStamp )
		Q_snprintf( pszStamp, nStamp, "maps/graphs/%s.rpmnav", pszMap );
	if ( pszAin )
		Q_snprintf( pszAin, nAin, "maps/graphs/%s.ain", pszMap );
}

static void LoadNavFile( RPMNavFile_t &nav )
{
	nav.nodes.Purge();
	nav.bLoaded = false;
	nav.crc = 0;
	char szNav[MAX_PATH];
	NavPaths( szNav, sizeof( szNav ), NULL, 0, NULL, 0 );
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !filesystem->ReadFile( szNav, "MOD", buf ) )
		return;
	nav.crc = CRC32_ProcessSingleBuffer( buf.Base(), buf.TellPut() );
	CUtlString text;
	text.SetDirect( (const char *)buf.Base(), buf.TellPut() );
	for ( const char *p = text.Get(); p && *p; )
	{
		const char *pEnd = strchr( p, '\n' );
		if ( *p != '/' && *p != '#' )
		{
			Vector v;
			if ( sscanf( p, "%f %f %f", &v.x, &v.y, &v.z ) == 3 )
				nav.nodes.AddToTail( v );
		}
		p = pEnd ? pEnd + 1 : NULL;
	}
	nav.bLoaded = true;
}

// before the world spawns: a changed nav file makes the saved node graph stale
static void CheckNavStamp( const RPMNavFile_t &nav )
{
	char szStamp[MAX_PATH], szAin[MAX_PATH];
	NavPaths( NULL, 0, szStamp, sizeof( szStamp ), szAin, sizeof( szAin ) );

	char szWant[64] = "";
	if ( nav.bLoaded )
		Q_snprintf( szWant, sizeof( szWant ), "%08x %d", (unsigned)nav.crc, nav.nodes.Count() );

	char szHave[64] = "";
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( filesystem->ReadFile( szStamp, "MOD", buf ) )
		buf.GetLine( szHave, sizeof( szHave ) );
	int len = Q_strlen( szHave );
	while ( len > 0 && ( szHave[len - 1] == '\n' || szHave[len - 1] == '\r' ) )
		szHave[--len] = 0;

	if ( !Q_strcmp( szWant, szHave ) )
		return;

	if ( filesystem->FileExists( szAin, "MOD" ) )
	{
		DevMsg( "RPMWorld: nav nodes changed (%s -> %s), rebuilding the node graph\n", szHave[0] ? szHave : "none", szWant[0] ? szWant : "none" );
		filesystem->RemoveFile( szAin, "MOD" );
		if ( filesystem->FileExists( szAin, "MOD" ) )
			filesystem->RemoveFile( szAin, "DEFAULT_WRITE_PATH" );
	}
	if ( szWant[0] )
	{
		filesystem->CreateDirHierarchy( "maps/graphs", "DEFAULT_WRITE_PATH" );
		CUtlBuffer out( 0, 0, CUtlBuffer::TEXT_BUFFER );
		out.Printf( "%s\n", szWant );
		filesystem->WriteFile( szStamp, "DEFAULT_WRITE_PATH", out );
	}
	else
	{
		filesystem->RemoveFile( szStamp, "DEFAULT_WRITE_PATH" );
	}
}

static void AddNavNodesToGraph( const RPMNavFile_t &nav )
{
	if ( !nav.bLoaded || !nav.nodes.Count() || !g_pBigAINet || !g_pAINetworkManager )
		return;
	if ( CAI_NetworkManager::NetworksLoaded() )
		return;	// the saved graph already has them
	CAI_NetworkEditTools *pEdit = g_pAINetworkManager->GetEditOps();
	int nAdded = 0;
	for ( int i = 0; i < nav.nodes.Count() && g_pBigAINet->NumNodes() < MAX_NODES - 1; i++ )
	{
		CAI_Node *pNode = g_pBigAINet->AddNode( nav.nodes[i], 0.0f );
		pNode->SetType( NODE_GROUND );
		if ( pEdit && pEdit->m_pNodeIndexTable )
			pEdit->m_pNodeIndexTable[pNode->GetId()] = pEdit->m_nNextWCIndex++;
		nAdded++;
	}
	DevMsg( "RPMWorld: %d nav nodes added to the node graph (%d total)\n", nAdded, g_pBigAINet->NumNodes() );
}

//-----------------------------------------------------------------------------
// Level
//-----------------------------------------------------------------------------
void CRPMWorldSystem::LevelInitPreEntity()
{
	s_bTransitionsSpawned = false;
	m_bSpawnLayer = ( gpGlobals->eLoadType != MapLoad_LoadGame ) && ( gpGlobals->eLoadType != MapLoad_Background ) && !g_bHL2RPMRestoringLevel;
	m_DialogNotes.Purge();
	m_flNextTick = 0;
	m_flNextPresence = 0;
	LoadDefinitions();
	LoadNavFile( m_Nav );
	CheckNavStamp( m_Nav );
}

void CRPMWorldSystem::LevelInitPostEntity()
{
	AddNavNodesToGraph( m_Nav );
	if ( m_bSpawnLayer && rpm_world.GetBool() )
		SpawnLayer();
	m_bSpawnLayer = false;
	// faction relations before anyone sees anyone (the player comes later, the tick does him)
	ApplyRelations( true );
}

void CRPMWorldSystem::LevelShutdownPostEntity()
{
	ClearDefinitions();
	m_DialogNotes.Purge();
}

static void FormatVec( char *psz, int n, const Vector &v )
{
	Q_snprintf( psz, n, "%.2f %.2f %.2f", v.x, v.y, v.z );
}

static void TagLayerEntity( CBaseEntity *pEnt, const char *pszRefId )
{
	pEnt->AddContext( "rpmref", pszRefId, 0.0f );
}

static void ApplyKeyBlock( CBaseEntity *pEnt, KeyValues *pKeys )
{
	for ( KeyValues *pK = pKeys ? pKeys->GetFirstValue() : NULL; pK; pK = pK->GetNextValue() )
		pEnt->KeyValue( pK->GetName(), pK->GetString() );
}

// the entity of an NPC definition at a place
static CBaseEntity *SpawnNPCFromDef( const RPMNPCDef_t &def, const Vector &origin, const QAngle &angles, const char *pszRefId )
{
	CBaseEntity *pEnt = CreateEntityByName( def.classname.IsEmpty() ? "npc_citizen" : def.classname.Get() );
	if ( !pEnt )
	{
		Warning( "RPMWorld: unknown NPC class '%s' (%s)\n", def.classname.Get(), def.targetname.Get() );
		return NULL;
	}
	char sz[128];
	pEnt->KeyValue( "targetname", def.targetname );
	FormatVec( sz, sizeof( sz ), origin );
	pEnt->KeyValue( "origin", sz );
	Q_snprintf( sz, sizeof( sz ), "%.1f %.1f %.1f", angles.x, angles.y, angles.z );
	pEnt->KeyValue( "angles", sz );
	const char *pszModel = def.models.Count() ? def.models[RandomInt( 0, def.models.Count() - 1 )] : def.model.Get();
	if ( pszModel && pszModel[0] )
	{
		pEnt->KeyValue( "model", pszModel );
		// a citizen picks a random model of its type unless it is "unique"
		if ( FClassnameIs( pEnt, "npc_citizen" ) && !( def.pKeys && def.pKeys->FindKey( "citizentype" ) ) )
			pEnt->KeyValue( "citizentype", "4" );
	}
	int skin = def.skinMax > def.skin ? RandomInt( def.skin, def.skinMax ) : def.skin;
	if ( skin )
		pEnt->KeyValue( "skin", skin );
	const char *pszWeapon = def.weapons.Count() ? def.weapons[RandomInt( 0, def.weapons.Count() - 1 )] : def.weapon.Get();
	if ( pszWeapon && pszWeapon[0] )
		pEnt->KeyValue( "additionalequipment", pszWeapon );
	if ( !def.squad.IsEmpty() )
		pEnt->KeyValue( "squadname", def.squad );
	if ( !def.faction.IsEmpty() )
		pEnt->KeyValue( "ftag", def.faction );
	ApplyKeyBlock( pEnt, def.pKeys );
	if ( DispatchSpawn( pEnt ) < 0 )
		return NULL;
	pEnt->Activate();
	TagLayerEntity( pEnt, pszRefId );
	if ( def.health > 0 )
	{
		pEnt->SetMaxHealth( def.health );
		pEnt->SetHealth( def.health );
	}
	if ( def.pNameList )
	{
		// names marked "female" go to female models, the rest to the others; a list of one kind only serves everyone
		const char *pszMdl = STRING( pEnt->GetModelName() );
		const bool bFemale = Q_stristr( pszMdl, "female" ) || Q_stristr( pszMdl, "alyx" ) || Q_stristr( pszMdl, "mossman" );
		CUtlVector<int> match, all;
		int i = 0;
		for ( KeyValues *k = def.pNameList->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey(), i++ )
		{
			all.AddToTail( i );
			if ( k->GetBool( "female" ) == bFemale )
				match.AddToTail( i );
		}
		const CUtlVector<int> &from = match.Count() ? match : all;
		if ( from.Count() )
			pEnt->AddContext( "rpmname", CFmtStr( "%d", from[RandomInt( 0, from.Count() - 1 )] ), 0.0f );
	}
	if ( def.proficiency >= 0 )
	{
		variant_t v;
		v.SetInt( def.proficiency );
		pEnt->AcceptInput( "SetProficiencyOverride", pEnt, pEnt, v, 0 );
	}
	return pEnt;
}

CBaseEntity *RPMWorld::SpawnNPC( const char *pszDef, const Vector &origin, const QAngle &angles, const char *pszTag )
{
	const RPMNPCDef_t *pDef = FindNPCDef( pszDef );
	if ( !pDef )
		return NULL;
	return SpawnNPCFromDef( *pDef, origin, angles, pszTag );
}

static const char *DefaultMarkerAnim( const char *pszKind )
{
	if ( !Q_stricmp( pszKind, "sleep" ) )	return "laycouch1";
	if ( !Q_stricmp( pszKind, "sit" ) )		return "ACT_BUSY_SIT_CHAIR";
	if ( !Q_stricmp( pszKind, "eat" ) )		return "sitchairtable1";
	if ( !Q_stricmp( pszKind, "lean" ) )	return "ACT_BUSY_LEAN_BACK";
	if ( !Q_stricmp( pszKind, "ground" ) )	return "ACT_BUSY_SIT_GROUND";
	return "";
}

static CAI_Hint *SpawnMarker( const RPMMarker_t &marker, const Vector &origin, float flYaw, const char *pszRefId )
{
	HintNodeData data;
	data.strEntityName = AllocPooledString( marker.name );
	data.vecPosition = origin;
	data.nHintType = HINT_WORLD_WORK_POSITION;
	data.nNodeID = NO_NODE;
	// a group of its own: actbusy goals of the map never pick the kit's markers
	data.strGroup = AllocPooledString( "rpm_marker" );
	data.iDisabled = 0;
	const char *pszAnim = marker.anim.IsEmpty() ? DefaultMarkerAnim( marker.kind ) : marker.anim.Get();
	data.iszActivityName = pszAnim[0] ? AllocPooledString( pszAnim ) : NULL_STRING;
	data.nTargetWCNodeID = -1;
	data.fIgnoreFacing = HIF_NO;
	data.minState = NPC_STATE_IDLE;
	data.maxState = NPC_STATE_ALERT;
#ifdef MAPBASE
	data.nRadius = 0;
	data.flWeight = 1.0f;
	data.flWeightInverse = 1.0f;
#endif
	data.nWCNodeID = -1;
	CAI_Hint *pHint = CAI_HintManager::CreateHint( &data, NULL );
	if ( !pHint )
		return NULL;
	pHint->SetLocalAngles( QAngle( 0, flYaw, 0 ) );
	pHint->KeyValue( "nodeFOV", "360" );
	pHint->Activate();
	TagLayerEntity( pHint, pszRefId );
	return pHint;
}

//-----------------------------------------------------------------------------
// Transitions between maps (FNV-like doors). A layer ref "transition" is a door box on
// this map leading to the paired transition "target" on "target_map", plus the point
// "arrive" where the player coming through the paired door appears. Engine transitions
// need a landmark of the same name in both maps and a trigger_changelevel back in the
// new map; every door makes:
//   info_landmark rpmlm_<name> at the door, info_landmark rpmlm_<target> at the arrival point,
//   trigger_changelevel (touch) -> target_map / rpmlm_<name>,
//   trigger_changelevel (no touch, only the connection) -> target_map / rpmlm_<target>,
//   trigger_transition rpmlm_<name> (door box) and rpmlm_<target> (a small box at the arrival
//   point) - without them everything in the landmark's PVS (an open map!) would go along.
//-----------------------------------------------------------------------------
#define RPM_LANDMARK_HEIGHT 48.0f

static CBaseEntity *SpawnLayerEntity( const char *pszClass, const char *pszRefId, const Vector &origin, const char *pszName )
{
	CBaseEntity *pEnt = CreateEntityByName( pszClass );
	if ( !pEnt )
		return NULL;
	char sz[128];
	FormatVec( sz, sizeof( sz ), origin );
	pEnt->KeyValue( "origin", sz );
	if ( pszName && pszName[0] )
		pEnt->KeyValue( "targetname", pszName );
	return pEnt;
}

static bool FinishLayerEntity( CBaseEntity *pEnt, const char *pszRefId )
{
	if ( DispatchSpawn( pEnt ) < 0 )
		return false;
	pEnt->Activate();
	TagLayerEntity( pEnt, pszRefId );
	return true;
}

static void SetBoxKeys( CBaseEntity *pEnt, const Vector &mins, const Vector &maxs )
{
	char sz[128];
	FormatVec( sz, sizeof( sz ), mins );
	pEnt->KeyValue( "rpm_box_mins", sz );
	FormatVec( sz, sizeof( sz ), maxs );
	pEnt->KeyValue( "rpm_box_maxs", sz );
}

static bool SpawnTransition( KeyValues *pRef, const char *pszId )
{
	const char *pszName = pRef->GetString( "name" );
	const char *pszMap = pRef->GetString( "target_map" );
	const char *pszTarget = pRef->GetString( "target" );
	if ( !pszName[0] || !pszMap[0] || !pszTarget[0] )
	{
		Warning( "RPMWorld: transition %s needs name, target_map and target\n", pszId );
		return false;
	}
	Vector origin, arrive;
	ParseVector( pRef->GetString( "origin" ), origin );
	ParseVector( pRef->GetString( "arrive", pRef->GetString( "origin" ) ), arrive );
	Vector size( 64, 64, 96 );
	sscanf( pRef->GetString( "size", "64 64 96" ), "%f %f %f", &size.x, &size.y, &size.z );
	char szLm[128], szLmIn[128];
	Q_snprintf( szLm, sizeof( szLm ), "rpmlm_%s", pszName );
	Q_snprintf( szLmIn, sizeof( szLmIn ), "rpmlm_%s", pszTarget );
	const Vector half( size.x * 0.5f, size.y * 0.5f, 0.0f );

	CBaseEntity *pEnt;
	if ( ( pEnt = SpawnLayerEntity( "info_landmark", pszId, origin + Vector( 0, 0, RPM_LANDMARK_HEIGHT ), szLm ) ) != NULL )
		FinishLayerEntity( pEnt, pszId );
	if ( ( pEnt = SpawnLayerEntity( "info_landmark", pszId, arrive + Vector( 0, 0, RPM_LANDMARK_HEIGHT ), szLmIn ) ) != NULL )
		FinishLayerEntity( pEnt, pszId );
	if ( ( pEnt = SpawnLayerEntity( "trigger_changelevel", pszId, origin, NULL ) ) != NULL )
	{
		pEnt->KeyValue( "map", pszMap );
		pEnt->KeyValue( "landmark", szLm );
		SetBoxKeys( pEnt, -half, Vector( half.x, half.y, size.z ) );
		FinishLayerEntity( pEnt, pszId );
	}
	if ( ( pEnt = SpawnLayerEntity( "trigger_changelevel", pszId, arrive, NULL ) ) != NULL )
	{
		pEnt->KeyValue( "map", pszMap );
		pEnt->KeyValue( "landmark", szLmIn );
		pEnt->KeyValue( "spawnflags", "2" );		// no touch: only the connection back for the engine
		SetBoxKeys( pEnt, Vector( -4, -4, 0 ), Vector( 4, 4, 8 ) );
		FinishLayerEntity( pEnt, pszId );
	}
	if ( ( pEnt = SpawnLayerEntity( "trigger_transition", pszId, origin, szLm ) ) != NULL )
	{
		SetBoxKeys( pEnt, -half - Vector( 16, 16, 0 ), Vector( half.x + 16, half.y + 16, size.z + 16 ) );
		FinishLayerEntity( pEnt, pszId );
	}
	if ( ( pEnt = SpawnLayerEntity( "trigger_transition", pszId, arrive, szLmIn ) ) != NULL )
	{
		SetBoxKeys( pEnt, Vector( -4, -4, 0 ), Vector( 4, 4, 8 ) );
		FinishLayerEntity( pEnt, pszId );
	}
	return true;
}

static char s_szArrivalLandmark[128];
static float s_flArrivalYaw = 0.0f;
static bool s_bArrivalPending = false;

void CRPMWorldSystem::SpawnTransitions()
{
	if ( s_bTransitionsSpawned )
		return;
	s_bTransitionsSpawned = true;
	KeyValues *pRefs = m_pLayer ? m_pLayer->FindKey( "refs" ) : NULL;
	for ( KeyValues *pRef = pRefs ? pRefs->GetFirstTrueSubKey() : NULL; pRef; pRef = pRef->GetNextTrueSubKey() )
	{
		if ( !Q_stricmp( pRef->GetString( "type" ), "transition" ) )
			SpawnTransition( pRef, pRef->GetName() );
	}
}

void RPMWorld::SpawnTransitionsEarly()
{
	if ( g_RPMWorld.m_bSpawnLayer && rpm_world.GetBool() )
		g_RPMWorld.SpawnTransitions();
}

void RPMWorld::NoteArrival( const char *pszLandmark )
{
	// the player came through a door of the layer: face the direction of its arrival point
	s_bArrivalPending = false;
	if ( !pszLandmark || Q_strnicmp( pszLandmark, "rpmlm_", 6 ) )
		return;
	KeyValues *pRefs = g_RPMWorld.m_pLayer ? g_RPMWorld.m_pLayer->FindKey( "refs" ) : NULL;
	for ( KeyValues *pRef = pRefs ? pRefs->GetFirstTrueSubKey() : NULL; pRef; pRef = pRef->GetNextTrueSubKey() )
	{
		if ( Q_stricmp( pRef->GetString( "type" ), "transition" ) || Q_stricmp( pRef->GetString( "target" ), pszLandmark + 6 ) )
			continue;
		Q_strncpy( s_szArrivalLandmark, pszLandmark, sizeof( s_szArrivalLandmark ) );
		s_flArrivalYaw = pRef->GetFloat( "arrive_yaw", 0.0f );
		s_bArrivalPending = true;
		return;
	}
}

// presence of layer objects ("conditions" of a ref): objects the conditions removed or kept from spawning
// are marked in the RPG state, so an NPC killed or an item taken by the player doesn't come back
static KeyValues *PresenceState()
{
	return RPMState::Section( "presence" );
}

static CUtlString PresenceKey( const char *pszRefId )
{
	CUtlString k;
	k.Format( "%s.%s", STRING( gpGlobals->mapname ), pszRefId );
	return k;
}

void CRPMWorldSystem::SpawnLayer()
{
	KeyValues *pRefs = m_pLayer ? m_pLayer->FindKey( "refs" ) : NULL;
	if ( !pRefs )
		return;
	SpawnTransitions();

	int nCount[3] = { 0, 0, 0 }, nFailed = 0, nHidden = 0;
	RPMLogic::Ctx_t ctx;
	for ( KeyValues *pRef = pRefs->GetFirstTrueSubKey(); pRef; pRef = pRef->GetNextTrueSubKey() )
	{
		KeyValues *pConds = pRef->FindKey( "conditions" );
		if ( pConds && !RPMLogic::Check( pConds, ctx ) )
		{
			PresenceState()->SetInt( PresenceKey( pRef->GetName() ), 1 );
			nHidden++;
			continue;
		}
		int kind;
		SpawnRef( pRef, kind );
		if ( kind >= 0 )
			nCount[kind]++;
		else if ( kind == -1 )
			nFailed++;
	}
	Msg( "RPMWorld: %s - spawned %d NPCs, %d markers, %d objects%s%s\n", STRING( gpGlobals->mapname ), nCount[0], nCount[1], nCount[2],
		nHidden ? CFmtStr( ", %d waiting for their conditions", nHidden ).Access() : "",
		nFailed ? CFmtStr( ", %d failed (see the warnings)", nFailed ).Access() : "" );
}

// the entity of a layer ref; kind: 0 NPC, 1 marker, 2 other, -1 failed, -2 data only (paths, zones, transitions)
CBaseEntity *CRPMWorldSystem::SpawnRef( KeyValues *pRef, int &kind )
{
	kind = -1;
	{
		const char *pszType = pRef->GetString( "type" );
		const char *pszId = pRef->GetName();
		Vector origin;
		ParseVector( pRef->GetString( "origin" ), origin );
		QAngle angles( 0, 0, 0 );
		sscanf( pRef->GetString( "angles", "0 0 0" ), "%f %f %f", &angles.x, &angles.y, &angles.z );

		if ( !Q_stricmp( pszType, "npc" ) )
		{
			const RPMNPCDef_t *pDef = FindNPCDef( pRef->GetString( "npc" ) );
			if ( !pDef )
			{
				Warning( "RPMWorld: ref %s - no NPC definition '%s'\n", pszId, pRef->GetString( "npc" ) );
				return NULL;
			}
			CBaseEntity *pEnt = SpawnNPCFromDef( *pDef, origin, angles, pszId );
			if ( pEnt )
				kind = 0;
			return pEnt;
		}
		else if ( !Q_stricmp( pszType, "marker" ) )
		{
			char szName[128];
			Q_strncpy( szName, pRef->GetString( "name" ), sizeof( szName ) );
			if ( !szName[0] )
				Q_snprintf( szName, sizeof( szName ), "rpm_marker_%s", pszId );
			const RPMMarker_t *pMarker = FindMarker( szName );
			CAI_Hint *pHint = pMarker ? SpawnMarker( *pMarker, origin, angles.y, pszId ) : NULL;
			if ( pHint )
				kind = 1;
			return pHint;
		}
		else if ( !Q_stricmp( pszType, "zone" ) )
		{
			// a location with a soundscape: the engine's own triggerable soundscape and its box trigger
			kind = -2;
			const char *pszScape = pRef->GetString( "soundscape" );
			if ( !pszScape[0] )
				return NULL;
			Vector size( 256, 256, 128 );
			sscanf( pRef->GetString( "size", "256 256 128" ), "%f %f %f", &size.x, &size.y, &size.z );
			char szName[128];
			Q_snprintf( szName, sizeof( szName ), "rpmss_%s", pszId );
			CBaseEntity *pScape = SpawnLayerEntity( "env_soundscape_triggerable", pszId, origin + Vector( 0, 0, size.z * 0.5f ), szName );
			if ( pScape )
			{
				pScape->KeyValue( "soundscape", pszScape );
				pScape->KeyValue( "radius", "-1" );
				FinishLayerEntity( pScape, pszId );
			}
			CBaseEntity *pTrig = SpawnLayerEntity( "trigger_soundscape", pszId, origin, NULL );
			if ( pTrig )
			{
				// the box of the zone turned by its yaw, as an axis-aligned box
				float s, c;
				SinCos( DEG2RAD( angles.y ), &s, &c );
				float hx = fabsf( c ) * size.x * 0.5f + fabsf( s ) * size.y * 0.5f;
				float hy = fabsf( s ) * size.x * 0.5f + fabsf( c ) * size.y * 0.5f;
				pTrig->KeyValue( "soundscape", szName );
				pTrig->KeyValue( "spawnflags", "1" );		// clients
				SetBoxKeys( pTrig, Vector( -hx, -hy, 0 ), Vector( hx, hy, size.z ) );
				FinishLayerEntity( pTrig, pszId );
			}
			return pScape;
		}
		else if ( !Q_stricmp( pszType, "sound" ) )
		{
			// a sound on the map: ambient_generic (time of day / quest - the presence conditions)
			CBaseEntity *pEnt = SpawnLayerEntity( "ambient_generic", pszId, origin, pRef->GetString( "name" ) );
			if ( !pEnt )
				return NULL;
			const char *pszSnd = pRef->GetString( "sound" );
			CBaseEntity::PrecacheSound( pszSnd );
			pEnt->KeyValue( "message", pszSnd );
			pEnt->KeyValue( "health", CFmtStr( "%d", clamp( (int)( pRef->GetFloat( "volume", 1.0f ) * 10.0f + 0.5f ), 1, 10 ) ) );
			pEnt->KeyValue( "radius", pRef->GetString( "radius", "1250" ) );
			pEnt->KeyValue( "pitch", "100" );
			pEnt->KeyValue( "spawnflags", pRef->GetBool( "everywhere" ) ? "1" : "0" );
			if ( !FinishLayerEntity( pEnt, pszId ) )
				return NULL;
			kind = 2;
			return pEnt;
		}
		else if ( !Q_stricmp( pszType, "path" ) || !Q_stricmp( pszType, "transition" ) )
		{
			// data only / spawned by SpawnTransitions
			kind = -2;
			return NULL;
		}
		else
		{
			// item / container / prop / entity
			const char *pszClass = pRef->GetString( "classname" );
			if ( !Q_stricmp( pszType, "container" ) )
				pszClass = "prop_loot_container";
			else if ( !Q_stricmp( pszType, "prop" ) )
				pszClass = pRef->GetBool( "physics" ) ? "prop_physics_override" : "prop_dynamic_override";
			else if ( !Q_stricmp( pszType, "spawner" ) )
				pszClass = "rpm_spawner";
			else if ( !Q_stricmp( pszType, "terminal" ) )
				pszClass = "rpm_terminal";
			// an item of the database without an entity class of its own (note, key, money...)
			const char *pszItemId = NULL;
			if ( !Q_stricmp( pszType, "item" ) && RPMItems::IsGeneric( pszClass ) )
			{
				pszItemId = pszClass;
				pszClass = "item_rpm";
			}
			CBaseEntity *pEnt = pszClass[0] ? CreateEntityByName( pszClass ) : NULL;
			if ( pEnt && pszItemId )
			{
				pEnt->KeyValue( "item", pszItemId );
				if ( pRef->GetInt( "count", 1 ) > 1 )
					pEnt->KeyValue( "count", pRef->GetString( "count" ) );
			}
			if ( !pEnt )
			{
				Warning( "RPMWorld: ref %s - can't create '%s'\n", pszId, pszClass );
				return NULL;
			}
			char sz[128];
			FormatVec( sz, sizeof( sz ), origin );
			pEnt->KeyValue( "origin", sz );
			Q_snprintf( sz, sizeof( sz ), "%.1f %.1f %.1f", angles.x, angles.y, angles.z );
			pEnt->KeyValue( "angles", sz );
			const char *pszName = pRef->GetString( "name" );
			if ( pszName[0] )
				pEnt->KeyValue( "targetname", pszName );
			static const char *s_pszPassKeys[] = { "model", "skin", "loot_items", "loot_displayname", "loot_sound_open", "loot_sound_close",
				"terminal", "style", "screen_pos", "screen_ang", "screen_size" };
			for ( int k = 0; k < ARRAYSIZE( s_pszPassKeys ); k++ )
			{
				const char *pszValue = pRef->GetString( s_pszPassKeys[k] );
				if ( pszValue[0] )
					pEnt->KeyValue( s_pszPassKeys[k], pszValue );
			}
			if ( !Q_stricmp( pszType, "prop" ) && !pRef->GetBool( "physics" ) )
				pEnt->KeyValue( "solid", "6" );
			ApplyKeyBlock( pEnt, pRef->FindKey( "keyvalues" ) );
			if ( pEnt->GetModelName() != NULL_STRING )
				CBaseEntity::PrecacheModel( STRING( pEnt->GetModelName() ) );
			if ( DispatchSpawn( pEnt ) < 0 )
				return NULL;
			pEnt->Activate();
			TagLayerEntity( pEnt, pszId );
			kind = 2;
			return pEnt;
		}
	}
}

void CRPMWorldSystem::UpdatePresence()
{
	KeyValues *pRefs = m_pLayer ? m_pLayer->FindKey( "refs" ) : NULL;
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( !pRefs || !pPlayer )
		return;
	bool bAny = false;
	for ( KeyValues *pRef = pRefs->GetFirstTrueSubKey(); pRef && !bAny; pRef = pRef->GetNextTrueSubKey() )
		bAny = pRef->FindKey( "conditions" ) != NULL;
	if ( !bAny )
		return;
	// the entities of the layer by ref id
	CUtlDict<CBaseEntity *, int> ents;
	for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
	{
		const char *pszRef = pEnt->FindContextByName( "rpmref" ) != -1 ? pEnt->GetContextValue( "rpmref" ) : NULL;
		if ( pszRef && pszRef[0] && ents.Find( pszRef ) == ents.InvalidIndex() && !pEnt->IsMarkedForDeletion() )
			ents.Insert( pszRef, pEnt );
	}
	KeyValues *pState = PresenceState();
	for ( KeyValues *pRef = pRefs->GetFirstTrueSubKey(); pRef; pRef = pRef->GetNextTrueSubKey() )
	{
		KeyValues *pConds = pRef->FindKey( "conditions" );
		const char *pszType = pRef->GetString( "type" );
		if ( !pConds || !Q_stricmp( pszType, "path" ) || !Q_stricmp( pszType, "transition" ) || !Q_stricmp( pszType, "zone" ) )
			continue;		// (zone soundscapes have no presence)
		const char *pszId = pRef->GetName();
		int i = ents.Find( pszId );
		CBaseEntity *pEnt = i != ents.InvalidIndex() ? ents[i] : NULL;
		RPMLogic::Ctx_t ctx;
		ctx.pPlayer = pPlayer;
		ctx.pSelf = pEnt;
		bool bWant = RPMLogic::Check( pConds, ctx );
		CUtlString key = PresenceKey( pszId );
		if ( bWant && !pEnt && pState->GetInt( key, 0 ) )
		{
			int kind;
			if ( SpawnRef( pRef, kind ) )
			{
				if ( KeyValues *k = pState->FindKey( key ) )
				{
					pState->RemoveSubKey( k );
					k->deleteThis();
				}
				if ( rpm_logic_debug.GetBool() )
					Msg( "[logic] presence: %s appears\n", pszId );
			}
		}
		else if ( !bWant && pEnt )
		{
			// an NPC doesn't vanish in front of the player
			if ( pEnt->MyNPCPointer() && ( pEnt->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).Length() < 2048.0f &&
				pPlayer->FInViewCone( pEnt ) && pPlayer->FVisible( pEnt ) )
				continue;
			for ( CBaseEntity *p = gEntList.FirstEnt(); p; p = gEntList.NextEnt( p ) )
			{
				if ( p->IsPlayer() || p->FindContextByName( "rpmref" ) == -1 || Q_strcmp( p->GetContextValue( "rpmref" ), pszId ) )
					continue;
				UTIL_Remove( p );
			}
			pState->SetInt( key, 1 );
			if ( rpm_logic_debug.GetBool() )
				Msg( "[logic] presence: %s removed\n", pszId );
		}
	}
}

int RPMWorld::ZoneCount() { return g_RPMWorld.m_Zones.Count(); }
KeyValues *RPMWorld::ZoneKeys( int i ) { return g_RPMWorld.m_Zones.IsValidIndex( i ) ? g_RPMWorld.m_Zones[i].pRef : NULL; }
const char *RPMWorld::ZoneName( int i ) { return g_RPMWorld.m_Zones.IsValidIndex( i ) ? g_RPMWorld.m_Zones[i].name.Get() : ""; }
Vector RPMWorld::ZoneSize( int i ) { return g_RPMWorld.m_Zones.IsValidIndex( i ) ? g_RPMWorld.m_Zones[i].size : vec3_origin; }

int RPMWorld::FindZone( const char *pszName )
{
	for ( int i = 0; pszName && pszName[0] && i < g_RPMWorld.m_Zones.Count(); i++ )
		if ( !Q_stricmp( g_RPMWorld.m_Zones[i].name, pszName ) )
			return i;
	return -1;
}

Vector RPMWorld::ZoneCenter( int i )
{
	if ( !g_RPMWorld.m_Zones.IsValidIndex( i ) )
		return vec3_origin;
	const RPMZone_t &z = g_RPMWorld.m_Zones[i];
	return z.origin + Vector( 0, 0, z.size.z * 0.5f );
}

bool RPMWorld::IsInZone( int i, const Vector &pos )
{
	if ( !g_RPMWorld.m_Zones.IsValidIndex( i ) )
		return false;
	const RPMZone_t &z = g_RPMWorld.m_Zones[i];
	Vector d = pos - z.origin;
	float s, c;
	SinCos( DEG2RAD( -z.yaw ), &s, &c );
	float x = d.x * c - d.y * s, y = d.x * s + d.y * c;
	return fabsf( x ) <= z.size.x * 0.5f && fabsf( y ) <= z.size.y * 0.5f && d.z >= -16.0f && d.z <= z.size.z;
}

int CRPMWorldSystem::RemoveLayerEntities()
{
	int n = 0;
	for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
	{
		if ( pEnt->FindContextByName( "rpmref" ) == -1 )
			continue;
		if ( pEnt->IsPlayer() )
			continue;
		UTIL_Remove( pEnt );
		n++;
	}
	return n;
}

//-----------------------------------------------------------------------------
// Relations: definition / faction disposition to the player (once per NPC - the
// player may anger them later), faction vs faction, essential NPCs
//-----------------------------------------------------------------------------
void CRPMWorldSystem::ApplyRelations( bool bForce )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	const int nAIs = g_AI_Manager.NumAIs();

	for ( int i = 0; i < nAIs; i++ )
	{
		CAI_BaseNPC *pNPC = ppAIs[i];
		if ( !pNPC || !pNPC->IsAlive() )
			continue;
		const RPMNPCDef_t *pDef = FindNPCDef( pNPC->GetEntityName().ToCStr() );
		if ( pDef )
		{
			pNPC->SetRPMEssential( pDef->essential );
			// HL2 recruits every friendly citizen near the player into his squad: an NPC
			// with a daily routine stays out of it unless its definition allows it
			const int SF_CITIZEN_NOT_COMMANDABLE = ( 1 << 20 );
			if ( !pDef->commandable && FClassnameIs( pNPC, "npc_citizen" ) && !pNPC->HasSpawnFlags( SF_CITIZEN_NOT_COMMANDABLE ) )
				pNPC->AddSpawnFlags( SF_CITIZEN_NOT_COMMANDABLE );
		}
		if ( !pPlayer || pNPC->HasRPMPlayerRelation() )
			continue;
		int disp = pDef ? pDef->relation : -1;
		if ( disp < 0 )
		{
			const RPMFaction_t *pFac = FindFaction( pNPC->GetFactionTag() );
			disp = pFac ? pFac->playerDisp : -1;
		}
		// aggression (New Vegas): 0 - never attacks the player first (hate_on_dmg answers a hit),
		// 2 - attacks him unless he is a friend, 3 - attacks everybody
		if ( pDef && pDef->aggression >= 0 )
		{
			const int cur = disp >= 0 ? disp : pNPC->IRelationType( pPlayer );
			if ( pDef->aggression == 0 && cur == D_HT )
				disp = D_NU;
			else if ( pDef->aggression == 2 && cur != D_LI )
				disp = D_HT;
			else if ( pDef->aggression == 3 )
				disp = D_HT;
		}
		if ( disp >= 0 )
			pNPC->AddEntityRelationship( pPlayer, (Disposition_t)disp, 10 );
		if ( disp >= 0 || pDef )
			pNPC->SetRPMPlayerRelation();
	}

	// faction vs faction: only when the set of NPCs changes
	if ( !bForce && nAIs == m_nRelationsAI )
		return;
	m_nRelationsAI = nAIs;
	// frenzied NPCs (aggression 3) hate every other NPC too
	for ( int i = 0; i < nAIs; i++ )
	{
		const RPMNPCDef_t *pDef = ppAIs[i] && ppAIs[i]->IsAlive() ? FindNPCDef( ppAIs[i]->GetEntityName().ToCStr() ) : NULL;
		if ( !pDef || pDef->aggression != 3 )
			continue;
		for ( int j = 0; j < nAIs; j++ )
			if ( j != i && ppAIs[j] && ppAIs[j]->IsAlive() )
				ppAIs[i]->AddEntityRelationship( ppAIs[j], D_HT, 50 );
	}
	if ( !m_Factions.Count() )
		return;
	for ( int i = 0; i < nAIs; i++ )
	{
		CAI_BaseNPC *pA = ppAIs[i];
		const RPMFaction_t *pFac = pA ? FindFaction( pA->GetFactionTag() ) : NULL;
		if ( !pFac || !pFac->otherIds.Count() )
			continue;
		for ( int j = 0; j < nAIs; j++ )
		{
			CAI_BaseNPC *pB = ppAIs[j];
			if ( !pB || pB == pA )
				continue;
			const char *pszOther = pB->GetFactionTag();
			if ( !pszOther[0] )
				continue;
			for ( int k = 0; k < pFac->otherIds.Count(); k++ )
			{
				if ( !Q_stricmp( pFac->otherIds[k], pszOther ) )
				{
					pA->AddEntityRelationship( pB, (Disposition_t)pFac->otherDisps[k], 10 );
					break;
				}
			}
		}
	}
}

void CRPMWorldSystem::FrameUpdatePostEntityThink()
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( pPlayer && s_bArrivalPending )
	{
		s_bArrivalPending = false;
		pPlayer->SnapEyeAngles( QAngle( 0, s_flArrivalYaw, 0 ) );
		Msg( "RPMWorld: arrived through %s at %.0f %.0f %.0f, facing %.0f\n", s_szArrivalLandmark,
			pPlayer->GetAbsOrigin().x, pPlayer->GetAbsOrigin().y, pPlayer->GetAbsOrigin().z, s_flArrivalYaw );
	}
	if ( pPlayer && rpm_start.GetString()[0] && gpGlobals->curtime > 0.5f )
	{
		Vector pos;
		QAngle ang( 0, 0, 0 );
		float flHour = -1;
		if ( sscanf( rpm_start.GetString(), "%f %f %f %f %f %f", &pos.x, &pos.y, &pos.z, &ang.x, &ang.y, &flHour ) >= 3 )
		{
			pPlayer->Teleport( &pos, &ang, &vec3_origin );
			pPlayer->SnapEyeAngles( ang );
			if ( flHour >= 0 )
				engine->ServerCommand( CFmtStr( "sv_timecycle_set_time %.2f\n", flHour ) );
		}
		rpm_start.SetValue( "" );
	}
	if ( pPlayer && rpm_nav_autogen.GetString()[0] && gpGlobals->curtime > 3.0f )
	{
		char szCmd[512];
		Q_snprintf( szCmd, sizeof( szCmd ), "rpm_nav_generate %s\nwait 30\nquit\n", rpm_nav_autogen.GetString() );
		rpm_nav_autogen.SetValue( "" );
		engine->ServerCommand( szCmd );
	}
	// relations to the player as soon as there is a player / a new NPC: until then the class
	// relations hold (police hates the player) - a cop who saw the player in that window went
	// into combat on the first frames of the map and kept his baton on
	if ( pPlayer && g_AI_Manager.NumAIs() != m_nPlayerRelAI )
	{
		ApplyRelations( false );
		m_nPlayerRelAI = g_AI_Manager.NumAIs();
	}
	else if ( !pPlayer )
		m_nPlayerRelAI = -1;
	if ( gpGlobals->curtime >= m_flNextPresence && gpGlobals->curtime > 1.0f )
	{
		m_flNextPresence = gpGlobals->curtime + 0.5f;
		UpdatePresence();
	}
	if ( gpGlobals->curtime >= m_flNextCourage )
	{
		m_flNextCourage = gpGlobals->curtime + 0.25f;
		UpdateConfidence();
	}
	if ( gpGlobals->curtime < m_flNextTick )
		return;
	m_flNextTick = gpGlobals->curtime + 1.0f;
	ApplyRelations( false );
	UpdateZoneSoundscapes();
}

//-----------------------------------------------------------------------------
// Confidence (New Vegas): an NPC of a definition with confidence 0..2 drops the
// fight below its share of health (cowardly - at once) and runs from its enemy;
// the enemy stays feared (the health of NPCs does not come back)
//-----------------------------------------------------------------------------
void CRPMWorldSystem::UpdateConfidence()
{
	static const float s_flFleeBelow[] = { 1.01f, 0.5f, 0.25f };
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *pNPC = ppAIs[i];
		if ( !pNPC || !pNPC->IsAlive() || pNPC->GetEntityName() == NULL_STRING )
			continue;
		const RPMNPCDef_t *pDef = FindNPCDef( pNPC->GetEntityName().ToCStr() );
		if ( !pDef || pDef->confidence < 0 || pDef->confidence > 2 )
			continue;
		CBaseEntity *pEnemy = pNPC->GetEnemy();
		if ( !pEnemy || !pEnemy->IsAlive() || pNPC->GetMaxHealth() <= 0 )
			continue;
		if ( pNPC->GetHealth() >= pNPC->GetMaxHealth() * s_flFleeBelow[pDef->confidence] )
			continue;
		if ( pNPC->IRelationType( pEnemy ) != D_FR )
		{
			pNPC->AddEntityRelationship( pEnemy, D_FR, 100 );
			if ( rpm_ai_debug.GetBool() )
				Msg( "RPMWorld: %s (confidence %d, health %d/%d) flees from %s\n", pNPC->GetDebugName(), pDef->confidence,
					pNPC->GetHealth(), pNPC->GetMaxHealth(), pEnemy->GetDebugName() );
		}
		// the generic AI does not always run from a feared enemy by itself: standing near it - run
		// (not while it moves: classes translate the schedule to their own, restarting it stops the run)
		if ( ( pEnemy->GetAbsOrigin() - pNPC->GetAbsOrigin() ).LengthSqr() < 1000.0f * 1000.0f && !pNPC->IsMoving() &&
			pNPC->GetState() != NPC_STATE_SCRIPT )
			pNPC->SetSchedule( SCHED_RUN_FROM_ENEMY );
	}
}

//-----------------------------------------------------------------------------
// Locations with a soundscape for the night (21-6 h) and / or the rain: the
// triggerable soundscape of the zone switches its scene; a player inside hears
// the new one at once (his audio params point at this soundscape)
//-----------------------------------------------------------------------------
void CRPMWorldSystem::UpdateZoneSoundscapes()
{
	const char *pszWeather = RPMLogic::CurrentWeatherName();
	const bool bRain = !Q_stricmp( pszWeather, "drizzle" ) || !Q_stricmp( pszWeather, "rain" ) || !Q_stricmp( pszWeather, "thunderstorm" );
	const float h = RPMWorld::GetHours();
	const bool bNight = h >= 21.0f || h < 6.0f;
	for ( int i = 0; i < m_Zones.Count(); i++ )
	{
		KeyValues *pRef = m_Zones[i].pRef;
		if ( !pRef || !pRef->GetString( "soundscape" )[0] )
			continue;
		const char *pszNight = pRef->GetString( "soundscape_night" ), *pszRain = pRef->GetString( "soundscape_rain" );
		if ( !pszNight[0] && !pszRain[0] )
			continue;
		const char *pszWant = ( bRain && pszRain[0] ) ? pszRain : ( bNight && pszNight[0] ) ? pszNight : pRef->GetString( "soundscape" );
		CEnvSoundscape *pScape = dynamic_cast<CEnvSoundscape *>( gEntList.FindEntityByName( NULL, CFmtStr( "rpmss_%s", pRef->GetName() ) ) );
		if ( !pScape || !Q_stricmp( STRING( pScape->m_soundscapeName ), pszWant ) )
			continue;
		pScape->m_soundscapeName = AllocPooledString( pszWant );
		pScape->m_soundscapeIndex = g_SoundscapeSystem.GetSoundscapeIndex( pszWant );
		if ( rpm_audio_debug.GetBool() )
			Msg( "RPMWorld: zone %s plays soundscape '%s' (index %d)\n", m_Zones[i].name.Get(), pszWant, pScape->m_soundscapeIndex );
		for ( int p = 1; p <= gpGlobals->maxClients; p++ )
		{
			CBasePlayer *pPlayer = UTIL_PlayerByIndex( p );
			if ( pPlayer && pPlayer->m_hTriggerSoundscapeList.Count() && pPlayer->m_hTriggerSoundscapeList[0].Get() == pScape )
				pScape->WriteAudioParamsTo( pPlayer->GetAudioParams() );
		}
	}
}

//-----------------------------------------------------------------------------
// Queries for the routine behavior
//-----------------------------------------------------------------------------
float RPMWorld::GetHours()
{
	float h = 12.0f;
	if ( !WorldClock_GetHours( h ) )
		h = 12.0f;
	return h;
}

bool RPMWorld::IsPackageTime( const RPMPackage_t &pkg, float flHours )
{
	if ( pkg.duration >= 24.0f )
		return true;
	float t = fmodf( flHours - pkg.start + 48.0f, 24.0f );
	return t < pkg.duration;
}

bool RPMWorld::PackageConditionsMet( const RPMPackage_t &pkg, CBaseEntity *pNPC )
{
	if ( pkg.pConds )
	{
		RPMLogic::Ctx_t ctx;
		ctx.pSelf = pNPC;
		if ( !RPMLogic::Check( pkg.pConds, ctx ) )
			return false;
	}
	if ( pkg.reqQuestStateId.IsEmpty() && pkg.reqQuestStageId.IsEmpty() )
		return true;
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( !pPlayer )
		return false;
	if ( !pkg.reqQuestStateId.IsEmpty() && pkg.reqQuestState != INT_MIN )
	{
		const PlayerQuestState *st = QuestSystem().FindPlayerQuest( pPlayer, pkg.reqQuestStateId );
		const int state = st ? (int)st->state : (int)kQuestState_Inactive;
		if ( state != pkg.reqQuestState )
			return false;
	}
	if ( !pkg.reqQuestStageId.IsEmpty() && pkg.reqQuestStage != INT_MIN )
	{
		const PlayerQuestState *st = QuestSystem().FindPlayerQuest( pPlayer, pkg.reqQuestStageId );
		if ( !st || st->state != kQuestState_Active || st->stage < pkg.reqQuestStage )
			return false;
	}
	return true;
}

bool RPMWorld::MarkerMatchesKinds( const RPMMarker_t *pMarker, const char *pszKinds )
{
	if ( !pMarker )
		return false;
	if ( !pszKinds || !pszKinds[0] )
		return true;
	const int len = pMarker->kind.Length();
	for ( const char *p = pszKinds; *p; )
	{
		const char *pEnd = strchr( p, ';' );
		const int n = pEnd ? pEnd - p : Q_strlen( p );
		if ( n == len && !Q_strnicmp( p, pMarker->kind, n ) )
			return true;
		if ( !pEnd )
			break;
		p = pEnd + 1;
	}
	return false;
}

CAI_Hint *RPMWorld::FindFreeMarker( CAI_BaseNPC *pNPC, const char *pszKinds, const Vector &center, float flRadius, bool bRandom )
{
	const char *pszMe = pNPC->GetEntityName().ToCStr();
	CAI_Hint *pOwned = NULL;
	float flOwnedDist = FLT_MAX;
	CUtlVector<CAI_Hint *> candidates;
	CAI_Hint *pNearest = NULL;
	float flNearest = FLT_MAX;
	const float flRadiusSqr = ( flRadius > 0 ) ? flRadius * flRadius : FLT_MAX;

	AIHintIter_t iter;
	for ( CAI_Hint *pHint = CAI_HintManager::GetFirstHint( &iter ); pHint; pHint = CAI_HintManager::GetNextHint( &iter ) )
	{
		const RPMMarker_t *pMarker = g_RPMWorld.FindMarker( pHint->GetEntityName().ToCStr() );
		if ( !pMarker || !MarkerMatchesKinds( pMarker, pszKinds ) )
			continue;
		if ( pHint->IsDisabled() )
			continue;
		if ( pHint->IsLocked() && !pHint->IsLockedBy( pNPC ) )
			continue;
		const bool bMine = !pMarker->owner.IsEmpty() && !Q_stricmp( pMarker->owner, pszMe );
		if ( !pMarker->owner.IsEmpty() && !bMine )
			continue;
		const float flDistSqr = ( pHint->GetAbsOrigin() - center ).LengthSqr();
		if ( bMine )
		{
			// the NPC's own bed/chair: wherever it is
			if ( flDistSqr < flOwnedDist )
			{
				flOwnedDist = flDistSqr;
				pOwned = pHint;
			}
			continue;
		}
		if ( flDistSqr > flRadiusSqr )
			continue;
		candidates.AddToTail( pHint );
		if ( flDistSqr < flNearest )
		{
			flNearest = flDistSqr;
			pNearest = pHint;
		}
	}
	if ( pOwned )
		return pOwned;
	if ( !candidates.Count() )
		return NULL;
	if ( bRandom )
		return candidates[random->RandomInt( 0, candidates.Count() - 1 )];
	return pNearest;
}

void RPMWorld::NoteDialogActivity( CBaseEntity *pNPC )
{
	if ( !pNPC )
		return;
	CUtlVector<CRPMWorldSystem::DialogNote_t> &notes = g_RPMWorld.m_DialogNotes;
	for ( int i = notes.Count() - 1; i >= 0; i-- )
	{
		if ( notes[i].hEnt == pNPC || !notes[i].hEnt )
			notes.FastRemove( i );
	}
	CRPMWorldSystem::DialogNote_t note;
	note.hEnt = pNPC;
	note.flTime = gpGlobals->curtime;
	notes.AddToTail( note );
}

bool RPMWorld::IsInDialog( CBaseEntity *pNPC )
{
	CUtlVector<CRPMWorldSystem::DialogNote_t> &notes = g_RPMWorld.m_DialogNotes;
	for ( int i = 0; i < notes.Count(); i++ )
	{
		if ( notes[i].hEnt != pNPC )
			continue;
		const float flAge = gpGlobals->curtime - notes[i].flTime;
		if ( flAge < 0 || flAge < 8.0f )
			return true;
		// the window is open as long as the player reads it, standing close
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		return flAge < 90.0f && pPlayer && ( pPlayer->GetAbsOrigin() - pNPC->GetAbsOrigin() ).LengthSqr() < 200.0f * 200.0f;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Commands
//-----------------------------------------------------------------------------
CON_COMMAND( rpm_world_reload, "HL2RPM Creation Kit: reload NPC definitions, factions and the map layer; 'rpm_world_reload 1' also removes the layer's entities and spawns them again" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	g_RPMWorld.LoadDefinitions();
	if ( args.ArgC() > 1 && atoi( args[1] ) != 0 )
	{
		int n = g_RPMWorld.RemoveLayerEntities();
		Msg( "RPMWorld: removed %d layer entities\n", n );
		s_bTransitionsSpawned = false;
		g_RPMWorld.SpawnLayer();
	}
	g_RPMWorld.ApplyRelations( true );	// after the spawn: the new NPCs get theirs at once
	Msg( "RPMWorld: reloaded (%d NPC definitions, %d markers, %d paths)\n", g_RPMWorld.m_NPCDefs.Count(), g_RPMWorld.m_Markers.Count(), g_RPMWorld.m_Paths.Count() );
}

extern void RPMRoutine_DumpNPC( CAI_BaseNPC *pNPC );

CON_COMMAND( rpm_world_dump, "HL2RPM Creation Kit: NPC definitions and what every NPC with a routine is doing" )
{
	Msg( "RPMWorld: map %s, time %.2f h, %d NPC definitions, %d markers, %d paths, %d factions, nav %d nodes\n",
		STRING( gpGlobals->mapname ), RPMWorld::GetHours(), g_RPMWorld.m_NPCDefs.Count(), g_RPMWorld.m_Markers.Count(),
		g_RPMWorld.m_Paths.Count(), g_RPMWorld.m_Factions.Count(), g_RPMWorld.m_Nav.nodes.Count() );
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		if ( ppAIs[i] )
			RPMRoutine_DumpNPC( ppAIs[i] );
	}
}

//-----------------------------------------------------------------------------
// rpm_nav_generate [spacing] [max nodes] [output file]: AI nodes on a grid over
// everything the NPC hull can walk to from the player, the NPCs and the player
// starts (real collision: brushes, displacements, props). Written as
// scripts/world/<map>_nav.txt; the node graph is rebuilt on the next map load.
//-----------------------------------------------------------------------------
static bool NavFindGround( const Vector &above, float flDrop, const Vector &mins, const Vector &maxs, Vector &ground )
{
	trace_t tr;
	UTIL_TraceHull( above, above - Vector( 0, 0, flDrop ), mins, maxs, MASK_NPCSOLID, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.startsolid || tr.fraction >= 1.0f )
		return false;
	if ( tr.plane.normal.z < 0.7f )
		return false;
	ground = tr.endpos;
	return true;
}

// walk from a to b in short steps (step height 18, drops up to 40 per step)
static bool NavWalk( const Vector &a, const Vector &bxy, const Vector &mins, const Vector &maxs, Vector &end )
{
	Vector cur = a;
	Vector delta = bxy - a;
	delta.z = 0;
	const float flLen = delta.Length2D();
	if ( flLen < 1 )
		return false;
	const int nSteps = MAX( 1, (int)ceilf( flLen / 24.0f ) );
	const Vector step = delta / nSteps;
	trace_t tr;
	for ( int i = 0; i < nSteps; i++ )
	{
		const Vector up = cur + Vector( 0, 0, 18 );
		UTIL_TraceHull( up, up + step, mins, maxs, MASK_NPCSOLID, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.startsolid || tr.fraction < 1.0f )
			return false;
		Vector ground;
		if ( !NavFindGround( tr.endpos, 18 + 40, mins, maxs, ground ) )
			return false;
		cur = ground;
	}
	end = cur;
	return true;
}

CON_COMMAND( rpm_nav_generate, "HL2RPM: generate AI nodes for this map: rpm_nav_generate [spacing 128] [max nodes 4000] [output file]" )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;
	const float flSpacing = clamp( ( args.ArgC() > 1 ) ? (float)atof( args[1] ) : 128.0f, 48.0f, 512.0f );
	const int nMax = clamp( ( args.ArgC() > 2 ) ? atoi( args[2] ) : 4000, 16, MAX_NODES - 64 );
	char szOut[MAX_PATH];
	if ( args.ArgC() > 3 )
		Q_strncpy( szOut, args[3], sizeof( szOut ) );
	else
		NavPaths( szOut, sizeof( szOut ), NULL, 0, NULL, 0 );

	const Vector mins = NAI_Hull::Mins( HULL_HUMAN );
	const Vector maxs = NAI_Hull::Maxs( HULL_HUMAN );
	const float flStartTime = Plat_FloatTime();

	CUtlVector<Vector> seeds;
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( pPlayer )
		seeds.AddToTail( pPlayer->GetAbsOrigin() );
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *pNPC = g_AI_Manager.AccessAIs()[i];
		if ( pNPC && pNPC->GetMoveType() == MOVETYPE_STEP )
			seeds.AddToTail( pNPC->GetAbsOrigin() );
	}
	for ( CBaseEntity *pStart = gEntList.FindEntityByClassname( NULL, "info_player_start" ); pStart; pStart = gEntList.FindEntityByClassname( pStart, "info_player_start" ) )
		seeds.AddToTail( pStart->GetAbsOrigin() );
	for ( CBaseEntity *pHint = gEntList.FindEntityByClassname( NULL, "ai_hint" ); pHint; pHint = gEntList.FindEntityByClassname( pHint, "ai_hint" ) )
		seeds.AddToTail( pHint->GetAbsOrigin() );

	// cell key: grid x, grid y, floor (64 units)
	CUtlMap<uint64, int, int> visited( DefLessFunc( uint64 ) );	// (int indices: more than 65535 cells on a big map)
	CUtlVector<Vector> nodes;
	CUtlVector<int> queue;
	auto Key = [flSpacing]( const Vector &p ) -> uint64
	{
		const int64 ix = (int64)floorf( p.x / flSpacing + 0.5f ) + 0x8000;
		const int64 iy = (int64)floorf( p.y / flSpacing + 0.5f ) + 0x8000;
		const int64 iz = (int64)floorf( p.z / 64.0f ) + 0x8000;
		return ( (uint64)ix << 40 ) | ( (uint64)iy << 20 ) | (uint64)iz;
	};
	auto InWater = []( const Vector &p ) -> bool
	{
		return ( enginetrace->GetPointContents( p + Vector( 0, 0, 24 ) ) & MASK_WATER ) != 0;
	};

	for ( int i = 0; i < seeds.Count() && nodes.Count() < nMax; i++ )
	{
		Vector ground;
		if ( !NavFindGround( seeds[i] + Vector( 0, 0, 24 ), 160, mins, maxs, ground ) )
			continue;
		const Vector start = ground;
		const uint64 key = Key( start );
		if ( visited.Find( key ) != visited.InvalidIndex() || InWater( start ) )
			continue;
		visited.Insert( key, nodes.Count() );
		queue.AddToTail( nodes.AddToTail( start ) );
	}

	static const int s_Dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	int nHead = 0;
	while ( nHead < queue.Count() && nodes.Count() < nMax )
	{
		const Vector from = nodes[queue[nHead++]];
		for ( int d = 0; d < 8 && nodes.Count() < nMax; d++ )
		{
			const Vector target( from.x + s_Dirs[d][0] * flSpacing, from.y + s_Dirs[d][1] * flSpacing, from.z );
			Vector end;
			if ( !NavWalk( from, target, mins, maxs, end ) )
				continue;
			const uint64 key = Key( end );
			if ( visited.Find( key ) != visited.InvalidIndex() )
				continue;
			visited.Insert( key, nodes.Count() );
			if ( InWater( end ) )
				continue;
			queue.AddToTail( nodes.AddToTail( end ) );
		}
	}

	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	buf.Printf( "// HL2RPM nav nodes: %s, spacing %.0f, %d nodes (rpm_nav_generate)\n", STRING( gpGlobals->mapname ), flSpacing, nodes.Count() );
	for ( int i = 0; i < nodes.Count(); i++ )
		buf.Printf( "%.1f %.1f %.1f\n", nodes[i].x, nodes[i].y, nodes[i].z + 1.0f );
	char szDir[MAX_PATH];
	Q_strncpy( szDir, szOut, sizeof( szDir ) );
	Q_StripFilename( szDir );
	if ( szDir[0] )
		filesystem->CreateDirHierarchy( szDir, "DEFAULT_WRITE_PATH" );
	const bool bOk = filesystem->WriteFile( szOut, "DEFAULT_WRITE_PATH", buf );
	Msg( "RPMWorld: rpm_nav_generate - %d nodes (spacing %.0f, %d seeds, %.1f s)%s -> %s %s\n", nodes.Count(), flSpacing, seeds.Count(),
		Plat_FloatTime() - flStartTime, nodes.Count() >= nMax ? " - reached the limit" : "", szOut, bOk ? "written" : "NOT WRITTEN" );
	if ( bOk )
		Msg( "RPMWorld: reload the map to build the node graph from them\n" );
}

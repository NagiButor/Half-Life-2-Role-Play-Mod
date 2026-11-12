//========= HL2RPM ============================================================//
//
// Shadow maps of spot lamps kept between frames - see deferred_shadowcache.h.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_shadowcache.h"
#include "clientleafsystem.h"
#include "c_baseanimating.h"
#include "model_types.h"
#include "materialsystem/imaterialvar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar r_deferred_shadow_cache( "r_deferred_shadow_cache", "1", FCVAR_NONE,
	"HL2RPM: shadow maps of spot lamps are kept while nothing in their cone changes (0 = drawn every frame)" );
static ConVar r_deferred_shadow_cache_debug( "r_deferred_shadow_cache_debug", "0", FCVAR_NONE,
	"HL2RPM: once a second - spot shadows taken from the cache / drawn, and why they were drawn" );
static ConVar r_deferred_shadow_cache_maxage( "r_deferred_shadow_cache_maxage", "0", FCVAR_NONE,
	"HL2RPM: seconds after which a kept spot shadow is drawn again anyway (0 = only when something changes)" );

// r_deferred_treesway lives in the shader DLL (hl2rpm_pbr.cpp): looked up when first used
static int TreeswayMode()
{
	static ConVarRef r_deferred_treesway( "r_deferred_treesway" );
	return r_deferred_treesway.IsValid() ? r_deferred_treesway.GetInt() : 1;
}

struct ShadowCacheEntry_t
{
	def_light_t *pLight;
	Vector vecPos;
	QAngle angDir;
	float flRadius;
	float flFov;
	int iRes;
	int iExclude;
	unsigned int uCasters;
	unsigned int uSettings;
	int iEpoch;
	int iLastUsedFrame;
	float flDrawnTime;
	bool bValid;
};
static ShadowCacheEntry_t s_Entries[ MAX_SHADOW_CACHE_PROJ ];
static int s_iEpoch = 1;

// statistics for r_deferred_shadow_cache_debug
static int s_nStatKept, s_nStatNew, s_nStatLight, s_nStatCasters, s_nStatDynamic, s_nStatSettings, s_nStatAge, s_nStatNoEntry;
static float s_flNextStat = 0.0f;

//-----------------------------------------------------------------------------
static inline unsigned int Mix( unsigned int h, unsigned int v )
{
	h ^= v;
	h *= 16777619u;
	return h ^ ( h >> 15 );
}

static inline unsigned int MixFloat( unsigned int h, float f )
{
	unsigned int u;
	memcpy( &u, &f, sizeof( u ) );
	return Mix( h, u );
}

// does the model have swaying materials (the shadow VS moves them every frame: TREESWAY)
static CUtlMap< const model_t *, bool > s_SwayModels( DefLessFunc( const model_t * ) );

static bool ModelSways( const model_t *pModel )
{
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
		return false;
	const unsigned short idx = s_SwayModels.Find( pModel );
	if ( idx != s_SwayModels.InvalidIndex() )
		return s_SwayModels[ idx ];

	bool bSways = false;
	const int nMaterials = modelinfo->GetModelMaterialCount( pModel );
	if ( nMaterials > 0 )
	{
		CUtlVector< IMaterial * > materials;
		materials.SetCount( nMaterials );
		modelinfo->GetModelMaterials( pModel, nMaterials, materials.Base() );
		for ( int i = 0; i < nMaterials && !bSways; i++ )
		{
			IMaterial *pMat = materials[ i ];
			if ( !pMat || pMat->IsErrorMaterial() )
				continue;
			bool bFound = false;
			IMaterialVar *pVar = pMat->FindVar( "$treesway", &bFound, false );
			if ( bFound && pVar && pVar->GetIntValue() > 0 )
				bSways = true;
		}
	}
	s_SwayModels.Insert( pModel, bSways );
	return bSways;
}

// what the shadow map would show: the renderables collected for the view and their state.
// bDynamic: something in it moves every frame by itself (animated characters, swaying trees)
static unsigned int CastersSignature( const CClientRenderablesList *pList, bool &bDynamic )
{
	bDynamic = false;
	unsigned int h = 2166136261u;
	if ( !pList )
		return h;
	const bool bSway = TreeswayMode() != 0;

	for ( int g = 0; g < RENDER_GROUP_COUNT; g++ )
	{
		const int nCount = pList->m_RenderGroupCounts[ g ];
		const CClientRenderablesList::CEntry *pEntries = pList->m_RenderGroups[ g ];
		for ( int i = 0; i < nCount; i++ )
		{
			IClientRenderable *pRenderable = pEntries[ i ].m_pRenderable;
			if ( !pRenderable )
				continue;	// (the place of a leaf in the list)

			h = Mix( h, (unsigned int)(uintp)pRenderable );
			h = Mix( h, (unsigned int)g );
			const Vector &vecOrigin = pRenderable->GetRenderOrigin();
			const QAngle &angles = pRenderable->GetRenderAngles();
			h = MixFloat( h, vecOrigin.x ); h = MixFloat( h, vecOrigin.y ); h = MixFloat( h, vecOrigin.z );
			h = MixFloat( h, angles.x ); h = MixFloat( h, angles.y ); h = MixFloat( h, angles.z );
			const model_t *pModel = pRenderable->GetModel();
			h = Mix( h, (unsigned int)(uintp)pModel );
			h = Mix( h, (unsigned int)pRenderable->GetFxBlend() );

			if ( bSway && ModelSways( pModel ) )
				bDynamic = true;

			IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
			C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
			if ( !pEntity )
				continue;
			if ( pEntity->IsNPC() || pEntity->IsPlayer() )
				bDynamic = true;
			h = Mix( h, (unsigned int)pEntity->GetEffects() );

			C_BaseAnimating *pAnimating = pEntity->GetBaseAnimating();
			if ( !pAnimating )
				continue;
			if ( pAnimating->IsRagdoll() )
				bDynamic = true;
			h = Mix( h, (unsigned int)pAnimating->GetSequence() );
			h = MixFloat( h, pAnimating->GetCycle() );
			h = Mix( h, (unsigned int)pAnimating->GetSkin() );
			h = Mix( h, (unsigned int)pAnimating->GetBody() );
			CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();
			const int nPose = pStudioHdr ? Min( pStudioHdr->GetNumPoseParameters(), (int)MAXSTUDIOPOSEPARAM ) : 0;
			for ( int k = 0; k < nPose; k++ )
				h = MixFloat( h, pAnimating->GetPoseParameter( k ) );
		}
	}
	return h;
}

// the settings of the shadow passes that change what a map holds
static unsigned int SettingsSignature()
{
	static int s_iFrame = -1;
	static unsigned int s_uHash = 0;
	if ( s_iFrame == gpGlobals->framecount )
		return s_uHash;
	s_iFrame = gpGlobals->framecount;

	static ConVarRef r_deferred_shadow_proj_slope_bias( "r_deferred_shadow_proj_slope_bias" );
	static ConVarRef r_deferred_shadow_proj_depth_bias( "r_deferred_shadow_proj_depth_bias" );
	static ConVarRef r_deferred_shadow_worldmesh( "r_deferred_shadow_worldmesh" );
	static ConVarRef r_drawstaticprops( "r_drawstaticprops" );
	static ConVarRef r_drawentities( "r_drawentities" );

	unsigned int h = 2166136261u;
	if ( r_deferred_shadow_proj_slope_bias.IsValid() ) h = MixFloat( h, r_deferred_shadow_proj_slope_bias.GetFloat() );
	if ( r_deferred_shadow_proj_depth_bias.IsValid() ) h = MixFloat( h, r_deferred_shadow_proj_depth_bias.GetFloat() );
	if ( r_deferred_shadow_worldmesh.IsValid() ) h = Mix( h, (unsigned int)r_deferred_shadow_worldmesh.GetInt() );
	if ( r_drawstaticprops.IsValid() ) h = Mix( h, (unsigned int)r_drawstaticprops.GetInt() );
	if ( r_drawentities.IsValid() ) h = Mix( h, (unsigned int)r_drawentities.GetInt() );
	h = Mix( h, (unsigned int)TreeswayMode() );
	s_uHash = h;
	return h;
}

static void PrintStats()
{
	if ( !r_deferred_shadow_cache_debug.GetBool() || gpGlobals->realtime < s_flNextStat )
		return;
	s_flNextStat = gpGlobals->realtime + 1.0f;
	int nEntries = 0;
	for ( int i = 0; i < GetNumShadowCacheRT_Proj(); i++ )
	{
		if ( s_Entries[ i ].bValid )
			nEntries++;
	}
	Msg( "[shadowcache] 1 s: kept %d, drawn: new %d, lamp changed %d, casters changed %d, moving casters %d, settings %d, age %d; no entry %d; %d/%d maps hold a lamp\n",
		s_nStatKept, s_nStatNew, s_nStatLight, s_nStatCasters, s_nStatDynamic, s_nStatSettings, s_nStatAge, s_nStatNoEntry,
		nEntries, GetNumShadowCacheRT_Proj() );
	s_nStatKept = s_nStatNew = s_nStatLight = s_nStatCasters = s_nStatDynamic = s_nStatSettings = s_nStatAge = s_nStatNoEntry = 0;
}

//-----------------------------------------------------------------------------
int DeferredShadowCache_Acquire( def_light_t *pLight )
{
	PrintStats();

	const int nEntries = Min( GetNumShadowCacheRT_Proj(), (int)MAX_SHADOW_CACHE_PROJ );
	if ( !r_deferred_shadow_cache.GetBool() || nEntries <= 0 || !pLight )
		return -1;

	const int iFrame = gpGlobals->framecount;
	int iFree = -1;
	int iOldest = INT_MAX;
	for ( int i = 0; i < nEntries; i++ )
	{
		ShadowCacheEntry_t &e = s_Entries[ i ];
		if ( e.pLight == pLight )
		{
			e.iLastUsedFrame = iFrame;
			return i;
		}
		// (an entry used this frame holds the map of another lamp drawn now)
		if ( e.iLastUsedFrame != iFrame && e.iLastUsedFrame < iOldest )
		{
			iOldest = e.iLastUsedFrame;
			iFree = i;
		}
	}
	if ( iFree < 0 )
	{
		s_nStatNoEntry++;
		return -1;
	}

	ShadowCacheEntry_t &e = s_Entries[ iFree ];
	e.pLight = pLight;
	e.bValid = false;
	e.iLastUsedFrame = iFrame;
	return iFree;
}

ITexture *DeferredShadowCache_GetTexture( int iEntry )
{
	return GetShadowCacheRT_Proj( iEntry );
}

bool DeferredShadowCache_IsCurrent( int iEntry, def_light_t *pLight, int iResolution, const CClientRenderablesList *pList )
{
	if ( iEntry < 0 || iEntry >= MAX_SHADOW_CACHE_PROJ )
		return false;
	ShadowCacheEntry_t &e = s_Entries[ iEntry ];

	bool bDynamic = false;
	const unsigned int uCasters = CastersSignature( pList, bDynamic );
	const unsigned int uSettings = SettingsSignature();
	const float flFov = pLight->GetFOV();
	const float flMaxAge = r_deferred_shadow_cache_maxage.GetFloat();

	bool bCurrent = false;
	if ( !e.bValid || e.iEpoch != s_iEpoch )
		s_nStatNew++;
	else if ( e.vecPos != pLight->pos || e.angDir != pLight->ang || e.flRadius != pLight->flRadius || e.flFov != flFov
		|| e.iRes != iResolution || e.iExclude != pLight->iShadowExcludeEntIndex )
		s_nStatLight++;
	else if ( bDynamic )
		s_nStatDynamic++;
	else if ( e.uSettings != uSettings )
		s_nStatSettings++;
	else if ( e.uCasters != uCasters )
		s_nStatCasters++;
	else if ( flMaxAge > 0.0f && gpGlobals->realtime - e.flDrawnTime > flMaxAge )
		s_nStatAge++;
	else
		bCurrent = true;

	if ( bCurrent )
	{
		s_nStatKept++;
		return true;
	}

	// drawn now: remember what it shows
	e.vecPos = pLight->pos;
	e.angDir = pLight->ang;
	e.flRadius = pLight->flRadius;
	e.flFov = flFov;
	e.iRes = iResolution;
	e.iExclude = pLight->iShadowExcludeEntIndex;
	e.uCasters = uCasters;
	e.uSettings = uSettings;
	e.iEpoch = s_iEpoch;
	e.flDrawnTime = gpGlobals->realtime;
	e.bValid = true;
	return false;
}

void DeferredShadowCache_InvalidateAll()
{
	s_iEpoch++;
	for ( int i = 0; i < MAX_SHADOW_CACHE_PROJ; i++ )
	{
		s_Entries[ i ].bValid = false;
		s_Entries[ i ].pLight = NULL;
		s_Entries[ i ].iLastUsedFrame = -1000;
	}
	s_SwayModels.RemoveAll();
}

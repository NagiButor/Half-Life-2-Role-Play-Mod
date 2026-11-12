//========= HL2RPM ============================================================//
//
// Purpose: What of the world each point light reaches ("light reach maps").
//
// Only the nearest lamps get shadow maps (the shadow budget, the bright converted
// lamps); every other point light, the fill (ambient) of every lamp and the bounce
// lights (deferred_bounce.cpp) lit through walls: a lamp behind a thick wall lit
// its far side and the street in front of it. A shadow map for each would cost
// milliseconds a light; the world does not move, though. So each point light gets
// a small cube of free distances, traced once against the world (BSP brushes,
// displacements - not props, not brush entities: doors and props are the shadow
// maps' business): LIGHTREACH_N x LIGHTREACH_N directions per face. The point
// light pass (lightingpass_point_ps30, LightReach) reads the distance in the
// direction of the pixel and drops the light behind the first wall.
//
// The rays also tell where the light gets to at all: the box of their ends is
// the box the light is culled by (def_light_t::SetReachBounds) - tighter than its
// radius in a room, and unlike the box of the 14 traces it was culled by before,
// it holds the light that falls between them.
//
// Slots of an atlas (LIGHTREACH_SLOTS_X x LIGHTREACH_SLOTS_Y; layout in
// deferred_global_common.h), traced on the main thread within a budget a frame
// (everything around the camera in the first frames of a map), written into the
// atlas as points like the GI probes. A light that moves gets no map until it
// stays; a new map eases in (the shader parameter carries the fade).
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_lightreach.h"
#include "deferred/deferred_dlight_bridge.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"
#include "debugoverlay_shared.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_lightreach( "r_deferred_lightreach", "1", 0,
	"Point lights stop at the walls of the world: a cube of traced free distances per light (no light, fill or bounce through walls)" );
static ConVar r_deferred_lightreach_budget_ms( "r_deferred_lightreach_budget_ms", "1.5", 0, "Light reach: ms of traces a frame" );
static ConVar r_deferred_lightreach_load_ms( "r_deferred_lightreach_load_ms", "1500", 0, "Light reach: ms of traces a frame in the first seconds of a map" );
static ConVar r_deferred_lightreach_range( "r_deferred_lightreach_range", "2500", 0, "Light reach: lights farther than their radius + this from the camera wait" );
static ConVar r_deferred_lightreach_fade( "r_deferred_lightreach_fade", "0.4", 0, "Light reach: seconds a new map eases in" );
static ConVar r_deferred_lightreach_debug( "r_deferred_lightreach_debug", "0", 0,
	"Light reach: 1 = stats once a second, 2 = and the reach box of the lights around the camera" );

#define REACH_TEXELS	( 6 * LIGHTREACH_N * LIGHTREACH_N )
#define REACH_SLOTS		( LIGHTREACH_SLOTS_X * LIGHTREACH_SLOTS_Y )

struct ReachSlot_t
{
	const def_light_t *pLight;	// compared, never followed (the light may be gone)
	Vector vecPos;
	float flRadius;
	int nDone;					// rays traced
	int nSolid;					// started in solid
	bool bReady;
	bool bUploaded;
	bool bUseless;				// the light sits in a wall: no map
	bool bBoundsGiven;			// the light got the box of its reach
	Vector vecMins, vecMaxs;	// the ends of the rays
	int iLastUsed;				// frame
	float flDist[ REACH_TEXELS ];
};

static ReachSlot_t s_Slots[ REACH_SLOTS ];
static Vector s_vecDir[ REACH_TEXELS ];
static bool s_bDirsReady = false;
static CTextureReference g_tex_LightReach;
static IMaterial *s_pMatUpload = NULL;
static bool s_bClearAtlas = true;
static float s_flLevelStart = -1.0f;

// stats (debug 1)
static int s_nStatTraced = 0, s_nStatUploaded = 0;
static double s_flStatMs = 0.0;

void InitLightReachRTs()
{
	g_tex_LightReach.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_lightreach", LIGHTREACH_ATLAS_W, LIGHTREACH_ATLAS_H, RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET, 0 ) );
	DeferredLightReach_OnRTContentLost();
}

void DeferredLightReach_OnRTContentLost()
{
	s_bClearAtlas = true;
	for ( int i = 0; i < REACH_SLOTS; i++ )
		s_Slots[i].bUploaded = false;
}

static void ClearSlots()
{
	for ( int i = 0; i < REACH_SLOTS; i++ )
	{
		s_Slots[i].pLight = NULL;
		s_Slots[i].bReady = s_Slots[i].bUploaded = false;
		s_Slots[i].iLastUsed = -1000000;
	}
	s_bClearAtlas = true;
}

// The direction of each texel: face f (+x -x +y -y +z -z), column i, row j; the shader finds the
// texel of a direction the same way (the major axis, the other two over it, in -1..1)
static void InitDirs()
{
	for ( int f = 0; f < 6; f++ )
	{
		for ( int j = 0; j < LIGHTREACH_N; j++ )
		{
			for ( int i = 0; i < LIGHTREACH_N; i++ )
			{
				const float u = ( i + 0.5f ) / LIGHTREACH_N * 2.0f - 1.0f;
				const float v = ( j + 0.5f ) / LIGHTREACH_N * 2.0f - 1.0f;
				const float s = ( f & 1 ) ? -1.0f : 1.0f;
				Vector d;
				switch ( f >> 1 )
				{
				case 0: d.Init( s, u, v ); break;
				case 1: d.Init( u, s, v ); break;
				default: d.Init( u, v, s ); break;
				}
				VectorNormalize( d );
				s_vecDir[ ( f * LIGHTREACH_N + j ) * LIGHTREACH_N + i ] = d;
			}
		}
	}
	s_bDirsReady = true;
}

static bool EnsureMaterial()
{
	if ( !s_pMatUpload )
	{
		// (the GI probe upload: the texel gets the vertex data as is)
		s_pMatUpload = materials->CreateMaterial( "__hl2rpm_lightreach_upload", new KeyValues( "DEFERRED_GI_UPLOAD" ) );
		if ( s_pMatUpload )
			s_pMatUpload->IncrementReferenceCount();
	}
	return s_pMatUpload && !s_pMatUpload->IsErrorMaterial();
}

static bool IsEnabled()
{
	return r_deferred_lightreach.GetBool() && g_tex_LightReach.IsValid() && EnsureMaterial();
}

// Lights that get a map: point lights (the spot pass does not read it - spots have shadow maps);
// not the short ones (muzzle flashes, explosions) nor the engine's dlights
static bool WantsReach( def_light_t *l )
{
	if ( !l->IsPoint() || l->flRadius < 32.0f || !( l->iFlags & DEFLIGHT_ENABLED ) )
		return false;
	if ( GetLightingManager()->IsTempLight( l ) || DeferredDLights_IsMirrored( l ) )
		return false;
	return true;
}

static ReachSlot_t *SlotOf( const def_light_t *l )
{
	if ( l->iReachSlot < 0 || l->iReachSlot >= REACH_SLOTS )
		return NULL;
	ReachSlot_t &s = s_Slots[ l->iReachSlot ];
	if ( s.pLight != l || s.vecPos != l->pos || s.flRadius != l->flRadius )
		return NULL;
	return &s;
}

// A free slot, else the one unused the longest (not within the last 2 seconds: no thrashing)
static int AllocSlot( int iFrame )
{
	int iBest = -1, iBestFrame = INT_MAX;
	for ( int i = 0; i < REACH_SLOTS; i++ )
	{
		if ( !s_Slots[i].pLight )
			return i;
		if ( s_Slots[i].iLastUsed < iBestFrame )
		{
			iBestFrame = s_Slots[i].iLastUsed;
			iBest = i;
		}
	}
	const int iMinAge = (int)( 2.0f / Max( gpGlobals->frametime, 0.001f ) ) + 2;
	return ( iBest >= 0 && iFrame - iBestFrame > iMinAge ) ? iBest : -1;
}

static void TraceSlot( ReachSlot_t &s, double flDeadline )
{
	CTraceFilterWorldOnly filter;
	const float flLen = s.flRadius + 16.0f;
	while ( s.nDone < REACH_TEXELS )
	{
		const int nEnd = Min( s.nDone + 64, REACH_TEXELS );
		for ( ; s.nDone < nEnd; s.nDone++ )
		{
			const Vector &d = s_vecDir[ s.nDone ];
			Ray_t ray;
			ray.Init( s.vecPos, s.vecPos + d * flLen );
			trace_t tr;
			// (world brushes and displacements; glass and other see-through brushes let the light by)
			enginetrace->TraceRay( ray, MASK_OPAQUE, &filter, &tr );
			float flFree = flLen;
			if ( tr.startsolid )
			{
				flFree = 0.0f;
				s.nSolid++;
			}
			else if ( tr.fraction < 1.0f )
				flFree = tr.fraction * flLen;
			s.flDist[ s.nDone ] = flFree;
			const Vector vecEnd = s.vecPos + d * flFree;
			VectorMin( s.vecMins, vecEnd, s.vecMins );
			VectorMax( s.vecMaxs, vecEnd, s.vecMaxs );
		}
		s_nStatTraced += 64;
		if ( Plat_FloatTime() > flDeadline )
			return;
	}
	s.bReady = true;
	s.bUploaded = false;
	s.bUseless = s.nSolid * 2 > REACH_TEXELS;
	// (a margin for what stands a little off the walls the rays found: props, people)
	s.vecMins -= Vector( 24.0f, 24.0f, 24.0f );
	s.vecMaxs += Vector( 24.0f, 24.0f, 24.0f );
}

// (D3D9: pixel u's center is at screen x = u - see deferred_gi.cpp EmitTexel)
static inline void EmitPoint( CMeshBuilder &mb, int u, int v, float flValue )
{
	mb.Position3f( u * ( 2.0f / LIGHTREACH_ATLAS_W ) - 1.0f, 1.0f - v * ( 2.0f / LIGHTREACH_ATLAS_H ), 0.0f );
	mb.TexCoord4f( 0, flValue, flValue, flValue, 1.0f );
	mb.AdvanceVertex();
}

static void UploadSlots( int nMax )
{
	CMatRenderContextPtr pRenderContext( materials );
	if ( s_bClearAtlas )
	{
		pRenderContext->PushRenderTargetAndViewport( g_tex_LightReach, 0, 0, LIGHTREACH_ATLAS_W, LIGHTREACH_ATLAS_H );
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
		s_bClearAtlas = false;
	}

	int n = 0;
	for ( int i = 0; i < REACH_SLOTS && n < nMax; i++ )
	{
		ReachSlot_t &s = s_Slots[i];
		if ( !s.pLight || !s.bReady || s.bUploaded || s.bUseless )
			continue;
		if ( n == 0 )
		{
			pRenderContext->PushRenderTargetAndViewport( g_tex_LightReach, 0, 0, LIGHTREACH_ATLAS_W, LIGHTREACH_ATLAS_H );
			pRenderContext->Bind( s_pMatUpload );
		}
		const int iRow = i / LIGHTREACH_SLOTS_X, iCol = i % LIGHTREACH_SLOTS_X;
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder mb;
		mb.Begin( pMesh, MATERIAL_POINTS, REACH_TEXELS );
		for ( int t = 0; t < REACH_TEXELS; t++ )
		{
			const int ti = t % LIGHTREACH_N, tj = ( t / LIGHTREACH_N ) % LIGHTREACH_N, f = t / ( LIGHTREACH_N * LIGHTREACH_N );
			EmitPoint( mb, ( iCol * 6 + f ) * LIGHTREACH_N + ti, iRow * LIGHTREACH_N + tj, s.flDist[t] );
		}
		mb.End();
		pMesh->Draw();
		s.bUploaded = true;
		n++;
	}
	if ( n > 0 )
		pRenderContext->PopRenderTargetAndViewport();
	s_nStatUploaded += n;
}

struct ReachPending_t
{
	int iSlot;
	float flDist;
};

static int __cdecl ComparePending( const void *a, const void *b )
{
	const float x = ( (const ReachPending_t *)a )->flDist, y = ( (const ReachPending_t *)b )->flDist;
	return ( x < y ) ? -1 : ( x > y ) ? 1 : 0;
}

void DeferredLightReach_Update( const Vector &vecViewOrigin, CUtlVector< def_light_t* > &lights )
{
	static int s_iLastFrame = -1;
	const int iFrame = gpGlobals->framecount;
	if ( iFrame == s_iLastFrame )
		return;
	s_iLastFrame = iFrame;

	const bool bOn = IsEnabled();
	if ( !s_bDirsReady )
		InitDirs();
	if ( s_flLevelStart < 0.0f || gpGlobals->curtime < s_flLevelStart )
		s_flLevelStart = gpGlobals->curtime;

	const float flFadeStep = gpGlobals->frametime / Max( r_deferred_lightreach_fade.GetFloat(), 0.01f );
	const float flRange = r_deferred_lightreach_range.GetFloat();
	static CUtlVector< ReachPending_t > s_Pending;
	s_Pending.RemoveAll();

	for ( int i = 0; i < lights.Count(); i++ )
	{
		def_light_t *l = lights[i];
		l->flReachParam = -1.0f;
		if ( !bOn || !WantsReach( l ) )
			continue;

		// moved or changed: no map until it stays (a map is traced for one place and radius)
		if ( l->flReachTrackRadius < 0.0f )
		{
			// (first seen: a lamp of the map - at once)
			l->reachTrackPos = l->pos;
			l->flReachTrackRadius = l->flRadius;
			l->flReachStillSince = gpGlobals->curtime - 10.0f;
		}
		else if ( l->reachTrackPos != l->pos || l->flReachTrackRadius != l->flRadius )
		{
			l->reachTrackPos = l->pos;
			l->flReachTrackRadius = l->flRadius;
			l->flReachStillSince = gpGlobals->curtime;
			l->flReachFade = 0.0f;
			l->ClearReachBounds();
		}
		if ( gpGlobals->curtime - l->flReachStillSince < 0.3f )
			continue;

		const float flDist = l->pos.DistTo( vecViewOrigin );
		ReachSlot_t *s = SlotOf( l );
		if ( !s )
		{
			if ( flDist > l->flRadius + flRange )
				continue;
			const int iSlot = AllocSlot( iFrame );
			if ( iSlot < 0 )
				continue;
			s = &s_Slots[ iSlot ];
			s->pLight = l;
			s->vecPos = l->pos;
			s->flRadius = l->flRadius;
			s->nDone = s->nSolid = 0;
			s->bReady = s->bUploaded = s->bUseless = s->bBoundsGiven = false;
			s->vecMins = s->vecMaxs = l->pos;
			l->iReachSlot = iSlot;
			l->flReachFade = 0.0f;
		}
		s->iLastUsed = iFrame;

		if ( !s->bReady )
		{
			ReachPending_t &p = s_Pending[ s_Pending.AddToTail() ];
			p.iSlot = l->iReachSlot;
			p.flDist = flDist - l->flRadius;
			continue;
		}
		if ( s->bUseless )
			continue;
		if ( !s->bBoundsGiven )
		{
			l->SetReachBounds( s->vecMins, s->vecMaxs );
			s->bBoundsGiven = true;
		}
		if ( !s->bUploaded )
			continue;
		l->flReachFade = Min( 1.0f, l->flReachFade + flFadeStep );
		l->flReachParam = (float)l->iReachSlot + 0.5f * l->flReachFade;
	}

	if ( !bOn )
	{
		ITexture *pNone = NULL;
		QUEUE_FIRE( CommitTexture_LightReach, pNone );
		return;
	}

	// trace: the nearest first, within the budget (the first seconds of a map: all at once)
	const double flStart = Plat_FloatTime();
	if ( s_Pending.Count() )
	{
		qsort( s_Pending.Base(), s_Pending.Count(), sizeof( ReachPending_t ), ComparePending );
		const bool bLoading = gpGlobals->curtime - s_flLevelStart < 3.0f;
		const double flDeadline = flStart + 0.001 * ( bLoading ? r_deferred_lightreach_load_ms.GetFloat() : r_deferred_lightreach_budget_ms.GetFloat() );
		for ( int i = 0; i < s_Pending.Count() && Plat_FloatTime() < flDeadline; i++ )
			TraceSlot( s_Slots[ s_Pending[i].iSlot ], flDeadline );
	}
	UploadSlots( 32 );
	s_flStatMs += Plat_FloatTime() - flStart;

	ITexture *pReach = g_tex_LightReach;
	QUEUE_FIRE( CommitTexture_LightReach, pReach );

	const int nDebug = r_deferred_lightreach_debug.GetInt();
	if ( nDebug >= 1 )
	{
		static float s_flNext = 0.0f;
		if ( gpGlobals->realtime > s_flNext || gpGlobals->realtime < s_flNext - 2.0f )
		{
			s_flNext = gpGlobals->realtime + 1.0f;
			int nUsed = 0, nReady = 0, nUseless = 0;
			for ( int i = 0; i < REACH_SLOTS; i++ )
			{
				nUsed += s_Slots[i].pLight ? 1 : 0;
				nReady += ( s_Slots[i].pLight && s_Slots[i].bUploaded ) ? 1 : 0;
				nUseless += ( s_Slots[i].pLight && s_Slots[i].bUseless ) ? 1 : 0;
			}
			Msg( "[lightreach] slots %d used, %d in the atlas, %d in walls; %d waiting; last second: %d rays, %d uploaded, %.2f ms\n",
				nUsed, nReady, nUseless, s_Pending.Count(), s_nStatTraced, s_nStatUploaded, s_flStatMs * 1000.0 );
			s_nStatTraced = s_nStatUploaded = 0;
			s_flStatMs = 0.0;
		}
		if ( nDebug >= 2 )
		{
			for ( int i = 0; i < lights.Count(); i++ )
			{
				const def_light_t *l = lights[i];
				const ReachSlot_t *s = SlotOf( l );
				if ( s && s->bReady && l->pos.DistTo( vecViewOrigin ) < 1500.0f )
					debugoverlay->AddBoxOverlay( vec3_origin, s->vecMins, s->vecMaxs, vec3_angle, s->bUseless ? 255 : 40, 200, 255, 10, 0.0f );
			}
		}
	}
}

bool DeferredLightReach_IsReady( const def_light_t *l )
{
	if ( !r_deferred_lightreach.GetBool() || !g_tex_LightReach.IsValid() )
		return true;
	const ReachSlot_t *s = SlotOf( l );
	if ( !s )
		return false;	// (not traced yet; the caller does not wait forever - no room in the atlas)
	return s->bUseless || ( s->bReady && s->bUploaded );
}

CON_COMMAND( r_deferred_lightreach_reset, "Light reach: trace every light's map again" )
{
	ClearSlots();
}

class CLightReachLevelSystem : public CAutoGameSystem
{
public:
	CLightReachLevelSystem() : CAutoGameSystem( "CLightReachLevelSystem" ) {}

	virtual void LevelInitPreEntity() { ClearSlots(); s_flLevelStart = -1.0f; }
	virtual void LevelShutdownPostEntity() { ClearSlots(); }
	virtual void Shutdown()
	{
		if ( s_pMatUpload )
		{
			s_pMatUpload->DecrementReferenceCount();
			s_pMatUpload = NULL;
		}
	}
};
static CLightReachLevelSystem s_LightReachLevelSystem;

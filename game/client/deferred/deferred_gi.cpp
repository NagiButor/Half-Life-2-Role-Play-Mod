//========= HL2RPM ============================================================//
//
// Purpose: World-space indirect light of the global light ("GI probes").
//
// The sky light used to reach everything the top-down rain map didn't see under a
// roof, and under a roof it dropped to a flat fraction: a closed room was as grey
// as a porch, a window made no difference. Real interiors are lit through their
// openings: the sky light falls in through windows and doors and fades with the
// distance from them, and the sun on the floor by a window lights the walls and
// the ceiling (bounce light).
//
// A grid of probes around the camera (GI_NX x GI_NY x GI_NZ, r_deferred_gi_spacing
// apart; toroidal - a step of the camera only traces the new slice) shoots rays
// through the world (BSP + static props, enginetrace) in GI_RAYS directions:
//  - a ray that escapes to the sky adds to the probe's sky visibility,
//  - a ray that hits a surface keeps the point, its normal and its albedo (the
//    texture's reflectivity); a ray from there toward the sun tells whether the sun
//    lights it: sun light bounced toward the probe.
// Both are kept as L1 spherical harmonics (4 numbers each: the light a surface of any
// orientation at the probe gets), with the color of the bounce. Plus the free distance
// along the 6 axes: a probe beyond a wall must not light the room (leaks).
//
// The probes go to the GPU through a render target (one texel per probe and plane,
// written as tiny quads - a texture lock from the main thread would stall the render
// thread); DEFERRED_GI blends the 8 probes around every pixel at half resolution
// (dropping the ones behind the surface or out of its sight) into the indirect light
// the global light pass uses instead of its flat sky light.
//
// CPU: a time budget per frame (r_deferred_gi_budget_ms), nearest probes first. The
// sun's bounce is redone from the kept hits when the sun has moved
// (r_deferred_gi_sun_refresh degrees).
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_gi.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include "view.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"
#include "debugoverlay_shared.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_gi( "r_deferred_gi", "1", FCVAR_ARCHIVE,
	"World-space indirect light: sky light through windows and doors, sun light bounced off lit surfaces (probes traced around the camera)" );
static ConVar r_deferred_gi_budget_ms( "r_deferred_gi_budget_ms", "1.0", FCVAR_ARCHIVE,
	"GI probes: CPU time per frame for tracing (ms)", true, 0.1f, true, 50.0f );
static ConVar r_deferred_gi_load_budget_ms( "r_deferred_gi_load_budget_ms", "12", 0,
	"GI probes: CPU time per frame right after a map loads, until the probes near the camera are done (ms)", true, 0.1f, true, 100.0f );
static ConVar r_deferred_gi_bounce( "r_deferred_gi_bounce", "1.0", FCVAR_ARCHIVE,
	"GI: strength of the sun light bounced off lit surfaces", true, 0.0f, true, 4.0f );
static ConVar r_deferred_gi_bounce_indoor( "r_deferred_gi_bounce_indoor", "2.5", 0,
	"GI: sun bounce x this where no sky is in sight (rooms): the eye adapts to a dark room far more than the exposure", true, 1.0f, true, 8.0f );
static ConVar r_deferred_gi_skybounce( "r_deferred_gi_skybounce", "1.0", 0,
	"GI: strength of the sky light bounced off the surroundings (the ambient low color)", true, 0.0f, true, 4.0f );
static ConVar r_deferred_gi_fill( "r_deferred_gi_fill", "0.5", 0,
	"GI: multiple bounces - part of the average bounce light added from every direction", true, 0.0f, true, 2.0f );
static ConVar r_deferred_gi_min( "r_deferred_gi_min", "0.07", FCVAR_ARCHIVE,
	"GI: part of the sky light left where no sky is in sight - how dark a closed room gets (0..1)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_gi_normal_offset( "r_deferred_gi_normal_offset", "0.4", 0,
	"GI: probe lookup off the surface along its normal (x probe spacing)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_gi_leak_tolerance( "r_deferred_gi_leak_tolerance", "4", 0,
	"GI: how much farther than its free distance a probe may still light a point (units)", true, 0.0f, true, 64.0f );
static ConVar r_deferred_gi_sky_boost( "r_deferred_gi_sky_boost", "4", FCVAR_ARCHIVE,
	"GI: sky light by day x this - the GI occludes it, the maps' ambient was tuned for the flat sky light (a few % of the sun)", true, 1.0f, true, 16.0f );
static ConVar r_deferred_gi_props( "r_deferred_gi_props", "1", 0,
	"GI: static props block the probes' rays" );
static ConVar r_deferred_gi_sun_refresh( "r_deferred_gi_sun_refresh", "1.5", 0,
	"GI: redo a probe's sun bounce when the sun has moved this many degrees", true, 0.1f, true, 45.0f );
static ConVar r_deferred_gi_spacing( "r_deferred_gi_spacing", "64", 0,
	"GI: distance between the probes (units; applied by r_deferred_gi_reset / a new map)", true, 16.0f, true, 256.0f );
static ConVar r_deferred_gi_debug( "r_deferred_gi_debug", "0", FCVAR_CHEAT,
	"GI: 1 = ambient light only, 2 = + draw the probes near the camera, 3 = sky visibility, 4 = sun bounce, 5 = probe weights / coverage" );

enum
{
	GI_NX = 24,
	GI_NY = 24,
	GI_NZ = 10,
	GI_NUM_PROBES = GI_NX * GI_NY * GI_NZ,
	GI_RAYS = 32,
	GI_PLANES = 4,
	GI_ATLAS_W = GI_NX * GI_NZ,
	GI_ATLAS_H = GI_NY * GI_PLANES,
	GI_PROBES_PER_BATCH = 1024,
};

// probes sit off the grid lines (cell + 0.45): map geometry is aligned to powers of two, probes at
// multiples of 64 lay exactly on floors, ceilings and walls (half their rays started in the solid)
#define GI_OFFSET			0.45f

// (a ray that reaches nothing within this is open sky: long rays through open space cost the most)
#define GI_RAY_LENGTH		3072.0f
#define GI_SUN_RAY_LENGTH	16384.0f

// the camera sits this many probes above the bottom of the grid: most of what matters (rooms,
// roofs, the street) is around and above the eye, and below the ground probes are wasted
#define GI_CAMERA_LEVEL		2.5f

struct GIHit_t
{
	Vector vecPos;			// off the surface along its normal
	Vector vecNormal;
	Vector vecAlbedo;
	int iRay;
};

struct GIProbe_t
{
	int cell[3];			// world cell this slot holds
	bool bAssigned;
	bool bTraced;			// geometry rays done
	bool bSunValid;			// bounce done for vecSunDir
	bool bDirty;			// changed since the last upload

	float flValid;			// 0: in a wall / closed space
	float vis[4];			// sky visibility: vis( n ) = vis[0] + dot( vis[1..3], n )
	float bnc[4];			// sun bounce, the same way
	Vector vecChroma;		// color of the bounce (luminance 1)
	float freeDist[6];		// +x -x +y -y +z -z (units)
	Vector vecSunDir;
	Vector vecTracePos;		// where the rays start (moved out of a wall)

	int nHits;
	GIHit_t hits[ GI_RAYS ];
};

static GIProbe_t *s_pProbes = NULL;
static Vector s_vecRayDir[ GI_RAYS ];
static bool s_bRayDirsInit = false;

static float s_flSpacing = 64.0f;
static int s_iGridMin[3] = { 0, 0, 0 };
static bool s_bGridValid = false;
static bool s_bClearAtlas = true;
static bool s_bReuploadAll = false;
static double s_flResetTime = 0.0;

// probe offsets in the grid, nearest to its middle first
static CUtlVector< int > s_Order;

static CUtlMap< uintp, Vector > s_AlbedoCache( DefLessFunc( uintp ) );

// work left: probes not traced yet; the light direction every probe's bounce was last done for
static int s_nPending = 0;
static Vector s_vecSunDone( 0, 0, 0 );
static bool s_bSunDone = false;

// statistics
static int s_nInvalidSolid = 0;
static int s_nInvalidClosed = 0;
static int s_nTracesLastFrame = 0;
static lightData_Global_t s_LastLight;
static int s_nProbesLastFrame = 0;
static int s_nSunLastFrame = 0;
static float s_flMsLastFrame = 0.0f;

static CTextureReference g_tex_GIProbes;
static CTextureReference g_tex_GI;
static CTextureReference g_tex_GIBlur;
static IMaterial *g_pMatGI = NULL;
static IMaterial *g_pMatGIBlurH = NULL;
static IMaterial *g_pMatGIBlurV = NULL;
static IMaterial *g_pMatGIUpload = NULL;

//-----------------------------------------------------------------------------
// Render targets and materials
//-----------------------------------------------------------------------------
void InitGIRTs()
{
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	// probe atlas: x = probe x + probe z * GI_NX, y = probe y + plane * GI_NY
	g_tex_GIProbes.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_gi_probes",
		GI_ATLAS_W, GI_ATLAS_H,
		RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_RGBA16161616F,
		MATERIAL_RT_DEPTH_NONE,
		flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );

	// half resolution indirect light in the top left of a full size target (like the SSAO)
	g_tex_GI.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_gi",
		128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
		IMAGE_FORMAT_RGBA16161616F,
		MATERIAL_RT_DEPTH_NONE,
		flags, 0 ) );

	g_tex_GIBlur.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_gi_blur",
		128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
		IMAGE_FORMAT_RGBA16161616F,
		MATERIAL_RT_DEPTH_NONE,
		flags, 0 ) );

	// render targets lose their content with the device: write every probe again
	s_bClearAtlas = true;
	s_bReuploadAll = true;
}

static IMaterial *CreateGIMaterial( const char *pszName, const char *pszShader, int iDirection )
{
	KeyValues *pKV = new KeyValues( pszShader );
	if ( iDirection >= 0 )
		pKV->SetInt( "$direction", iDirection );
	IMaterial *pMat = materials->CreateMaterial( pszName, pKV );
	if ( pMat )
		pMat->IncrementReferenceCount();
	return pMat;
}

static bool IsMaterialOk( IMaterial *pMat )
{
	return pMat && !pMat->IsErrorMaterial();
}

static bool EnsureGIMaterials()
{
	if ( !g_pMatGI )
	{
		g_pMatGI = CreateGIMaterial( "__hl2rpm_gi", "DEFERRED_GI", -1 );
		g_pMatGIBlurH = CreateGIMaterial( "__hl2rpm_gi_blur_h", "DEFERRED_GIBLUR", 0 );
		g_pMatGIBlurV = CreateGIMaterial( "__hl2rpm_gi_blur_v", "DEFERRED_GIBLUR", 1 );
		g_pMatGIUpload = CreateGIMaterial( "__hl2rpm_gi_upload", "DEFERRED_GI_UPLOAD", -1 );
	}
	return IsMaterialOk( g_pMatGI ) && IsMaterialOk( g_pMatGIBlurH ) && IsMaterialOk( g_pMatGIBlurV ) && IsMaterialOk( g_pMatGIUpload );
}

void ShutdownGI()
{
	if ( g_pMatGI ) { g_pMatGI->DecrementReferenceCount(); g_pMatGI = NULL; }
	if ( g_pMatGIBlurH ) { g_pMatGIBlurH->DecrementReferenceCount(); g_pMatGIBlurH = NULL; }
	if ( g_pMatGIBlurV ) { g_pMatGIBlurV->DecrementReferenceCount(); g_pMatGIBlurV = NULL; }
	if ( g_pMatGIUpload ) { g_pMatGIUpload->DecrementReferenceCount(); g_pMatGIUpload = NULL; }
	delete[] s_pProbes;
	s_pProbes = NULL;
}

//-----------------------------------------------------------------------------
// Grid
//-----------------------------------------------------------------------------
static void InitRayDirs()
{
	if ( s_bRayDirsInit )
		return;
	s_bRayDirsInit = true;

	// spherical Fibonacci: even directions over the whole sphere
	const float flGolden = M_PI_F * ( 3.0f - sqrtf( 5.0f ) );
	for ( int i = 0; i < GI_RAYS; i++ )
	{
		const float z = 1.0f - ( 2.0f * i + 1.0f ) / GI_RAYS;
		const float r = sqrtf( Max( 0.0f, 1.0f - z * z ) );
		const float phi = flGolden * i;
		s_vecRayDir[i].Init( r * cosf( phi ), r * sinf( phi ), z );
	}
}

static inline int PosMod( int a, int n )
{
	const int m = a % n;
	return ( m < 0 ) ? m + n : m;
}

static inline int SlotIndex( int cx, int cy, int cz )
{
	return PosMod( cx, GI_NX ) + GI_NX * ( PosMod( cy, GI_NY ) + GI_NY * PosMod( cz, GI_NZ ) );
}

static int __cdecl OrderSort( const int *a, const int *b )
{
	// offsets from the camera's place in the grid; height counts a bit more (other floors)
	const int ax = ( *a % GI_NX ) * 2 - ( GI_NX - 1 ), ay = ( ( *a / GI_NX ) % GI_NY ) * 2 - ( GI_NY - 1 ), az = ( *a / ( GI_NX * GI_NY ) ) * 2 - (int)( GI_CAMERA_LEVEL * 2.0f );
	const int bx = ( *b % GI_NX ) * 2 - ( GI_NX - 1 ), by = ( ( *b / GI_NX ) % GI_NY ) * 2 - ( GI_NY - 1 ), bz = ( *b / ( GI_NX * GI_NY ) ) * 2 - (int)( GI_CAMERA_LEVEL * 2.0f );
	const int da = ax * ax + ay * ay + az * az * 2;
	const int db = bx * bx + by * by + bz * bz * 2;
	return ( da < db ) ? -1 : ( ( da > db ) ? 1 : ( *a - *b ) );
}

// The render targets lost their content (a device reset, mat_reloadallmaterials, a video
// mode change while in game - InitGIRTs is only run again outside of a game): the probes are
// still known on the CPU and are written into the atlas again. Without this the atlas stayed
// empty - every probe "invalid" - and the interiors went flat and bright until the map changed.
void DeferredGI_OnRTContentLost()
{
	s_bClearAtlas = true;
	s_bReuploadAll = true;
}

void DeferredGI_Reset()
{
	s_bGridValid = false;
	s_bClearAtlas = true;
	s_AlbedoCache.RemoveAll();
	if ( s_pProbes )
	{
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			s_pProbes[i].bAssigned = false;
			s_pProbes[i].bTraced = false;
			s_pProbes[i].bSunValid = false;
			s_pProbes[i].bDirty = false;
		}
	}
	s_nPending = 0;
	s_bSunDone = false;
	s_nInvalidSolid = 0;
	s_nInvalidClosed = 0;
	s_flResetTime = Plat_FloatTime();
}

static void EnsureProbes()
{
	if ( s_pProbes )
		return;

	s_pProbes = new GIProbe_t[ GI_NUM_PROBES ];
	memset( s_pProbes, 0, sizeof( GIProbe_t ) * GI_NUM_PROBES );

	s_Order.SetCount( GI_NUM_PROBES );
	for ( int i = 0; i < GI_NUM_PROBES; i++ )
		s_Order[i] = i;
	s_Order.Sort( OrderSort );

	InitRayDirs();
	DeferredGI_Reset();
}

static void ResetProbe( GIProbe_t &p, int cx, int cy, int cz )
{
	if ( !p.bAssigned || p.bTraced )
		s_nPending++;
	p.cell[0] = cx;
	p.cell[1] = cy;
	p.cell[2] = cz;
	p.bAssigned = true;
	p.bTraced = false;
	p.bSunValid = false;
	p.bDirty = true;	// upload as invalid until traced
	p.flValid = 0.0f;
	p.nHits = 0;
	for ( int i = 0; i < 4; i++ )
	{
		p.vis[i] = 0.0f;
		p.bnc[i] = 0.0f;
	}
	p.vecChroma.Init( 1, 1, 1 );
	for ( int i = 0; i < 6; i++ )
		p.freeDist[i] = 0.0f;
}

// keeps the camera within a cell of the middle; returns whether the grid moved
static bool UpdateGrid( const Vector &vecCamera )
{
	const float S = s_flSpacing;
	const float g[3] = { vecCamera.x / S - GI_OFFSET, vecCamera.y / S - GI_OFFSET, vecCamera.z / S - GI_OFFSET };
	const int dims[3] = { GI_NX, GI_NY, GI_NZ };

	int iMin[3];
	bool bMove = !s_bGridValid;
	for ( int a = 0; a < 3; a++ )
	{
		const float flHalf = ( a == 2 ) ? GI_CAMERA_LEVEL : ( dims[a] - 1 ) * 0.5f;
		iMin[a] = s_iGridMin[a];
		if ( !s_bGridValid || fabsf( g[a] - ( s_iGridMin[a] + flHalf ) ) > 1.0f )
		{
			iMin[a] = (int)floorf( g[a] - flHalf + 0.5f );
			bMove = bMove || iMin[a] != s_iGridMin[a];
		}
	}

	if ( !bMove )
		return false;

	for ( int a = 0; a < 3; a++ )
		s_iGridMin[a] = iMin[a];
	s_bGridValid = true;

	for ( int z = 0; z < GI_NZ; z++ )
		for ( int y = 0; y < GI_NY; y++ )
			for ( int x = 0; x < GI_NX; x++ )
			{
				const int cx = iMin[0] + x, cy = iMin[1] + y, cz = iMin[2] + z;
				GIProbe_t &p = s_pProbes[ SlotIndex( cx, cy, cz ) ];
				if ( !p.bAssigned || p.cell[0] != cx || p.cell[1] != cy || p.cell[2] != cz )
					ResetProbe( p, cx, cy, cz );
			}
	return true;
}

//-----------------------------------------------------------------------------
// Tracing
//-----------------------------------------------------------------------------
static inline float Lum( const Vector &c )
{
	return c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
}

// average color of the surface a trace hit (vtex stores it in every VTF)
static Vector SurfaceAlbedo( const trace_t &tr )
{
	const uintp key = (uintp)tr.surface.name;
	const unsigned short idx = s_AlbedoCache.Find( key );
	if ( idx != s_AlbedoCache.InvalidIndex() )
		return s_AlbedoCache[ idx ];

	Vector vecAlbedo( 0.3f, 0.3f, 0.3f );
	if ( tr.surface.name && *tr.surface.name && tr.surface.name[0] != '*'
		&& ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D | SURF_NODRAW ) ) == 0 )
	{
		IMaterial *pMat = materials->FindMaterial( tr.surface.name, TEXTURE_GROUP_WORLD, false );
		if ( pMat && !pMat->IsErrorMaterial() )
		{
			pMat->GetReflectivity( vecAlbedo );
			for ( int i = 0; i < 3; i++ )
				vecAlbedo[i] = clamp( vecAlbedo[i], 0.02f, 0.9f );
		}
	}
	s_AlbedoCache.Insert( key, vecAlbedo );
	return vecAlbedo;
}

class CGITraceFilter : public ITraceFilter
{
public:
	CGITraceFilter( bool bProps ) : m_bProps( bProps ) {}
	virtual bool ShouldHitEntity( IHandleEntity *pEntity, int contentsMask ) { return false; }
	// TRACE_EVERYTHING tests static props regardless of ShouldHitEntity (world + props)
	virtual TraceType_t GetTraceType() const { return m_bProps ? TRACE_EVERYTHING : TRACE_WORLD_ONLY; }
private:
	bool m_bProps;
};

static int s_nTraces = 0;

static inline void GITrace( const Vector &vecStart, const Vector &vecEnd, ITraceFilter *pFilter, trace_t &tr )
{
	Ray_t ray;
	ray.Init( vecStart, vecEnd );
	enginetrace->TraceRay( ray, MASK_OPAQUE, pFilter, &tr );
	s_nTraces++;
}

static inline bool IsSkyHit( const trace_t &tr )
{
	return tr.fraction >= 1.0f || ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) != 0;
}

static inline bool IsSolidPoint( const Vector &vecPos )
{
	return ( enginetrace->GetPointContents( vecPos ) & CONTENTS_SOLID ) != 0;
}

static void TraceProbeGeometry( GIProbe_t &p, ITraceFilter *pFilter )
{
	const float S = s_flSpacing;
	const Vector vecCell( ( p.cell[0] + GI_OFFSET ) * S, ( p.cell[1] + GI_OFFSET ) * S, ( p.cell[2] + GI_OFFSET ) * S );

	if ( !p.bTraced )
		s_nPending--;
	p.bTraced = true;
	p.bSunValid = false;
	p.bDirty = true;
	p.nHits = 0;
	p.flValid = 0.0f;
	for ( int i = 0; i < 4; i++ )
	{
		p.vis[i] = 0.0f;
		p.bnc[i] = 0.0f;
	}
	for ( int i = 0; i < 6; i++ )
		p.freeDist[i] = 0.0f;

	// in a wall: the nearest free point within half a spacing traces for it
	Vector vecPos = vecCell;
	if ( IsSolidPoint( vecPos ) )
	{
		static const Vector s_Axes[6] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ), Vector( 0, 0, 1 ), Vector( 0, 0, -1 ) };
		bool bFound = false;
		for ( int step = 1; step <= 2 && !bFound; step++ )
		{
			for ( int a = 0; a < 6; a++ )
			{
				const Vector vecTry = vecCell + s_Axes[a] * ( S * 0.22f * step );
				if ( !IsSolidPoint( vecTry ) )
				{
					vecPos = vecTry;
					bFound = true;
					break;
				}
			}
		}
		if ( !bFound )
		{
			s_nInvalidSolid++;
			return;
		}
	}

	p.vecTracePos = vecPos;
	int nBack = 0, nStartSolid = 0;
	float vis[4] = { 0, 0, 0, 0 };
	trace_t tr;
	for ( int k = 0; k < GI_RAYS; k++ )
	{
		const Vector &dir = s_vecRayDir[k];
		GITrace( vecPos, vecPos + dir * GI_RAY_LENGTH, pFilter, tr );
		// (starting on a surface: the directions into it are simply blocked)
		if ( tr.startsolid || tr.allsolid )
		{
			nStartSolid++;
			continue;
		}
		if ( IsSkyHit( tr ) )
		{
			vis[0] += 1.0f;
			vis[1] += 2.0f * dir.x;
			vis[2] += 2.0f * dir.y;
			vis[3] += 2.0f * dir.z;
			continue;
		}
		// the back of a face: the probe is inside something closed (a prop, a sealed cavity)
		if ( DotProduct( tr.plane.normal, dir ) > 0.0f )
		{
			nBack++;
			continue;
		}
		GIHit_t &h = p.hits[ p.nHits++ ];
		h.vecNormal = tr.plane.normal;
		h.vecPos = tr.endpos + tr.plane.normal * 2.0f;
		h.vecAlbedo = SurfaceAlbedo( tr );
		h.iRay = k;
	}

	for ( int i = 0; i < 4; i++ )
		p.vis[i] = vis[i] / GI_RAYS;
	p.flValid = ( nBack * 4 > GI_RAYS || nStartSolid * 4 > GI_RAYS * 3 ) ? 0.0f : 1.0f;
	if ( p.flValid <= 0.0f )
		s_nInvalidClosed++;

	// free distance along the axes: whether the probe sees a point of its 8 cells
	static const Vector s_Axes[6] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ), Vector( 0, 0, 1 ), Vector( 0, 0, -1 ) };
	for ( int a = 0; a < 6; a++ )
	{
		GITrace( vecPos, vecPos + s_Axes[a] * S, pFilter, tr );
		p.freeDist[a] = ( tr.startsolid || tr.allsolid ) ? 0.0f : tr.fraction * S;
	}
}

static void ComputeProbeSun( GIProbe_t &p, const Vector &vecLight, bool bLight, ITraceFilter *pFilter )
{
	float bnc[4] = { 0, 0, 0, 0 };
	Vector vecChroma( 0, 0, 0 );

	if ( bLight && p.flValid > 0.0f && vecLight.z > -0.1f )
	{
		trace_t tr;
		for ( int i = 0; i < p.nHits; i++ )
		{
			const GIHit_t &h = p.hits[i];
			const float flNdL = DotProduct( h.vecNormal, vecLight );
			if ( flNdL <= 0.01f )
				continue;
			GITrace( h.vecPos, h.vecPos + vecLight * GI_SUN_RAY_LENGTH, pFilter, tr );
			if ( tr.startsolid || !IsSkyHit( tr ) )
				continue;
			const float f = Lum( h.vecAlbedo ) * flNdL;
			const Vector &dir = s_vecRayDir[ h.iRay ];
			bnc[0] += f;
			bnc[1] += 2.0f * f * dir.x;
			bnc[2] += 2.0f * f * dir.y;
			bnc[3] += 2.0f * f * dir.z;
			vecChroma += h.vecAlbedo * flNdL;
		}
	}

	for ( int i = 0; i < 4; i++ )
		p.bnc[i] = bnc[i] / GI_RAYS;
	const float flLum = Lum( vecChroma );
	p.vecChroma = ( flLum > 1e-5f ) ? vecChroma / flLum : Vector( 1, 1, 1 );
	p.vecSunDir = vecLight;
	p.bSunValid = true;
	p.bDirty = true;
}

static void TraceProbes( const Vector &vecLight, bool bLight )
{
	const double flStart = Plat_FloatTime();
	const bool bLoading = ( flStart - s_flResetTime ) < 4.0;
	const double flBudget = ( bLoading ? r_deferred_gi_load_budget_ms.GetFloat() : r_deferred_gi_budget_ms.GetFloat() ) * 0.001;
	const double flEnd = flStart + flBudget;
	const float flSunCos = cosf( DEG2RAD( r_deferred_gi_sun_refresh.GetFloat() ) );

	CGITraceFilter filter( r_deferred_gi_props.GetBool() );
	// (toward the sun only the world: the sun's own shadows of props are in the cascades, for
	// the bounce they hardly matter - and these rays are long)
	CGITraceFilter filterSun( false );
	s_nTraces = 0;
	int nProbes = 0, nSun = 0;

	// probes not traced yet, nearest first
	for ( int i = 0; i < GI_NUM_PROBES && s_nPending > 0; i++ )
	{
		const int o = s_Order[i];
		const int cx = s_iGridMin[0] + o % GI_NX, cy = s_iGridMin[1] + ( o / GI_NX ) % GI_NY, cz = s_iGridMin[2] + o / ( GI_NX * GI_NY );
		GIProbe_t &p = s_pProbes[ SlotIndex( cx, cy, cz ) ];
		if ( p.bTraced )
			continue;
		TraceProbeGeometry( p, &filter );
		ComputeProbeSun( p, vecLight, bLight, &filterSun );
		nProbes++;
		if ( Plat_FloatTime() > flEnd )
			break;
	}

	// sun bounce of probes the sun has moved away from (nothing to look for while every probe
	// is done for about this light direction)
	if ( s_bSunDone && DotProduct( s_vecSunDone, vecLight ) < flSunCos )
		s_bSunDone = false;
	if ( !s_bSunDone && s_nPending == 0 && Plat_FloatTime() < flEnd )
	{
		bool bAll = true;
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			const int o = s_Order[i];
			const int cx = s_iGridMin[0] + o % GI_NX, cy = s_iGridMin[1] + ( o / GI_NX ) % GI_NY, cz = s_iGridMin[2] + o / ( GI_NX * GI_NY );
			GIProbe_t &p = s_pProbes[ SlotIndex( cx, cy, cz ) ];
			if ( !p.bTraced || ( p.bSunValid && DotProduct( p.vecSunDir, vecLight ) >= flSunCos ) )
				continue;
			ComputeProbeSun( p, vecLight, bLight, &filterSun );
			nSun++;
			if ( Plat_FloatTime() > flEnd )
			{
				bAll = false;
				break;
			}
		}
		if ( bAll )
		{
			s_bSunDone = true;
			s_vecSunDone = vecLight;
		}
	}

	s_nTracesLastFrame = s_nTraces;
	s_nProbesLastFrame = nProbes;
	s_nSunLastFrame = nSun;
	s_flMsLastFrame = ( Plat_FloatTime() - flStart ) * 1000.0f;
}

//-----------------------------------------------------------------------------
// Upload: one texel per probe and plane, written as tiny quads in clip space
//-----------------------------------------------------------------------------
static inline void EmitTexel( CMeshBuilder &mb, int u, int v, const float *d )
{
	// (D3D9: pixel u's center is at screen x = u; the quad covers only it)
	const float x0 = ( u - 0.4f ) * ( 2.0f / GI_ATLAS_W ) - 1.0f;
	const float x1 = ( u + 0.4f ) * ( 2.0f / GI_ATLAS_W ) - 1.0f;
	const float y0 = 1.0f - ( v - 0.4f ) * ( 2.0f / GI_ATLAS_H );
	const float y1 = 1.0f - ( v + 0.4f ) * ( 2.0f / GI_ATLAS_H );

	mb.Position3f( x0, y0, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x1, y0, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x1, y1, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x0, y1, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
}

static void EmitProbe( CMeshBuilder &mb, int iSlot, const GIProbe_t &p )
{
	const int sx = iSlot % GI_NX;
	const int sy = ( iSlot / GI_NX ) % GI_NY;
	const int sz = iSlot / ( GI_NX * GI_NY );
	const int u = sx + sz * GI_NX;

	const bool bValid = p.bTraced && p.flValid > 0.0f;
	float d[4];

	// 0: sky visibility
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.vis[i] : 0.0f;
	EmitTexel( mb, u, sy, d );

	// 1: sun bounce
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.bnc[i] : 0.0f;
	EmitTexel( mb, u, sy + GI_NY, d );

	// 2: bounce color, validity
	d[0] = p.vecChroma.x;
	d[1] = p.vecChroma.y;
	d[2] = p.vecChroma.z;
	d[3] = bValid ? 1.0f : 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 2, d );

	// 3: free distances, 5 bits each: +axis * 32 + -axis
	for ( int a = 0; a < 3; a++ )
	{
		const int qPos = clamp( (int)( p.freeDist[ a * 2 ] / s_flSpacing * 31.0f + 0.5f ), 0, 31 );
		const int qNeg = clamp( (int)( p.freeDist[ a * 2 + 1 ] / s_flSpacing * 31.0f + 0.5f ), 0, 31 );
		d[a] = (float)( qPos * 32 + qNeg );
	}
	d[3] = 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 3, d );
}

static void UploadProbes()
{
	CMatRenderContextPtr pRenderContext( materials );

	if ( s_bClearAtlas )
	{
		pRenderContext->PushRenderTargetAndViewport( g_tex_GIProbes, 0, 0, GI_ATLAS_W, GI_ATLAS_H );
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
		s_bClearAtlas = false;
	}

	if ( s_bReuploadAll )
	{
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			if ( s_pProbes[i].bAssigned )
				s_pProbes[i].bDirty = true;
		}
		s_bReuploadAll = false;
	}

	CUtlVector< int > dirty;
	for ( int i = 0; i < GI_NUM_PROBES; i++ )
	{
		if ( s_pProbes[i].bDirty )
			dirty.AddToTail( i );
	}
	if ( dirty.Count() == 0 )
		return;

	pRenderContext->PushRenderTargetAndViewport( g_tex_GIProbes, 0, 0, GI_ATLAS_W, GI_ATLAS_H );
	pRenderContext->Bind( g_pMatGIUpload );

	for ( int iFirst = 0; iFirst < dirty.Count(); iFirst += GI_PROBES_PER_BATCH )
	{
		const int n = Min( (int)GI_PROBES_PER_BATCH, dirty.Count() - iFirst );
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder mb;
		mb.Begin( pMesh, MATERIAL_QUADS, n * GI_PLANES );
		for ( int i = 0; i < n; i++ )
		{
			const int iSlot = dirty[ iFirst + i ];
			EmitProbe( mb, iSlot, s_pProbes[ iSlot ] );
			s_pProbes[ iSlot ].bDirty = false;
		}
		mb.End();
		pMesh->Draw();
	}

	pRenderContext->PopRenderTargetAndViewport();
}

//-----------------------------------------------------------------------------
// Debug
//-----------------------------------------------------------------------------
static void DrawProbesDebug( const Vector &vecCamera )
{
	if ( !debugoverlay )
		return;
	const float flDuration = Max( 0.05f, gpGlobals->frametime * 1.5f );
	const Vector vecExt( 1.5f, 1.5f, 1.5f );
	for ( int i = 0; i < GI_NUM_PROBES; i++ )
	{
		const GIProbe_t &p = s_pProbes[i];
		if ( !p.bAssigned )
			continue;
		const Vector vecPos( ( p.cell[0] + GI_OFFSET ) * s_flSpacing, ( p.cell[1] + GI_OFFSET ) * s_flSpacing, ( p.cell[2] + GI_OFFSET ) * s_flSpacing );
		if ( vecPos.DistToSqr( vecCamera ) > 400.0f * 400.0f )
			continue;
		int r = 128, g = 128, b = 128;
		if ( p.bTraced )
		{
			if ( p.flValid <= 0.0f )
			{
				r = 255; g = 0; b = 0;
			}
			else
			{
				// sky visibility (average, 0.5 = open) in green, sun bounce in yellow
				const int sky = clamp( (int)( p.vis[0] * 2.0f * 255.0f ), 0, 255 );
				const int sun = clamp( (int)( p.bnc[0] * 4.0f * 255.0f ), 0, 255 );
				r = sun; g = Max( sky, sun ); b = 32;
			}
		}
		debugoverlay->AddBoxOverlay( vecPos, -vecExt, vecExt, vec3_angle, r, g, b, 255, flDuration );
	}
}

static void DeferredGI_Stats()
{
	if ( !s_pProbes )
	{
		Msg( "[gi] not started\n" );
		return;
	}
	int nAssigned = 0, nTraced = 0, nValid = 0, nSun = 0;
	for ( int i = 0; i < GI_NUM_PROBES; i++ )
	{
		const GIProbe_t &p = s_pProbes[i];
		nAssigned += p.bAssigned ? 1 : 0;
		nTraced += p.bTraced ? 1 : 0;
		nValid += ( p.bTraced && p.flValid > 0.0f ) ? 1 : 0;
		nSun += p.bSunValid ? 1 : 0;
	}
	Msg( "[gi] grid %d %d %d (spacing %.0f): %d probes, %d traced, %d valid, %d sun, %d pending; last frame %d new, %d sun, %d traces, %.2f ms\n",
		s_iGridMin[0], s_iGridMin[1], s_iGridMin[2], s_flSpacing, nAssigned, nTraced, nValid, nSun, s_nPending,
		s_nProbesLastFrame, s_nSunLastFrame, s_nTracesLastFrame, s_flMsLastFrame );
	Msg( "[gi] invalid since reset: %d in solid, %d closed (back faces)\n", s_nInvalidSolid, s_nInvalidClosed );

	// per height: valid / traced, mean sky visibility and sun bounce of the valid ones
	for ( int z = 0; z < GI_NZ; z++ )
	{
		int nT = 0, nV = 0;
		float flSky = 0.0f, flBnc = 0.0f;
		for ( int y = 0; y < GI_NY; y++ )
		{
			for ( int x = 0; x < GI_NX; x++ )
			{
				const GIProbe_t &p = s_pProbes[ SlotIndex( s_iGridMin[0] + x, s_iGridMin[1] + y, s_iGridMin[2] + z ) ];
				if ( !p.bTraced )
					continue;
				nT++;
				if ( p.flValid > 0.0f )
				{
					nV++;
					flSky += p.vis[0];
					flBnc += p.bnc[0];
				}
			}
		}
		Msg( "[gi]   z %6.0f: %3d / %3d valid, sky %.3f, bounce %.3f\n", ( s_iGridMin[2] + z ) * s_flSpacing, nV, nT,
			nV ? flSky / nV : 0.0f, nV ? flBnc / nV : 0.0f );
	}
}

// the shader's blend of the 8 probes around the surface under the crosshair, step by step
CON_COMMAND_F( r_deferred_gi_probe_info, "GI probes: the 8 probes blended for the surface under the crosshair", FCVAR_CHEAT )
{
	if ( !s_pProbes || !s_bGridValid )
	{
		Msg( "[gi] no grid\n" );
		return;
	}
	Vector vecFwd;
	AngleVectors( MainViewAngles(), &vecFwd );
	trace_t tr;
	CTraceFilterWorldAndPropsOnly filter;
	Ray_t ray;
	ray.Init( MainViewOrigin(), MainViewOrigin() + vecFwd * 8192.0f );
	enginetrace->TraceRay( ray, MASK_OPAQUE, &filter, &tr );
	if ( tr.fraction >= 1.0f )
		return;

	const float S = s_flSpacing;
	const Vector P = tr.endpos;
	const Vector N = tr.plane.normal;
	const Vector Ps = P + N * ( r_deferred_gi_normal_offset.GetFloat() * S );
	const float g[3] = { Ps.x / S - GI_OFFSET, Ps.y / S - GI_OFFSET, Ps.z / S - GI_OFFSET };
	int base[3];
	float f[3];
	for ( int a = 0; a < 3; a++ )
	{
		base[a] = (int)floorf( g[a] );
		f[a] = g[a] - base[a];
	}
	Msg( "[gi] point %.1f %.1f %.1f normal %.2f %.2f %.2f, lookup %.1f %.1f %.1f (%s)\n", P.x, P.y, P.z, N.x, N.y, N.z, Ps.x, Ps.y, Ps.z,
		tr.surface.name ? tr.surface.name : "?" );

	float wsum = 0.0f, sky = 0.0f;
	for ( int c = 0; c < 8; c++ )
	{
		const int idx[3] = { base[0] + ( c & 1 ), base[1] + ( ( c >> 1 ) & 1 ), base[2] + ( ( c >> 2 ) & 1 ) };
		const GIProbe_t &p = s_pProbes[ SlotIndex( idx[0], idx[1], idx[2] ) ];
		const bool bMatch = p.bAssigned && p.cell[0] == idx[0] && p.cell[1] == idx[1] && p.cell[2] == idx[2];
		const float tw = ( ( c & 1 ) ? f[0] : 1.0f - f[0] ) * ( ( ( c >> 1 ) & 1 ) ? f[1] : 1.0f - f[1] ) * ( ( ( c >> 2 ) & 1 ) ? f[2] : 1.0f - f[2] );
		const bool bValid = bMatch && p.bTraced && p.flValid > 0.0f;
		const Vector probePos( ( idx[0] + GI_OFFSET ) * S, ( idx[1] + GI_OFFSET ) * S, ( idx[2] + GI_OFFSET ) * S );
		const Vector d = Ps - probePos;
		bool bBlocked = false;
		float flFree[3];
		for ( int a = 0; a < 3; a++ )
		{
			const float flRaw = p.freeDist[ a * 2 + ( d[a] >= 0.0f ? 0 : 1 ) ];
			const int q = clamp( (int)( flRaw / S * 31.0f + 0.5f ), 0, 31 );
			flFree[a] = q * S / 31.0f;
			if ( fabsf( d[a] ) - flFree[a] - r_deferred_gi_leak_tolerance.GetFloat() > 0.0f )
				bBlocked = true;
		}
		Vector toProbe = probePos - P;
		const float flFacing = clamp( DotProduct( toProbe, N ) / Max( toProbe.Length(), 1.0f ) * 0.5f + 0.5f, 0.0f, 1.0f );
		const float w = bValid && !bBlocked ? tw * ( flFacing * flFacing + 0.02f ) : 0.0f;
		const float vis = clamp( p.vis[0] + p.vis[1] * N.x + p.vis[2] * N.y + p.vis[3] * N.z, 0.0f, 1.0f );
		wsum += w;
		sky += w * vis;
		Msg( "[gi]  %d cell %d %d %d %s%s trace from %.0f %.0f %.0f, tw %.2f facing %.2f, free %.0f %.0f %.0f (d %.0f %.0f %.0f)%s -> w %.3f, sky vis %.2f (avg %.2f), bounce %.3f\n",
			c, idx[0], idx[1], idx[2], bMatch ? "" : "[slot holds another cell] ", bValid ? "valid" : "INVALID",
			p.vecTracePos.x, p.vecTracePos.y, p.vecTracePos.z, tw, flFacing, flFree[0], flFree[1], flFree[2], d.x, d.y, d.z,
			bBlocked ? " BLOCKED" : "", w, vis, p.vis[0], p.bnc[0] );
	}
	Msg( "[gi]  total weight %.3f, sky visibility %.3f\n", wsum, wsum > 1e-5f ? sky / wsum : -1.0f );
	const lightData_Global_t &l = s_LastLight;
	const float flSky = wsum > 1e-5f ? sky / wsum : 0.0f;
	const float flMin = r_deferred_gi_min.GetFloat();
	Msg( "[gi]  global light: sun %.3f %.3f %.3f (dir %.2f %.2f %.2f), sky %.3f %.3f %.3f, ground %.3f %.3f %.3f -> sky light here %.4f\n",
		l.diff.x, l.diff.y, l.diff.z, l.vecLight.x, l.vecLight.y, l.vecLight.z, l.ambh.x, l.ambh.y, l.ambh.z, l.ambl.x, l.ambl.y, l.ambl.z,
		( l.ambh.x * 0.299f + l.ambh.y * 0.587f + l.ambh.z * 0.114f ) * ( flMin + ( 1.0f - flMin ) * flSky ) );
}

CON_COMMAND( r_deferred_gi_stats, "GI probes: how many are traced / valid, the cost of the last frame" )
{
	DeferredGI_Stats();
}

CON_COMMAND( r_deferred_gi_reset, "GI probes: trace every probe again (applies r_deferred_gi_spacing)" )
{
	DeferredGI_Reset();
}

CON_COMMAND_F( r_deferred_gi_bench, "GI probes: time N rays from the camera (world only / with static props)", FCVAR_CHEAT )
{
	const int n = ( args.ArgC() > 1 ) ? clamp( atoi( args[1] ), 1, 200000 ) : 4000;
	const Vector vecOrigin = MainViewOrigin();
	for ( int iMode = 0; iMode < 2; iMode++ )
	{
		CGITraceFilter filter( iMode == 1 );
		int nSky = 0;
		const double t0 = Plat_FloatTime();
		trace_t tr;
		for ( int i = 0; i < n; i++ )
		{
			Vector dir( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( -1, 1 ) );
			if ( dir.NormalizeInPlace() < 0.01f )
				dir.Init( 0, 0, 1 );
			GITrace( vecOrigin, vecOrigin + dir * GI_RAY_LENGTH, &filter, tr );
			nSky += IsSkyHit( tr ) ? 1 : 0;
		}
		const double dt = Plat_FloatTime() - t0;
		Msg( "[gi] bench %s: %d rays, %.2f us per ray, %d%% sky\n", iMode ? "world + props" : "world only", n, dt * 1e6 / n, nSky * 100 / n );
	}
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------
void DeferredGI_Render( const CViewSetup &view, const lightData_Global_t &globalLight )
{
	giData_t data;

	const bool bOn = r_deferred_gi.GetBool() && globalLight.bEnabled
		&& g_tex_GIProbes.IsValid() && g_tex_GI.IsValid() && g_tex_GIBlur.IsValid() && EnsureGIMaterials();

	ITexture *pProbes = g_tex_GIProbes;
	ITexture *pGI = g_tex_GI;
	ITexture *pBlur = g_tex_GIBlur;

	if ( !bOn )
	{
		QUEUE_FIRE( CommitGIData, data );
		QUEUE_FIRE( CommitTexture_GI, pProbes, pGI, pBlur );
		return;
	}

	EnsureProbes();
	if ( !s_bGridValid )
		s_flSpacing = r_deferred_gi_spacing.GetFloat();

	UpdateGrid( view.origin );

	const Vector vecLight = globalLight.vecLight.AsVector3D();
	s_LastLight = globalLight;
	TraceProbes( vecLight, globalLight.bEnabled );
	UploadProbes();

	const int iDebug = r_deferred_gi_debug.GetInt();
	if ( iDebug == 2 )
		DrawProbesDebug( view.origin );

	const int w = Max( 1, view.width / 2 );
	const int h = Max( 1, view.height / 2 );
	const float flUVScale = (float)w / Max( 1, g_tex_GI->GetActualWidth() );
	const float S = s_flSpacing;

	data.bEnabled = true;
	data.vecGrid.Init( s_iGridMin[0], s_iGridMin[1], s_iGridMin[2], 1.0f / S );
	data.vecDims.Init( GI_NX, GI_NY, GI_NZ, S );
	data.vecAtlas.Init( 1.0f / GI_ATLAS_W, 1.0f / GI_ATLAS_H, r_deferred_gi_normal_offset.GetFloat() * S, (float)iDebug );
	data.vecParams0.Init( r_deferred_gi_bounce.GetFloat(), r_deferred_gi_skybounce.GetFloat(), r_deferred_gi_fill.GetFloat(), r_deferred_gi_min.GetFloat() );
	data.vecParams1.Init( r_deferred_gi_leak_tolerance.GetFloat(), GI_OFFSET, r_deferred_gi_sky_boost.GetFloat(), r_deferred_gi_bounce_indoor.GetFloat() );
	data.vecBlurH.Init( 1.0f / w, 0.0f, 0.04f, flUVScale );
	data.vecBlurV.Init( 0.0f, 1.0f / h, 0.04f, flUVScale );
	data.vecApply.Init( 1.0f, ( iDebug == 1 || iDebug >= 3 ) ? 1.0f : 0.0f, 0.0f, flUVScale );

	QUEUE_FIRE( CommitGIData, data );
	QUEUE_FIRE( CommitTexture_GI, pProbes, pGI, pBlur );

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->PushRenderTargetAndViewport( g_tex_GI, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatGI, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_GIBlur, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatGIBlurH, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_GI, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatGIBlurV, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();
}

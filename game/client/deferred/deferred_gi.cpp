//========= HL2RPM ============================================================//
//
// Purpose: World-space indirect light of the global light ("GI probes").
//
// The sky light used to reach everything the top-down rain map didn't see under a
// roof, and under a roof it dropped to a flat fraction: a closed room was as grey
// as a porch, a window made no difference. Real interiors are lit through their
// openings: the sky light falls in through windows and doors and fades with the
// distance from them, and the sun on the floor by a window lights the walls and
// the ceiling (bounce light) - which light each other in turn.
//
// Two grids of probes around the camera (GI_NX x GI_NY x GI_NZ each; the near one
// r_deferred_gi_spacing apart, the far one 4 x that; toroidal - a step of the camera
// only traces the new slice) shoot rays through the world (the BSP and the brush
// entities at rest, enginetrace):
//  - a ray that escapes to the sky adds to the probe's sky visibility,
//  - a ray that hits a surface keeps the point, its normal and its albedo (the
//    texture's reflectivity); a ray from there toward the sun tells whether the sun
//    lights it.
//
// A probe is traced in passes of GI_RAYS rays, each pass in directions of its own (a
// rotation of the same even set, different for every probe): the first pass makes the
// probe usable at once, the following ones (up to r_deferred_gi_rays in all) are traced
// when nothing new is waiting, nearest to the camera first. 32 rays alone see an
// opening - a door from across the room, the patch of sun on the floor - with one ray
// or with none: probe next to probe got light or none, and the light of a room came in
// blobs the size of the grid ("the shade loads in chunks, not a soft spread of light").
//
// From the hits of a probe its indirect light is gathered (GatherProbe): what a hit
// sends back is its albedo x the light it gets - the sun (if it is lit), the sky it
// sees and the indirect light at it, all three taken from the probes around the hit as
// they are now. Repeated (a few probes every frame, all of them over and over while
// anything changes) that is light bouncing as many times as it takes: the sun patch by
// the door lights the wall, the wall the ceiling, the room fills softly. The sky
// visibility is smoothed with the neighbour probes in sight of each other first.
//
// Everything is kept as L1 spherical harmonics (4 numbers: the light a surface of any
// orientation at the probe gets) - sky visibility, indirect sun, bounced sky - the two
// indirect ones as luminance with a color. Plus the free distance along the 6 axes: a
// probe beyond a wall must not light the room (leaks).
//
// The probes go to the GPU through render targets (one texel per probe and plane,
// written as tiny quads - a texture lock from the main thread would stall the render
// thread); DEFERRED_GI blends the 8 probes around every pixel at half resolution
// (dropping the ones behind the surface or out of its sight): the near grid, fading
// over its outer half into the far grid, and beyond both an estimate from the sky
// visibility map - the indirect light the global light pass uses instead of its flat
// sky light.
//
// CPU: the rays are traced on the thread pool (the BSP trace keeps its state per
// thread, and brush entities are clipped through the same code; static props and the
// material system are not thread safe - props don't occlude, the albedo is looked up
// on the main thread afterwards). The whole near grid gets its first pass in the first
// frame of a map (the level is still loading), a new slice within a frame or two; a
// wall time budget per frame (r_deferred_gi_budget_ms; more while a lot is missing
// after a teleport), and a probe fades in instead of popping. The sun's part is redone
// from the kept hits when the sun has moved (r_deferred_gi_sun_refresh degrees).
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
#include "vstdlib/jobthread.h"
#include "debugoverlay_shared.h"
#include "engine/ICollideable.h"
#include "engine/ivmodelinfo.h"
#include "cliententitylist.h"
#include "collisionutils.h"
#include "model_types.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_gi( "r_deferred_gi", "1", FCVAR_ARCHIVE,
	"World-space indirect light: sky light through windows and doors, sun light bounced off lit surfaces (probes traced around the camera)" );
static ConVar r_deferred_gi_budget_ms( "r_deferred_gi_budget_ms", "1.0", FCVAR_ARCHIVE,
	"GI probes: time per frame for tracing (ms; the worker threads trace during it)", true, 0.1f, true, 50.0f );
static ConVar r_deferred_gi_catchup_ms( "r_deferred_gi_catchup_ms", "4", 0,
	"GI probes: time per frame for tracing while many probes are missing (after a teleport)", true, 0.1f, true, 50.0f );
static ConVar r_deferred_gi_load_ms( "r_deferred_gi_load_ms", "2500", 0,
	"GI probes: the first frame after a map loads traces the probes around the camera - at most this long (ms)", true, 0.0f, true, 8000.0f );
static ConVar r_deferred_gi_threads( "r_deferred_gi_threads", "1", 0,
	"GI probes: worker threads - 0 none (the main thread traces), 1 the engine's pool (3 threads), 2 a pool of its own with a thread per spare core, more = that many" );
static ConVar r_deferred_gi_fade( "r_deferred_gi_fade", "0.35", 0,
	"GI probes: seconds a newly traced probe takes to fade in", true, 0.0f, true, 5.0f );
static ConVar r_deferred_gi_bounce( "r_deferred_gi_bounce", "1.0", FCVAR_ARCHIVE,
	"GI: strength of the sun light bounced off lit surfaces", true, 0.0f, true, 4.0f );
// (r_deferred_gi_bounce_indoor before, 2.5 for the single bounce of the sun: renamed - the
// light bounces on now and the bounced sky light is boosted too, the old value stored in
// somebody's config would be too much)
static ConVar r_deferred_gi_indoor_boost( "r_deferred_gi_indoor_boost", "2.5", 0,
	"GI: bounced light x this where no sky is in sight (rooms): the eye adapts to a dark room far more than the exposure does", true, 1.0f, true, 8.0f );
static ConVar r_deferred_gi_skybounce( "r_deferred_gi_skybounce", "1.0", 0,
	"GI: strength of the sky light bounced off the surroundings", true, 0.0f, true, 4.0f );
static ConVar r_deferred_gi_multibounce( "r_deferred_gi_multibounce", "1", 0,
	"GI: light bounces more than once (0 = the first bounce of the sun and of the sky only)" );
static ConVar r_deferred_gi_filter( "r_deferred_gi_filter", "1", 0,
	"GI: smooth the sky visibility of a probe with its neighbours in sight (0 = each probe's own rays only)" );
static ConVar r_deferred_gi_min( "r_deferred_gi_min", "0.07", FCVAR_ARCHIVE,
	"GI: part of the sky light left where no sky is in sight - how dark a closed room gets (0..1)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_gi_normal_offset( "r_deferred_gi_normal_offset", "0.4", 0,
	"GI: probe lookup off the surface along its normal (x probe spacing)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_gi_leak_tolerance( "r_deferred_gi_leak_tolerance", "4", 0,
	"GI: how much farther than its free distance a probe may still light a point (units at a spacing of 64)", true, 0.0f, true, 64.0f );
// HL2RPM: the sky light by how much of the sky a point sees (deferred_gi_ps30, SkyResponse). The
// maps' ambient was tuned for a flat sky light of a few % of the sun; the old linear boost (x 4) that
// lit the rooms made the open shade as bright as a sunlit wall and pale blue.
static ConVar r_deferred_gi_sky_open( "r_deferred_gi_sky_open", "1.8", FCVAR_ARCHIVE,
	"GI: sky light by day x this where the whole sky is in sight (the GI occludes it; less sky in sight gets relatively more, r_deferred_gi_sky_gamma)", true, 1.0f, true, 16.0f );
static ConVar r_deferred_gi_sky_gamma( "r_deferred_gi_sky_gamma", "0.5", FCVAR_ARCHIVE,
	"GI: the sky light of a point as ( part of the sky in sight ) ^ this (1 = linear; less = rooms brighter against the open)", true, 0.2f, true, 1.0f );
static ConVar r_deferred_gi_sky_indoor( "r_deferred_gi_sky_indoor", "4", 0,
	"GI: the sky light that reaches a room - bounced inside it, and the least of it where no sky is in sight - x this by day (what lit the rooms before the open sky was toned down)", true, 1.0f, true, 16.0f );
static ConVar r_deferred_gi_sky_desat( "r_deferred_gi_sky_desat", "0.3", FCVAR_ARCHIVE,
	"GI: how much of its color the sky light loses (the whole sky lights a point - its pale horizon too - not the deep blue zenith)", true, 0.0f, true, 1.0f );
// (the old name, still in configs: unused)
static ConVar r_deferred_gi_sky_boost( "r_deferred_gi_sky_boost", "4", FCVAR_HIDDEN, "Obsolete: r_deferred_gi_sky_open, r_deferred_gi_sky_gamma" );
static ConVar r_deferred_gi_sun_refresh( "r_deferred_gi_sun_refresh", "1.5", 0,
	"GI: redo a probe's sun bounce when the sun has moved this many degrees", true, 0.1f, true, 45.0f );
static ConVar r_deferred_gi_spacing( "r_deferred_gi_spacing", "64", 0,
	"GI: distance between the probes of the near grid (units; applied by r_deferred_gi_reset / a new map)", true, 16.0f, true, 256.0f );
static ConVar r_deferred_gi_far( "r_deferred_gi_far", "1", 0,
	"GI: the far grid of probes (4 x the spacing) around the near one" );
static ConVar r_deferred_gi_rays( "r_deferred_gi_rays", "192", FCVAR_ARCHIVE,
	"GI: rays per probe of the near grid (traced 32 at a time; more = smoother light through small openings, longer until it settles)", true, 32.0f, true, 256.0f );
static ConVar r_deferred_gi_rays_far( "r_deferred_gi_rays_far", "64", 0,
	"GI: rays per probe of the far grid", true, 32.0f, true, 256.0f );
static ConVar r_deferred_gi_brush_entities( "r_deferred_gi_brush_entities", "1", 0,
	"GI: brush entities at rest (func_brush, closed doors...) block the light like the world does (0 = the world only)" );
static ConVar r_deferred_gi_fade_cells( "r_deferred_gi_fade_cells", "5", 0,
	"GI: the near grid fades into the far one over this many of its outer cells (a wide band: nothing is seen to 'load in' on the way there)", true, 1.0f, true, 15.0f );
static ConVar r_deferred_gi_debug( "r_deferred_gi_debug", "0", FCVAR_CHEAT,
	"GI: 1 = ambient light only, 2 = + draw the probes near the camera, 3 = sky visibility, 4 = indirect sun, 5 = which source lights a pixel (red near grid, green far grid, blue estimate), 6 = bounced sky light" );

enum
{
	// (32 x 32: the near grid reaches 990 units from the camera - with 24 the far wall of a large
	// room, the houses across a square were lit by the far grid, 256 units between its probes)
	GI_NX = 32,
	GI_NY = 32,
	GI_NZ = 10,
	GI_NUM_PROBES = GI_NX * GI_NY * GI_NZ,
	GI_RAYS = 32,			// rays of one pass
	GI_MAX_PASSES = 8,
	GI_PLANES = 6,
	GI_ATLAS_W = GI_NX * GI_NZ,
	GI_ATLAS_H = GI_NY * GI_PLANES,
	GI_PROBES_PER_BATCH = 512,
	GI_CASCADES = 2,
	GI_MAX_CANDIDATES = 48,
};

// probes sit off the grid lines (cell + 0.45): map geometry is aligned to powers of two, probes at
// multiples of 64 lay exactly on floors, ceilings and walls (half their rays started in the solid)
#define GI_OFFSET			0.45f

// (a ray that reaches nothing within this is open sky: long rays through open space cost the
// most - on a map like demo_map a ray of 3072 units takes ~10 us, and the rays toward the sun,
// once 16384 long, took most of a probe's time. Per grid: x its spacing / 64.)
#define GI_RAY_LENGTH		1536.0f
#define GI_RAY_LENGTH_MAX	3072.0f
#define GI_SUN_RAY_LENGTH	2560.0f

// the far grid's probes are this many times farther apart
#define GI_FAR_SCALE		4.0f

// the camera sits this many probes above the bottom of a grid: most of what matters (rooms,
// roofs, the street) is around and above the eye, and below the ground probes are wasted
static const float s_flCameraLevel[ GI_CASCADES ] = { 3.5f, 3.5f };

// a surface a ray of a probe reached (20 bytes: a probe keeps up to 256 of them)
struct GIHit_t
{
	Vector vecPos;				// off the surface along its normal
	signed char n[3];			// its normal x 127
	unsigned char bLit;			// the sun reaches it (for the probe's vecSunDir)
	unsigned char albedo[3];	// x 255
	unsigned char pad;
};

struct GIProbe_t
{
	int cell[3];			// world cell this slot holds
	bool bAssigned;
	bool bTraced;			// the first pass is done: the probe lights
	bool bSunValid;			// the hits' sun rays are done for vecSunDir
	bool bDirty;			// changed since the last upload
	bool bFading;			// in the list of probes fading in
	bool bQueued;			// in the batch being worked on
	bool bRetrace;			// what is around it has changed (a door): start over, lighting with the old data meanwhile
	bool bAlbedoPending;
	unsigned char nPasses;	// passes of GI_RAYS rays traced

	float flFade;			// 0..1 after it was traced
	float flValid;			// 0: in a wall / closed space
	int nBack;				// rays that hit the back of a face / started in the solid, over all passes
	int nStartSolid;
	float visSum[4];		// sky visibility, summed over the rays
	float vis[4];			// sky visibility: vis( n ) = vis[0] + dot( vis[1..3], n )
	float visF[4];			// ... smoothed with the neighbour probes in sight (what lights)
	float sun[4];			// indirect light of the sun (luminance), the same way
	Vector vecSunChroma;	// its color (luminance 1)
	float sky[4];			// sky light bounced off the surroundings
	Vector vecSkyChroma;
	float freeDist[6];		// +x -x +y -y +z -z (units)
	Vector vecSunDir;
	Vector vecTracePos;		// where the rays start (moved out of a wall)

	int nHits;
	int nHitsAlloc;
	GIHit_t *pHits;
};

struct GICascade_t
{
	float flSpacing;
	int iGridMin[3];
	bool bGridValid;
	bool bClearAtlas;
	bool bReuploadAll;
	GIProbe_t *pProbes;
	// probe offsets in the grid, nearest to the camera's place in it first
	CUtlVector< int > order;
	// work left: probes without their first pass; the light direction every probe's sun rays were last done for
	int nPending;
	Vector vecSunDone;
	bool bSunDone;
	bool bRefined;			// no probe is waiting for more passes
	// the gather goes round the grid: where it is, how many more rounds
	int iSweepCursor;
	int nSweepsLeft;
	CTextureReference texProbes;
};

enum
{
	GIJOB_TRACE = 0,	// a pass of rays (and the sun rays of the hits)
	GIJOB_SUN,			// the sun rays of the hits alone
	GIJOB_GATHER,		// the indirect light from the hits
};

// what one worker does for one probe
struct GIJob_t
{
	GIProbe_t *pProbe;
	int iCascade;
	int iType;
	Vector vecLight;
	bool bLight;
	// results
	int nTraces;
	int nInvalid;		// 1 = in the solid, 2 = in a closed space
	bool bSkipped;		// the frame's time was up before a worker got to it
	bool bSunAll;		// the sun rays of all the hits were traced (not only of the new ones)
	int iFirstNewHit;
	const char *pszSurface[ GI_RAYS ];	// of the new hits (the albedo is looked up on the main thread)
};

static GICascade_t s_Cascades[ GI_CASCADES ];
static bool s_bProbesAllocated = false;
static Vector s_vecRayDir[ GI_RAYS ];
static bool s_bRayDirsInit = false;

static double s_flResetTime = 0.0;
static bool s_bFirstFill = true;				// a map has just been loaded: trace before the first frame shows
static CUtlVector< GIProbe_t * > s_Fading;
static float s_flProbesPerMs = 8.0f;			// measured throughput of the workers
static float s_flGathersPerMs = 100.0f;			// ... gathering
static float s_flCameraSky = 1.0f;				// sky in sight at the camera (0 closed room .. 1 open), smoothed

static CUtlMap< uintp, Vector > s_AlbedoCache( DefLessFunc( uintp ) );

// statistics
static int s_nInvalidSolid = 0;
static int s_nInvalidClosed = 0;
static int s_nTracesLastFrame = 0;
static lightData_Global_t s_LastLight;
static int s_nProbesLastFrame = 0;
static int s_nRefineLastFrame = 0;
static int s_nSunLastFrame = 0;
static int s_nGatherLastFrame = 0;
static float s_flMsLastFrame = 0.0f;
static float s_flMsWorst = 0.0f;

static CTextureReference g_tex_GI;
static CTextureReference g_tex_GIBlur;
static IMaterial *g_pMatGI = NULL;
static IMaterial *g_pMatGIBlurH = NULL;
static IMaterial *g_pMatGIBlurV = NULL;
static IMaterial *g_pMatGIUp = NULL;
static IMaterial *g_pMatGIUpload = NULL;

//-----------------------------------------------------------------------------
// Render targets and materials
//-----------------------------------------------------------------------------
void InitGIRTs()
{
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	// probe atlases: x = probe x + probe z * GI_NX, y = probe y + plane * GI_NY
	static const char *s_pszAtlas[ GI_CASCADES ] = { "_rt_hl2rpm_gi_probes", "_rt_hl2rpm_gi_probes_far" };
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		s_Cascades[c].texProbes.Init( materials->CreateNamedRenderTargetTextureEx2(
			s_pszAtlas[c],
			GI_ATLAS_W, GI_ATLAS_H,
			RT_SIZE_NO_CHANGE,
			IMAGE_FORMAT_RGBA16161616F,
			MATERIAL_RT_DEPTH_NONE,
			flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );

		// render targets lose their content with the device: write every probe again
		s_Cascades[c].bClearAtlas = true;
		s_Cascades[c].bReuploadAll = true;
	}

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
		g_pMatGIUp = CreateGIMaterial( "__hl2rpm_gi_up", "DEFERRED_GIUP", -1 );
		g_pMatGIUpload = CreateGIMaterial( "__hl2rpm_gi_upload", "DEFERRED_GI_UPLOAD", -1 );
	}
	return IsMaterialOk( g_pMatGI ) && IsMaterialOk( g_pMatGIBlurH ) && IsMaterialOk( g_pMatGIBlurV ) && IsMaterialOk( g_pMatGIUp ) && IsMaterialOk( g_pMatGIUpload );
}

//-----------------------------------------------------------------------------
// Brush entities that block the light
//
// The rays used to see the world brushes alone: a house built of func_brush was lit
// inside like the street around it. Brush entities at rest are traced too - through
// a stand-in collideable that says SOLID_BSP whatever the entity's own solidity is
// (the engine then clips the ray to the brush model with the code it uses for the
// world, which is safe on the worker threads; a vphysics entity would go through
// vphysics). One that moves (a door opening, a lift) stops blocking until it has
// come to rest again, and the probes around its old and its new place start over.
//-----------------------------------------------------------------------------
class CGIBrushCollide : public ICollideable
{
public:
	virtual IHandleEntity	*GetEntityHandle() { return m_pHandle; }
	virtual const Vector&	OBBMinsPreScaled() const { return m_vecMins; }
	virtual const Vector&	OBBMaxsPreScaled() const { return m_vecMaxs; }
	virtual const Vector&	OBBMins() const { return m_vecMins; }
	virtual const Vector&	OBBMaxs() const { return m_vecMaxs; }
	virtual void			WorldSpaceTriggerBounds( Vector *pVecWorldMins, Vector *pVecWorldMaxs ) const { *pVecWorldMins = m_vecWorldMins; *pVecWorldMaxs = m_vecWorldMaxs; }
	virtual bool			TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr ) { return false; }
	virtual bool			TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr ) { return false; }
	virtual int				GetCollisionModelIndex() { return m_iModelIndex; }
	virtual const model_t*	GetCollisionModel() { return m_pModel; }
	virtual const Vector&	GetCollisionOrigin() const { return m_vecOrigin; }
	virtual const QAngle&	GetCollisionAngles() const { return m_angAngles; }
	virtual const matrix3x4_t&	CollisionToWorldTransform() const { return m_matToWorld; }
	virtual SolidType_t		GetSolid() const { return SOLID_BSP; }
	virtual int				GetSolidFlags() const { return 0; }
	virtual IClientUnknown*	GetIClientUnknown() { return m_pUnknown; }
	virtual int				GetCollisionGroup() const { return 0; }
	virtual void			WorldSpaceSurroundingBounds( Vector *pVecMins, Vector *pVecMaxs ) { *pVecMins = m_vecWorldMins; *pVecMaxs = m_vecWorldMaxs; }
	virtual bool			ShouldTouchTrigger( int triggerSolidFlags ) const { return false; }
	virtual const matrix3x4_t	*GetRootParentToWorldTransform() const { return NULL; }

	IHandleEntity *m_pHandle;
	IClientUnknown *m_pUnknown;
	const model_t *m_pModel;
	int m_iModelIndex;
	Vector m_vecMins, m_vecMaxs;
	Vector m_vecWorldMins, m_vecWorldMaxs;
	Vector m_vecOrigin;
	QAngle m_angAngles;
	matrix3x4_t m_matToWorld;
};

struct GIBrushEnt_t
{
	CGIBrushCollide collide;
	EHANDLE hEnt;
};

// what the scan remembers of an entity between scans
struct GIBrushTrack_t
{
	EHANDLE hEnt;
	Vector vecOrigin;
	QAngle angAngles;
	float flStableSince;
	bool bSeen;
};

static CUtlVector< GIBrushEnt_t * > s_BrushEnts;		// the ones blocking now (read by the workers)
static CUtlVector< GIBrushTrack_t > s_BrushTracks;
static float s_flNextBrushScan = 0.0f;
static int s_nBrushInvalidations = 0;

static void InvalidateProbesAround( const Vector &vecMins, const Vector &vecMaxs )
{
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		if ( !cas.pProbes || !cas.bGridValid )
			continue;
		// the probes whose rays the change matters to most: the ones around it
		const float flReach = ( c == 0 ) ? 384.0f : cas.flSpacing * 1.5f;
		const Vector vecLo = vecMins - Vector( flReach, flReach, flReach );
		const Vector vecHi = vecMaxs + Vector( flReach, flReach, flReach );
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			GIProbe_t &p = cas.pProbes[i];
			if ( !p.bAssigned || !p.bTraced || p.bRetrace )
				continue;
			const Vector vecPos( ( p.cell[0] + GI_OFFSET ) * cas.flSpacing, ( p.cell[1] + GI_OFFSET ) * cas.flSpacing, ( p.cell[2] + GI_OFFSET ) * cas.flSpacing );
			if ( vecPos.x < vecLo.x || vecPos.y < vecLo.y || vecPos.z < vecLo.z || vecPos.x > vecHi.x || vecPos.y > vecHi.y || vecPos.z > vecHi.z )
				continue;
			p.bRetrace = true;
			cas.nPending++;
		}
	}
	s_nBrushInvalidations++;
}

static void ClearBrushEntities()
{
	s_BrushEnts.PurgeAndDeleteElements();
	s_BrushTracks.Purge();
	s_flNextBrushScan = 0.0f;
}

// main thread, a few times a second: which brush entities block the light now
static void UpdateBrushEntities( bool bFirst )
{
	if ( !bFirst && gpGlobals->realtime < s_flNextBrushScan )
		return;
	s_flNextBrushScan = gpGlobals->realtime + 0.25f;

	if ( !r_deferred_gi_brush_entities.GetBool() )
	{
		if ( s_BrushEnts.Count() )
		{
			for ( int i = 0; i < s_BrushEnts.Count(); i++ )
				InvalidateProbesAround( s_BrushEnts[i]->collide.m_vecWorldMins, s_BrushEnts[i]->collide.m_vecWorldMaxs );
			ClearBrushEntities();
		}
		return;
	}

	for ( int i = 0; i < s_BrushTracks.Count(); i++ )
		s_BrushTracks[i].bSeen = false;

	// The engine asks the collideable it has clipped a ray to for its entity (the trace's m_pEnt).
	// The stand-ins answer with the world: it is there as long as the map is, while the brush
	// entity itself can be deleted between two scans - a worker would hand the engine a pointer to
	// freed memory.
	C_BaseEntity *pWorld = ClientEntityList().GetBaseEntity( 0 );

	CUtlVector< GIBrushEnt_t * > now;
	for ( C_BaseEntity *pEnt = ClientEntityList().FirstBaseEntity(); pEnt; pEnt = ClientEntityList().NextBaseEntity( pEnt ) )
	{
		// (a dormant entity - out of the player's PVS - stands where it stood: skipping those
		// would start the probes around every brush entity over each time it comes into sight)
		if ( !pWorld || pEnt->entindex() <= 0 || pEnt->IsEffectActive( EF_NODRAW ) )
			continue;
		const model_t *pModel = pEnt->GetModel();
		if ( !pModel || modelinfo->GetModelType( pModel ) != mod_brush || pEnt->GetModelIndex() <= 1 )
			continue;
		// (what is drawn see-through lets the light through)
		if ( pEnt->GetRenderMode() != kRenderNormal || pEnt->GetRenderColor().a < 255 )
			continue;

		const Vector vecOrigin = pEnt->GetAbsOrigin();
		const QAngle angAngles = pEnt->GetAbsAngles();

		int iTrack = -1;
		for ( int i = 0; i < s_BrushTracks.Count(); i++ )
		{
			if ( s_BrushTracks[i].hEnt.Get() == pEnt )
			{
				iTrack = i;
				break;
			}
		}
		if ( iTrack < 0 )
		{
			iTrack = s_BrushTracks.AddToTail();
			GIBrushTrack_t &t = s_BrushTracks[ iTrack ];
			t.hEnt = pEnt;
			t.vecOrigin = vecOrigin;
			t.angAngles = angAngles;
			// (what is there when the map starts has been there all along)
			t.flStableSince = bFirst ? -1000.0f : gpGlobals->realtime;
		}
		GIBrushTrack_t &t = s_BrushTracks[ iTrack ];
		t.bSeen = true;
		if ( ( t.vecOrigin - vecOrigin ).LengthSqr() > 0.01f
			|| fabsf( AngleDiff( t.angAngles.x, angAngles.x ) ) + fabsf( AngleDiff( t.angAngles.y, angAngles.y ) ) + fabsf( AngleDiff( t.angAngles.z, angAngles.z ) ) > 0.05f )
		{
			t.vecOrigin = vecOrigin;
			t.angAngles = angAngles;
			t.flStableSince = gpGlobals->realtime;
		}
		if ( gpGlobals->realtime - t.flStableSince < 0.4f )
			continue;	// moving

		GIBrushEnt_t *pNew = new GIBrushEnt_t;
		pNew->hEnt = pEnt;
		CGIBrushCollide &col = pNew->collide;
		col.m_pHandle = pWorld;
		col.m_pUnknown = pWorld;
		col.m_pModel = pModel;
		col.m_iModelIndex = pEnt->GetModelIndex();
		modelinfo->GetModelBounds( pModel, col.m_vecMins, col.m_vecMaxs );
		col.m_vecOrigin = vecOrigin;
		col.m_angAngles = angAngles;
		AngleMatrix( angAngles, vecOrigin, col.m_matToWorld );
		TransformAABB( col.m_matToWorld, col.m_vecMins, col.m_vecMaxs, col.m_vecWorldMins, col.m_vecWorldMaxs );
		now.AddToTail( pNew );
	}

	for ( int i = s_BrushTracks.Count() - 1; i >= 0; i-- )
	{
		if ( !s_BrushTracks[i].bSeen )
			s_BrushTracks.FastRemove( i );
	}

	// what has changed since the last scan: the probes around it start over
	if ( !bFirst )
	{
		for ( int i = 0; i < s_BrushEnts.Count(); i++ )
		{
			const CGIBrushCollide &a = s_BrushEnts[i]->collide;
			bool bSame = false;
			for ( int k = 0; k < now.Count() && !bSame; k++ )
			{
				const CGIBrushCollide &b = now[k]->collide;
				bSame = s_BrushEnts[i]->hEnt == now[k]->hEnt && a.m_iModelIndex == b.m_iModelIndex
					&& ( a.m_vecOrigin - b.m_vecOrigin ).LengthSqr() < 0.01f && a.m_angAngles == b.m_angAngles;
			}
			if ( !bSame )
				InvalidateProbesAround( a.m_vecWorldMins, a.m_vecWorldMaxs );
		}
		for ( int k = 0; k < now.Count(); k++ )
		{
			const CGIBrushCollide &b = now[k]->collide;
			bool bSame = false;
			for ( int i = 0; i < s_BrushEnts.Count() && !bSame; i++ )
			{
				const CGIBrushCollide &a = s_BrushEnts[i]->collide;
				bSame = s_BrushEnts[i]->hEnt == now[k]->hEnt && a.m_iModelIndex == b.m_iModelIndex
					&& ( a.m_vecOrigin - b.m_vecOrigin ).LengthSqr() < 0.01f && a.m_angAngles == b.m_angAngles;
			}
			if ( !bSame )
				InvalidateProbesAround( b.m_vecWorldMins, b.m_vecWorldMaxs );
		}
	}

	s_BrushEnts.PurgeAndDeleteElements();
	s_BrushEnts.AddVectorToTail( now );
}

static void ShutdownGIPool();

void ShutdownGI()
{
	if ( g_pMatGI ) { g_pMatGI->DecrementReferenceCount(); g_pMatGI = NULL; }
	if ( g_pMatGIBlurH ) { g_pMatGIBlurH->DecrementReferenceCount(); g_pMatGIBlurH = NULL; }
	if ( g_pMatGIBlurV ) { g_pMatGIBlurV->DecrementReferenceCount(); g_pMatGIBlurV = NULL; }
	if ( g_pMatGIUp ) { g_pMatGIUp->DecrementReferenceCount(); g_pMatGIUp = NULL; }
	if ( g_pMatGIUpload ) { g_pMatGIUpload->DecrementReferenceCount(); g_pMatGIUpload = NULL; }
	s_Fading.Purge();
	ClearBrushEntities();
	ShutdownGIPool();
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		if ( s_Cascades[c].pProbes )
		{
			for ( int i = 0; i < GI_NUM_PROBES; i++ )
			{
				if ( s_Cascades[c].pProbes[i].pHits )
					free( s_Cascades[c].pProbes[i].pHits );
			}
		}
		delete[] s_Cascades[c].pProbes;
		s_Cascades[c].pProbes = NULL;
		s_Cascades[c].order.Purge();
	}
	s_bProbesAllocated = false;
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

// the directions of a probe's pass: the even set, turned - another way for every pass and
// every probe (the same set everywhere made the rays of neighbouring probes miss the same
// opening together; passes add up to an ever denser set)
static void PassRotation( const GIProbe_t &p, int iPass, matrix3x4_t &mat )
{
	unsigned int h = (unsigned int)p.cell[0] * 73856093u ^ (unsigned int)p.cell[1] * 19349663u ^ (unsigned int)p.cell[2] * 83492791u ^ (unsigned int)( iPass + 1 ) * 2654435761u;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	// a uniform random rotation (Shoemake)
	const float u1 = ( h & 0x3ff ) * ( 1.0f / 1023.0f );
	const float u2 = ( ( h >> 10 ) & 0x7ff ) * ( 1.0f / 2047.0f );
	const float u3 = ( ( h >> 21 ) & 0x7ff ) * ( 1.0f / 2047.0f );
	const float a = sqrtf( 1.0f - u1 ), b = sqrtf( u1 );
	Quaternion q;
	q.x = a * sinf( 2.0f * M_PI_F * u2 );
	q.y = a * cosf( 2.0f * M_PI_F * u2 );
	q.z = b * sinf( 2.0f * M_PI_F * u3 );
	q.w = b * cosf( 2.0f * M_PI_F * u3 );
	QuaternionMatrix( q, mat );
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

static float s_flSortLevel = 2.5f;
static int __cdecl OrderSort( const int *a, const int *b )
{
	// offsets from the camera's place in the grid; height counts a bit more (other floors)
	const int iLevel = (int)( s_flSortLevel * 2.0f );
	const int ax = ( *a % GI_NX ) * 2 - ( GI_NX - 1 ), ay = ( ( *a / GI_NX ) % GI_NY ) * 2 - ( GI_NY - 1 ), az = ( *a / ( GI_NX * GI_NY ) ) * 2 - iLevel;
	const int bx = ( *b % GI_NX ) * 2 - ( GI_NX - 1 ), by = ( ( *b / GI_NX ) % GI_NY ) * 2 - ( GI_NY - 1 ), bz = ( *b / ( GI_NX * GI_NY ) ) * 2 - iLevel;
	const int da = ax * ax + ay * ay + az * az * 2;
	const int db = bx * bx + by * by + bz * bz * 2;
	return ( da < db ) ? -1 : ( ( da > db ) ? 1 : ( *a - *b ) );
}

// The render targets lost their content (a device reset, mat_reloadallmaterials, a video
// mode change while in game - InitGIRTs is only run again outside of a game): the probes are
// still known on the CPU and are written into the atlases again. Without this the atlases stayed
// empty - every probe "invalid" - and the interiors went flat and bright until the map changed.
void DeferredGI_OnRTContentLost()
{
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		s_Cascades[c].bClearAtlas = true;
		s_Cascades[c].bReuploadAll = true;
	}
}

void DeferredGI_Reset()
{
	s_AlbedoCache.RemoveAll();
	s_Fading.RemoveAll();
	ClearBrushEntities();
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		cas.bGridValid = false;
		cas.bClearAtlas = true;
		cas.nPending = 0;
		cas.bSunDone = false;
		cas.bRefined = false;
		cas.iSweepCursor = 0;
		cas.nSweepsLeft = 0;
		if ( cas.pProbes )
		{
			for ( int i = 0; i < GI_NUM_PROBES; i++ )
			{
				GIProbe_t &p = cas.pProbes[i];
				p.bAssigned = false;
				p.bTraced = false;
				p.bSunValid = false;
				p.bDirty = false;
				p.bFading = false;
				p.bQueued = false;
				p.bRetrace = false;
				p.bAlbedoPending = false;
				p.nPasses = 0;
				p.nHits = 0;
			}
		}
	}
	s_nInvalidSolid = 0;
	s_nInvalidClosed = 0;
	s_flMsWorst = 0.0f;
	s_flCameraSky = 1.0f;
	s_flResetTime = Plat_FloatTime();
	s_bFirstFill = true;
}

static void EnsureProbes()
{
	if ( s_bProbesAllocated )
		return;
	s_bProbesAllocated = true;

	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		cas.pProbes = new GIProbe_t[ GI_NUM_PROBES ];
		memset( cas.pProbes, 0, sizeof( GIProbe_t ) * GI_NUM_PROBES );

		cas.order.SetCount( GI_NUM_PROBES );
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
			cas.order[i] = i;
		s_flSortLevel = s_flCameraLevel[c];
		cas.order.Sort( OrderSort );
	}

	InitRayDirs();
	DeferredGI_Reset();
}

static void ClearProbeLight( GIProbe_t &p )
{
	p.nBack = 0;
	p.nStartSolid = 0;
	for ( int i = 0; i < 4; i++ )
	{
		p.visSum[i] = 0.0f;
		p.vis[i] = 0.0f;
		p.visF[i] = 0.0f;
		p.sun[i] = 0.0f;
		p.sky[i] = 0.0f;
	}
	p.vecSunChroma.Init( 1, 1, 1 );
	p.vecSkyChroma.Init( 1, 1, 1 );
}

static void ResetProbe( GICascade_t &cas, GIProbe_t &p, int cx, int cy, int cz )
{
	// (counted as pending: no first pass yet, or starting over)
	const bool bWasPending = p.bAssigned && ( !p.bTraced || p.bRetrace );
	if ( !bWasPending )
		cas.nPending++;
	p.cell[0] = cx;
	p.cell[1] = cy;
	p.cell[2] = cz;
	p.bAssigned = true;
	p.bTraced = false;
	p.bSunValid = false;
	p.bDirty = true;	// upload as invalid until traced
	p.bQueued = false;
	p.bRetrace = false;
	p.bAlbedoPending = false;
	p.nPasses = 0;
	p.flFade = 0.0f;
	p.flValid = 0.0f;
	p.nHits = 0;
	ClearProbeLight( p );
	for ( int i = 0; i < 6; i++ )
		p.freeDist[i] = 0.0f;
}

// keeps the camera within a cell of its place in the grid; returns whether the grid moved
static bool UpdateGrid( int iCascade, const Vector &vecCamera )
{
	GICascade_t &cas = s_Cascades[ iCascade ];
	const float S = cas.flSpacing;
	const float g[3] = { vecCamera.x / S - GI_OFFSET, vecCamera.y / S - GI_OFFSET, vecCamera.z / S - GI_OFFSET };
	const int dims[3] = { GI_NX, GI_NY, GI_NZ };

	int iMin[3];
	bool bMove = !cas.bGridValid;
	for ( int a = 0; a < 3; a++ )
	{
		const float flHalf = ( a == 2 ) ? s_flCameraLevel[ iCascade ] : ( dims[a] - 1 ) * 0.5f;
		iMin[a] = cas.iGridMin[a];
		if ( !cas.bGridValid || fabsf( g[a] - ( cas.iGridMin[a] + flHalf ) ) > 1.0f )
		{
			iMin[a] = (int)floorf( g[a] - flHalf + 0.5f );
			bMove = bMove || iMin[a] != cas.iGridMin[a];
		}
	}

	if ( !bMove )
		return false;

	for ( int a = 0; a < 3; a++ )
		cas.iGridMin[a] = iMin[a];
	cas.bGridValid = true;

	for ( int z = 0; z < GI_NZ; z++ )
		for ( int y = 0; y < GI_NY; y++ )
			for ( int x = 0; x < GI_NX; x++ )
			{
				const int cx = iMin[0] + x, cy = iMin[1] + y, cz = iMin[2] + z;
				GIProbe_t &p = cas.pProbes[ SlotIndex( cx, cy, cz ) ];
				if ( !p.bAssigned || p.cell[0] != cx || p.cell[1] != cy || p.cell[2] != cz )
					ResetProbe( cas, p, cx, cy, cz );
			}
	// (new probes: the ones around them gather again)
	cas.bRefined = false;
	cas.nSweepsLeft = Max( cas.nSweepsLeft, 2 );
	return true;
}

// the probe of a world cell, if the grid holds it and it can light
static inline const GIProbe_t *LitProbeAt( const GICascade_t &cas, int cx, int cy, int cz )
{
	const GIProbe_t &p = cas.pProbes[ SlotIndex( cx, cy, cz ) ];
	if ( !p.bAssigned || p.cell[0] != cx || p.cell[1] != cy || p.cell[2] != cz || !p.bTraced || p.flValid <= 0.0f )
		return NULL;
	return &p;
}

//-----------------------------------------------------------------------------
// Tracing: the worker part (any thread - traces of the world and of brush models
// only, nothing shared is written)
//-----------------------------------------------------------------------------
static inline float Lum( const Vector &c )
{
	return c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
}

// the brush entities a probe's rays can meet
struct GICandidates_t
{
	int n;
	const GIBrushEnt_t *p[ GI_MAX_CANDIDATES ];
};

static void FindCandidates( const Vector &vecCenter, float flRadius, GICandidates_t &cand )
{
	cand.n = 0;
	const int nEnts = s_BrushEnts.Count();
	for ( int i = 0; i < nEnts && cand.n < GI_MAX_CANDIDATES; i++ )
	{
		const CGIBrushCollide &col = s_BrushEnts[i]->collide;
		// (distance from the sphere's center to the box)
		float flDistSqr = 0.0f;
		for ( int a = 0; a < 3; a++ )
		{
			const float d = ( vecCenter[a] < col.m_vecWorldMins[a] ) ? col.m_vecWorldMins[a] - vecCenter[a]
				: ( ( vecCenter[a] > col.m_vecWorldMaxs[a] ) ? vecCenter[a] - col.m_vecWorldMaxs[a] : 0.0f );
			flDistSqr += d * d;
		}
		if ( flDistSqr <= flRadius * flRadius )
			cand.p[ cand.n++ ] = s_BrushEnts[i];
	}
}

static inline void GITrace( const Vector &vecStart, const Vector &vecEnd, trace_t &tr, const GICandidates_t &cand )
{
	// (TRACE_WORLD_ONLY: the BSP alone - its trace keeps its state per thread)
	CTraceFilterWorldOnly filter;
	Ray_t ray;
	ray.Init( vecStart, vecEnd );
	enginetrace->TraceRay( ray, MASK_OPAQUE, &filter, &tr );

	// ...and the brush entities in the way: clipped by the same code as the world
	for ( int i = 0; i < cand.n; i++ )
	{
		if ( tr.startsolid )
			return;
		const CGIBrushCollide &col = cand.p[i]->collide;
		const Vector vecDelta = tr.endpos - vecStart;
		if ( !IsBoxIntersectingRay( col.m_vecWorldMins, col.m_vecWorldMaxs, vecStart, vecDelta ) )
			continue;
		Ray_t rayEnt;
		rayEnt.Init( vecStart, tr.endpos );
		trace_t trEnt;
		enginetrace->ClipRayToCollideable( rayEnt, MASK_OPAQUE, const_cast< CGIBrushCollide * >( &col ), &trEnt );
		if ( trEnt.startsolid || trEnt.allsolid )
		{
			tr = trEnt;
			tr.startsolid = true;
			return;
		}
		if ( trEnt.fraction < 1.0f )
		{
			// (its fraction is of the shortened ray)
			const float flFraction = trEnt.fraction * tr.fraction;
			tr = trEnt;
			tr.fraction = flFraction;
		}
	}
}

static inline bool IsSkyHit( const trace_t &tr )
{
	return tr.fraction >= 1.0f || ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) != 0;
}

static inline bool IsSolidPoint( const Vector &vecPos, const GICandidates_t &cand )
{
	trace_t tr;
	GITrace( vecPos, vecPos + Vector( 0.0f, 0.0f, 0.25f ), tr, cand );
	return tr.startsolid || tr.allsolid;
}

static inline void HitNormal( const GIHit_t &h, Vector &n )
{
	n.Init( h.n[0] * ( 1.0f / 127.0f ), h.n[1] * ( 1.0f / 127.0f ), h.n[2] * ( 1.0f / 127.0f ) );
}

static inline float RayLength( float flSpacing )
{
	return Min( GI_RAY_LENGTH * flSpacing / 64.0f, GI_RAY_LENGTH_MAX );
}

// one pass of rays; the first one also finds where the probe traces from
static void JobTracePass( GIJob_t &job, const GICandidates_t &cand )
{
	GIProbe_t &p = *job.pProbe;
	const float S = s_Cascades[ job.iCascade ].flSpacing;
	const Vector vecCell( ( p.cell[0] + GI_OFFSET ) * S, ( p.cell[1] + GI_OFFSET ) * S, ( p.cell[2] + GI_OFFSET ) * S );
	static const Vector s_Axes[6] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ), Vector( 0, 0, 1 ), Vector( 0, 0, -1 ) };

	const bool bFirst = p.nPasses == 0 || p.bRetrace || !p.bTraced;
	const float flRayLength = RayLength( S );
	trace_t tr;

	// (a probe starting over keeps what it has until the new first pass is complete: the
	// sums go into locals and replace the probe's at the end)
	float visSum[4] = { 0, 0, 0, 0 };
	int nBack = 0, nStartSolid = 0;
	int nHits = bFirst ? 0 : p.nHits;
	int nPasses = bFirst ? 0 : p.nPasses;
	Vector vecPos = p.vecTracePos;
	float freeDist[6];
	for ( int i = 0; i < 6; i++ )
		freeDist[i] = p.freeDist[i];
	job.iFirstNewHit = nHits;

	if ( bFirst )
	{
		// in a wall: the nearest free point within half a spacing (of the near grid) traces for it
		vecPos = vecCell;
		job.nTraces++;
		if ( IsSolidPoint( vecPos, cand ) )
		{
			const float flStep = Min( S, 64.0f ) * 0.22f;
			bool bFound = false;
			for ( int step = 1; step <= 2 && !bFound; step++ )
			{
				for ( int a = 0; a < 6; a++ )
				{
					const Vector vecTry = vecCell + s_Axes[a] * ( flStep * step );
					job.nTraces++;
					if ( !IsSolidPoint( vecTry, cand ) )
					{
						vecPos = vecTry;
						bFound = true;
						break;
					}
				}
			}
			if ( !bFound )
			{
				p.vecTracePos = vecCell;
				p.nHits = 0;
				p.nPasses = 1;
				p.flValid = 0.0f;
				ClearProbeLight( p );
				for ( int i = 0; i < 6; i++ )
					p.freeDist[i] = 0.0f;
				job.nInvalid = 1;
				return;
			}
		}

		// free distance along the axes: whether the probe sees a point of its 8 cells
		for ( int a = 0; a < 6; a++ )
		{
			GITrace( vecPos, vecPos + s_Axes[a] * S, tr, cand );
			job.nTraces++;
			freeDist[a] = ( tr.startsolid || tr.allsolid ) ? 0.0f : tr.fraction * S;
		}
	}
	else
	{
		for ( int i = 0; i < 4; i++ )
			visSum[i] = p.visSum[i];
		nBack = p.nBack;
		nStartSolid = p.nStartSolid;
	}

	// room for this pass' hits
	if ( nHits + GI_RAYS > p.nHitsAlloc )
	{
		const int nAlloc = nHits + GI_RAYS * 2;
		GIHit_t *pNew = (GIHit_t *)realloc( p.pHits, nAlloc * sizeof( GIHit_t ) );
		if ( !pNew )
		{
			job.bSkipped = true;
			return;
		}
		p.pHits = pNew;
		p.nHitsAlloc = nAlloc;
	}

	matrix3x4_t matRot;
	PassRotation( p, nPasses, matRot );

	int nNew = 0;
	for ( int k = 0; k < GI_RAYS; k++ )
	{
		Vector dir;
		VectorRotate( s_vecRayDir[k], matRot, dir );
		GITrace( vecPos, vecPos + dir * flRayLength, tr, cand );
		job.nTraces++;
		// (starting on a surface: the directions into it are simply blocked)
		if ( tr.startsolid || tr.allsolid )
		{
			nStartSolid++;
			continue;
		}
		if ( IsSkyHit( tr ) )
		{
			visSum[0] += 1.0f;
			visSum[1] += 2.0f * dir.x;
			visSum[2] += 2.0f * dir.y;
			visSum[3] += 2.0f * dir.z;
			continue;
		}
		// the back of a face: the probe is inside something closed (a sealed cavity)
		if ( DotProduct( tr.plane.normal, dir ) > 0.0f )
		{
			nBack++;
			continue;
		}
		GIHit_t &h = p.pHits[ nHits ];
		h.vecPos = tr.endpos + tr.plane.normal * 2.0f;
		for ( int a = 0; a < 3; a++ )
			h.n[a] = (signed char)clamp( (int)floorf( tr.plane.normal[a] * 127.0f + 0.5f ), -127, 127 );
		h.albedo[0] = h.albedo[1] = h.albedo[2] = 77;	// 0.3 until looked up
		h.bLit = 0;
		h.pad = 0;
		job.pszSurface[ nNew ] = ( tr.surface.flags & SURF_NODRAW ) ? NULL : tr.surface.name;
		nHits++;
		nNew++;
	}

	nPasses++;
	const float flInv = 1.0f / ( nPasses * GI_RAYS );
	const int nRays = nPasses * GI_RAYS;

	p.vecTracePos = vecPos;
	for ( int i = 0; i < 6; i++ )
		p.freeDist[i] = freeDist[i];
	for ( int i = 0; i < 4; i++ )
	{
		p.visSum[i] = visSum[i];
		p.vis[i] = visSum[i] * flInv;
	}
	if ( bFirst )
	{
		// (until the gather smooths it)
		for ( int i = 0; i < 4; i++ )
			p.visF[i] = p.vis[i];
	}
	p.nBack = nBack;
	p.nStartSolid = nStartSolid;
	p.nHits = nHits;
	p.nPasses = (unsigned char)nPasses;
	p.flValid = ( nBack * 4 > nRays || nStartSolid * 4 > nRays * 3 ) ? 0.0f : 1.0f;
	if ( p.flValid <= 0.0f )
		job.nInvalid = 2;
}

// the sun rays of the hits from iFirst on
static void JobTraceSun( GIJob_t &job, const GICandidates_t &cand, int iFirst )
{
	GIProbe_t &p = *job.pProbe;
	const bool bSun = job.bLight && p.flValid > 0.0f && job.vecLight.z > -0.1f;
	trace_t tr;
	for ( int i = iFirst; i < p.nHits; i++ )
	{
		GIHit_t &h = p.pHits[i];
		h.bLit = 0;
		if ( !bSun )
			continue;
		Vector n;
		HitNormal( h, n );
		if ( DotProduct( n, job.vecLight ) <= 0.01f )
			continue;
		GITrace( h.vecPos, h.vecPos + job.vecLight * GI_SUN_RAY_LENGTH, tr, cand );
		job.nTraces++;
		h.bLit = ( !tr.startsolid && IsSkyHit( tr ) ) ? 1 : 0;
	}
}

//-----------------------------------------------------------------------------
// The indirect light of a probe from its hits (any thread: reads the other probes
// as they are - a value another worker is just writing is as good as the one before)
//-----------------------------------------------------------------------------

// what the probes give a surface point: the sky it sees, the indirect light of the sun and
// of the sky at it. False: no probe there (beyond the grids).
static bool FieldLookup( const Vector &vecPos, const Vector &vecNormal, float &flVis, Vector &vecSun, Vector &vecSky )
{
	const int nCascades = r_deferred_gi_far.GetBool() ? GI_CASCADES : 1;
	for ( int c = 0; c < nCascades; c++ )
	{
		const GICascade_t &cas = s_Cascades[c];
		if ( !cas.bGridValid || !cas.pProbes )
			continue;
		const float S = cas.flSpacing;
		const float flInvS = 1.0f / S;
		const Vector Ps = vecPos + vecNormal * ( 0.4f * S );
		const float g[3] = { Ps.x * flInvS - GI_OFFSET, Ps.y * flInvS - GI_OFFSET, Ps.z * flInvS - GI_OFFSET };
		int base[3];
		float f[3];
		bool bInside = true;
		static const int s_iDims[3] = { GI_NX, GI_NY, GI_NZ };
		// (the slots of the cell's corners: two along every axis - the grid is toroidal)
		int slot[3][2];
		for ( int a = 0; a < 3; a++ )
		{
			base[a] = (int)g[a];
			if ( g[a] < base[a] )
				base[a]--;
			f[a] = g[a] - base[a];
			bInside = bInside && base[a] >= cas.iGridMin[a] && base[a] + 1 <= cas.iGridMin[a] + s_iDims[a] - 1;
			slot[a][0] = PosMod( base[a], s_iDims[a] );
			slot[a][1] = ( slot[a][0] + 1 == s_iDims[a] ) ? 0 : slot[a][0] + 1;
		}
		if ( !bInside )
			continue;

		float flWeight = 0.0f, flSumVis = 0.0f;
		Vector vecSumSun( 0, 0, 0 ), vecSumSky( 0, 0, 0 );
		for ( int k = 0; k < 8; k++ )
		{
			const int kx = k & 1, ky = ( k >> 1 ) & 1, kz = ( k >> 2 ) & 1;
			float w = ( kx ? f[0] : 1.0f - f[0] ) * ( ky ? f[1] : 1.0f - f[1] ) * ( kz ? f[2] : 1.0f - f[2] );
			if ( w < 0.01f )
				continue;
			const GIProbe_t &q = cas.pProbes[ slot[0][kx] + GI_NX * ( slot[1][ky] + GI_NY * slot[2][kz] ) ];
			const int idx[3] = { base[0] + kx, base[1] + ky, base[2] + kz };
			if ( !q.bAssigned || !q.bTraced || q.flValid <= 0.0f || q.cell[0] != idx[0] || q.cell[1] != idx[1] || q.cell[2] != idx[2] )
				continue;
			// (the probe behind the surface lights the other side of the wall)
			const Vector vecToProbe( ( idx[0] + GI_OFFSET ) * S - vecPos.x, ( idx[1] + GI_OFFSET ) * S - vecPos.y, ( idx[2] + GI_OFFSET ) * S - vecPos.z );
			if ( DotProduct( vecToProbe, vecNormal ) < 0.0f )
				w *= 0.05f;
			w += 0.0005f;

			flSumVis += w * clamp( q.visF[0] + q.visF[1] * vecNormal.x + q.visF[2] * vecNormal.y + q.visF[3] * vecNormal.z, 0.0f, 1.0f );
			// (half the directional part: L1 overshoots toward a small bright source, and what
			// overshoots here comes back with the next round)
			const float flSun = Max( 0.0f, q.sun[0] + 0.5f * ( q.sun[1] * vecNormal.x + q.sun[2] * vecNormal.y + q.sun[3] * vecNormal.z ) );
			const float flSky = Max( 0.0f, q.sky[0] + 0.5f * ( q.sky[1] * vecNormal.x + q.sky[2] * vecNormal.y + q.sky[3] * vecNormal.z ) );
			vecSumSun += q.vecSunChroma * ( w * flSun );
			vecSumSky += q.vecSkyChroma * ( w * flSky );
			flWeight += w;
		}
		if ( flWeight < 0.004f )
			continue;
		const float flInv = 1.0f / flWeight;
		flVis = flSumVis * flInv;
		vecSun = vecSumSun * flInv;
		vecSky = vecSumSky * flInv;
		for ( int a = 0; a < 3; a++ )
		{
			vecSun[a] = Min( vecSun[a], 1.5f );
			vecSky[a] = Min( vecSky[a], 1.5f );
		}
		return true;
	}
	return false;
}

static void GatherProbe( int iCascade, GIProbe_t &p, const Vector &vecLight, bool bLight )
{
	const GICascade_t &cas = s_Cascades[ iCascade ];
	if ( !p.bTraced || p.flValid <= 0.0f || p.nPasses == 0 )
		return;

	// the sky visibility, smoothed: the probe's own (twice) and the 6 neighbours it sees
	if ( r_deferred_gi_filter.GetBool() )
	{
		static const int s_iStep[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		float sum[4] = { p.vis[0] * 2.0f, p.vis[1] * 2.0f, p.vis[2] * 2.0f, p.vis[3] * 2.0f };
		float flWeight = 2.0f;
		for ( int a = 0; a < 6; a++ )
		{
			if ( p.freeDist[a] < cas.flSpacing * 0.97f )
				continue;
			const GIProbe_t *q = LitProbeAt( cas, p.cell[0] + s_iStep[a][0], p.cell[1] + s_iStep[a][1], p.cell[2] + s_iStep[a][2] );
			if ( !q )
				continue;
			for ( int i = 0; i < 4; i++ )
				sum[i] += q->vis[i];
			flWeight += 1.0f;
		}
		for ( int i = 0; i < 4; i++ )
			p.visF[i] = sum[i] / flWeight;
	}
	else
	{
		for ( int i = 0; i < 4; i++ )
			p.visF[i] = p.vis[i];
	}

	const bool bSun = bLight && vecLight.z > -0.1f;
	const bool bMulti = r_deferred_gi_multibounce.GetBool();

	float sunSH[4] = { 0, 0, 0, 0 }, skySH[4] = { 0, 0, 0, 0 };
	Vector vecSunRGB( 0, 0, 0 ), vecSkyRGB( 0, 0, 0 );
	for ( int i = 0; i < p.nHits; i++ )
	{
		const GIHit_t &h = p.pHits[i];
		Vector n;
		HitNormal( h, n );
		const Vector vecAlbedo( h.albedo[0] * ( 1.0f / 255.0f ), h.albedo[1] * ( 1.0f / 255.0f ), h.albedo[2] * ( 1.0f / 255.0f ) );

		// what reaches the hit: the sun, the sky it sees, the indirect light at it
		const float flLit = ( bSun && h.bLit ) ? Max( 0.0f, DotProduct( n, vecLight ) ) : 0.0f;
		float flVisHit = 0.0f;
		Vector vecIndSun( 0, 0, 0 ), vecIndSky( 0, 0, 0 );
		if ( !FieldLookup( h.vecPos, n, flVisHit, vecIndSun, vecIndSky ) )
		{
			// (beyond the grids: an open hemisphere above the surface)
			flVisHit = 0.5f + 0.5f * n.z;
		}
		if ( !bMulti )
		{
			vecIndSun.Init();
			vecIndSky.Init();
		}

		const Vector vecOutSun( vecAlbedo.x * ( flLit + vecIndSun.x ), vecAlbedo.y * ( flLit + vecIndSun.y ), vecAlbedo.z * ( flLit + vecIndSun.z ) );
		const Vector vecOutSky( vecAlbedo.x * ( flVisHit + vecIndSky.x ), vecAlbedo.y * ( flVisHit + vecIndSky.y ), vecAlbedo.z * ( flVisHit + vecIndSky.z ) );

		Vector dir = h.vecPos - p.vecTracePos;
		const float flLen = dir.Length();
		if ( flLen < 0.01f )
			continue;
		dir *= 1.0f / flLen;

		const float fSun = Lum( vecOutSun ), fSky = Lum( vecOutSky );
		sunSH[0] += fSun;
		sunSH[1] += 2.0f * fSun * dir.x;
		sunSH[2] += 2.0f * fSun * dir.y;
		sunSH[3] += 2.0f * fSun * dir.z;
		skySH[0] += fSky;
		skySH[1] += 2.0f * fSky * dir.x;
		skySH[2] += 2.0f * fSky * dir.y;
		skySH[3] += 2.0f * fSky * dir.z;
		vecSunRGB += vecOutSun;
		vecSkyRGB += vecOutSky;
	}

	const float flInv = 1.0f / ( p.nPasses * GI_RAYS );
	for ( int i = 0; i < 4; i++ )
	{
		p.sun[i] = sunSH[i] * flInv;
		p.sky[i] = skySH[i] * flInv;
	}
	const float flLumSun = Lum( vecSunRGB ), flLumSky = Lum( vecSkyRGB );
	p.vecSunChroma = ( flLumSun > 1e-5f ) ? vecSunRGB / flLumSun : Vector( 1, 1, 1 );
	p.vecSkyChroma = ( flLumSky > 1e-5f ) ? vecSkyRGB / flLumSky : Vector( 1, 1, 1 );
	p.bDirty = true;
}

// The time the workers have this frame. A probe costs anything between a few point tests (in
// the solid) and a millisecond (long rays through a detailed map): a batch sized by the average
// of the last ones ran up to ten times over its time now and then - a frame of 14 ms in a walk
// through demo_map. Each worker looks at the clock before it starts a probe instead; what is
// left stays pending.
static double s_flJobDeadline = 0.0;
// (how far the sun may have moved since a probe's sun rays for them to stand)
static float s_flJobSunCos = 1.0f;

static void JobProcess( GIJob_t &job )
{
	if ( job.iType == GIJOB_GATHER )
	{
		GatherProbe( job.iCascade, *job.pProbe, job.vecLight, job.bLight );
		return;
	}

	if ( Plat_FloatTime() > s_flJobDeadline )
	{
		job.bSkipped = true;
		return;
	}

	GIProbe_t &p = *job.pProbe;
	const float S = s_Cascades[ job.iCascade ].flSpacing;
	GICandidates_t cand;
	// (the probe's rays, and the rays from where they end toward the sun)
	FindCandidates( Vector( ( p.cell[0] + GI_OFFSET ) * S, ( p.cell[1] + GI_OFFSET ) * S, ( p.cell[2] + GI_OFFSET ) * S ),
		RayLength( S ) + GI_SUN_RAY_LENGTH + S, cand );

	if ( job.iType == GIJOB_TRACE )
	{
		// (a pass after the first: the sun rays of its new hits only, while the sun stands
		// where it stood for the others)
		const bool bRestart = p.nPasses == 0 || p.bRetrace || !p.bTraced;
		JobTracePass( job, cand );
		if ( !job.bSkipped && job.nInvalid != 1 )
		{
			job.bSunAll = bRestart || !p.bSunValid || DotProduct( p.vecSunDir, job.vecLight ) < s_flJobSunCos;
			JobTraceSun( job, cand, job.bSunAll ? 0 : job.iFirstNewHit );
		}
	}
	else
	{
		job.bSunAll = true;
		JobTraceSun( job, cand, 0 );
	}
}

//-----------------------------------------------------------------------------
// Tracing: the main thread part
//-----------------------------------------------------------------------------
// average color of a surface (vtex stores it in every VTF)
static Vector SurfaceAlbedo( const char *pszSurface )
{
	Vector vecAlbedo( 0.3f, 0.3f, 0.3f );
	if ( !pszSurface || !*pszSurface || pszSurface[0] == '*' )
		return vecAlbedo;

	const uintp key = (uintp)pszSurface;
	const unsigned short idx = s_AlbedoCache.Find( key );
	if ( idx != s_AlbedoCache.InvalidIndex() )
		return s_AlbedoCache[ idx ];

	// (only a material the map has loaded anyway: looking up one that isn't - the cubemap
	// patched water materials of a map compiled without them - reads the disk and fails, a few
	// milliseconds each, in the middle of a frame)
	if ( materials->IsMaterialLoaded( pszSurface ) )
	{
		IMaterial *pMat = materials->FindMaterial( pszSurface, TEXTURE_GROUP_WORLD, false );
		if ( pMat && !pMat->IsErrorMaterial() )
		{
			pMat->GetReflectivity( vecAlbedo );
			// (no surface throws back more than ~80 %: whiter than that and the bounces never end)
			for ( int i = 0; i < 3; i++ )
				vecAlbedo[i] = clamp( vecAlbedo[i], 0.02f, 0.8f );
		}
	}
	s_AlbedoCache.Insert( key, vecAlbedo );
	return vecAlbedo;
}

// what the job left in the probe -> counters, flags; true: the probe's light has to be gathered
static bool FinishJob( GICascade_t &cas, GIJob_t &job, bool bInstant )
{
	GIProbe_t &p = *job.pProbe;
	p.bQueued = false;
	if ( job.bSkipped )
		return false;

	s_nTracesLastFrame += job.nTraces;

	if ( job.iType == GIJOB_TRACE )
	{
		for ( int i = job.iFirstNewHit, k = 0; i < p.nHits && k < GI_RAYS; i++, k++ )
		{
			const Vector vecAlbedo = SurfaceAlbedo( job.pszSurface[k] );
			for ( int a = 0; a < 3; a++ )
				p.pHits[i].albedo[a] = (unsigned char)clamp( (int)( vecAlbedo[a] * 255.0f + 0.5f ), 0, 255 );
		}

		const bool bFirstPass = !p.bTraced || p.bRetrace;
		if ( bFirstPass )
		{
			cas.nPending--;
			if ( job.nInvalid == 1 )
				s_nInvalidSolid++;
			else if ( job.nInvalid == 2 )
				s_nInvalidClosed++;

			// (it fades in; the probes of a map's first frame, and one starting over, are simply there)
			if ( !p.bTraced )
			{
				p.flFade = ( bInstant || r_deferred_gi_fade.GetFloat() <= 0.0f ) ? 1.0f : 0.0f;
				if ( p.flFade < 1.0f && !p.bFading )
				{
					p.bFading = true;
					s_Fading.AddToTail( &p );
				}
			}
			p.bTraced = true;
			p.bRetrace = false;
		}
	}

	if ( job.bSunAll )
	{
		p.vecSunDir = job.vecLight;
		p.bSunValid = true;
	}
	p.bDirty = true;
	return p.flValid > 0.0f;
}

static inline GIProbe_t &OrderedProbe( GICascade_t &cas, int i )
{
	const int o = cas.order[i];
	const int cx = cas.iGridMin[0] + o % GI_NX, cy = cas.iGridMin[1] + ( o / GI_NX ) % GI_NY, cz = cas.iGridMin[2] + o / ( GI_NX * GI_NY );
	return cas.pProbes[ SlotIndex( cx, cy, cz ) ];
}

static inline void AddJob( CUtlVector< GIJob_t > &jobs, GIProbe_t &p, int iCascade, int iType, const Vector &vecLight, bool bLight )
{
	p.bQueued = true;
	GIJob_t &job = jobs[ jobs.AddToTail() ];
	job.pProbe = &p;
	job.iCascade = iCascade;
	job.iType = iType;
	job.vecLight = vecLight;
	job.bLight = bLight;
	job.nTraces = 0;
	job.nInvalid = 0;
	job.bSkipped = false;
	job.bSunAll = false;
	job.iFirstNewHit = 0;
}

// probes of one grid without their first pass, nearest first, until the batch holds nLimit jobs
static void CollectPending( CUtlVector< GIJob_t > &jobs, int c, int nLimit, const Vector &vecLight, bool bLight )
{
	GICascade_t &cas = s_Cascades[c];
	if ( cas.nPending <= 0 || !cas.bGridValid )
		return;
	for ( int i = 0; i < GI_NUM_PROBES && jobs.Count() < nLimit; i++ )
	{
		GIProbe_t &p = OrderedProbe( cas, i );
		if ( ( p.bTraced && !p.bRetrace ) || p.bQueued )
			continue;
		AddJob( jobs, p, c, GIJOB_TRACE, vecLight, bLight );
	}
}

static inline int TargetPasses( int iCascade )
{
	const float flRays = ( iCascade == 0 ) ? r_deferred_gi_rays.GetFloat() : r_deferred_gi_rays_far.GetFloat();
	return clamp( (int)( flRays / GI_RAYS + 0.5f ), 1, (int)GI_MAX_PASSES );
}

// up to nMax probes with work to do, the most wanted first: first passes of the near grid
// (nearest first; a quarter of the batch is kept for the far grid, which a walking player would
// starve otherwise), of the far grid, the sun rays of probes the sun has moved away from, then
// further passes (nearest first)
static void CollectJobs( CUtlVector< GIJob_t > &jobs, int nMax, const Vector &vecLight, bool bLight, float flSunCos )
{
	const int nCascades = r_deferred_gi_far.GetBool() ? GI_CASCADES : 1;

	const bool bFarWork = nCascades > 1 && s_Cascades[1].nPending > 0 && s_Cascades[1].bGridValid;
	CollectPending( jobs, 0, bFarWork ? nMax - nMax / 4 : nMax, vecLight, bLight );
	if ( nCascades > 1 )
		CollectPending( jobs, 1, nMax, vecLight, bLight );
	CollectPending( jobs, 0, nMax, vecLight, bLight );

	for ( int c = 0; c < nCascades && jobs.Count() < nMax; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		if ( !cas.bGridValid || cas.nPending > 0 )
			continue;
		if ( cas.bSunDone && DotProduct( cas.vecSunDone, vecLight ) < flSunCos )
			cas.bSunDone = false;
		if ( cas.bSunDone )
			continue;

		bool bAll = true;
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			GIProbe_t &p = OrderedProbe( cas, i );
			if ( !p.bTraced || p.bQueued || p.nHits == 0 || ( p.bSunValid && DotProduct( p.vecSunDir, vecLight ) >= flSunCos ) )
				continue;
			if ( jobs.Count() >= nMax )
			{
				bAll = false;
				break;
			}
			AddJob( jobs, p, c, GIJOB_SUN, vecLight, bLight );
		}
		if ( bAll )
		{
			// (true once the jobs collected here are done; the caller takes it back if the
			// frame's time ran out before they all were)
			cas.bSunDone = true;
			cas.vecSunDone = vecLight;
		}
	}

	for ( int c = 0; c < nCascades && jobs.Count() < nMax; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		if ( !cas.bGridValid || cas.nPending > 0 || cas.bRefined )
			continue;
		const int nTarget = TargetPasses( c );
		bool bAll = true;
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			GIProbe_t &p = OrderedProbe( cas, i );
			if ( !p.bTraced || p.bQueued || p.flValid <= 0.0f || p.nPasses >= nTarget )
				continue;
			// (nothing but sky around it: two passes tell as much as eight)
			if ( p.nHits == 0 && p.nPasses >= 2 )
				continue;
			if ( jobs.Count() >= nMax )
			{
				bAll = false;
				break;
			}
			AddJob( jobs, p, c, GIJOB_TRACE, vecLight, bLight );
		}
		if ( bAll )
			cas.bRefined = true;	// (taken back if a job of it was skipped)
	}
}

// The engine's pool has 3 threads whatever the CPU. A pool of the GI's own with a thread for
// every spare core is there to try (r_deferred_gi_threads 2), but measured it doesn't pay: on
// demo_map (8 logical cores) 3 + 1 threads trace 33 - 47 probes a millisecond, 6 + 1 threads
// 23 - 27, the main thread alone 17 - 19 - the batches of a millisecond are too short for more
// threads to wake up for, and the traces share the cache.
static IThreadPool *s_pGIPool = NULL;
static int s_nGIPoolThreads = 0;

static void ShutdownGIPool()
{
	if ( s_pGIPool )
	{
		s_pGIPool->Stop();
		DestroyThreadPool( s_pGIPool );
		s_pGIPool = NULL;
	}
	s_nGIPoolThreads = 0;
}

// the pool to trace on (NULL: the main thread alone)
static IThreadPool *GetGIPool()
{
	const int iMode = r_deferred_gi_threads.GetInt();
	if ( iMode <= 0 )
		return NULL;
	if ( iMode == 1 )
	{
		if ( s_pGIPool )
			ShutdownGIPool();
		return ( g_pThreadPool && g_pThreadPool->NumThreads() > 0 ) ? g_pThreadPool : NULL;
	}

	const int nWanted = ( iMode == 2 ) ? clamp( (int)GetCPUInformation()->m_nLogicalProcessors - 2, 1, 12 ) : clamp( iMode, 1, 16 );
	if ( s_pGIPool && s_nGIPoolThreads != nWanted )
		ShutdownGIPool();
	if ( !s_pGIPool )
	{
		s_pGIPool = CreateThreadPool();
		ThreadPoolStartParams_t params;
		params.nThreads = nWanted;
		params.fDistribute = TRS_FALSE;
		params.bIOThreads = false;
		if ( !s_pGIPool || !s_pGIPool->Start( params, "HL2RPM_GI" ) )
		{
			if ( s_pGIPool )
				DestroyThreadPool( s_pGIPool );
			s_pGIPool = NULL;
			s_nGIPoolThreads = 0;
			return ( g_pThreadPool && g_pThreadPool->NumThreads() > 0 ) ? g_pThreadPool : NULL;
		}
		s_nGIPoolThreads = nWanted;
	}
	return s_pGIPool;
}

static void RunJobs( CUtlVector< GIJob_t > &jobs, IThreadPool *pPool )
{
	if ( pPool && jobs.Count() > 1 )
	{
		CParallelProcessor< GIJob_t, CFuncJobItemProcessor< GIJob_t > > processor( "DeferredGI" );
		processor.m_ItemProcessor.Init( &JobProcess, NULL, NULL );
		processor.Run( jobs.Base(), jobs.Count(), INT_MAX, pPool );
	}
	else
	{
		for ( int i = 0; i < jobs.Count(); i++ )
			JobProcess( jobs[i] );
	}
}

// a round of gathering over part of a grid (nCount probes from the cursor on)
static int CollectSweep( CUtlVector< GIJob_t > &jobs, int c, int nCount, const Vector &vecLight, bool bLight )
{
	GICascade_t &cas = s_Cascades[c];
	if ( !cas.bGridValid || !cas.pProbes || cas.nSweepsLeft <= 0 )
		return 0;
	int nAdded = 0;
	for ( int i = 0; i < nCount; i++ )
	{
		GIProbe_t &p = cas.pProbes[ cas.iSweepCursor ];
		if ( ++cas.iSweepCursor >= GI_NUM_PROBES )
		{
			cas.iSweepCursor = 0;
			if ( --cas.nSweepsLeft <= 0 )
			{
				i = nCount;	// (this probe yet)
			}
		}
		if ( !p.bAssigned || !p.bTraced || p.flValid <= 0.0f || p.bQueued )
			continue;
		AddJob( jobs, p, c, GIJOB_GATHER, vecLight, bLight );
		nAdded++;
	}
	return nAdded;
}

static void TraceProbes( const Vector &vecLight, bool bLight )
{
	const double flStart = Plat_FloatTime();
	const float flSunCos = cosf( DEG2RAD( r_deferred_gi_sun_refresh.GetFloat() ) );
	const int nCascades = r_deferred_gi_far.GetBool() ? GI_CASCADES : 1;

	int nPending = 0;
	for ( int c = 0; c < GI_CASCADES; c++ )
		nPending += s_Cascades[c].nPending;

	// the first frame of a map: everything around the camera at once (nobody sees this frame
	// take longer); else the budget - and more of it the more is missing (a walking player
	// uncovers ~900 probes a second, a teleport all of them), up to the catch-up time
	const bool bFirstFill = s_bFirstFill;
	float flBudgetMs = r_deferred_gi_budget_ms.GetFloat();
	if ( bFirstFill )
		flBudgetMs = Max( flBudgetMs, r_deferred_gi_load_ms.GetFloat() );
	else
	{
		const float flBacklog = s_Cascades[0].nPending + s_Cascades[1].nPending * 0.5f;
		flBudgetMs = Min( Max( flBudgetMs, r_deferred_gi_catchup_ms.GetFloat() ), flBudgetMs * ( 1.0f + flBacklog / 500.0f ) );
	}
	const double flEnd = flStart + flBudgetMs * 0.001;

	IThreadPool *pPool = GetGIPool();

	s_nTracesLastFrame = 0;
	int nProbes = 0, nRefine = 0, nSun = 0, nGather = 0;

	CUtlVector< GIJob_t > jobs;
	CUtlVector< GIJob_t > gather;
	for ( ;; )
	{
		const double flNow = Plat_FloatTime();
		const float flLeftMs = (float)( ( flEnd - flNow ) * 1000.0 );
		if ( flLeftMs <= 0.02f )
			break;

		// about twice what the workers would get done in the time left: they stop at the
		// deadline themselves (JobProcess), what they didn't reach stays pending
		const int nBatch = clamp( (int)( s_flProbesPerMs * flLeftMs * 2.0f ), 16, 1024 );
		jobs.RemoveAll();
		CollectJobs( jobs, nBatch, vecLight, bLight, flSunCos );
		if ( jobs.Count() == 0 )
			break;

		s_flJobDeadline = flEnd;
		s_flJobSunCos = flSunCos;
		RunJobs( jobs, pPool );

		int nDone = 0;
		gather.RemoveAll();
		for ( int i = 0; i < jobs.Count(); i++ )
		{
			GIJob_t &job = jobs[i];
			GICascade_t &cas = s_Cascades[ job.iCascade ];
			const bool bFirstPass = job.iType == GIJOB_TRACE && ( !job.pProbe->bTraced || job.pProbe->bRetrace );
			const bool bGather = FinishJob( cas, job, bFirstFill );
			if ( job.bSkipped )
			{
				// (left undone: this grid is not "done for this sun" / "refined" yet)
				if ( job.iType == GIJOB_SUN )
					cas.bSunDone = false;
				else if ( !bFirstPass )
					cas.bRefined = false;
				continue;
			}
			nDone++;
			if ( job.iType == GIJOB_SUN )
				nSun++;
			else if ( bFirstPass )
				nProbes++;
			else
				nRefine++;
			cas.nSweepsLeft = Max( cas.nSweepsLeft, 3 );
			if ( bGather )
				AddJob( gather, *job.pProbe, job.iCascade, GIJOB_GATHER, vecLight, bLight );
		}

		// the light of what was just traced, right away (cheap next to the rays)
		if ( gather.Count() )
		{
			RunJobs( gather, pPool );
			for ( int i = 0; i < gather.Count(); i++ )
				gather[i].pProbe->bQueued = false;
			nGather += gather.Count();
		}

		const float flBatchMs = (float)( ( Plat_FloatTime() - flNow ) * 1000.0 );
		if ( flBatchMs > 0.01f && nDone > 0 )
		{
			const float flRate = nDone / flBatchMs;
			s_flProbesPerMs = clamp( s_flProbesPerMs * 0.7f + flRate * 0.3f, 1.0f, 400.0f );
		}
	}

	// The light goes round: every probe gathers again from the others as they are now - one
	// more bounce each round. The first frame of a map does its rounds at once; later a round
	// takes what the tracing left of the frame's time (a little of it at least, or it would
	// never end while probes are being traced).
	const int nRounds = bFirstFill ? 4 : 1;
	for ( int r = 0; r < nRounds; r++ )
	{
		const double flRoundStart = Plat_FloatTime();
		int nCount = GI_NUM_PROBES;
		if ( !bFirstFill )
		{
			const float flLeftMs = Max( (float)( ( flEnd - flRoundStart ) * 1000.0 ), 0.12f );
			nCount = clamp( (int)( s_flGathersPerMs * flLeftMs ), 16, GI_NUM_PROBES / 16 );
		}
		gather.RemoveAll();
		for ( int c = 0; c < nCascades; c++ )
		{
			if ( bFirstFill )
			{
				s_Cascades[c].nSweepsLeft = 1;
				s_Cascades[c].iSweepCursor = 0;
			}
			// (the grids in turn; a probe that can't light costs nothing)
			const int nAdded = CollectSweep( gather, c, bFirstFill ? GI_NUM_PROBES : Max( 16, nCount - gather.Count() ) * 2, vecLight, bLight );
			if ( !bFirstFill && nAdded >= nCount )
				break;
		}
		if ( gather.Count() == 0 )
			break;
		RunJobs( gather, pPool );
		for ( int i = 0; i < gather.Count(); i++ )
			gather[i].pProbe->bQueued = false;
		nGather += gather.Count();

		const float flRoundMs = (float)( ( Plat_FloatTime() - flRoundStart ) * 1000.0 );
		if ( flRoundMs > 0.02f )
			s_flGathersPerMs = clamp( s_flGathersPerMs * 0.8f + ( gather.Count() / flRoundMs ) * 0.2f, 20.0f, 4000.0f );
	}
	if ( bFirstFill )
	{
		for ( int c = 0; c < nCascades; c++ )
			s_Cascades[c].nSweepsLeft = 2;
	}

	// (the first fill is over once it ran: what it didn't reach comes with the normal budget)
	if ( bFirstFill && nPending > 0 )
		s_bFirstFill = false;

	s_nProbesLastFrame = nProbes;
	s_nRefineLastFrame = nRefine;
	s_nSunLastFrame = nSun;
	s_nGatherLastFrame = nGather;
	s_flMsLastFrame = (float)( ( Plat_FloatTime() - flStart ) * 1000.0 );
	if ( !bFirstFill )
		s_flMsWorst = Max( s_flMsWorst, s_flMsLastFrame );
}

// probes fade in after they were traced
static void UpdateFades()
{
	const float flTime = r_deferred_gi_fade.GetFloat();
	const float flStep = ( flTime > 0.001f ) ? clamp( gpGlobals->absoluteframetime, 0.0f, 0.1f ) / flTime : 1.0f;
	for ( int i = s_Fading.Count() - 1; i >= 0; i-- )
	{
		GIProbe_t *p = s_Fading[i];
		p->flFade = Min( 1.0f, p->flFade + flStep );
		p->bDirty = true;
		if ( p->flFade >= 1.0f || !p->bTraced )
		{
			p->bFading = false;
			s_Fading.FastRemove( i );
		}
	}
}

// how much sky is in sight where the camera is (the probes of the near grid around it)
static void UpdateCameraSky( const Vector &vecCamera )
{
	const GICascade_t &cas = s_Cascades[0];
	if ( !cas.bGridValid )
		return;
	const float S = cas.flSpacing;
	const float g[3] = { vecCamera.x / S - GI_OFFSET, vecCamera.y / S - GI_OFFSET, vecCamera.z / S - GI_OFFSET };
	int base[3];
	float f[3];
	for ( int a = 0; a < 3; a++ )
	{
		base[a] = (int)floorf( g[a] );
		f[a] = g[a] - base[a];
	}
	float flSum = 0.0f, flWeight = 0.0f;
	for ( int c = 0; c < 8; c++ )
	{
		const int idx[3] = { base[0] + ( c & 1 ), base[1] + ( ( c >> 1 ) & 1 ), base[2] + ( ( c >> 2 ) & 1 ) };
		const GIProbe_t *p = LitProbeAt( cas, idx[0], idx[1], idx[2] );
		if ( !p )
			continue;
		const float w = ( ( c & 1 ) ? f[0] : 1.0f - f[0] ) * ( ( ( c >> 1 ) & 1 ) ? f[1] : 1.0f - f[1] ) * ( ( ( c >> 2 ) & 1 ) ? f[2] : 1.0f - f[2] ) + 0.01f;
		// (the average over the sphere: 0.5 with the whole sky in sight)
		flSum += w * clamp( p->visF[0] * 2.0f, 0.0f, 1.0f );
		flWeight += w;
	}
	if ( flWeight <= 0.0f )
		return;
	const float flTarget = flSum / flWeight;
	const float flBlend = 1.0f - expf( -clamp( gpGlobals->absoluteframetime, 0.0f, 0.2f ) / 0.4f );
	s_flCameraSky += ( flTarget - s_flCameraSky ) * flBlend;
}

float DeferredGI_GetCameraSky()
{
	return ( r_deferred_gi.GetBool() && s_bProbesAllocated && s_Cascades[0].bGridValid ) ? s_flCameraSky : 1.0f;
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

static void EmitProbe( CMeshBuilder &mb, int iSlot, const GIProbe_t &p, float flSpacing )
{
	const int sx = iSlot % GI_NX;
	const int sy = ( iSlot / GI_NX ) % GI_NY;
	const int sz = iSlot / ( GI_NX * GI_NY );
	const int u = sx + sz * GI_NX;

	const bool bValid = p.bTraced && p.flValid > 0.0f;
	float d[4];

	// 0: sky visibility
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.visF[i] : 0.0f;
	EmitTexel( mb, u, sy, d );

	// 1: indirect sun light (luminance)
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.sun[i] : 0.0f;
	EmitTexel( mb, u, sy + GI_NY, d );

	// 2: its color, weight (0 = in a wall / not traced yet; fading in after the trace)
	d[0] = p.vecSunChroma.x;
	d[1] = p.vecSunChroma.y;
	d[2] = p.vecSunChroma.z;
	d[3] = bValid ? clamp( p.flFade, 0.0f, 1.0f ) : 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 2, d );

	// 3: free distances, 5 bits each: +axis * 32 + -axis
	for ( int a = 0; a < 3; a++ )
	{
		const int qPos = clamp( (int)( p.freeDist[ a * 2 ] / flSpacing * 31.0f + 0.5f ), 0, 31 );
		const int qNeg = clamp( (int)( p.freeDist[ a * 2 + 1 ] / flSpacing * 31.0f + 0.5f ), 0, 31 );
		d[a] = (float)( qPos * 32 + qNeg );
	}
	d[3] = 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 3, d );

	// 4: bounced sky light (luminance)
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.sky[i] : 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 4, d );

	// 5: its color
	d[0] = p.vecSkyChroma.x;
	d[1] = p.vecSkyChroma.y;
	d[2] = p.vecSkyChroma.z;
	d[3] = 0.0f;
	EmitTexel( mb, u, sy + GI_NY * 5, d );
}

// At most nMax probes a frame, the nearest to the camera first; the rest stay dirty for the
// next frames. The quads go through the material system's call queue (mat_queue_mode 2), which
// keeps the vertices of a frame's dynamic meshes in a memory stack of 16 MB: the first frame of
// a map wrote every probe of both grids at once - 14 MB of vertices with the 32 x 32 grid and 6
// planes - and whatever was drawn after it in that frame ran the stack over ("failed to commit",
// a crash in materialsystem.dll during the first frame, now and then).
static void UploadProbes( GICascade_t &cas, int nMax )
{
	if ( !cas.texProbes.IsValid() || !cas.pProbes )
		return;

	CMatRenderContextPtr pRenderContext( materials );

	if ( cas.bClearAtlas )
	{
		pRenderContext->PushRenderTargetAndViewport( cas.texProbes, 0, 0, GI_ATLAS_W, GI_ATLAS_H );
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
		cas.bClearAtlas = false;
	}

	if ( cas.bReuploadAll )
	{
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			if ( cas.pProbes[i].bAssigned )
				cas.pProbes[i].bDirty = true;
		}
		cas.bReuploadAll = false;
	}

	CUtlVector< int > dirty;
	if ( cas.bGridValid )
	{
		for ( int i = 0; i < GI_NUM_PROBES && dirty.Count() < nMax; i++ )
		{
			const int o = cas.order[i];
			const int iSlot = SlotIndex( cas.iGridMin[0] + o % GI_NX, cas.iGridMin[1] + ( o / GI_NX ) % GI_NY, cas.iGridMin[2] + o / ( GI_NX * GI_NY ) );
			if ( cas.pProbes[ iSlot ].bDirty )
				dirty.AddToTail( iSlot );
		}
	}
	if ( dirty.Count() == 0 )
		return;

	pRenderContext->PushRenderTargetAndViewport( cas.texProbes, 0, 0, GI_ATLAS_W, GI_ATLAS_H );
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
			EmitProbe( mb, iSlot, cas.pProbes[ iSlot ], cas.flSpacing );
			cas.pProbes[ iSlot ].bDirty = false;
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
	const GICascade_t &cas = s_Cascades[0];
	const float flDuration = Max( 0.05f, gpGlobals->frametime * 1.5f );
	const Vector vecExt( 1.5f, 1.5f, 1.5f );
	for ( int i = 0; i < GI_NUM_PROBES; i++ )
	{
		const GIProbe_t &p = cas.pProbes[i];
		if ( !p.bAssigned )
			continue;
		const Vector vecPos( ( p.cell[0] + GI_OFFSET ) * cas.flSpacing, ( p.cell[1] + GI_OFFSET ) * cas.flSpacing, ( p.cell[2] + GI_OFFSET ) * cas.flSpacing );
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
				// sky visibility (average, 0.5 = open) in green, indirect sun in yellow, passes in blue
				const int sky = clamp( (int)( p.visF[0] * 2.0f * 255.0f ), 0, 255 );
				const int sun = clamp( (int)( p.sun[0] * 4.0f * 255.0f ), 0, 255 );
				r = sun; g = Max( sky, sun ); b = p.nPasses * 30;
			}
		}
		debugoverlay->AddBoxOverlay( vecPos, -vecExt, vecExt, vec3_angle, r, g, b, 255, flDuration );
	}
}

static void DeferredGI_Stats()
{
	if ( !s_bProbesAllocated )
	{
		Msg( "[gi] not started\n" );
		return;
	}
	int nHitsTotal = 0, nHitsAlloc = 0;
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		const GICascade_t &cas = s_Cascades[c];
		int nAssigned = 0, nTraced = 0, nValid = 0, nSun = 0, nPasses = 0, nFull = 0;
		const int nTarget = TargetPasses( c );
		for ( int i = 0; i < GI_NUM_PROBES; i++ )
		{
			const GIProbe_t &p = cas.pProbes[i];
			nAssigned += p.bAssigned ? 1 : 0;
			nTraced += p.bTraced ? 1 : 0;
			nSun += p.bSunValid ? 1 : 0;
			nHitsAlloc += p.nHitsAlloc;
			if ( p.bTraced && p.flValid > 0.0f )
			{
				nValid++;
				nPasses += p.nPasses;
				nFull += ( p.nPasses >= nTarget || ( p.nHits == 0 && p.nPasses >= 2 ) ) ? 1 : 0;
				nHitsTotal += p.nHits;
			}
		}
		Msg( "[gi] %s grid %d %d %d (spacing %.0f): %d probes, %d traced, %d valid (%d with all their %d rays, %.1f passes on average), %d sun, %d pending, sweeps left %d\n", c ? "far" : "near",
			cas.iGridMin[0], cas.iGridMin[1], cas.iGridMin[2], cas.flSpacing, nAssigned, nTraced, nValid, nFull, nTarget * GI_RAYS,
			nValid ? (float)nPasses / nValid : 0.0f, nSun, cas.nPending, cas.nSweepsLeft );
	}
	Msg( "[gi] last frame: %d new, %d refined, %d sun, %d gathered, %d traces, %.2f ms (worst since the map loaded %.2f ms); %d fading in; %.0f probes traced / %.0f gathered per ms on %d threads; sky at the camera %.2f\n",
		s_nProbesLastFrame, s_nRefineLastFrame, s_nSunLastFrame, s_nGatherLastFrame, s_nTracesLastFrame, s_flMsLastFrame, s_flMsWorst, s_Fading.Count(), s_flProbesPerMs, s_flGathersPerMs,
		GetGIPool() ? GetGIPool()->NumThreads() + 1 : 1, s_flCameraSky );
	Msg( "[gi] invalid since reset: %d in solid, %d closed (back faces); hits kept %d (%.1f MB allocated); brush entities blocking %d (%d tracked, %d changes)\n",
		s_nInvalidSolid, s_nInvalidClosed, nHitsTotal, nHitsAlloc * sizeof( GIHit_t ) / ( 1024.0f * 1024.0f ),
		s_BrushEnts.Count(), s_BrushTracks.Count(), s_nBrushInvalidations );

	// per height of the near grid: valid / traced, mean sky visibility and indirect light of the valid ones
	const GICascade_t &cas = s_Cascades[0];
	for ( int z = 0; z < GI_NZ; z++ )
	{
		int nT = 0, nV = 0;
		float flSky = 0.0f, flSun = 0.0f, flSkyB = 0.0f;
		for ( int y = 0; y < GI_NY; y++ )
		{
			for ( int x = 0; x < GI_NX; x++ )
			{
				const GIProbe_t &p = cas.pProbes[ SlotIndex( cas.iGridMin[0] + x, cas.iGridMin[1] + y, cas.iGridMin[2] + z ) ];
				if ( !p.bTraced )
					continue;
				nT++;
				if ( p.flValid > 0.0f )
				{
					nV++;
					flSky += p.visF[0];
					flSun += p.sun[0];
					flSkyB += p.sky[0];
				}
			}
		}
		Msg( "[gi]   z %6.0f: %3d / %3d valid, sky %.3f, indirect sun %.3f, bounced sky %.3f\n", ( cas.iGridMin[2] + z ) * cas.flSpacing, nV, nT,
			nV ? flSky / nV : 0.0f, nV ? flSun / nV : 0.0f, nV ? flSkyB / nV : 0.0f );
	}
}

// the shader's blend of the 8 probes of the near grid around the surface under the crosshair
CON_COMMAND_F( r_deferred_gi_probe_info, "GI probes: the 8 probes blended for the surface under the crosshair", FCVAR_CHEAT )
{
	const GICascade_t &cas = s_Cascades[0];
	if ( !s_bProbesAllocated || !cas.bGridValid )
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

	const float S = cas.flSpacing;
	const float flTolerance = r_deferred_gi_leak_tolerance.GetFloat() * S / 64.0f;
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
		const GIProbe_t &p = cas.pProbes[ SlotIndex( idx[0], idx[1], idx[2] ) ];
		const bool bMatch = p.bAssigned && p.cell[0] == idx[0] && p.cell[1] == idx[1] && p.cell[2] == idx[2];
		const float tw = ( ( c & 1 ) ? f[0] : 1.0f - f[0] ) * ( ( ( c >> 1 ) & 1 ) ? f[1] : 1.0f - f[1] ) * ( ( ( c >> 2 ) & 1 ) ? f[2] : 1.0f - f[2] );
		const bool bValid = bMatch && p.bTraced && p.flValid > 0.0f;
		const Vector probePos( ( idx[0] + GI_OFFSET ) * S, ( idx[1] + GI_OFFSET ) * S, ( idx[2] + GI_OFFSET ) * S );
		const Vector d = Ps - probePos;
		float flOver = -1e9f;
		float flFree[3];
		for ( int a = 0; a < 3; a++ )
		{
			const float flRaw = p.freeDist[ a * 2 + ( d[a] >= 0.0f ? 0 : 1 ) ];
			const int q = clamp( (int)( flRaw / S * 31.0f + 0.5f ), 0, 31 );
			flFree[a] = q * S / 31.0f;
			flOver = Max( flOver, fabsf( d[a] ) - flFree[a] - flTolerance );
		}
		const float flOpen = clamp( 1.0f - flOver / ( S * 0.125f ), 0.0f, 1.0f );
		Vector toProbe = probePos - P;
		const float flFacing = clamp( DotProduct( toProbe, N ) / Max( toProbe.Length(), 1.0f ) * 0.5f + 0.5f, 0.0f, 1.0f );
		const float w = bValid ? tw * flOpen * p.flFade * ( flFacing * flFacing + 0.02f ) : 0.0f;
		const float vis = clamp( p.visF[0] + p.visF[1] * N.x + p.visF[2] * N.y + p.visF[3] * N.z, 0.0f, 1.0f );
		wsum += w;
		sky += w * vis;
		Msg( "[gi]  %d cell %d %d %d %s%s trace from %.0f %.0f %.0f, tw %.2f facing %.2f, free %.0f %.0f %.0f (d %.0f %.0f %.0f) open %.2f fade %.2f -> w %.3f, sky vis %.2f (avg %.2f, own rays %.2f), indirect sun %.3f sky %.3f, %d passes, %d hits\n",
			c, idx[0], idx[1], idx[2], bMatch ? "" : "[slot holds another cell] ", bValid ? "valid" : "INVALID",
			p.vecTracePos.x, p.vecTracePos.y, p.vecTracePos.z, tw, flFacing, flFree[0], flFree[1], flFree[2], d.x, d.y, d.z,
			flOpen, p.flFade, w, vis, p.visF[0], p.vis[0], p.sun[0], p.sky[0], (int)p.nPasses, p.nHits );
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
	// (as after a teleport, not as on a map's first frame: the fill is spread over the frames)
	s_bFirstFill = ( args.ArgC() > 1 && atoi( args[1] ) != 0 );
}

CON_COMMAND_F( r_deferred_gi_bench, "GI probes: time N rays from the camera", FCVAR_CHEAT )
{
	const int n = ( args.ArgC() > 1 ) ? clamp( atoi( args[1] ), 1, 200000 ) : 4000;
	const Vector vecOrigin = MainViewOrigin();
	GICandidates_t cand;
	FindCandidates( vecOrigin, GI_RAY_LENGTH, cand );
	int nSky = 0;
	const double t0 = Plat_FloatTime();
	trace_t tr;
	for ( int i = 0; i < n; i++ )
	{
		Vector dir( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( -1, 1 ) );
		if ( dir.NormalizeInPlace() < 0.01f )
			dir.Init( 0, 0, 1 );
		GITrace( vecOrigin, vecOrigin + dir * GI_RAY_LENGTH, tr, cand );
		nSky += IsSkyHit( tr ) ? 1 : 0;
	}
	const double dt = Plat_FloatTime() - t0;
	Msg( "[gi] bench: %d rays, %.2f us per ray, %d%% sky, %d brush entities in reach\n", n, dt * 1e6 / n, nSky * 100 / n, cand.n );
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------
void DeferredGI_Render( const CViewSetup &view, const lightData_Global_t &globalLight )
{
	giData_t data;

	const bool bOn = r_deferred_gi.GetBool() && globalLight.bEnabled
		&& s_Cascades[0].texProbes.IsValid() && s_Cascades[1].texProbes.IsValid()
		&& g_tex_GI.IsValid() && g_tex_GIBlur.IsValid() && EnsureGIMaterials();

	ITexture *pProbes = s_Cascades[0].texProbes;
	ITexture *pProbesFar = s_Cascades[1].texProbes;
	ITexture *pGI = g_tex_GI;
	ITexture *pBlur = g_tex_GIBlur;

	if ( !bOn )
	{
		QUEUE_FIRE( CommitGIData, data );
		QUEUE_FIRE( CommitTexture_GI, pProbes, pProbesFar, pGI, pBlur );
		return;
	}

	EnsureProbes();
	if ( !s_Cascades[0].bGridValid )
	{
		s_Cascades[0].flSpacing = r_deferred_gi_spacing.GetFloat();
		s_Cascades[1].flSpacing = s_Cascades[0].flSpacing * GI_FAR_SCALE;
	}

	const bool bFar = r_deferred_gi_far.GetBool();
	UpdateGrid( 0, view.origin );
	if ( bFar )
		UpdateGrid( 1, view.origin );
	UpdateBrushEntities( s_bFirstFill );

	const Vector vecLight = globalLight.vecLight.AsVector3D();
	s_LastLight = globalLight;
	TraceProbes( vecLight, globalLight.bEnabled );
	UpdateFades();
	UpdateCameraSky( view.origin );
	UploadProbes( s_Cascades[0], bFar ? 1536 : 2048 );
	if ( bFar )
		UploadProbes( s_Cascades[1], 512 );

	const int iDebug = r_deferred_gi_debug.GetInt();
	if ( iDebug == 2 )
		DrawProbesDebug( view.origin );

	const int w = Max( 1, view.width / 2 );
	const int h = Max( 1, view.height / 2 );
	const float flUVScale = (float)w / Max( 1, g_tex_GI->GetActualWidth() );

	data.bEnabled = true;
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		const GICascade_t &cas = s_Cascades[c];
		const bool bUse = ( c == 0 ) || ( bFar && cas.bGridValid );
		data.vecGrid[c].Init( cas.iGridMin[0], cas.iGridMin[1], cas.iGridMin[2], 1.0f / cas.flSpacing );
		// (no probes per axis: the cascade is off)
		data.vecDims[c].Init( bUse ? GI_NX : 0, bUse ? GI_NY : 0, bUse ? GI_NZ : 0, cas.flSpacing );
	}
	data.vecAtlas.Init( 1.0f / GI_ATLAS_W, 1.0f / GI_ATLAS_H, r_deferred_gi_normal_offset.GetFloat(), (float)iDebug );
	data.vecParams0.Init( r_deferred_gi_bounce.GetFloat(), r_deferred_gi_skybounce.GetFloat(), 1.0f / r_deferred_gi_fade_cells.GetFloat(), r_deferred_gi_min.GetFloat() );
	data.vecParams1.Init( r_deferred_gi_leak_tolerance.GetFloat() / 64.0f, GI_OFFSET, r_deferred_gi_sky_open.GetFloat(), r_deferred_gi_indoor_boost.GetFloat() );
	data.vecParams2.Init( s_flCameraSky, r_deferred_gi_sky_gamma.GetFloat(), r_deferred_gi_sky_desat.GetFloat(), r_deferred_gi_sky_indoor.GetFloat() );
	data.vecBlurH.Init( 1.0f / w, 0.0f, 0.04f, flUVScale );
	data.vecBlurV.Init( 0.0f, 1.0f / h, 0.04f, flUVScale );
	data.vecApply.Init( 1.0f, ( iDebug == 1 || iDebug >= 3 ) ? 1.0f : 0.0f, 0.0f, flUVScale );

	QUEUE_FIRE( CommitGIData, data );
	QUEUE_FIRE( CommitTexture_GI, pProbes, pProbesFar, pGI, pBlur );

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

	// ...and to the full resolution, every pixel from the half resolution pixels of its own
	// surface: into the (full size) blur target, which is what the global light pass reads
	const int wFull = view.width, hFull = view.height;
	pRenderContext->PushRenderTargetAndViewport( g_tex_GIBlur, 0, 0, wFull, hFull );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatGIUp, 0, 0, wFull, hFull, 0, 0, wFull - 1, hFull - 1, wFull, hFull );
	pRenderContext->PopRenderTargetAndViewport();

	data.vecApply.w = (float)wFull / Max( 1, g_tex_GIBlur->GetActualWidth() );
	QUEUE_FIRE( CommitGIData, data );
	QUEUE_FIRE( CommitTexture_GI, pProbes, pProbesFar, pBlur, pGI );
}

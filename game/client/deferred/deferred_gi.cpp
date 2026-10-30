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
// Two grids of probes around the camera (toroidal - a step of the camera only adds
// the new slice): the near one, 64 x 64 x 16 probes r_deferred_gi_spacing apart
// (2048 units to either side), and around it the far one at twice the spacing.
// Each probe shoots rays through the world (the BSP and the brush entities at rest,
// enginetrace):
//  - a ray that escapes to the sky adds to the probe's sky visibility,
//  - a ray that hits a surface keeps the point (8 bytes: which ray, how far, the
//    normal, the albedo of its material); a ray from there toward the sun tells
//    whether the sun lights it.
//
// From the hits of a probe its indirect light is gathered (GatherProbe): what a hit
// sends back is its albedo x the light it gets - the sun (if it is lit), the sky it
// sees and the indirect light at it, all three taken from the probes around the hit as
// they are now. Repeated, that is light bouncing as many times as it takes: the sun
// patch by the door lights the wall, the wall the ceiling, the room fills softly.
//
// Everything is kept as L1 spherical harmonics (4 numbers: the light a surface of any
// orientation at the probe gets) - sky visibility, indirect sun, bounced sky - the two
// indirect ones as luminance with a color. Plus the free distance along the 6 axes: a
// probe beyond a wall must not light the room (leaks).
//
// The way it is computed is laid out so that nothing is seen "loading" (the first
// version traced a far grid 4 x coarser than the near one, probes in passes of 32 rays
// that were refined later, and gathered the bounces in sweeps through the slots - the
// darkness of a hall came in by blocks while the player walked, and its far end turned
// darker as the near grid reached it):
//  - one spacing for everything up to 1600 - 2048 units, where the near grid fades into
//    the far one (twice as coarse, to 3072 units; beyond, an estimate from the sky
//    visibility of the rain map);
//  - the work is done by worker threads of the GI's own (below normal priority) in the
//    background, not in the frame: a probe gets all its rays at once (fewer, refined
//    later, if it is farther than r_deferred_gi_hq_radius from the camera), the main
//    thread takes in what is done, schedules more and uploads;
//  - what the lighting reads eases toward what the probes are (deferred_gi_blend_ps30,
//    r_deferred_gi_smooth): a probe's new light never snaps, a new probe fades in;
//  - the background gathers go through the probes in a scattered order.
// The first frame of a map traces everything around the camera at once (the level is
// still loading).
//
// The probes go to the GPU through render targets (one texel per probe and plane,
// written as points - a texture lock from the main thread would stall the render
// thread); DEFERRED_GI blends the 8 probes around every pixel at half resolution
// (dropping the ones behind the surface or out of its sight).
//
// Threads: the rays are traced on the workers (the BSP trace keeps its state per thread,
// and brush entities are clipped through the same code; static props and the material
// system are not thread safe - props don't occlude, the albedo is looked up on the main
// thread when the results are taken in). A probe has at most one job at a time; a job
// writes into its own results, except the sun job, which marks the lit hits of its probe
// in place (nothing else touches them meanwhile). The brush entities a job clips to are
// a snapshot that is not changed while a job holds it.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_gi.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imesh.h"
#include "view.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"
#include "tier0/threadtools.h"
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
	"GI probes: main thread time per frame at most for taking in what the worker threads have done", true, 0.1f, true, 50.0f );
static ConVar r_deferred_gi_load_ms( "r_deferred_gi_load_ms", "6000", 0,
	"GI probes: the first frame after a map loads traces the probes around the camera - at most this long (ms; the rest follows in the background)", true, 0.0f, true, 20000.0f );
static ConVar r_deferred_gi_threads( "r_deferred_gi_threads", "-1", 0,
	"GI probes: worker threads (-1 = by the CPU: logical processors / 2 - 1, 1..3; applied on the next map)", true, -1.0f, true, 8.0f );
static ConVar r_deferred_gi_smooth( "r_deferred_gi_smooth", "0.4", 0,
	"GI probes: seconds the light the lighting reads takes to follow a probe's new light (0 = at once)", true, 0.0f, true, 5.0f );
static ConVar r_deferred_gi_fade( "r_deferred_gi_fade", "0.5", 0,
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
	"GI: the far grid of probes (twice the spacing) around the near one" );
static ConVar r_deferred_gi_rays( "r_deferred_gi_rays", "192", FCVAR_ARCHIVE,
	"GI: rays per probe of the near grid within r_deferred_gi_hq_radius of the camera (more = smoother light through small openings)", true, 32.0f, true, 256.0f );
static ConVar r_deferred_gi_rays_mid( "r_deferred_gi_rays_mid", "96", 0,
	"GI: rays per probe of the near grid farther from the camera (refined to r_deferred_gi_rays when it comes closer)", true, 32.0f, true, 256.0f );
static ConVar r_deferred_gi_rays_far( "r_deferred_gi_rays_far", "64", 0,
	"GI: rays per probe of the far grid", true, 32.0f, true, 256.0f );
static ConVar r_deferred_gi_hq_radius( "r_deferred_gi_hq_radius", "1024", 0,
	"GI: probes of the near grid within this of the camera get r_deferred_gi_rays", true, 0.0f, true, 4096.0f );
static ConVar r_deferred_gi_gather_hits( "r_deferred_gi_gather_hits", "96", 0,
	"GI: hits of a probe its bounced light is gathered from at most (spread over all of them)", true, 16.0f, true, 256.0f );
static ConVar r_deferred_gi_sweep_interval( "r_deferred_gi_sweep_interval", "3", 0,
	"GI: seconds between two rounds of gathering all probes again while anything changes (the light spreading farther)", true, 0.0f, true, 60.0f );
static ConVar r_deferred_gi_upload_max( "r_deferred_gi_upload_max", "2048", 0,
	"GI: probes of the near grid written to the GPU per frame at most (the rest the next frames; half of it for the far grid, 4 x after a new map or a teleport)", true, 256.0f, true, 65536.0f );
static ConVar r_deferred_gi_upload_points( "r_deferred_gi_upload_points", "1", 0,
	"GI: write the probe texels as points (1) or as tiny quads (0, four times the vertices)" );
static ConVar r_deferred_gi_brush_entities( "r_deferred_gi_brush_entities", "1", 0,
	"GI: brush entities at rest (func_brush, closed doors...) block the light like the world does (0 = the world only)" );
static ConVar r_deferred_gi_fade_cells( "r_deferred_gi_fade_cells", "6", 0,
	"GI: the near grid fades into the far one over this many of its outer cells", true, 1.0f, true, 15.0f );
static ConVar r_deferred_gi_debug_point( "r_deferred_gi_debug_point", "0 0", FCVAR_CHEAT, "GI: x y of the point r_deferred_gi_debug 9 measures the distance from" );
static ConVar r_deferred_gi_debug( "r_deferred_gi_debug", "0", FCVAR_CHEAT,
	"GI: 1 = ambient light only, 2 = + draw the probes near the camera, 3 = sky visibility, 4 = indirect sun, 5 = which source lights a pixel (red near grid, green far grid, blue estimate), 6 = bounced sky light, 7 = the pixel's world position (frac / 64), 8 = its normal, 9 = its distance from r_deferred_gi_debug_point, 10 = near grid: its share (r), the weight of its probes (g), sky visibility x 10 (b)" );

enum
{
	GI_CASCADES = 2,
	GI_RAYS = 32,			// rays of one pass (a probe has up to GI_MAX_PASSES)
	GI_MAX_PASSES = 8,
	GI_PLANES = 6,
	GI_MAX_CANDIDATES = 48,
	GI_MAX_WORKERS = 8,
	GI_MAX_ALBEDO = 4096,
	GI_SURFACE_CACHE = 4096,
	GI_SWEEP_STRIDE = 7919,		// (prime: the background visits the slots in a scattered order)
};

struct GIGridDesc_t
{
	int n[3];				// probes per axis
	float flScale;			// spacing x this
	float flCameraLevel;	// probes below the camera
	const char *pszAtlas;
};

// (the near grid: one spacing for everything the player looks at up close and across a hall, a
// square, a street - 2048 units to either side, fading into the far grid from ~1660 on)
static const GIGridDesc_t s_GridDesc[ GI_CASCADES ] =
{
	{ { 64, 64, 16 }, 1.0f, 5.5f, "_rt_hl2rpm_gi_probes" },
	{ { 48, 48, 12 }, 2.0f, 4.5f, "_rt_hl2rpm_gi_probes_far" },
};

// probes sit off the grid lines (cell + 0.45): map geometry is aligned to powers of two, probes at
// multiples of 64 lay exactly on floors, ceilings and walls (half their rays started in the solid)
#define GI_OFFSET			0.45f

// (a ray that reaches nothing within this is open sky: long rays through open space cost the
// most. Per grid: x its spacing / 64.)
#define GI_RAY_LENGTH		1536.0f
#define GI_RAY_LENGTH_MAX	3072.0f
#define GI_SUN_RAY_LENGTH	2560.0f

// a surface a ray of a probe reached
struct GIHit_t
{
	unsigned short dist;	// along the ray to the surface, 1/8 unit
	unsigned char ray;		// which ray of the probe: pass * GI_RAYS + ray of the pass
	unsigned char bLit;		// the sun reaches it (for the probe's vecSunDir)
	signed char n[2];		// its normal, octahedral x 127
	unsigned short albedo;	// in s_Albedo
};

struct GIProbe_t
{
	int cell[3];			// world cell this slot holds
	unsigned int gen;		// another cell in the slot (or traced anew): the result of a job for the old one is dropped
	bool bAssigned;
	bool bTraced;			// it has its rays: it lights
	bool bSunValid;			// the hits' sun rays are done for vecSunDir
	bool bBusy;				// a job of it is with the workers
	bool bRetrace;			// what is around it has changed (a door): start over, lighting with the old data meanwhile
	bool bGather;			// its light wants gathering (new hits, the sun moved, a neighbour is new)
	bool bDirty;			// in the upload list
	bool bDirtyNow;			// in the list uploaded first (a new cell in the slot)
	bool bInPending;		// in the list of probes waiting for a trace
	unsigned char nPasses;	// passes of GI_RAYS rays traced

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

	float uploaded[12];		// visF, sun, sky as last written to the GPU (a gather that changes nothing visible is not)

	int nHits;
	GIHit_t *pHits;
};

struct GICascade_t
{
	int n[3];
	int nProbes;
	float flSpacing;
	float flCameraLevel;
	int iGridMin[3];
	bool bGridValid;
	GIProbe_t *pProbes;
	// probe offsets in the grid, nearest to the camera's place in it first
	CUtlVector< int > order;
	CUtlVector< int > pending;	// slots waiting for a trace without one out (nearest first once sorted)
	int nPendingUnsorted;	// added since the list was last sorted
	int nPending;			// probes without their first trace (or starting over)
	// the background: where it is in the scattered order, rounds of gathering everything left
	int iSweep;
	int nSweepsLeft;
	float flNextSweep;
	bool bChanged;			// something was traced since the last round
	// upload
	CUtlVector< int > dirty;
	CUtlVector< int > dirtyNow;
	bool bClearAtlas;
	bool bReuploadAll;
	bool bInitialUpload;	// the first fill is being uploaded: what the lighting reads is the target as it is
	// render targets: written (target), eased toward it (display, ping-pong)
	CTextureReference texTarget;
	CTextureReference texDisplay[2];
	int iDisplay;
	IMaterial *pMatBlend[2];	// display[ i ] -> display[ 1 - i ]
	float flLastUpload;
	bool bBlendReset;
};

// brush entities blocking the light: what a job clips its rays to (not changed while a job holds it)
class CGIBrushCollide;
struct GIBrushSnapshot_t;

enum
{
	GIJOB_TRACE = 0,	// passes of rays (and the sun rays of the hits)
	GIJOB_SUN,			// the sun rays of the hits alone
	GIJOB_GATHER,		// the indirect light from the hits
};

// what one worker does for one probe
struct GIJob_t
{
	// in
	GIProbe_t *pProbe;
	unsigned int gen;
	int iCascade;
	int iType;
	int cell[3];
	float flSpacing;
	Vector vecLight;
	bool bLight;
	GIBrushSnapshot_t *pBrushes;
	// (trace: passes it has and is to have; sun / gather: its hits)
	int nPassesHave;
	int nPassesTarget;
	Vector vecTracePos;
	GIHit_t *pHits;
	int nHits;
	float visSumIn[4];
	int nBackIn, nStartSolidIn;
	float freeDistIn[6];
	bool bSunValidIn;
	Vector vecSunDirIn;

	// out
	bool bSkipped;
	int nTraces;
	int nInvalid;		// 1 = in the solid, 2 = in a closed space
	// trace
	Vector vecTracePosOut;
	float freeDist[6];
	float visSum[4];
	int nBack, nStartSolid;
	int nPasses;
	float flValid;
	GIHit_t *pNewHits;	// all the hits (the old ones copied)
	int nNewHits;
	int iFirstNewHit;
	const char **ppSurfaces;	// of the hits from iFirstNewHit on (the albedo is looked up on the main thread)
	bool bSunAll;
	// gather
	float visF[4], sun[4], sky[4];
	Vector vecSunChroma, vecSkyChroma;
};

static GICascade_t s_Cascades[ GI_CASCADES ];
static bool s_bProbesAllocated = false;
static Vector s_vecRayDir[ GI_RAYS ];
static bool s_bRayDirsInit = false;

static bool s_bFirstFill = true;				// a map has just been loaded: trace before the first frame shows
static float s_flCameraSky = 1.0f;				// sky in sight at the camera (0 closed room .. 1 open), smoothed
static Vector s_vecCamera( 0, 0, 0 );

// albedo of the surfaces the rays hit (index 0: unknown). Only appended to, by the main thread,
// before a job that could read an entry is given out; never reallocated.
static Vector s_Albedo[ GI_MAX_ALBEDO ];
static int s_nAlbedo = 1;
static CUtlMap< uintp, unsigned short > s_AlbedoMap( DefLessFunc( uintp ) );
static uintp s_SurfaceCacheKey[ GI_SURFACE_CACHE ];
static unsigned short s_SurfaceCacheVal[ GI_SURFACE_CACHE ];

// statistics
static int s_nInvalidSolid = 0;
static int s_nInvalidClosed = 0;
static lightData_Global_t s_LastLight;
static int s_nDoneTrace = 0, s_nDoneRefine = 0, s_nDoneSun = 0, s_nDoneGather = 0, s_nDoneTraces = 0;
static double s_flStatsStart = 0.0;
static float s_flFirstFillMs = 0.0f;
static int s_nFirstFillDone = 0, s_nFirstFillLeft = 0;
static float s_flMsLastFrame = 0.0f;
static float s_flMsWorst = 0.0f;
static int s_nUploadLastFrame = 0;
// the main thread's time by phase: grids, taking in, scheduling, writing, easing (sum / max over a window)
enum { GIPHASE_GRID, GIPHASE_COLLECT, GIPHASE_SCHEDULE, GIPHASE_UPLOAD, GIPHASE_BLEND, GIPHASE_COUNT };
static const char *s_pszPhase[ GIPHASE_COUNT ] = { "grid", "take in", "schedule", "write", "ease" };
static double s_flPhaseSum[ GIPHASE_COUNT ], s_flPhaseMax[ GIPHASE_COUNT ];
static double s_flPhaseLastSum[ GIPHASE_COUNT ], s_flPhaseLastMax[ GIPHASE_COUNT ];
static int s_nPhaseFrames = 0, s_nPhaseLastFrames = 0;
static int s_nCollected = 0, s_nLastCollected = 0;
static double s_flPhaseWindowStart = 0.0;

static inline void PhaseTime( int iPhase, double &flMark )
{
	const double flNow = Plat_FloatTime();
	const double flMs = ( flNow - flMark ) * 1000.0;
	s_flPhaseSum[ iPhase ] += flMs;
	s_flPhaseMax[ iPhase ] = Max( s_flPhaseMax[ iPhase ], flMs );
	flMark = flNow;
}

static void PhaseFrameDone()
{
	s_nPhaseFrames++;
	const double flNow = Plat_FloatTime();
	if ( flNow - s_flPhaseWindowStart < 2.0 )
		return;
	for ( int i = 0; i < GIPHASE_COUNT; i++ )
	{
		s_flPhaseLastSum[i] = s_flPhaseSum[i];
		s_flPhaseLastMax[i] = s_flPhaseMax[i];
		s_flPhaseSum[i] = s_flPhaseMax[i] = 0.0;
	}
	s_nPhaseLastFrames = s_nPhaseFrames;
	s_nLastCollected = s_nCollected;
	s_nPhaseFrames = 0;
	s_nCollected = 0;
	s_flPhaseWindowStart = flNow;
}

static CTextureReference g_tex_GI;
static CTextureReference g_tex_GIBlur;
static IMaterial *g_pMatGI = NULL;
static IMaterial *g_pMatGIBlurH = NULL;
static IMaterial *g_pMatGIBlurV = NULL;
static IMaterial *g_pMatGIUp = NULL;
static IMaterial *g_pMatGIUpload = NULL;

static inline void ProbePos( const GICascade_t &cas, const int cell[3], Vector &vecPos )
{
	vecPos.Init( ( cell[0] + GI_OFFSET ) * cas.flSpacing, ( cell[1] + GI_OFFSET ) * cas.flSpacing, ( cell[2] + GI_OFFSET ) * cas.flSpacing );
}

//-----------------------------------------------------------------------------
// Render targets and materials
//-----------------------------------------------------------------------------
static const char *s_pszDisplaySuffix[2] = { "_a", "_b" };

void InitGIRTs()
{
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	// probe atlases: x = probe x + probe z * x probes, y = probe y + plane * y probes
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		const GIGridDesc_t &d = s_GridDesc[c];
		const int w = d.n[0] * d.n[2], h = d.n[1] * GI_PLANES;
		s_Cascades[c].texTarget.Init( materials->CreateNamedRenderTargetTextureEx2(
			d.pszAtlas, w, h, RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE,
			flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
		for ( int i = 0; i < 2; i++ )
		{
			char szName[64];
			Q_snprintf( szName, sizeof( szName ), "%s%s", d.pszAtlas, s_pszDisplaySuffix[i] );
			s_Cascades[c].texDisplay[i].Init( materials->CreateNamedRenderTargetTextureEx2(
				szName, w, h, RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE,
				flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
		}

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

		for ( int c = 0; c < GI_CASCADES; c++ )
		{
			for ( int i = 0; i < 2; i++ )
			{
				char szName[64], szPrev[64];
				Q_snprintf( szName, sizeof( szName ), "__hl2rpm_gi_blend_%d%d", c, i );
				Q_snprintf( szPrev, sizeof( szPrev ), "%s%s", s_GridDesc[c].pszAtlas, s_pszDisplaySuffix[i] );
				KeyValues *pKV = new KeyValues( "DEFERRED_GI_BLEND" );
				pKV->SetString( "$targetatlas", s_GridDesc[c].pszAtlas );
				pKV->SetString( "$prevatlas", szPrev );
				IMaterial *pMat = materials->CreateMaterial( szName, pKV );
				if ( pMat )
					pMat->IncrementReferenceCount();
				s_Cascades[c].pMatBlend[i] = pMat;
			}
		}
	}
	bool bOk = IsMaterialOk( g_pMatGI ) && IsMaterialOk( g_pMatGIBlurH ) && IsMaterialOk( g_pMatGIBlurV ) && IsMaterialOk( g_pMatGIUp ) && IsMaterialOk( g_pMatGIUpload );
	for ( int c = 0; c < GI_CASCADES; c++ )
		bOk = bOk && IsMaterialOk( s_Cascades[c].pMatBlend[0] ) && IsMaterialOk( s_Cascades[c].pMatBlend[1] );
	return bOk;
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

// what the jobs clip their rays to: built whole, then never changed; freed when neither the GI
// (as its current one) nor a job holds it any more (both on the main thread)
struct GIBrushSnapshot_t
{
	int nRefs;
	int nEnts;
	GIBrushEnt_t *pEnts;
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

static GIBrushSnapshot_t *s_pBrushSnap = NULL;
static CUtlVector< GIBrushTrack_t > s_BrushTracks;
static float s_flNextBrushScan = 0.0f;
static int s_nBrushInvalidations = 0;

static void ReleaseBrushSnapshot( GIBrushSnapshot_t *pSnap )
{
	if ( !pSnap )
		return;
	if ( --pSnap->nRefs <= 0 )
	{
		delete[] pSnap->pEnts;
		delete pSnap;
	}
}

static void AddPending( GICascade_t &cas, int iSlot );

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
		for ( int i = 0; i < cas.nProbes; i++ )
		{
			GIProbe_t &p = cas.pProbes[i];
			if ( !p.bAssigned || !p.bTraced || p.bRetrace )
				continue;
			Vector vecPos;
			ProbePos( cas, p.cell, vecPos );
			if ( vecPos.x < vecLo.x || vecPos.y < vecLo.y || vecPos.z < vecLo.z || vecPos.x > vecHi.x || vecPos.y > vecHi.y || vecPos.z > vecHi.z )
				continue;
			p.bRetrace = true;
			cas.nPending++;
			AddPending( cas, i );
		}
	}
	s_nBrushInvalidations++;
}

static void ClearBrushEntities()
{
	ReleaseBrushSnapshot( s_pBrushSnap );
	s_pBrushSnap = NULL;
	s_BrushTracks.Purge();
	s_flNextBrushScan = 0.0f;
}

static bool SameBrush( const GIBrushEnt_t &a, const GIBrushEnt_t &b )
{
	return a.hEnt == b.hEnt && a.collide.m_iModelIndex == b.collide.m_iModelIndex
		&& ( a.collide.m_vecOrigin - b.collide.m_vecOrigin ).LengthSqr() < 0.01f && a.collide.m_angAngles == b.collide.m_angAngles;
}

// main thread, a few times a second: which brush entities block the light now
static void UpdateBrushEntities( bool bFirst )
{
	if ( !bFirst && gpGlobals->realtime < s_flNextBrushScan )
		return;
	s_flNextBrushScan = gpGlobals->realtime + 0.25f;

	if ( !r_deferred_gi_brush_entities.GetBool() )
	{
		if ( s_pBrushSnap && s_pBrushSnap->nEnts )
		{
			for ( int i = 0; i < s_pBrushSnap->nEnts; i++ )
				InvalidateProbesAround( s_pBrushSnap->pEnts[i].collide.m_vecWorldMins, s_pBrushSnap->pEnts[i].collide.m_vecWorldMaxs );
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

	CUtlVector< GIBrushEnt_t > now;
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

		GIBrushEnt_t &e = now[ now.AddToTail() ];
		e.hEnt = pEnt;
		CGIBrushCollide &col = e.collide;
		col.m_pHandle = pWorld;
		col.m_pUnknown = pWorld;
		col.m_pModel = pModel;
		col.m_iModelIndex = pEnt->GetModelIndex();
		modelinfo->GetModelBounds( pModel, col.m_vecMins, col.m_vecMaxs );
		col.m_vecOrigin = vecOrigin;
		col.m_angAngles = angAngles;
		AngleMatrix( angAngles, vecOrigin, col.m_matToWorld );
		TransformAABB( col.m_matToWorld, col.m_vecMins, col.m_vecMaxs, col.m_vecWorldMins, col.m_vecWorldMaxs );
	}

	for ( int i = s_BrushTracks.Count() - 1; i >= 0; i-- )
	{
		if ( !s_BrushTracks[i].bSeen )
			s_BrushTracks.FastRemove( i );
	}

	// what has changed since the last scan: the probes around it start over
	bool bChanged = !s_pBrushSnap || s_pBrushSnap->nEnts != now.Count();
	const int nOld = s_pBrushSnap ? s_pBrushSnap->nEnts : 0;
	for ( int i = 0; i < nOld; i++ )
	{
		const GIBrushEnt_t &a = s_pBrushSnap->pEnts[i];
		bool bSame = false;
		for ( int k = 0; k < now.Count() && !bSame; k++ )
			bSame = SameBrush( a, now[k] );
		if ( !bSame )
		{
			bChanged = true;
			if ( !bFirst )
				InvalidateProbesAround( a.collide.m_vecWorldMins, a.collide.m_vecWorldMaxs );
		}
	}
	for ( int k = 0; k < now.Count(); k++ )
	{
		bool bSame = false;
		for ( int i = 0; i < nOld && !bSame; i++ )
			bSame = SameBrush( s_pBrushSnap->pEnts[i], now[k] );
		if ( !bSame )
		{
			bChanged = true;
			if ( !bFirst )
				InvalidateProbesAround( now[k].collide.m_vecWorldMins, now[k].collide.m_vecWorldMaxs );
		}
	}
	if ( !bChanged )
		return;

	GIBrushSnapshot_t *pSnap = new GIBrushSnapshot_t;
	pSnap->nRefs = 1;
	pSnap->nEnts = now.Count();
	pSnap->pEnts = pSnap->nEnts ? new GIBrushEnt_t[ pSnap->nEnts ] : NULL;
	for ( int i = 0; i < pSnap->nEnts; i++ )
		pSnap->pEnts[i] = now[i];
	ReleaseBrushSnapshot( s_pBrushSnap );
	s_pBrushSnap = pSnap;
}

//-----------------------------------------------------------------------------
// Worker threads
//-----------------------------------------------------------------------------
static CThreadFastMutex s_JobMutex;
static CUtlVector< GIJob_t * > s_JobQueue;		// given out from s_iJobHead on
static int s_iJobHead = 0;
static CUtlVector< GIJob_t * > s_JobDone;
static CThreadManualEvent s_evWork;
static volatile bool s_bWorkersQuit = false;
static volatile bool s_bCancelJobs = false;
static ThreadHandle_t s_hWorkers[ GI_MAX_WORKERS ];
static int s_nWorkers = 0;
static int s_nOutstanding = 0;					// given out and not taken in yet (main thread)

static void ProcessJob( GIJob_t &job );

// (a few at a time: a gather takes some 30 us, and every visit of the queue is a lock the main
// thread may be waiting for)
static int PopJobs( GIJob_t **ppJobs, int nMax )
{
	AUTO_LOCK( s_JobMutex );
	int n = 0;
	while ( n < nMax && s_iJobHead < s_JobQueue.Count() )
		ppJobs[ n++ ] = s_JobQueue[ s_iJobHead++ ];
	if ( s_iJobHead >= s_JobQueue.Count() )
	{
		s_JobQueue.RemoveAll();
		s_iJobHead = 0;
		s_evWork.Reset();
	}
	return n;
}

static void PushDone( GIJob_t **ppJobs, int n )
{
	AUTO_LOCK( s_JobMutex );
	s_JobDone.AddMultipleToTail( n, ppJobs );
}

static void RunJobs( GIJob_t **ppJobs, int n )
{
	for ( int i = 0; i < n; i++ )
	{
		if ( s_bCancelJobs )
			ppJobs[i]->bSkipped = true;
		else
			ProcessJob( *ppJobs[i] );
	}
	PushDone( ppJobs, n );
}

static unsigned GIWorkerMain( void *pParam )
{
	GIJob_t *pJobs[4];
	while ( !s_bWorkersQuit )
	{
		const int n = PopJobs( pJobs, 4 );
		if ( !n )
		{
			s_evWork.Wait( 20 );
			continue;
		}
		RunJobs( pJobs, n );
	}
	return 0;
}

static int WantedWorkers()
{
	const int iSet = r_deferred_gi_threads.GetInt();
	if ( iSet >= 0 )
		return clamp( iSet, 0, (int)GI_MAX_WORKERS );
	return clamp( (int)GetCPUInformation()->m_nLogicalProcessors / 2 - 1, 1, 3 );
}

static void StartWorkers()
{
	if ( s_nWorkers > 0 )
		return;
	s_bWorkersQuit = false;
	const int nWanted = WantedWorkers();
	for ( int i = 0; i < nWanted; i++ )
	{
		ThreadHandle_t h = CreateSimpleThread( GIWorkerMain, NULL );
		if ( !h )
			break;
		// (below the game's own threads: the GI waits, the frame doesn't)
		ThreadSetPriority( h, -1 );
		s_hWorkers[ s_nWorkers++ ] = h;
	}
}

static void StopWorkers()
{
	if ( s_nWorkers <= 0 )
		return;
	s_bWorkersQuit = true;
	s_evWork.Set();
	for ( int i = 0; i < s_nWorkers; i++ )
	{
		ThreadJoin( s_hWorkers[i] );
		ReleaseThreadHandle( s_hWorkers[i] );
		s_hWorkers[i] = NULL;
	}
	s_nWorkers = 0;
	s_bWorkersQuit = false;
}

// jobs given out are collected and handed to the workers at once (FlushJobs)
static CUtlVector< GIJob_t * > s_JobsToSubmit;

static void SubmitJob( GIJob_t *pJob )
{
	pJob->pProbe->bBusy = true;
	s_nOutstanding++;
	s_JobsToSubmit.AddToTail( pJob );
}

static void FlushJobs()
{
	if ( !s_JobsToSubmit.Count() )
		return;
	{
		AUTO_LOCK( s_JobMutex );
		s_JobQueue.AddVectorToTail( s_JobsToSubmit );
		s_evWork.Set();
	}
	s_JobsToSubmit.RemoveAll();
}

// the main thread works too (the first fill, or no workers at all)
static void HelpWithJobs( double flDeadline )
{
	FlushJobs();
	GIJob_t *pJobs[4];
	while ( Plat_FloatTime() < flDeadline )
	{
		const int n = PopJobs( pJobs, 4 );
		if ( !n )
			break;
		RunJobs( pJobs, n );
	}
}

static int QueuedJobs()
{
	AUTO_LOCK( s_JobMutex );
	return s_JobQueue.Count() - s_iJobHead;
}

static void IntegrateJob( GIJob_t *pJob );

// (done jobs are kept for the next ones: tens of thousands go through a map's first frame)
static CUtlVector< GIJob_t * > s_FreeJobs;
static void FreeJob( GIJob_t *pJob )
{
	if ( s_FreeJobs.Count() < 4096 )
		s_FreeJobs.AddToTail( pJob );
	else
		delete pJob;
}

// takes in what the workers have done (at most until flDeadline, 0 = all)
static int CollectJobs( double flDeadline )
{
	CUtlVector< GIJob_t * > done;
	{
		AUTO_LOCK( s_JobMutex );
		done.AddVectorToTail( s_JobDone );
		s_JobDone.RemoveAll();
	}
	int i = 0;
	for ( ; i < done.Count(); i++ )
	{
		// (what is left goes back for the next frame - a probe stays busy until then)
		if ( flDeadline > 0.0 && ( i & 15 ) == 15 && Plat_FloatTime() > flDeadline )
			break;
		IntegrateJob( done[i] );
	}
	if ( i < done.Count() )
	{
		AUTO_LOCK( s_JobMutex );
		const int nLeft = done.Count() - i;
		s_JobDone.InsertMultipleBefore( 0, nLeft );
		for ( int k = 0; k < nLeft; k++ )
			s_JobDone[k] = done[ i + k ];
	}
	return i;
}

// every job given out comes back (dropped): before the probes, the brush entities or the map go away
static void DrainJobs()
{
	if ( s_nOutstanding <= 0 )
		return;
	s_bCancelJobs = true;
	FlushJobs();
	while ( s_nOutstanding > 0 )
	{
		HelpWithJobs( Plat_FloatTime() + 1.0 );
		CollectJobs( 0.0 );
		if ( s_nOutstanding > 0 )
			ThreadSleep( 1 );
	}
	s_bCancelJobs = false;
}

static void ShutdownGIProbes();

void ShutdownGI()
{
	DrainJobs();
	StopWorkers();
	if ( g_pMatGI ) { g_pMatGI->DecrementReferenceCount(); g_pMatGI = NULL; }
	if ( g_pMatGIBlurH ) { g_pMatGIBlurH->DecrementReferenceCount(); g_pMatGIBlurH = NULL; }
	if ( g_pMatGIBlurV ) { g_pMatGIBlurV->DecrementReferenceCount(); g_pMatGIBlurV = NULL; }
	if ( g_pMatGIUp ) { g_pMatGIUp->DecrementReferenceCount(); g_pMatGIUp = NULL; }
	if ( g_pMatGIUpload ) { g_pMatGIUpload->DecrementReferenceCount(); g_pMatGIUpload = NULL; }
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		for ( int i = 0; i < 2; i++ )
		{
			if ( s_Cascades[c].pMatBlend[i] )
			{
				s_Cascades[c].pMatBlend[i]->DecrementReferenceCount();
				s_Cascades[c].pMatBlend[i] = NULL;
			}
		}
	}
	ClearBrushEntities();
	ShutdownGIProbes();
	s_FreeJobs.PurgeAndDeleteElements();
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
static void PassRotation( const int cell[3], int iPass, matrix3x4_t &mat )
{
	unsigned int h = (unsigned int)cell[0] * 73856093u ^ (unsigned int)cell[1] * 19349663u ^ (unsigned int)cell[2] * 83492791u ^ (unsigned int)( iPass + 1 ) * 2654435761u;
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

static inline int SlotIndex( const GICascade_t &cas, int cx, int cy, int cz )
{
	return PosMod( cx, cas.n[0] ) + cas.n[0] * ( PosMod( cy, cas.n[1] ) + cas.n[1] * PosMod( cz, cas.n[2] ) );
}

static const GICascade_t *s_pSortCascade = NULL;
static int __cdecl OrderSort( const int *a, const int *b )
{
	// offsets from the camera's place in the grid; height counts a bit more (other floors)
	const GICascade_t &cas = *s_pSortCascade;
	const int nx = cas.n[0], ny = cas.n[1];
	const int iLevel = (int)( cas.flCameraLevel * 2.0f );
	const int ax = ( *a % nx ) * 2 - ( nx - 1 ), ay = ( ( *a / nx ) % ny ) * 2 - ( ny - 1 ), az = ( *a / ( nx * ny ) ) * 2 - iLevel;
	const int bx = ( *b % nx ) * 2 - ( nx - 1 ), by = ( ( *b / nx ) % ny ) * 2 - ( ny - 1 ), bz = ( *b / ( nx * ny ) ) * 2 - iLevel;
	const int da = ax * ax + ay * ay + az * az * 2;
	const int db = bx * bx + by * by + bz * bz * 2;
	return ( da < db ) ? -1 : ( ( da > db ) ? 1 : ( *a - *b ) );
}

static inline int OrderedSlot( const GICascade_t &cas, int i )
{
	const int o = cas.order[i];
	const int nx = cas.n[0], ny = cas.n[1];
	return SlotIndex( cas, cas.iGridMin[0] + o % nx, cas.iGridMin[1] + ( o / nx ) % ny, cas.iGridMin[2] + o / ( nx * ny ) );
}

static void AddDirty( GICascade_t &cas, int iSlot, bool bNow )
{
	GIProbe_t &p = cas.pProbes[ iSlot ];
	if ( bNow )
	{
		if ( !p.bDirtyNow )
		{
			p.bDirtyNow = true;
			cas.dirtyNow.AddToTail( iSlot );
		}
	}
	else if ( !p.bDirty && !p.bDirtyNow )
	{
		p.bDirty = true;
		cas.dirty.AddToTail( iSlot );
	}
}

static void AddPending( GICascade_t &cas, int iSlot )
{
	GIProbe_t &p = cas.pProbes[ iSlot ];
	if ( p.bInPending )
		return;
	p.bInPending = true;
	cas.pending.AddToTail( iSlot );
	cas.nPendingUnsorted++;
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

void DeferredGI_Reset()
{
	DrainJobs();
	s_AlbedoMap.RemoveAll();
	s_nAlbedo = 1;
	s_Albedo[0].Init( 0.3f, 0.3f, 0.3f );
	memset( s_SurfaceCacheKey, 0, sizeof( s_SurfaceCacheKey ) );
	ClearBrushEntities();
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		cas.bGridValid = false;
		cas.bClearAtlas = true;
		cas.bInitialUpload = false;
		cas.bBlendReset = true;
		cas.nPending = 0;
		cas.pending.RemoveAll();
		cas.nPendingUnsorted = 0;
		cas.iSweep = 0;
		cas.nSweepsLeft = 0;
		cas.flNextSweep = 0.0f;
		cas.bChanged = false;
		cas.dirty.RemoveAll();
		cas.dirtyNow.RemoveAll();
		if ( cas.pProbes )
		{
			for ( int i = 0; i < cas.nProbes; i++ )
			{
				GIProbe_t &p = cas.pProbes[i];
				p.bAssigned = false;
				p.bTraced = false;
				p.bSunValid = false;
				p.bBusy = false;
				p.bRetrace = false;
				p.bGather = false;
				p.bDirty = false;
				p.bDirtyNow = false;
				p.bInPending = false;
				p.nPasses = 0;
				p.nHits = 0;
			}
		}
	}
	s_nInvalidSolid = 0;
	s_nInvalidClosed = 0;
	s_flMsWorst = 0.0f;
	s_flCameraSky = 1.0f;
	s_nDoneTrace = s_nDoneRefine = s_nDoneSun = s_nDoneGather = s_nDoneTraces = 0;
	s_flStatsStart = Plat_FloatTime();
	s_bFirstFill = true;
}

static void ShutdownGIProbes()
{
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		if ( cas.pProbes )
		{
			for ( int i = 0; i < cas.nProbes; i++ )
			{
				if ( cas.pProbes[i].pHits )
					free( cas.pProbes[i].pHits );
			}
		}
		delete[] cas.pProbes;
		cas.pProbes = NULL;
		cas.order.Purge();
		cas.dirty.Purge();
		cas.dirtyNow.Purge();
		cas.pending.Purge();
	}
	s_bProbesAllocated = false;
}

static void EnsureProbes()
{
	if ( s_bProbesAllocated )
		return;
	s_bProbesAllocated = true;

	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		const GIGridDesc_t &d = s_GridDesc[c];
		for ( int a = 0; a < 3; a++ )
			cas.n[a] = d.n[a];
		cas.nProbes = d.n[0] * d.n[1] * d.n[2];
		cas.flCameraLevel = d.flCameraLevel;
		cas.pProbes = new GIProbe_t[ cas.nProbes ];
		memset( cas.pProbes, 0, sizeof( GIProbe_t ) * cas.nProbes );

		cas.order.SetCount( cas.nProbes );
		for ( int i = 0; i < cas.nProbes; i++ )
			cas.order[i] = i;
		s_pSortCascade = &cas;
		cas.order.Sort( OrderSort );
	}

	InitRayDirs();
	DeferredGI_Reset();
}

static void ResetProbe( GICascade_t &cas, int iSlot, int cx, int cy, int cz, bool bUrgent )
{
	GIProbe_t &p = cas.pProbes[ iSlot ];
	// (counted as pending: no first trace yet, or starting over)
	const bool bWasPending = p.bAssigned && ( !p.bTraced || p.bRetrace );
	bUrgent = bUrgent && p.bAssigned;
	if ( !bWasPending )
		cas.nPending++;
	p.cell[0] = cx;
	p.cell[1] = cy;
	p.cell[2] = cz;
	// (a job of the old cell still out: its result is dropped; its hits stay allocated until then)
	p.gen++;
	p.bAssigned = true;
	p.bTraced = false;
	p.bSunValid = false;
	p.bRetrace = false;
	p.bGather = false;
	p.nPasses = 0;
	p.flValid = 0.0f;
	p.nHits = 0;
	ClearProbeLight( p );
	for ( int i = 0; i < 6; i++ )
		p.freeDist[i] = 0.0f;
	// (written at once as "not traced" with the new cell's stamp: the slot held a probe at the
	// other end of the grid, and the lighting must not take its light for this cell's - unless
	// the atlases are cleared anyway)
	AddDirty( cas, iSlot, bUrgent );
	AddPending( cas, iSlot );
}

// keeps the camera within a cell of its place in the grid; returns whether the grid moved
static bool UpdateGrid( int iCascade, const Vector &vecCamera )
{
	GICascade_t &cas = s_Cascades[ iCascade ];
	const float S = cas.flSpacing;
	const float g[3] = { vecCamera.x / S - GI_OFFSET, vecCamera.y / S - GI_OFFSET, vecCamera.z / S - GI_OFFSET };

	int iMin[3];
	bool bMove = !cas.bGridValid;
	for ( int a = 0; a < 3; a++ )
	{
		const float flHalf = ( a == 2 ) ? cas.flCameraLevel : ( cas.n[a] - 1 ) * 0.5f;
		iMin[a] = cas.iGridMin[a];
		if ( !cas.bGridValid || fabsf( g[a] - ( cas.iGridMin[a] + flHalf ) ) > 1.0f )
		{
			iMin[a] = (int)floorf( g[a] - flHalf + 0.5f );
			bMove = bMove || iMin[a] != cas.iGridMin[a];
		}
	}

	if ( !bMove )
		return false;

	// (a jump - a teleport, a level loaded - moves most of the grid: the atlases are cleared, the
	// probes that stay written again; the slots with a new cell can't all be written in one frame)
	int nMoved = 0;
	for ( int a = 0; a < 3; a++ )
		nMoved = Max( nMoved, abs( iMin[a] - cas.iGridMin[a] ) * cas.nProbes / cas.n[a] );
	const bool bJump = cas.bGridValid && nMoved > cas.nProbes / 4;
	if ( bJump )
	{
		cas.bClearAtlas = true;
		cas.bReuploadAll = true;
	}

	// the cells that came into the grid: all of it, or the slabs it moved by along each axis (a
	// walk to the next cell visited the whole grid - 65536 slots, a few milliseconds a step)
	const bool bWhole = !cas.bGridValid || nMoved >= cas.nProbes;
	int iOldMin[3];
	for ( int a = 0; a < 3; a++ )
	{
		iOldMin[a] = cas.iGridMin[a];
		cas.iGridMin[a] = iMin[a];
	}
	cas.bGridValid = true;

	for ( int a = bWhole ? -1 : 0; a < 3; a++ )
	{
		int lo[3], hi[3];
		for ( int k = 0; k < 3; k++ )
		{
			lo[k] = iMin[k];
			hi[k] = iMin[k] + cas.n[k];
		}
		if ( a >= 0 )
		{
			const int d = iMin[a] - iOldMin[a];
			if ( d == 0 )
				continue;
			if ( d > 0 )
				lo[a] = Max( lo[a], iOldMin[a] + cas.n[a] );
			else
				hi[a] = Min( hi[a], iOldMin[a] );
		}
		for ( int cz = lo[2]; cz < hi[2]; cz++ )
			for ( int cy = lo[1]; cy < hi[1]; cy++ )
				for ( int cx = lo[0]; cx < hi[0]; cx++ )
				{
					const int iSlot = SlotIndex( cas, cx, cy, cz );
					const GIProbe_t &p = cas.pProbes[ iSlot ];
					if ( !p.bAssigned || p.cell[0] != cx || p.cell[1] != cy || p.cell[2] != cz )
						ResetProbe( cas, iSlot, cx, cy, cz, !bJump );
				}
		if ( a < 0 )
			break;
	}
	return true;
}

// the probe of a world cell, if the grid holds it and it can light
static inline const GIProbe_t *LitProbeAt( const GICascade_t &cas, int cx, int cy, int cz )
{
	const GIProbe_t &p = cas.pProbes[ SlotIndex( cas, cx, cy, cz ) ];
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
	const CGIBrushCollide *p[ GI_MAX_CANDIDATES ];
};

static void FindCandidates( const GIBrushSnapshot_t *pSnap, const Vector &vecCenter, float flRadius, GICandidates_t &cand )
{
	cand.n = 0;
	if ( !pSnap )
		return;
	for ( int i = 0; i < pSnap->nEnts && cand.n < GI_MAX_CANDIDATES; i++ )
	{
		const CGIBrushCollide &col = pSnap->pEnts[i].collide;
		// (distance from the sphere's center to the box)
		float flDistSqr = 0.0f;
		for ( int a = 0; a < 3; a++ )
		{
			const float d = ( vecCenter[a] < col.m_vecWorldMins[a] ) ? col.m_vecWorldMins[a] - vecCenter[a]
				: ( ( vecCenter[a] > col.m_vecWorldMaxs[a] ) ? vecCenter[a] - col.m_vecWorldMaxs[a] : 0.0f );
			flDistSqr += d * d;
		}
		if ( flDistSqr <= flRadius * flRadius )
			cand.p[ cand.n++ ] = &col;
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
		const CGIBrushCollide &col = *cand.p[i];
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

static inline void OctEncode( const Vector &n, signed char out[2] )
{
	const float l1 = fabsf( n.x ) + fabsf( n.y ) + fabsf( n.z );
	float x = n.x / Max( l1, 1e-6f ), y = n.y / Max( l1, 1e-6f );
	if ( n.z < 0.0f )
	{
		const float ox = ( 1.0f - fabsf( y ) ) * ( ( x >= 0.0f ) ? 1.0f : -1.0f );
		const float oy = ( 1.0f - fabsf( x ) ) * ( ( y >= 0.0f ) ? 1.0f : -1.0f );
		x = ox;
		y = oy;
	}
	out[0] = (signed char)clamp( (int)floorf( x * 127.0f + 0.5f ), -127, 127 );
	out[1] = (signed char)clamp( (int)floorf( y * 127.0f + 0.5f ), -127, 127 );
}

static inline void OctDecode( const signed char in[2], Vector &n )
{
	float x = in[0] * ( 1.0f / 127.0f ), y = in[1] * ( 1.0f / 127.0f );
	const float z = 1.0f - fabsf( x ) - fabsf( y );
	if ( z < 0.0f )
	{
		const float ox = ( 1.0f - fabsf( y ) ) * ( ( x >= 0.0f ) ? 1.0f : -1.0f );
		const float oy = ( 1.0f - fabsf( x ) ) * ( ( y >= 0.0f ) ? 1.0f : -1.0f );
		x = ox;
		y = oy;
	}
	n.Init( x, y, z );
	VectorNormalize( n );
}

// a hit's place: off the surface along its normal
static inline void HitPos( const Vector &vecTracePos, const matrix3x4_t *pRot, const GIHit_t &h, const Vector &n, Vector &vecPos )
{
	Vector dir;
	VectorRotate( s_vecRayDir[ h.ray % GI_RAYS ], pRot[ h.ray / GI_RAYS ], dir );
	vecPos = vecTracePos + dir * ( h.dist * 0.125f ) + n * 2.0f;
}

static inline float RayLength( float flSpacing )
{
	return Min( GI_RAY_LENGTH * flSpacing / 64.0f, GI_RAY_LENGTH_MAX );
}

// the sun rays of the hits from iFirst on (in place: the job's probe is its own while it runs)
static void TraceSunRays( GIJob_t &job, const GICandidates_t &cand, GIHit_t *pHits, int nHits, int iFirst, const Vector &vecTracePos, bool bValid )
{
	const bool bSun = job.bLight && bValid && job.vecLight.z > -0.1f;
	matrix3x4_t rot[ GI_MAX_PASSES ];
	const int nPasses = ( nHits > 0 ) ? pHits[ nHits - 1 ].ray / GI_RAYS + 1 : 0;
	for ( int i = 0; bSun && i < nPasses; i++ )
		PassRotation( job.cell, i, rot[i] );
	trace_t tr;
	for ( int i = iFirst; i < nHits; i++ )
	{
		GIHit_t &h = pHits[i];
		h.bLit = 0;
		if ( !bSun )
			continue;
		Vector n;
		OctDecode( h.n, n );
		if ( DotProduct( n, job.vecLight ) <= 0.01f )
			continue;
		Vector vecPos;
		HitPos( vecTracePos, rot, h, n, vecPos );
		GITrace( vecPos, vecPos + job.vecLight * GI_SUN_RAY_LENGTH, tr, cand );
		job.nTraces++;
		h.bLit = ( !tr.startsolid && IsSkyHit( tr ) ) ? 1 : 0;
	}
}

// passes of rays up to nPassesTarget (the first one also finds where the probe traces from),
// into a new array of hits (the old ones copied)
static void JobTrace( GIJob_t &job, const GICandidates_t &cand )
{
	const float S = job.flSpacing;
	const Vector vecCell( ( job.cell[0] + GI_OFFSET ) * S, ( job.cell[1] + GI_OFFSET ) * S, ( job.cell[2] + GI_OFFSET ) * S );
	static const Vector s_Axes[6] = { Vector( 1, 0, 0 ), Vector( -1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, -1, 0 ), Vector( 0, 0, 1 ), Vector( 0, 0, -1 ) };

	const bool bFirst = job.nPassesHave == 0;
	const float flRayLength = RayLength( S );
	trace_t tr;

	Vector vecPos = bFirst ? vecCell : job.vecTracePos;
	for ( int i = 0; i < 4; i++ )
		job.visSum[i] = bFirst ? 0.0f : job.visSumIn[i];
	job.nBack = bFirst ? 0 : job.nBackIn;
	job.nStartSolid = bFirst ? 0 : job.nStartSolidIn;
	for ( int i = 0; i < 6; i++ )
		job.freeDist[i] = bFirst ? 0.0f : job.freeDistIn[i];
	const int nOld = bFirst ? 0 : job.nHits;

	if ( bFirst )
	{
		// in a wall: the nearest free point within half a spacing (of the near grid) traces for it
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
				job.vecTracePosOut = vecCell;
				job.nPasses = job.nPassesTarget;
				job.flValid = 0.0f;
				job.nInvalid = 1;
				job.pNewHits = NULL;
				job.nNewHits = 0;
				job.iFirstNewHit = 0;
				return;
			}
		}

		// free distance along the axes: whether the probe sees a point of its 8 cells
		for ( int a = 0; a < 6; a++ )
		{
			GITrace( vecPos, vecPos + s_Axes[a] * S, tr, cand );
			job.nTraces++;
			job.freeDist[a] = ( tr.startsolid || tr.allsolid ) ? 0.0f : tr.fraction * S;
		}
	}

	const int nNewRays = ( job.nPassesTarget - job.nPassesHave ) * GI_RAYS;
	GIHit_t *pHits = (GIHit_t *)malloc( ( nOld + nNewRays ) * sizeof( GIHit_t ) );
	const char **ppSurfaces = (const char **)malloc( Max( nNewRays, 1 ) * sizeof( const char * ) );
	if ( !pHits || !ppSurfaces )
	{
		free( pHits );
		free( (void *)ppSurfaces );
		job.bSkipped = true;
		return;
	}
	if ( nOld )
		memcpy( pHits, job.pHits, nOld * sizeof( GIHit_t ) );

	int nHits = nOld;
	for ( int iPass = job.nPassesHave; iPass < job.nPassesTarget; iPass++ )
	{
		matrix3x4_t matRot;
		PassRotation( job.cell, iPass, matRot );
		for ( int k = 0; k < GI_RAYS; k++ )
		{
			Vector dir;
			VectorRotate( s_vecRayDir[k], matRot, dir );
			GITrace( vecPos, vecPos + dir * flRayLength, tr, cand );
			job.nTraces++;
			// (starting on a surface: the directions into it are simply blocked)
			if ( tr.startsolid || tr.allsolid )
			{
				job.nStartSolid++;
				continue;
			}
			if ( IsSkyHit( tr ) )
			{
				job.visSum[0] += 1.0f;
				job.visSum[1] += 2.0f * dir.x;
				job.visSum[2] += 2.0f * dir.y;
				job.visSum[3] += 2.0f * dir.z;
				continue;
			}
			// the back of a face: the probe is inside something closed (a sealed cavity)
			if ( DotProduct( tr.plane.normal, dir ) > 0.0f )
			{
				job.nBack++;
				continue;
			}
			GIHit_t &h = pHits[ nHits ];
			h.dist = (unsigned short)clamp( (int)( tr.fraction * flRayLength * 8.0f + 0.5f ), 0, 65535 );
			h.ray = (unsigned char)( iPass * GI_RAYS + k );
			h.bLit = 0;
			OctEncode( tr.plane.normal, h.n );
			h.albedo = 0;
			ppSurfaces[ nHits - nOld ] = ( tr.surface.flags & SURF_NODRAW ) ? NULL : tr.surface.name;
			nHits++;
		}
		if ( s_bCancelJobs )
		{
			free( pHits );
			free( (void *)ppSurfaces );
			job.bSkipped = true;
			return;
		}
	}

	const int nRays = job.nPassesTarget * GI_RAYS;
	job.vecTracePosOut = vecPos;
	job.nPasses = job.nPassesTarget;
	job.pNewHits = pHits;
	job.nNewHits = nHits;
	job.iFirstNewHit = nOld;
	job.ppSurfaces = ppSurfaces;
	job.flValid = ( job.nBack * 4 > nRays || job.nStartSolid * 4 > nRays * 3 ) ? 0.0f : 1.0f;
	if ( job.flValid <= 0.0f )
		job.nInvalid = 2;

	// the sun rays: of the new hits, or of all of them if the sun has moved since the others'
	job.bSunAll = bFirst || !job.bSunValidIn || DotProduct( job.vecSunDirIn, job.vecLight ) < cosf( DEG2RAD( r_deferred_gi_sun_refresh.GetFloat() ) );
	TraceSunRays( job, cand, pHits, nHits, job.bSunAll ? 0 : nOld, vecPos, job.flValid > 0.0f );
}

//-----------------------------------------------------------------------------
// The indirect light of a probe from its hits (any thread: reads the other probes
// as they are - a value the main thread is just writing is as good as the one before)
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
		// (the slots of the cell's corners: two along every axis - the grid is toroidal)
		int slot[3][2];
		for ( int a = 0; a < 3; a++ )
		{
			base[a] = (int)g[a];
			if ( g[a] < base[a] )
				base[a]--;
			f[a] = g[a] - base[a];
			bInside = bInside && base[a] >= cas.iGridMin[a] && base[a] + 1 <= cas.iGridMin[a] + cas.n[a] - 1;
			slot[a][0] = PosMod( base[a], cas.n[a] );
			slot[a][1] = ( slot[a][0] + 1 == cas.n[a] ) ? 0 : slot[a][0] + 1;
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
			const GIProbe_t &q = cas.pProbes[ slot[0][kx] + cas.n[0] * ( slot[1][ky] + cas.n[1] * slot[2][kz] ) ];
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

static void JobGather( GIJob_t &job )
{
	const GICascade_t &cas = s_Cascades[ job.iCascade ];

	// the sky visibility, smoothed: the probe's own (twice) and the 6 neighbours it sees
	float vis[4], freeDist[6];
	for ( int i = 0; i < 4; i++ )
		vis[i] = job.visSumIn[i];
	for ( int i = 0; i < 6; i++ )
		freeDist[i] = job.freeDistIn[i];
	if ( r_deferred_gi_filter.GetBool() )
	{
		static const int s_iStep[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		float sum[4] = { vis[0] * 2.0f, vis[1] * 2.0f, vis[2] * 2.0f, vis[3] * 2.0f };
		float flWeight = 2.0f;
		for ( int a = 0; a < 6; a++ )
		{
			if ( freeDist[a] < cas.flSpacing * 0.97f )
				continue;
			const GIProbe_t *q = LitProbeAt( cas, job.cell[0] + s_iStep[a][0], job.cell[1] + s_iStep[a][1], job.cell[2] + s_iStep[a][2] );
			if ( !q )
				continue;
			for ( int i = 0; i < 4; i++ )
				sum[i] += q->vis[i];
			flWeight += 1.0f;
		}
		for ( int i = 0; i < 4; i++ )
			job.visF[i] = sum[i] / flWeight;
	}
	else
	{
		for ( int i = 0; i < 4; i++ )
			job.visF[i] = vis[i];
	}

	const bool bSun = job.bLight && job.vecLight.z > -0.1f;
	const bool bMulti = r_deferred_gi_multibounce.GetBool();

	matrix3x4_t rot[ GI_MAX_PASSES ];
	const int nPasses = clamp( job.nPassesHave, 1, (int)GI_MAX_PASSES );
	for ( int i = 0; i < nPasses; i++ )
		PassRotation( job.cell, i, rot[i] );

	// (at most r_deferred_gi_gather_hits of them, spread over all: the bounced light is a smooth
	// average, and gathering is what the background spends most of its time on)
	const int nMax = r_deferred_gi_gather_hits.GetInt();
	const int nStep = ( job.nHits > nMax ) ? ( job.nHits + nMax - 1 ) / nMax : 1;
	int nUsed = 0;

	float sunSH[4] = { 0, 0, 0, 0 }, skySH[4] = { 0, 0, 0, 0 };
	Vector vecSunRGB( 0, 0, 0 ), vecSkyRGB( 0, 0, 0 );
	for ( int i = 0; i < job.nHits; i += nStep )
	{
		const GIHit_t &h = job.pHits[i];
		nUsed++;
		Vector n;
		OctDecode( h.n, n );
		const Vector &vecAlbedo = s_Albedo[ ( h.albedo < GI_MAX_ALBEDO ) ? h.albedo : 0 ];
		Vector vecPos;
		HitPos( job.vecTracePos, rot, h, n, vecPos );

		// what reaches the hit: the sun, the sky it sees, the indirect light at it
		const float flLit = ( bSun && h.bLit ) ? Max( 0.0f, DotProduct( n, job.vecLight ) ) : 0.0f;
		float flVisHit = 0.0f;
		Vector vecIndSun( 0, 0, 0 ), vecIndSky( 0, 0, 0 );
		if ( !FieldLookup( vecPos, n, flVisHit, vecIndSun, vecIndSky ) )
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

		Vector dir = vecPos - job.vecTracePos;
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

	// (the hits used stand for all of them; every ray of the probe counts)
	const float flInv = ( nUsed > 0 ) ? ( (float)job.nHits / nUsed ) / ( nPasses * GI_RAYS ) : 0.0f;
	for ( int i = 0; i < 4; i++ )
	{
		job.sun[i] = sunSH[i] * flInv;
		job.sky[i] = skySH[i] * flInv;
	}
	const float flLumSun = Lum( vecSunRGB ), flLumSky = Lum( vecSkyRGB );
	job.vecSunChroma = ( flLumSun > 1e-5f ) ? vecSunRGB / flLumSun : Vector( 1, 1, 1 );
	job.vecSkyChroma = ( flLumSky > 1e-5f ) ? vecSkyRGB / flLumSky : Vector( 1, 1, 1 );
}

static void ProcessJob( GIJob_t &job )
{
	if ( job.iType == GIJOB_GATHER )
	{
		JobGather( job );
		return;
	}

	GICandidates_t cand;
	// (the probe's rays, and the rays from where they end toward the sun)
	const float S = job.flSpacing;
	FindCandidates( job.pBrushes, Vector( ( job.cell[0] + GI_OFFSET ) * S, ( job.cell[1] + GI_OFFSET ) * S, ( job.cell[2] + GI_OFFSET ) * S ),
		RayLength( S ) + GI_SUN_RAY_LENGTH + S, cand );

	if ( job.iType == GIJOB_TRACE )
		JobTrace( job, cand );
	else
		TraceSunRays( job, cand, job.pHits, job.nHits, 0, job.vecTracePos, true );
}

//-----------------------------------------------------------------------------
// Jobs: the main thread part
//-----------------------------------------------------------------------------
// average color of a surface (vtex stores it in every VTF) -> its index in s_Albedo
static unsigned short SurfaceAlbedo( const char *pszSurface )
{
	if ( !pszSurface || !*pszSurface || pszSurface[0] == '*' )
		return 0;

	const uintp key = (uintp)pszSurface;
	const int iCache = (int)( ( key >> 3 ) ^ ( key >> 13 ) ) & ( GI_SURFACE_CACHE - 1 );
	if ( s_SurfaceCacheKey[ iCache ] == key )
		return s_SurfaceCacheVal[ iCache ];

	unsigned short iAlbedo = 0;
	const unsigned short idx = s_AlbedoMap.Find( key );
	if ( idx != s_AlbedoMap.InvalidIndex() )
		iAlbedo = s_AlbedoMap[ idx ];
	else
	{
		// (only a material the map has loaded anyway: looking up one that isn't - the cubemap
		// patched water materials of a map compiled without them - reads the disk and fails, a few
		// milliseconds each, in the middle of a frame)
		if ( s_nAlbedo < GI_MAX_ALBEDO && materials->IsMaterialLoaded( pszSurface ) )
		{
			IMaterial *pMat = materials->FindMaterial( pszSurface, TEXTURE_GROUP_WORLD, false );
			if ( pMat && !pMat->IsErrorMaterial() )
			{
				Vector vecAlbedo;
				pMat->GetReflectivity( vecAlbedo );
				// (no surface throws back more than ~80 %: whiter than that and the bounces never end)
				for ( int i = 0; i < 3; i++ )
					vecAlbedo[i] = clamp( vecAlbedo[i], 0.02f, 0.8f );
				iAlbedo = (unsigned short)s_nAlbedo;
				s_Albedo[ s_nAlbedo++ ] = vecAlbedo;
			}
		}
		s_AlbedoMap.Insert( key, iAlbedo );
	}
	s_SurfaceCacheKey[ iCache ] = key;
	s_SurfaceCacheVal[ iCache ] = iAlbedo;
	return iAlbedo;
}

// passes a probe is to have: near the camera, farther in the near grid, in the far grid (this frame)
static int s_nPassesHQ = 6, s_nPassesMid = 3, s_nPassesFar = 2;
static float s_flHQRadiusSqr = 1024.0f * 1024.0f;

static inline int RaysToPasses( float flRays )
{
	return clamp( (int)( flRays / GI_RAYS + 0.5f ), 1, (int)GI_MAX_PASSES );
}

static void UpdateTargetPasses()
{
	s_nPassesHQ = RaysToPasses( r_deferred_gi_rays.GetFloat() );
	s_nPassesMid = Min( s_nPassesHQ, RaysToPasses( r_deferred_gi_rays_mid.GetFloat() ) );
	s_nPassesFar = RaysToPasses( r_deferred_gi_rays_far.GetFloat() );
	s_flHQRadiusSqr = r_deferred_gi_hq_radius.GetFloat() * r_deferred_gi_hq_radius.GetFloat();
}

static inline int TargetPasses( int iCascade, float flDistSqr )
{
	if ( iCascade == 0 )
		return ( flDistSqr <= s_flHQRadiusSqr ) ? s_nPassesHQ : s_nPassesMid;
	return s_nPassesFar;
}

static GIJob_t *NewJob( GICascade_t &cas, int iCascade, GIProbe_t &p, int iType, const Vector &vecLight, bool bLight )
{
	GIJob_t *pJob = s_FreeJobs.Count() ? s_FreeJobs.Tail() : new GIJob_t;
	if ( s_FreeJobs.Count() )
		s_FreeJobs.RemoveMultipleFromTail( 1 );
	memset( pJob, 0, sizeof( GIJob_t ) );
	pJob->pProbe = &p;
	pJob->gen = p.gen;
	pJob->iCascade = iCascade;
	pJob->iType = iType;
	for ( int a = 0; a < 3; a++ )
		pJob->cell[a] = p.cell[a];
	pJob->flSpacing = cas.flSpacing;
	pJob->vecLight = vecLight;
	pJob->bLight = bLight;
	pJob->vecTracePos = p.vecTracePos;
	pJob->pHits = p.pHits;
	pJob->nHits = p.nHits;
	pJob->nPassesHave = p.nPasses;
	for ( int i = 0; i < 4; i++ )
		pJob->visSumIn[i] = ( iType == GIJOB_GATHER ) ? p.vis[i] : p.visSum[i];
	pJob->nBackIn = p.nBack;
	pJob->nStartSolidIn = p.nStartSolid;
	for ( int i = 0; i < 6; i++ )
		pJob->freeDistIn[i] = p.freeDist[i];
	pJob->bSunValidIn = p.bSunValid;
	pJob->vecSunDirIn = p.vecSunDir;
	if ( iType != GIJOB_GATHER )
	{
		pJob->pBrushes = s_pBrushSnap;
		if ( s_pBrushSnap )
			s_pBrushSnap->nRefs++;
	}
	return pJob;
}

static void SubmitTrace( GICascade_t &cas, int iCascade, GIProbe_t &p, bool bFresh, int nPassesTarget, const Vector &vecLight, bool bLight )
{
	GIJob_t *pJob = NewJob( cas, iCascade, p, GIJOB_TRACE, vecLight, bLight );
	if ( bFresh )
	{
		pJob->nPassesHave = 0;
		pJob->nHits = 0;
	}
	pJob->nPassesTarget = Max( nPassesTarget, pJob->nPassesHave + 1 );
	pJob->nPassesTarget = Min( pJob->nPassesTarget, (int)GI_MAX_PASSES );
	SubmitJob( pJob );
}

static void MarkNeighboursGather( GICascade_t &cas, const GIProbe_t &p )
{
	for ( int dz = -1; dz <= 1; dz++ )
		for ( int dy = -1; dy <= 1; dy++ )
			for ( int dx = -1; dx <= 1; dx++ )
			{
				const int cx = p.cell[0] + dx, cy = p.cell[1] + dy, cz = p.cell[2] + dz;
				GIProbe_t &q = cas.pProbes[ SlotIndex( cas, cx, cy, cz ) ];
				if ( q.bAssigned && q.bTraced && q.cell[0] == cx && q.cell[1] == cy && q.cell[2] == cz )
					q.bGather = true;
			}
}

static bool ProbeChangedVisibly( const GIProbe_t &p )
{
	const float *now[3] = { p.visF, p.sun, p.sky };
	for ( int k = 0; k < 3; k++ )
	{
		for ( int i = 0; i < 4; i++ )
		{
			const float a = now[k][i], b = p.uploaded[ k * 4 + i ];
			if ( fabsf( a - b ) > 0.002f + 0.01f * fabsf( b ) )
				return true;
		}
	}
	return false;
}

static void IntegrateJob( GIJob_t *pJob )
{
	GIJob_t &job = *pJob;
	GICascade_t &cas = s_Cascades[ job.iCascade ];
	GIProbe_t &p = *job.pProbe;
	const int iSlot = (int)( &p - cas.pProbes );
	s_nOutstanding--;
	p.bBusy = false;
	ReleaseBrushSnapshot( job.pBrushes );

	const bool bCurrent = !job.bSkipped && job.gen == p.gen && p.bAssigned && s_bProbesAllocated;
	if ( !bCurrent )
	{
		free( job.pNewHits );
		free( (void *)job.ppSurfaces );
		FreeJob( pJob );
		// (still waiting for its trace: back in the list)
		if ( s_bProbesAllocated && p.bAssigned && ( !p.bTraced || p.bRetrace ) )
			AddPending( cas, iSlot );
		return;
	}

	s_nDoneTraces += job.nTraces;

	if ( job.iType == GIJOB_TRACE )
	{
		const bool bFirstTrace = !p.bTraced || p.bRetrace;
		for ( int i = job.iFirstNewHit; i < job.nNewHits; i++ )
			job.pNewHits[i].albedo = SurfaceAlbedo( job.ppSurfaces[ i - job.iFirstNewHit ] );
		free( (void *)job.ppSurfaces );
		job.ppSurfaces = NULL;

		// (the old hits were only read by this job: nothing else holds them now)
		if ( p.pHits )
			free( p.pHits );
		p.pHits = job.pNewHits;
		p.nHits = job.nNewHits;
		job.pNewHits = NULL;

		p.vecTracePos = job.vecTracePosOut;
		for ( int i = 0; i < 6; i++ )
			p.freeDist[i] = job.freeDist[i];
		p.nBack = job.nBack;
		p.nStartSolid = job.nStartSolid;
		p.nPasses = (unsigned char)job.nPasses;
		p.flValid = job.flValid;
		const float flInv = 1.0f / ( Max( 1, job.nPasses ) * GI_RAYS );
		for ( int i = 0; i < 4; i++ )
		{
			p.visSum[i] = job.visSum[i];
			p.vis[i] = ( job.flValid > 0.0f ) ? job.visSum[i] * flInv : 0.0f;
		}
		if ( bFirstTrace )
		{
			// (until the gather smooths it: the probe lights with its own sky visibility)
			for ( int i = 0; i < 4; i++ )
				p.visF[i] = p.vis[i];
			if ( !p.bTraced )
			{
				for ( int i = 0; i < 4; i++ )
				{
					p.sun[i] = 0.0f;
					p.sky[i] = 0.0f;
				}
			}
			cas.nPending--;
			if ( job.nInvalid == 1 )
				s_nInvalidSolid++;
			else if ( job.nInvalid == 2 )
				s_nInvalidClosed++;
			s_nDoneTrace++;
		}
		else
			s_nDoneRefine++;
		p.bTraced = true;
		p.bRetrace = false;
		if ( job.bSunAll )
		{
			p.vecSunDir = job.vecLight;
			p.bSunValid = true;
		}
		if ( p.flValid > 0.0f )
		{
			p.bGather = true;
			MarkNeighboursGather( cas, p );
		}
		cas.bChanged = true;
		// (its first light and its validity, at once - the gather follows)
		AddDirty( cas, iSlot, false );
	}
	else if ( job.iType == GIJOB_SUN )
	{
		s_nDoneSun++;
		p.vecSunDir = job.vecLight;
		p.bSunValid = true;
		p.bGather = true;
	}
	else
	{
		s_nDoneGather++;
		for ( int i = 0; i < 4; i++ )
		{
			p.visF[i] = job.visF[i];
			p.sun[i] = job.sun[i];
			p.sky[i] = job.sky[i];
		}
		p.vecSunChroma = job.vecSunChroma;
		p.vecSkyChroma = job.vecSkyChroma;
		if ( ProbeChangedVisibly( p ) )
			AddDirty( cas, iSlot, false );
	}
	FreeJob( pJob );
}

// the waiting probes nearest to the camera first (a counting sort by distance: after a teleport
// or a new map the list holds the whole grid in slot order)
static void SortPending( GICascade_t &cas )
{
	cas.nPendingUnsorted = 0;
	const int n = cas.pending.Count();
	if ( n < 2 )
		return;
	CUtlVector< unsigned char > key;
	key.SetCount( n );
	int counts[ 257 ];
	memset( counts, 0, sizeof( counts ) );
	const float flInvS = 1.0f / cas.flSpacing;
	for ( int i = 0; i < n; i++ )
	{
		Vector vecPos;
		ProbePos( cas, cas.pProbes[ cas.pending[i] ].cell, vecPos );
		Vector d = vecPos - s_vecCamera;
		d.z *= 1.41f;	// (other floors: height counts a bit more)
		key[i] = (unsigned char)Min( 255, (int)( d.Length() * flInvS ) );
		counts[ key[i] + 1 ]++;
	}
	for ( int b = 0; b < 256; b++ )
		counts[ b + 1 ] += counts[b];
	CUtlVector< int > sorted;
	sorted.SetCount( n );
	for ( int i = 0; i < n; i++ )
		sorted[ counts[ key[i] ]++ ] = cas.pending[i];
	cas.pending.Swap( sorted );
}

// probes of one grid without their first trace (or starting over), nearest first
static int SchedulePending( int iCascade, int nMax, const Vector &vecLight, bool bLight )
{
	GICascade_t &cas = s_Cascades[ iCascade ];
	if ( nMax <= 0 || cas.pending.Count() == 0 || !cas.bGridValid )
		return 0;
	if ( cas.nPendingUnsorted > 1024 )
		SortPending( cas );
	int nAdded = 0;
	int i = 0;
	CUtlVector< int > busy;
	for ( ; i < cas.pending.Count() && nAdded < nMax; i++ )
	{
		const int iSlot = cas.pending[i];
		GIProbe_t &p = cas.pProbes[ iSlot ];
		if ( !p.bAssigned || ( p.bTraced && !p.bRetrace ) )
		{
			p.bInPending = false;
			continue;
		}
		// (a job of it is out: it waits on at the end of the list)
		if ( p.bBusy )
		{
			busy.AddToTail( iSlot );
			continue;
		}
		p.bInPending = false;
		Vector vecPos;
		ProbePos( cas, p.cell, vecPos );
		SubmitTrace( cas, iCascade, p, true, TargetPasses( iCascade, vecPos.DistToSqr( s_vecCamera ) ), vecLight, bLight );
		nAdded++;
	}
	cas.pending.RemoveMultipleFromHead( i );
	cas.pending.AddVectorToTail( busy );
	return nAdded;
}

// The background: the probes in a scattered order (a sweep in slot order was seen as the light
// of the grid changing slab by slab) - more rays for the ones the camera has come close to, the
// sun rays where the sun has moved on, the light of the ones that want it gathered again; while
// rounds of gathering everything are left, every probe.
static int ScheduleBackground( int iCascade, int nMax, int nVisits, const Vector &vecLight, bool bLight, float flSunCos )
{
	GICascade_t &cas = s_Cascades[ iCascade ];
	if ( nMax <= 0 || !cas.bGridValid )
		return 0;
	int nAdded = 0;
	// (rows of the grid along x in a scattered order, the probes of a row one after another: the
	// probes are visited where they lie in memory)
	const int nx = cas.n[0];
	const unsigned int nRows = (unsigned int)( cas.nProbes / nx );
	for ( int v = 0; v < nVisits && nAdded < nMax; v++ )
	{
		const unsigned int iRow = ( (unsigned int)( cas.iSweep / nx ) * (unsigned int)GI_SWEEP_STRIDE ) % nRows;
		const int iSlot = (int)iRow * nx + cas.iSweep % nx;
		if ( ++cas.iSweep >= cas.nProbes )
		{
			cas.iSweep = 0;
			if ( cas.nSweepsLeft > 0 )
				cas.nSweepsLeft--;
		}
		GIProbe_t &p = cas.pProbes[ iSlot ];
		if ( !p.bAssigned || !p.bTraced || p.bBusy || p.bRetrace || p.flValid <= 0.0f )
			continue;

		int nTarget = p.nPasses;
		if ( p.nPasses < s_nPassesHQ )
		{
			Vector vecPos;
			ProbePos( cas, p.cell, vecPos );
			nTarget = TargetPasses( iCascade, vecPos.DistToSqr( s_vecCamera ) );
		}
		// (nothing but sky around it: two passes tell as much as eight)
		if ( p.nPasses < nTarget && !( p.nHits == 0 && p.nPasses >= 2 ) )
			SubmitTrace( cas, iCascade, p, false, nTarget, vecLight, bLight );
		else if ( p.nHits > 0 && ( !p.bSunValid || DotProduct( p.vecSunDir, vecLight ) < flSunCos ) )
			SubmitJob( NewJob( cas, iCascade, p, GIJOB_SUN, vecLight, bLight ) );
		else if ( p.bGather || cas.nSweepsLeft > 0 )
		{
			p.bGather = false;
			SubmitJob( NewJob( cas, iCascade, p, GIJOB_GATHER, vecLight, bLight ) );
		}
		else
			continue;
		nAdded++;
	}
	return nAdded;
}

static void ScheduleJobs( const Vector &vecLight, bool bLight )
{
	const int nCascades = r_deferred_gi_far.GetBool() ? GI_CASCADES : 1;
	const float flSunCos = cosf( DEG2RAD( r_deferred_gi_sun_refresh.GetFloat() ) );

	// a round of gathering everything again, now and then while things change: the light
	// spreads farther than the neighbours of what was traced
	for ( int c = 0; c < nCascades; c++ )
	{
		GICascade_t &cas = s_Cascades[c];
		if ( cas.bChanged && cas.nSweepsLeft <= 0 && gpGlobals->realtime >= cas.flNextSweep )
		{
			cas.bChanged = false;
			cas.nSweepsLeft = 1;
			cas.flNextSweep = gpGlobals->realtime + r_deferred_gi_sweep_interval.GetFloat();
		}
	}

	// (a frame's work of the workers ahead - a gather takes some 30 us, a trace a millisecond: what
	// is wanted most is decided again every frame)
	const int nCap = Max( 1, s_nWorkers ) * 256;
	int nFree = nCap - s_nOutstanding;
	if ( nFree <= 0 )
		return;

	// first traces of the near grid, a quarter kept for the far one (a walking player would
	// starve it otherwise), then what the grids have left
	const bool bFarWork = nCascades > 1 && s_Cascades[1].nPending > 0;
	nFree -= SchedulePending( 0, bFarWork ? nFree - nFree / 4 : nFree, vecLight, bLight );
	if ( nCascades > 1 )
		nFree -= SchedulePending( 1, nFree, vecLight, bLight );
	nFree -= SchedulePending( 0, nFree, vecLight, bLight );

	for ( int c = 0; c < nCascades && nFree > 0; c++ )
		nFree -= ScheduleBackground( c, ( c == 0 && nCascades > 1 ) ? nFree - nFree / 4 : nFree, 2048, vecLight, bLight, flSunCos );
}

// The first frame of a map: everything around the camera at once (nobody sees this frame take
// longer) - the near grid within the radius of the full ray count, the far grid, the rest of the
// near one; then one round of gathering. What the time doesn't allow follows in the background.
static void FirstFill( const Vector &vecLight, bool bLight )
{
	const double flStart = Plat_FloatTime();
	const double flEnd = flStart + r_deferred_gi_load_ms.GetFloat() * 0.001;
	const int nCascades = r_deferred_gi_far.GetBool() ? GI_CASCADES : 1;

	GICascade_t &nearCas = s_Cascades[0];
	const float flRadius = r_deferred_gi_hq_radius.GetFloat();
	int nNearFirst = 0;
	while ( nNearFirst < nearCas.nProbes )
	{
		Vector vecPos;
		ProbePos( nearCas, nearCas.pProbes[ OrderedSlot( nearCas, nNearFirst ) ].cell, vecPos );
		if ( vecPos.DistToSqr( s_vecCamera ) > flRadius * flRadius )
			break;
		nNearFirst++;
	}
	int nSubmitted = SchedulePending( 0, nNearFirst, vecLight, bLight );
	if ( nCascades > 1 )
		nSubmitted += SchedulePending( 1, s_Cascades[1].nProbes, vecLight, bLight );
	nSubmitted += SchedulePending( 0, nearCas.nProbes, vecLight, bLight );

	while ( Plat_FloatTime() < flEnd )
	{
		HelpWithJobs( flEnd );
		CollectJobs( 0.0 );
		if ( s_nOutstanding <= 0 )
			break;
		ThreadSleep( 0 );
	}
	CollectJobs( 0.0 );

	// one round of gathering (the bounces of the bounces follow in the background)
	if ( s_nOutstanding <= 0 )
	{
		for ( int c = 0; c < nCascades; c++ )
		{
			GICascade_t &cas = s_Cascades[c];
			for ( int i = 0; i < cas.nProbes; i++ )
			{
				GIProbe_t &p = cas.pProbes[i];
				if ( !p.bAssigned || !p.bTraced || p.bBusy || p.flValid <= 0.0f )
					continue;
				p.bGather = false;
				SubmitJob( NewJob( cas, c, p, GIJOB_GATHER, vecLight, bLight ) );
			}
			cas.nSweepsLeft = 2;
			cas.iSweep = 0;
		}
		while ( Plat_FloatTime() < flEnd )
		{
			HelpWithJobs( flEnd );
			CollectJobs( 0.0 );
			if ( s_nOutstanding <= 0 )
				break;
			ThreadSleep( 0 );
		}
		CollectJobs( 0.0 );
	}

	for ( int c = 0; c < nCascades; c++ )
	{
		s_Cascades[c].bInitialUpload = true;
		s_Cascades[c].bBlendReset = true;
	}
	s_flFirstFillMs = (float)( ( Plat_FloatTime() - flStart ) * 1000.0 );
	s_nFirstFillDone = nSubmitted - s_nOutstanding;
	s_nFirstFillLeft = s_nOutstanding;
	s_bFirstFill = false;
}

static void UpdateGIWork( const Vector &vecLight, bool bLight )
{
	const double flStart = Plat_FloatTime();
	UpdateTargetPasses();
	if ( s_bFirstFill )
	{
		FirstFill( vecLight, bLight );
		return;
	}

	// (no workers: the main thread traces for a while itself)
	if ( s_nWorkers <= 0 )
		HelpWithJobs( flStart + r_deferred_gi_budget_ms.GetFloat() * 0.001 );

	double flMark = flStart;
	s_nCollected += CollectJobs( flStart + r_deferred_gi_budget_ms.GetFloat() * 0.001 );
	PhaseTime( GIPHASE_COLLECT, flMark );
	ScheduleJobs( vecLight, bLight );
	FlushJobs();
	PhaseTime( GIPHASE_SCHEDULE, flMark );
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
// Upload: one texel per probe and plane, written as points (or tiny quads) in clip
// space into the target atlas; the display atlas the lighting reads eases toward it
//-----------------------------------------------------------------------------
static inline void EmitTexel( CMeshBuilder &mb, bool bPoints, int u, int v, int w, int h, const float *d )
{
	// (D3D9: pixel u's center is at screen x = u; a point of size 1 or the quad covers only it)
	const float x = u * ( 2.0f / w ) - 1.0f;
	const float y = 1.0f - v * ( 2.0f / h );
	if ( bPoints )
	{
		mb.Position3f( x, y, 0.0f );
		mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
		mb.AdvanceVertex();
		return;
	}
	const float dx = 0.4f * ( 2.0f / w ), dy = 0.4f * ( 2.0f / h );
	mb.Position3f( x - dx, y + dy, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x + dx, y + dy, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x + dx, y - dy, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
	mb.Position3f( x - dx, y - dy, 0.0f );
	mb.TexCoord4f( 0, d[0], d[1], d[2], d[3] );
	mb.AdvanceVertex();
}

// (which cell a slot holds, for the display atlas to tell a new probe in it: never 0 - the
// cleared atlas)
static inline float CellStamp( const int cell[3] )
{
	return (float)( 1 + ( ( (unsigned int)cell[0] * 73u + (unsigned int)cell[1] * 151u + (unsigned int)cell[2] * 283u ) & 1023u ) );
}

static void EmitProbe( CMeshBuilder &mb, bool bPoints, const GICascade_t &cas, int iSlot, GIProbe_t &p )
{
	const int nx = cas.n[0], ny = cas.n[1];
	const int sx = iSlot % nx;
	const int sy = ( iSlot / nx ) % ny;
	const int sz = iSlot / ( nx * ny );
	const int u = sx + sz * nx;
	const int w = nx * cas.n[2], h = ny * GI_PLANES;

	const bool bValid = p.bTraced && p.flValid > 0.0f;
	float d[4];

	// 0: sky visibility
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.visF[i] : 0.0f;
	EmitTexel( mb, bPoints, u, sy, w, h, d );

	// 1: indirect sun light (luminance)
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.sun[i] : 0.0f;
	EmitTexel( mb, bPoints, u, sy + ny, w, h, d );

	// 2: its color, weight (0 = in a wall / not traced yet; the display atlas fades it in)
	d[0] = p.vecSunChroma.x;
	d[1] = p.vecSunChroma.y;
	d[2] = p.vecSunChroma.z;
	d[3] = bValid ? 1.0f : 0.0f;
	EmitTexel( mb, bPoints, u, sy + ny * 2, w, h, d );

	// 3: free distances, 5 bits each: +axis * 32 + -axis; the cell's stamp
	for ( int a = 0; a < 3; a++ )
	{
		const int qPos = clamp( (int)( p.freeDist[ a * 2 ] / cas.flSpacing * 31.0f + 0.5f ), 0, 31 );
		const int qNeg = clamp( (int)( p.freeDist[ a * 2 + 1 ] / cas.flSpacing * 31.0f + 0.5f ), 0, 31 );
		d[a] = (float)( qPos * 32 + qNeg );
	}
	d[3] = p.bAssigned ? CellStamp( p.cell ) : 0.0f;
	EmitTexel( mb, bPoints, u, sy + ny * 3, w, h, d );

	// 4: bounced sky light (luminance)
	for ( int i = 0; i < 4; i++ )
		d[i] = bValid ? p.sky[i] : 0.0f;
	EmitTexel( mb, bPoints, u, sy + ny * 4, w, h, d );

	// 5: its color
	d[0] = p.vecSkyChroma.x;
	d[1] = p.vecSkyChroma.y;
	d[2] = p.vecSkyChroma.z;
	d[3] = 0.0f;
	EmitTexel( mb, bPoints, u, sy + ny * 5, w, h, d );

	for ( int i = 0; i < 4; i++ )
	{
		p.uploaded[i] = p.visF[i];
		p.uploaded[ 4 + i ] = p.sun[i];
		p.uploaded[ 8 + i ] = p.sky[i];
	}
}

// At most nMax probes a frame, the slots with a new cell first; the rest the next frames. The
// texels go through the material system's call queue (mat_queue_mode 2), which keeps the vertices
// of a frame's dynamic meshes in a memory stack of 16 MB: writing every probe of both grids at
// once ran the stack over ("failed to commit", a crash in materialsystem.dll).
static int UploadProbes( GICascade_t &cas, int nMax )
{
	if ( !cas.texTarget.IsValid() || !cas.pProbes )
		return 0;

	CMatRenderContextPtr pRenderContext( materials );
	const int w = cas.n[0] * cas.n[2], h = cas.n[1] * GI_PLANES;

	if ( cas.bClearAtlas )
	{
		pRenderContext->PushRenderTargetAndViewport( cas.texTarget, 0, 0, w, h );
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
		for ( int i = 0; i < 2; i++ )
		{
			pRenderContext->PushRenderTargetAndViewport( cas.texDisplay[i], 0, 0, w, h );
			pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			pRenderContext->PopRenderTargetAndViewport();
		}
		cas.bClearAtlas = false;
		cas.bBlendReset = true;
	}

	if ( cas.bReuploadAll )
	{
		for ( int i = 0; i < cas.nProbes; i++ )
		{
			if ( cas.pProbes[i].bAssigned )
				AddDirty( cas, i, false );
		}
		cas.bReuploadAll = false;
		cas.bInitialUpload = true;
	}

	if ( cas.dirtyNow.Count() + cas.dirty.Count() == 0 )
		return 0;

	CUtlVector< int > slots;
	slots.EnsureCapacity( Min( nMax, cas.dirtyNow.Count() + cas.dirty.Count() ) );
	// (the slots with a new cell all of them: the lighting must not read a probe of the other end
	// of the grid for this cell)
	const int nNow = Min( cas.dirtyNow.Count(), Max( nMax, 4096 ) );
	for ( int i = 0; i < nNow; i++ )
	{
		cas.pProbes[ cas.dirtyNow[i] ].bDirtyNow = false;
		cas.pProbes[ cas.dirtyNow[i] ].bDirty = false;
		slots.AddToTail( cas.dirtyNow[i] );
	}
	cas.dirtyNow.RemoveMultipleFromHead( nNow );
	int iTaken = 0;
	for ( ; iTaken < cas.dirty.Count() && slots.Count() < nMax; iTaken++ )
	{
		GIProbe_t &p = cas.pProbes[ cas.dirty[ iTaken ] ];
		if ( !p.bDirty )
			continue;	// (already written as a new cell)
		p.bDirty = false;
		slots.AddToTail( cas.dirty[ iTaken ] );
	}
	cas.dirty.RemoveMultipleFromHead( iTaken );
	if ( slots.Count() == 0 )
		return 0;

	const bool bPoints = r_deferred_gi_upload_points.GetBool();
	const int nPerBatch = bPoints ? 2048 : 512;
	pRenderContext->PushRenderTargetAndViewport( cas.texTarget, 0, 0, w, h );
	pRenderContext->Bind( g_pMatGIUpload );
	for ( int iFirst = 0; iFirst < slots.Count(); iFirst += nPerBatch )
	{
		const int n = Min( nPerBatch, slots.Count() - iFirst );
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder mb;
		mb.Begin( pMesh, bPoints ? MATERIAL_POINTS : MATERIAL_QUADS, n * GI_PLANES );
		for ( int i = 0; i < n; i++ )
		{
			const int iSlot = slots[ iFirst + i ];
			EmitProbe( mb, bPoints, cas, iSlot, cas.pProbes[ iSlot ] );
		}
		mb.End();
		pMesh->Draw();
	}
	pRenderContext->PopRenderTargetAndViewport();

	cas.flLastUpload = gpGlobals->realtime;
	return slots.Count();
}

// the display atlas one step toward the target (ping-pong)
static void BlendAtlas( GICascade_t &cas )
{
	const float flSmooth = r_deferred_gi_smooth.GetFloat();
	const float flFade = r_deferred_gi_fade.GetFloat();
	// (nothing written for a while: everything has arrived)
	if ( !cas.bBlendReset && !cas.bInitialUpload && gpGlobals->realtime - cas.flLastUpload > 6.0f * flSmooth + 2.0f * flFade + 0.5f )
		return;

	const float dt = clamp( gpGlobals->absoluteframetime, 0.0f, 0.25f );
	const bool bReset = cas.bBlendReset || cas.bInitialUpload;
	const int iFrom = cas.iDisplay, iTo = 1 - cas.iDisplay;
	IMaterial *pMat = cas.pMatBlend[ iFrom ];
	const int w = cas.n[0] * cas.n[2], h = cas.n[1] * GI_PLANES;

	bool bFound = false;
	IMaterialVar *pVar = pMat->FindVar( "$blendparams", &bFound, false );
	if ( pVar && bFound )
		pVar->SetVecValue( ( flSmooth > 0.001f ) ? 1.0f - expf( -dt / flSmooth ) : 1.0f, ( flFade > 0.001f ) ? dt / flFade : 1.0f, (float)cas.n[1], bReset ? 1.0f : 0.0f );
	pVar = pMat->FindVar( "$atlassize", &bFound, false );
	if ( pVar && bFound )
		pVar->SetVecValue( (float)w, (float)h, 1.0f / w, 1.0f / h );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( cas.texDisplay[ iTo ], 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( pMat, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();
	cas.iDisplay = iTo;

	cas.bBlendReset = false;
	// (the first fill is written over a few frames: until it all is, the lighting reads it as it is)
	if ( cas.bInitialUpload && cas.dirty.Count() == 0 && cas.dirtyNow.Count() == 0 )
		cas.bInitialUpload = false;
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
	for ( int i = 0; i < cas.nProbes; i++ )
	{
		const GIProbe_t &p = cas.pProbes[i];
		if ( !p.bAssigned )
			continue;
		Vector vecPos;
		ProbePos( cas, p.cell, vecPos );
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
	int nHitsTotal = 0;
	for ( int c = 0; c < GI_CASCADES; c++ )
	{
		const GICascade_t &cas = s_Cascades[c];
		int nAssigned = 0, nTraced = 0, nValid = 0, nSun = 0, nPasses = 0, nBusy = 0, nGather = 0, nUnder = 0;
		for ( int i = 0; i < cas.nProbes; i++ )
		{
			const GIProbe_t &p = cas.pProbes[i];
			nAssigned += p.bAssigned ? 1 : 0;
			nTraced += p.bTraced ? 1 : 0;
			nSun += p.bSunValid ? 1 : 0;
			nBusy += p.bBusy ? 1 : 0;
			nGather += p.bGather ? 1 : 0;
			if ( p.bTraced && p.flValid > 0.0f )
			{
				nValid++;
				nPasses += p.nPasses;
				nHitsTotal += p.nHits;
				Vector vecPos;
				ProbePos( cas, p.cell, vecPos );
				if ( p.nPasses < TargetPasses( c, vecPos.DistToSqr( s_vecCamera ) ) && !( p.nHits == 0 && p.nPasses >= 2 ) )
					nUnder++;
			}
		}
		Msg( "[gi] %s grid %dx%dx%d at %d %d %d (spacing %.0f): %d probes, %d traced, %d valid (%.1f passes on average, %d waiting for more), %d sun, %d pending, %d busy, %d want gathering, sweeps left %d, upload queue %d + %d\n",
			c ? "far" : "near", cas.n[0], cas.n[1], cas.n[2], cas.iGridMin[0], cas.iGridMin[1], cas.iGridMin[2], cas.flSpacing,
			nAssigned, nTraced, nValid, nValid ? (float)nPasses / nValid : 0.0f, nUnder, nSun, cas.nPending, nBusy, nGather, cas.nSweepsLeft,
			cas.dirtyNow.Count(), cas.dirty.Count() );
	}
	const float flSecs = Max( 0.001f, (float)( Plat_FloatTime() - s_flStatsStart ) );
	Msg( "[gi] %d workers, %d jobs out (%d queued); since the map loaded (%.0f s): %d traced, %d refined, %d sun, %d gathered, %d rays (%.0f rays/s)\n",
		s_nWorkers, s_nOutstanding, QueuedJobs(), flSecs, s_nDoneTrace, s_nDoneRefine, s_nDoneSun, s_nDoneGather, s_nDoneTraces, s_nDoneTraces / flSecs );
	Msg( "[gi] first fill %.0f ms (%d jobs done, %d left to the background); main thread last frame %.2f ms (worst %.2f ms), %d probes written; sky at the camera %.2f\n",
		s_flFirstFillMs, s_nFirstFillDone, s_nFirstFillLeft, s_flMsLastFrame, s_flMsWorst, s_nUploadLastFrame, s_flCameraSky );
	if ( s_nPhaseLastFrames > 0 )
	{
		char szPhases[512] = "";
		for ( int i = 0; i < GIPHASE_COUNT; i++ )
		{
			char szOne[64];
			Q_snprintf( szOne, sizeof( szOne ), "%s%s %.2f / %.2f", i ? ", " : "", s_pszPhase[i], s_flPhaseLastSum[i] / s_nPhaseLastFrames, s_flPhaseLastMax[i] );
			Q_strncat( szPhases, szOne, sizeof( szPhases ) );
		}
		Msg( "[gi] main thread ms per frame (average / worst over %d frames): %s; %.0f jobs taken in a frame\n", s_nPhaseLastFrames, szPhases, (float)s_nLastCollected / s_nPhaseLastFrames );
	}
	Msg( "[gi] invalid since reset: %d in solid, %d closed (back faces); hits kept %d (%.1f MB); %d albedos; brush entities blocking %d (%d tracked, %d changes)\n",
		s_nInvalidSolid, s_nInvalidClosed, nHitsTotal, nHitsTotal * sizeof( GIHit_t ) / ( 1024.0f * 1024.0f ), s_nAlbedo,
		s_pBrushSnap ? s_pBrushSnap->nEnts : 0, s_BrushTracks.Count(), s_nBrushInvalidations );
	// (the ones within 512 units of the camera: what blocks the light there)
	for ( int i = 0; s_pBrushSnap && i < s_pBrushSnap->nEnts; i++ )
	{
		const GIBrushEnt_t &e = s_pBrushSnap->pEnts[i];
		const Vector &lo = e.collide.m_vecWorldMins, &hi = e.collide.m_vecWorldMaxs;
		Vector vecNearest;
		for ( int a = 0; a < 3; a++ )
			vecNearest[a] = clamp( s_vecCamera[a], lo[a], hi[a] );
		if ( vecNearest.DistTo( s_vecCamera ) > 512.0f )
			continue;
		C_BaseEntity *pEnt = e.hEnt.Get();
		Msg( "[gi]   brush %s (%s) model %d, box %.0f %.0f %.0f .. %.0f %.0f %.0f\n", pEnt ? pEnt->GetClassname() : "?",
			pEnt && pEnt->GetModel() ? modelinfo->GetModelName( pEnt->GetModel() ) : "?", e.collide.m_iModelIndex, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z );
	}

	// per height of the near grid: valid / traced, mean sky visibility and indirect light of the valid ones
	const GICascade_t &cas = s_Cascades[0];
	for ( int z = 0; z < cas.n[2]; z++ )
	{
		int nT = 0, nV = 0;
		float flSky = 0.0f, flSun = 0.0f, flSkyB = 0.0f;
		for ( int y = 0; y < cas.n[1]; y++ )
		{
			for ( int x = 0; x < cas.n[0]; x++ )
			{
				const GIProbe_t &p = cas.pProbes[ SlotIndex( cas, cas.iGridMin[0] + x, cas.iGridMin[1] + y, cas.iGridMin[2] + z ) ];
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
		Msg( "[gi]   z %6.0f: %4d / %4d valid, sky %.3f, indirect sun %.3f, bounced sky %.3f\n", ( cas.iGridMin[2] + z ) * cas.flSpacing, nV, nT,
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
		const GIProbe_t &p = cas.pProbes[ SlotIndex( cas, idx[0], idx[1], idx[2] ) ];
		const bool bMatch = p.bAssigned && p.cell[0] == idx[0] && p.cell[1] == idx[1] && p.cell[2] == idx[2];
		const float tw = ( ( c & 1 ) ? f[0] : 1.0f - f[0] ) * ( ( ( c >> 1 ) & 1 ) ? f[1] : 1.0f - f[1] ) * ( ( ( c >> 2 ) & 1 ) ? f[2] : 1.0f - f[2] );
		const bool bValid = bMatch && p.bTraced && p.flValid > 0.0f;
		Vector probePos;
		ProbePos( cas, idx, probePos );
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
		const float w = bValid ? tw * flOpen * ( flFacing * flFacing + 0.02f ) : 0.0f;
		const float vis = clamp( p.visF[0] + p.visF[1] * N.x + p.visF[2] * N.y + p.visF[3] * N.z, 0.0f, 1.0f );
		wsum += w;
		sky += w * vis;
		Msg( "[gi]  %d cell %d %d %d %s%s trace from %.0f %.0f %.0f, tw %.2f facing %.2f, free %.0f %.0f %.0f (d %.0f %.0f %.0f) open %.2f -> w %.3f, sky vis %.2f (avg %.2f, own rays %.2f), indirect sun %.3f sky %.3f, %d passes, %d hits\n",
			c, idx[0], idx[1], idx[2], bMatch ? "" : "[slot holds another cell] ", bValid ? "valid" : "INVALID",
			p.vecTracePos.x, p.vecTracePos.y, p.vecTracePos.z, tw, flFacing, flFree[0], flFree[1], flFree[2], d.x, d.y, d.z,
			flOpen, w, vis, p.visF[0], p.vis[0], p.sun[0], p.sky[0], (int)p.nPasses, p.nHits );
	}
	Msg( "[gi]  total weight %.3f, sky visibility %.3f\n", wsum, wsum > 1e-5f ? sky / wsum : -1.0f );

	// the column of probes over the point: what is above and below each
	Msg( "[gi]  eye %.1f %.1f %.1f; the column at cell %d %d:\n", MainViewOrigin().x, MainViewOrigin().y, MainViewOrigin().z, base[0], base[1] );
	for ( int z = cas.iGridMin[2]; z < cas.iGridMin[2] + cas.n[2]; z++ )
	{
		const GIProbe_t &p = cas.pProbes[ SlotIndex( cas, base[0], base[1], z ) ];
		if ( !p.bAssigned || p.cell[0] != base[0] || p.cell[1] != base[1] || p.cell[2] != z )
			continue;
		Msg( "[gi]    z %6.0f: %s, trace from z %.0f, free up %.0f down %.0f, sky %.2f, %d hits of %d rays, back %d, start solid %d\n",
			( z + GI_OFFSET ) * S, !p.bTraced ? "not traced" : ( p.flValid > 0.0f ? "valid" : "INVALID" ), p.vecTracePos.z,
			p.freeDist[4], p.freeDist[5], p.vis[0] * 2.0f, p.nHits, p.nPasses * GI_RAYS, p.nBack, p.nStartSolid );
	}
	const lightData_Global_t &l = s_LastLight;
	const float flSky = wsum > 1e-5f ? sky / wsum : 0.0f;
	const float flMin = r_deferred_gi_min.GetFloat();
	Msg( "[gi]  global light: sun %.3f %.3f %.3f (dir %.2f %.2f %.2f), sky %.3f %.3f %.3f, ground %.3f %.3f %.3f -> sky light here %.4f\n",
		l.diff.x, l.diff.y, l.diff.z, l.vecLight.x, l.vecLight.y, l.vecLight.z, l.ambh.x, l.ambh.y, l.ambh.z, l.ambl.x, l.ambl.y, l.ambl.z,
		( l.ambh.x * 0.299f + l.ambh.y * 0.587f + l.ambh.z * 0.114f ) * ( flMin + ( 1.0f - flMin ) * flSky ) );
}

CON_COMMAND( r_deferred_gi_stats, "GI probes: how many are traced / valid, what the workers have done" )
{
	DeferredGI_Stats();
}

CON_COMMAND( r_deferred_gi_reset, "GI probes: trace every probe again (applies r_deferred_gi_spacing; 1 = all at once, as on a new map)" )
{
	DeferredGI_Reset();
	// (as after a teleport, not as on a map's first frame: the work goes to the background)
	s_bFirstFill = ( args.ArgC() > 1 && atoi( args[1] ) != 0 );
	// (the workers again, with r_deferred_gi_threads as it is now)
	StopWorkers();
}

CON_COMMAND_F( r_deferred_gi_bench, "GI probes: time N rays from the camera", FCVAR_CHEAT )
{
	const int n = ( args.ArgC() > 1 ) ? clamp( atoi( args[1] ), 1, 200000 ) : 4000;
	const Vector vecOrigin = MainViewOrigin();
	GICandidates_t cand;
	FindCandidates( s_pBrushSnap, vecOrigin, GI_RAY_LENGTH, cand );
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

	bool bOn = r_deferred_gi.GetBool() && globalLight.bEnabled && g_tex_GI.IsValid() && g_tex_GIBlur.IsValid() && EnsureGIMaterials();
	for ( int c = 0; c < GI_CASCADES; c++ )
		bOn = bOn && s_Cascades[c].texTarget.IsValid() && s_Cascades[c].texDisplay[0].IsValid() && s_Cascades[c].texDisplay[1].IsValid();

	ITexture *pGI = g_tex_GI;
	ITexture *pBlur = g_tex_GIBlur;

	if ( !bOn )
	{
		ITexture *pProbes = s_Cascades[0].texDisplay[ s_Cascades[0].iDisplay ];
		ITexture *pProbesFar = s_Cascades[1].texDisplay[ s_Cascades[1].iDisplay ];
		QUEUE_FIRE( CommitGIData, data );
		QUEUE_FIRE( CommitTexture_GI, pProbes, pProbesFar, pGI, pBlur );
		return;
	}

	const double flStart = Plat_FloatTime();
	const bool bFirstFrame = s_bFirstFill;
	EnsureProbes();
	StartWorkers();
	if ( !s_Cascades[0].bGridValid )
	{
		for ( int c = 0; c < GI_CASCADES; c++ )
			s_Cascades[c].flSpacing = r_deferred_gi_spacing.GetFloat() * s_GridDesc[c].flScale;
	}

	s_vecCamera = view.origin;
	const bool bFar = r_deferred_gi_far.GetBool();
	double flMark = flStart;
	UpdateGrid( 0, view.origin );
	if ( bFar )
		UpdateGrid( 1, view.origin );
	UpdateBrushEntities( s_bFirstFill );
	PhaseTime( GIPHASE_GRID, flMark );

	const Vector vecLight = globalLight.vecLight.AsVector3D();
	s_LastLight = globalLight;
	UpdateGIWork( vecLight, globalLight.bEnabled );
	UpdateCameraSky( view.origin );
	flMark = Plat_FloatTime();

	// (writing a probe takes the main thread a microsecond: a round of gathering changes most of
	// them, 8192 a frame was a hitch of 7 ms every few seconds of walking - the display atlas eases
	// toward them anyway. A new map or a teleport writes the whole grid: faster then.)
	const int nUploadMax = r_deferred_gi_upload_max.GetInt() / ( r_deferred_gi_upload_points.GetBool() ? 1 : 4 );
	s_nUploadLastFrame = UploadProbes( s_Cascades[0], nUploadMax * ( s_Cascades[0].bInitialUpload ? 4 : 1 ) );
	if ( bFar )
		s_nUploadLastFrame += UploadProbes( s_Cascades[1], nUploadMax / 2 * ( s_Cascades[1].bInitialUpload ? 4 : 1 ) );
	PhaseTime( GIPHASE_UPLOAD, flMark );
	BlendAtlas( s_Cascades[0] );
	if ( bFar )
		BlendAtlas( s_Cascades[1] );
	PhaseTime( GIPHASE_BLEND, flMark );

	// (the main thread's part of the frame: taking in, scheduling, writing to the GPU)
	if ( !bFirstFrame )
	{
		s_flMsLastFrame = (float)( ( Plat_FloatTime() - flStart ) * 1000.0 );
		s_flMsWorst = Max( s_flMsWorst, s_flMsLastFrame );
		PhaseFrameDone();
	}

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
		data.vecDims[c].Init( bUse ? cas.n[0] : 0, bUse ? cas.n[1] : 0, bUse ? cas.n[2] : 0, cas.flSpacing );
	}
	// (xy: r_deferred_gi_debug 9 shows the distance of every pixel's position from this point)
	float flDebugPoint[2] = { 0.0f, 0.0f };
	sscanf( r_deferred_gi_debug_point.GetString(), "%f %f", &flDebugPoint[0], &flDebugPoint[1] );
	data.vecAtlas.Init( flDebugPoint[0], flDebugPoint[1], r_deferred_gi_normal_offset.GetFloat(), (float)iDebug );
	data.vecParams0.Init( r_deferred_gi_bounce.GetFloat(), r_deferred_gi_skybounce.GetFloat(), 1.0f / r_deferred_gi_fade_cells.GetFloat(), r_deferred_gi_min.GetFloat() );
	data.vecParams1.Init( r_deferred_gi_leak_tolerance.GetFloat() / 64.0f, GI_OFFSET, r_deferred_gi_sky_open.GetFloat(), r_deferred_gi_indoor_boost.GetFloat() );
	data.vecParams2.Init( s_flCameraSky, r_deferred_gi_sky_gamma.GetFloat(), r_deferred_gi_sky_desat.GetFloat(), r_deferred_gi_sky_indoor.GetFloat() );
	data.vecBlurH.Init( 1.0f / w, 0.0f, 0.04f, flUVScale );
	data.vecBlurV.Init( 0.0f, 1.0f / h, 0.04f, flUVScale );
	data.vecApply.Init( 1.0f, ( iDebug == 1 || iDebug >= 3 ) ? 1.0f : 0.0f, 0.0f, flUVScale );

	ITexture *pProbes = s_Cascades[0].texDisplay[ s_Cascades[0].iDisplay ];
	ITexture *pProbesFar = s_Cascades[1].texDisplay[ s_Cascades[1].iDisplay ];
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


#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"


// ---------------------------------------------------------------
// r_csm_quality  –  Sun shadow (cascaded shadow map) quality
//   0 = Very Low, 1 = Low, 2 = Medium, 3 = High, 4 = Very High, 5 = Ultra
//
// HL2RPM: cascades are fitted to the camera frustum ("practical split
// scheme"), so every texel of the atlas lands in front of the camera.
// Quality now controls what actually costs GPU time:
//   - how many cascades are rendered (each one is a full scene pass),
//   - the resolution of each cascade,
//   - how far sun shadows reach,
//   - how often the far cascades are re-rendered (staggered updates).
// All cascades live in one 4096x4096 atlas (2x2 tiles of up to 2048),
// so switching quality never needs a render target re-allocation.
// ---------------------------------------------------------------

struct csm_quality_preset_t
{
	int iNumCascades;
	int iResolution;
	float flShadowDistance;
	float flSplitLambda;	// 0 = uniform splits, 1 = logarithmic
	int iUpdateInterval[4];
};

// HL2RPM: a cascade fitted to the bounding sphere of its slice gets texels proportional to
// the slice's far end - about 1.3 texels per pixel there whatever the split, but much
// coarser right in front of the camera. With lambda ~0.78 the first cascade reached 420+
// units: 0.63 unit texels (8 pixels per texel on the floor at the player's feet, table legs
// cast no visible shadow), and the longer-distance presets were even coarser up close.
// Splits closer to logarithmic keep the first cascade at ~180-220 units (0.27 - 0.5 unit
// texels) and halve the texels of the middle ones; the last cascade doesn't change.
// (2026-10-02: High / Very High / Ultra closer still - 0.95 / 0.96 / 0.97: up close the
// texels showed as stairs. Ultra: the first cascade ~134 units with 0.22 unit texels, the
// second ~460 units with 0.7 - both finer than before, only 134 - 220 units got coarser.)
static const csm_quality_preset_t g_CSMPresets[] =
{
	//  cascades  res   distance  lambda   update interval per cascade (frames)
	{ 2,        1024,  3000.0f,  0.92f, { 1, 4, 8, 8 } },	// Very Low
	{ 3,        1024,  4500.0f,  0.92f, { 1, 2, 6, 8 } },	// Low
	{ 3,        1536,  6000.0f,  0.92f, { 1, 2, 4, 8 } },	// Medium
	// (every cascade re-render is a full scene pass - ~2 ms on an open map on a GTX 960M;
	// a still camera doesn't need the second one every frame: moving, turning and the
	// sun moving force an update anyway, see ShouldUpdateCascade)
	{ 4,        2048,  7000.0f,  0.95f, { 1, 2, 3, 6 } },	// High
	{ 4,        2048,  9000.0f,  0.96f, { 1, 2, 2, 4 } },	// Very High
	{ 4,        2048, 12000.0f,  0.97f, { 1, 1, 1, 2 } },	// Ultra
};

COMPILE_TIME_ASSERT( SHADOW_NUM_CASCADES >= 4 );

static cascade_t g_CascadeInfo[SHADOW_NUM_CASCADES];
static int g_iActiveCascades = 4;
static int g_iCSMQuality = -1;
static const int iNumCascades = SHADOW_NUM_CASCADES;

static void ApplyCSMQuality( int quality )
{
	quality = clamp( quality, 0, (int)ARRAYSIZE( g_CSMPresets ) - 1 );
	g_iCSMQuality = quality;
	const csm_quality_preset_t &preset = g_CSMPresets[quality];

	g_iActiveCascades = clamp( preset.iNumCascades, 1, 4 );

	const int iTileSize = CSM_COMP_RES_X / 2;

	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		cascade_t &c = g_CascadeInfo[i];
		const int iSlot = Min( i, 3 );

		c.iResolution       = Min( preset.iResolution, iTileSize );
		c.flProjectionSize  = 1024.0f;	// recomputed per frame from the view frustum
		c.flOriginOffset    = 12000.0f;
		c.flFarZ            = 26000.0f;
		c.flSlopeScaleMin   = 0.0f;		// bias is computed adaptively from the texel size
		c.flSlopeScaleMax   = 0.0f;
		c.flNormalScaleMax  = 0.0f;
		c.flUpdateDelay     = 0.0f;
		c.iUpdateInterval   = Max( 1, preset.iUpdateInterval[iSlot] );
		c.bOutputRadiosityData    = ( i < 2 );
		c.iRadiosityCascadeTarget = ( i < 2 ) ? i : 1;
		c.flSplitNear = 0.0f;
		c.flSplitFar = preset.flShadowDistance;
#if CSM_USE_COMPOSITED_TARGET
		// 2x2 tiles inside the composited atlas
		c.iViewport_x = ( iSlot % 2 ) * iTileSize;
		c.iViewport_y = ( iSlot / 2 ) * iTileSize;
#endif
	}
}

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_csm_quality( "r_csm_quality", "3", FCVAR_ARCHIVE,
	"Sun shadow quality (0=Very Low, 1=Low, 2=Medium, 3=High, 4=Very High, 5=Ultra). Controls cascade count, resolution, distance and update rate.",
	true, 0.0f, true, 5.0f, OnCSMQualityChanged );

static ConVar r_csm_distance_scale( "r_csm_distance_scale", "1.0", FCVAR_ARCHIVE,
	"Multiplier for the sun shadow distance of the current r_csm_quality", true, 0.25f, true, 4.0f );

static ConVar r_csm_split_lambda( "r_csm_split_lambda", "-1", 0,
	"Dev: cascade split distribution (0 = uniform, 1 = logarithmic), -1 = the r_csm_quality preset", true, -1.0f, true, 1.0f );

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVar *pConVar = static_cast<ConVar *>( var );
	ApplyCSMQuality( pConVar->GetInt() );
	const csm_quality_preset_t &p = g_CSMPresets[g_iCSMQuality];
	DevMsg( "r_csm_quality: %d  (%d cascades @ %d, distance %.0f)\n",
		pConVar->GetInt(), p.iNumCascades, p.iResolution, p.flShadowDistance );
}

static void EnsureCascadesInitialized()
{
	if ( g_iCSMQuality != r_csm_quality.GetInt() )
		ApplyCSMQuality( r_csm_quality.GetInt() );
}

const cascade_t &GetCascadeInfo( int index )
{
	EnsureCascadesInitialized();

	Assert( index >= 0 && index < iNumCascades );
	COMPILE_TIME_ASSERT( iNumCascades == SHADOW_NUM_CASCADES );

	return g_CascadeInfo[ index ];
}

int GetActiveCascadeCount()
{
	EnsureCascadesInitialized();
	return g_iActiveCascades;
}

float GetCascadeShadowDistance()
{
	EnsureCascadesInitialized();
	return g_CSMPresets[g_iCSMQuality].flShadowDistance * r_csm_distance_scale.GetFloat();
}

void UpdateCascadeSplits( float flZNear, float flFov, float flAspect )
{
	EnsureCascadesInitialized();

	const csm_quality_preset_t &preset = g_CSMPresets[g_iCSMQuality];
	const int iCount = g_iActiveCascades;
	const float flNear = Max( flZNear, 4.0f );
	const float flFar = Max( GetCascadeShadowDistance(), flNear + 64.0f );
	const float flLambda = ( r_csm_split_lambda.GetFloat() >= 0.0f ) ? r_csm_split_lambda.GetFloat() : preset.flSplitLambda;

	float flSplits[5];
	flSplits[0] = flNear;
	for ( int i = 1; i < iCount; i++ )
	{
		const float f = i / (float)iCount;
		const float flLog = flNear * powf( flFar / flNear, f );
		const float flUniform = flNear + ( flFar - flNear ) * f;
		flSplits[i] = Lerp( flLambda, flUniform, flLog );
	}
	flSplits[iCount] = flFar;

	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		cascade_t &c = g_CascadeInfo[i];
		if ( i < iCount )
		{
			c.flSplitNear = ( i == 0 ) ? 0.0f : flSplits[i];
			c.flSplitFar = flSplits[i + 1];
		}
		else
		{
			c.flSplitNear = c.flSplitFar = flFar;
		}
	}
}

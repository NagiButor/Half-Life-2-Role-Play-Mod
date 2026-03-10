
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"


// ---------------------------------------------------------------
// r_csm_quality  –  Sun shadow quality preset
//   0 = Very Low, 1 = Low, 2 = Medium, 3 = High, 4 = Very High, 5 = Ultra
//
// 8 cascades @ 2048 per-cascade, composited into 8192×4096 (4×2).
// Texels-per-unit for Ultra cascade-0: 2048/64 = 32.0
//
// Projection sizes follow a ≈2.4× geometric progression so that
// each cascade smoothly takes over from the previous one without
// visible pop.  The last cascade reaches ±12 288 world-units.
// ---------------------------------------------------------------

struct csm_quality_preset_t
{
	float flProjectionSize[SHADOW_NUM_CASCADES];
	float flSlopeScaleMin[SHADOW_NUM_CASCADES];
	float flSlopeScaleMax[SHADOW_NUM_CASCADES];
	float flNormalScaleMax[SHADOW_NUM_CASCADES];
};

static const csm_quality_preset_t g_CSMPresets[] = {
	//                               c0        c1        c2        c3        c4        c5        c6        c7
	// Very Low (0)
	{ { 1024.0f, 2560.0f, 4096.0f, 6144.0f, 10240.0f, 16384.0f, 20480.0f, 24576.0f },
	  { 1.55f, 1.55f, 1.55f, 1.75f, 1.75f, 1.75f, 1.75f, 1.75f },
	  { 5.80f, 5.80f, 5.80f, 6.60f, 6.60f, 6.60f, 6.60f, 6.60f },
	  { 6.00f, 6.00f, 6.00f, 8.00f, 8.00f, 8.00f, 8.00f, 8.00f } },
	// Low (1)
	{ {  512.0f, 1280.0f, 2560.0f, 5120.0f, 8192.0f, 14336.0f, 20480.0f, 24576.0f },
	  { 0.80f, 0.80f, 0.80f, 0.90f, 0.90f, 0.90f, 0.90f, 0.90f },
	  { 3.00f, 3.00f, 3.00f, 3.40f, 3.40f, 3.40f, 3.40f, 3.40f },
	  { 3.00f, 3.00f, 3.00f, 4.00f, 4.00f, 4.00f, 4.00f, 4.00f } },
	// Medium (2)
	{ {  256.0f,  640.0f, 1536.0f, 3072.0f, 6144.0f, 10240.0f, 18432.0f, 24576.0f },
	  { 0.42f, 0.42f, 0.42f, 0.48f, 0.48f, 0.48f, 0.48f, 0.48f },
	  { 1.55f, 1.55f, 1.55f, 1.80f, 1.80f, 1.80f, 1.80f, 1.80f },
	  { 1.50f, 1.50f, 1.50f, 2.00f, 2.00f, 2.00f, 2.00f, 2.00f } },
	// High (3)
	{ {  128.0f,  320.0f,  768.0f, 1792.0f, 4096.0f, 8192.0f, 16384.0f, 24576.0f },
	  { 0.22f, 0.22f, 0.22f, 0.26f, 0.26f, 0.26f, 0.26f, 0.26f },
	  { 0.80f, 0.80f, 0.80f, 0.95f, 0.95f, 0.95f, 0.95f, 0.95f },
	  { 0.75f, 0.75f, 0.75f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f } },
	// Very High (4)
	{ {   96.0f,  224.0f,  560.0f, 1408.0f, 3328.0f, 7168.0f, 15360.0f, 24576.0f },
	  { 0.17f, 0.17f, 0.17f, 0.20f, 0.20f, 0.20f, 0.20f, 0.20f },
	  { 0.62f, 0.62f, 0.62f, 0.74f, 0.74f, 0.74f, 0.74f, 0.74f },
	  { 0.56f, 0.56f, 0.56f, 0.74f, 0.74f, 0.74f, 0.74f, 0.74f } },
	// Ultra (5) – tightest near cascade, sharpest shadows
	{ {   64.0f,  160.0f,  400.0f, 1024.0f, 2560.0f, 6144.0f, 14336.0f, 24576.0f },
	  { 0.12f, 0.12f, 0.12f, 0.14f, 0.14f, 0.14f, 0.14f, 0.14f },
	  { 0.46f, 0.46f, 0.46f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f },
	  { 0.38f, 0.38f, 0.38f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f } },
};

static cascade_t g_CascadeInfo[SHADOW_NUM_CASCADES];
static const int iNumCascades = SHADOW_NUM_CASCADES;

static void ApplyCSMQuality( int quality )
{
	quality = clamp( quality, 0, ARRAYSIZE( g_CSMPresets ) - 1 );
	const csm_quality_preset_t &preset = g_CSMPresets[quality];

	// Per-cascade far-Z / origin-offset / update-delay tables
	static const float s_flFarZ[]         = { 12000, 12000, 15000, 15000, 25000, 25000, 40000, 40000 };
	static const float s_flOriginOffset[] = { 10000, 10000, 10000, 10000, 15000, 15000, 20000, 20000 };
	static const float s_flUpdateDelay[]  = { 0.0f,  0.05f, 0.10f, 0.10f, 0.15f, 0.20f, 0.30f, 0.35f };

	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		g_CascadeInfo[i].iResolution       = 2048;
		g_CascadeInfo[i].flProjectionSize  = preset.flProjectionSize[i];
		g_CascadeInfo[i].flOriginOffset    = s_flOriginOffset[i];
		g_CascadeInfo[i].flFarZ            = s_flFarZ[i];
		g_CascadeInfo[i].flSlopeScaleMin   = preset.flSlopeScaleMin[i];
		g_CascadeInfo[i].flSlopeScaleMax   = preset.flSlopeScaleMax[i];
		g_CascadeInfo[i].flNormalScaleMax  = preset.flNormalScaleMax[i];
		g_CascadeInfo[i].flUpdateDelay     = s_flUpdateDelay[i];
		g_CascadeInfo[i].bOutputRadiosityData    = ( i < 2 );
		g_CascadeInfo[i].iRadiosityCascadeTarget = ( i < 2 ) ? i : 1;
#if CSM_USE_COMPOSITED_TARGET
		// 4-column × 2-row layout inside the 8192×4096 composited atlas
		g_CascadeInfo[i].iViewport_x = ( i % 4 ) * g_CascadeInfo[i].iResolution;
		g_CascadeInfo[i].iViewport_y = ( i / 4 ) * g_CascadeInfo[i].iResolution;
#endif
	}
}

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_csm_quality( "r_csm_quality", "5", FCVAR_ARCHIVE,
	"Sun shadow quality preset (0=Very Low, 1=Low, 2=Medium, 3=High, 4=Very High, 5=Ultra)",
	true, 0.0f, true, 5.0f, OnCSMQualityChanged );

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVar *pConVar = static_cast<ConVar *>( var );
	ApplyCSMQuality( pConVar->GetInt() );
	DevMsg( "r_csm_quality: %d  (c0=%.0f c1=%.0f c2=%.0f c3=%.0f c4=%.0f c5=%.0f c6=%.0f c7=%.0f)\n",
		pConVar->GetInt(),
		g_CascadeInfo[0].flProjectionSize,
		g_CascadeInfo[1].flProjectionSize,
		g_CascadeInfo[2].flProjectionSize,
		g_CascadeInfo[3].flProjectionSize,
		g_CascadeInfo[4].flProjectionSize,
		g_CascadeInfo[5].flProjectionSize,
		g_CascadeInfo[6].flProjectionSize,
		g_CascadeInfo[7].flProjectionSize );
}

static bool g_bCascadeInitialized = false;

const cascade_t &GetCascadeInfo( int index )
{
	if ( !g_bCascadeInitialized )
	{
		ApplyCSMQuality( r_csm_quality.GetInt() );
		g_bCascadeInitialized = true;
	}

	Assert( index >= 0 && index < iNumCascades );
	COMPILE_TIME_ASSERT( iNumCascades == SHADOW_NUM_CASCADES );

	return g_CascadeInfo[ index ];
}

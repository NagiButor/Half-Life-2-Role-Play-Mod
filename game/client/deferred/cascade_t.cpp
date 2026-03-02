
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"


// ---------------------------------------------------------------
// r_csm_quality  –  Sun shadow quality preset
//   0 = Very Low, 1 = Low, 2 = Medium, 3 = High, 4 = Ultra
//
// Texels-per-unit at 4096 resolution:
//   Ultra   : cascade0 = 16.0   cascade1 = 4.0
//   High    : cascade0 =  8.0   cascade1 = 2.0
//   Medium  : cascade0 =  4.0   cascade1 = 1.33
//   Low     : cascade0 =  2.67  cascade1 = 1.0
//   VeryLow : cascade0 =  2.0   cascade1 = 0.67
// ---------------------------------------------------------------

struct csm_quality_preset_t
{
	float flProjectionSize[SHADOW_NUM_CASCADES];
	float flSlopeScaleMin[SHADOW_NUM_CASCADES];
	float flSlopeScaleMax[SHADOW_NUM_CASCADES];
	float flNormalScaleMax[SHADOW_NUM_CASCADES];
};

static const csm_quality_preset_t g_CSMPresets[] = {
	//                   ortho              slopeMin            slopeMax            normalMax
	// Very Low (0) – widest projection, lowest quality
	{ { 2048.0f, 3072.0f, 4608.0f, 6144.0f }, { 1.60f, 2.57f, 3.53f, 4.50f }, { 6.00f, 7.00f, 8.00f, 9.00f }, { 8.00f, 11.33f, 14.67f, 18.00f } },
	// Low (1)
	{ { 1024.0f, 1792.0f, 3072.0f, 5120.0f }, { 0.80f, 1.78f, 2.77f, 3.75f }, { 3.00f, 4.50f, 6.00f, 7.50f }, { 4.00f, 7.67f, 11.33f, 15.00f } },
	// Medium (2)
	{ {  512.0f, 1024.0f, 2048.0f, 4096.0f }, { 0.40f, 1.27f, 2.13f, 3.00f }, { 1.50f, 3.00f, 4.50f, 6.00f }, { 2.00f, 5.33f, 8.67f, 12.00f } },
	// High (3)
	{ {  256.0f,  640.0f, 1600.0f, 4096.0f }, { 0.20f, 1.13f, 2.07f, 3.00f }, { 0.75f, 2.50f, 4.25f, 6.00f }, { 1.00f, 4.67f, 8.33f, 12.00f } },
	// Ultra (4) – tightest near cascade, sharpest shadows
	{ {  128.0f,  384.0f, 1152.0f, 4096.0f }, { 0.10f, 1.07f, 2.03f, 3.00f }, { 0.38f, 2.25f, 4.13f, 6.00f }, { 0.50f, 4.33f, 8.17f, 12.00f } },
};

static cascade_t g_CascadeInfo[SHADOW_NUM_CASCADES];
static const int iNumCascades = SHADOW_NUM_CASCADES;

static void ApplyCSMQuality( int quality )
{
	quality = clamp( quality, 0, ARRAYSIZE( g_CSMPresets ) - 1 );
	const csm_quality_preset_t &preset = g_CSMPresets[quality];

	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		g_CascadeInfo[i].iResolution       = 4096;
		g_CascadeInfo[i].flProjectionSize  = preset.flProjectionSize[i];
		g_CascadeInfo[i].flOriginOffset    = 10000.0f;
		g_CascadeInfo[i].flFarZ            = ( i == 0 ) ? 12000.0f : 15000.0f;
		g_CascadeInfo[i].flSlopeScaleMin   = preset.flSlopeScaleMin[i];
		g_CascadeInfo[i].flSlopeScaleMax   = preset.flSlopeScaleMax[i];
		g_CascadeInfo[i].flNormalScaleMax  = preset.flNormalScaleMax[i];
		g_CascadeInfo[i].flUpdateDelay     = ( i == 0 ) ? 0.0f : ( ( i == 1 ) ? 0.10f : 0.25f );
		g_CascadeInfo[i].bOutputRadiosityData    = ( i < 2 );
		g_CascadeInfo[i].iRadiosityCascadeTarget = ( i < 2 ) ? i : 1;
#if CSM_USE_COMPOSITED_TARGET
		g_CascadeInfo[i].iViewport_x = ( i & 1 ) * g_CascadeInfo[i].iResolution;
		g_CascadeInfo[i].iViewport_y = ( i >> 1 ) * g_CascadeInfo[i].iResolution;
#endif
	}
}

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_csm_quality( "r_csm_quality", "4", FCVAR_ARCHIVE,
	"Sun shadow quality preset (0=Very Low, 1=Low, 2=Medium, 3=High, 4=Ultra)",
	true, 0.0f, true, 4.0f, OnCSMQualityChanged );

static void OnCSMQualityChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVar *pConVar = static_cast<ConVar *>( var );
	ApplyCSMQuality( pConVar->GetInt() );
	DevMsg( "r_csm_quality: %d  (cascade0 ortho=%.0f, cascade1 ortho=%.0f, cascade2 ortho=%.0f, cascade3 ortho=%.0f)\n",
		pConVar->GetInt(),
		g_CascadeInfo[0].flProjectionSize,
		g_CascadeInfo[1].flProjectionSize,
		g_CascadeInfo[2].flProjectionSize,
		g_CascadeInfo[3].flProjectionSize );
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

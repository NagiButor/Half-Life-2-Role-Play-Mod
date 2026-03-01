
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
	float flProjectionSize[2];
	float flSlopeScaleMin[2];
	float flSlopeScaleMax[2];
	float flNormalScaleMax[2];
};

static const csm_quality_preset_t g_CSMPresets[] = {
	//                   ortho              slopeMin            slopeMax            normalMax
	// Very Low (0) – widest projection, lowest quality
	{ { 2048.0f, 6144.0f }, { 1.60f, 4.50f }, { 6.00f, 9.00f }, { 8.00f, 18.00f } },
	// Low (1)
	{ { 1024.0f, 5120.0f }, { 0.80f, 3.75f }, { 3.00f, 7.50f }, { 4.00f, 15.00f } },
	// Medium (2)
	{ {  512.0f, 4096.0f }, { 0.40f, 3.00f }, { 1.50f, 6.00f }, { 2.00f, 12.00f } },
	// High (3)
	{ {  256.0f, 4096.0f }, { 0.20f, 3.00f }, { 0.75f, 6.00f }, { 1.00f, 12.00f } },
	// Ultra (4) – tightest near cascade, sharpest shadows
	{ {  128.0f, 4096.0f }, { 0.10f, 3.00f }, { 0.38f, 6.00f }, { 0.50f, 12.00f } },
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
		g_CascadeInfo[i].flUpdateDelay     = ( i == 0 ) ? 0.0f : 0.25f;
		g_CascadeInfo[i].bOutputRadiosityData    = true;
		g_CascadeInfo[i].iRadiosityCascadeTarget = i;
#if CSM_USE_COMPOSITED_TARGET
		g_CascadeInfo[i].iViewport_x = i * 4096;
		g_CascadeInfo[i].iViewport_y = 0;
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
	DevMsg( "r_csm_quality: %d  (cascade0 ortho=%.0f, cascade1 ortho=%.0f)\n",
		pConVar->GetInt(),
		g_CascadeInfo[0].flProjectionSize,
		g_CascadeInfo[1].flProjectionSize );
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

#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "tier0/memdbgon.h"

static void OnShadowRTResolutionChanged( IConVar *var, const char *pOldValue, float flOldValue );

ConVar r_deferred_rt_shadowspot_res( "r_deferred_rt_shadowspot_res", "2048", 0, "", OnShadowRTResolutionChanged );
#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
ConVar r_deferred_rt_shadowspot_lod1_res( "r_deferred_rt_shadowspot_lod1_res", "1024", 0, "", OnShadowRTResolutionChanged );
ConVar r_deferred_rt_shadowspot_lod2_res( "r_deferred_rt_shadowspot_lod2_res", "512", 0, "", OnShadowRTResolutionChanged );
#endif
ConVar r_deferred_rt_shadowpoint_res( "r_deferred_rt_shadowpoint_res", "2048", 0, "", OnShadowRTResolutionChanged );
#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
ConVar r_deferred_rt_shadowpoint_lod1_res( "r_deferred_rt_shadowpoint_lod1_res", "1024", 0, "", OnShadowRTResolutionChanged );
ConVar r_deferred_rt_shadowpoint_lod2_res( "r_deferred_rt_shadowpoint_lod2_res", "512", 0, "", OnShadowRTResolutionChanged );
#endif

struct pointspot_shadow_quality_preset_t
{
	int iMainRes;
	int iLod1Res;
	int iLod2Res;
};

static const pointspot_shadow_quality_preset_t g_PointSpotShadowPresets[] =
{
	{  128,   64,   32 },
	{  256,  128,   64 },
	{  512,  256,  128 },
	{ 1024,  512,  256 },
	{ 1536,  768,  384 },
	{ 2048, 1024,  512 },
};

static void ApplyPointSpotShadowQualityPreset( int quality )
{
	quality = clamp( quality, 0, ARRAYSIZE( g_PointSpotShadowPresets ) - 1 );
	const pointspot_shadow_quality_preset_t &preset = g_PointSpotShadowPresets[quality];

	r_deferred_rt_shadowspot_res.SetValue( preset.iMainRes );
	r_deferred_rt_shadowpoint_res.SetValue( preset.iMainRes );

#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
	r_deferred_rt_shadowspot_lod1_res.SetValue( preset.iLod1Res );
	r_deferred_rt_shadowspot_lod2_res.SetValue( preset.iLod2Res );
	r_deferred_rt_shadowpoint_lod1_res.SetValue( preset.iLod1Res );
	r_deferred_rt_shadowpoint_lod2_res.SetValue( preset.iLod2Res );
#endif
}

static void OnPointSpotShadowQualityChanged( IConVar *var, const char *pOldValue, float flOldValue );
ConVar r_deferred_shadow_quality_pointspot( "r_deferred_shadow_quality_pointspot", "5", FCVAR_ARCHIVE,
	"Point/Spot shadow quality preset (0=Very Low, 1=Low, 2=Medium, 3=High, 4=Very High, 5=Ultra)",
	true, 0.0f, true, 5.0f, OnPointSpotShadowQualityChanged );

ConVar r_deferred_shadowpoint_legacy( "r_deferred_shadowpoint_legacy", "0", FCVAR_ARCHIVE,
	"A/B test switch for point shadows (0=Cube atlas, 1=Dual paraboloid)" );

static void OnShadowRTResolutionChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	static float s_flLastRestartWarningTime = -1000.0f;
	const int oldValue = (int)flOldValue;
	const int newValue = static_cast<ConVar *>( var )->GetInt();

	if ( oldValue == newValue )
		return;

	RequestDeferredRTRefresh();

	if ( engine->IsInGame() && gpGlobals && gpGlobals->curtime - s_flLastRestartWarningTime > 1.0f )
	{
		Warning( "Shadow quality changes will fully apply after restarting the game.\n" );
		s_flLastRestartWarningTime = gpGlobals->curtime;
	}
}

static void OnPointSpotShadowQualityChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVar *pConVar = static_cast<ConVar *>( var );
	const int oldQuality = clamp( (int)flOldValue, 0, ARRAYSIZE( g_PointSpotShadowPresets ) - 1 );
	const int newQuality = clamp( pConVar->GetInt(), 0, ARRAYSIZE( g_PointSpotShadowPresets ) - 1 );

	if ( oldQuality == newQuality )
		return;

	ApplyPointSpotShadowQualityPreset( newQuality );
	RequestDeferredRTRefresh();
}

static bool g_bPointSpotQualityInitialized = false;
void EnsurePointSpotShadowQualityInitialized()
{
	if ( g_bPointSpotQualityInitialized )
		return;

	ApplyPointSpotShadowQualityPreset( r_deferred_shadow_quality_pointspot.GetInt() );
	g_bPointSpotQualityInitialized = true;
}

ConVar r_deferred_light_stats( "r_deferred_light_stats", "0", 0, "Shows stats panel for rendered lights, shadows, etc." );

ConVar r_deferred_light_global_override( "r_deferred_light_global_override", "0", 0, "Overrides light_deferred_global (or converted light_environment)" );
ConVar r_deferred_light_global_override_shadow( "r_deferred_light_global_override_shadow", "1", 0, "Render shadows from global light" );
ConVar r_deferred_light_global_override_diffuse( "r_deferred_light_global_override_diffuse", "1 1 1" );
ConVar r_deferred_light_global_override_ambient_high( "r_deferred_light_global_override_ambient_high", "0 0.04 0.08" );
ConVar r_deferred_light_global_override_ambient_low( "r_deferred_light_global_override_ambient_low", "0 0.07 0.09" );

ConVar r_deferred_radiosity( "r_deferred_radiosity", "0", 0, "Enable radiosity for diffused lighting simulation - buggy" );
ConVar r_deferred_radiosity_propagate_count( "r_deferred_radiosity_propagate_count", "1" ); // 1
ConVar r_deferred_radiosity_propagate_far_count( "r_deferred_radiosity_propagate_far_count", "0" );
ConVar r_deferred_radiosity_blur_count( "r_deferred_radiosity_blur_count", "4" ); // 2
ConVar r_deferred_radiosity_blur_far_count( "r_deferred_radiosity_blur_far_count", "2" ); // 1
ConVar r_deferred_radiosity_nodes( "r_deferred_radiosity_nodes", "0", 0, "Shows radiosity nodes in the world for debugging" );

void OnCookieTableChanged( void *object, INetworkStringTable *stringTable, int stringNumber, const char *newString, void const *newData )
{
	if ( !newString || Q_strlen( newString ) < 1 )
		return;

	GetLightingManager()->OnCookieStringReceived( newString, stringNumber );
}

void CalcBoundaries( Vector *list, const int &num, Vector &min, Vector &max )
{
	Assert( num > 0 );

#if DEFCFG_USE_SSE && 0
	fltx4 vTestPoint = _mm_set_ps( list[0].x, list[0].y, list[0].z, 0 );
	fltx4 vMin = vTestPoint;
	fltx4 vMax = vTestPoint;

	for( int i = 1; i < num; i++ )
	{
		vTestPoint = _mm_set_ps( list[i].x, list[i].y, list[i].z, 0 );
		vMin = _mm_min_ps( vMin, vTestPoint );
		vMax = _mm_max_ps( vMax, vTestPoint );
	}

	min = Vector( SubFloat( vMin, 0 ), SubFloat( vMin, 1 ), SubFloat( vMin, 2 ) );
	max = Vector( SubFloat( vMax, 0 ), SubFloat( vMax, 1 ), SubFloat( vMax, 2 ) );
#else
	min = *list;
	max = *list;

	for ( int i = 1; i < num; i++ )
	{
		for ( int x = 0; x < 3; x++ )
		{
			min[ x ] = Min( min[ x ], list[ i ][ x ] );
			max[ x ] = Max( max[ x ], list[ i ][ x ] );
		}
	}
#endif
}

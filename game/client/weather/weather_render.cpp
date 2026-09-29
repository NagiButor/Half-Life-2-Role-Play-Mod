//========= HL2RPM ============================================================//
//
// Purpose: Weather rendering passes (see weather_render.h).
//
//=============================================================================//

#include "cbase.h"
#include "weather/weather_render.h"
#include "weather/c_weather_system.h"
#include "deferred/deferred_shared_common.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imesh.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"

#include "tier0/memdbgon.h"

// ---------------------------------------------------------------------------
// Quality settings (driven by the graphics options page)
// ---------------------------------------------------------------------------
static ConVar r_weather_clouds( "r_weather_clouds", "2", FCVAR_ARCHIVE,
	"Volumetric clouds: 0 = off, 1 = low (1/4 res), 2 = medium (1/2 res), 3 = high (1/2 res, more steps)", true, 0, true, 3 );
static ConVar r_weather_sunshafts( "r_weather_sunshafts", "2", FCVAR_ARCHIVE,
	"Volumetric sun light shafts: 0 = off, 1 = low, 2 = medium, 3 = high", true, 0, true, 3 );
static ConVar r_weather_rain( "r_weather_rain", "1", FCVAR_ARCHIVE, "Draw rain" );
static ConVar r_weather_rain_density( "r_weather_rain_density", "1.0", FCVAR_ARCHIVE, "Rain drop count multiplier", true, 0.1f, true, 2.0f );
static ConVar r_weather_wetness( "r_weather_wetness", "1", FCVAR_ARCHIVE, "Wet surfaces and puddles after rain" );
static ConVar r_weather_fog( "r_weather_fog", "1", FCVAR_ARCHIVE, "Weather height fog" );
static ConVar r_weather_cloud_temporal( "r_weather_cloud_temporal", "1", FCVAR_ARCHIVE, "Temporal accumulation for clouds (less noise)" );
static ConVar r_weather_cloud_brightness( "r_weather_cloud_brightness", "1.0", FCVAR_ARCHIVE, "Cloud lighting multiplier" );
static ConVar r_weather_rainmap_size( "r_weather_rainmap_size", "3072", 0, "World size (units) covered by the rain occlusion map" );

int WeatherRender_GetCloudQuality()
{
	return r_weather_clouds.GetInt();
}

int WeatherRender_GetSunShaftQuality()
{
	return r_weather_sunshafts.GetInt();
}

bool WeatherRender_WantsRain()
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	return r_weather_rain.GetBool() && pSys->IsActive() && pSys->GetParams().flRain > 0.005f;
}

bool WeatherRender_WantsRainMap()
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	if ( !pSys->IsActive() )
		return false;
	return WeatherRender_WantsRain() || ( r_weather_wetness.GetBool() && pSys->GetWetness() > 0.005f );
}

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------
#define RAINMAP_RES 1024

static CTextureReference g_tex_Clouds[2];
static CTextureReference g_tex_RainDepth;
static CTextureReference g_tex_RainDummy;

void InitWeatherRTs()
{
	const unsigned int cloudFlags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	for ( int i = 0; i < 2; i++ )
	{
		g_tex_Clouds[i].Init( materials->CreateNamedRenderTargetTextureEx2(
			VarArgs( "_rt_hl2rpm_clouds%d", i ),
			128, 128,
			RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
			IMAGE_FORMAT_RGBA16161616F,
			MATERIAL_RT_DEPTH_NONE,
			cloudFlags, 0 ) );
	}

	g_tex_RainDepth.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_rainmap",
		RAINMAP_RES, RAINMAP_RES,
		RT_SIZE_NO_CHANGE,
		GetDeferredManager()->GetShadowDepthFormat(),
		MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET, 0 ) );

	g_tex_RainDummy.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_rainmap_dummy",
		RAINMAP_RES, RAINMAP_RES,
		RT_SIZE_NO_CHANGE,
		materials->GetNullTextureFormat(),
		MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
}

ITexture *GetWeatherRT_Clouds( int index )
{
	return g_tex_Clouds[ index & 1 ];
}

ITexture *GetWeatherRT_RainMapDepth()
{
	return g_tex_RainDepth;
}

ITexture *GetWeatherRT_RainMapDummy()
{
	return g_tex_RainDummy;
}

int GetWeatherRainMapResolution()
{
	return RAINMAP_RES;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------
static IMaterial *g_pMatClouds = NULL;
static IMaterial *g_pMatPost = NULL;
static IMaterial *g_pMatRain = NULL;
static IMaterial *g_pMatSunShafts = NULL;

static IMaterial *CreateWeatherMaterial( const char *pszName, const char *pszShader )
{
	KeyValues *pKV = new KeyValues( pszShader );
	IMaterial *pMat = materials->CreateMaterial( pszName, pKV );
	if ( pMat )
	{
		pMat->IncrementReferenceCount();
		pMat->Refresh();
	}
	return pMat;
}

static void EnsureWeatherMaterials()
{
	if ( g_pMatClouds )
		return;

	g_pMatClouds = CreateWeatherMaterial( "__hl2rpm_weather_clouds", "WEATHER_CLOUDS" );
	g_pMatPost = CreateWeatherMaterial( "__hl2rpm_weather_post", "WEATHER_POST" );
	g_pMatRain = CreateWeatherMaterial( "__hl2rpm_weather_rain", "WEATHER_RAIN" );
	g_pMatSunShafts = CreateWeatherMaterial( "__hl2rpm_volume_sun", "VOLUME_SUN" );
}

// ---------------------------------------------------------------------------
// Frame state
// ---------------------------------------------------------------------------
static int g_iCloudCurrent = 0;
static bool g_bCloudHistoryValid = false;
static int g_iLastCloudDivisor = 0;
static int g_iLastCloudFrame = -100;
static VMatrix g_matPrevViewProj;
static bool g_bHavePrevViewProj = false;
static int g_iCloudDivisor = 2;

static VMatrix g_matRainMap;
static Vector g_vecRainMapCenter( 0, 0, 0 );
static float g_flRainMapSize = 3072.0f;
static float g_flRainMapZFar = 16384.0f;
static float g_flRainMapTime = -100.0f;
static bool g_bRainMapValid = false;

void WeatherRender_LevelInit()
{
	g_bCloudHistoryValid = false;
	g_bHavePrevViewProj = false;
	g_bRainMapValid = false;
	g_flRainMapTime = -100.0f;
}

static inline float Luminance( const Vector &v )
{
	return v.x * 0.2126f + v.y * 0.7152f + v.z * 0.0722f;
}

float WeatherRender_GetSkyLightIlluminance()
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	CDeferredLightGlobal *pGlobal = GetGlobalLight();
	const float flBaseLum = pGlobal ? Max( Luminance( pGlobal->GetColor_Diffuse() ), 0.001f ) : 1.0f;
	if ( pSys->GetNightFactor() < 0.5f )
		return flBaseLum;
	return flBaseLum * pSys->GetMoonLightScale();
}

// Rotation-only view projection: clouds are "at infinity", so only the camera
// orientation matters when reprojecting last frame's clouds.
static void BuildRotationViewProj( const CViewSetup &view, VMatrix &out )
{
	VMatrix matView, matPerspective;
	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, view.angles );
	MatrixSourceToDeviceSpace( matView );
	matView = matView.Transpose3x3();
	matView.SetTranslation( vec3_origin );
	MatrixBuildPerspectiveX( matPerspective, view.fov, view.m_flAspectRatio, 1.0f, 100000.0f );
	MatrixMultiply( matPerspective, matView, out );
}

void WeatherRender_CommitFrame( const CViewSetup &view, const lightData_Global_t &light )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	weatherData_t data;

	if ( !pSys->IsActive() )
	{
		data.bEnabled = false;
		QUEUE_FIRE( CommitWeatherData, data );
		return;
	}

	EnsureWeatherMaterials();

	const WeatherParams_t &p = pSys->GetParams();
	pSys->FillRenderData( data, view );

	// --- light the clouds with the direct light *above* them (not dimmed by the weather).
	// By day that is the map's sunlight colored by the atmosphere at cloud height: the
	// clouds glow orange at sunset while the ground is already in the shade.
	CDeferredLightGlobal *pGlobal = GetGlobalLight();
	const Vector vecBaseDiff = pGlobal ? pGlobal->GetColor_Diffuse() : Vector( 1, 1, 1 );
	const bool bSunLight = pSys->GetNightFactor() < 0.5f;	// else the global light is the moon
	Vector vecDirect;
	if ( bSunLight )
	{
		// the upper part of the clouds catches the last light after sunset
		const float flCloudAlt = p.flCloudBase + p.flCloudThickness * 0.75f;
		const float flSunAlt = pSys->GetSunAltitude();
		// the clouds see the sun a bit longer than the ground does (horizon dip)
		const float flDip = RAD2DEG( sqrtf( 2.0f * Max( flCloudAlt, 0.0f ) / 6371000.0f ) );
		const float flVis = clamp( ( flSunAlt + flDip + 1.0f ) / 1.4f, 0.0f, 1.0f );
		Vector col = Weather_SunlightColor( flSunAlt, flCloudAlt, p.flHaze );
		const float flColLum = Luminance( col );
		if ( flColLum > 1e-4f )
			col *= powf( flColLum, 0.6f ) / flColLum;
		vecDirect.Init( vecBaseDiff.x * col.x, vecBaseDiff.y * col.y, vecBaseDiff.z * col.z );
		vecDirect *= flVis * flVis * ( 3.0f - 2.0f * flVis );
	}
	else
	{
		vecDirect = light.diff.AsVector3D() / Max( p.flSunIntensity, 0.05f );
	}
	const float flDirectLum = Luminance( vecDirect );
	if ( flDirectLum > 6.0f )
		vecDirect *= 6.0f / flDirectLum;
	data.vecSunColor.Init( vecDirect.x, vecDirect.y, vecDirect.z, pSys->GetNightFactor() );

	// --- the sky LUT is built for the global light: the untinted sun (the atmosphere colors
	// it and casts the earth's shadow at dusk) or, once it has taken over, the moon
	const float flSkyLight = WeatherRender_GetSkyLightIlluminance();
	data.vecSkyLight.Init( flSkyLight, flSkyLight, flSkyLight, 0.0f );

	// --- sky light, used for cloud ambient, fog and wet reflections
	Vector vecAmbHigh = light.ambh.AsVector3D();
	Vector vecAmbLow = light.ambl.AsVector3D();
	Vector vecZenith = vecAmbHigh * p.flSkyBrightness;
	Vector vecHorizon = ( vecAmbHigh * 0.75f + vecAmbLow * 0.25f ) * ( 1.15f * p.flSkyBrightness ) + light.diff.AsVector3D() * 0.04f;
	data.vecSkyZenith.Init( vecZenith.x, vecZenith.y, vecZenith.z, 0.0f );
	data.vecSkyHorizon.Init( vecHorizon.x, vecHorizon.y, vecHorizon.z, 0.0f );

	static ConVarRef cl_weather_debug( "cl_weather_debug" );
	if ( cl_weather_debug.IsValid() && cl_weather_debug.GetBool() )
	{
		static float s_flNextPrint = 0.0f;
		if ( gpGlobals->realtime > s_flNextPrint )
		{
			s_flNextPrint = gpGlobals->realtime + 1.0f;
			Msg( "[weather light] sun alt %.2f night %.2f | cloud sun %.3f %.3f %.3f | zenith %.4f %.4f %.4f | horizon %.4f %.4f %.4f | light dir %.2f %.2f %.2f diff %.3f %.3f %.3f\n",
				pSys->GetSunAltitude(), pSys->GetNightFactor(), vecDirect.x, vecDirect.y, vecDirect.z,
				vecZenith.x, vecZenith.y, vecZenith.z, vecHorizon.x, vecHorizon.y, vecHorizon.z,
				light.vecLight.x, light.vecLight.y, light.vecLight.z, light.diff.x, light.diff.y, light.diff.z );
		}
	}

	// fog takes the horizon color, greyed out under overcast
	const float flHorizonLum = Luminance( vecHorizon );
	const Vector vecGrey = Vector( 0.93f, 0.96f, 1.0f ) * flHorizonLum;
	Vector vecFog = Lerp( Clamp( p.flFogSkyColor, 0.0f, 1.0f ), vecGrey, vecHorizon );
	data.vecFogColor.Init( vecFog.x, vecFog.y, vecFog.z, 0.985f );

	if ( !r_weather_fog.GetBool() )
		data.vecFogParams.x = 0.0f;
	if ( !r_weather_wetness.GetBool() )
	{
		data.vecRainParams.y = 0.0f;
		data.vecRainParams.z = 0.0f;
	}

	// --- clouds: quality / resolution / temporal
	const int iQuality = r_weather_clouds.GetInt();
	g_iCloudDivisor = ( iQuality >= 2 ) ? 2 : 4;
	const bool bMainSized = true;
	const bool bConsecutive = ( gpGlobals->framecount - g_iLastCloudFrame ) <= 2;
	const bool bHistory = r_weather_cloud_temporal.GetBool() && g_bCloudHistoryValid && bConsecutive
		&& g_iLastCloudDivisor == g_iCloudDivisor && bMainSized;

	static const float s_flSteps[4] = { 0.0f, 28.0f, 36.0f, 52.0f };
	data.vecCloudParams2.Init( r_weather_cloud_brightness.GetFloat(),
		clamp( ( p.flCloudCoverage - 0.08f ) * 1.4f, 0.0f, 0.85f ),
		s_flSteps[ clamp( iQuality, 0, 3 ) ],
		bHistory ? 0.88f : 0.0f );

	data.vecScreenParams.Init( 1.0f / Max( view.width, 1 ), 1.0f / Max( view.height, 1 ),
		1.0f / g_iCloudDivisor, (float)( gpGlobals->framecount & 1023 ) );

	VMatrix matCurViewProj;
	BuildRotationViewProj( view, matCurViewProj );
	data.matPrevViewProj = g_bHavePrevViewProj ? g_matPrevViewProj : matCurViewProj;
	g_matPrevViewProj = matCurViewProj;
	g_bHavePrevViewProj = true;

	// --- rain occlusion map
	data.bRainMapValid = g_bRainMapValid;
	data.matRainMap = g_matRainMap;
	const float flTexel = g_flRainMapSize / RAINMAP_RES;
	const float flDropFraction = clamp( p.flRain * r_weather_rain_density.GetFloat(), 0.0f, 1.0f );
	data.vecRainMapParams.Init( ( flTexel * 2.0f ) / g_flRainMapZFar, g_flRainMapSize, flTexel, flDropFraction );

	QUEUE_FIRE( CommitWeatherData, data );

	// ping-pong: this frame renders into [cur], reads history from [1-cur]
	g_iCloudCurrent = 1 - g_iCloudCurrent;
	ITexture *pCloud = g_tex_Clouds[ g_iCloudCurrent ];
	ITexture *pHistory = g_tex_Clouds[ 1 - g_iCloudCurrent ];
	ITexture *pNoise = GetWeatherTexture_CloudNoise();
	ITexture *pWeatherMap = GetWeatherTexture_WeatherMap();
	ITexture *pRainMap = g_tex_RainDepth;
	QUEUE_FIRE( CommitTexture_Weather, pCloud, pHistory, pNoise, pWeatherMap, pRainMap );
}

void WeatherRender_SetCloudTextureValid( bool bValid )
{
	QUEUE_FIRE( CommitWeatherCloudTextureValid, bValid );
}

// ---------------------------------------------------------------------------
// Volumetric clouds (screen space, reduced resolution, temporally accumulated)
// ---------------------------------------------------------------------------
void WeatherRender_Clouds( const CViewSetup &view )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	const int iQuality = r_weather_clouds.GetInt();
	if ( !pSys->IsActive() || iQuality <= 0 || !g_pMatClouds || pSys->GetParams().flCloudCoverage < 0.005f )
	{
		g_bCloudHistoryValid = false;
		WeatherRender_SetCloudTextureValid( false );
		return;
	}

	const int w = Max( 1, view.width / g_iCloudDivisor );
	const int h = Max( 1, view.height / g_iCloudDivisor );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( g_tex_Clouds[ g_iCloudCurrent ], 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatClouds,
		0, 0, w, h,
		0, 0, w - 1, h - 1,
		w, h );
	pRenderContext->PopRenderTargetAndViewport();

	g_bCloudHistoryValid = true;
	g_iLastCloudDivisor = g_iCloudDivisor;
	g_iLastCloudFrame = gpGlobals->framecount;

	WeatherRender_SetCloudTextureValid( true );
}

// ---------------------------------------------------------------------------
// Wet surfaces / puddles / height fog, applied to the lit opaque scene
// ---------------------------------------------------------------------------
void WeatherRender_PostOpaque( const CViewSetup &view )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	if ( !pSys->IsActive() || !g_pMatPost )
		return;

	const WeatherParams_t &p = pSys->GetParams();
	const bool bWet = r_weather_wetness.GetBool() && pSys->GetWetness() > 0.005f;
	const bool bFog = r_weather_fog.GetBool() && p.flFogDensity > 0.001f;
	if ( !bWet && !bFog )
		return;

	DrawLightPassFullscreen( g_pMatPost, view.width, view.height );
}

// ---------------------------------------------------------------------------
// Rain: a static mesh of drop quads, animated entirely in the vertex shader.
// ---------------------------------------------------------------------------
#define RAIN_MAX_DROPS 16000

static IMesh *g_pRainMesh = NULL;

static void BuildRainMesh()
{
	if ( g_pRainMesh || !g_pMatRain )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	const VertexFormat_t fmt = VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ) | VERTEX_TEXCOORD_SIZE( 1, 4 );
	g_pRainMesh = pRenderContext->CreateStaticMesh( fmt, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_OTHER, g_pMatRain );
	if ( !g_pRainMesh )
		return;

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( g_pRainMesh, MATERIAL_QUADS, RAIN_MAX_DROPS );

	static const float s_flCorners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for ( int i = 0; i < RAIN_MAX_DROPS; i++ )
	{
		// position in the unit box, plus per-drop random values
		const Vector vecSeed( RandomFloat( 0, 1 ), RandomFloat( 0, 1 ), RandomFloat( 0, 1 ) );
		const float flOrder = ( i + 0.5f ) / RAIN_MAX_DROPS;	// drops are enabled in order as rain increases
		const float flSize = RandomFloat( 0.75f, 1.25f );
		const float flSpeed = RandomFloat( 0.85f, 1.15f );
		const float flBright = RandomFloat( 0.6f, 1.0f );

		for ( int c = 0; c < 4; c++ )
		{
			meshBuilder.Position3fv( vecSeed.Base() );
			meshBuilder.TexCoord2f( 0, s_flCorners[c][0], s_flCorners[c][1] );
			meshBuilder.TexCoord4f( 1, flOrder, flSize, flSpeed, flBright );
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
}

static void DestroyRainMesh()
{
	if ( g_pRainMesh )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->DestroyStaticMesh( g_pRainMesh );
		g_pRainMesh = NULL;
	}
}

void WeatherRender_Rain( const CViewSetup &view )
{
	if ( !WeatherRender_WantsRain() || !g_pMatRain )
		return;

	BuildRainMesh();
	if ( !g_pRainMesh )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->Bind( g_pMatRain );
	g_pRainMesh->Draw();

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

// ---------------------------------------------------------------------------
// Sun shafts: raymarch the sun shadow map through the (height) fog
// ---------------------------------------------------------------------------
void WeatherRender_SunShafts( const CViewSetup &view, ITexture *pTarget )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	if ( !pSys->IsActive() || r_weather_sunshafts.GetInt() <= 0 || !g_pMatSunShafts || !pTarget )
		return;

	if ( pSys->GetParams().flVolumetricDensity <= 0.001f )
		return;

	const int w = pTarget->GetActualWidth();
	const int h = pTarget->GetActualHeight();

	static ConVarRef cl_weather_debug( "cl_weather_debug" );
	if ( cl_weather_debug.IsValid() && cl_weather_debug.GetBool() )
	{
		static float s_flNext = 0.0f;
		if ( gpGlobals->realtime > s_flNext )
		{
			s_flNext = gpGlobals->realtime + 1.0f;
			Msg( "[sunshafts] draw %dx%d into %s (material %s error %d, shader %s)\n", w, h, pTarget->GetName(),
				g_pMatSunShafts->GetName(), g_pMatSunShafts->IsErrorMaterial() ? 1 : 0, g_pMatSunShafts->GetShaderName() );
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( pTarget );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatSunShafts,
		0, 0, w, h,
		0, 0, w - 1, h - 1,
		w, h );
	pRenderContext->PopRenderTargetAndViewport();
}

// ---------------------------------------------------------------------------
// Rain occlusion map scheduling
// ---------------------------------------------------------------------------
bool WeatherRender_ShouldUpdateRainMap( const CViewSetup &view, Vector &vecCenter, float &flSize )
{
	if ( !WeatherRender_WantsRainMap() )
		return false;

	flSize = Clamp( r_weather_rainmap_size.GetFloat(), 1024.0f, 8192.0f );
	const float flTexel = flSize / RAINMAP_RES;

	// snap to texels so the map doesn't swim
	vecCenter.x = floorf( view.origin.x / flTexel ) * flTexel;
	vecCenter.y = floorf( view.origin.y / flTexel ) * flTexel;
	vecCenter.z = view.origin.z;

	if ( !g_bRainMapValid || fabsf( g_flRainMapSize - flSize ) > 1.0f )
		return true;

	const float flMoved = ( vecCenter.AsVector2D() - g_vecRainMapCenter.AsVector2D() ).Length();
	if ( flMoved > flSize * 0.12f || fabsf( vecCenter.z - g_vecRainMapCenter.z ) > 512.0f )
		return true;

	// moving doors/props: refresh now and then
	return ( gpGlobals->curtime - g_flRainMapTime ) > 1.0f;
}

void WeatherRender_OnRainMapRendered( const VMatrix &matWorldToTexture, const Vector &vecCenter, float flSize, float flZFar )
{
	g_matRainMap = matWorldToTexture;
	g_vecRainMapCenter = vecCenter;
	g_flRainMapSize = flSize;
	g_flRainMapZFar = flZFar;
	g_flRainMapTime = gpGlobals->curtime;
	g_bRainMapValid = true;
}

// ---------------------------------------------------------------------------
class CWeatherRenderSystem : public CAutoGameSystem
{
public:
	CWeatherRenderSystem() : CAutoGameSystem( "CWeatherRenderSystem" ) {}

	virtual void LevelInitPostEntity()
	{
		WeatherRender_LevelInit();
	}

	virtual void Shutdown()
	{
		DestroyRainMesh();
		if ( g_pMatClouds ) { g_pMatClouds->DecrementReferenceCount(); g_pMatClouds = NULL; }
		if ( g_pMatPost ) { g_pMatPost->DecrementReferenceCount(); g_pMatPost = NULL; }
		if ( g_pMatRain ) { g_pMatRain->DecrementReferenceCount(); g_pMatRain = NULL; }
		if ( g_pMatSunShafts ) { g_pMatSunShafts->DecrementReferenceCount(); g_pMatSunShafts = NULL; }
		ShutdownWeatherTextures();
	}
};
static CWeatherRenderSystem g_WeatherRenderSystem;

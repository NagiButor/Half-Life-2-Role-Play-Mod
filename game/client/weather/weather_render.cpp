//========= HL2RPM ============================================================//
//
// Purpose: Weather rendering passes (see weather_render.h).
//
//=============================================================================//

#include "cbase.h"
#include "weather/weather_render.h"
#include "weather/c_weather_system.h"
#include "deferred/deferred_ssao.h"
#include "deferred/deferred_gi.h"
#include "deferred/deferred_postfx.h"
#include "deferred/deferred_taa.h"
#include "deferred/deferred_shared_common.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imesh.h"
#include "view_shared.h"
#include "viewrender.h"
#include "renderparm.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"
#include "vstdlib/random.h"
#include "hl2rpm_crashdebug.h"

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
static ConVar r_weather_motes( "r_weather_motes", "1", FCVAR_ARCHIVE,
	"Dust motes in the air: specks floating around the camera, lit where the sun reaches them - the beams through the windows of a room" );
static ConVar r_weather_fog( "r_weather_fog", "1", FCVAR_ARCHIVE, "Weather height fog" );
static ConVar r_weather_cloud_temporal( "r_weather_cloud_temporal", "1", FCVAR_ARCHIVE, "Temporal accumulation for clouds (less noise)" );
static ConVar r_weather_cloud_brightness( "r_weather_cloud_brightness", "1.0", FCVAR_ARCHIVE, "Cloud lighting multiplier" );
static ConVar r_weather_rainmap_size( "r_weather_rainmap_size", "3072", 0, "World size (units) covered by the rain occlusion map" );
static ConVar r_weather_godrays( "r_weather_godrays", "1", FCVAR_ARCHIVE, "Crepuscular rays through the gaps in the clouds (screen space)" );
static ConVar r_weather_godrays_strength( "r_weather_godrays_strength", "1.0", FCVAR_ARCHIVE, "Strength of the crepuscular rays" );
static ConVar r_weather_debug_view( "r_weather_debug_view", "0", FCVAR_CHEAT, "Weather debug view: 1 = rain exposure (red = open sky, blue = covered)" );
static ConVar r_weather_ssr( "r_weather_ssr", "1", FCVAR_ARCHIVE,
	"Reflections of the surroundings in puddles and on wet surfaces (screen space): 0 = off, 1 = on, 2 = longer rays", true, 0, true, 2 );
static ConVar r_weather_ssr_puddles( "r_weather_ssr_puddles", "1.0", 0, "Screen space reflections: strength in puddles", true, 0.0f, true, 1.0f );
static ConVar r_weather_ssr_wet( "r_weather_ssr_wet", "0", 0, "Screen space reflections: strength on wet surfaces outside puddles (0 = only puddles: the film on rough ground reflects a blurred sky, little to see for ~1 ms)", true, 0.0f, true, 1.0f );

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
	// also the sky visibility of the ambient light (deferred_ssao.cpp) reads it
	return WeatherRender_WantsRain() || ( r_weather_wetness.GetBool() && pSys->GetWetness() > 0.005f ) || DeferredSSAO_WantsSkyVisibility();
}

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------
#define RAINMAP_RES 1024

static CTextureReference g_tex_Clouds[2];		// resolved clouds, ping-pong history
static CTextureReference g_tex_CloudRaw;		// this frame's raymarch
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

	g_tex_CloudRaw.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_clouds_raw",
		128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
		IMAGE_FORMAT_RGBA16161616F,
		MATERIAL_RT_DEPTH_NONE,
		cloudFlags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );

	g_tex_RainDepth.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_rainmap",
		RAINMAP_RES, RAINMAP_RES,
		RT_SIZE_NO_CHANGE,
		GetDeferredManager()->GetShadowDepthFormat(),
		MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET, 0 ) );

	// the color target keeps the raw depth (R32F, written by the shadow pass like the sun
	// atlas' one): the sky visibility of the ambient light is filtered from it
	// (deferred_ssao.cpp, DEFERRED_SKYVIS)
	g_tex_RainDummy.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_rainmap_raw",
		RAINMAP_RES, RAINMAP_RES,
		RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_R32F,
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
static IMaterial *g_pMatCloudResolve = NULL;
static IMaterial *g_pMatPost = NULL;
static IMaterial *g_pMatRain = NULL;
static IMaterial *g_pMatMotes = NULL;
static IMaterial *g_pMatSunShafts = NULL;
static IMaterial *g_pMatGodRays = NULL;

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
	g_pMatCloudResolve = CreateWeatherMaterial( "__hl2rpm_weather_cloudresolve", "WEATHER_CLOUDRESOLVE" );
	g_pMatPost = CreateWeatherMaterial( "__hl2rpm_weather_post", "WEATHER_POST" );
	g_pMatRain = CreateWeatherMaterial( "__hl2rpm_weather_rain", "WEATHER_RAIN" );
	g_pMatMotes = CreateWeatherMaterial( "__hl2rpm_weather_motes", "WEATHER_MOTES" );
	g_pMatSunShafts = CreateWeatherMaterial( "__hl2rpm_volume_sun", "VOLUME_SUN" );
	g_pMatGodRays = CreateWeatherMaterial( "__hl2rpm_godrays", "WEATHER_GODRAYS" );
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

// where the light of sky and clouds (the sun, or the moon) is in the main view this frame
static bool s_bSunScreenValid = false;
static Vector2D s_vecSunScreen( 0.5f, 0.5f );
static Vector s_vecSunScreenDir( 0, 0, 1 );

bool WeatherRender_GetSunScreenPos( Vector2D &vecScreen, Vector &vecDir )
{
	vecScreen = s_vecSunScreen;
	vecDir = s_vecSunScreenDir;
	return s_bSunScreenValid;
}

void WeatherRender_LevelInit()
{
	s_bSunScreenValid = false;
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
	// the engine's exact matrices for this view (a hand-built one was mirrored, which
	// made the reprojected history drift the wrong way while turning)
	VMatrix matWorldToView, matViewToProj, matWorldToProj, matWorldToPixels;
	render->GetMatricesForView( view, &matWorldToView, &matViewToProj, &matWorldToProj, &matWorldToPixels );
	matWorldToView.SetTranslation( vec3_origin );
	MatrixMultiply( matViewToProj, matWorldToView, out );
}

static Vector4D s_vecSkyLightDir( 0, 0, 1, 0 );
static Vector s_vecWeatherFogColor( 0.5f, 0.5f, 0.5f );
static bool s_bWeatherFogColorValid = false;

bool WeatherRender_GetSkyboxFogColor( float *pColor )
{
	if ( !GetWeatherSystem()->IsActive() || !s_bWeatherFogColorValid )
		return false;
	// the fog color is linear scene light, the engine fog wants gamma space
	for ( int i = 0; i < 3; i++ )
		pColor[i] = powf( clamp( s_vecWeatherFogColor[i], 0.0f, 1.0f ), 1.0f / 2.2f );
	return true;
}
static Vector4D s_vecSkyLightDiff( 0, 0, 0, 0 );
static bool s_bGodRaysVisible = false;	// this frame's sun / moon is on (or near) the screen

void WeatherRender_SetSkyLight( const lightData_Global_t &light )
{
	s_vecSkyLightDir = light.vecLight;
	s_vecSkyLightDiff = light.diff;
}

static void UpdateForwardMaterials( float flScale );

void WeatherRender_CommitFrame( const CViewSetup &view, const lightData_Global_t &light )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	weatherData_t data;

	if ( !pSys->IsActive() )
	{
		data.bEnabled = false;
		QUEUE_FIRE( CommitWeatherData, data );
		UpdateForwardMaterials( 1.0f );
		return;
	}

	EnsureWeatherMaterials();
	UpdateForwardMaterials( pSys->GetEnvLightScale() );

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
	data.vecDebug.Init( (float)r_weather_debug_view.GetInt(), 0.0f, 0.0f, 0.0f );
	data.vecSkyLightDir = s_vecSkyLightDir;
	data.vecSkyLightDiff = s_vecSkyLightDiff;

	// --- where the sun (or the moon) is on screen, for the lens flare (deferred_postfx.cpp)
	s_bSunScreenValid = false;
	{
		const Vector vecLightDir = s_vecSkyLightDir.AsVector3D().Normalized();
		Vector vecForward;
		AngleVectors( view.angles, &vecForward );
		if ( DotProduct( vecForward, vecLightDir ) > 0.05f )
		{
			VMatrix matWorldToView, matViewToProj, matWorldToProj, matWorldToPixels;
			render->GetMatricesForView( view, &matWorldToView, &matViewToProj, &matWorldToProj, &matWorldToPixels );
			Vector4D vecClip;
			matWorldToProj.V4Mul( Vector4D( view.origin.x + vecLightDir.x * 1000.0f, view.origin.y + vecLightDir.y * 1000.0f,
				view.origin.z + vecLightDir.z * 1000.0f, 1.0f ), vecClip );
			if ( vecClip.w > 0.001f )
			{
				s_vecSunScreen.Init( vecClip.x / vecClip.w * 0.5f + 0.5f, -vecClip.y / vecClip.w * 0.5f + 0.5f );
				s_vecSunScreenDir = vecLightDir;
				s_bSunScreenValid = true;
			}
		}
	}

	// --- crepuscular rays: where the sun (or the moon) is on screen
	data.vecGodRays0.Init();
	data.vecGodRays1.Init();
	s_bGodRaysVisible = false;
	if ( r_weather_godrays.GetBool() && r_weather_sunshafts.GetInt() > 0 )
	{
		const Vector vecLightDir = s_vecSkyLightDir.AsVector3D().Normalized();
		Vector vecForward;
		AngleVectors( view.angles, &vecForward );
		const float flFacing = DotProduct( vecForward, vecLightDir );
		if ( flFacing > 0.05f && vecLightDir.z > -0.03f )
		{
			VMatrix matWorldToView, matViewToProj, matWorldToProj, matWorldToPixels;
			render->GetMatricesForView( view, &matWorldToView, &matViewToProj, &matWorldToProj, &matWorldToPixels );
			Vector4D vecClip;
			matWorldToProj.V4Mul( Vector4D( view.origin.x + vecLightDir.x * 1000.0f, view.origin.y + vecLightDir.y * 1000.0f,
				view.origin.z + vecLightDir.z * 1000.0f, 1.0f ), vecClip );
			if ( vecClip.w > 0.001f )
			{
				const float flX = vecClip.x / vecClip.w * 0.5f + 0.5f;
				const float flY = -vecClip.y / vecClip.w * 0.5f + 0.5f;
				// fade out as the sun leaves the screen and near the horizon
				const float flOff = Max( fabsf( flX - 0.5f ), fabsf( flY - 0.5f ) );
				const float flOnScreen = clamp( ( 1.1f - flOff ) / 0.5f, 0.0f, 1.0f );
				const float flHorizon = clamp( ( vecLightDir.z + 0.03f ) / 0.08f, 0.0f, 1.0f );
				const float flNight = pSys->GetNightFactor();
				float flStrength = ( 0.15f + p.flVolumetricDensity * 0.85f ) * flOnScreen * flHorizon * r_weather_godrays_strength.GetFloat();
				flStrength *= Lerp( flNight, 1.0f, 0.25f );
				// light: the direct light above the clouds, dimmed like the sky it shines through
				Vector vecColor = vecDirect * 0.18f;
				data.vecGodRays0.Init( flX, flY, flStrength, (float)view.width / Max( view.height, 1 ) );
				data.vecGodRays1.Init( vecColor.x, vecColor.y, vecColor.z, 0.965f );
				s_bGodRaysVisible = flStrength > 0.0f;
			}
		}
	}

	// --- sky light, used for cloud ambient, fog and wet reflections
	Vector vecAmbHigh = light.ambh.AsVector3D();
	Vector vecAmbLow = light.ambl.AsVector3D();
	Vector vecZenith = vecAmbHigh * p.flSkyBrightness;
	Vector vecHorizon = ( vecAmbHigh * 0.75f + vecAmbLow * 0.25f ) * ( 1.15f * p.flSkyBrightness ) + light.diff.AsVector3D() * 0.04f;
	data.vecSkyZenith.Init( vecZenith.x, vecZenith.y, vecZenith.z, pSys->GetEnvLightScale() );
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
	s_vecWeatherFogColor = vecFog;
	s_bWeatherFogColorValid = true;

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
		&& g_iLastCloudDivisor == g_iCloudDivisor && bMainSized && g_bHavePrevViewProj;

	static const float s_flSteps[4] = { 0.0f, 28.0f, 36.0f, 52.0f };
	data.vecCloudParams2.Init( r_weather_cloud_brightness.GetFloat(),
		clamp( ( p.flCloudCoverage - 0.08f ) * 1.4f, 0.0f, 0.85f ),
		s_flSteps[ clamp( iQuality, 0, 3 ) ],
		bHistory ? 0.9f : 0.0f );

	data.vecScreenParams.Init( 1.0f / Max( view.width, 1 ), 1.0f / Max( view.height, 1 ),
		1.0f / g_iCloudDivisor, (float)( gpGlobals->framecount & 1023 ) );

	// last frame's main view (updated by WeatherRender_Clouds only, other views
	// such as reflections must not overwrite it)
	if ( g_bHavePrevViewProj )
		data.matPrevViewProj = g_matPrevViewProj;
	else
		BuildRotationViewProj( view, data.matPrevViewProj );

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
	ITexture *pCloudRaw = g_tex_CloudRaw;
	ITexture *pNoise = GetWeatherTexture_CloudNoise();
	ITexture *pWeatherMap = GetWeatherTexture_WeatherMap();
	ITexture *pRainMap = g_tex_RainDepth;
	QUEUE_FIRE( CommitTexture_Weather, pCloud, pHistory, pCloudRaw, pNoise, pWeatherMap, pRainMap );
}

static bool s_bCloudTextureValidNow = false;

void WeatherRender_SetCloudTextureValid( bool bValid )
{
	s_bCloudTextureValidNow = bValid;
	QUEUE_FIRE( CommitWeatherCloudTextureValid, bValid );
}

bool WeatherRender_IsCloudTextureValid()
{
	return s_bCloudTextureValidNow;
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

	// 1. jittered raymarch of this frame
	pRenderContext->PushRenderTargetAndViewport( g_tex_CloudRaw, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatClouds,
		0, 0, w, h,
		0, 0, w - 1, h - 1,
		w, h );
	pRenderContext->PopRenderTargetAndViewport();

	// 2. temporal resolve: reprojected history clamped to this frame's neighbourhood
	pRenderContext->PushRenderTargetAndViewport( g_tex_Clouds[ g_iCloudCurrent ], 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatCloudResolve ? g_pMatCloudResolve : g_pMatClouds,
		0, 0, w, h,
		0, 0, w - 1, h - 1,
		w, h );
	pRenderContext->PopRenderTargetAndViewport();

	BuildRotationViewProj( view, g_matPrevViewProj );
	g_bHavePrevViewProj = true;

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
	if ( !bWet && !bFog && r_weather_debug_view.GetInt() == 0 )
		return;

	// HL2RPM: screen space reflections read the lit opaque scene - only the main view's own
	// (the water / glass views have other cameras)
	CMatRenderContextPtr pRenderContext( materials );
	int iSSRSteps = 0;
	if ( bWet && r_weather_ssr.GetInt() > 0 && CurrentViewID() == VIEW_MAIN )
	{
		ITexture *pFB = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
		if ( pFB && !pFB->IsError() )
		{
			Rect_t rect;
			rect.x = view.x;
			rect.y = view.y;
			rect.width = view.width;
			rect.height = view.height;
			pRenderContext->CopyRenderTargetToTextureEx( pFB, 0, &rect, &rect );
			iSSRSteps = ( r_weather_ssr.GetInt() >= 2 ) ? 20 : 14;
		}
	}
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_HL2RPM_SSR, iSSRSteps );

	DrawLightPassFullscreen( g_pMatPost, view.width, view.height );

	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_HL2RPM_SSR, 0 );
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

	RPM_CRUMB( "weather: build rain mesh" );
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
// Dust motes: as the rain, a static mesh of quads placed and moved in the vertex
// shader (weather_motes_vs30) - specks floating around the camera. The pixel shader
// lights them with the sun through its cascaded shadows: they show in the beams
// that come through the windows of a room, hardly outdoors.
// ---------------------------------------------------------------------------
#define MOTES_MAX 6000

static IMesh *g_pMotesMesh = NULL;

static void BuildMotesMesh()
{
	if ( g_pMotesMesh || !g_pMatMotes )
		return;

	RPM_CRUMB( "weather: build motes mesh" );
	CMatRenderContextPtr pRenderContext( materials );
	const VertexFormat_t fmt = VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ) | VERTEX_TEXCOORD_SIZE( 1, 4 );
	g_pMotesMesh = pRenderContext->CreateStaticMesh( fmt, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_OTHER, g_pMatMotes );
	if ( !g_pMotesMesh )
		return;

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( g_pMotesMesh, MATERIAL_QUADS, MOTES_MAX );

	static const float s_flCorners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for ( int i = 0; i < MOTES_MAX; i++ )
	{
		// position in the unit box, plus per-mote random values
		const Vector vecSeed( RandomFloat( 0, 1 ), RandomFloat( 0, 1 ), RandomFloat( 0, 1 ) );
		const float flOrder = ( i + 0.5f ) / MOTES_MAX;	// motes are enabled in order (density)
		// a few larger specks among many small ones
		const float flRnd = RandomFloat( 0, 1 );
		const float flSize = 0.6f + 1.6f * flRnd * flRnd * flRnd;
		const float flPhase = RandomFloat( 0, 1 );
		const float flBright = RandomFloat( 0.35f, 1.0f );

		for ( int c = 0; c < 4; c++ )
		{
			meshBuilder.Position3fv( vecSeed.Base() );
			meshBuilder.TexCoord2f( 0, s_flCorners[c][0], s_flCorners[c][1] );
			meshBuilder.TexCoord4f( 1, flOrder, flSize, flPhase, flBright );
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
}

static void DestroyMotesMesh()
{
	if ( g_pMotesMesh )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->DestroyStaticMesh( g_pMotesMesh );
		g_pMotesMesh = NULL;
	}
}

void WeatherRender_Motes( const CViewSetup &view )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	if ( !r_weather_motes.GetBool() || !pSys || !pSys->IsActive() || !g_pMatMotes || g_pMatMotes->IsErrorMaterial() )
		return;

	BuildMotesMesh();
	if ( !g_pMotesMesh )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->Bind( g_pMatMotes );
	g_pMotesMesh->Draw();

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

// ---------------------------------------------------------------------------
// Lightning channel: a branching bolt drawn far away in the strike direction,
// sized so that it spans from the horizon to the cloud base as seen from the
// camera. Depth tested: buildings and terrain hide its lower part.
// ---------------------------------------------------------------------------
static IMaterial *g_pMatLightning = NULL;

struct BoltSegment_t
{
	Vector a, b;		// in bolt space: z up (0 ground .. 1 cloud base), x sideways, y depth; units of height
	float flWidthA, flWidthB;
	float flBrightA, flBrightB;
};
static CUtlVector<BoltSegment_t> s_BoltSegments;
static int s_iBoltSeed = 0;

static void BoltPath( CUniformRandomStream &rng, const Vector &a, const Vector &b, float flRough, int nLevels, CUtlVector<Vector> &pts )
{
	pts.RemoveAll();
	pts.AddToTail( a );
	pts.AddToTail( b );
	CUtlVector<Vector> next;
	for ( int lvl = 0; lvl < nLevels; lvl++ )
	{
		next.RemoveAll();
		for ( int i = 0; i < pts.Count() - 1; i++ )
		{
			const Vector &p0 = pts[i];
			const Vector &p1 = pts[i + 1];
			const float flLen = ( p1 - p0 ).Length();
			Vector mid = ( p0 + p1 ) * 0.5f;
			mid.x += rng.RandomFloat( -1.0f, 1.0f ) * flLen * flRough;
			mid.y += rng.RandomFloat( -1.0f, 1.0f ) * flLen * flRough * 0.5f;
			mid.z += rng.RandomFloat( -0.25f, 0.25f ) * flLen * flRough;
			next.AddToTail( p0 );
			next.AddToTail( mid );
		}
		next.AddToTail( pts.Tail() );
		pts.Swap( next );
	}
}

static void BoltAddPath( const CUtlVector<Vector> &pts, float flWidth0, float flWidth1, float flBright0, float flBright1 )
{
	const int n = pts.Count() - 1;
	for ( int i = 0; i < n; i++ )
	{
		const float t0 = i / (float)n;
		const float t1 = ( i + 1 ) / (float)n;
		BoltSegment_t &seg = s_BoltSegments[ s_BoltSegments.AddToTail() ];
		seg.a = pts[i];
		seg.b = pts[i + 1];
		seg.flWidthA = Lerp( t0, flWidth0, flWidth1 );
		seg.flWidthB = Lerp( t1, flWidth0, flWidth1 );
		seg.flBrightA = Lerp( t0, flBright0, flBright1 );
		seg.flBrightB = Lerp( t1, flBright0, flBright1 );
	}
}

static void BuildBoltShape( int iSeed )
{
	if ( iSeed == s_iBoltSeed && s_BoltSegments.Count() )
		return;
	s_iBoltSeed = iSeed;
	s_BoltSegments.RemoveAll();

	CUniformRandomStream rng;
	rng.SetSeed( iSeed );

	// main channel: cloud base to the ground (a little below the horizon)
	CUtlVector<Vector> main;
	const Vector vecTop( 0.0f, 0.0f, 1.0f );
	const Vector vecGround( rng.RandomFloat( -0.25f, 0.25f ), rng.RandomFloat( -0.1f, 0.1f ), -0.04f );
	BoltPath( rng, vecTop, vecGround, 0.22f, 7, main );
	BoltAddPath( main, 1.0f, 1.0f, 1.0f, 1.0f );

	// branches leave the upper part of the channel and die out on the way down
	CUtlVector<Vector> branch, sub;
	const int nBranches = rng.RandomInt( 3, 7 );
	for ( int i = 0; i < nBranches; i++ )
	{
		const int iStart = rng.RandomInt( main.Count() / 20, main.Count() * 7 / 10 );
		const Vector &p = main[iStart];
		const float flSide = rng.RandomFloat( 0.0f, 1.0f ) < 0.5f ? -1.0f : 1.0f;
		const float flLen = rng.RandomFloat( 0.15f, 0.45f ) * Max( p.z, 0.2f );
		const Vector vecEnd = p + Vector( flSide * rng.RandomFloat( 0.3f, 0.9f ) * flLen, rng.RandomFloat( -0.3f, 0.3f ) * flLen, -flLen );
		BoltPath( rng, p, vecEnd, 0.28f, 5, branch );
		const float flBright = rng.RandomFloat( 0.35f, 0.65f );
		BoltAddPath( branch, 0.6f, 0.25f, flBright, flBright * 0.15f );

		if ( rng.RandomFloat( 0.0f, 1.0f ) < 0.5f )
		{
			const Vector &q = branch[ rng.RandomInt( branch.Count() / 4, branch.Count() * 3 / 4 ) ];
			const float flSubLen = flLen * rng.RandomFloat( 0.3f, 0.6f );
			const Vector vecSubEnd = q + Vector( -flSide * rng.RandomFloat( 0.2f, 0.7f ) * flSubLen, 0.0f, -flSubLen );
			BoltPath( rng, q, vecSubEnd, 0.3f, 4, sub );
			BoltAddPath( sub, 0.35f, 0.15f, flBright * 0.5f, 0.0f );
		}
	}
}

void WeatherRender_Lightning( const CViewSetup &view )
{
	LightningBoltInfo_t bolt;
	if ( !GetWeatherSystem()->GetLightningBolt( bolt ) )
		return;

	if ( !g_pMatLightning )
	{
		g_pMatLightning = materials->FindMaterial( "hl2rpm/weather/lightning_bolt", TEXTURE_GROUP_OTHER );
		if ( !g_pMatLightning || g_pMatLightning->IsErrorMaterial() )
		{
			g_pMatLightning = NULL;
			return;
		}
		g_pMatLightning->IncrementReferenceCount();
	}

	BuildBoltShape( bolt.iSeed );
	if ( !s_BoltSegments.Count() )
		return;

	// place it far away (in front of the sky, behind almost everything) keeping its angular size
	const float flRenderDist = clamp( view.zFar * 0.8f, 3000.0f, 24000.0f );
	const float flHeight = flRenderDist * clamp( bolt.flCloudBase / Max( bolt.flDistance, 100.0f ), 0.03f, 1.2f );
	const Vector vecBase = view.origin + bolt.vecDir * flRenderDist;
	const Vector vecUp( 0, 0, 1 );
	Vector vecRight = CrossProduct( bolt.vecDir, vecUp );
	VectorNormalize( vecRight );

	// channel width: a few meters of glow at the real distance
	const float flCoreWidth = flRenderDist * clamp( 5.0f / bolt.flDistance, 0.0006f, 0.004f );
	const float flBright = Min( bolt.flBrightness * 1.8f, 1.0f ) * ( 0.35f + 0.65f * bolt.flVisibility );
	const Vector vecCoreColor( 0.92f, 0.95f, 1.0f );
	const Vector vecGlowColor( 0.60f, 0.66f, 1.0f );

	static ConVarRef cl_weather_debug( "cl_weather_debug" );
	if ( cl_weather_debug.IsValid() && cl_weather_debug.GetBool() )
	{
		Msg( "[lightning] draw: %d segs, dist %.0f units height %.0f width %.1f bright %.2f zfar %.0f\n",
			s_BoltSegments.Count(), flRenderDist, flHeight, flCoreWidth, flBright, view.zFar );
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->Bind( g_pMatLightning );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	const int nSegs = s_BoltSegments.Count();
	CMeshBuilder mb;
	mb.Begin( pMesh, MATERIAL_QUADS, nSegs * 2 );

	for ( int pass = 0; pass < 2; pass++ )
	{
		// pass 0: wide dim glow, pass 1: hot core
		const float flWidthScale = pass == 0 ? 6.0f : 1.0f;
		const Vector &vecColor = pass == 0 ? vecGlowColor : vecCoreColor;
		const float flPassBright = pass == 0 ? 0.45f : 1.0f;

		for ( int i = 0; i < nSegs; i++ )
		{
			const BoltSegment_t &seg = s_BoltSegments[i];
			const Vector a = vecBase + vecRight * ( seg.a.x * flHeight ) + bolt.vecDir * ( seg.a.y * flHeight ) + vecUp * ( seg.a.z * flHeight );
			const Vector b = vecBase + vecRight * ( seg.b.x * flHeight ) + bolt.vecDir * ( seg.b.y * flHeight ) + vecUp * ( seg.b.z * flHeight );

			Vector vecSide = CrossProduct( b - a, view.origin - ( a + b ) * 0.5f );
			if ( VectorNormalize( vecSide ) < 1e-4f )
				continue;

			// the top disappears into the cloud base
			const float flFadeA = clamp( ( 1.0f - seg.a.z ) / 0.18f, 0.0f, 1.0f );
			const float flFadeB = clamp( ( 1.0f - seg.b.z ) / 0.18f, 0.0f, 1.0f );
			const float flA = clamp( seg.flBrightA * flFadeA * flBright * flPassBright, 0.0f, 1.0f );
			const float flB = clamp( seg.flBrightB * flFadeB * flBright * flPassBright, 0.0f, 1.0f );
			const Vector vecSideA = vecSide * ( seg.flWidthA * flCoreWidth * flWidthScale );
			const Vector vecSideB = vecSide * ( seg.flWidthB * flCoreWidth * flWidthScale );

			const unsigned char rA = (unsigned char)( vecColor.x * flA * 255.0f ), gA = (unsigned char)( vecColor.y * flA * 255.0f ), bA = (unsigned char)( vecColor.z * flA * 255.0f );
			const unsigned char rB = (unsigned char)( vecColor.x * flB * 255.0f ), gB = (unsigned char)( vecColor.y * flB * 255.0f ), bB = (unsigned char)( vecColor.z * flB * 255.0f );

			mb.Position3fv( ( a - vecSideA ).Base() );
			mb.Color4ub( rA, gA, bA, 255 );
			mb.TexCoord2f( 0, 0.0f, 0.0f );
			mb.AdvanceVertex();

			mb.Position3fv( ( a + vecSideA ).Base() );
			mb.Color4ub( rA, gA, bA, 255 );
			mb.TexCoord2f( 0, 1.0f, 0.0f );
			mb.AdvanceVertex();

			mb.Position3fv( ( b + vecSideB ).Base() );
			mb.Color4ub( rB, gB, bB, 255 );
			mb.TexCoord2f( 0, 1.0f, 1.0f );
			mb.AdvanceVertex();

			mb.Position3fv( ( b - vecSideB ).Base() );
			mb.Color4ub( rB, gB, bB, 255 );
			mb.TexCoord2f( 0, 0.0f, 1.0f );
			mb.AdvanceVertex();
		}
	}

	mb.End();
	pMesh->Draw();

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
	// crepuscular rays through the cloud gaps, added on top (only with the sun / moon in view)
	if ( s_bGodRaysVisible && r_weather_godrays.GetBool() && g_pMatGodRays && !g_pMatGodRays->IsErrorMaterial() )
	{
		pRenderContext->DrawScreenSpaceRectangle( g_pMatGodRays,
			0, 0, w, h,
			0, 0, w - 1, h - 1,
			w, h );
	}
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

	// snap so a re-render of the same world gives the same map: to 4 texels (the sky
	// visibility filter halves the resolution) and to 64 units of height
	const float flSnap = flTexel * 4.0f;
	vecCenter.x = floorf( view.origin.x / flSnap ) * flSnap;
	vecCenter.y = floorf( view.origin.y / flSnap ) * flSnap;
	vecCenter.z = floorf( view.origin.z / 64.0f ) * 64.0f;

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
// Forward materials under the time of day.
// Water, glass and other alpha-blended materials are not drawn by the deferred
// pipeline: the stock shaders light them with the baked lightmaps / static prop
// lighting and reflect the baked cubemaps - all made for the map's daylight, so at
// night they glowed. Their colors are scaled with the world's brightness now.
// ---------------------------------------------------------------------------
// The material is referenced and the variable looked up by name every time: the
// IMaterialVar pointers used to be kept, but the deferred material passthru rebuilds
// a material's variables when it replaces its shader (SetShaderAndParams, often long
// after this scan, when a model first uses it) and materials are freed on map changes -
// the next update wrote into freed memory (heap corruption: crashes on map change, in
// mat_reloadallmaterials, in GameUI...).
struct ForwardMatVar_t
{
	IMaterial *pMat;
	const char *pszVar;		// a string literal
	Vector vecOrig;
};
static CUtlVector< ForwardMatVar_t > s_ForwardVars;
static int s_nForwardMatCount = -1;
static float s_flForwardScale = 1.0f;

static IMaterialVar *FindForwardVar( IMaterial *pMat, const char *pszVar )
{
	bool bFound = false;
	IMaterialVar *pVar = pMat->FindVar( pszVar, &bFound, false );
	if ( !bFound || !pVar || pVar->GetType() != MATERIAL_VAR_TYPE_VECTOR )
		return NULL;
	return pVar;
}

static void AddForwardVar( IMaterial *pMat, const char *pszVar )
{
	IMaterialVar *pVar = FindForwardVar( pMat, pszVar );
	if ( !pVar )
		return;

	float flVec[3] = { 1.0f, 1.0f, 1.0f };
	pVar->GetVecValue( flVec, 3 );
	pMat->IncrementReferenceCount();
	ForwardMatVar_t &v = s_ForwardVars[ s_ForwardVars.AddToTail() ];
	v.pMat = pMat;
	v.pszVar = pszVar;
	v.vecOrig.Init( flVec[0], flVec[1], flVec[2] );
}

static void RestoreForwardMaterials()
{
	for ( int i = 0; i < s_ForwardVars.Count(); i++ )
	{
		const ForwardMatVar_t &v = s_ForwardVars[i];
		IMaterialVar *pVar = FindForwardVar( v.pMat, v.pszVar );
		if ( pVar )
			pVar->SetVecValue( v.vecOrig.x, v.vecOrig.y, v.vecOrig.z );
		v.pMat->DecrementReferenceCount();
	}
	s_ForwardVars.RemoveAll();
	s_nForwardMatCount = -1;
	s_flForwardScale = 1.0f;
}

static void ScanForwardMaterials()
{
	RestoreForwardMaterials();

	for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
	{
		IMaterial *pMat = materials->GetMaterial( h );
		if ( !pMat || pMat->IsErrorMaterial() )
			continue;
		const char *pszShader = pMat->GetShaderName();
		if ( !pszShader )
			continue;

		if ( V_stristr( pszShader, "Water" ) )
		{
			// the water's own color
			AddForwardVar( pMat, "$fogcolor" );
			// real-time reflections are lit right (REFLECTVIEW); the cubemap one isn't
			bool bFound = false;
			IMaterialVar *pReflect = pMat->FindVar( "$reflecttexture", &bFound, false );
			if ( !bFound || !pReflect || !pReflect->IsDefined() || !pReflect->IsTexture() )
				AddForwardVar( pMat, "$reflecttint" );
		}
		else if ( pMat->IsTranslucent() && ( V_stristr( pszShader, "LightmappedGeneric" ) || V_stristr( pszShader, "VertexLitGeneric" ) ) )
		{
			// glass & co: baked lighting and a baked cubemap
			AddForwardVar( pMat, "$envmaptint" );
			// (models: studiorender overwrites $color with the entity's color modulation on
			// every draw - the scaled value never showed, the translucent shell of the skybox
			// Citadel shone blue at night. Their static tint is $color2, which the base
			// shader multiplies in.)
			AddForwardVar( pMat, V_stristr( pszShader, "VertexLitGeneric" ) ? "$color2" : "$color" );
		}
	}

	s_nForwardMatCount = materials->GetNumMaterials();
	s_flForwardScale = -1.0f;	// apply on the next update
}

static bool IsDeferredShaderMaterial( IMaterial *pMat )
{
	const char *pszShader = pMat->GetShaderName();
	return pszShader && V_strnicmp( pszShader, "DEFERRED_", 9 ) == 0;
}

static void UpdateForwardMaterials( float flScale )
{
	// a scanned material the passthru has since replaced by a deferred shader (it copied
	// our scaled values): put the original values back, it isn't a forward one any more
	bool bReplaced = false;
	for ( int i = 0; i < s_ForwardVars.Count() && !bReplaced; i++ )
		bReplaced = IsDeferredShaderMaterial( s_ForwardVars[i].pMat );

	if ( bReplaced || materials->GetNumMaterials() != s_nForwardMatCount )
		ScanForwardMaterials();

	if ( fabsf( flScale - s_flForwardScale ) < 0.01f )
		return;
	s_flForwardScale = flScale;

	for ( int i = 0; i < s_ForwardVars.Count(); i++ )
	{
		const ForwardMatVar_t &v = s_ForwardVars[i];
		IMaterialVar *pVar = FindForwardVar( v.pMat, v.pszVar );
		if ( pVar )
			pVar->SetVecValue( v.vecOrig.x * flScale, v.vecOrig.y * flScale, v.vecOrig.z * flScale );
	}
}

// ---------------------------------------------------------------------------
class CWeatherRenderSystem : public CAutoGameSystem
{
public:
	CWeatherRenderSystem() : CAutoGameSystem( "CWeatherRenderSystem" ) {}

	virtual void LevelInitPostEntity()
	{
		WeatherRender_LevelInit();
		ScanForwardMaterials();
	}

	virtual void LevelShutdownPreEntity()
	{
		// the materials outlive the map: leave them as they were loaded
		RestoreForwardMaterials();
	}

	virtual void Shutdown()
	{
		DestroyRainMesh();
		DestroyMotesMesh();
		if ( g_pMatMotes ) { g_pMatMotes->DecrementReferenceCount(); g_pMatMotes = NULL; }
		if ( g_pMatClouds ) { g_pMatClouds->DecrementReferenceCount(); g_pMatClouds = NULL; }
		if ( g_pMatPost ) { g_pMatPost->DecrementReferenceCount(); g_pMatPost = NULL; }
		if ( g_pMatRain ) { g_pMatRain->DecrementReferenceCount(); g_pMatRain = NULL; }
		if ( g_pMatSunShafts ) { g_pMatSunShafts->DecrementReferenceCount(); g_pMatSunShafts = NULL; }
		if ( g_pMatGodRays ) { g_pMatGodRays->DecrementReferenceCount(); g_pMatGodRays = NULL; }
		if ( g_pMatLightning ) { g_pMatLightning->DecrementReferenceCount(); g_pMatLightning = NULL; }
		ShutdownSSAO();
		ShutdownGI();
		DeferredPostFX_Shutdown();
		DeferredTAA_Shutdown();
		ShutdownWeatherTextures();
	}
};
static CWeatherRenderSystem g_WeatherRenderSystem;

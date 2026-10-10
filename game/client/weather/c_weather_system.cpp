//========= HL2RPM ============================================================//
//
// Purpose: Client weather system (see c_weather_system.h).
//
//=============================================================================//

#include "cbase.h"
#include "weather/c_weather_system.h"
#include "weather/env_weather.h"
#include "timecycle/env_timecycle.h"
#include "deferred/deferred_shared_common.h"

#include "engine/IEngineSound.h"
#include "soundflags.h"
#include "view.h"
#include "view_shared.h"

#include "tier0/memdbgon.h"

static C_WeatherSystem g_WeatherSystem;

CON_COMMAND_F( cl_weather_set_wetness, "Dev: set the ground's wetness and puddles now: cl_weather_set_wetness <0..1> [puddles 0..1]", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "wetness %.2f puddles %.2f\n", g_WeatherSystem.GetWetness(), g_WeatherSystem.GetPuddles() );
		return;
	}
	const float flWet = atof( args[1] );
	g_WeatherSystem.SetWetness( flWet, ( args.ArgC() > 2 ) ? atof( args[2] ) : flWet );
}
C_WeatherSystem *GetWeatherSystem()
{
	return &g_WeatherSystem;
}

static ConVar cl_weather( "cl_weather", "1", FCVAR_ARCHIVE, "Enable dynamic weather visuals" );
static ConVar cl_weather_volume( "cl_weather_volume", "1.0", FCVAR_ARCHIVE, "Rain / wind / thunder volume" );
static ConVar cl_weather_debug( "cl_weather_debug", "0", 0, "Show weather state on screen" );
static ConVar cl_weather_moonlight( "cl_weather_moonlight", "0.045", FCVAR_ARCHIVE, "Moonlight strength relative to the map's sunlight" );
static ConVar cl_weather_night_ambient( "cl_weather_night_ambient", "0.06", FCVAR_ARCHIVE, "Night sky light relative to the map's day ambient" );
// HL2RPM: a map is a few hundred meters wide and real clouds are 1-2 km up: walking
// across it they would not move at all. The clouds live in a scaled space instead
// (walking moves you 'parallax' times further under them) and drift faster.
static ConVar r_weather_cloud_parallax( "r_weather_cloud_parallax", "10", FCVAR_ARCHIVE, "How much faster the clouds pass over a walking player than real clouds would (cloud space scale)", true, 1.0f, true, 60.0f );
static ConVar r_weather_cloud_wind_scale( "r_weather_cloud_wind_scale", "2.5", FCVAR_ARCHIVE, "Cloud drift speed multiplier", true, 0.0f, true, 10.0f );
static ConVar r_weather_cloud_debug( "r_weather_cloud_debug", "0", FCVAR_CHEAT, "Cloud lighting debug: 1 = sun light only, 2 = sky light only, 3 = opacity" );

#define WEATHER_SOUND_RAIN_OUT	"hl2rpm/weather/rain_loop.wav"
#define WEATHER_SOUND_RAIN_IN	"hl2rpm/weather/rain_roof_loop.wav"
#define WEATHER_SOUND_WIND		"hl2rpm/weather/wind_loop.wav"

// Thunder by distance (hl2rpm/weather/thunder, made from the user's recordings).
// Long rolls stream from disk ('*').
static const char *s_pszThunderClose[] =
{
	"hl2rpm/weather/thunder/close_01.wav",	// short single strike
	"hl2rpm/weather/thunder/close_02.wav",	// clean strike
	"hl2rpm/weather/thunder/close_03.wav",	// summer lightning crash
	"hl2rpm/weather/thunder/close_04.wav",	// crack after a strong flash
	"*hl2rpm/weather/thunder/close_05.wav",	// flash + long roll
	"*hl2rpm/weather/thunder/close_06.wav",	// strong discharge + roll
};
static const char *s_pszThunderMid[] =
{
	"*hl2rpm/weather/thunder/mid_02.wav",
	"*hl2rpm/weather/thunder/mid_03.wav",
	"*hl2rpm/weather/thunder/mid_04.wav",
	"*hl2rpm/weather/thunder/mid_05.wav",
	"*hl2rpm/weather/thunder/mid_06.wav",
	"ambient/atmosphere/thunder1.wav",
	"ambient/atmosphere/thunder3.wav",
};
static const char *s_pszThunderFar[] =
{
	"*hl2rpm/weather/thunder/far_01.wav",
	"*hl2rpm/weather/thunder/far_02.wav",
	"*hl2rpm/weather/thunder/far_03.wav",
	"*hl2rpm/weather/thunder/far_04.wav",
	"*hl2rpm/weather/thunder/far_05.wav",
	"ambient/outro/thunder01.wav",
	"ambient/outro/thunder03.wav",
	"ambient/outro/thunder05.wav",
	"ambient/outro/thunder07.wav",
};
// the first thunder of a storm: a long roll that announces it
#define THUNDER_FIRST_OF_STORM "*hl2rpm/weather/thunder/mid_01.wav"

static const char **s_ppszThunderSets[] = { s_pszThunderClose, s_pszThunderMid, s_pszThunderFar };
static const int s_nThunderSets[] = { ARRAYSIZE( s_pszThunderClose ), ARRAYSIZE( s_pszThunderMid ), ARRAYSIZE( s_pszThunderFar ) };

static ConVar cl_weather_lightning_bolts( "cl_weather_lightning_bolts", "1", FCVAR_ARCHIVE, "Visible lightning channels in the sky" );
static ConVar cl_weather_lightning_yaw( "cl_weather_lightning_yaw", "-1", FCVAR_CHEAT, "Debug: direction (yaw) of the next strikes, -1 = random" );
static ConVar cl_weather_lightning_light( "cl_weather_lightning_light", "1", FCVAR_ARCHIVE, "Close lightning lights the scene from the bolt (with shadows)" );

static inline float SmoothStep01( float x )
{
	x = clamp( x, 0.0f, 1.0f );
	return x * x * ( 3.0f - 2.0f * x );
}

static inline float Luminance( const Vector &v )
{
	return v.x * 0.2126f + v.y * 0.7152f + v.z * 0.0722f;
}

C_WeatherSystem::C_WeatherSystem() : CAutoGameSystemPerFrame( "C_WeatherSystem" )
{
	m_bActive = false;
	m_Current = GetWeatherPresetInfo( WEATHER_FAIR ).params;
	m_From = m_To = m_Current;
	m_iTargetPreset = WEATHER_FAIR;
	m_iLastSerial = -1;
	m_flBlendStart = 0.0f;
	m_flBlendDuration = 0.0f;
	m_flBlend = 1.0f;

	m_vecSunDir.Init( 0, 0, 1 );
	m_flSunAltitude = 90.0f;
	m_vecMoonDir.Init( 0, 0, -1 );
	m_flNightFactor = 0.0f;

	m_vecWindDir.Init( 1, 0 );
	m_flWindYaw = 0.0f;
	m_vecCloudOffset.Init();
	m_flCloudTime = 0.0f;
	m_flGust = 0.0f;

	m_flWetness = 0.0f;
	m_flPuddles = 0.0f;

	m_flNextLightning = 0.0f;
	m_flFlash = 0.0f;
	m_flFlashLight = 0.0f;
	m_vecFlashDir.Init( 1, 0, 0.3f );
	m_vecBoltLightDir.Init( 1, 0, 0.3f );
	m_nPulses = 0;
	m_flStrikeTime = -1000.0f;
	m_flStrikeDistance = 5000.0f;
	m_flStrikeCloudBase = 1000.0f;
	m_flStrikeFade = 1.0f;
	m_iStrikeClass = STRIKE_FAR;
	m_iStrikeSeed = 0;
	m_bStrikeChannel = false;
	m_bLightningWasActive = false;
	m_bFirstStrike = false;
	m_flBaseSunLum = 1.0f;
	m_flBaseSkyLum = 0.3f;

	m_flSkyExposure = 1.0f;
	m_flOuterExposure = 1.0f;
	m_flParticleLightScale = 1.0f;
	m_flEnvLightScale = 1.0f;
	m_flNextExposureTrace = 0.0f;
	m_flRainVolumeOut = m_flRainVolumeIn = m_flWindVolume = 0.0f;
	m_bRainOutPlaying = m_bRainInPlaying = m_bWindPlaying = false;
}

bool C_WeatherSystem::Init()
{
	return true;
}

void C_WeatherSystem::LevelInitPreEntity()
{
	enginesound->PrecacheSound( WEATHER_SOUND_RAIN_OUT, true );
	enginesound->PrecacheSound( WEATHER_SOUND_RAIN_IN, true );
	enginesound->PrecacheSound( WEATHER_SOUND_WIND, true );
	for ( int iSet = 0; iSet < ARRAYSIZE( s_ppszThunderSets ); iSet++ )
	{
		for ( int i = 0; i < s_nThunderSets[iSet]; i++ )
			enginesound->PrecacheSound( s_ppszThunderSets[iSet][i], true );
	}
	enginesound->PrecacheSound( THUNDER_FIRST_OF_STORM, true );
}

void C_WeatherSystem::LevelInitPostEntity()
{
	m_iLastSerial = -1;
	m_flWetness = 0.0f;
	m_flPuddles = 0.0f;
	m_Thunder.RemoveAll();
	m_nPulses = 0;
	m_flFlash = 0.0f;
	m_flFlashLight = 0.0f;
	m_flStrikeTime = -1000.0f;
	m_bLightningWasActive = false;
	m_bFirstStrike = false;
	m_flNextLightning = gpGlobals->curtime + 10.0f;
	// the engine stopped every sound on level change
	m_bRainOutPlaying = m_bRainInPlaying = m_bWindPlaying = false;
	m_flRainVolumeOut = m_flRainVolumeIn = m_flWindVolume = 0.0f;
	m_flNextExposureTrace = 0.0f;
	m_Precip.LevelInit();
}

void C_WeatherSystem::LevelShutdownPreEntity()
{
	StopAudio();
	m_bActive = false;
	m_Precip.LevelShutdown();
}

void C_WeatherSystem::Shutdown()
{
	m_Precip.LevelShutdown();
}

void C_WeatherSystem::Update( float frametime )
{
	CEnvWeather *pWeather = GetWeatherEntity();
	const bool bWasActive = m_bActive;
	m_bActive = pWeather != NULL && cl_weather.GetBool() && GetDeferredManager()->IsDeferredRenderingEnabled();

	if ( !m_bActive )
	{
		m_flParticleLightScale = 1.0f;
		m_flEnvLightScale = 1.0f;
		if ( bWasActive )
		{
			StopAudio();
			WeatherPrecipInput_t none;
			V_memset( &none, 0, sizeof( none ) );
			m_Precip.Update( false, none );
		}
		return;
	}

	// don't simulate while paused, but keep the blend consistent
	const float dt = ( engine->IsPaused() ) ? 0.0f : Clamp( gpGlobals->frametime, 0.0f, 0.1f );

	UpdateTargetFromEntity();
	UpdateBlend();
	UpdateCelestial();
	UpdateWind( dt );
	UpdateWetness( dt );
	UpdateLightning( dt );
	UpdateSkyExposure( dt );
	UpdateAudio( dt );
	UpdatePrecipitation();

	if ( cl_weather_debug.GetBool() )
	{
		engine->Con_NPrintf( 4, "weather: %s -> %s  blend %.2f  auto %d", GetWeatherPresetInfo( m_iTargetPreset ).pszName,
			GetWeatherPresetInfo( m_iTargetPreset ).pszName, m_flBlend, pWeather->IsAutomatic() ? 1 : 0 );
		engine->Con_NPrintf( 5, "clouds cov %.2f dens %.2f type %.2f | rain %.2f wet %.2f puddles %.2f | fog %.2f | sun %.2f",
			m_Current.flCloudCoverage, m_Current.flCloudDensity, m_Current.flCloudType,
			m_Current.flRain, m_flWetness, m_flPuddles, m_Current.flFogDensity, m_Current.flSunIntensity );
		engine->Con_NPrintf( 6, "sun alt %.1f  night %.2f  exposure %.2f  flash %.2f  wind %.0f deg",
			m_flSunAltitude, m_flNightFactor, m_flSkyExposure, m_flFlash, m_flWindYaw );
	}
}

// ---------------------------------------------------------------------------
// Blending
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateTargetFromEntity()
{
	CEnvWeather *pWeather = GetWeatherEntity();
	if ( !pWeather )
		return;

	const int iSerial = pWeather->GetTransitionSerial();
	if ( iSerial == m_iLastSerial )
		return;

	const bool bFirst = ( m_iLastSerial == -1 );
	m_iLastSerial = iSerial;
	m_iTargetPreset = clamp( pWeather->GetTargetPreset(), 0, WEATHER_PRESET_COUNT - 1 );

	m_To = GetWeatherPresetInfo( m_iTargetPreset ).params;
	m_flBlendStart = pWeather->GetTransitionStartTime();
	m_flBlendDuration = pWeather->GetTransitionDuration();

	if ( bFirst || m_flBlendDuration <= 0.0f )
	{
		// level start / load / instant change: snap
		m_From = m_Current = m_To;
		m_flBlendDuration = 0.0f;
		if ( bFirst )
		{
			// start wet if it's already raining
			m_flWetness = m_To.flWetness;
			m_flPuddles = SmoothStep01( ( m_To.flWetness - 0.5f ) * 2.0f );
		}
	}
	else
	{
		// blend from whatever is on screen right now, even mid-transition
		m_From = m_Current;
	}
}

void C_WeatherSystem::UpdateBlend()
{
	if ( m_flBlendDuration <= 0.0f )
	{
		m_flBlend = 1.0f;
		m_Current = m_To;
		return;
	}

	const float t = clamp( ( gpGlobals->curtime - m_flBlendStart ) / m_flBlendDuration, 0.0f, 1.0f );
	m_flBlend = t;
	WeatherParams_Lerp( m_From, m_To, SmoothStep01( t ), m_Current );
}

// ---------------------------------------------------------------------------
// Sun & moon
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateCelestial()
{
	CDeferredLightGlobal *pGlobal = GetGlobalLight();
	if ( pGlobal )
	{
		Vector fwd;
		AngleVectors( pGlobal->GetAbsAngles(), &fwd );
		m_vecSunDir = -fwd;
		VectorNormalize( m_vecSunDir );
	}

	m_flSunAltitude = RAD2DEG( asinf( clamp( m_vecSunDir.z, -1.0f, 1.0f ) ) );
	m_flNightFactor = SmoothStep01( ( -m_flSunAltitude - 3.0f ) / 6.0f );

	// The moon rises around sunset on the opposite side of the sky and follows
	// an arc through the night.
	float flHour = 12.0f;
	if ( GetTimecycle() )
		flHour = GetTimecycle()->GetTimeOfDayHours();

	float flNightPhase = ( flHour >= 18.0f ) ? ( flHour - 18.0f ) / 12.0f : ( flHour + 6.0f ) / 12.0f;
	if ( flHour >= 6.0f && flHour < 18.0f )
		flNightPhase = -0.25f; // below the horizon during the day

	const float flMoonAlt = ( flNightPhase >= 0.0f ) ? sinf( flNightPhase * M_PI_F ) * 52.0f + 4.0f : -20.0f;

	float flSunYaw = RAD2DEG( atan2f( m_vecSunDir.y, m_vecSunDir.x ) );
	const float flMoonYaw = DEG2RAD( flSunYaw + 165.0f );
	float sa, ca;
	SinCos( DEG2RAD( flMoonAlt ), &sa, &ca );
	m_vecMoonDir.Init( ca * cosf( flMoonYaw ), ca * sinf( flMoonYaw ), sa );
	VectorNormalize( m_vecMoonDir );
}

float C_WeatherSystem::GetMoonLightScale() const
{
	if ( m_flNightFactor <= 0.0f || m_vecMoonDir.z <= -0.05f )
		return 0.0f;
	const float flMoonUp = SmoothStep01( ( m_vecMoonDir.z + 0.02f ) / 0.15f );
	return cl_weather_moonlight.GetFloat() * m_flNightFactor * flMoonUp;
}

void C_WeatherSystem::ModifyGlobalLight( lightData_Global_t &data, const Vector &vecBaseDiffuse, const Vector &vecBaseAmbHigh ) const
{
	if ( !m_bActive )
		return;

	const WeatherParams_t &p = m_Current;
	const float flBaseLum = Max( Luminance( vecBaseDiffuse ), 0.001f );
	const float flDay = SmoothStep01( ( m_flSunAltitude + 2.0f ) / 12.0f );

	Vector diff = data.diff.AsVector3D();
	Vector ambH = data.ambh.AsVector3D();
	Vector ambL = data.ambl.AsVector3D();

	// --- Moon: once the sun is well below the horizon the global light becomes the moon.
	if ( m_flNightFactor > 0.0f && m_vecMoonDir.z > -0.05f )
	{
		const Vector vecMoonColor( 0.62f, 0.74f, 1.0f );
		const Vector moon = vecMoonColor * ( flBaseLum * GetMoonLightScale() );
		diff += moon;

		if ( m_flNightFactor > 0.5f )
		{
			data.vecLight.Init( m_vecMoonDir.x, m_vecMoonDir.y, m_vecMoonDir.z, 0.0f );
			data.bShadow = data.bShadow || ( data.bEnabled && m_vecMoonDir.z > 0.08f );
		}

		// moonlit night sky
		const float flNightAmb = cl_weather_night_ambient.GetFloat() * m_flNightFactor;
		ambH += Vector( 0.35f, 0.50f, 0.85f ) * ( Luminance( vecBaseAmbHigh ) * flNightAmb );
		ambL += Vector( 0.25f, 0.35f, 0.60f ) * ( Luminance( vecBaseAmbHigh ) * flNightAmb * 0.6f );
	}

	// --- Weather: clouds block the direct light and turn the sky into one big soft light.
	const float flSun = Clamp( p.flSunIntensity, 0.0f, 2.0f );
	diff *= flSun;

	const float flBlocked = ( 1.0f - Min( flSun, 1.0f ) ) * flDay;
	const Vector vecOvercastSky( 0.92f, 0.96f, 1.0f );
	ambH += vecOvercastSky * ( flBaseLum * flBlocked * 0.12f );
	ambL += vecOvercastSky * ( flBaseLum * flBlocked * 0.07f );

	ambH *= p.flAmbientIntensity;
	ambL *= p.flAmbientIntensity;

	// overcast light is grey and comes evenly from everywhere
	const float flDesat = Clamp( p.flAmbientDesaturation, 0.0f, 1.0f );
	ambH = Lerp( flDesat, ambH, Vector( 1, 1, 1 ) * Luminance( ambH ) );
	ambL = Lerp( flDesat, ambL, Vector( 1, 1, 1 ) * Luminance( ambL ) );
	ambL = Lerp( flDesat * 0.5f, ambL, ambH * 0.85f );
	diff = Lerp( flDesat * 0.35f, diff, Vector( 1, 1, 1 ) * Luminance( diff ) );

	// the lightning flash is added after the time smoothing of the global light (ApplyLightningFlash)
	m_flBaseSunLum = flBaseLum;
	m_flBaseSkyLum = Max( Luminance( vecBaseAmbHigh ), 0.001f );

	data.diff.Init( diff.x, diff.y, diff.z, data.diff.w );
	data.ambh.Init( ambH.x, ambH.y, ambH.z, data.ambh.w );
	data.ambl.Init( ambL.x, ambL.y, ambL.z, data.ambl.w );

	// Particles are lit from the lightmaps, baked for the map's daylight: how bright
	// is a horizontal surface outdoors now compared to that (sky light + direct light)
	const float flLightUp = Max( data.vecLight.z, 0.0f );
	const float flNow = Luminance( ambH ) * 0.6f + Luminance( ambL ) * 0.4f + Luminance( diff ) * flLightUp;
	const float flBase = Luminance( vecBaseAmbHigh ) + flBaseLum * 0.8f;
	// softened: splashes and drops also catch the sky, and should stay readable in a storm
	m_flParticleLightScale = powf( Clamp( flNow / Max( flBase, 0.001f ), 0.005f, 1.8f ), 0.6f );
	// cubemap reflections: linear, a reflection at night is really dark (but never quite black)
	m_flEnvLightScale = Clamp( flNow / Max( flBase, 0.001f ), 0.03f, 1.2f );
}

float WeatherPrecip_GetParticleLightScale( const Vector &vecPos )
{
	const C_WeatherSystem *pSys = GetWeatherSystem();
	if ( !pSys->IsActive() )
		return 1.0f;
	const float flScale = pSys->GetParticleLightScale();
	if ( fabsf( flScale - 1.0f ) < 0.01f )
		return 1.0f;
	// indoors the lightmaps are right (lamps)
	if ( !WeatherPrecip_IsOpenSky( vecPos + Vector( 0, 0, 16 ) ) )
		return 1.0f;
	return flScale;
}

// ---------------------------------------------------------------------------
// Wind
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateWind( float dt )
{
	CEnvWeather *pWeather = GetWeatherEntity();
	const float flTargetYaw = pWeather ? pWeather->GetWindYaw() : 45.0f;
	m_flWindYaw = ApproachAngle( flTargetYaw, m_flWindYaw, dt * 5.0f );

	float s, c;
	SinCos( DEG2RAD( m_flWindYaw ), &s, &c );
	m_vecWindDir.Init( c, s );

	// gusts: slow noise on top of the base wind
	const float t = gpGlobals->curtime;
	m_flGust = 0.5f + 0.5f * sinf( t * 0.37f ) * sinf( t * 0.11f + 1.3f );

	const float flCloudSpeed = m_Current.flCloudSpeed * ( 0.85f + 0.3f * m_flGust ) * r_weather_cloud_wind_scale.GetFloat();
	m_vecCloudOffset += m_vecWindDir * ( flCloudSpeed * dt );
	// keep the offset bounded; all cloud noise tiles with this period
	const float flPeriod = 160000.0f;
	m_vecCloudOffset.x = fmodf( m_vecCloudOffset.x, flPeriod );
	m_vecCloudOffset.y = fmodf( m_vecCloudOffset.y, flPeriod );

	// clouds slowly change shape
	m_flCloudTime += dt * ( 0.5f + m_Current.flWind );
}

// ---------------------------------------------------------------------------
// Wetness: surfaces get wet while it rains and dry out slowly afterwards.
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateWetness( float dt )
{
	const float flRain = m_Current.flRain;
	if ( flRain > 0.02f )
	{
		m_flWetness = Min( 1.0f, m_flWetness + dt * ( 0.015f + 0.06f * flRain ) );
		if ( m_flWetness > 0.5f )
			m_flPuddles = Min( flRain, m_flPuddles + dt * 0.02f * flRain );
	}
	else
	{
		// sunshine dries faster than a grey day
		const float flDry = 0.0025f + 0.006f * Clamp( m_Current.flSunIntensity, 0.0f, 1.0f );
		m_flWetness = Max( 0.0f, m_flWetness - dt * flDry );
		m_flPuddles = Max( 0.0f, m_flPuddles - dt * flDry * 0.5f );
	}

	// damp air (fog, mist) keeps surfaces slightly wet
	m_flWetness = Max( m_flWetness, Min( m_Current.flWetness, 0.3f ) * ( 1.0f - m_Current.flRain ) );
	m_flPuddles = Min( m_flPuddles, m_flWetness );
}

// ---------------------------------------------------------------------------
// Lightning & thunder
// ---------------------------------------------------------------------------
void C_WeatherSystem::TriggerLightning( int iForceClass )
{
	// how far: most strikes are distant, a few are right overhead
	int iClass = iForceClass;
	if ( iClass < 0 || iClass >= STRIKE_CLASS_COUNT )
	{
		const float r = RandomFloat( 0.0f, 1.0f );
		iClass = ( r < 0.2f ) ? STRIKE_CLOSE : ( ( r < 0.6f ) ? STRIKE_MID : STRIKE_FAR );
	}
	const bool bForced = ( iForceClass >= 0 );
	if ( m_bFirstStrike && !bForced )
		iClass = STRIKE_MID;

	static const float s_flDistMin[STRIKE_CLASS_COUNT] = { 250.0f, 1200.0f, 4000.0f };
	static const float s_flDistMax[STRIKE_CLASS_COUNT] = { 1200.0f, 4000.0f, 12000.0f };
	const float flDist = RandomFloat( s_flDistMin[iClass], s_flDistMax[iClass] );	// meters
	const float flCloseness = 1.0f - clamp( ( flDist - 250.0f ) / 11750.0f, 0.0f, 1.0f );

	m_iStrikeClass = iClass;
	m_flStrikeDistance = flDist;
	m_flStrikeTime = gpGlobals->curtime;
	m_iStrikeSeed = RandomInt( 1, 0x7fffffff );
	// cloud-to-ground strikes show their channel, the rest light the clouds from inside
	m_bStrikeChannel = ( iClass == STRIKE_CLOSE ) ? RandomFloat() < 0.85f : ( iClass == STRIKE_MID ? RandomFloat() < 0.6f : RandomFloat() < 0.25f );
	if ( bForced )
		m_bStrikeChannel = true;	// cl_weather_lightning, map inputs: show it
	m_flStrikeCloudBase = clamp( m_Current.flCloudBase, 400.0f, 3000.0f );

	// rain and haze swallow distant strikes
	const float flVisibilityM = Lerp( clamp( m_Current.flRain, 0.0f, 1.0f ), 14000.0f, 5000.0f ) / Max( 1.0f, m_Current.flHaze * 0.5f );
	m_flStrikeFade = expf( -flDist / flVisibilityM );

	// direction: anywhere around, the glow shows low in the sky
	const float flYaw = ( cl_weather_lightning_yaw.GetFloat() >= 0.0f ) ? DEG2RAD( cl_weather_lightning_yaw.GetFloat() ) : RandomFloat( 0.0f, 2.0f * M_PI_F );
	const float flGlowElev = atanf( m_flStrikeCloudBase / flDist ) * 0.7f + DEG2RAD( 3.0f );
	m_vecFlashDir.Init( cosf( flYaw ) * cosf( flGlowElev ), sinf( flYaw ) * cosf( flGlowElev ), sinf( flGlowElev ) );
	// the scene is lit from the middle of the channel
	// (not lower than 30 degrees: at grazing angles the bolt's split-second shadows were
	// endless streaks and self-shadowing acne - read as a lighting glitch)
	const float flLightElev = clamp( atanf( 0.5f * m_flStrikeCloudBase / flDist ), DEG2RAD( 30.0f ), DEG2RAD( 60.0f ) );
	m_vecBoltLightDir.Init( cosf( flYaw ) * cosf( flLightElev ), sinf( flYaw ) * cosf( flLightElev ), sinf( flLightElev ) );

	// a strike is a few return strokes 40-120 ms apart along the same channel
	m_nPulses = RandomInt( 2, 5 );
	float flStart = gpGlobals->curtime;
	for ( int i = 0; i < m_nPulses; i++ )
	{
		m_Pulses[i].flStart = flStart;
		m_Pulses[i].flDecay = RandomFloat( 0.035f, 0.09f );
		m_Pulses[i].flAmp = ( i == 0 ? 1.0f : RandomFloat( 0.45f, 0.9f ) ) * ( 0.3f + 0.7f * flCloseness ) * ( 0.35f + 0.65f * m_flStrikeFade );
		flStart += RandomFloat( 0.04f, 0.12f );
	}

	// thunder: speed of sound, but nobody waits half a minute for it
	Thunder_t thunder;
	thunder.flTime = gpGlobals->curtime + Min( flDist / 343.0f, 14.0f );
	static const float s_flVolume[STRIKE_CLASS_COUNT] = { 1.0f, 0.75f, 0.5f };
	thunder.flVolume = s_flVolume[iClass] * RandomFloat( 0.85f, 1.0f );
	thunder.iClass = iClass;
	thunder.bFirst = m_bFirstStrike && !bForced;
	m_Thunder.AddToTail( thunder );

	if ( cl_weather_debug.GetBool() )
	{
		Msg( "[lightning] class %d dist %.0f m channel %d fade %.2f cloudbase %.0f pulses %d first %d yaw %.0f\n",
			iClass, flDist, m_bStrikeChannel ? 1 : 0, m_flStrikeFade, m_flStrikeCloudBase, m_nPulses, m_bFirstStrike ? 1 : 0, RAD2DEG( flYaw ) );
	}

	if ( !bForced )
		m_bFirstStrike = false;
}

void C_WeatherSystem::ForceLightning( int iClass )
{
	TriggerLightning( clamp( iClass, 0, STRIKE_CLASS_COUNT - 1 ) );
}

// brightness of the current strike at a time (sum of the return strokes);
// the channel keeps a dim afterglow between strokes
float C_WeatherSystem::GetStrikeEnvelope( float flTime, bool bChannel ) const
{
	float flSum = 0.0f;
	for ( int i = 0; i < m_nPulses; i++ )
	{
		const float t = flTime - m_Pulses[i].flStart;
		if ( t < 0.0f || t > 1.5f )
			continue;
		const float flRise = Min( t / 0.008f, 1.0f );
		float e = expf( -t / m_Pulses[i].flDecay );
		if ( bChannel )
		{
			// the eye holds the channel much longer than the light lasts
			e = expf( -t / Max( m_Pulses[i].flDecay * 2.0f, 0.1f ) ) * 0.75f + expf( -t / 0.3f ) * 0.25f;
		}
		flSum += m_Pulses[i].flAmp * flRise * e;
	}
	return Min( flSum, 1.5f );
}

void C_WeatherSystem::UpdateLightning( float dt )
{
	const float flRate = m_Current.flLightning;	// strikes per minute
	const bool bLightning = flRate > 0.05f;
	if ( bLightning && !m_bLightningWasActive )
	{
		// a storm arrives: the first thunder comes a little later and rolls long
		m_bFirstStrike = true;
		m_flNextLightning = Max( m_flNextLightning, gpGlobals->curtime + RandomFloat( 6.0f, 15.0f ) );
	}
	m_bLightningWasActive = bLightning;

	if ( bLightning && dt > 0.0f )
	{
		if ( gpGlobals->curtime >= m_flNextLightning )
		{
			TriggerLightning( -1 );
			const float flMean = 60.0f / flRate;
			m_flNextLightning = gpGlobals->curtime + Max( 4.0f, -logf( RandomFloat( 0.01f, 1.0f ) ) * flMean );
		}
	}
	else if ( !bLightning )
	{
		m_flNextLightning = Max( m_flNextLightning, gpGlobals->curtime + 5.0f );
	}

	// flash envelope
	const float flEnv = GetStrikeEnvelope( gpGlobals->curtime, false );
	m_flFlash = flEnv;
	// the channel lights the scene directly only when it is close enough
	static const float s_flDirectional[STRIKE_CLASS_COUNT] = { 1.0f, 0.35f, 0.0f };
	m_flFlashLight = ( m_bStrikeChannel && cl_weather_lightning_light.GetBool() ) ? flEnv * s_flDirectional[m_iStrikeClass] : 0.0f;

	// thunder arrives later (speed of sound)
	for ( int i = m_Thunder.Count() - 1; i >= 0; i-- )
	{
		if ( gpGlobals->curtime < m_Thunder[i].flTime )
			continue;

		// muffled indoors
		const float flInside = 0.55f + 0.45f * m_flSkyExposure;
		const float flVol = clamp( m_Thunder[i].flVolume * flInside * cl_weather_volume.GetFloat(), 0.0f, 1.0f );
		if ( flVol > 0.01f )
		{
			const int iSet = clamp( m_Thunder[i].iClass, 0, STRIKE_CLASS_COUNT - 1 );
			const char *pszSound = m_Thunder[i].bFirst ? THUNDER_FIRST_OF_STORM
				: s_ppszThunderSets[iSet][ RandomInt( 0, s_nThunderSets[iSet] - 1 ) ];
			enginesound->EmitAmbientSound( pszSound, flVol, RandomInt( 92, 104 ) );
		}
		m_Thunder.Remove( i );
	}
}

void C_WeatherSystem::ApplyLightningFlash( lightData_Global_t &data, bool bCanShadow ) const
{
	if ( !m_bActive || m_flFlash <= 0.001f )
		return;

	const Vector vecFlash( 0.80f, 0.86f, 1.0f );

	// the whole sky lights up: sky light
	const float flSkyFlash = Max( m_flBaseSunLum * 0.35f, m_flBaseSkyLum * 3.0f ) * m_flFlash;
	Vector ambH = data.ambh.AsVector3D() + vecFlash * ( flSkyFlash * 0.55f );
	Vector ambL = data.ambl.AsVector3D() + vecFlash * ( flSkyFlash * 0.35f );
	data.ambh.Init( ambH.x, ambH.y, ambH.z, data.ambh.w );
	data.ambl.Init( ambL.x, ambL.y, ambL.z, data.ambl.w );

	// a close channel is a light source of its own: sharp shadows for a split second.
	// Takes over the global light while it is much brighter than the storm's sun / moon.
	if ( m_flFlashLight > 0.02f )
	{
		const float flBolt = m_flBaseSunLum * 1.6f * m_flFlashLight;
		const Vector vecCur = data.diff.AsVector3D();
		if ( flBolt > Luminance( vecCur ) * 1.5f )
		{
			const Vector diff = vecFlash * flBolt;
			data.diff.Init( diff.x, diff.y, diff.z, data.diff.w );
			data.vecLight.Init( m_vecBoltLightDir.x, m_vecBoltLightDir.y, m_vecBoltLightDir.z, 0.0f );
			data.bShadow = bCanShadow;
		}
		else
		{
			const Vector diff = vecCur + vecFlash * flBolt;
			data.diff.Init( diff.x, diff.y, diff.z, data.diff.w );
		}
	}
}

bool C_WeatherSystem::GetLightningBolt( LightningBoltInfo_t &info ) const
{
	if ( !m_bActive || !m_bStrikeChannel || !cl_weather_lightning_bolts.GetBool() )
		return false;

	const float flAge = gpGlobals->curtime - m_flStrikeTime;
	if ( flAge < 0.0f || flAge > 1.5f )
		return false;

	const float flBright = GetStrikeEnvelope( gpGlobals->curtime, true );
	if ( flBright < 0.01f )
		return false;

	info.vecDir.Init( m_vecFlashDir.x, m_vecFlashDir.y, 0.0f );
	VectorNormalize( info.vecDir );
	info.flDistance = m_flStrikeDistance;
	info.flCloudBase = m_flStrikeCloudBase;
	info.iSeed = m_iStrikeSeed;
	info.flBrightness = flBright;
	info.flVisibility = m_flStrikeFade;
	return true;
}

CON_COMMAND( cl_weather_lightning, "Trigger a lightning strike: cl_weather_lightning [0 close | 1 mid | 2 far]" )
{
	GetWeatherSystem()->ForceLightning( args.ArgC() > 1 ? atoi( args[1] ) : 0 );
}

// ---------------------------------------------------------------------------
// How much of the sky is open around the camera (rain audio, precipitation)
// ---------------------------------------------------------------------------

// Open sky above a point near the camera. The point is pulled back in front of
// walls so it stays in the camera's space (a wall doesn't make a room outdoors).
static bool IsOpenSkyNear( const Vector &vecEye, float flOffsetX, float flOffsetY )
{
	Vector vecTarget = vecEye + Vector( flOffsetX, flOffsetY, 0.0f );
	const float flLen = sqrtf( flOffsetX * flOffsetX + flOffsetY * flOffsetY );
	if ( flLen > 1.0f )
	{
		trace_t tr;
		CTraceFilterWorldOnly filter;
		UTIL_TraceLine( vecEye, vecTarget, MASK_SOLID_BRUSHONLY, &filter, &tr );
		if ( tr.fraction < 1.0f )
			vecTarget = vecEye + ( vecTarget - vecEye ) * Max( 0.0f, tr.fraction - 16.0f / flLen );
	}
	return WeatherPrecip_TraceOpenSky( vecTarget );
}

void C_WeatherSystem::UpdateSkyExposure( float dt )
{
	if ( gpGlobals->curtime < m_flNextExposureTrace )
		return;
	m_flNextExposureTrace = gpGlobals->curtime + 0.25f;

	const Vector vecEye = MainViewOrigin();

	// around the camera
	static const Vector2D s_Inner[] = { Vector2D( 0, 0 ), Vector2D( 120, 0 ), Vector2D( -120, 0 ), Vector2D( 0, 120 ), Vector2D( 0, -120 ) };
	int nOpen = 0;
	for ( int i = 0; i < ARRAYSIZE( s_Inner ); i++ )
	{
		if ( IsOpenSkyNear( vecEye, s_Inner[i].x, s_Inner[i].y ) )
			nOpen++;
	}
	m_flSkyExposure = Approach( nOpen / (float)ARRAYSIZE( s_Inner ), m_flSkyExposure, 0.5f );

	// a ring further out: rain seen through doors, windows, from under a roof
	const int nRing = 8;
	int nOpenRing = 0;
	for ( int i = 0; i < nRing; i++ )
	{
		float s, c;
		SinCos( i * ( 2.0f * M_PI_F / nRing ), &s, &c );
		if ( IsOpenSkyNear( vecEye, c * 300.0f, s * 300.0f ) )
			nOpenRing++;
	}
	m_flOuterExposure = Approach( nOpenRing / (float)nRing, m_flOuterExposure, 0.5f );
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static void UpdateLoop( const char *pszSound, float flVolume, bool &bPlaying, float &flLastVolume )
{
	flVolume = clamp( flVolume, 0.0f, 1.0f );
	if ( flVolume < 0.01f )
	{
		if ( bPlaying )
		{
			enginesound->EmitAmbientSound( pszSound, 0.0f, PITCH_NORM, SND_STOP );
			bPlaying = false;
		}
		flLastVolume = 0.0f;
		return;
	}

	if ( !bPlaying )
	{
		enginesound->EmitAmbientSound( pszSound, flVolume, PITCH_NORM );
		bPlaying = true;
		flLastVolume = flVolume;
		return;
	}

	if ( fabsf( flVolume - flLastVolume ) > 0.02f )
	{
		enginesound->EmitAmbientSound( pszSound, flVolume, PITCH_NORM, SND_CHANGE_VOL );
		flLastVolume = flVolume;
	}
}

void C_WeatherSystem::UpdateAudio( float dt )
{
	const float flMaster = cl_weather_volume.GetFloat();
	const float flRain = powf( Clamp( m_Current.flRain, 0.0f, 1.0f ), 0.6f );

	float flOut = flRain * ( 0.15f + 0.85f * m_flSkyExposure ) * flMaster;
	float flIn = flRain * ( 1.0f - m_flSkyExposure ) * 0.8f * flMaster;
	float flWind = Clamp( m_Current.flWind, 0.0f, 1.0f ) * ( 0.25f + 0.75f * m_flSkyExposure ) * ( 0.6f + 0.4f * m_flGust ) * 0.55f * flMaster;

	if ( engine->IsPaused() )
		flOut = flIn = flWind = 0.0f;

	UpdateLoop( WEATHER_SOUND_RAIN_OUT, flOut, m_bRainOutPlaying, m_flRainVolumeOut );
	UpdateLoop( WEATHER_SOUND_RAIN_IN, flIn, m_bRainInPlaying, m_flRainVolumeIn );
	UpdateLoop( WEATHER_SOUND_WIND, flWind, m_bWindPlaying, m_flWindVolume );
}

void C_WeatherSystem::StopAudio()
{
	float flDummy = 0.0f;
	UpdateLoop( WEATHER_SOUND_RAIN_OUT, 0.0f, m_bRainOutPlaying, flDummy );
	UpdateLoop( WEATHER_SOUND_RAIN_IN, 0.0f, m_bRainInPlaying, flDummy );
	UpdateLoop( WEATHER_SOUND_WIND, 0.0f, m_bWindPlaying, flDummy );
}

// ---------------------------------------------------------------------------
// Particle precipitation around the camera
// ---------------------------------------------------------------------------
Vector2D C_WeatherSystem::GetRainSlant() const
{
	// drops fall at ~9 m/s, the wind pushes them sideways
	const float flSlant = Clamp( m_Current.flWind, 0.0f, 1.0f ) * ( 0.15f + 0.35f * m_flGust );
	return m_vecWindDir * flSlant;
}

void C_WeatherSystem::UpdatePrecipitation()
{
	WeatherPrecipInput_t in;
	in.flRain = m_Current.flRain;
	in.flSnow = m_Current.flSnow;
	in.flAsh = m_Current.flAsh;
	in.flInnerExposure = m_flSkyExposure;
	in.flOuterExposure = Max( m_flOuterExposure, m_flSkyExposure );
	in.vecSlant = GetRainSlant();
	// snow and ash drift much more than they fall
	if ( in.flSnow + in.flAsh > in.flRain )
		in.vecSlant *= 2.0f;

	m_Precip.Update( true, in );
}

// ---------------------------------------------------------------------------
// Render data
// ---------------------------------------------------------------------------
void C_WeatherSystem::FillRenderData( weatherData_t &data, const CViewSetup &view ) const
{
	const WeatherParams_t &p = m_Current;

	data.bEnabled = m_bActive;

	data.vecCloudParams0.Init( p.flCloudCoverage, p.flCloudDensity, p.flCloudType, p.flCloudDarkness );
	data.vecCloudParams1.Init( p.flCloudBase, p.flCloudThickness, p.flCirrus, m_flCloudTime );
	// altocumulus sit well above the cumulus tops
	// z: meters of cloud space per world unit (1 unit = 1 inch, times the parallax scale)
	data.vecCloudParams3.Init( p.flAltocumulus, Max( 4800.0f, p.flCloudBase + p.flCloudThickness + 1200.0f ),
		0.0254f * r_weather_cloud_parallax.GetFloat(), (float)r_weather_cloud_debug.GetInt() );

	const Vector2D vecSlant = GetRainSlant();
	data.vecWind.Init( m_vecCloudOffset.x, m_vecCloudOffset.y, vecSlant.x, vecSlant.y );

	data.vecAtmoParams.Init( p.flHaze, p.flSkyDesaturation, p.flSkyBrightness, m_flFlash );
	data.vecFogParams.Init( p.flFogDensity, p.flFogHeightFalloff, view.origin.z - 96.0f, p.flVolumetricDensity );
	data.vecRainParams.Init( p.flRain, m_flWetness, m_flPuddles, gpGlobals->curtime );

	data.vecMoonDir.Init( m_vecMoonDir.x, m_vecMoonDir.y, m_vecMoonDir.z, m_flNightFactor );
	data.vecSunDir.Init( m_vecSunDir.x, m_vecSunDir.y, m_vecSunDir.z, m_flSunAltitude );
	data.vecLightning.Init( m_vecFlashDir.x, m_vecFlashDir.y, m_vecFlashDir.z, m_flFlash );

	data.vecCameraParams.Init( view.origin.x, view.origin.y, view.origin.z, 0.0f );
}

// ---------------------------------------------------------------------------
// Debug commands
// ---------------------------------------------------------------------------
CON_COMMAND( cl_weather_state, "Print the current weather parameters" )
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	const WeatherParams_t &p = pSys->GetParams();
	Msg( "Weather %s (blend %.2f, active %d)\n", GetWeatherPresetInfo( pSys->GetTargetPreset() ).pszName, pSys->GetTransitionProgress(), pSys->IsActive() );
	Msg( "  clouds: coverage %.2f density %.2f type %.2f base %.0f thick %.0f dark %.2f cirrus %.2f speed %.1f\n",
		p.flCloudCoverage, p.flCloudDensity, p.flCloudType, p.flCloudBase, p.flCloudThickness, p.flCloudDarkness, p.flCirrus, p.flCloudSpeed );
	Msg( "  sky: haze %.2f desat %.2f bright %.2f | light: sun %.2f amb %.2f desat %.2f\n",
		p.flHaze, p.flSkyDesaturation, p.flSkyBrightness, p.flSunIntensity, p.flAmbientIntensity, p.flAmbientDesaturation );
	Msg( "  fog: density %.3f falloff %.2f skycol %.2f volumetric %.2f\n", p.flFogDensity, p.flFogHeightFalloff, p.flFogSkyColor, p.flVolumetricDensity );
	Msg( "  rain %.2f wet %.2f (target %.2f) puddles %.2f lightning %.2f/min wind %.2f exposure %.2f\n",
		p.flRain, pSys->GetWetness(), p.flWetness, pSys->GetPuddles(), p.flLightning, p.flWind, pSys->GetSkyExposure() );
}

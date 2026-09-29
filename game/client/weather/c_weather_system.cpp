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
#include "particles_simple.h"
#include "view_shared.h"

#include "tier0/memdbgon.h"

static C_WeatherSystem g_WeatherSystem;
C_WeatherSystem *GetWeatherSystem()
{
	return &g_WeatherSystem;
}

static ConVar cl_weather( "cl_weather", "1", FCVAR_ARCHIVE, "Enable dynamic weather visuals" );
static ConVar cl_weather_volume( "cl_weather_volume", "1.0", FCVAR_ARCHIVE, "Rain / wind / thunder volume" );
static ConVar cl_weather_splashes( "cl_weather_splashes", "1", FCVAR_ARCHIVE, "Rain splashes on the ground" );
static ConVar cl_weather_debug( "cl_weather_debug", "0", 0, "Show weather state on screen" );
static ConVar cl_weather_moonlight( "cl_weather_moonlight", "0.045", FCVAR_ARCHIVE, "Moonlight strength relative to the map's sunlight" );
static ConVar cl_weather_night_ambient( "cl_weather_night_ambient", "0.06", FCVAR_ARCHIVE, "Night sky light relative to the map's day ambient" );

#define WEATHER_SOUND_RAIN_OUT	"hl2rpm/weather/rain_loop.wav"
#define WEATHER_SOUND_RAIN_IN	"hl2rpm/weather/rain_roof_loop.wav"
#define WEATHER_SOUND_WIND		"hl2rpm/weather/wind_loop.wav"

static const char *s_pszThunderClose[] =
{
	"ambient/atmosphere/thunder1.wav",
	"ambient/atmosphere/thunder2.wav",
	"ambient/atmosphere/thunder3.wav",
	"ambient/atmosphere/thunder4.wav",
};
static const char *s_pszThunderFar[] =
{
	"ambient/outro/thunder01.wav",
	"ambient/outro/thunder02.wav",
	"ambient/outro/thunder03.wav",
	"ambient/outro/thunder04.wav",
	"ambient/outro/thunder05.wav",
	"ambient/outro/thunder06.wav",
	"ambient/outro/thunder07.wav",
};

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
	m_vecFlashDir.Init( 1, 0, 0.3f );
	m_nPulses = 0;

	m_flSkyExposure = 1.0f;
	m_flNextExposureTrace = 0.0f;
	m_flRainVolumeOut = m_flRainVolumeIn = m_flWindVolume = 0.0f;
	m_bRainOutPlaying = m_bRainInPlaying = m_bWindPlaying = false;
	m_flSplashAccum = 0.0f;
	m_hSplashMaterial = INVALID_MATERIAL_HANDLE;
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
	for ( int i = 0; i < ARRAYSIZE( s_pszThunderClose ); i++ )
		enginesound->PrecacheSound( s_pszThunderClose[i], true );
	for ( int i = 0; i < ARRAYSIZE( s_pszThunderFar ); i++ )
		enginesound->PrecacheSound( s_pszThunderFar[i], true );
}

void C_WeatherSystem::LevelInitPostEntity()
{
	m_iLastSerial = -1;
	m_flWetness = 0.0f;
	m_flPuddles = 0.0f;
	m_Thunder.RemoveAll();
	m_nPulses = 0;
	m_flFlash = 0.0f;
	m_flNextLightning = gpGlobals->curtime + 10.0f;
	// the engine stopped every sound on level change
	m_bRainOutPlaying = m_bRainInPlaying = m_bWindPlaying = false;
	m_flRainVolumeOut = m_flRainVolumeIn = m_flWindVolume = 0.0f;
}

void C_WeatherSystem::LevelShutdownPreEntity()
{
	StopAudio();
	m_bActive = false;
	m_pSplashEmitter = NULL;
}

void C_WeatherSystem::Shutdown()
{
	m_pSplashEmitter = NULL;
}

void C_WeatherSystem::Update( float frametime )
{
	CEnvWeather *pWeather = GetWeatherEntity();
	const bool bWasActive = m_bActive;
	m_bActive = pWeather != NULL && cl_weather.GetBool() && GetDeferredManager()->IsDeferredRenderingEnabled();

	if ( !m_bActive )
	{
		if ( bWasActive )
			StopAudio();
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
	UpdateSplashes( dt );

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

	// --- Lightning lights up everything for a split second
	if ( m_flFlash > 0.001f )
	{
		const Vector vecFlash( 0.80f, 0.86f, 1.0f );
		const float flFlashLum = Max( flBaseLum, Luminance( vecBaseAmbHigh ) * 4.0f ) * m_flFlash;
		ambH += vecFlash * ( flFlashLum * 0.55f );
		ambL += vecFlash * ( flFlashLum * 0.35f );
	}

	data.diff.Init( diff.x, diff.y, diff.z, data.diff.w );
	data.ambh.Init( ambH.x, ambH.y, ambH.z, data.ambh.w );
	data.ambl.Init( ambL.x, ambL.y, ambL.z, data.ambl.w );
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

	const float flCloudSpeed = m_Current.flCloudSpeed * ( 0.85f + 0.3f * m_flGust );
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
void C_WeatherSystem::TriggerLightning( bool bForceClose )
{
	// direction: somewhere in the sky, mostly low
	const float flYaw = RandomFloat( 0.0f, 2.0f * M_PI_F );
	const float flPitch = DEG2RAD( RandomFloat( 5.0f, 40.0f ) );
	m_vecFlashDir.Init( cosf( flYaw ) * cosf( flPitch ), sinf( flYaw ) * cosf( flPitch ), sinf( flPitch ) );

	const float flDist = bForceClose ? RandomFloat( 300.0f, 900.0f ) : RandomFloat( 500.0f, 9000.0f );	// meters
	const float flCloseness = 1.0f - clamp( flDist / 9000.0f, 0.0f, 1.0f );

	// a strike is several quick return strokes
	m_nPulses = RandomInt( 1, 4 );
	float flStart = gpGlobals->curtime;
	for ( int i = 0; i < m_nPulses; i++ )
	{
		m_Pulses[i].flStart = flStart;
		m_Pulses[i].flDecay = RandomFloat( 0.04f, 0.12f );
		m_Pulses[i].flAmp = RandomFloat( 0.5f, 1.0f ) * ( 0.35f + 0.65f * flCloseness ) * ( i == 0 ? 1.0f : 0.7f );
		flStart += RandomFloat( 0.06f, 0.22f );
	}

	Thunder_t thunder;
	thunder.flTime = gpGlobals->curtime + flDist / 343.0f;
	thunder.flVolume = clamp( 0.35f + 0.75f * flCloseness, 0.2f, 1.0f );
	thunder.bClose = flDist < 2000.0f;
	m_Thunder.AddToTail( thunder );
}

void C_WeatherSystem::ForceLightning()
{
	TriggerLightning( true );
}

void C_WeatherSystem::UpdateLightning( float dt )
{
	const float flRate = m_Current.flLightning;	// strikes per minute
	if ( flRate > 0.05f && dt > 0.0f )
	{
		if ( gpGlobals->curtime >= m_flNextLightning )
		{
			TriggerLightning( false );
			const float flMean = 60.0f / flRate;
			m_flNextLightning = gpGlobals->curtime + Max( 2.0f, -logf( RandomFloat( 0.01f, 1.0f ) ) * flMean );
		}
	}
	else
	{
		m_flNextLightning = Max( m_flNextLightning, gpGlobals->curtime + 5.0f );
	}

	// flash envelope
	m_flFlash = 0.0f;
	for ( int i = 0; i < m_nPulses; i++ )
	{
		const float t = gpGlobals->curtime - m_Pulses[i].flStart;
		if ( t >= 0.0f && t < 1.0f )
			m_flFlash += m_Pulses[i].flAmp * expf( -t / m_Pulses[i].flDecay );
	}
	m_flFlash = Min( m_flFlash, 1.5f );

	// thunder arrives later (speed of sound)
	for ( int i = m_Thunder.Count() - 1; i >= 0; i-- )
	{
		if ( gpGlobals->curtime < m_Thunder[i].flTime )
			continue;

		const float flVol = clamp( m_Thunder[i].flVolume * cl_weather_volume.GetFloat(), 0.0f, 1.0f );
		if ( flVol > 0.01f )
		{
			const char *pszSound = m_Thunder[i].bClose
				? s_pszThunderClose[ RandomInt( 0, ARRAYSIZE( s_pszThunderClose ) - 1 ) ]
				: s_pszThunderFar[ RandomInt( 0, ARRAYSIZE( s_pszThunderFar ) - 1 ) ];
			enginesound->EmitAmbientSound( pszSound, flVol, RandomInt( 90, 105 ) );
		}
		m_Thunder.Remove( i );
	}
}

CON_COMMAND( cl_weather_lightning, "Trigger a close lightning strike" )
{
	GetWeatherSystem()->ForceLightning();
}

// ---------------------------------------------------------------------------
// How much the player is under the open sky (rain audio, splashes)
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateSkyExposure( float dt )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	if ( gpGlobals->curtime >= m_flNextExposureTrace )
	{
		m_flNextExposureTrace = gpGlobals->curtime + 0.25f;

		static const Vector2D s_Offsets[] = { Vector2D( 0, 0 ), Vector2D( 120, 0 ), Vector2D( -120, 0 ), Vector2D( 0, 120 ), Vector2D( 0, -120 ) };
		const Vector vecEye = pPlayer->EyePosition();
		int nOpen = 0;
		for ( int i = 0; i < ARRAYSIZE( s_Offsets ); i++ )
		{
			Vector vecStart = vecEye + Vector( s_Offsets[i].x, s_Offsets[i].y, 0.0f );
			trace_t tr;
			UTIL_TraceLine( vecStart, vecStart + Vector( 0, 0, 8192 ), MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 1.0f || ( tr.surface.flags & SURF_SKY ) )
				nOpen++;
		}
		m_flSkyExposure = Approach( nOpen / (float)ARRAYSIZE( s_Offsets ), m_flSkyExposure, 0.5f );
	}
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
// Splashes where drops hit the ground around the player
// ---------------------------------------------------------------------------
void C_WeatherSystem::UpdateSplashes( float dt )
{
	if ( !cl_weather_splashes.GetBool() || m_Current.flRain < 0.05f || dt <= 0.0f )
		return;

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer )
		return;

	// the emitter belongs to the particle manager: never keep it past the level
	// (a static smart pointer released it after the particle system shut down -> crash on quit)
	if ( !m_pSplashEmitter.IsValid() )
	{
		m_pSplashEmitter = CSimpleEmitter::Create( "WeatherSplashes" );
		if ( !m_pSplashEmitter.IsValid() )
			return;
		m_hSplashMaterial = m_pSplashEmitter->GetPMaterial( "effects/splash2" );
	}
	CSimpleEmitter *s_pEmitter = m_pSplashEmitter.GetObject();
	const PMaterialHandle s_hSplash = m_hSplashMaterial;

	const Vector vecEye = pPlayer->EyePosition();
	s_pEmitter->SetSortOrigin( vecEye );

	m_flSplashAccum += dt * 45.0f * m_Current.flRain;
	int nSplashes = (int)m_flSplashAccum;
	m_flSplashAccum -= nSplashes;
	nSplashes = Min( nSplashes, 6 );

	Vector vecFwd;
	pPlayer->EyeVectors( &vecFwd );

	for ( int i = 0; i < nSplashes; i++ )
	{
		// mostly in front of the player where they can be seen
		const float flAng = atan2f( vecFwd.y, vecFwd.x ) + RandomFloat( -1.3f, 1.3f );
		const float flDist = RandomFloat( 40.0f, 700.0f );
		Vector vecTop( vecEye.x + cosf( flAng ) * flDist, vecEye.y + sinf( flAng ) * flDist, vecEye.z + 512.0f );

		trace_t tr;
		UTIL_TraceLine( vecTop, vecTop - Vector( 0, 0, 1536 ), MASK_SOLID | MASK_WATER, pPlayer, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal.z < 0.5f )
			continue;

		// only where the drop could actually have come from the sky
		trace_t trUp;
		UTIL_TraceLine( tr.endpos + Vector( 0, 0, 4 ), tr.endpos + Vector( 0, 0, 8192 ), MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &trUp );
		if ( trUp.fraction < 1.0f && !( trUp.surface.flags & SURF_SKY ) )
			continue;

		const int nParticles = RandomInt( 2, 3 );
		for ( int j = 0; j < nParticles; j++ )
		{
			SimpleParticle *pParticle = (SimpleParticle *)s_pEmitter->AddParticle( sizeof( SimpleParticle ), s_hSplash, tr.endpos + Vector( 0, 0, 1 ) );
			if ( !pParticle )
				break;

			pParticle->m_flLifetime = 0.0f;
			pParticle->m_flDieTime = RandomFloat( 0.12f, 0.25f );
			pParticle->m_vecVelocity.Init( RandomFloat( -12, 12 ), RandomFloat( -12, 12 ), RandomFloat( 25, 60 ) );
			pParticle->m_uchColor[0] = pParticle->m_uchColor[1] = pParticle->m_uchColor[2] = 190;
			pParticle->m_uchStartAlpha = 110;
			pParticle->m_uchEndAlpha = 0;
			pParticle->m_uchStartSize = RandomInt( 1, 3 );
			pParticle->m_uchEndSize = pParticle->m_uchStartSize + RandomInt( 3, 6 );
			pParticle->m_flRoll = RandomFloat( 0, 360 );
			pParticle->m_flRollDelta = 0.0f;
		}
	}
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

	// rain falls at ~9 m/s, the wind pushes it sideways
	const float flSlant = Clamp( p.flWind, 0.0f, 1.0f ) * ( 0.15f + 0.35f * m_flGust );
	data.vecWind.Init( m_vecCloudOffset.x, m_vecCloudOffset.y, m_vecWindDir.x * flSlant, m_vecWindDir.y * flSlant );

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

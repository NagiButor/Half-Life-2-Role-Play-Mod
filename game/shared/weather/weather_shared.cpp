//========= HL2RPM ============================================================//
//
// Purpose: Dynamic weather - preset table and transition logic.
//
//=============================================================================//

#include "cbase.h"
#include "weather/weather_shared.h"

#include "tier0/memdbgon.h"

// ---------------------------------------------------------------------------
// Preset table.
// Looks are modeled after the Half-Life 2 skyboxes (cold, desaturated, heavy
// layered clouds with warm light breaking through near the horizon) with the
// variety and transitions of RDR2's weather.
// ---------------------------------------------------------------------------
static const WeatherPresetInfo_t s_WeatherPresets[WEATHER_PRESET_COUNT] =
{
	//  name           display token                     clouds: coverage density type base   thick  dark  cirrus speed | sky: haze desat bright | light: sun  amb  ambDesat | fog: density falloff skyCol vol | rain wet  lightning wind | hours
	{ "clear",         "#HL2RPM_Weather_Clear",         { 0.06f, 1.00f, 0.70f, 1600.0f, 1300.0f, 0.00f, 0.55f,  6.0f,   1.0f, 0.00f, 1.00f,   1.00f, 1.00f, 0.00f,   0.00f, 0.60f, 0.90f, 0.25f,   0.00f, 0.00f, 0.0f, 0.15f }, 3.0f, 8.0f },
	{ "fair",          "#HL2RPM_Weather_Fair",          { 0.30f, 1.00f, 0.85f, 1500.0f, 1700.0f, 0.00f, 0.35f,  8.0f,   1.3f, 0.05f, 1.00f,   0.95f, 1.00f, 0.05f,   0.02f, 0.60f, 0.90f, 0.35f,   0.00f, 0.00f, 0.0f, 0.25f }, 2.0f, 6.0f },
	{ "partlycloudy",  "#HL2RPM_Weather_PartlyCloudy",  { 0.55f, 1.10f, 0.75f, 1400.0f, 2100.0f, 0.10f, 0.20f, 10.0f,   1.6f, 0.15f, 0.95f,   0.85f, 1.05f, 0.12f,   0.04f, 0.60f, 0.85f, 0.45f,   0.00f, 0.00f, 0.0f, 0.35f }, 2.0f, 5.0f },
	{ "overcast",      "#HL2RPM_Weather_Overcast",      { 0.97f, 1.30f, 0.25f, 1100.0f, 1800.0f, 0.35f, 0.00f,  9.0f,   2.2f, 0.65f, 0.80f,   0.22f, 1.25f, 0.50f,   0.08f, 0.50f, 0.50f, 0.15f,   0.00f, 0.00f, 0.0f, 0.40f }, 2.0f, 6.0f },
	{ "fog",           "#HL2RPM_Weather_Fog",           { 0.92f, 1.20f, 0.15f,  900.0f, 1400.0f, 0.25f, 0.00f,  4.0f,   4.0f, 0.60f, 0.85f,   0.35f, 1.20f, 0.50f,   0.90f, 0.90f, 0.40f, 0.80f,   0.00f, 0.15f, 0.0f, 0.05f }, 1.0f, 3.0f },
	{ "drizzle",       "#HL2RPM_Weather_Drizzle",       { 0.98f, 1.40f, 0.20f, 1000.0f, 2000.0f, 0.45f, 0.00f, 10.0f,   2.8f, 0.70f, 0.72f,   0.14f, 1.10f, 0.55f,   0.18f, 0.60f, 0.45f, 0.20f,   0.25f, 0.45f, 0.0f, 0.35f }, 1.0f, 3.0f },
	{ "rain",          "#HL2RPM_Weather_Rain",          { 1.00f, 1.60f, 0.35f,  900.0f, 2600.0f, 0.60f, 0.00f, 14.0f,   3.2f, 0.75f, 0.60f,   0.06f, 0.90f, 0.60f,   0.28f, 0.55f, 0.40f, 0.10f,   0.75f, 0.85f, 0.15f, 0.55f }, 1.0f, 3.0f },
	{ "thunderstorm",  "#HL2RPM_Weather_Thunderstorm",  { 1.00f, 1.90f, 0.90f,  800.0f, 4500.0f, 0.85f, 0.00f, 18.0f,   3.0f, 0.80f, 0.45f,   0.03f, 0.70f, 0.60f,   0.25f, 0.55f, 0.35f, 0.10f,   1.00f, 1.00f, 3.0f, 0.90f }, 0.5f, 1.5f },
	{ "misty",         "#HL2RPM_Weather_Misty",         { 0.22f, 0.90f, 0.50f, 1300.0f, 1200.0f, 0.00f, 0.30f,  5.0f,   3.5f, 0.20f, 1.05f,   0.80f, 1.10f, 0.15f,   0.45f, 1.20f, 0.85f, 1.00f,   0.00f, 0.10f, 0.0f, 0.10f }, 1.0f, 3.0f },
};

const WeatherPresetInfo_t &GetWeatherPresetInfo( int iPreset )
{
	iPreset = clamp( iPreset, 0, WEATHER_PRESET_COUNT - 1 );
	return s_WeatherPresets[iPreset];
}

int WeatherPresetFromName( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return -1;

	for ( int i = 0; i < WEATHER_PRESET_COUNT; i++ )
	{
		if ( !Q_stricmp( pszName, s_WeatherPresets[i].pszName ) )
			return i;
	}

	if ( pszName[0] >= '0' && pszName[0] <= '9' )
	{
		const int i = atoi( pszName );
		if ( i >= 0 && i < WEATHER_PRESET_COUNT )
			return i;
	}

	return -1;
}

void WeatherParams_Lerp( const WeatherParams_t &a, const WeatherParams_t &b, float t, WeatherParams_t &out )
{
	COMPILE_TIME_ASSERT( sizeof( WeatherParams_t ) % sizeof( float ) == 0 );
	const float *pA = reinterpret_cast<const float *>( &a );
	const float *pB = reinterpret_cast<const float *>( &b );
	float *pOut = reinterpret_cast<float *>( &out );
	const int n = sizeof( WeatherParams_t ) / sizeof( float );
	for ( int i = 0; i < n; i++ )
		pOut[i] = pA[i] + ( pB[i] - pA[i] ) * t;
}

// ---------------------------------------------------------------------------
// Transition graph: weather evolves plausibly (clear -> fair -> clouds ->
// overcast -> rain -> storm -> clearing...) instead of jumping randomly.
// ---------------------------------------------------------------------------
static const float s_flTransitionWeights[WEATHER_PRESET_COUNT][WEATHER_PRESET_COUNT] =
{
	//            clear  fair  partly overc  fog   drizz  rain  storm  misty
	/* clear   */ { 0.0f, 0.50f, 0.25f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.25f },
	/* fair    */ { 0.30f, 0.0f, 0.45f, 0.12f, 0.00f, 0.00f, 0.00f, 0.00f, 0.13f },
	/* partly  */ { 0.10f, 0.30f, 0.0f, 0.33f, 0.00f, 0.14f, 0.00f, 0.13f, 0.00f },
	/* overc   */ { 0.00f, 0.10f, 0.30f, 0.0f, 0.13f, 0.25f, 0.22f, 0.00f, 0.00f },
	/* fog     */ { 0.00f, 0.00f, 0.00f, 0.40f, 0.0f, 0.20f, 0.00f, 0.00f, 0.40f },
	/* drizzle */ { 0.00f, 0.00f, 0.10f, 0.40f, 0.15f, 0.0f, 0.35f, 0.00f, 0.00f },
	/* rain    */ { 0.00f, 0.00f, 0.00f, 0.30f, 0.10f, 0.35f, 0.0f, 0.25f, 0.00f },
	/* storm   */ { 0.00f, 0.00f, 0.00f, 0.15f, 0.00f, 0.25f, 0.60f, 0.0f, 0.00f },
	/* misty   */ { 0.30f, 0.40f, 0.15f, 0.00f, 0.15f, 0.00f, 0.00f, 0.00f, 0.0f },
};

static float TimeOfDayBias( int iPreset, float flHour )
{
	// fog and mist gather in the early morning, storms build in the afternoon
	const bool bMorning = flHour >= 3.5f && flHour <= 9.5f;
	const bool bAfternoon = flHour >= 13.0f && flHour <= 20.0f;
	switch ( iPreset )
	{
	case WEATHER_FOG:			return bMorning ? 2.5f : 0.5f;
	case WEATHER_MISTY:			return bMorning ? 3.0f : 0.6f;
	case WEATHER_THUNDERSTORM:	return bAfternoon ? 1.8f : 0.6f;
	}
	return 1.0f;
}

int WeatherPickNextPreset( int iCurrent, float flHourOfDay, float flRandom01 )
{
	iCurrent = clamp( iCurrent, 0, WEATHER_PRESET_COUNT - 1 );

	float flWeights[WEATHER_PRESET_COUNT];
	float flTotal = 0.0f;
	for ( int i = 0; i < WEATHER_PRESET_COUNT; i++ )
	{
		flWeights[i] = s_flTransitionWeights[iCurrent][i] * TimeOfDayBias( i, flHourOfDay );
		flTotal += flWeights[i];
	}

	if ( flTotal <= 0.0f )
		return WEATHER_FAIR;

	float flPick = flRandom01 * flTotal;
	for ( int i = 0; i < WEATHER_PRESET_COUNT; i++ )
	{
		if ( flPick < flWeights[i] )
			return i;
		flPick -= flWeights[i];
	}
	return iCurrent;
}

// ---------------------------------------------------------------------------
// Sunlight color through the atmosphere
// ---------------------------------------------------------------------------
static Vector SunTransmittance( float flSunAltDeg, float flAltitudeM, float flHaze )
{
	// Kasten-Young relative air mass (valid down to the horizon, clamped just below it)
	const float flAlt = Max( flSunAltDeg, -1.0f );
	const float flSin = sinf( DEG2RAD( flAlt ) );
	const float flAirMass = 1.0f / Max( flSin + 0.50572f * powf( flAlt + 6.07995f, -1.6364f ), 0.01f );
	// aerosols hug the ground: their path grows faster toward the horizon
	const float flMieAirMass = flAirMass * ( 1.0f + 1.4f * clamp( 1.0f - flSin / 0.25f, 0.0f, 1.0f ) );

	// vertical optical depths = coefficient x scale height (Rayleigh 8 km, Mie 1.2 km incl.
	// absorption, ozone tent 15 km), the same values the sky LUT shaders integrate
	const float flR = expf( -flAltitudeM / 8000.0f ) * flAirMass;
	const float flM = expf( -flAltitudeM / 1200.0f ) * Max( flHaze, 0.1f ) * flMieAirMass;
	const float flO = Min( flAirMass, 12.0f );

	const float tauR = 0.0464f * flR + 0.0101f * flM + 0.00975f * flO;
	const float tauG = 0.1085f * flR + 0.0101f * flM + 0.02820f * flO;
	const float tauB = 0.2648f * flR + 0.0101f * flM + 0.00128f * flO;
	return Vector( expf( -tauR ), expf( -tauG ), expf( -tauB ) );
}

Vector Weather_SunlightColor( float flSunAltDeg, float flAltitudeM, float flHaze )
{
	const Vector t = SunTransmittance( flSunAltDeg, flAltitudeM, flHaze );
	const Vector ref = SunTransmittance( 60.0f, flAltitudeM, flHaze );
	return Vector( Min( t.x / ref.x, 1.0f ), Min( t.y / ref.y, 1.0f ), Min( t.z / ref.z, 1.0f ) );
}

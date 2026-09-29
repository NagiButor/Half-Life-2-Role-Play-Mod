//========= HL2RPM ============================================================//
//
// Purpose: Dynamic weather - definitions shared by server and client.
//
// A weather preset is a full set of parameters (clouds, sky, light, fog,
// rain...). The server (env_weather) decides WHICH preset the world is
// heading to and how long the transition takes; the client blends every
// parameter smoothly from whatever it currently shows to the target, so
// any preset can transition into any other without pops.
//
//=============================================================================//

#ifndef WEATHER_SHARED_H
#define WEATHER_SHARED_H
#pragma once

enum WeatherPreset_e
{
	WEATHER_CLEAR = 0,		// sunny, a few high cirrus
	WEATHER_FAIR,			// scattered fair-weather cumulus
	WEATHER_PARTLY_CLOUDY,	// broken cumulus / stratocumulus, sun breaking through
	WEATHER_OVERCAST,		// the classic City 17 grey lid
	WEATHER_FOG,			// low cloud + dense ground fog
	WEATHER_DRIZZLE,		// overcast with light rain and mist
	WEATHER_RAIN,			// steady rain
	WEATHER_THUNDERSTORM,	// cumulonimbus, downpour, lightning, wind
	WEATHER_MISTY,			// hazy morning, soft light, strong light shafts

	WEATHER_PRESET_COUNT
};

#define WEATHER_PRESET_BITS 4

// Every field is blended linearly between presets.
struct WeatherParams_t
{
	// Clouds (volumetric layer, meters)
	float flCloudCoverage;		// 0..1 fraction of sky covered
	float flCloudDensity;		// extinction multiplier (thicker, darker undersides)
	float flCloudType;			// 0 = flat stratus, 0.5 = stratocumulus, 1 = towering cumulus
	float flCloudBase;			// cloud base altitude above the player, meters
	float flCloudThickness;		// layer thickness, meters
	float flCloudDarkness;		// storm darkening of the scattering albedo
	float flCirrus;				// high thin wispy layer amount
	float flCloudSpeed;			// wind speed at cloud altitude, m/s

	// Sky / atmosphere
	float flHaze;				// aerosol (Mie) multiplier, 1 = clean air
	float flSkyDesaturation;	// grey-out of the clear-sky color under overcast
	float flSkyBrightness;		// overall sky brightness multiplier

	// Lighting
	float flSunIntensity;		// direct sun/moon light multiplier (before cloud shadows)
	float flAmbientIntensity;	// sky light multiplier
	float flAmbientDesaturation;// overcast light is flat and grey

	// Fog (world units; density is extinction per 1000 units at the base height)
	float flFogDensity;
	float flFogHeightFalloff;	// per 1000 units above the base height
	float flFogSkyColor;		// 0 = neutral grey fog, 1 = fog takes the horizon sky color
	float flVolumetricDensity;	// sun light shafts ("god rays") strength

	// Precipitation / wind
	float flRain;				// 0..1 rain intensity
	float flWetness;			// 0..1 target surface wetness
	float flLightning;			// lightning strikes per minute
	float flWind;				// 0..1 wind strength (rain slant, gusts)
};

struct WeatherPresetInfo_t
{
	const char *pszName;		// console / script name
	const char *pszDisplayName;	// localization token
	WeatherParams_t params;
	float flMinHours;			// how long this weather lasts (game hours)
	float flMaxHours;
};

const WeatherPresetInfo_t &GetWeatherPresetInfo( int iPreset );
int WeatherPresetFromName( const char *pszName );	// -1 if not found, also accepts numbers

void WeatherParams_Lerp( const WeatherParams_t &a, const WeatherParams_t &b, float t, WeatherParams_t &out );

// Pick the next weather after iCurrent, weighted by plausibility and time of day.
// flRandom01 must be uniform in [0,1).
int WeatherPickNextPreset( int iCurrent, float flHourOfDay, float flRandom01 );

// Color of the sunlight after crossing the atmosphere (same Rayleigh / Mie / ozone
// coefficients as the sky shaders) for a sun altitude (degrees) seen from an altitude
// (meters). Normalized so the sun at 60 degrees is white: golden in the afternoon,
// deep orange at sunset. Does not include the horizon cut-off.
Vector Weather_SunlightColor( float flSunAltDeg, float flAltitudeM, float flHaze );

#endif // WEATHER_SHARED_H

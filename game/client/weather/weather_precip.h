//========= HL2RPM ============================================================//
//
// Purpose: Weather precipitation.
//
// Drives the particle precipitation of func_precipitation ("Particle Rain",
// "Particle Rainstorm", "Particle Snow", "Particle Ash" from rain_fx.pcf) from
// the weather instead of a brush volume: the systems follow the camera and
// their density follows the weather.
//
// Also provides the "Cull relative to Ray Trace Environment" particle
// initializer the precipitation systems were authored with (it is missing
// from the Source 2013 particle library, so rain used to fall through roofs):
// particles that spawn without open sky above them are removed.
//
//=============================================================================//

#ifndef WEATHER_PRECIP_H
#define WEATHER_PRECIP_H
#pragma once

#include "particles_new.h"

struct WeatherPrecipInput_t
{
	float flRain;			// 0..1 (drizzle ~0.25, rain ~0.75, storm 1)
	float flSnow;			// 0..1
	float flAsh;			// 0..1
	float flInnerExposure;	// 0..1 open sky right around the camera
	float flOuterExposure;	// 0..1 open sky on a ring around the camera
	Vector2D vecSlant;		// horizontal drift per unit of fall (wind)
};

class CWeatherPrecipitation
{
public:
	CWeatherPrecipitation();

	void LevelInit();
	void LevelShutdown();

	// bActive false: stop emitting (particles in flight finish their fall)
	void Update( bool bActive, const WeatherPrecipInput_t &in );

private:
	void StopLayer( int iLayer );

	enum { MAX_PRECIP_LAYERS = 16 };
	CSmartPtr<CNewParticleEffect> m_pLayers[MAX_PRECIP_LAYERS];
	float m_flDensity[MAX_PRECIP_LAYERS];
	bool m_bPrecached;
};

// Registers the particle operators above; call before the .pcf files are parsed.
void WeatherPrecip_AddParticleOperators();

// Open-sky test used by the particle culling (cached per cell, thread safe).
bool WeatherPrecip_IsOpenSky( const Vector &vecPos );
// The same test, exact and uncached.
bool WeatherPrecip_TraceOpenSky( const Vector &vecPos );

// Multiplier for the (baked) particle lighting at a point: outdoors it follows the
// current time of day and weather, indoors and without dynamic weather it is 1.
float WeatherPrecip_GetParticleLightScale( const Vector &vecPos );

#endif // WEATHER_PRECIP_H

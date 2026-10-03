//========= HL2RPM ============================================================//
//
// Purpose: Client weather system.
//
// Blends the weather parameters toward the preset chosen by env_weather,
// simulates surface wetness, lightning/thunder, rain & wind audio, the moon,
// and feeds everything to the renderer (global light, sky, clouds, fog, rain).
//
//=============================================================================//

#ifndef C_WEATHER_SYSTEM_H
#define C_WEATHER_SYSTEM_H
#pragma once

#include "igamesystem.h"
#include "weather/weather_shared.h"
#include "weather/weather_precip.h"

struct lightData_Global_t;
struct weatherData_t;
class CViewSetup;

// The visible channel of the current lightning strike (see WeatherRender_Lightning)
struct LightningBoltInfo_t
{
	Vector vecDir;			// horizontal direction from the camera toward the strike
	float flDistance;		// meters
	float flCloudBase;		// meters above the ground
	int iSeed;				// channel shape
	float flBrightness;		// channel brightness now (return strokes + afterglow)
	float flVisibility;		// 0..1 haze / rain between the camera and the strike
};

class C_WeatherSystem : public CAutoGameSystemPerFrame
{
public:
	C_WeatherSystem();

	// IGameSystem
	virtual bool Init();
	virtual void LevelInitPreEntity();
	virtual void LevelInitPostEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Shutdown();
	virtual void Update( float frametime );

	// state
	bool IsActive() const { return m_bActive; }
	const WeatherParams_t &GetParams() const { return m_Current; }
	int GetTargetPreset() const { return m_iTargetPreset; }
	float GetTransitionProgress() const { return m_flBlend; }
	float GetWetness() const { return m_flWetness; }
	float GetPuddles() const { return m_flPuddles; }
	float GetLightningFlash() const { return m_flFlash; }
	float GetNightFactor() const { return m_flNightFactor; }
	float GetSunAltitude() const { return m_flSunAltitude; }
	// strength of the moonlight relative to the map's sunlight (0 by day)
	float GetMoonLightScale() const;
	float GetSkyExposure() const { return m_flSkyExposure; }
	// open sky on a ring ~300 units around the camera (rain seen through doors and windows)
	float GetOuterSkyExposure() const { return m_flOuterExposure; }
	// outdoor light now relative to the map's baked daylight (particle lighting)
	float GetParticleLightScale() const { return m_flParticleLightScale; }
	// brightness of the world now relative to the daylight the cubemaps were baked in
	float GetEnvLightScale() const { return m_flEnvLightScale; }
	const Vector &GetMoonDir() const { return m_vecMoonDir; }
	const Vector &GetSunDir() const { return m_vecSunDir; }
	Vector2D GetWindDir() const { return m_vecWindDir; }

	// called by CDeferredLightGlobal::GetState (sun/moon + weather lighting)
	void ModifyGlobalLight( lightData_Global_t &data, const Vector &vecBaseDiffuse, const Vector &vecBaseAmbHigh ) const;
	// Lightning flash on top of the (time-smoothed) global light: brightens the sky
	// light and, for close strikes, turns the global light into the bolt (with shadows)
	void ApplyLightningFlash( lightData_Global_t &data, bool bCanShadow ) const;
	// visible channel of the current strike, false when there is none
	bool GetLightningBolt( LightningBoltInfo_t &info ) const;
	// called once per rendered frame to build the shader constants
	void FillRenderData( weatherData_t &data, const CViewSetup &view ) const;

	// Offset added to the map fog (EnableWorldFog). Only used for translucents, see weather_render.
	void ForceLightning( int iClass = 0 );

private:
	void UpdateTargetFromEntity();
	void UpdateBlend();
	void UpdateCelestial();
	void UpdateWind( float dt );
	void UpdateWetness( float dt );
	void UpdateLightning( float dt );
	void UpdateSkyExposure( float dt );
	void UpdateAudio( float dt );
	void UpdatePrecipitation();
	Vector2D GetRainSlant() const;
	void StopAudio();

	void TriggerLightning( int iForceClass );
	float GetStrikeEnvelope( float flTime, bool bChannel ) const;

	bool m_bActive;

	// blending
	WeatherParams_t m_Current;
	WeatherParams_t m_From;
	WeatherParams_t m_To;
	int m_iTargetPreset;
	int m_iLastSerial;
	float m_flBlendStart;
	float m_flBlendDuration;
	float m_flBlend;

	// celestial
	Vector m_vecSunDir;
	float m_flSunAltitude;
	Vector m_vecMoonDir;
	float m_flNightFactor;

	// wind / clouds
	Vector2D m_vecWindDir;
	float m_flWindYaw;
	Vector2D m_vecCloudOffset;
	float m_flCloudTime;
	float m_flGust;

	// surfaces
	float m_flWetness;
	float m_flPuddles;

	// lightning
	enum
	{
		STRIKE_CLOSE = 0,	// < 1.2 km: crack + boom, lights the scene with shadows
		STRIKE_MID,			// 1.2..4 km: rolling thunder, visible bolt
		STRIKE_FAR,			// > 4 km: distant rumble, glow in the clouds
		STRIKE_CLASS_COUNT
	};
	float m_flNextLightning;
	float m_flFlash;			// sky / ambient flash now
	float m_flFlashLight;		// directional part (close strikes)
	Vector m_vecFlashDir;		// toward the flash in the sky (sky / clouds)
	Vector m_vecBoltLightDir;	// toward the middle of the channel (scene lighting)
	struct FlashPulse_t { float flStart; float flDecay; float flAmp; };
	FlashPulse_t m_Pulses[6];
	int m_nPulses;
	float m_flStrikeTime;
	float m_flStrikeDistance;	// meters
	float m_flStrikeCloudBase;	// meters
	float m_flStrikeFade;		// haze / rain between the camera and the strike
	int m_iStrikeClass;
	int m_iStrikeSeed;
	bool m_bStrikeChannel;		// cloud-to-ground with a visible channel
	bool m_bLightningWasActive;
	bool m_bFirstStrike;		// the first strike of a storm uses its own roll
	struct Thunder_t { float flTime; float flVolume; int iClass; bool bFirst; };
	CUtlVector<Thunder_t> m_Thunder;
	// base light of the map, for the flash strength (set in ModifyGlobalLight)
	mutable float m_flBaseSunLum;
	mutable float m_flBaseSkyLum;

	// audio / exposure
	float m_flSkyExposure;
	float m_flOuterExposure;
	mutable float m_flParticleLightScale;
	mutable float m_flEnvLightScale;
	float m_flNextExposureTrace;
	float m_flRainVolumeOut;
	float m_flRainVolumeIn;
	float m_flWindVolume;
	bool m_bRainOutPlaying;
	bool m_bRainInPlaying;
	bool m_bWindPlaying;

	CWeatherPrecipitation m_Precip;
};

C_WeatherSystem *GetWeatherSystem();

#endif // C_WEATHER_SYSTEM_H

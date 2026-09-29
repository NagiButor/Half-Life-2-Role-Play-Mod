//========= HL2RPM ============================================================//
//
// Purpose: env_weather - authoritative weather state.
//
// The server owns the target weather preset, the transition duration and the
// automatic weather cycle (a Markov chain over presets, see weather_shared).
// The client (C_WeatherSystem) blends the visuals toward the target.
// Created automatically on maps with a light_deferred_global.
//
//=============================================================================//

#ifndef ENV_WEATHER_H
#define ENV_WEATHER_H
#pragma once

#include "weather/weather_shared.h"

// Same class name on both sides (like env_timecycle), see IMPLEMENT_NETWORKCLASS_DT.
class CEnvWeather : public CBaseEntity
{
	DECLARE_CLASS( CEnvWeather, CBaseEntity );
	DECLARE_NETWORKCLASS();
#ifdef GAME_DLL
	DECLARE_DATADESC();
#endif

public:
	CEnvWeather();
	~CEnvWeather();

	int GetTargetPreset() const { return m_iTargetPreset; }
	float GetTransitionStartTime() const { return m_flTransitionStartTime; }
	float GetTransitionDuration() const { return m_flTransitionDuration; }
	int GetTransitionSerial() const { return m_iTransitionSerial; }
	bool IsAutomatic() const { return m_bAutomatic; }
	float GetWindYaw() const { return m_flWindYaw; }

#ifdef GAME_DLL
	virtual void Spawn();
	virtual void Activate();
	virtual int UpdateTransmitState();
	virtual void OnRestore();

	void SetWeather( int iPreset, float flTransitionSeconds );
	void SetAutomatic( bool bAuto );

	void InputSetWeather( inputdata_t &inputdata );
	void InputSetWeatherInstant( inputdata_t &inputdata );
	void InputEnableAuto( inputdata_t &inputdata );
	void InputDisableAuto( inputdata_t &inputdata );

private:
	void WeatherThink();
	float GameHoursToSeconds( float flHours ) const;
	void ScheduleNextChange();

	int m_iStartPreset;
	bool m_bStartAutomatic;
	float m_flNextChangeTime;
	float m_flDurationScale;
	float m_flWindYawTarget;
#endif

private:
	CNetworkVar( int, m_iTargetPreset );
	CNetworkVar( float, m_flTransitionStartTime );
	CNetworkVar( float, m_flTransitionDuration );
	CNetworkVar( int, m_iTransitionSerial );		// bumped on every change, even to the same preset
	CNetworkVar( bool, m_bAutomatic );
	CNetworkVar( float, m_flWindYaw );
};

CEnvWeather *GetWeatherEntity();

#endif // ENV_WEATHER_H

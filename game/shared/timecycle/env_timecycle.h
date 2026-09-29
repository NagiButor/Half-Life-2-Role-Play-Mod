#ifndef ENV_TIMECYCLE_H
#define ENV_TIMECYCLE_H

#include "cbase.h"

class CEnvTimecycle : public CBaseEntity
{
	DECLARE_CLASS( CEnvTimecycle, CBaseEntity );
	DECLARE_NETWORKCLASS();
#ifdef GAME_DLL
	DECLARE_DATADESC();
#endif

public:
	CEnvTimecycle();
	~CEnvTimecycle();

	float GetTimeOfDayHours() const { return m_flTimeOfDayHours; }
	float GetTimeScale() const { return m_flTimeScale; }

#ifdef GAME_DLL
	virtual void Spawn();
	virtual void Activate();
	virtual int UpdateTransmitState();

	void SetTimeOfDayHours( float hours );
	void SetTimeScale( float scale );

	// HL2RPM: game hours that pass per real second (used by env_weather durations)
	float GetHoursPerSecond() const { return ( m_flDayLengthSeconds > 0.0f ) ? ( 24.0f / m_flDayLengthSeconds ) * m_flTimeScale : 0.0f; }
	float GetSunriseHour() const { return m_flSunriseHour; }
	float GetSunsetHour() const { return m_flSunsetHour; }

private:
	void TimecycleThink();
	void UpdateSun();

	float m_flStartTimeHours;
	float m_flDayLengthSeconds;
	float m_flSunriseHour;
	float m_flSunsetHour;
	float m_flMaxAltitudeDeg;
	float m_flAzimuthOffsetDeg;
	float m_flTwilightHours;
	float m_flNightAltitudeDeg;
#endif

private:
	CNetworkVar( float, m_flTimeOfDayHours );
	CNetworkVar( float, m_flTimeScale );
};

CEnvTimecycle *GetTimecycle();

#endif

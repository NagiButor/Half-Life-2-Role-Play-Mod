#ifndef ENV_TIMECYCLE_H
#define ENV_TIMECYCLE_H

#include "cbase.h"

#ifdef GAME_DLL
// HL2RPM: sv_timecycle_speed_scale - every map's time_scale is multiplied by it (0.1: the
// FGD default time_scale 10 gives the documented 2.4 hour day instead of 14.4 minutes)
float TimecycleSpeedScale();
#endif

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
	virtual void OnRestore();
	virtual int UpdateTransmitState();
	// HL2RPM: world lighting/weather belongs to its map: never carried through a
	// changelevel landmark (point entities are by default -> doubled suns and lamps)
	virtual int ObjectCaps() { return BaseClass::ObjectCaps() & ~FCAP_ACROSS_TRANSITION; }

	void SetTimeOfDayHours( float hours );
	void SetTimeScale( float scale );

	// HL2RPM: game hours that pass per real second (used by env_weather durations)
	float GetHoursPerSecond() const { return ( m_flDayLengthSeconds > 0.0f ) ? ( 24.0f / m_flDayLengthSeconds ) * m_flTimeScale * TimecycleSpeedScale() : 0.0f; }
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

#ifdef GAME_DLL
// HL2RPM: time of day of the world, also on maps without an env_timecycle
bool WorldClock_GetHours( float &flHours );
#endif

#endif

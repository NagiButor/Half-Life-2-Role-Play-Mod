
#include "cbase.h"
#include "timecycle/env_timecycle.h"

#ifdef GAME_DLL
#include "deferred/CDefLightGlobal.h"
#endif

#include "tier0/memdbgon.h"

static CEnvTimecycle *g_pTimecycle = NULL;
CEnvTimecycle *GetTimecycle()
{
	return g_pTimecycle;
}

#ifdef GAME_DLL
BEGIN_DATADESC( CEnvTimecycle )
	DEFINE_KEYFIELD( m_flStartTimeHours, FIELD_FLOAT, "start_time" ),
	DEFINE_KEYFIELD( m_flTimeScale, FIELD_FLOAT, "time_scale" ),
	DEFINE_KEYFIELD( m_flDayLengthSeconds, FIELD_FLOAT, "day_length_seconds" ),
	DEFINE_KEYFIELD( m_flSunriseHour, FIELD_FLOAT, "sunrise_hour" ),
	DEFINE_KEYFIELD( m_flSunsetHour, FIELD_FLOAT, "sunset_hour" ),
	DEFINE_KEYFIELD( m_flMaxAltitudeDeg, FIELD_FLOAT, "max_altitude" ),
	DEFINE_KEYFIELD( m_flAzimuthOffsetDeg, FIELD_FLOAT, "azimuth_offset" ),
	DEFINE_KEYFIELD( m_flTwilightHours, FIELD_FLOAT, "twilight_hours" ),
	DEFINE_KEYFIELD( m_flNightAltitudeDeg, FIELD_FLOAT, "night_altitude" ),
END_DATADESC()
#endif

IMPLEMENT_NETWORKCLASS_DT( CEnvTimecycle, CEnvTimecycle_DT )
#ifdef GAME_DLL
	SendPropFloat( SENDINFO( m_flTimeOfDayHours ) ),
	SendPropFloat( SENDINFO( m_flTimeScale ) ),
#else
	RecvPropFloat( RECVINFO( m_flTimeOfDayHours ) ),
	RecvPropFloat( RECVINFO( m_flTimeScale ) ),
#endif
END_NETWORK_TABLE();

LINK_ENTITY_TO_CLASS( env_timecycle, CEnvTimecycle );

CEnvTimecycle::CEnvTimecycle()
{
	Assert( g_pTimecycle == NULL );
	g_pTimecycle = this;

	m_flTimeOfDayHours = 12.0f;
	m_flTimeScale = 10.0f;

#ifdef GAME_DLL
	m_flStartTimeHours = 12.0f;
	m_flDayLengthSeconds = 8640.0f;
	m_flSunriseHour = 6.0f;
	m_flSunsetHour = 18.0f;
	m_flMaxAltitudeDeg = 60.0f;
	m_flAzimuthOffsetDeg = 0.0f;
	m_flTwilightHours = 1.25f;
	m_flNightAltitudeDeg = -12.0f;
#endif
}

CEnvTimecycle::~CEnvTimecycle()
{
	Assert( g_pTimecycle == this );
	g_pTimecycle = NULL;
}

#ifdef GAME_DLL

void CEnvTimecycle::Spawn()
{
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );

	m_flTimeOfDayHours = m_flStartTimeHours;
	if ( m_flDayLengthSeconds <= 0.0f )
		m_flDayLengthSeconds = 8640.0f;
	if ( m_flSunsetHour <= m_flSunriseHour )
	{
		m_flSunriseHour = 6.0f;
		m_flSunsetHour = 18.0f;
	}
	if ( m_flMaxAltitudeDeg <= 0.0f )
		m_flMaxAltitudeDeg = 60.0f;
	if ( m_flTwilightHours <= 0.0f )
		m_flTwilightHours = 1.25f;
	if ( m_flNightAltitudeDeg > 0.0f )
		m_flNightAltitudeDeg = -12.0f;

	SetThink( &CEnvTimecycle::TimecycleThink );
	SetNextThink( gpGlobals->curtime );
}

void CEnvTimecycle::Activate()
{
	BaseClass::Activate();
	UpdateSun();
}

int CEnvTimecycle::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

void CEnvTimecycle::SetTimeOfDayHours( float hours )
{
	while ( hours < 0.0f ) hours += 24.0f;
	while ( hours >= 24.0f ) hours -= 24.0f;
	m_flTimeOfDayHours = hours;
	UpdateSun();
}

void CEnvTimecycle::SetTimeScale( float scale )
{
	m_flTimeScale = Max( 0.0f, scale );
}

void CEnvTimecycle::TimecycleThink()
{
	const float dt = gpGlobals->frametime;
	const float hoursPerSecond = 24.0f / m_flDayLengthSeconds;

	float hours = m_flTimeOfDayHours;
	hours += dt * hoursPerSecond * m_flTimeScale;
	if ( hours >= 24.0f || hours < 0.0f )
		hours = fmodf( hours, 24.0f );
	if ( hours < 0.0f )
		hours += 24.0f;

	m_flTimeOfDayHours = hours;

	UpdateSun();

	SetNextThink( gpGlobals->curtime );
}

static QAngle AnglesFromDirNegated( const Vector &vecLight )
{
	Vector dir = -vecLight;
	QAngle ang;
	VectorAngles( dir, ang );
	return ang;
}

void CEnvTimecycle::UpdateSun()
{
	CDeferredLightGlobal *pGlobal = GetGlobalLight();
	if ( !pGlobal )
		return;

	const float t = m_flTimeOfDayHours;
	const float daySpan = m_flSunsetHour - m_flSunriseHour;

	float altitudeDeg = 0.0f;
	if ( t >= m_flSunriseHour && t <= m_flSunsetHour )
	{
		float day01 = ( t - m_flSunriseHour ) / daySpan;
		day01 = clamp( day01, 0.0f, 1.0f );
		altitudeDeg = sinf( day01 * M_PI_F ) * m_flMaxAltitudeDeg;
	}
	else
	{
		float delta = 0.0f;
		if ( t < m_flSunriseHour )
			delta = m_flSunriseHour - t;
		else
			delta = t - m_flSunsetHour;

		float x = ( m_flTwilightHours > 0.0f ) ? clamp( delta / m_flTwilightHours, 0.0f, 1.0f ) : 1.0f;
		x = x * x * ( 3.0f - 2.0f * x );

		altitudeDeg = Lerp( 0.0f, m_flNightAltitudeDeg, x );
	}

	const float azimuthDeg = m_flAzimuthOffsetDeg + ( t / 24.0f ) * 360.0f;

	float sa, ca, sz, cz;
	SinCos( DEG2RAD( altitudeDeg ), &sa, &ca );
	SinCos( DEG2RAD( azimuthDeg ), &sz, &cz );

	Vector sunDir( ca * cz, ca * sz, sa );
	VectorNormalize( sunDir );

	pGlobal->SetAbsAngles( AnglesFromDirNegated( sunDir ) );
}

static CEnvTimecycle *EnsureTimecycle()
{
	CEnvTimecycle *tc = GetTimecycle();
	if ( tc )
		return tc;

	CBaseEntity *ent = CreateEntityByName( "env_timecycle" );
	if ( !ent )
		return NULL;

	DispatchSpawn( ent );
	ent->Activate();

	return GetTimecycle();
}

CON_COMMAND( sv_timecycle_set_speed, "" )
{
	if ( args.ArgC() < 2 )
		return;
	CEnvTimecycle *tc = EnsureTimecycle();
	if ( tc )
		tc->SetTimeScale( atof( args[1] ) );
}

CON_COMMAND( sv_timecycle_set_time, "" )
{
	if ( args.ArgC() < 2 )
		return;
	CEnvTimecycle *tc = EnsureTimecycle();
	if ( tc )
		tc->SetTimeOfDayHours( atof( args[1] ) );
}

#endif

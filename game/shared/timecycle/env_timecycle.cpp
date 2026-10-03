
#include "cbase.h"
#include "timecycle/env_timecycle.h"

#ifdef GAME_DLL
#include "deferred/CDefLightGlobal.h"
#endif

#include "tier0/memdbgon.h"

#ifdef GAME_DLL
// HL2RPM: the maps keep the FGD default time_scale 10, which the FGD described as a 2.4
// hour day - it was 8640 s / 10 = 14.4 minutes: the sun crawled across the sky visibly
static ConVar sv_timecycle_speed_scale( "sv_timecycle_speed_scale", "0.1", FCVAR_ARCHIVE,
	"Multiplies the time_scale of every map (0.1: time_scale 10 = a day of day_length_seconds = 2.4 hours)", true, 0.0f, true, 100.0f );

float TimecycleSpeedScale()
{
	return sv_timecycle_speed_scale.GetFloat();
}
#endif

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

	// HL2RPM: without these a loaded game restarted at the constructor's 12:00 and
	// the clock never moved again (the think function was not restored)
	DEFINE_FIELD( m_flTimeOfDayHours, FIELD_FLOAT ),
	DEFINE_THINKFUNC( TimecycleThink ),
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

// ---------------------------------------------------------------------------
// HL2RPM: the world clock outlives a map. A level transition (changelevel) keeps
// the time of day, its speed and the day length, whether the next map is new or
// revisited; a new game starts from the map's start_time. Maps without a
// timecycle (interiors without sun) keep the clock running.
// ---------------------------------------------------------------------------
struct WorldClock_t
{
	bool bValid;
	float flHours;
	float flScale;
	float flDayLength;
};
static WorldClock_t s_WorldClock = { false, 12.0f, 10.0f, 8640.0f };

static void WorldClock_Store( float flHours, float flScale, float flDayLength )
{
	s_WorldClock.bValid = true;
	s_WorldClock.flHours = flHours;
	s_WorldClock.flScale = flScale;
	s_WorldClock.flDayLength = flDayLength;
}

bool WorldClock_GetHours( float &flHours )
{
	if ( GetTimecycle() )
	{
		flHours = GetTimecycle()->GetTimeOfDayHours();
		return true;
	}
	flHours = s_WorldClock.flHours;
	return s_WorldClock.bValid;
}

class CWorldClockSystem : public CAutoGameSystemPerFrame
{
public:
	CWorldClockSystem() : CAutoGameSystemPerFrame( "CWorldClockSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		// a new game (or the menu background) starts with the map's own settings
		if ( gpGlobals->eLoadType == MapLoad_NewGame || gpGlobals->eLoadType == MapLoad_Background )
			s_WorldClock.bValid = false;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		// no env_timecycle on this map: the time still passes
		if ( GetTimecycle() || !s_WorldClock.bValid || s_WorldClock.flDayLength <= 0.0f )
			return;
		float h = s_WorldClock.flHours + gpGlobals->frametime * ( 24.0f / s_WorldClock.flDayLength ) * s_WorldClock.flScale * TimecycleSpeedScale();
		h = fmodf( h, 24.0f );
		if ( h < 0.0f )
			h += 24.0f;
		s_WorldClock.flHours = h;
	}
};
static CWorldClockSystem g_WorldClockSystem;

void CEnvTimecycle::OnRestore()
{
	BaseClass::OnRestore();

	if ( gpGlobals->eLoadType == MapLoad_Transition && s_WorldClock.bValid )
	{
		// revisited map: its saved clock is from when the player left, continue the world's
		m_flTimeOfDayHours = s_WorldClock.flHours;
		m_flTimeScale = s_WorldClock.flScale;
		m_flDayLengthSeconds = s_WorldClock.flDayLength;
	}
	else
	{
		WorldClock_Store( m_flTimeOfDayHours, m_flTimeScale, m_flDayLengthSeconds );
	}
	UpdateSun();
}

void CEnvTimecycle::Spawn()
{
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );

	m_flTimeOfDayHours = m_flStartTimeHours;
	if ( m_flDayLengthSeconds <= 0.0f )
		m_flDayLengthSeconds = 8640.0f;

	// changelevel into a map visited for the first time: keep the world's clock
	if ( gpGlobals->eLoadType == MapLoad_Transition && s_WorldClock.bValid )
	{
		m_flTimeOfDayHours = s_WorldClock.flHours;
		m_flTimeScale = s_WorldClock.flScale;
		m_flDayLengthSeconds = s_WorldClock.flDayLength;
	}
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
	WorldClock_Store( hours, m_flTimeScale, m_flDayLengthSeconds );
	UpdateSun();
}

void CEnvTimecycle::SetTimeScale( float scale )
{
	m_flTimeScale = Max( 0.0f, scale );
	WorldClock_Store( m_flTimeOfDayHours, m_flTimeScale, m_flDayLengthSeconds );
}

void CEnvTimecycle::TimecycleThink()
{
	const float dt = gpGlobals->frametime;
	const float hoursPerSecond = 24.0f / m_flDayLengthSeconds;

	float hours = m_flTimeOfDayHours;
	hours += dt * hoursPerSecond * m_flTimeScale * TimecycleSpeedScale();
	if ( hours >= 24.0f || hours < 0.0f )
		hours = fmodf( hours, 24.0f );
	if ( hours < 0.0f )
		hours += 24.0f;

	m_flTimeOfDayHours = hours;
	WorldClock_Store( hours, m_flTimeScale, m_flDayLengthSeconds );

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

		// HL2RPM: Lerp( percent, a, b ) - the arguments were swapped, so the sun dropped to
		// the night altitude the moment it set (no twilight at all)
		altitudeDeg = Lerp( x, 0.0f, m_flNightAltitudeDeg );
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

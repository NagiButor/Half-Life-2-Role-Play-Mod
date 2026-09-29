//========= HL2RPM ============================================================//
//
// Purpose: env_weather - authoritative weather state (see env_weather.h).
//
//=============================================================================//

#include "cbase.h"
#include "weather/env_weather.h"
#include "timecycle/env_timecycle.h"

#include "tier0/memdbgon.h"

static CEnvWeather *g_pWeatherEntity = NULL;
CEnvWeather *GetWeatherEntity()
{
	return g_pWeatherEntity;
}

#ifdef GAME_DLL
BEGIN_DATADESC( CEnvWeather )
	DEFINE_KEYFIELD( m_iStartPreset, FIELD_INTEGER, "start_weather" ),
	DEFINE_KEYFIELD( m_bStartAutomatic, FIELD_BOOLEAN, "auto_weather" ),
	DEFINE_KEYFIELD( m_flDurationScale, FIELD_FLOAT, "duration_scale" ),

	DEFINE_FIELD( m_iTargetPreset, FIELD_INTEGER ),
	DEFINE_FIELD( m_flTransitionStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flTransitionDuration, FIELD_FLOAT ),
	DEFINE_FIELD( m_iTransitionSerial, FIELD_INTEGER ),
	DEFINE_FIELD( m_bAutomatic, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextChangeTime, FIELD_TIME ),
	DEFINE_FIELD( m_flWindYaw, FIELD_FLOAT ),
	DEFINE_FIELD( m_flWindYawTarget, FIELD_FLOAT ),

	DEFINE_INPUTFUNC( FIELD_STRING, "SetWeather", InputSetWeather ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetWeatherInstant", InputSetWeatherInstant ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableAuto", InputEnableAuto ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableAuto", InputDisableAuto ),

	DEFINE_THINKFUNC( WeatherThink ),
END_DATADESC()
#endif

IMPLEMENT_NETWORKCLASS_DT( CEnvWeather, CEnvWeather_DT )
#ifdef GAME_DLL
	SendPropInt( SENDINFO( m_iTargetPreset ), WEATHER_PRESET_BITS, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flTransitionStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flTransitionDuration ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_iTransitionSerial ), 16, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bAutomatic ) ),
	SendPropFloat( SENDINFO( m_flWindYaw ), 0, SPROP_NOSCALE ),
#else
	RecvPropInt( RECVINFO( m_iTargetPreset ) ),
	RecvPropFloat( RECVINFO( m_flTransitionStartTime ) ),
	RecvPropFloat( RECVINFO( m_flTransitionDuration ) ),
	RecvPropInt( RECVINFO( m_iTransitionSerial ) ),
	RecvPropBool( RECVINFO( m_bAutomatic ) ),
	RecvPropFloat( RECVINFO( m_flWindYaw ) ),
#endif
END_NETWORK_TABLE();

LINK_ENTITY_TO_CLASS( env_weather, CEnvWeather );

CEnvWeather::CEnvWeather()
{
	Assert( g_pWeatherEntity == NULL );
	g_pWeatherEntity = this;

	m_iTargetPreset = WEATHER_FAIR;
	m_flTransitionStartTime = 0.0f;
	m_flTransitionDuration = 0.0f;
	m_iTransitionSerial = 0;
	m_bAutomatic = true;
	m_flWindYaw = 45.0f;

#ifdef GAME_DLL
	m_iStartPreset = -1;
	m_bStartAutomatic = true;
	m_flNextChangeTime = 0.0f;
	m_flDurationScale = 1.0f;
	m_flWindYawTarget = 45.0f;
#endif
}

CEnvWeather::~CEnvWeather()
{
	if ( g_pWeatherEntity == this )
		g_pWeatherEntity = NULL;
}

#ifdef GAME_DLL

static ConVar sv_weather_duration_scale( "sv_weather_duration_scale", "1.0", FCVAR_ARCHIVE,
	"Multiplier for how long each automatic weather lasts" );
static ConVar sv_weather_min_seconds( "sv_weather_min_seconds", "90", FCVAR_ARCHIVE,
	"Automatic weather never changes faster than this (real seconds)" );
static ConVar sv_weather_transition_seconds( "sv_weather_transition_seconds", "60", FCVAR_ARCHIVE,
	"Default duration of a weather transition (real seconds)" );
static ConVar sv_weather_procedural_sky( "sv_weather_procedural_sky", "1", FCVAR_ARCHIVE,
	"Replace the map's static skybox with the procedural sky (clouds, sun, moon, stars follow the weather)" );

#define WEATHER_PROCEDURAL_SKY "sky_proc_atmo_01"

// Static skyboxes can't show dynamic weather: switch the map to the procedural sky.
// (Mapbase reloads the sky when sv_skyname changes, like skybox_swapper does.)
static void ApplyProceduralSky()
{
	if ( !sv_weather_procedural_sky.GetBool() )
		return;

	static ConVarRef sv_skyname( "sv_skyname", false );
	if ( !sv_skyname.IsValid() || !Q_stricmp( sv_skyname.GetString(), WEATHER_PROCEDURAL_SKY ) )
		return;

	static const char *s_pszFaces[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
	for ( int i = 0; i < 6; i++ )
		PrecacheMaterial( UTIL_VarArgs( "skybox/%s%s", WEATHER_PROCEDURAL_SKY, s_pszFaces[i] ) );

	DevMsg( "env_weather: replacing skybox '%s' with '%s'\n", sv_skyname.GetString(), WEATHER_PROCEDURAL_SKY );
	sv_skyname.SetValue( WEATHER_PROCEDURAL_SKY );
}

void CEnvWeather::Spawn()
{
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );

	if ( m_flDurationScale <= 0.0f )
		m_flDurationScale = 1.0f;

	int iStart = m_iStartPreset;
	if ( iStart < 0 || iStart >= WEATHER_PRESET_COUNT )
	{
		// no explicit start weather: begin with something calm
		static const int s_iCalmStarts[] = { WEATHER_CLEAR, WEATHER_FAIR, WEATHER_FAIR, WEATHER_PARTLY_CLOUDY, WEATHER_OVERCAST };
		iStart = s_iCalmStarts[ RandomInt( 0, ARRAYSIZE( s_iCalmStarts ) - 1 ) ];
	}

	m_iTargetPreset = iStart;
	m_flTransitionStartTime = gpGlobals->curtime;
	m_flTransitionDuration = 0.0f;
	m_iTransitionSerial = m_iTransitionSerial + 1;
	m_bAutomatic = m_bStartAutomatic;
	m_flWindYaw = m_flWindYawTarget = RandomFloat( 0.0f, 360.0f );

	ScheduleNextChange();

	ApplyProceduralSky();

	SetThink( &CEnvWeather::WeatherThink );
	SetNextThink( gpGlobals->curtime + 1.0f );
}

void CEnvWeather::Activate()
{
	BaseClass::Activate();
}

void CEnvWeather::OnRestore()
{
	BaseClass::OnRestore();

	ApplyProceduralSky();

	// the client must snap to the saved weather instead of blending from the menu map
	m_iTransitionSerial = m_iTransitionSerial + 1;
	m_flTransitionDuration = 0.0f;
}

int CEnvWeather::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

float CEnvWeather::GameHoursToSeconds( float flHours ) const
{
	float flHoursPerSecond = 24.0f / 864.0f;	// default: 14.4 real minutes per game day
	CEnvTimecycle *pTimecycle = GetTimecycle();
	if ( pTimecycle && pTimecycle->GetHoursPerSecond() > 0.0f )
		flHoursPerSecond = pTimecycle->GetHoursPerSecond();

	return flHours / flHoursPerSecond;
}

void CEnvWeather::ScheduleNextChange()
{
	const WeatherPresetInfo_t &info = GetWeatherPresetInfo( m_iTargetPreset );
	float flSeconds = GameHoursToSeconds( RandomFloat( info.flMinHours, info.flMaxHours ) );
	flSeconds *= m_flDurationScale * Max( 0.05f, sv_weather_duration_scale.GetFloat() );
	flSeconds = Max( flSeconds, sv_weather_min_seconds.GetFloat() );

	// the timer counts from the end of the transition into this weather
	m_flNextChangeTime = m_flTransitionStartTime + m_flTransitionDuration + flSeconds;
}

void CEnvWeather::SetWeather( int iPreset, float flTransitionSeconds )
{
	iPreset = clamp( iPreset, 0, WEATHER_PRESET_COUNT - 1 );

	m_iTargetPreset = iPreset;
	m_flTransitionStartTime = gpGlobals->curtime;
	m_flTransitionDuration = Max( 0.0f, flTransitionSeconds );
	m_iTransitionSerial = ( m_iTransitionSerial + 1 ) & 0xFFFF;

	ScheduleNextChange();

	DevMsg( "env_weather: -> %s over %.0f s (auto %s)\n", GetWeatherPresetInfo( iPreset ).pszName,
		flTransitionSeconds, m_bAutomatic ? "on" : "off" );
}

void CEnvWeather::SetAutomatic( bool bAuto )
{
	m_bAutomatic = bAuto;
	if ( bAuto )
		ScheduleNextChange();
}

void CEnvWeather::WeatherThink()
{
	SetNextThink( gpGlobals->curtime + 1.0f );

	// slow random walk of the wind direction
	if ( fabsf( AngleDiff( m_flWindYawTarget, m_flWindYaw ) ) < 1.0f && RandomFloat() < 0.02f )
		m_flWindYawTarget = AngleNormalize( m_flWindYaw + RandomFloat( -60.0f, 60.0f ) );
	const float flYawStep = clamp( AngleDiff( m_flWindYawTarget, m_flWindYaw ), -0.5f, 0.5f );
	if ( flYawStep != 0.0f )
		m_flWindYaw = AngleNormalize( m_flWindYaw + flYawStep );

	if ( !m_bAutomatic )
		return;

	if ( gpGlobals->curtime < m_flNextChangeTime )
		return;

	float flHour = 12.0f;
	if ( GetTimecycle() )
		flHour = GetTimecycle()->GetTimeOfDayHours();

	const int iNext = WeatherPickNextPreset( m_iTargetPreset, flHour, RandomFloat( 0.0f, 0.9999f ) );

	// transitions take roughly a game hour, within sane real-time limits
	float flTransition = clamp( GameHoursToSeconds( RandomFloat( 0.6f, 1.2f ) ),
		sv_weather_transition_seconds.GetFloat() * 0.5f, sv_weather_transition_seconds.GetFloat() * 3.0f );
	SetWeather( iNext, flTransition );
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
static void ParseWeatherArgs( const char *pszArgs, int &iPreset, float &flSeconds )
{
	char szName[64];
	szName[0] = 0;
	flSeconds = -1.0f;
	if ( pszArgs )
		sscanf( pszArgs, "%63s %f", szName, &flSeconds );
	iPreset = WeatherPresetFromName( szName );
}

void CEnvWeather::InputSetWeather( inputdata_t &inputdata )
{
	int iPreset;
	float flSeconds;
	ParseWeatherArgs( inputdata.value.String(), iPreset, flSeconds );
	if ( iPreset < 0 )
	{
		Warning( "env_weather: unknown weather '%s'\n", inputdata.value.String() );
		return;
	}
	SetWeather( iPreset, flSeconds >= 0.0f ? flSeconds : sv_weather_transition_seconds.GetFloat() );
}

void CEnvWeather::InputSetWeatherInstant( inputdata_t &inputdata )
{
	int iPreset;
	float flSeconds;
	ParseWeatherArgs( inputdata.value.String(), iPreset, flSeconds );
	if ( iPreset >= 0 )
		SetWeather( iPreset, 0.0f );
}

void CEnvWeather::InputEnableAuto( inputdata_t &inputdata )
{
	SetAutomatic( true );
}

void CEnvWeather::InputDisableAuto( inputdata_t &inputdata )
{
	SetAutomatic( false );
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
static CEnvWeather *EnsureWeather()
{
	CEnvWeather *pWeather = GetWeatherEntity();
	if ( pWeather )
		return pWeather;

	CBaseEntity *pEnt = CreateEntityByName( "env_weather" );
	if ( !pEnt )
		return NULL;

	DispatchSpawn( pEnt );
	pEnt->Activate();
	return GetWeatherEntity();
}

CON_COMMAND( sv_weather, "Set the weather: sv_weather <name|index> [transition seconds]. sv_weather_list shows the names." )
{
	if ( args.ArgC() < 2 )
	{
		CEnvWeather *pWeather = GetWeatherEntity();
		Msg( "Weather: %s (auto %s)\n", pWeather ? GetWeatherPresetInfo( pWeather->GetTargetPreset() ).pszName : "none",
			( pWeather && pWeather->IsAutomatic() ) ? "on" : "off" );
		return;
	}

	const int iPreset = WeatherPresetFromName( args[1] );
	if ( iPreset < 0 )
	{
		Warning( "Unknown weather '%s'. Use sv_weather_list.\n", args[1] );
		return;
	}

	CEnvWeather *pWeather = EnsureWeather();
	if ( !pWeather )
		return;

	const float flSeconds = args.ArgC() >= 3 ? atof( args[2] ) : sv_weather_transition_seconds.GetFloat();
	pWeather->SetWeather( iPreset, flSeconds );
}

CON_COMMAND( sv_weather_auto, "Automatic weather changes: sv_weather_auto <0|1>" )
{
	CEnvWeather *pWeather = EnsureWeather();
	if ( !pWeather )
		return;

	if ( args.ArgC() >= 2 )
		pWeather->SetAutomatic( atoi( args[1] ) != 0 );

	Msg( "Automatic weather: %s\n", pWeather->IsAutomatic() ? "on" : "off" );
}

CON_COMMAND( sv_weather_next, "Immediately start the next automatic weather change" )
{
	CEnvWeather *pWeather = EnsureWeather();
	if ( !pWeather )
		return;

	float flHour = GetTimecycle() ? GetTimecycle()->GetTimeOfDayHours() : 12.0f;
	const int iNext = WeatherPickNextPreset( pWeather->GetTargetPreset(), flHour, RandomFloat( 0.0f, 0.9999f ) );
	pWeather->SetWeather( iNext, sv_weather_transition_seconds.GetFloat() );
}

CON_COMMAND( sv_weather_list, "List weather presets" )
{
	for ( int i = 0; i < WEATHER_PRESET_COUNT; i++ )
		Msg( "  %d  %s\n", i, GetWeatherPresetInfo( i ).pszName );
}

#endif // GAME_DLL

//========= HL2RPM ============================================================//
//
// Purpose: Developer "weather photo tour": loads a map, goes through weather
// presets / times of day, takes a screenshot of each (screenshots/*.jpg) and
// optionally quits. Handy to compare looks after tuning.
//
//   hl2.exe ... +cl_weather_tour_map test_deferred      (auto start + quit)
//   cl_weather_tour                                      (in game, no quit)
//
//=============================================================================//

#include "cbase.h"
#include "igamesystem.h"
#include "weather/c_weather_system.h"

#include "tier0/memdbgon.h"

static ConVar cl_weather_tour_map( "cl_weather_tour_map", "", 0, "Dev: load this map at startup, run the weather photo tour, then quit" );
static ConVar cl_weather_tour_angles( "cl_weather_tour_angles", "-14 40 0", 0, "Dev: camera angles used by the weather photo tour" );
static ConVar cl_weather_tour_step( "cl_weather_tour_step", "6", 0, "Dev: seconds between weather photo tour steps" );
static ConVar cl_weather_tour_first( "cl_weather_tour_first", "0", 0, "Dev: first step of the weather photo tour" );
static ConVar cl_weather_tour_delay( "cl_weather_tour_delay", "4", 0, "Dev: seconds after startup before the tour loads its map" );
static ConVar cl_weather_tour_quit_delay( "cl_weather_tour_quit_delay", "3", 0, "Dev: seconds between the last screenshot and quitting" );
static ConVar cl_weather_tour_last( "cl_weather_tour_last", "-1", 0, "Dev: last step of the weather photo tour (-1 = all)" );
static ConVar cl_weather_tour_debug( "cl_weather_tour_debug", "0", 0, "Dev: print the view angles during the weather photo tour" );
static ConVar cl_weather_tour_exec( "cl_weather_tour_exec", "", 0, "Dev: with cl_weather_tour_map, exec this cfg right after the map loaded" );
static ConVar cl_weather_tour_run( "cl_weather_tour_run", "1", 0, "Dev: with cl_weather_tour_map, run the photo tour (0 = only load the map and exec cl_weather_tour_exec)" );

enum TourView_e
{
	TOURVIEW_DEFAULT = 0,	// cl_weather_tour_angles
	TOURVIEW_SUN,			// look toward the sun (sun in the upper left of the frame)
};

struct TourStep_t
{
	const char *pszCommands;
	bool bScreenshot;
	int iView;
};

static const TourStep_t s_TourSteps[] =
{
	{ "sv_timecycle_set_time 15; sv_weather clear 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather fair 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather partlycloudy 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather overcast 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather drizzle 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather rain 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather thunderstorm 0; cl_weather_lightning", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather fog 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 15; sv_weather partlycloudy 0", true, TOURVIEW_SUN },	// silver linings, cloud shadows
	{ "sv_timecycle_set_time 7.5; sv_weather misty 0", true, TOURVIEW_SUN },		// light shafts
	{ "sv_timecycle_set_time 17.5; sv_weather fair 0", true, TOURVIEW_SUN },		// golden hour
	{ "sv_timecycle_set_time 17.9; sv_weather partlycloudy 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 18.3; sv_weather fair 0", true, TOURVIEW_SUN },		// just after sunset
	{ "sv_timecycle_set_time 1; sv_weather clear 0", true, TOURVIEW_DEFAULT },
	{ "sv_timecycle_set_time 1; sv_weather overcast 0", true, TOURVIEW_DEFAULT },
};

class CWeatherDevTour : public CAutoGameSystemPerFrame
{
public:
	CWeatherDevTour() : CAutoGameSystemPerFrame( "CWeatherDevTour" )
	{
		m_bStartupDone = false;
		m_bRunning = false;
		m_bQuitWhenDone = false;
		m_iStep = -1;
		m_flNextTime = 0.0f;
		m_flAimTime = -1.0f;
	}

	void Start( bool bQuitWhenDone )
	{
		m_bRunning = true;
		m_bQuitWhenDone = bQuitWhenDone;
		m_iStep = cl_weather_tour_first.GetInt() - 1;
		m_bShotTaken = true;	// no screenshot of the state before the first step
		m_flNextTime = gpGlobals->realtime + 6.0f;	// let the level settle (clouds accumulate, shaders load)
		engine->ClientCmd_Unrestricted( "sv_cheats 1; cl_drawhud 0; sv_weather_auto 0; sv_timecycle_set_speed 0; jpeg_quality 92" );
		engine->ClientCmd_Unrestricted( VarArgs( "setang %s", cl_weather_tour_angles.GetString() ) );
	}

	virtual void Update( float frametime )
	{
		// startup mode: load the map once the menu is up
		if ( !m_bStartupDone && cl_weather_tour_map.GetString()[0] )
		{
			if ( !engine->IsInGame() && gpGlobals->realtime > cl_weather_tour_delay.GetFloat() )
			{
				m_bStartupDone = true;
				m_bPendingStart = true;
				engine->ClientCmd_Unrestricted( VarArgs( "map %s", cl_weather_tour_map.GetString() ) );
			}
			return;
		}

		if ( m_bPendingStart && engine->IsInGame() && !engine->IsDrawingLoadingImage() )
		{
			m_bPendingStart = false;
			if ( cl_weather_tour_exec.GetString()[0] )
				engine->ClientCmd_Unrestricted( VarArgs( "exec %s\n", cl_weather_tour_exec.GetString() ) );
			if ( cl_weather_tour_run.GetBool() )
				Start( true );
		}

		if ( m_flQuitTime > 0.0f && gpGlobals->realtime >= m_flQuitTime )
		{
			m_flQuitTime = -1.0f;
			engine->ClientCmd_Unrestricted( "quit\n" );
		}

		if ( !m_bRunning || !engine->IsInGame() )
			return;

		if ( cl_weather_tour_debug.GetBool() && gpGlobals->realtime >= m_flNextDebug )
		{
			m_flNextDebug = gpGlobals->realtime + 0.5f;
			QAngle ang;
			engine->GetViewAngles( ang );
			Msg( "[tour] t=%.2f step %d view %.2f %.2f %.2f\n", gpGlobals->realtime, m_iStep, ang.x, ang.y, ang.z );
		}

		// aim once the new sun position has arrived from the server
		if ( m_flAimTime > 0.0f && gpGlobals->realtime >= m_flAimTime )
		{
			m_flAimTime = -1.0f;
			AimView();
		}

		// frame time of the settled step (after the camera aimed and the clouds accumulated)
		if ( !m_bShotTaken && m_iStep >= 0 && gpGlobals->realtime >= m_flMeasureStart )
		{
			m_flMeasureSum += gpGlobals->absoluteframetime;
			m_flMeasureWorst = Max( m_flMeasureWorst, gpGlobals->absoluteframetime );
			m_nMeasureFrames++;
		}

		if ( gpGlobals->realtime < m_flNextTime )
			return;

		const float flStep = Max( 1.0f, cl_weather_tour_step.GetFloat() );

		// screenshot first, move on a moment later: the capture happens at the end of a
		// later frame and must not see the next step's camera snap (motion blur)
		if ( !m_bShotTaken && m_iStep >= 0 && m_iStep < (int)ARRAYSIZE( s_TourSteps ) && s_TourSteps[m_iStep].bScreenshot )
		{
			if ( m_nMeasureFrames > 0 )
			{
				const float flAvg = m_flMeasureSum / m_nMeasureFrames;
				Msg( "[tour] step %d \"%s\": %.2f ms avg (%.1f fps), worst %.2f ms, %d frames\n", m_iStep,
					s_TourSteps[m_iStep].pszCommands, flAvg * 1000.0f, 1.0f / Max( flAvg, 1e-4f ), m_flMeasureWorst * 1000.0f, m_nMeasureFrames );
			}
			engine->ClientCmd_Unrestricted( "jpeg" );
			m_bShotTaken = true;
			m_flNextTime = gpGlobals->realtime + 0.5f;
			return;
		}
		m_bShotTaken = false;

		m_iStep++;
		if ( m_iStep >= (int)ARRAYSIZE( s_TourSteps ) || ( cl_weather_tour_last.GetInt() >= 0 && m_iStep > cl_weather_tour_last.GetInt() ) )
		{
			m_bRunning = false;
			engine->ClientCmd_Unrestricted( "cl_drawhud 1" );
			// give the last screenshot time to be written before shutting down
			if ( m_bQuitWhenDone )
				m_flQuitTime = gpGlobals->realtime + cl_weather_tour_quit_delay.GetFloat();
			return;
		}

		engine->ClientCmd_Unrestricted( s_TourSteps[m_iStep].pszCommands );
		engine->ClientCmd_Unrestricted( VarArgs( "setang %s", cl_weather_tour_angles.GetString() ) );
		m_flAimTime = ( s_TourSteps[m_iStep].iView != TOURVIEW_DEFAULT ) ? gpGlobals->realtime + 1.0f : -1.0f;
		m_flNextTime = gpGlobals->realtime + flStep;
		m_flMeasureStart = gpGlobals->realtime + Min( 3.0f, flStep * 0.5f );
		m_flMeasureSum = 0.0f;
		m_flMeasureWorst = 0.0f;
		m_nMeasureFrames = 0;
	}

	void AimView()
	{
		if ( m_iStep < 0 || m_iStep >= (int)ARRAYSIZE( s_TourSteps ) )
			return;

		if ( s_TourSteps[m_iStep].iView == TOURVIEW_SUN )
		{
			const Vector &sun = GetWeatherSystem()->GetSunDir();
			const float flYaw = RAD2DEG( atan2f( sun.y, sun.x ) ) - 15.0f;
			const float flAlt = RAD2DEG( asinf( clamp( sun.z, -1.0f, 1.0f ) ) );
			const float flPitch = -( Clamp( flAlt, 0.0f, 50.0f ) * 0.7f + 5.0f );
			engine->ClientCmd_Unrestricted( VarArgs( "setang %.1f %.1f 0", flPitch, flYaw ) );
			if ( cl_weather_tour_debug.GetBool() )
				Msg( "[tour] aim: setang %.1f %.1f 0 (sun %.2f %.2f %.2f)\n", flPitch, flYaw, sun.x, sun.y, sun.z );
		}
	}

	bool m_bStartupDone;
	bool m_bPendingStart = false;
	bool m_bRunning;
	bool m_bQuitWhenDone;
	int m_iStep;
	float m_flNextTime;
	float m_flAimTime;
	float m_flNextDebug = 0.0f;

	float m_flMeasureStart = 0.0f;
	float m_flQuitTime = -1.0f;
	float m_flMeasureSum = 0.0f;
	float m_flMeasureWorst = 0.0f;
	int m_nMeasureFrames = 0;
	bool m_bShotTaken = false;
};

static CWeatherDevTour g_WeatherDevTour;

CON_COMMAND( cl_weather_tour, "Dev: take screenshots of every weather preset (screenshots/*.jpg)" )
{
	g_WeatherDevTour.Start( false );
}

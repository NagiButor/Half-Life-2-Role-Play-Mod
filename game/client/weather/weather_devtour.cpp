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
#include "filesystem.h"
#include "tier1/utlbuffer.h"

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
static ConVar cl_weather_tour_disconnect( "cl_weather_tour_disconnect", "1", 0, "Dev: disconnect before quitting at the end of the tour" );
static ConVar cl_weather_tour_flash_lead( "cl_weather_tour_flash_lead", "0.05", 0, "Dev: seconds between a tour step's lightning and its screenshot" );
static ConVar cl_weather_tour_script( "cl_weather_tour_script", "", 0, "Dev: run the steps of this file (cfg/<name>) instead of the built-in tour. "
	"One step per line: <flags>|<console commands>, flags: shot, sun, spin (comma separated, may be empty). "
	"A step that contains setang keeps its own angles." );

enum TourView_e
{
	TOURVIEW_DEFAULT = 0,	// cl_weather_tour_angles
	TOURVIEW_SUN,			// look toward the sun (sun in the upper left of the frame)
	TOURVIEW_SPIN,			// keep turning the camera (checks the cloud reprojection)
	TOURVIEW_MOON,			// the moon in the middle of the frame
};

struct TourStep_t
{
	const char *pszCommands;
	bool bScreenshot;
	int iView;
	int iFlash = -1;	// lightning class to strike right before the screenshot (-1 none)
};

static const TourStep_t s_BuiltinTourSteps[] =
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
	{ "sv_timecycle_set_time 15; sv_weather partlycloudy 0", true, TOURVIEW_SPIN },
	{ "sv_timecycle_set_time 15; sv_weather rain 0; r_weather_debug_view 1", true, TOURVIEW_DEFAULT },
	{ "r_weather_debug_view 2", true, TOURVIEW_DEFAULT },
	{ "r_weather_debug_view 0", false, TOURVIEW_DEFAULT },
};

// steps of the running tour: built-in or loaded from cl_weather_tour_script
static CUtlVector<TourStep_t> s_TourSteps;
static CUtlVector<char *> s_TourStrings;

static void LoadTourSteps()
{
	s_TourSteps.RemoveAll();
	for ( int i = 0; i < s_TourStrings.Count(); i++ )
		delete[] s_TourStrings[i];
	s_TourStrings.RemoveAll();

	const char *pszScript = cl_weather_tour_script.GetString();
	if ( pszScript[0] )
	{
		CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
		if ( g_pFullFileSystem->ReadFile( VarArgs( "cfg/%s", pszScript ), "MOD", buf ) )
		{
			char szLine[1024];
			while ( buf.IsValid() )
			{
				buf.GetLine( szLine, sizeof( szLine ) );
				if ( !szLine[0] && !buf.IsValid() )
					break;
				// trim
				int n = V_strlen( szLine );
				while ( n > 0 && ( szLine[n - 1] == '\n' || szLine[n - 1] == '\r' || szLine[n - 1] == ' ' || szLine[n - 1] == '\t' ) )
					szLine[--n] = 0;
				if ( !szLine[0] || szLine[0] == '#' || ( szLine[0] == '/' && szLine[1] == '/' ) )
					continue;
				char *pszBar = strchr( szLine, '|' );
				if ( !pszBar )
					continue;
				*pszBar = 0;
				TourStep_t step;
				step.bScreenshot = V_stristr( szLine, "shot" ) != NULL;
				step.iView = V_stristr( szLine, "spin" ) ? TOURVIEW_SPIN : ( V_stristr( szLine, "sun" ) ? TOURVIEW_SUN :
					( V_stristr( szLine, "moon" ) ? TOURVIEW_MOON : TOURVIEW_DEFAULT ) );
				// "flash" / "flash1" / "flash2": lightning (close / mid / far) just before the shot
				const char *pszFlash = V_stristr( szLine, "flash" );
				step.iFlash = pszFlash ? ( ( pszFlash[5] >= '0' && pszFlash[5] <= '2' ) ? pszFlash[5] - '0' : 0 ) : -1;
				const int nLen = V_strlen( pszBar + 1 ) + 1;
				char *pszCommands = new char[nLen];
				V_strncpy( pszCommands, pszBar + 1, nLen );
				s_TourStrings.AddToTail( pszCommands );
				step.pszCommands = pszCommands;
				s_TourSteps.AddToTail( step );
			}
			Msg( "[tour] %d steps from cfg/%s\n", s_TourSteps.Count(), pszScript );
			return;
		}
		Warning( "[tour] can't read cfg/%s, running the built-in tour\n", pszScript );
	}

	for ( int i = 0; i < ARRAYSIZE( s_BuiltinTourSteps ); i++ )
		s_TourSteps.AddToTail( s_BuiltinTourSteps[i] );
}

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
		LoadTourSteps();
		m_bRunning = true;
		m_bQuitWhenDone = bQuitWhenDone;
		m_iStep = cl_weather_tour_first.GetInt() - 1;
		m_bShotTaken = true;	// no screenshot of the state before the first step
		m_flNextTime = gpGlobals->realtime + 6.0f;	// let the level settle (clouds accumulate, shaders load)
		engine->ClientCmd_Unrestricted( "sv_cheats 1; cl_drawhud 0; sv_weather_auto 0; sv_timecycle_set_speed 0; jpeg_quality 92" );
		// (motion blur used to be switched off here: a material system setting, and every map
		// change of a tour then applied a changed config - the shots are taken seconds after
		// the camera snaps anyway)
		m_iSavedMotionBlur = -1;
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
			// leave the map first, then quit from the menu like a player would
			if ( engine->IsInGame() && cl_weather_tour_disconnect.GetBool() )
			{
				engine->ClientCmd_Unrestricted( "disconnect\n" );
				m_flQuitTime = gpGlobals->realtime + 2.0f;
			}
			else
			{
				m_flQuitTime = -1.0f;
				engine->ClientCmd_Unrestricted( "quit\n" );
			}
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

		// keep turning for the reprojection check
		if ( m_iStep >= 0 && m_iStep < s_TourSteps.Count() && s_TourSteps[m_iStep].iView == TOURVIEW_SPIN )
		{
			QAngle ang;
			engine->GetViewAngles( ang );
			ang.x = -20.0f;
			ang.y += 40.0f * gpGlobals->absoluteframetime;
			engine->SetViewAngles( ang );
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

		// lightning a moment before the screenshot
		if ( !m_bFlashFired && !m_bShotTaken && m_iStep >= 0 && m_iStep < s_TourSteps.Count() && s_TourSteps[m_iStep].iFlash >= 0 &&
			gpGlobals->realtime >= m_flNextTime - cl_weather_tour_flash_lead.GetFloat() )
		{
			m_bFlashFired = true;
			engine->ClientCmd_Unrestricted( VarArgs( "cl_weather_lightning %d", s_TourSteps[m_iStep].iFlash ) );
		}

		if ( gpGlobals->realtime < m_flNextTime )
			return;

		const float flStep = Max( 1.0f, cl_weather_tour_step.GetFloat() );

		// screenshot first, move on a moment later: the capture happens at the end of a
		// later frame and must not see the next step's camera snap (motion blur)
		if ( !m_bShotTaken && m_iStep >= 0 && m_iStep < s_TourSteps.Count() && s_TourSteps[m_iStep].bScreenshot )
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
		if ( m_iStep >= s_TourSteps.Count() || ( cl_weather_tour_last.GetInt() >= 0 && m_iStep > cl_weather_tour_last.GetInt() ) )
		{
			m_bRunning = false;
			engine->ClientCmd_Unrestricted( "cl_drawhud 1" );
			static ConVarRef mat_motion_blur_enabled( "mat_motion_blur_enabled" );
			if ( mat_motion_blur_enabled.IsValid() && m_iSavedMotionBlur >= 0 )
				mat_motion_blur_enabled.SetValue( m_iSavedMotionBlur );
			// give the last screenshot time to be written before shutting down
			if ( m_bQuitWhenDone )
				m_flQuitTime = gpGlobals->realtime + cl_weather_tour_quit_delay.GetFloat();
			return;
		}

		engine->ClientCmd_Unrestricted( s_TourSteps[m_iStep].pszCommands );
		if ( !V_stristr( s_TourSteps[m_iStep].pszCommands, "setang" ) )
			engine->ClientCmd_Unrestricted( VarArgs( "setang %s", cl_weather_tour_angles.GetString() ) );
		m_flAimTime = ( s_TourSteps[m_iStep].iView != TOURVIEW_DEFAULT ) ? gpGlobals->realtime + 1.0f : -1.0f;
		m_flNextTime = gpGlobals->realtime + flStep;
		m_flMeasureStart = gpGlobals->realtime + Min( 3.0f, flStep * 0.5f );
		m_flMeasureSum = 0.0f;
		m_flMeasureWorst = 0.0f;
		m_nMeasureFrames = 0;
		m_bFlashFired = false;
	}

	void AimView()
	{
		if ( m_iStep < 0 || m_iStep >= s_TourSteps.Count() )
			return;

		if ( s_TourSteps[m_iStep].iView == TOURVIEW_MOON )
		{
			const Vector &moon = GetWeatherSystem()->GetMoonDir();
			const float flYaw = RAD2DEG( atan2f( moon.y, moon.x ) );
			const float flAlt = RAD2DEG( asinf( clamp( moon.z, -1.0f, 1.0f ) ) );
			engine->ClientCmd_Unrestricted( VarArgs( "setang %.1f %.1f 0", -flAlt, flYaw ) );
		}
		else if ( s_TourSteps[m_iStep].iView == TOURVIEW_SUN )
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
	int m_iSavedMotionBlur = -1;
	float m_flMeasureSum = 0.0f;
	float m_flMeasureWorst = 0.0f;
	int m_nMeasureFrames = 0;
	bool m_bShotTaken = false;
	bool m_bFlashFired = false;
};

static CWeatherDevTour g_WeatherDevTour;

CON_COMMAND( cl_weather_tour, "Dev: take screenshots of every weather preset (screenshots/*.jpg)" )
{
	g_WeatherDevTour.Start( false );
}

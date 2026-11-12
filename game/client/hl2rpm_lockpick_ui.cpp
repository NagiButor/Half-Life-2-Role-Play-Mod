//========= HL2RPM ============================================================//
//
// The lockpicking minigame like in Fallout (M6.5; the server half is
// server/hl2rpm_lockpick.cpp). The face of the lock: the bobby pin moves with the
// mouse (or the arrows) over the upper half, holding A / D / the left button turns
// the cylinder with the tension wrench. The cylinder turns all the way only when the
// pin is in the sweet spot; elsewhere it stops part way, the pin bends and breaks
// (one bobby pin spent). F - force the lock (a chance, else it jams for good).
//
// rpm_lockpick_ui <token> <level> <skill> <sweet> <half> <hold> <pins> <force%> "<name>" "<difficulty>"
//   (from the server) opens it; rpm_lockpick_pins <token> <left> - the count after a break.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include "iclientmode.h"
#include "engine/IEngineSound.h"
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui_controls/Panel.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

static void PlaySnd( const char *s, float vol = 1.0f, int pitch = 100 )
{
	enginesound->EmitAmbientSound( s, vol, pitch );
}

class CRPMLockpickWindow : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMLockpickWindow, Panel );
public:
	enum State_t { ST_PLAY, ST_BROKEN, ST_OPEN, ST_OUT };

	CRPMLockpickWindow( Panel *parent ) : BaseClass( parent, "RPMLockpickWindow" )
	{
		SetPaintBackgroundEnabled( false );
		SetVisible( false );
		m_iToken = 0;
	}

	virtual void ApplySchemeSettings( IScheme *s )
	{
		BaseClass::ApplySchemeSettings( s );
		LoadFonts( s );
	}

	void Open( int token, int level, int skill, float sweet, float half, float hold, int pins, int force, const char *name, const char *diff )
	{
		m_iToken = token;
		m_iLevel = level;
		m_iSkill = skill;
		m_flSweet = sweet;
		m_flHalf = half;
		m_flHold = Max( 0.3f, hold );
		m_iPins = pins;
		m_iForce = force;
		m_Name = name;
		m_Diff = diff;
		NewPin();
		m_flPin = 90.0f;
		m_iState = ST_PLAY;
		m_flStateTime = m_flOpenTime = gpGlobals->realtime;
		m_bKeyTurn = m_bMouseTurn = m_bLeft = m_bRight = false;
		m_flLastThink = gpGlobals->realtime;
		m_flLastTick = 0;
		m_flTickPin = m_flPin;
		if ( !IsVisible() )
		{
			if ( g_pClientMode && g_pClientMode->GetViewport() && GetParent() != g_pClientMode->GetViewport() )
				SetParent( g_pClientMode->GetViewport() );
			int sw, sh;
			surface()->GetScreenSize( sw, sh );
			SetBounds( 0, 0, sw, sh );
			SetVisible( true );
			MakePopup();
			SetZPos( 1600 );
			SetKeyBoardInputEnabled( true );
			SetMouseInputEnabled( true );
			MoveToFront();
			RequestFocus();
			SetCursor( dc_none );
			// the pin starts in the middle: put the cursor there
			input()->SetCursorPos( sw / 2, sh / 2 );
			engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
		}
		PlaySnd( "physics/metal/metal_canister_impact_soft1.wav", 0.6f, 120 );
	}

	void Close( const char *report )
	{
		if ( report && m_iToken )
			engine->ClientCmd_Unrestricted( CFmtStr( "rpm_lockpick_report %d %s\n", m_iToken, report ) );
		m_iToken = 0;
		if ( !IsVisible() )
			return;
		SetVisible( false );
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( false );
		engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
	}

	void SetPins( int token, int left )
	{
		if ( token != m_iToken )
			return;
		m_iPins = left;
	}

	//---------------------------------------------------------------------
	void NewPin()
	{
		m_flStress = 0.0f;
		m_flTurn = 0.0f;
		m_flShake = 0.0f;
	}

	// how far the cylinder may turn (0..1) with the pin at this angle
	float Allowed( float pin ) const
	{
		float d = fabsf( pin - m_flSweet );
		if ( d <= m_flHalf )
			return 1.0f;
		const float partial = 34.0f;
		float a = 1.0f - ( d - m_flHalf ) / partial;
		return Max( 0.04f, 0.85f * clamp( a, 0.0f, 1.0f ) );
	}

	bool Turning() const { return m_bKeyTurn || m_bMouseTurn; }

	virtual void OnThink()
	{
		float now = gpGlobals->realtime;
		float dt = clamp( now - m_flLastThink, 0.0f, 0.1f );
		m_flLastThink = now;

		if ( m_iState == ST_OPEN )
		{
			m_flTurn = Min( 1.0f, m_flTurn + dt * 3.0f );
			if ( now - m_flStateTime > 0.7f )
				Close( "open" );
			return;
		}
		if ( m_iState == ST_OUT )
		{
			if ( now - m_flStateTime > 1.6f )
				Close( NULL );
			return;
		}
		if ( m_iState == ST_BROKEN )
		{
			m_flTurn = Max( 0.0f, m_flTurn - dt * 4.0f );
			if ( now - m_flStateTime > 0.7f )
			{
				if ( m_iPins <= 0 )
				{
					m_iState = ST_OUT;
					m_flStateTime = now;
					PlaySnd( "doors/default_locked.wav", 0.7f );
				}
				else
				{
					m_iState = ST_PLAY;
					NewPin();
					PlaySnd( "physics/metal/metal_canister_impact_soft2.wav", 0.6f, 125 );
				}
			}
			return;
		}

		// the pin: arrows
		if ( m_bLeft || m_bRight )
			m_flPin = clamp( m_flPin + ( m_bRight ? 70.0f : -70.0f ) * dt * ( Turning() ? 0.0f : 1.0f ), 0.0f, 180.0f );
		// a soft tick every few degrees the pin moves (only while the lock is at rest)
		if ( fabsf( m_flPin - m_flTickPin ) > 9.0f && now - m_flLastTick > 0.06f && m_flTurn < 0.02f )
		{
			m_flTickPin = m_flPin;
			m_flLastTick = now;
			PlaySnd( "physics/metal/metal_computer_impact_soft1.wav", 0.12f, 160 + RandomInt( 0, 30 ) );
		}

		float allowed = Allowed( m_flPin );
		if ( Turning() )
		{
			if ( m_flTurn < 0.01f && m_flShake <= 0.0f )
				PlaySnd( "doors/door_latch3.wav", 0.25f, 140 );
			if ( m_flTurn < allowed )
			{
				m_flTurn = Min( allowed, m_flTurn + dt * 1.4f );
				m_flShake = Max( 0.0f, m_flShake - dt * 4.0f );
			}
			else if ( allowed < 1.0f )
			{
				// stuck: the pin bends
				m_flStress += dt / m_flHold;
				m_flShake = Min( 1.0f, m_flShake + dt * 6.0f );
				if ( now - m_flLastTick > 0.16f )
				{
					m_flLastTick = now;
					PlaySnd( "physics/metal/metal_solid_impact_soft2.wav", 0.35f, 120 + (int)( m_flStress * 60 ) + RandomInt( -6, 6 ) );
				}
				if ( m_flStress >= 1.0f )
				{
					m_iState = ST_BROKEN;
					m_flStateTime = now;
					m_iPins = Max( 0, m_iPins - 1 );
					PlaySnd( "physics/metal/metal_computer_impact_bullet2.wav", 0.8f, 130 );
					if ( m_iToken )
						engine->ClientCmd_Unrestricted( CFmtStr( "rpm_lockpick_report %d broke\n", m_iToken ) );
				}
			}
			if ( m_flTurn >= 0.999f )
			{
				m_iState = ST_OPEN;
				m_flStateTime = now;
				PlaySnd( "doors/door_latch1.wav", 0.9f );
			}
		}
		else
		{
			if ( m_flTurn > 0.05f && m_flTurn - dt * 3.5f <= 0.05f )
				PlaySnd( "physics/metal/metal_canister_impact_soft3.wav", 0.25f, 140 );
			m_flTurn = Max( 0.0f, m_flTurn - dt * 3.5f );
			m_flShake = Max( 0.0f, m_flShake - dt * 5.0f );
		}
	}

	//---------------------------------------------------------------------
	virtual void OnCursorMoved( int x, int y )
	{
		if ( m_iState != ST_PLAY || Turning() )
			return;
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		// the screen from 15 % to 85 % = the pin from 0 to 180 degrees
		float f = ( (float)x / Max( 1, sw ) - 0.15f ) / 0.7f;
		m_flPin = clamp( f, 0.0f, 1.0f ) * 180.0f;
	}

	virtual void OnMousePressed( MouseCode code )
	{
		if ( code == MOUSE_LEFT )
			m_bMouseTurn = true;
		else if ( code == MOUSE_RIGHT )
			Close( "cancel" );
	}

	virtual void OnMouseReleased( MouseCode code )
	{
		if ( code == MOUSE_LEFT )
			m_bMouseTurn = false;
	}

	virtual void OnKeyCodePressed( KeyCode code )
	{
		switch ( code )
		{
		case KEY_A: case KEY_D: case KEY_SPACE: m_bKeyTurn = true; return;
		case KEY_LEFT: m_bLeft = true; return;
		case KEY_RIGHT: m_bRight = true; return;
		case KEY_F:
			if ( m_iState == ST_PLAY )
			{
				PlaySnd( "physics/metal/metal_grenade_impact_hard2.wav", 0.8f, 90 );
				Close( "force" );
			}
			return;
		case KEY_TAB: case KEY_E:
			Close( "cancel" );
			return;
		default:
			break;
		}
		BaseClass::OnKeyCodePressed( code );
	}

	virtual void OnKeyCodeReleased( KeyCode code )
	{
		switch ( code )
		{
		case KEY_A: case KEY_D: case KEY_SPACE: m_bKeyTurn = false; return;
		case KEY_LEFT: m_bLeft = false; return;
		case KEY_RIGHT: m_bRight = false; return;
		default:
			break;
		}
		BaseClass::OnKeyCodeReleased( code );
	}

	virtual void OnKeyCodeTyped( KeyCode code )
	{
		if ( code == KEY_ESCAPE )
		{
			Close( "cancel" );
			engine->ClientCmd_Unrestricted( "gameui_hide\n" );
			engine->ClientCmd_Unrestricted( "wait;gameui_hide\n" );
			return;
		}
		BaseClass::OnKeyCodeTyped( code );
	}

	// test hook (rpm_lockpick_key): key name, "pin <deg>"
	void Simulate( const char *what, const char *arg )
	{
		if ( !Q_stricmp( what, "pin" ) )
			m_flPin = clamp( (float)atof( arg ), 0.0f, 180.0f );
		else if ( !Q_stricmp( what, "off" ) )	// <deg> away from the sweet spot (towards the middle)
			m_flPin = clamp( m_flSweet + ( m_flSweet > 90.0f ? -1.0f : 1.0f ) * (float)atof( arg ), 0.0f, 180.0f );
		else if ( !Q_stricmp( what, "turn" ) )
			m_bKeyTurn = atoi( arg ) != 0;
		else if ( !Q_stricmp( what, "force" ) )
			OnKeyCodePressed( KEY_F );
		else if ( !Q_stricmp( what, "esc" ) )
			OnKeyCodeTyped( KEY_ESCAPE );
	}

	//---------------------------------------------------------------------
	virtual void Paint()
	{
		const Palette_t &p = Pal();
		int sw = GetWide(), sh = GetTall();
		float now = gpGlobals->realtime;
		float a = Ramp( m_flOpenTime, 0.25f );

		// the game stays visible behind, darkened
		Rect( 0, 0, sw, sh, Color( 0, 0, 0, (int)( 150 * a ) ) );

		int w = Sc( 600 ), h = Sc( 660 );
		int x = ( sw - w ) / 2, y = ( sh - h ) / 2 + (int)( ( 1.0f - a ) * Sc( 20 ) );
		Plate( x, y, w, h, (int)( 240 * a ) );
		FrameBox( x, y, w, h, -1, (int)( 255 * a ) );

		// header
		int hx = x + Sc( 28 ), hy = y + Sc( 20 );
		Spaced( F_TAB, hx, hy, Fade( p.accent, a ), TR( "Lockpick", "Взлом замка" ), Sc( 3 ), true );
		Text( F_TEXT, x + w - Sc( 28 ), hy + Sc( 4 ), Fade( p.text, a ), m_Name.Get(), false, 2, w / 2 );
		hy += Sc( 40 );
		Line( hx, hy, w - Sc( 56 ), Fade( p.faint, a ) );
		hy += Sc( 10 );
		bool bLow = m_iSkill < m_iLevel;
		Text( F_SMALL, hx, hy, Fade( p.dim, a ), CFmtStr( "%s: %s (%d)", TR( "Difficulty", "Сложность" ), m_Diff.Get(), m_iLevel ) );
		Text( F_SMALL, x + w - Sc( 28 ), hy, Fade( bLow ? p.bad : p.good, a ), CFmtStr( "%s %d", TR( "Lockpick", "Взлом" ), m_iSkill ), false, 2 );

		// the lock
		float cx = x + w * 0.5f, cy = y + Sc( 330 );
		float R = (float)Sc( 170 );
		PaintLock( cx, cy, R, a, now );

		// state line
		int ly = (int)( cy + R + Sc( 34 ) );
		const char *state = NULL;
		Color sc = p.accent;
		if ( m_iState == ST_OPEN )
			state = TR( "UNLOCKED", "ОТКРЫТО" ), sc = p.good;
		else if ( m_iState == ST_BROKEN )
			state = TR( "The pin broke", "Шпилька сломалась" ), sc = p.bad;
		else if ( m_iState == ST_OUT )
			state = TR( "No bobby pins left", "Шпильки кончились" ), sc = p.bad;
		else if ( m_flShake > 0.3f )
			state = TR( "The pin is bending!", "Шпилька гнётся!" ), sc = Fade( p.bad, 0.6f + 0.4f * sinf( now * 20.0f ) );
		if ( state )
			Spaced( F_TEXT, (int)cx, ly, Fade( sc, a ), state, Sc( 2 ), true, 1 );
		ly += Sc( 36 );

		// pins and force
		Text( F_TEXT, hx, ly, Fade( p.text, a ), CFmtStr( "%s: %d", TR( "Bobby pins", "Шпильки" ), m_iPins ) );
		Text( F_TEXT, x + w - Sc( 28 ), ly, Fade( p.dim, a ), CFmtStr( "[F] %s %d%%", TR( "Force", "Силой" ), m_iForce ), false, 2 );
		ly += Sc( 34 );
		Line( hx, ly, w - Sc( 56 ), Fade( p.faint, a ) );
		ly += Sc( 10 );
		Wrap( F_SMALL, hx, ly, w - Sc( 56 ), Fade( p.grey, a ),
			TR( "Mouse or arrows - move the pin. Hold A / D / left button - turn the lock. Esc - leave.",
				"Мышь или стрелки — шпилька. Удерживайте A / D / левую кнопку — поворот замка. Esc — выйти." ), 2 );
	}

	void PaintLock( float cx, float cy, float R, float a, float now )
	{
		const Palette_t &p = Pal();
		// the turn of the cylinder: clockwise up to 90 degrees, shaking when stuck
		float shake = m_flShake * ( sinf( now * 61.0f ) * 1.6f + sinf( now * 37.0f ) * 0.9f );
		float rot = m_flTurn * 90.0f + shake;
		float rr = DEG2RAD( rot );

		// body: brass-dark disc with a rim
		Disc( cx, cy, R * 1.08f, Color( 24, 22, 18, (int)( 255 * a ) ), 56 );
		Ring( cx, cy, R * 1.02f, R * 1.08f, Fade( p.dim, a * 0.8f ), 56 );
		Ring( cx, cy, R * 0.9f, R * 0.93f, Fade( p.faint, a ), 56 );
		// the pin's scale over the upper half (0..180)
		for ( int i = 0; i <= 12; i++ )
		{
			float t = DEG2RAD( 180.0f + i * 15.0f );
			float r0 = R * ( i % 3 ? 0.95f : 0.94f ), r1 = R * 1.0f;
			ThickLine( cx + cosf( t ) * r0, cy + sinf( t ) * r0, cx + cosf( t ) * r1, cy + sinf( t ) * r1, i % 3 ? 1.0f : 2.0f, Fade( p.faint, a * 1.6f ) );
		}

		// the cylinder
		float cr = R * 0.62f;
		Disc( cx, cy, cr, Color( 46, 41, 32, (int)( 255 * a ) ), 48 );
		Ring( cx, cy, cr * 0.96f, cr, Fade( p.accent, a * 0.55f ), 48 );
		// pin tumbler marks turning with it
		for ( int i = 0; i < 5; i++ )
		{
			float t = rr + DEG2RAD( -90.0f + ( i - 2 ) * 16.0f );
			Disc( cx + cosf( t ) * cr * 0.8f, cy + sinf( t ) * cr * 0.8f, cr * 0.05f, Fade( p.dim, a * 0.7f ), 10 );
		}
		// the keyhole: a round top and a slot widening downwards, turning with the cylinder
		float ux = -sinf( rr ), uy = cosf( rr );		// "down" of the cylinder
		float vx = cosf( rr ), vy = sinf( rr );			// its "right"
		Color hole( 6, 6, 5, (int)( 255 * a ) );
		Disc( cx - ux * cr * 0.12f, cy - uy * cr * 0.12f, cr * 0.13f, hole, 20 );
		Quad( cx - ux * cr * 0.1f - vx * cr * 0.06f, cy - uy * cr * 0.1f - vy * cr * 0.06f, cx - ux * cr * 0.1f + vx * cr * 0.06f, cy - uy * cr * 0.1f + vy * cr * 0.06f,
			cx + ux * cr * 0.34f + vx * cr * 0.1f, cy + uy * cr * 0.34f + vy * cr * 0.1f, cx + ux * cr * 0.34f - vx * cr * 0.1f, cy + uy * cr * 0.34f - vy * cr * 0.1f, hole );

		// the tension wrench: from the bottom of the keyhole down and out, an L, turning with the cylinder
		Color steel( 150, 150, 158, (int)( 255 * a ) );
		float wx0 = cx + ux * cr * 0.26f, wy0 = cy + uy * cr * 0.26f;
		float wx1 = cx + ux * R * 1.2f, wy1 = cy + uy * R * 1.2f;
		ThickLine( wx0, wy0, wx1, wy1, R * 0.06f, steel );
		ThickLine( wx1 - vx * R * 0.03f, wy1 - vy * R * 0.03f, wx1 + vx * R * 0.3f, wy1 + vy * R * 0.3f, R * 0.06f, steel );
		ThickLine( wx0 + vx * 1.0f, wy0 + vy * 1.0f, wx1 + vx * 1.0f, wy1 + vy * 1.0f, 1.5f, Color( 220, 220, 228, (int)( 160 * a ) ) );

		// the bobby pin (gone for a moment when it broke)
		if ( m_iState == ST_OUT )
			return;
		float brokeT = m_iState == ST_BROKEN ? clamp( ( now - m_flStateTime ) / 0.5f, 0.0f, 1.0f ) : 0.0f;
		float pinA = m_flPin + ( m_flShake > 0.0f ? sinf( now * 53.0f ) * m_flShake * 2.5f : 0.0f );
		float t = DEG2RAD( 180.0f + pinA );
		float dx = cosf( t ), dy = sinf( t );
		float nx = -dy, ny = dx;
		// bending: the tip curls more with stress
		float bend = m_flStress * 0.22f;
		int stressR = (int)( 210 + 45 * m_flStress ), stressG = (int)( 205 - 130 * m_flStress ), stressB = (int)( 190 - 140 * m_flStress );
		Color pin( stressR, stressG, stressB, (int)( 255 * a * ( 1.0f - brokeT ) ) );
		float r0 = R * 0.05f, r1 = R * 1.3f;
		if ( m_iState == ST_BROKEN )
		{
			// the broken half falls away
			float fall = brokeT * R * 0.6f;
			ThickLine( cx + dx * r0, cy + dy * r0, cx + dx * R * 0.5f, cy + dy * R * 0.5f, R * 0.035f, pin );
			ThickLine( cx + dx * R * 0.6f, cy + dy * R * 0.6f + fall, cx + dx * r1, cy + dy * r1 + fall * 1.4f, R * 0.035f, pin );
			return;
		}
		float mx = cx + dx * R * 0.25f + nx * bend * R, my = cy + dy * R * 0.25f + ny * bend * R;
		ThickLine( cx + dx * r0 + nx * bend * R * 1.6f, cy + dy * r0 + ny * bend * R * 1.6f, mx, my, R * 0.035f, pin );
		ThickLine( mx, my, cx + dx * r1, cy + dy * r1, R * 0.035f, pin );
		// the bent tail (a bobby pin's wavy end) outside
		ThickLine( cx + dx * r1, cy + dy * r1, cx + dx * ( r1 + R * 0.08f ) + nx * R * 0.05f, cy + dy * ( r1 + R * 0.08f ) + ny * R * 0.05f, R * 0.035f, pin );
		ThickLine( cx + dx * R * 0.6f + nx * 1.0f, cy + dy * R * 0.6f + ny * 1.0f, cx + dx * r1 + nx * 1.0f, cy + dy * r1 + ny * 1.0f, 1.0f, Color( 255, 255, 255, (int)( 120 * a ) ) );
	}

private:
	int m_iToken, m_iLevel, m_iSkill, m_iPins, m_iForce;
	float m_flSweet, m_flHalf, m_flHold;
	CUtlString m_Name, m_Diff;
	int m_iState;
	float m_flStateTime, m_flOpenTime, m_flLastThink, m_flLastTick, m_flTickPin;
	float m_flPin, m_flTurn, m_flStress, m_flShake;
	bool m_bKeyTurn, m_bMouseTurn, m_bLeft, m_bRight;
};

static CRPMLockpickWindow *s_pLockpick = NULL;

static CRPMLockpickWindow *Window()
{
	if ( !s_pLockpick )
	{
		s_pLockpick = new CRPMLockpickWindow( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
		s_pLockpick->InvalidateLayout( false, true );
	}
	return s_pLockpick;
}

bool RPMLockpick_IsOpen()
{
	return s_pLockpick && s_pLockpick->IsVisible();
}

CON_COMMAND_F( rpm_lockpick_ui, "HL2RPM (internal): open the lockpicking minigame (sent by the server)", FCVAR_CLIENTDLL )
{
	if ( args.ArgC() < 11 )
		return;
	Window()->Open( atoi( args[1] ), atoi( args[2] ), atoi( args[3] ), (float)atof( args[4] ), (float)atof( args[5] ), (float)atof( args[6] ),
		atoi( args[7] ), atoi( args[8] ), args[9], args[10] );
}

CON_COMMAND_F( rpm_lockpick_pins, "HL2RPM (internal): bobby pins left (sent by the server)", FCVAR_CLIENTDLL )
{
	if ( args.ArgC() < 3 || !s_pLockpick )
		return;
	s_pLockpick->SetPins( atoi( args[1] ), atoi( args[2] ) );
}

CON_COMMAND_F( rpm_lockpick_key, "HL2RPM: drive the lockpicking minigame from the console (tests): pin <deg> | off <deg from the sweet spot> | turn 0|1 | force | esc", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 || !RPMLockpick_IsOpen() )
		return;
	s_pLockpick->Simulate( args[1], args.ArgC() > 2 ? args[2] : "" );
}

//========= HL2RPM ============================================================//
//
// The HUD of a suit. A suit is what gives the player numbers at all: without one there
// is only the pulse (hud_vitals.cpp) and how the world looks. In one, this panel takes
// the place of HudHealth, HudSuit, HudAmmo, HudAmmoSecondary, HudSuitPower, HudFlashlight
// and HudPosture (cut out in hud_redraw.cpp), in the look of the other windows:
//
//   left - the name of the suit, health and armor, what draws on its power;
//   right - the weapon in the hands, the magazine, what is left, the second fire.
//
// Colors come from the theme of the suit (hl2rpm_ui.h).
//
// Work suits (the coverall of a Combine worker, the hazmat suit - themes worker / hazmat) are no
// combat suits, and their HUD does not pretend to be one: a cheap device clipped to the visor -
// a hazard-striped strip, an LCD in one colour with the ghosts of its unlit segments, segment bars,
// the time of the shift; the hazmat suit counts the dose (what the Geiger counter hears), the air
// and the filter. The weapon is "what is in hand".
//
//   cl_rpm_suithud 0 - nothing of it
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "iclientvehicle.h"
#include "ammodef.h"
#include "c_basehlplayer.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include "tier1/fmtstr.h"
#include "timecycle/env_timecycle.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

extern bool HL2RPM_IsDialogOpen();
extern bool RPMMenu_IsOpen();
extern bool RPMLoot_IsOpen();
extern bool RPMLockpick_IsOpen();
extern bool RPMSuitBoot_IsShowing();
extern int RPMHud_GetBattery();
extern int RPMHud_GeigerRange();

static ConVar cl_rpm_suithud( "cl_rpm_suithud", "1", FCVAR_ARCHIVE, "HL2RPM: the HUD of a suit" );

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

class CHudRPMSuit : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMSuit, Panel );
public:
	CHudRPMSuit( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMSuit" )
	{
		SetParent( g_pClientMode->GetViewport() );
		SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
		m_iLastHealth = m_iLastArmor = -1;
		m_flHealthHit = m_flArmorHit = -100.0f;
		m_hMono = m_hMonoBig = INVALID_FONT;
	}

	virtual void LevelInit()
	{
		m_iLastHealth = m_iLastArmor = -1;
	}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		LoadFonts( pScheme );
		m_hMono = pScheme->GetFont( "RPMUI_Mono", false );
		m_hMonoBig = pScheme->GetFont( "RPMUI_MonoBig", false );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
		int w, h;
		surface()->GetScreenSize( w, h );
		SetBounds( 0, 0, w, h );
	}

	virtual bool ShouldDraw()
	{
		return cl_rpm_suithud.GetBool() && CHudElement::ShouldDraw();
	}

	virtual void Paint()
	{
		C_BaseHLPlayer *pPlayer = dynamic_cast<C_BaseHLPlayer *>( C_BasePlayer::GetLocalPlayer() );
		if ( !pPlayer || RPMSuitBoot_IsShowing() || HL2RPM_IsDialogOpen() || RPMMenu_IsOpen() || RPMLoot_IsOpen() || RPMLockpick_IsOpen() )
			return;
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		if ( IsWorkTheme( CurrentTheme() ) )
		{
			PaintWorkStatus( pPlayer, sh );
			PaintWorkTool( pPlayer, sw, sh );
			return;
		}
		PaintStatus( pPlayer, sh );
		PaintWeapon( pPlayer, sw, sh );
	}

private:
	// a value and its bar; the color warns near the end and after a hit
	void Gauge( int x, int y, int w, const char *label, int value, int maxValue, float hit, bool bLowWarn )
	{
		const Palette_t &p = Pal();
		const float flHit = clamp( 1.0f - ( gpGlobals->realtime - hit ) / 0.6f, 0.0f, 1.0f );
		const float frac = maxValue > 0 ? clamp( value / (float)maxValue, 0.0f, 1.0f ) : 0.0f;
		const bool bLow = bLowWarn && frac <= 0.25f;
		const float flPulse = bLow ? 0.65f + 0.35f * sin( gpGlobals->realtime * 6.0f ) : 1.0f;
		Color c = bLow ? p.bad : p.accent;
		if ( flHit > 0.0f )
			c = Color( (int)Lerp( flHit, (float)c.r(), 255.0f ), (int)Lerp( flHit, (float)c.g(), (float)p.bad.g() ), (int)Lerp( flHit, (float)c.b(), (float)p.bad.b() ), 255 );
		Spaced( F_SMALL, x, y, Fade( p.dim, 0.9f ), label, Sc( 2 ) );
		// (the digits of HudNumbers sit 15 pixels below the top of the font and are 38 tall)
		Text( F_NUM, x, y + Sc( 1 ), Fade( c, flPulse ), CFmtStr( "%d", value ), flHit > 0.0f );
		Bar( x, y + Sc( 59 ), w, Max( 2, Sc( 4 ) ), frac, Fade( c, flPulse ) );
	}

	void PaintStatus( C_BaseHLPlayer *pPlayer, int sh )
	{
		const Palette_t &p = Pal();
		const int health = Max( pPlayer->GetHealth(), 0 ), armor = RPMHud_GetBattery();
		if ( m_iLastHealth >= 0 && health < m_iLastHealth )
			m_flHealthHit = gpGlobals->realtime;
		if ( m_iLastArmor >= 0 && armor < m_iLastArmor )
			m_flArmorHit = gpGlobals->realtime;
		m_iLastHealth = health;
		m_iLastArmor = armor;

		const float flPower = pPlayer->m_HL2Local.m_flSuitPower;
		const float flTorch = pPlayer->m_HL2Local.m_flFlashBattery;
		const bool bTorchOn = pPlayer->IsEffectActive( EF_DIMLIGHT );
		const bool bPower = flPower < 100.0f;
		const bool bTorch = flTorch >= 0.0f && ( bTorchOn || flTorch < 100.0f );

		const int w = Sc( 300 ), h = Sc( 108 ) + ( bPower ? Sc( 12 ) : 0 );
		const int x = Sc( 24 ), y = sh - Sc( 24 ) - h;
		Plate( x, y, w, h, 205, Sc( 6 ) );

		// the name of the suit, what is switched on
		Spaced( F_SMALL, x + Sc( 14 ), y + Sc( 9 ), p.dim, SuitName(), Sc( 2 ) );
		if ( bTorch )
			Text( F_SMALL, x + w - Sc( 14 ), y + Sc( 9 ), bTorchOn ? p.accent : p.dim, CFmtStr( "%s %d%%", TR( "LIGHT", "ФОНАРЬ" ), (int)flTorch ), false, 2 );

		const int gy = y + Sc( 28 ), gw = ( w - Sc( 44 ) ) / 2;
		Gauge( x + Sc( 14 ), gy, gw, TR( "HEALTH", "ЗДОРОВЬЕ" ), health, Max( pPlayer->GetMaxHealth(), 1 ), m_flHealthHit, true );
		Gauge( x + Sc( 30 ) + gw, gy, gw, TR( "ARMOR", "БРОНЯ" ), armor, 100, m_flArmorHit, false );

		if ( bPower )
		{
			const int by = y + h - Sc( 14 );
			const bool bLow = flPower < 20.0f;
			Bar( x + Sc( 14 ), by, w - Sc( 28 ), Max( 2, Sc( 3 ) ), flPower / 100.0f, bLow ? p.bad : p.dim );
		}
	}

	const char *SuitName()
	{
		switch ( CurrentTheme() )
		{
		case THEME_HEV: return "H.E.V.";
		case THEME_PCV: return "P.C.V.";
		case THEME_COMBINE: return TR( "OVERWATCH", "НАДЗОР" );
		case THEME_ELITE: return TR( "OVERWATCH ELITE", "ЭЛИТА НАДЗОРА" );
		case THEME_WORKER: return TR( "WORKER", "РАБОЧИЙ" );
		case THEME_HAZMAT: return TR( "HAZMAT", "ХИМЗАЩИТА" );
		}
		return "";
	}

	void PaintWeapon( C_BaseHLPlayer *pPlayer, int sw, int sh )
	{
		const Palette_t &p = Pal();
		int clip = -1, ammo = -1, clip2 = -1, ammo2 = -1;
		CUtlString name;

		IClientVehicle *pVehicle = pPlayer->GetVehicle();
		C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
		if ( pVehicle )
		{
			if ( pVehicle->GetPrimaryAmmoType() < 0 )
				return;
			clip = pVehicle->GetPrimaryAmmoClip();
			ammo = pVehicle->GetPrimaryAmmoCount();
		}
		else if ( pWeapon )
		{
			name = RPMItems::Name( pWeapon->GetName() );
			if ( pWeapon->UsesPrimaryAmmo() )
			{
				ammo = pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() );
				if ( pWeapon->UsesClipsForAmmo1() )
					clip = pWeapon->Clip1();
			}
			if ( pWeapon->UsesSecondaryAmmo() )
			{
				ammo2 = pPlayer->GetAmmoCount( pWeapon->GetSecondaryAmmoType() );
				if ( pWeapon->UsesClipsForAmmo2() )
					clip2 = pWeapon->Clip2();
			}
		}
		else
		{
			return;		// nothing in the hands: nothing to show
		}

		const bool bAmmo = clip >= 0 || ammo >= 0;
		const int w = Sc( 240 ), h = bAmmo ? Sc( 84 ) : Sc( 34 );
		const int x = sw - Sc( 24 ) - w, y = sh - Sc( 24 ) - h;
		Plate( x, y, w, h, 205, Sc( 6 ) );
		Text( F_SMALL, x + Sc( 14 ), y + Sc( 9 ), p.dim, name.Get(), false, 0, w - Sc( 28 ) );
		if ( !bAmmo )
			return;

		const int shown = clip >= 0 ? clip : ammo;
		const int ny = y + Sc( 14 );
		const bool bEmpty = shown == 0;
		Color c = bEmpty ? p.bad : p.accent;
		const int numW = Text( F_NUM, x + Sc( 14 ), ny, c, CFmtStr( "%d", shown ), false );
		if ( clip >= 0 )
			Text( F_TEXT, x + Sc( 22 ) + numW, ny + Sc( 33 ), p.dim, CFmtStr( "/ %d", Max( ammo, 0 ) ) );
		if ( ammo2 >= 0 )
		{
			Spaced( F_SMALL, x + w - Sc( 14 ), y + Sc( 9 ), p.dim, TR( "ALT", "АЛЬТ" ), Sc( 2 ), false, 2 );
			Text( F_TEXT, x + w - Sc( 14 ), ny + Sc( 33 ), ammo2 > 0 || clip2 > 0 ? p.accent : p.bad, CFmtStr( "%d", Max( clip2, 0 ) + ammo2 ), false, 2 );
		}
	}


	//-------------------------------------------------------------------------
	// the work suits: a device on the visor, not a combat display
	//-------------------------------------------------------------------------
	int WText( HFont f, int x, int y, Color c, const char *utf8, int align = 0 )
	{
		wchar_t w[256];
		V_UTF8ToUnicode( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( f, w, tw, th );
		if ( align == 1 )
			x -= tw / 2;
		else if ( align == 2 )
			x -= tw;
		surface()->DrawSetTextFont( f );
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( w, wcslen( w ) );
		return tw;
	}

	int WTextW( HFont f, const char *utf8 )
	{
		wchar_t w[256];
		V_UTF8ToUnicode( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( f, w, tw, th );
		return tw;
	}

	// a number on an LCD: the unlit segments ("8" in every place) faintly behind the lit ones
	void Lcd( int x, int y, int digits, int value, Color c, int align = 2 )
	{
		char ghost[16], num[16];
		digits = clamp( digits, 1, 8 );
		for ( int i = 0; i < digits; i++ )
			ghost[i] = '8';
		ghost[digits] = 0;
		Q_snprintf( num, sizeof( num ), "%0*d", digits, clamp( value, 0, 99999999 ) );
		const int gw = WTextW( m_hMonoBig, ghost );
		const int x0 = align == 2 ? x - gw : x;
		WText( m_hMonoBig, x0, y, Color( c.r(), c.g(), c.b(), 22 ), ghost );
		WText( m_hMonoBig, x0, y, c, num );
	}

	// a strip of diagonal stripes of the theme's colour and black
	void Hazard( int x, int y, int w, int h, Color c )
	{
		Rect( x, y, w, h, Color( 10, 9, 6, 230 ) );
		const float step = (float)h * 2.0f;
		const float L = (float)x, R = (float)( x + w );
		for ( float sx = L - h; sx < R; sx += step )
		{
			// the stripe leans: its top edge from sx, its bottom edge h further left, clipped to the strip
			float t0 = clamp( sx + h, L, R ), t1 = clamp( sx + h + step * 0.5f, L, R );
			float b0 = clamp( sx, L, R ), b1 = clamp( sx + step * 0.5f, L, R );
			if ( t1 - t0 < 0.5f && b1 - b0 < 0.5f )
				continue;
			Quad( t0, (float)y, t1, (float)y, b1, (float)( y + h ), b0, (float)( y + h ), c );
		}
	}

	// the case of the device: square corners, a thin rim, two screws, the stripe on top
	void Device( int x, int y, int w, int h, const Palette_t &p )
	{
		Rect( x, y, w, h, Color( p.plate.r() + 14, p.plate.g() + 14, p.plate.b() + 10, 214 ) );
		const int t = Max( 1, Sc( 1 ) );
		Color rim( p.accent.r(), p.accent.g(), p.accent.b(), 60 );
		Rect( x, y, w, t, rim );
		Rect( x, y + h - t, w, t, rim );
		Rect( x, y, t, h, rim );
		Rect( x + w - t, y, t, h, rim );
		Hazard( x + t, y + t, w - 2 * t, Sc( 8 ), Color( p.accent.r(), p.accent.g(), p.accent.b(), 210 ) );
		const float sr = (float)Max( 2, Sc( 2 ) ), m = (float)Sc( 6 );
		Color screw( 140, 140, 130, 150 );
		Disc( x + m, y + h - m, sr, screw, 10 );
		Disc( x + w - m, y + h - m, sr, screw, 10 );
	}

	// a bar of segments lit up to frac
	void Segments( int x, int y, int w, int h, int n, float frac, Color c, Color off )
	{
		const int gap = Max( 1, Sc( 2 ) );
		const int sw = ( w - gap * ( n - 1 ) ) / n;
		const int lit = (int)ceilf( clamp( frac, 0.0f, 1.0f ) * n - 0.001f );
		for ( int i = 0; i < n; i++ )
			Rect( x + i * ( sw + gap ), y, sw, h, i < lit ? c : off );
	}

	void PaintWorkStatus( C_BaseHLPlayer *pPlayer, int sh )
	{
		const Palette_t &p = Pal();
		const bool bHazmat = CurrentTheme() == THEME_HAZMAT;
		const int health = Max( pPlayer->GetHealth(), 0 ), armor = RPMHud_GetBattery();
		if ( m_iLastHealth >= 0 && health < m_iLastHealth )
			m_flHealthHit = gpGlobals->realtime;
		m_iLastHealth = health;
		m_iLastArmor = armor;
		const float flPower = pPlayer->m_HL2Local.m_flSuitPower;
		const float flTorch = pPlayer->m_HL2Local.m_flFlashBattery;
		const bool bTorchOn = pPlayer->IsEffectActive( EF_DIMLIGHT );

		const int w = Sc( 318 ), h = Sc( 134 );
		const int x = Sc( 24 ), y = sh - Sc( 24 ) - h;
		Device( x, y, w, h, p );
		const int ix = x + Sc( 12 ), iw = w - Sc( 24 );
		int cy = y + Sc( 16 );

		// the name of the device and the time of the shift
		WText( m_hMono, ix, cy, p.dim, bHazmat ? TR( "HAZMAT  KZ-4", "ХИМЗАЩИТА  КЗ-4" ) : TR( "CWU  COVERALL  R-7", "ГТС  РОБА  Р-7" ) );
		if ( CEnvTimecycle *tc = GetTimecycle() )
		{
			const float hrs = tc->GetTimeOfDayHours();
			const int hh = (int)hrs % 24, mm = (int)( ( hrs - floorf( hrs ) ) * 60.0f );
			const bool colon = ( (int)( gpGlobals->realtime * 2 ) & 1 ) != 0;
			WText( m_hMono, ix + iw, cy, p.accent, CFmtStr( colon ? "%02d:%02d" : "%02d %02d", hh, mm ), 2 );
		}
		cy += Sc( 24 );

		// vital signs, protection: a label, segments, the LCD number
		const float hFrac = health / (float)Max( pPlayer->GetMaxHealth(), 1 );
		const bool bLow = hFrac <= 0.25f;
		const bool blink = bLow && ( (int)( gpGlobals->realtime * 3 ) & 1 );
		const float flHit = clamp( 1.0f - ( gpGlobals->realtime - m_flHealthHit ) / 0.5f, 0.0f, 1.0f );
		Color hc = ( bLow || flHit > 0.0f ) ? p.bad : p.accent;
		Color off( p.accent.r(), p.accent.g(), p.accent.b(), 28 );
		const int numW = WTextW( m_hMonoBig, "888" );
		const int segX = ix + Sc( 70 ), segW = iw - Sc( 70 ) - numW - Sc( 10 );
		WText( m_hMono, ix, cy + Sc( 6 ), p.dim, TR( "VITAL", "ЖИЗН." ) );
		Segments( segX, cy + Sc( 8 ), segW, Sc( 14 ), 12, hFrac, blink ? Fade( hc, 0.45f ) : hc, off );
		Lcd( ix + iw, cy, 3, health, hc );
		cy += Sc( 30 );
		WText( m_hMono, ix, cy + Sc( 6 ), p.dim, bHazmat ? TR( "FILTER", "ФИЛЬТР" ) : TR( "GUARD", "ЗАЩИТА" ) );
		Segments( segX, cy + Sc( 8 ), segW, Sc( 14 ), 12, armor / 100.0f, p.accent, off );
		Lcd( ix + iw, cy, 3, armor, armor > 0 ? p.accent : p.bad );
		cy += Sc( 34 );

		// the bottom line: the lamp and the air (worker), the air and the dose (hazmat)
		const int half = ( iw - Sc( 12 ) ) / 2;
		const int lineH = Max( 2, Sc( 5 ) );
		if ( bHazmat )
		{
			WText( m_hMono, ix, cy - Sc( 4 ), p.dim, TR( "AIR", "ВОЗДУХ" ) );
			Bar( ix, cy + Sc( 16 ), half, lineH, flPower / 100.0f, flPower < 20.0f ? p.bad : p.accent );
			// the dose rate: what the Geiger counter of the suit hears (range: smaller - nearer)
			const int range = RPMHud_GeigerRange();
			const float nearness = range > 0 && range < 1000 ? 1.0f - range / 1000.0f : 0.0f;
			const float rate = 0.08f + 0.02f * sinf( gpGlobals->realtime * 0.7f ) + nearness * nearness * 12.0f;
			const bool hot = rate > 2.0f;
			Color rc = hot ? ( ( (int)( gpGlobals->realtime * 4 ) & 1 ) ? p.bad : Fade( p.bad, 0.5f ) ) : p.accent;
			WText( m_hMono, ix + half + Sc( 12 ), cy - Sc( 4 ), p.dim, TR( "DOSE", "ДОЗА" ) );
			WText( m_hMono, ix + iw, cy - Sc( 4 ), rc, CFmtStr( "%.2f", rate ), 2 );
			Bar( ix + half + Sc( 12 ), cy + Sc( 16 ), half, lineH, clamp( rate / 10.0f, 0.0f, 1.0f ), rc );
		}
		else
		{
			WText( m_hMono, ix, cy - Sc( 4 ), bTorchOn ? p.accent : p.dim, TR( "LAMP", "ФОНАРЬ" ) );
			if ( flTorch >= 0.0f )
				Bar( ix, cy + Sc( 16 ), half, lineH, flTorch / 100.0f, flTorch < 20.0f ? p.bad : ( bTorchOn ? p.accent : p.dim ) );
			WText( m_hMono, ix + half + Sc( 12 ), cy - Sc( 4 ), p.dim, TR( "AIR", "ВОЗДУХ" ) );
			Bar( ix + half + Sc( 12 ), cy + Sc( 16 ), half, lineH, flPower / 100.0f, flPower < 20.0f ? p.bad : p.accent );
		}
	}

	void PaintWorkTool( C_BaseHLPlayer *pPlayer, int sw, int sh )
	{
		const Palette_t &p = Pal();
		C_BaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
		if ( !pWeapon || pPlayer->GetVehicle() )
			return;
		int clip = -1, ammo = -1;
		if ( pWeapon->UsesPrimaryAmmo() )
		{
			ammo = pPlayer->GetAmmoCount( pWeapon->GetPrimaryAmmoType() );
			if ( pWeapon->UsesClipsForAmmo1() )
				clip = pWeapon->Clip1();
		}
		const bool bAmmo = clip >= 0 || ammo >= 0;
		const int w = Sc( 250 ), h = bAmmo ? Sc( 84 ) : Sc( 44 );
		const int x = sw - Sc( 24 ) - w, y = sh - Sc( 24 ) - h;
		Device( x, y, w, h, p );
		const int ix = x + Sc( 12 ), iw = w - Sc( 24 );
		// "IN HAND" and the name; a long name takes the whole line
		CUtlString name = RPMItems::Name( pWeapon->GetName() );
		const char *label = TR( "IN HAND", "В РУКАХ" );
		if ( WTextW( m_hMono, label ) + WTextW( m_hMono, name.Get() ) + Sc( 12 ) <= iw )
			WText( m_hMono, ix, y + Sc( 16 ), p.dim, label );
		WText( m_hMono, ix + iw, y + Sc( 16 ), p.accent, name.Get(), 2 );
		if ( !bAmmo )
			return;
		const int shown = clip >= 0 ? clip : ammo;
		Color c = shown == 0 ? p.bad : p.accent;
		const int ly = y + Sc( 40 );
		Lcd( ix, ly, 3, shown, c, 0 );
		if ( clip >= 0 )
		{
			const int lw = WTextW( m_hMonoBig, "888" );
			WText( m_hMono, ix + lw + Sc( 10 ), ly + Sc( 8 ), p.dim, "/" );
			Lcd( ix + lw + Sc( 26 ), ly, 3, Max( ammo, 0 ), p.dim, 0 );
		}
	}

	HFont m_hMono, m_hMonoBig;

	int m_iLastHealth, m_iLastArmor;
	float m_flHealthHit, m_flArmorHit;
};

DECLARE_HUDELEMENT( CHudRPMSuit );

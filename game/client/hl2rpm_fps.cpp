//========= HL2RPM ============================================================//
//
// Custom FPS counter HUD element in the M6 look (hl2rpm_ui.h), toggled by
// cl_rpm_fps (Options -> Effects -> FPS counter).
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "hl2rpm_ui.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include "tier0/platform.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

static ConVar cl_rpm_fps( "cl_rpm_fps", "0", FCVAR_ARCHIVE, "HL2RPM: show custom FPS counter in the top-right corner (0 = off, 1 = on)" );

class CHudRPMFPS : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMFPS, Panel );
public:
	CHudRPMFPS( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMFPS" )
	{
		SetParent( g_pClientMode->GetViewport() );
		SetHiddenBits( 0 );
		ResetCounters();
	}

	virtual void LevelInit()
	{
		ResetCounters();
	}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		LoadFonts( pScheme );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
		int w, h;
		surface()->GetScreenSize( w, h );
		SetBounds( 0, 0, w, h );
	}

	virtual bool ShouldDraw()
	{
		return cl_rpm_fps.GetBool() && CHudElement::ShouldDraw();
	}

	virtual void Paint()
	{
		const double flNow = Plat_FloatTime();
		if ( m_flLastSample <= 0.0 )
		{
			m_flLastSample = flNow;
			const float flDt = clamp( gpGlobals->absoluteframetime, 0.0005f, 0.5f );
			m_flFPS = 1.0f / flDt;
			m_flFrameMs = flDt * 1000.0f;
		}
		else
		{
			m_nFrames++;
			const double flElapsed = flNow - m_flLastSample;
			if ( flElapsed >= 0.25 )
			{
				// Ignore large stalls (e.g. map load / alt-tab) instead of showing 0 FPS for a cycle
				if ( flElapsed < 2.0 && m_nFrames > 0 )
				{
					m_flFPS = (float)( m_nFrames / flElapsed );
					m_flFrameMs = (float)( ( flElapsed / m_nFrames ) * 1000.0 );
				}
				m_nFrames = 0;
				m_flLastSample = flNow;
			}
		}

		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		const Palette_t &p = Pal();
		const int iFPS = Max( 0, (int)( m_flFPS + 0.5f ) );
		const Color c = ( iFPS >= 55 ) ? p.good : ( iFPS >= 30 ) ? p.accent : p.bad;

		const int w = Sc( 150 ), h = Sc( 34 );
		const int x = sw - Sc( 20 ) - w, y = Sc( 20 );
		Plate( x, y, w, h, 200, Sc( 6 ) );
		Rect( x + Sc( 2 ), y + Sc( 6 ), Sc( 3 ), h - Sc( 12 ), c );

		Spaced( F_SMALL, x + Sc( 12 ), y + Sc( 9 ), p.dim, "FPS", Sc( 1 ) );
		Text( F_TEXT, x + Sc( 44 ), y + Sc( 7 ), c, CFmtStr( "%d", iFPS ), true );
		Text( F_SMALL, x + w - Sc( 10 ), y + Sc( 9 ), p.dim, CFmtStr( "%.1f ms", m_flFrameMs ), false, 2 );
	}

private:
	void ResetCounters()
	{
		m_flLastSample = 0.0;
		m_nFrames = 0;
		m_flFPS = 0.0f;
		m_flFrameMs = 0.0f;
	}

	double m_flLastSample;
	int m_nFrames;
	float m_flFPS;
	float m_flFrameMs;
};

DECLARE_HUDELEMENT( CHudRPMFPS );

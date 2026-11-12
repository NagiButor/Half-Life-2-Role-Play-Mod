//========= HL2RPM ============================================================//
//
// Markers of quest objectives on the HUD. The server sends "rpm_markers" with the
// targets of the current objectives (quest_system.cpp, SendMarkers): a diamond over
// the target with the distance, an arrow at the screen edge when it is off screen,
// the objective's text when the player looks at it.
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "view.h"
#include "cdll_util.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

ConVar cl_rpm_markers( "cl_rpm_markers", "1", FCVAR_ARCHIVE, "HL2RPM: markers of quest objectives on the HUD" );

struct RPMHudMarker_t
{
	Vector pos;
	wchar_t label[128];
};
static CUtlVector<RPMHudMarker_t> s_HudMarkers;

CON_COMMAND( rpm_markers, "HL2RPM (server -> client): \"x y z~text;...\" - targets of the quest objectives" )
{
	s_HudMarkers.RemoveAll();
	if ( args.ArgC() < 2 )
		return;
	CUtlStringList parts;
	V_SplitString( args[1], ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
	{
		RPMHudMarker_t m;
		char *tilde = Q_strstr( parts[i], "~" );
		if ( tilde )
			*tilde = 0;
		if ( sscanf( parts[i], "%f %f %f", &m.pos.x, &m.pos.y, &m.pos.z ) != 3 )
			continue;
		g_pVGuiLocalize->ConvertANSIToUnicode( tilde ? tilde + 1 : "", m.label, sizeof( m.label ) );
		s_HudMarkers.AddToTail( m );
	}
}

class CHudRPMMarkers : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMMarkers, Panel );
public:
	CHudRPMMarkers( const char *pElementName );
	virtual void ApplySchemeSettings( IScheme *pScheme );
	virtual bool ShouldDraw();
	virtual void LevelInit() { s_HudMarkers.RemoveAll(); }
	virtual void Paint();

private:
	void DrawDiamond( int x, int y, int r, Color c, bool bFill );
	void DrawArrow( int x, int y, float dx, float dy, Color c );
	HFont m_hFont;
	int m_nWhite;
};

DECLARE_HUDELEMENT( CHudRPMMarkers );

CHudRPMMarkers::CHudRPMMarkers( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMMarkers" )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( HIDEHUD_PLAYERDEAD );
	m_hFont = INVALID_FONT;
	m_nWhite = -1;
}

void CHudRPMMarkers::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
	int w, h;
	surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	// the fonts and colors of the windows (hl2rpm_ui.h)
	RPMUI::LoadFonts( pScheme );
	m_hFont = RPMUI::Font( RPMUI::F_SMALL );
	if ( m_nWhite < 0 )
	{
		m_nWhite = surface()->CreateNewTextureID();
		surface()->DrawSetTextureFile( m_nWhite, "vgui/white", true, false );
	}
}

bool CHudRPMMarkers::ShouldDraw()
{
	return cl_rpm_markers.GetBool() && s_HudMarkers.Count() > 0 && engine->IsInGame() && CHudElement::ShouldDraw();
}

void CHudRPMMarkers::DrawDiamond( int x, int y, int r, Color c, bool bFill )
{
	Vertex_t v[4];
	v[0].Init( Vector2D( x, y - r ) );
	v[1].Init( Vector2D( x + r, y ) );
	v[2].Init( Vector2D( x, y + r ) );
	v[3].Init( Vector2D( x - r, y ) );
	surface()->DrawSetColor( c );
	if ( bFill )
	{
		surface()->DrawSetTexture( m_nWhite );
		surface()->DrawTexturedPolygon( 4, v );
	}
	else
	{
		for ( int i = 0; i < 4; i++ )
			surface()->DrawLine( (int)v[i].m_Position.x, (int)v[i].m_Position.y, (int)v[( i + 1 ) % 4].m_Position.x, (int)v[( i + 1 ) % 4].m_Position.y );
	}
}

void CHudRPMMarkers::DrawArrow( int x, int y, float dx, float dy, Color c )
{
	// a triangle pointing along (dx, dy)
	const float L = 14.0f, W = 8.0f;
	Vertex_t v[3];
	v[0].Init( Vector2D( x + dx * L, y + dy * L ) );
	v[1].Init( Vector2D( x - dy * W, y + dx * W ) );
	v[2].Init( Vector2D( x + dy * W, y - dx * W ) );
	surface()->DrawSetColor( c );
	surface()->DrawSetTexture( m_nWhite );
	surface()->DrawTexturedPolygon( 3, v );
}

void CHudRPMMarkers::Paint()
{
	int sw, sh;
	GetSize( sw, sh );
	const Vector &eye = MainViewOrigin();
	const Vector &fwd = MainViewForward(), &right = MainViewRight(), &up = MainViewUp();
	const float pulse = 0.75f + 0.25f * sinf( gpGlobals->curtime * 4.0f );
	const RPMUI::Palette_t &pal = RPMUI::Pal();
	const Color amber( pal.accent.r(), pal.accent.g(), pal.accent.b(), (int)( 235 * pulse ) );
	const Color shadow( 0, 0, 0, 150 );
	const int margin = YRES( 28 );

	for ( int i = 0; i < s_HudMarkers.Count(); i++ )
	{
		const RPMHudMarker_t &m = s_HudMarkers[i];
		Vector d = m.pos - eye;
		float dist = d.Length();
		int x = 0, y = 0;
		bool bOn = DotProduct( d, fwd ) > 0.0f && GetVectorInScreenSpace( m.pos, x, y ) &&
			x >= margin && x <= sw - margin && y >= margin && y <= sh - margin;
		if ( !bOn )
		{
			// off screen: on an ellipse around the centre, an arrow towards the target
			float dx = DotProduct( d, right ), dy = -DotProduct( d, up );
			float len = sqrtf( dx * dx + dy * dy );
			if ( len < 0.001f )
			{
				dx = 0;
				dy = 1;
			}
			else
			{
				dx /= len;
				dy /= len;
			}
			x = sw / 2 + (int)( dx * ( sw * 0.5f - margin ) );
			y = sh / 2 + (int)( dy * ( sh * 0.5f - margin ) );
			DrawArrow( x + 1, y + 1, dx, dy, shadow );
			DrawArrow( x, y, dx, dy, amber );
			continue;
		}
		const int r = YRES( 6 );
		DrawDiamond( x + 1, y + 1, r + 1, shadow, true );
		DrawDiamond( x, y, r, Color( pal.accent.r(), pal.accent.g(), pal.accent.b(), 90 ), true );
		DrawDiamond( x, y, r, amber, false );

		// distance (1 unit = 1 inch)
		wchar_t wsz[160];
		V_snwprintf( wsz, ARRAYSIZE( wsz ), !Q_stricmp( RPMItems::Language(), "russian" ) ? L"%d \x043C" : L"%d m", (int)( dist * 0.0254f + 0.5f ) );
		int tw, th;
		surface()->GetTextSize( m_hFont, wsz, tw, th );
		surface()->DrawSetTextFont( m_hFont );
		surface()->DrawSetTextColor( shadow );
		surface()->DrawSetTextPos( x - tw / 2 + 1, y + r + 3 );
		surface()->DrawPrintText( wsz, wcslen( wsz ) );
		surface()->DrawSetTextColor( Color( pal.text.r(), pal.text.g(), pal.text.b(), 230 ) );
		surface()->DrawSetTextPos( x - tw / 2, y + r + 2 );
		surface()->DrawPrintText( wsz, wcslen( wsz ) );

		// the objective's text when the player looks at the marker
		if ( m.label[0] && abs( x - sw / 2 ) < sw / 10 && abs( y - sh / 2 ) < sh / 8 )
		{
			surface()->GetTextSize( m_hFont, m.label, tw, th );
			surface()->DrawSetTextColor( shadow );
			surface()->DrawSetTextPos( x - tw / 2 + 1, y - r - th - 1 );
			surface()->DrawPrintText( m.label, wcslen( m.label ) );
			surface()->DrawSetTextColor( Color( pal.accent.r(), pal.accent.g(), pal.accent.b(), 240 ) );
			surface()->DrawSetTextPos( x - tw / 2, y - r - th - 2 );
			surface()->DrawPrintText( m.label, wcslen( m.label ) );
		}
	}
}

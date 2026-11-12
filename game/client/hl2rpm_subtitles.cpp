//========= HL2RPM ============================================================//
//
// Subtitles of NPC barks (Creation Kit), bottom centre like New Vegas: the name of
// the speaker in amber, the line under it, a few lines queued, fading. The server
// sends rpm_subtitle "<name>" "<text>" <seconds> when the player hears the bark
// (hl2rpm_audio.cpp).
// The only subtitles of the game: the stock close captions are cut out (hud_closecaption.cpp).
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

ConVar cl_rpm_subtitles( "cl_rpm_subtitles", "1", FCVAR_ARCHIVE, "HL2RPM: subtitles of NPC barks" );

extern bool HL2RPM_IsDialogOpen();
extern bool RPMLoot_IsOpen();
extern bool RPMMenu_IsOpen();
extern bool RPMTerminal_IsOpen();
extern bool RPMLockpick_IsOpen();

struct RPMSubtitle_t
{
	wchar_t name[64];
	wchar_t text[512];
	float start;
	float end;
};
static CUtlVector<RPMSubtitle_t> s_Subtitles;

static void AddSubtitle( const wchar_t *name, const wchar_t *text, float seconds )
{
	RPMSubtitle_t s;
	Q_wcsncpy( s.name, name, sizeof( s.name ) );
	Q_wcsncpy( s.text, text, sizeof( s.text ) );
	s.start = gpGlobals->realtime;
	s.end = s.start + clamp( seconds, 1.5f, 20.0f );
	// the same speaker replaces his previous line
	for ( int i = s_Subtitles.Count() - 1; i >= 0; i-- )
		if ( !wcscmp( s_Subtitles[i].name, s.name ) )
			s_Subtitles.Remove( i );
	s_Subtitles.AddToTail( s );
	while ( s_Subtitles.Count() > 3 )
		s_Subtitles.Remove( 0 );
}

CON_COMMAND( rpm_subtitle, "HL2RPM (server -> client): rpm_subtitle \"<name>\" \"<text>\" <seconds>" )
{
	if ( args.ArgC() < 3 )
		return;
	wchar_t name[64], text[512];
	g_pVGuiLocalize->ConvertANSIToUnicode( args[1], name, sizeof( name ) );
	g_pVGuiLocalize->ConvertANSIToUnicode( args[2], text, sizeof( text ) );
	AddSubtitle( name, text, args.ArgC() > 3 ? (float)atof( args[3] ) : 4.0f );
}

class CHudRPMSubtitles : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMSubtitles, Panel );
public:
	CHudRPMSubtitles( const char *pElementName );
	virtual void ApplySchemeSettings( IScheme *pScheme );
	virtual bool ShouldDraw();
	virtual void LevelInit() { s_Subtitles.RemoveAll(); }
	virtual void Paint();

private:
	struct Line_t { int start, len; };
	void WrapLines( const wchar_t *text, int maxWidth, CUtlVector<Line_t> &lines );
	int TextWidth( const wchar_t *text, int start, int len );
	HFont m_hName, m_hText;
};

DECLARE_HUDELEMENT( CHudRPMSubtitles );

CHudRPMSubtitles::CHudRPMSubtitles( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMSubtitles" )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( HIDEHUD_PLAYERDEAD );
	m_hName = m_hText = INVALID_FONT;
}

void CHudRPMSubtitles::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
	int w, h;
	surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	// Tahoma of the close captions: has Cyrillic (the Latin-only fonts show nothing)
	m_hName = pScheme->GetFont( "CloseCaption_Bold", false );
	m_hText = pScheme->GetFont( "CloseCaption_Normal", false );
}

bool CHudRPMSubtitles::ShouldDraw()
{
	if ( !cl_rpm_subtitles.GetBool() || !s_Subtitles.Count() || !CHudElement::ShouldDraw() )
		return false;
	// not over the windows: the dialog is in the same place at the bottom, the others cover the screen
	if ( HL2RPM_IsDialogOpen() || RPMLoot_IsOpen() || RPMMenu_IsOpen() || RPMTerminal_IsOpen() || RPMLockpick_IsOpen() )
		return false;
	const float now = gpGlobals->realtime;
	for ( int i = s_Subtitles.Count() - 1; i >= 0; i-- )
		if ( now > s_Subtitles[i].end + 0.6f )
			s_Subtitles.Remove( i );
	return s_Subtitles.Count() > 0;
}

int CHudRPMSubtitles::TextWidth( const wchar_t *text, int start, int len )
{
	wchar_t buf[512];
	len = Min( len, (int)ARRAYSIZE( buf ) - 1 );
	memcpy( buf, text + start, len * sizeof( wchar_t ) );
	buf[len] = 0;
	int tw, th;
	surface()->GetTextSize( m_hText, buf, tw, th );
	return tw;
}

void CHudRPMSubtitles::WrapLines( const wchar_t *text, int maxWidth, CUtlVector<Line_t> &lines )
{
	// greedy word wrap with the text font: a line grows word by word while it fits
	const int n = wcslen( text );
	int lineStart = 0, lineEnd = 0;		// [start, end) of the current line
	int p = 0;
	while ( p < n )
	{
		int wordEnd = p;
		while ( wordEnd < n && text[wordEnd] != L' ' )
			wordEnd++;
		if ( lineEnd > lineStart && TextWidth( text, lineStart, wordEnd - lineStart ) > maxWidth )
		{
			Line_t l = { lineStart, lineEnd - lineStart };
			lines.AddToTail( l );
			lineStart = p;
		}
		lineEnd = wordEnd;
		p = wordEnd + 1;
	}
	if ( lineEnd > lineStart )
	{
		Line_t l = { lineStart, lineEnd - lineStart };
		lines.AddToTail( l );
	}
}

void CHudRPMSubtitles::Paint()
{
	int sw, sh;
	GetSize( sw, sh );
	const float now = gpGlobals->realtime;
	const int maxW = (int)( sw * 0.56f );
	const int lineH = surface()->GetFontTall( m_hText ) + 2;
	int y = (int)( sh * 0.80f );
	// newest at the bottom: draw from the last one upwards
	for ( int i = s_Subtitles.Count() - 1; i >= 0; i-- )
	{
		const RPMSubtitle_t &s = s_Subtitles[i];
		float a = 1.0f;
		if ( now < s.start + 0.25f )
			a = ( now - s.start ) / 0.25f;
		else if ( now > s.end )
			a = 1.0f - ( now - s.end ) / 0.6f;
		a = clamp( a, 0.0f, 1.0f );
		if ( a <= 0.0f )
			continue;
		CUtlVector<Line_t> lines;
		WrapLines( s.text, maxW, lines );
		const int nameRows = s.name[0] ? 1 : 0;	// close captions of HL2 speech have no name
		int blockH = lineH * ( lines.Count() + nameRows );
		y -= blockH;
		if ( nameRows )
		{
			int nw, nh;
			surface()->GetTextSize( m_hName, s.name, nw, nh );
			surface()->DrawSetTextFont( m_hName );
			surface()->DrawSetTextColor( Color( 0, 0, 0, (int)( 180 * a ) ) );
			surface()->DrawSetTextPos( ( sw - nw ) / 2 + 2, y + 2 );
			surface()->DrawPrintText( s.name, wcslen( s.name ) );
			surface()->DrawSetTextColor( Color( 255, 186, 60, (int)( 255 * a ) ) );
			surface()->DrawSetTextPos( ( sw - nw ) / 2, y );
			surface()->DrawPrintText( s.name, wcslen( s.name ) );
		}
		surface()->DrawSetTextFont( m_hText );
		for ( int l = 0; l < lines.Count(); l++ )
		{
			wchar_t t[512];
			int len = Min( lines[l].len, (int)ARRAYSIZE( t ) - 1 );
			memcpy( t, s.text + lines[l].start, len * sizeof( wchar_t ) );
			t[len] = 0;
			int tw, th;
			surface()->GetTextSize( m_hText, t, tw, th );
			int ly = y + lineH * ( l + nameRows );
			surface()->DrawSetTextColor( Color( 0, 0, 0, (int)( 190 * a ) ) );
			surface()->DrawSetTextPos( ( sw - tw ) / 2 + 2, ly + 2 );
			surface()->DrawPrintText( t, wcslen( t ) );
			surface()->DrawSetTextColor( Color( 235, 228, 210, (int)( 255 * a ) ) );
			surface()->DrawSetTextPos( ( sw - tw ) / 2, ly );
			surface()->DrawPrintText( t, wcslen( t ) );
		}
		y -= 8;
	}
}

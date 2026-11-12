//========= HL2RPM ============================================================//
//
// The look of the HL2RPM windows: see hl2rpm_ui.h.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_ui.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

int RPMUI_SuitTheme();

static ConVar rpm_ui_theme( "rpm_ui_theme", "-1", FCVAR_ARCHIVE, "HL2RPM: colors of the windows: -1 by the suit, 0 none, 1 HEV, 2 PCV, 3 Combine, 4 elite, 5 worker, 6 hazmat" );

namespace RPMUI
{
	static HFont s_Fonts[F_COUNT];

	void LoadFonts( IScheme *s )
	{
		static const char *names[F_COUNT] = { "RPMUI_Title", "RPMUI_TitleGlow", "RPMUI_Tab", "RPMUI_TabGlow", "RPMUI_Text",
			"RPMUI_TextGlow", "RPMUI_Small", "RPMUI_SmallGlow", "HudNumbers", "HudNumbersGlow" };
		for ( int i = 0; i < F_COUNT; i++ )
		{
			s_Fonts[i] = s->GetFont( names[i], true );
			if ( s_Fonts[i] == INVALID_FONT )
				s_Fonts[i] = s->GetFont( "Trebuchet18", false );
		}
	}

	HFont Font( int f ) { return s_Fonts[clamp( f, 0, F_COUNT - 1 )]; }

	//-------------------------------------------------------------------------
	// themes
	//-------------------------------------------------------------------------
	static Palette_t MakePalette( Color accent, Color text, Color bar, Color plate )
	{
		Palette_t p;
		p.accent = accent;
		p.dim = Color( accent.r() * 3 / 4, accent.g() * 3 / 4, accent.b() * 3 / 4, 255 );
		p.faint = Color( accent.r(), accent.g(), accent.b(), 70 );
		p.text = text;
		p.grey = Color( 120, 110, 92, 255 );
		p.good = Color( 140, 220, 100, 255 );
		p.bad = Color( 235, 95, 70, 255 );
		p.bar = bar;
		p.plate = plate;
		return p;
	}

	static const Palette_t &PaletteOf( int theme )
	{
		static Palette_t s_Pal[THEME_COUNT];
		static bool s_bInit = false;
		if ( !s_bInit )
		{
			s_bInit = true;
			s_Pal[THEME_NONE] = MakePalette( Color( 255, 176, 46, 255 ), Color( 236, 222, 190, 255 ), Color( 178, 36, 24, 235 ), Color( 0, 0, 0, 255 ) );
			s_Pal[THEME_HEV] = MakePalette( Color( 255, 150, 30, 255 ), Color( 245, 222, 186, 255 ), Color( 178, 36, 24, 235 ), Color( 0, 0, 0, 255 ) );
			s_Pal[THEME_PCV] = MakePalette( Color( 130, 230, 110, 255 ), Color( 210, 240, 200, 255 ), Color( 40, 120, 40, 235 ), Color( 0, 4, 0, 255 ) );
			s_Pal[THEME_PCV].good = Color( 200, 255, 160, 255 );
			s_Pal[THEME_COMBINE] = MakePalette( Color( 90, 210, 225, 255 ), Color( 205, 235, 240, 255 ), Color( 30, 100, 130, 235 ), Color( 0, 3, 6, 255 ) );
			s_Pal[THEME_ELITE] = MakePalette( Color( 235, 80, 64, 255 ), Color( 240, 214, 205, 255 ), Color( 120, 30, 26, 235 ), Color( 4, 0, 0, 255 ) );
			s_Pal[THEME_ELITE].bad = Color( 255, 150, 60, 255 );
			// work suits: the amber of a cheap LCD, the lime of a dosimeter
			s_Pal[THEME_WORKER] = MakePalette( Color( 242, 188, 44, 255 ), Color( 238, 228, 196, 255 ), Color( 150, 108, 18, 235 ), Color( 6, 5, 2, 255 ) );
			s_Pal[THEME_WORKER].good = Color( 170, 230, 90, 255 );
			s_Pal[THEME_HAZMAT] = MakePalette( Color( 196, 232, 70, 255 ), Color( 228, 240, 204, 255 ), Color( 92, 124, 20, 235 ), Color( 2, 5, 0, 255 ) );
			s_Pal[THEME_HAZMAT].bad = Color( 255, 120, 60, 255 );
		}
		return s_Pal[clamp( theme, 0, THEME_COUNT - 1 )];
	}

	int CurrentTheme()
	{
		int t = rpm_ui_theme.GetInt();
		if ( t < 0 || t >= THEME_COUNT )
			t = RPMUI_SuitTheme();
		return clamp( t, 0, THEME_COUNT - 1 );
	}

	const Palette_t &Pal() { return PaletteOf( CurrentTheme() ); }
	const Palette_t &PalOf( int theme ) { return PaletteOf( theme ); }

	const char *ThemeName( int theme )
	{
		static const char *s[THEME_COUNT] = { "none", "hev", "pcv", "combine", "elite", "worker", "hazmat" };
		return s[clamp( theme, 0, THEME_COUNT - 1 )];
	}

	float Scale()
	{
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		return Max( 0.5f, sh / 900.0f );
	}

	//-------------------------------------------------------------------------
	// text
	//-------------------------------------------------------------------------
	static int ToWide( const char *utf8, wchar_t *w, int size )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( utf8 ? utf8 : "", w, size );
		return V_wcslen( w );
	}

	int TextWidth( int font, const char *utf8 )
	{
		wchar_t w[1024];
		ToWide( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( Font( font ), w, tw, th );
		return tw;
	}

	int LineHeight( int font, int lineGap )
	{
		return surface()->GetFontTall( Font( font ) ) + Sc( lineGap );
	}

	int Text( int font, int x, int y, Color c, const char *utf8, bool glow, int align, int maxW )
	{
		wchar_t w[1024];
		int n = ToWide( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( Font( font ), w, tw, th );
		if ( maxW > 0 && tw > maxW )
		{
			// cut with an ellipsis
			while ( n > 1 )
			{
				w[--n] = 0;
				w[n - 1] = 0x2026;
				surface()->GetTextSize( Font( font ), w, tw, th );
				if ( tw <= maxW )
					break;
			}
		}
		if ( align == 1 )
			x -= tw / 2;
		else if ( align == 2 )
			x -= tw;
		if ( glow && font + 1 < F_COUNT && ( font % 2 ) == 0 )
		{
			surface()->DrawSetTextFont( Font( font + 1 ) );
			surface()->DrawSetTextColor( c.r(), c.g(), c.b(), c.a() * 110 / 255 );
			surface()->DrawSetTextPos( x, y );
			surface()->DrawPrintText( w, n );
		}
		surface()->DrawSetTextFont( Font( font ) );
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( w, n );
		return tw;
	}

	// headers are capitals (Latin, Cyrillic)
	static void ToUpper( wchar_t *w, int n )
	{
		for ( int i = 0; i < n; i++ )
		{
			wchar_t c = w[i];
			if ( ( c >= L'a' && c <= L'z' ) || ( c >= 0x430 && c <= 0x44F ) )
				w[i] = c - 0x20;
			else if ( c >= 0x450 && c <= 0x45F )
				w[i] = c - 0x50;
		}
	}

	int SpacedWidth( int font, const char *utf8, int tracking )
	{
		wchar_t w[256];
		int n = ToWide( utf8, w, sizeof( w ) );
		ToUpper( w, n );
		int cx = 0;
		for ( int i = 0; i < n; i++ )
			cx += surface()->GetCharacterWidth( Font( font ), w[i] ) + tracking;
		return n ? cx - tracking : 0;
	}

	// capitals with letter spacing (headers, tabs)
	int Spaced( int font, int x, int y, Color c, const char *utf8, int tracking, bool glow, int align )
	{
		wchar_t w[256];
		int n = ToWide( utf8, w, sizeof( w ) );
		ToUpper( w, n );
		int width = SpacedWidth( font, utf8, tracking );
		if ( align == 1 )
			x -= width / 2;
		else if ( align == 2 )
			x -= width;
		for ( int pass = glow ? 0 : 1; pass < 2; pass++ )
		{
			int cx = x;
			surface()->DrawSetTextFont( Font( pass == 0 ? font + 1 : font ) );
			surface()->DrawSetTextColor( c.r(), c.g(), c.b(), pass == 0 ? c.a() * 120 / 255 : c.a() );
			for ( int i = 0; i < n; i++ )
			{
				surface()->DrawSetTextPos( cx, y );
				surface()->DrawUnicodeChar( w[i] );
				cx += surface()->GetCharacterWidth( Font( font ), w[i] ) + tracking;
			}
		}
		return width;
	}

	// lines of a word-wrapped text: [start, end) pairs
	static void WrapLines( HFont f, const wchar_t *text, int len, int width, CUtlVector<int> &out )
	{
		int start = 0;
		while ( start <= len )
		{
			int lastSpace = -1, i = start, wsum = 0;
			for ( ; i < len && text[i] != L'\n'; i++ )
			{
				wsum += surface()->GetCharacterWidth( f, text[i] );
				if ( text[i] == L' ' )
					lastSpace = i;
				if ( wsum > width && lastSpace > start )
				{
					i = lastSpace;
					break;
				}
			}
			out.AddToTail( start );
			out.AddToTail( i );
			start = i + 1;
			if ( i >= len )
				break;
		}
	}

	int Wrap( int font, int x, int y, int width, Color c, const char *utf8, int maxLines, int lineGap )
	{
		static wchar_t text[4096];
		int len = ToWide( utf8, text, sizeof( text ) );
		HFont f = Font( font );
		int lineH = LineHeight( font, lineGap );
		CUtlVector<int> lines;
		WrapLines( f, text, len, width, lines );
		int n = lines.Count() / 2;
		if ( maxLines > 0 && n > maxLines )
			n = maxLines;
		surface()->DrawSetTextFont( f );
		surface()->DrawSetTextColor( c );
		for ( int i = 0; i < n; i++ )
		{
			surface()->DrawSetTextPos( x, y + i * lineH );
			surface()->DrawPrintText( text + lines[i * 2], lines[i * 2 + 1] - lines[i * 2] );
		}
		return n * lineH;
	}

	int WrapHeight( int font, int width, const char *utf8, int lineGap )
	{
		static wchar_t text[4096];
		int len = ToWide( utf8, text, sizeof( text ) );
		CUtlVector<int> lines;
		WrapLines( Font( font ), text, len, width, lines );
		return ( lines.Count() / 2 ) * LineHeight( font, lineGap );
	}

	//-------------------------------------------------------------------------
	// shapes
	//-------------------------------------------------------------------------
	void Rect( int x, int y, int w, int h, Color c )
	{
		surface()->DrawSetColor( c );
		surface()->DrawFilledRect( x, y, x + w, y + h );
	}

	void Arrow( int cx, int y, int size, bool up, Color c )
	{
		surface()->DrawSetColor( c );
		for ( int i = 0; i < size; i++ )
		{
			int half = up ? i : size - 1 - i;
			surface()->DrawFilledRect( cx - half, y + i, cx + half + 1, y + i + 1 );
		}
	}

	// a black plate with rounded corners (like the HUD boxes of HL2) and faint scan lines
	void Plate( int x, int y, int w, int h, int alpha, int r )
	{
		if ( r < 0 )
			r = Sc( 8 );
		r = Min( r, Min( w, h ) / 2 );
		const Palette_t &p = Pal();
		surface()->DrawSetColor( p.plate.r(), p.plate.g(), p.plate.b(), alpha );
		for ( int i = 0; i < r; i++ )
		{
			float dy = r - i - 0.5f;
			int inset = (int)( r - sqrtf( Max( 0.0f, r * r - dy * dy ) ) + 0.5f );
			surface()->DrawFilledRect( x + inset, y + i, x + w - inset, y + i + 1 );
			surface()->DrawFilledRect( x + inset, y + h - i - 1, x + w - inset, y + h - i );
		}
		surface()->DrawFilledRect( x, y + r, x + w, y + h - r );
		surface()->DrawSetColor( p.accent.r(), p.accent.g(), p.accent.b(), 7 * alpha / 255 );
		int step = Max( 2, Sc( 3 ) );
		for ( int ly = y + step; ly < y + h - step; ly += step )
			surface()->DrawFilledRect( x + Sc( 4 ), ly, x + w - Sc( 4 ), ly + 1 );
	}

	// thin double frame with bright corner ticks
	void FrameBox( int x, int y, int w, int h, int tick, int alpha )
	{
		if ( tick < 0 )
			tick = Sc( 14 );
		const Palette_t &p = Pal();
		const Color &a = p.accent;
		surface()->DrawSetColor( a.r(), a.g(), a.b(), 70 * alpha / 255 );
		surface()->DrawOutlinedRect( x, y, x + w, y + h );
		int in = Max( 3, Sc( 4 ) );
		surface()->DrawSetColor( a.r(), a.g(), a.b(), 32 * alpha / 255 );
		surface()->DrawOutlinedRect( x + in, y + in, x + w - in, y + h - in );
		surface()->DrawSetColor( a.r(), a.g(), a.b(), alpha );
		const int t = Max( 2, Sc( 2 ) );
		surface()->DrawFilledRect( x, y, x + tick, y + t );
		surface()->DrawFilledRect( x, y, x + t, y + tick );
		surface()->DrawFilledRect( x + w - tick, y, x + w, y + t );
		surface()->DrawFilledRect( x + w - t, y, x + w, y + tick );
		surface()->DrawFilledRect( x, y + h - t, x + tick, y + h );
		surface()->DrawFilledRect( x, y + h - tick, x + t, y + h );
		surface()->DrawFilledRect( x + w - tick, y + h - t, x + w, y + h );
		surface()->DrawFilledRect( x + w - t, y + h - tick, x + w, y + h );
	}

	// the selected row: a red bar that flashes when it is chosen and settles
	void Highlight( int x, int y, int w, int h, float flash, int alpha )
	{
		flash = clamp( flash, 0.0f, 1.0f );
		const Color &b = Pal().bar;
		int a = (int)( ( 200 + 55 * flash ) * alpha / 255 );
		surface()->DrawSetColor( b.r(), b.g(), b.b(), a );
		surface()->DrawFilledRect( x, y, x + w, y + h );
		surface()->DrawSetColor( Min( 255, b.r() + 77 ), Min( 255, b.g() + 84 ), Min( 255, b.b() + 56 ), (int)( 140 * flash * alpha / 255 ) );
		surface()->DrawOutlinedRect( x, y, x + w, y + h );
		surface()->DrawSetColor( 255, 220, 160, 60 * alpha / 255 );
		surface()->DrawFilledRect( x, y, x + Max( 2, Sc( 3 ) ), y + h );
	}

	void Line( int x, int y, int w, Color c )
	{
		surface()->DrawSetColor( c );
		surface()->DrawFilledRect( x, y, x + w, y + 1 );
	}

	void Bar( int x, int y, int w, int h, float frac, Color c )
	{
		surface()->DrawSetColor( c.r(), c.g(), c.b(), c.a() / 5 );
		surface()->DrawFilledRect( x, y, x + w, y + h );
		surface()->DrawSetColor( c );
		surface()->DrawFilledRect( x, y, x + (int)( w * clamp( frac, 0.0f, 1.0f ) ), y + h );
	}

	// a small square: filled when on (tagged skill, chosen trait / perk)
	void Marker( int x, int y, int size, bool on, Color c )
	{
		surface()->DrawSetColor( c.r(), c.g(), c.b(), on ? c.a() : c.a() * 90 / 255 );
		surface()->DrawOutlinedRect( x, y, x + size, y + size );
		if ( on )
		{
			int in = Max( 2, size / 4 );
			surface()->DrawFilledRect( x + in, y + in, x + size - in, y + size - in );
		}
	}

	//-------------------------------------------------------------------------
	// filled shapes (a white texture under DrawTexturedPolygon)
	//-------------------------------------------------------------------------
	static void SetWhite()
	{
		static int s_iWhite = -1;
		if ( s_iWhite < 0 || !surface()->IsTextureIDValid( s_iWhite ) )
		{
			s_iWhite = surface()->CreateNewTextureID();
			surface()->DrawSetTextureFile( s_iWhite, "vgui/white", true, false );
		}
		surface()->DrawSetTexture( s_iWhite );
	}

	void Poly( int n, const float *xy, Color c )
	{
		if ( n < 3 || n > 64 )
			return;
		// one winding for all (the other one is culled): clockwise on the screen
		float area = 0.0f;
		for ( int i = 0; i < n; i++ )
		{
			int j = ( i + 1 ) % n;
			area += xy[i * 2] * xy[j * 2 + 1] - xy[j * 2] * xy[i * 2 + 1];
		}
		Vertex_t v[64];
		for ( int i = 0; i < n; i++ )
		{
			int k = area >= 0.0f ? i : n - 1 - i;
			v[i].Init( Vector2D( xy[k * 2], xy[k * 2 + 1] ), Vector2D( 0.5f, 0.5f ) );
		}
		SetWhite();
		surface()->DrawSetColor( c );
		surface()->DrawTexturedPolygon( n, v );
	}

	void Quad( float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c )
	{
		float xy[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
		Poly( 4, xy, c );
	}

	void ThickLine( float x0, float y0, float x1, float y1, float w, Color c )
	{
		float dx = x1 - x0, dy = y1 - y0;
		float len = sqrtf( dx * dx + dy * dy );
		if ( len < 0.01f )
			return;
		float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
		Quad( x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c );
	}

	void Disc( float cx, float cy, float r, Color c, int seg )
	{
		seg = clamp( seg, 6, 64 );
		float xy[128];
		for ( int i = 0; i < seg; i++ )
		{
			float a = i * 2.0f * M_PI_F / seg;
			xy[i * 2] = cx + cosf( a ) * r;
			xy[i * 2 + 1] = cy + sinf( a ) * r;
		}
		Poly( seg, xy, c );
	}

	void Ring( float cx, float cy, float r0, float r1, Color c, int seg, float a0, float a1 )
	{
		seg = clamp( seg, 3, 128 );
		for ( int i = 0; i < seg; i++ )
		{
			float t0 = DEG2RAD( a0 + ( a1 - a0 ) * i / seg ), t1 = DEG2RAD( a0 + ( a1 - a0 ) * ( i + 1 ) / seg );
			Quad( cx + cosf( t0 ) * r1, cy + sinf( t0 ) * r1, cx + cosf( t1 ) * r1, cy + sinf( t1 ) * r1,
				cx + cosf( t1 ) * r0, cy + sinf( t1 ) * r0, cx + cosf( t0 ) * r0, cy + sinf( t0 ) * r0, c );
		}
	}

	float Ramp( float t0, float dur, float delay )
	{
		float t = ( gpGlobals->realtime - t0 - delay ) / Max( 0.001f, dur );
		t = clamp( t, 0.0f, 1.0f );
		return 1.0f - ( 1.0f - t ) * ( 1.0f - t );
	}

	Color Fade( Color c, float a )
	{
		return Color( c.r(), c.g(), c.b(), (int)( c.a() * clamp( a, 0.0f, 1.0f ) ) );
	}

	void Sound( int s )
	{
		static const char *names[] = { "ui/buttonrollover.wav", "ui/buttonclickrelease.wav", "ui/buttonclick.wav",
			"ui/buttonclickrelease.wav", "ui/buttonrollover.wav", "buttons/button10.wav" };
		if ( s >= 0 && s < ARRAYSIZE( names ) )
			surface()->PlaySound( names[s] );
	}

	//-------------------------------------------------------------------------
	int CHotList::Add( int x, int y, int w, int h, int action, int arg, const char *id )
	{
		Hot_t hot;
		hot.x = x; hot.y = y; hot.w = w; hot.h = h;
		hot.action = action; hot.arg = arg;
		hot.id = id ? id : "";
		return m_Hot.AddToTail( hot );
	}

	const Hot_t *CHotList::Hit( int x, int y ) const
	{
		for ( int i = m_Hot.Count() - 1; i >= 0; i-- )
		{
			const Hot_t &h = m_Hot[i];
			if ( x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h )
				return &h;
		}
		return NULL;
	}

	bool CHotList::Button( int x, int y, int w, int h, const char *label, int action, int arg, bool enabled, int font )
	{
		const Palette_t &p = Pal();
		bool hover = enabled && Over( x, y, w, h );
		if ( hover )
			Highlight( x, y, w, h, 0.0f );
		else
		{
			surface()->DrawSetColor( p.plate.r(), p.plate.g(), p.plate.b(), 200 );
			surface()->DrawFilledRect( x, y, x + w, y + h );
		}
		Color fc = enabled ? p.accent : p.grey;
		surface()->DrawSetColor( fc.r(), fc.g(), fc.b(), enabled ? 150 : 70 );
		surface()->DrawOutlinedRect( x, y, x + w, y + h );
		int th = surface()->GetFontTall( Font( font ) );
		Text( font, x + w / 2, y + ( h - th ) / 2, hover ? p.text : fc, label, hover, 1 );
		if ( enabled )
			Add( x, y, w, h, action, arg );
		return hover;
	}

	float CHoverSound::Update( const Hot_t *pHot )
	{
		int a = pHot ? pHot->action : -1, arg = pHot ? pHot->arg : 0;
		if ( a != m_iLast || arg != m_iLastArg )
		{
			if ( a >= 0 )
				Sound( SND_HOVER );
			m_iLast = a;
			m_iLastArg = arg;
			m_flChange = gpGlobals->realtime;
		}
		return m_flChange;
	}
}

//=============================================================================
// the theme of the suit worn
//=============================================================================
int RPMUI_SuitTheme()
{
	// the server knows the suit worn (hl2rpm_char.cpp RPMChar_SuitTheme -> m_Local.m_iSuitTheme)
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || !pPlayer->IsSuitEquipped() )
		return RPMUI::THEME_NONE;
	int t = pPlayer->m_Local.m_iSuitTheme;
	return ( t > 0 && t < RPMUI::THEME_COUNT ) ? t : RPMUI::THEME_HEV;
}

//========= HL2RPM ============//
//
// RPG-интерфейс: палитра, шрифты, рисование (см. rpg_ui_common.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_ui_common.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

namespace RPGUI
{
	const Color clrPanel( 12, 16, 21, 228 );
	const Color clrPanelDeep( 6, 8, 11, 160 );
	const Color clrLine( 110, 190, 255, 70 );
	const Color clrAccent( 110, 190, 255, 255 );
	const Color clrAccentDim( 110, 190, 255, 150 );
	const Color clrText( 224, 230, 236, 255 );
	const Color clrTextDim( 158, 168, 178, 255 );
	const Color clrTextFaint( 104, 112, 120, 255 );
	const Color clrAmber( 255, 186, 82, 255 );
	const Color clrGood( 126, 222, 146, 255 );
	const Color clrBad( 236, 96, 84, 255 );
	const Color clrHover( 110, 190, 255, 38 );

	static HFont s_Fonts[FONT_COUNT];
	static bool s_bFontsLoaded = false;

	static const char *s_pszFontNames[FONT_COUNT] =
	{
		"HL2RPM.Title", "HL2RPM.Header", "HL2RPM.Body", "HL2RPM.BodySmall", "HL2RPM.Mono", "HL2RPM.MonoSmall", "HL2RPM.HudSmall",
		"HL2RPM.HudMedium", "HL2RPM.HudNumbers", "HL2RPM.HudGlyph", "HL2RPM.Huge"
	};

	void LoadFonts( IScheme *pScheme )
	{
		if ( !pScheme )
			return;
		HFont hDefault = pScheme->GetFont( "Default", true );
		for ( int i = 0; i < FONT_COUNT; ++i )
		{
			HFont h = pScheme->GetFont( s_pszFontNames[i], true );
			s_Fonts[i] = h ? h : hDefault;
		}
		s_bFontsLoaded = true;
	}

	HFont GetFont( int font )
	{
		if ( !s_bFontsLoaded )
		{
			HScheme hScheme = scheme()->GetScheme( "ClientScheme" );
			LoadFonts( scheme()->GetIScheme( hScheme ) );
		}
		return s_Fonts[clamp( font, 0, FONT_COUNT - 1 )];
	}

	float Scale()
	{
		int w, h;
		surface()->GetScreenSize( w, h );
		return h / 900.0f;
	}

	void Localize( const char *psz, wchar_t *pOut, int nOutBytes )
	{
		pOut[0] = L'\0';
		if ( !psz || !psz[0] )
			return;
		if ( psz[0] == '#' && g_pVGuiLocalize )
		{
			const wchar_t *pw = g_pVGuiLocalize->Find( psz + 1 );
			if ( pw )
			{
				V_wcsncpy( pOut, pw, nOutBytes );
				return;
			}
			g_pVGuiLocalize->ConvertANSIToUnicode( psz + 1, pOut, nOutBytes );
			return;
		}
		g_pVGuiLocalize->ConvertANSIToUnicode( psz, pOut, nOutBytes );
	}

	void LocalizeFormat( wchar_t *pOut, int nOutBytes, const char *pszToken, const char *pszFallbackFmt, const wchar_t *p1, const wchar_t *p2 )
	{
		wchar_t wFmt[512];
		const wchar_t *pFmt = ( pszToken && pszToken[0] == '#' ) ? g_pVGuiLocalize->Find( pszToken + 1 ) : NULL;
		if ( !pFmt )
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( pszFallbackFmt ? pszFallbackFmt : "%s1", wFmt, sizeof( wFmt ) );
			pFmt = wFmt;
		}
		g_pVGuiLocalize->ConstructString( pOut, nOutBytes, pFmt, 2, p1 ? p1 : L"", p2 ? p2 : L"" );
	}

	int TextWidth( HFont font, const wchar_t *pText, int nChars )
	{
		if ( !pText )
			return 0;
		int w = 0;
		const int n = nChars < 0 ? V_wcslen( pText ) : nChars;
		for ( int i = 0; i < n; ++i )
		{
			int a, b, c;
			surface()->GetCharABCwide( font, pText[i], a, b, c );
			w += a + b + c;
		}
		return w;
	}

	int FontTall( HFont font )
	{
		return surface()->GetFontTall( font );
	}

	void WrapText( HFont font, const wchar_t *pText, int maxWidth, CUtlVector<Span_t> &lines )
	{
		lines.RemoveAll();
		if ( !pText )
			return;
		const int len = V_wcslen( pText );
		int lineStart = 0;
		int lastBreak = -1;		// позиция пробела, где можно перенести
		int width = 0;
		for ( int i = 0; i <= len; ++i )
		{
			const wchar_t ch = i < len ? pText[i] : L'\0';
			if ( ch == L'\n' || ch == L'\0' )
			{
				Span_t s = { lineStart, i };
				lines.AddToTail( s );
				lineStart = i + 1;
				lastBreak = -1;
				width = 0;
				continue;
			}
			int a, b, c;
			surface()->GetCharABCwide( font, ch, a, b, c );
			const int cw = a + b + c;
			if ( ch == L' ' )
				lastBreak = i;
			if ( width + cw > maxWidth && i > lineStart )
			{
				if ( lastBreak > lineStart )
				{
					Span_t s = { lineStart, lastBreak };
					lines.AddToTail( s );
					lineStart = lastBreak + 1;
					i = lineStart - 1;	// пересчитать с начала новой строки
				}
				else
				{
					Span_t s = { lineStart, i };
					lines.AddToTail( s );
					lineStart = i;
					i = lineStart - 1;
				}
				lastBreak = -1;
				width = 0;
				continue;
			}
			width += cw;
		}
	}

	void DrawText( HFont font, int x, int y, const Color &clr, const wchar_t *pText, int nChars )
	{
		if ( !pText )
			return;
		surface()->DrawSetTextFont( font );
		surface()->DrawSetTextColor( clr );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( pText, nChars < 0 ? V_wcslen( pText ) : nChars );
	}

	void DrawTextRight( HFont font, int xRight, int y, const Color &clr, const wchar_t *pText )
	{
		DrawText( font, xRight - TextWidth( font, pText ), y, clr, pText );
	}

	void DrawRect( int x0, int y0, int x1, int y1, const Color &clr )
	{
		surface()->DrawSetColor( clr );
		surface()->DrawFilledRect( x0, y0, x1, y1 );
	}

	void DrawFrame( int x0, int y0, int x1, int y1, const Color &clr, int t )
	{
		surface()->DrawSetColor( clr );
		surface()->DrawFilledRect( x0, y0, x1, y0 + t );
		surface()->DrawFilledRect( x0, y1 - t, x1, y1 );
		surface()->DrawFilledRect( x0, y0, x0 + t, y1 );
		surface()->DrawFilledRect( x1 - t, y0, x1, y1 );
	}

	void DrawBrackets( int x0, int y0, int x1, int y1, int len, const Color &clr, int t )
	{
		surface()->DrawSetColor( clr );
		// верх-лево
		surface()->DrawFilledRect( x0, y0, x0 + len, y0 + t );
		surface()->DrawFilledRect( x0, y0, x0 + t, y0 + len );
		// верх-право
		surface()->DrawFilledRect( x1 - len, y0, x1, y0 + t );
		surface()->DrawFilledRect( x1 - t, y0, x1, y0 + len );
		// низ-лево
		surface()->DrawFilledRect( x0, y1 - t, x0 + len, y1 );
		surface()->DrawFilledRect( x0, y1 - len, x0 + t, y1 );
		// низ-право
		surface()->DrawFilledRect( x1 - len, y1 - t, x1, y1 );
		surface()->DrawFilledRect( x1 - t, y1 - len, x1, y1 );
	}

	void DrawWindow( int x0, int y0, int x1, int y1, bool bScanlines )
	{
		DrawRect( x0 - S( 6 ), y0 - S( 6 ), x1 + S( 6 ), y1 + S( 6 ), clrPanelDeep );
		DrawRect( x0, y0, x1, y1, clrPanel );
		if ( bScanlines )
		{
			// лёгкие горизонтальные сканлайны - «экран» Альянса
			surface()->DrawSetColor( Color( 110, 190, 255, 7 ) );
			const int step = Max( 3, S( 4 ) );
			for ( int y = y0 + 1; y < y1; y += step )
				surface()->DrawFilledRect( x0, y, x1, y + 1 );
		}
		DrawFrame( x0, y0, x1, y1, clrLine );
		DrawBrackets( x0, y0, x1, y1, S( 14 ), clrAccent, Max( 1, S( 2 ) ) );
	}

	void DrawBar( int x0, int y0, int x1, int y1, float frac, const Color &clrFill, const Color &clrBack )
	{
		DrawRect( x0, y0, x1, y1, clrBack );
		frac = clamp( frac, 0.0f, 1.0f );
		DrawRect( x0, y0, x0 + (int)( ( x1 - x0 ) * frac ), y1, clrFill );
	}

	static int s_iWhiteTexture = -1;
	static void BindWhite()
	{
		if ( s_iWhiteTexture < 0 || !surface()->IsTextureIDValid( s_iWhiteTexture ) )
		{
			s_iWhiteTexture = surface()->CreateNewTextureID();
			surface()->DrawSetTextureFile( s_iWhiteTexture, "vgui/white", true, false );
		}
		surface()->DrawSetTexture( s_iWhiteTexture );
	}

	void DrawLine( int x0, int y0, int x1, int y1, const Color &clr )
	{
		surface()->DrawSetColor( clr );
		surface()->DrawLine( x0, y0, x1, y1 );
	}

	void DrawPolygon( int n, const float *px, const float *py, const Color &clr )
	{
		if ( n < 3 || n > 64 )
			return;
		Vertex_t v[64];
		for ( int i = 0; i < n; ++i )
			v[i].Init( Vector2D( px[i], py[i] ), Vector2D( 0.5f, 0.5f ) );
		BindWhite();
		surface()->DrawSetColor( clr );
		surface()->DrawTexturedPolygon( n, v );
	}

	void DrawDisc( float cx, float cy, float r, const Color &clr, int segments )
	{
		segments = clamp( segments, 6, 48 );
		float px[48], py[48];
		for ( int i = 0; i < segments; ++i )
		{
			const float a = ( 2.0f * M_PI_F * i ) / segments;
			px[i] = cx + cosf( a ) * r;
			py[i] = cy + sinf( a ) * r;
		}
		DrawPolygon( segments, px, py, clr );
	}

	void DrawDiamond( float cx, float cy, float r, const Color &clr )
	{
		const float px[4] = { cx, cx + r, cx, cx - r };
		const float py[4] = { cy - r, cy, cy + r, cy };
		DrawPolygon( 4, px, py, clr );
	}

	void DrawFade( int x0, int y0, int x1, int y1, const Color &clr, int a0, int a1, bool bHorizontal )
	{
		surface()->DrawSetColor( Color( clr.r(), clr.g(), clr.b(), 255 ) );
		surface()->DrawFilledRectFade( x0, y0, x1, y1, clamp( a0, 0, 255 ), clamp( a1, 0, 255 ), bHorizontal );
	}

	Color Fade( const Color &clr, float alpha )
	{
		return Color( clr.r(), clr.g(), clr.b(), (int)clamp( clr.a() * alpha, 0.0f, 255.0f ) );
	}

	void PlayUISound( const char *pszSound )
	{
		if ( pszSound && pszSound[0] )
			surface()->PlaySound( pszSound );
	}
}

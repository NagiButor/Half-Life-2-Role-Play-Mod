//========= HL2RPM ============//
//
// RPG-интерфейс: общая палитра, шрифты и утилиты рисования (браслет, диалог, HUD, экраны).
// Стиль - «как если бы Valve сделала RPG в HL2»: тёмные панели, голубой акцент Альянса,
// янтарь HL2 для имён и предупреждений, тонкие линии и угловые скобки.
//
//=============================//

#ifndef RPG_UI_COMMON_H
#define RPG_UI_COMMON_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui/VGUI.h>
#include <Color.h>
#include "utlvector.h"

namespace vgui { class IScheme; }

namespace RPGUI
{
	// палитра (RPG_SPEC §9)
	extern const Color clrPanel;		// фон панелей
	extern const Color clrPanelDeep;	// подложка под панелями/окнами
	extern const Color clrLine;			// тонкие линии, рамки
	extern const Color clrAccent;		// голубой Альянса
	extern const Color clrAccentDim;
	extern const Color clrText;			// основной текст
	extern const Color clrTextDim;
	extern const Color clrTextFaint;
	extern const Color clrAmber;		// янтарь HL2: имена, заголовки, предупреждения
	extern const Color clrGood;			// проверка проходит
	extern const Color clrBad;			// не проходит / опасность
	extern const Color clrHover;		// подсветка строки под курсором

	// шрифты (clientscheme.res, HL2RPM.*): при отсутствии - Default
	enum Font_t
	{
		FONT_TITLE = 0,		// заголовки окон (Fira Sans Condensed SemiBold)
		FONT_HEADER,		// подзаголовки, имя собеседника
		FONT_BODY,			// основной текст (Fira Sans)
		FONT_BODY_SMALL,
		FONT_MONO,			// цифры, коды, данные (Fira Mono)
		FONT_MONO_SMALL,
		FONT_HUD_SMALL,		// подписи HUD
		FONT_HUD_MEDIUM,	// строки HUD (лента, задание)
		FONT_HUD_NUMBERS,	// крупные цифры HUD (браслет)
		FONT_HUD_GLYPH,		// моноширинные значки HUD (компас)
		FONT_HUGE,			// баннеры по центру (новый уровень, локация)
		FONT_COUNT
	};
	void LoadFonts( vgui::IScheme *pScheme );
	vgui::HFont GetFont( int font );

	// масштаб от высоты экрана (вёрстка под 900 строк)
	float Scale();
	inline int S( float v ) { return (int)( v * Scale() + 0.5f ); }

	// текст: токен (#...) или UTF-8 -> wide
	void Localize( const char *pszTokenOrText, wchar_t *pOut, int nOutBytes );
	// подставить %s1 %s2 в токен (или запасной формат UTF-8)
	void LocalizeFormat( wchar_t *pOut, int nOutBytes, const char *pszToken, const char *pszFallbackFmt, const wchar_t *p1, const wchar_t *p2 = NULL );

	int TextWidth( vgui::HFont font, const wchar_t *pText, int nChars = -1 );
	int FontTall( vgui::HFont font );
	// перенос по словам в ширину: строки - диапазоны [start, end) исходного текста
	struct Span_t { int start, end; };
	void WrapText( vgui::HFont font, const wchar_t *pText, int maxWidth, CUtlVector<Span_t> &lines );

	// рисование
	void DrawText( vgui::HFont font, int x, int y, const Color &clr, const wchar_t *pText, int nChars = -1 );
	void DrawTextRight( vgui::HFont font, int xRight, int y, const Color &clr, const wchar_t *pText );
	void DrawRect( int x0, int y0, int x1, int y1, const Color &clr );
	void DrawFrame( int x0, int y0, int x1, int y1, const Color &clr, int thickness = 1 );
	// угловые скобки по углам прямоугольника
	void DrawBrackets( int x0, int y0, int x1, int y1, int len, const Color &clr, int thickness = 1 );
	// фон окна: подложка + тонкая рамка + скобки + лёгкие сканлайны
	void DrawWindow( int x0, int y0, int x1, int y1, bool bScanlines = true );
	// полоса прогресса
	void DrawBar( int x0, int y0, int x1, int y1, float frac, const Color &clrFill, const Color &clrBack );
	// линия, залитый многоугольник (белая текстура), круг, ромб
	void DrawLine( int x0, int y0, int x1, int y1, const Color &clr );
	void DrawPolygon( int n, const float *px, const float *py, const Color &clr );
	void DrawDisc( float cx, float cy, float r, const Color &clr, int segments = 20 );
	void DrawDiamond( float cx, float cy, float r, const Color &clr );
	// прямоугольник с затуханием альфы (горизонтально: слева a0 -> справа a1)
	void DrawFade( int x0, int y0, int x1, int y1, const Color &clr, int a0, int a1, bool bHorizontal );
	// цвет с другой прозрачностью (alpha 0..1 - доля от исходной)
	Color Fade( const Color &clr, float alpha );

	// звук интерфейса
	void PlayUISound( const char *pszSound );
}

#endif // RPG_UI_COMMON_H

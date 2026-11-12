//========= HL2RPM ============================================================//
//
// The look of the HL2RPM windows (M6): black rounded plates like the HUD boxes of
// HL2, text in the accent color with a glow, thin double frames with corner ticks,
// the selected row a red bar that flashes, spaced capital headers, faint scan lines.
// Every window (dialog, the character menu, loot and trade, character creation) paints
// with these helpers, so the look is changed in one place.
//
// Sizes are given for a 900 pixel tall screen: Sc( v ) scales them to the real one
// (the RPMUI_* fonts of ClientScheme.res are proportional and scale the same way).
// Colors come from the theme of the suit the player wears (none / HEV / PCV / ...).
//
//=============================================================================//
#ifndef HL2RPM_UI_H
#define HL2RPM_UI_H
#pragma once

#include <vgui/VGUI.h>
#include <Color.h>
#include "utlvector.h"
#include "utlstring.h"

namespace vgui { class IScheme; }

namespace RPMUI
{
	enum Font_t { F_TITLE, F_TITLE_GLOW, F_TAB, F_TAB_GLOW, F_TEXT, F_TEXT_GLOW, F_SMALL, F_SMALL_GLOW, F_NUM, F_NUM_GLOW, F_COUNT };

	enum Theme_t { THEME_NONE, THEME_HEV, THEME_PCV, THEME_COMBINE, THEME_ELITE, THEME_WORKER, THEME_HAZMAT, THEME_COUNT };
	// (worker and hazmat: work suits, not combat ones - their HUD is drawn in a style of its own)
	inline bool IsWorkTheme( int t ) { return t == THEME_WORKER || t == THEME_HAZMAT; }

	struct Palette_t
	{
		Color accent;		// headers, values, normal text of lists
		Color dim;			// secondary text
		Color faint;		// thin lines, frames
		Color text;			// long texts (lines of NPCs, descriptions)
		Color grey;			// hints, unavailable
		Color good;			// passed checks, bonuses
		Color bad;			// failed checks, penalties
		Color bar;			// the selected row
		Color plate;		// rgb of the plates (alpha comes with the call)
	};

	void LoadFonts( vgui::IScheme *pScheme );
	vgui::HFont Font( int f );

	// the theme: rpm_ui_theme -1 = by the suit, else forced
	int CurrentTheme();
	const Palette_t &Pal();
	const Palette_t &PalOf( int theme );	// the colors of a given theme (the boot screen of a suit)
	const char *ThemeName( int theme );

	// sizes of a 900 pixel tall screen -> this screen
	float Scale();
	inline int Sc( float v ) { return (int)( v * Scale() + ( v >= 0 ? 0.5f : -0.5f ) ); }

	// text (UTF-8); align 0 left, 1 center, 2 right; returns the width
	int TextWidth( int font, const char *utf8 );
	int Text( int font, int x, int y, Color c, const char *utf8, bool glow = false, int align = 0, int maxW = 0 );
	int Spaced( int font, int x, int y, Color c, const char *utf8, int tracking, bool glow = false, int align = 0 );
	int SpacedWidth( int font, const char *utf8, int tracking );
	// word wrap inside width; maxLines 0 = all; returns the height used
	int Wrap( int font, int x, int y, int width, Color c, const char *utf8, int maxLines = 0, int lineGap = 2 );
	int WrapHeight( int font, int width, const char *utf8, int lineGap = 2 );
	int LineHeight( int font, int lineGap = 2 );

	// shapes
	void Plate( int x, int y, int w, int h, int alpha = 246, int radius = -1 );
	void FrameBox( int x, int y, int w, int h, int tick = -1, int alpha = 255 );
	void Highlight( int x, int y, int w, int h, float flash, int alpha = 255 );
	void Line( int x, int y, int w, Color c );
	void Bar( int x, int y, int w, int h, float frac, Color c );
	void Marker( int x, int y, int size, bool on, Color c );
	void Rect( int x, int y, int w, int h, Color c );
	void Arrow( int cx, int y, int size, bool up, Color c );	// a small triangle (more above / below)
	// filled polygons (convex, up to 64 points: x0 y0 x1 y1 ...), lines with a width, discs, rings
	// (angles in degrees, screen space: 0 = right, 90 = down)
	void Poly( int n, const float *xy, Color c );
	void Quad( float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, Color c );
	void ThickLine( float x0, float y0, float x1, float y1, float w, Color c );
	void Disc( float cx, float cy, float r, Color c, int seg = 40 );
	void Ring( float cx, float cy, float r0, float r1, Color c, int seg = 48, float a0 = 0.0f, float a1 = 360.0f );

	// 0..1 of an animation started at t0 lasting dur (eased out)
	float Ramp( float t0, float dur, float delay = 0.0f );
	Color Fade( Color c, float a );

	// interface sounds of HL2
	enum Sound_t { SND_HOVER, SND_CLICK, SND_OPEN, SND_CLOSE, SND_TAB, SND_DENY };
	void Sound( int s );

	// clickable regions of a painted window (rebuilt every paint)
	struct Hot_t
	{
		int x, y, w, h;
		int action, arg;
		CUtlString id;
	};

	class CHotList
	{
	public:
		void Begin( int mouseX, int mouseY ) { m_Hot.RemoveAll(); m_iMouseX = mouseX; m_iMouseY = mouseY; }
		int Add( int x, int y, int w, int h, int action, int arg = 0, const char *id = "" );
		bool Over( int x, int y, int w, int h ) const { return m_iMouseX >= x && m_iMouseX < x + w && m_iMouseY >= y && m_iMouseY < y + h; }
		const Hot_t *Hit( int x, int y ) const;
		const Hot_t *Hovered() const { return Hit( m_iMouseX, m_iMouseY ); }
		// a framed button with a centered label; returns true when hovered
		bool Button( int x, int y, int w, int h, const char *label, int action, int arg = 0, bool enabled = true, int font = F_TEXT );
		int m_iMouseX, m_iMouseY;
		CUtlVector<Hot_t> m_Hot;
	};

	// sounds a rollover when the hovered region changes
	class CHoverSound
	{
	public:
		CHoverSound() : m_iLast( -1 ), m_iLastArg( 0 ), m_flChange( 0 ) {}
		// returns the time the hover last changed (for the flash of the bar)
		float Update( const Hot_t *pHot );
		int m_iLast, m_iLastArg;
		float m_flChange;
	};
}

#endif // HL2RPM_UI_H

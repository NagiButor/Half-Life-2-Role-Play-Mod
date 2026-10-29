//========= HL2RPM ============//
//
// RPG-интерфейс: экраны чтения и сна.
//   «read» <classname>  - документ из инвентаря: имя предмета, текст #RPG_Item_<class>_Text (или описание)
//   «note» <id>         - записка: #NOTE_<id>_TITLE / #NOTE_<id>_TEXT (попадает в «Данные -> Заметки»)
//   «terminal» <id>     - терминал Альянса: тот же текст моноширинным шрифтом на зелёном экране
//   «sleep» <часы>      - кровать (prop_rpg_usable use_action sleep): выбор часов -> rpg_sleep <часы>
// Сервер присылает их через RPG_Notify OPEN_UI (см. c_rpg_character.cpp).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_conditions.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

enum { BTN_READER_CLOSE = 1, BTN_SLEEP_MINUS = 10, BTN_SLEEP_PLUS = 11, BTN_SLEEP_GO = 12, BTN_SLEEP_CANCEL = 13, BTN_SCROLL_UP = 20, BTN_SCROLL_DOWN = 21,
       BTN_TRAVEL_CANCEL = 30, BTN_TRAVEL_STOP = 100 };

// ---------------------------------------------------------------------------
class CRPGReader : public CRPGScreen
{
	DECLARE_CLASS_SIMPLE( CRPGReader, CRPGScreen );
public:
	CRPGReader() : BaseClass( "RPGReader" ), m_bTerminal( false ), m_iScroll( 0 ) { m_wTitle[0] = m_wText[0] = 0; }

	void Show( const char *pszKind, const char *pszParam )
	{
		m_bTerminal = !Q_stricmp( pszKind, "terminal" );
		m_iScroll = 0;
		m_wTitle[0] = m_wText[0] = 0;
		if ( !Q_stricmp( pszKind, "read" ) )
		{
			// документ: имя предмета + его текст
			const RPGItemDef_t *pDef = RPGData().FindItem( pszParam );
			Localize( pDef ? pDef->name.Get() : CFmtStr( "#RPG_Item_%s", pszParam ).Access(), m_wTitle, sizeof( m_wTitle ) );
			const wchar_t *pText = g_pVGuiLocalize->Find( CFmtStr( "RPG_Item_%s_Text", pszParam ) );
			if ( pText )
				V_wcsncpy( m_wText, pText, sizeof( m_wText ) );
			else if ( pDef )
				Localize( pDef->desc.Get(), m_wText, sizeof( m_wText ) );
		}
		else
		{
			Localize( CFmtStr( "#NOTE_%s_TITLE", pszParam ), m_wTitle, sizeof( m_wTitle ) );
			const wchar_t *pText = g_pVGuiLocalize->Find( CFmtStr( "NOTE_%s_TEXT", pszParam ) );
			if ( pText )
				V_wcsncpy( m_wText, pText, sizeof( m_wText ) );
		}
		Open();
		PlayUISound( m_bTerminal ? "buttons/combine_button2.wav" : "physics/cardboard/cardboard_box_impact_soft2.wav" );
	}

protected:
	virtual void OnKey( KeyCode code )
	{
		if ( code == KEY_E || code == KEY_SPACE || code == KEY_ENTER || code == KEY_TAB )
			Close();
		else if ( code == KEY_UP || code == KEY_W )
			m_iScroll = Max( 0, m_iScroll - 1 );
		else if ( code == KEY_DOWN || code == KEY_S )
			++m_iScroll;
	}
	virtual void OnWheel( int delta ) { m_iScroll = Max( 0, m_iScroll - delta ); }
	virtual void OnButton( int id, bool bRight )
	{
		if ( id == BTN_READER_CLOSE )
			Close();
		else if ( id == BTN_SCROLL_UP )
			m_iScroll = Max( 0, m_iScroll - 3 );
		else if ( id == BTN_SCROLL_DOWN )
			m_iScroll += 3;
	}

	virtual void PaintScreen()
	{
		DrawDimmer( 190 );
		const int sw = ScreenW(), sh = ScreenH();
		const int W = Min( S( 760 ), sw - S( 40 ) ), H = Min( S( 760 ), sh - S( 40 ) );
		const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
		const int pad = S( 40 );
		HFont fTitle = GetFont( FONT_TITLE ), fBody = m_bTerminal ? GetFont( FONT_MONO ) : GetFont( FONT_BODY ), fSmall = GetFont( FONT_MONO_SMALL );
		Color clrPaper, clrInk, clrTitle;
		if ( m_bTerminal )
		{
			// экран терминала: тёмно-зелёный, сканлайны
			DrawRect( x0, y0, x1, y1, Color( 6, 18, 12, 245 ) );
			for ( int y = y0; y < y1; y += S( 3 ) )
				DrawRect( x0, y, x1, y + 1, Color( 0, 0, 0, 60 ) );
			DrawFrame( x0, y0, x1, y1, Color( 90, 220, 140, 120 ), 2 );
			clrInk = Color( 120, 236, 160, 255 );
			clrTitle = Color( 170, 255, 200, 255 );
		}
		else
		{
			// бумага: тёплая, чуть неровная тень
			DrawRect( x0 + S( 8 ), y0 + S( 10 ), x1 + S( 8 ), y1 + S( 10 ), Color( 0, 0, 0, 120 ) );
			DrawRect( x0, y0, x1, y1, Color( 222, 212, 188, 255 ) );
			DrawFade( x0, y0, x1, y0 + S( 80 ), Color( 255, 250, 235 ), 60, 0, false );
			DrawFade( x0, y1 - S( 120 ), x1, y1, Color( 150, 130, 100 ), 0, 70, false );
			DrawRect( x0 + pad - S( 12 ), y0, x0 + pad - S( 11 ), y1, Color( 200, 80, 70, 90 ) );	// поле
			clrInk = Color( 44, 38, 32, 255 );
			clrTitle = Color( 30, 26, 22, 255 );
		}
		clrPaper = clrInk;
		int y = y0 + pad;
		if ( m_wTitle[0] )
		{
			DrawText( fTitle, x0 + pad, y, clrTitle, m_wTitle );
			y += FontTall( fTitle ) + S( 14 );
			DrawRect( x0 + pad, y - S( 8 ), x1 - pad, y - S( 7 ), Fade( clrInk, 0.4f ) );
		}
		CUtlVector<Span_t> lines;
		WrapText( fBody, m_wText, W - pad * 2, lines );
		const int lineH = FontTall( fBody ) + S( 5 );
		const int nVisible = Max( 1, ( y1 - pad - S( 40 ) - y ) / lineH );
		m_iScroll = clamp( m_iScroll, 0, Max( 0, lines.Count() - nVisible ) );
		for ( int i = m_iScroll; i < lines.Count() && i < m_iScroll + nVisible; ++i )
		{
			DrawText( fBody, x0 + pad, y, clrPaper, m_wText + lines[i].start, lines[i].end - lines[i].start );
			y += lineH;
		}
		if ( lines.Count() > nVisible )
		{
			TextButton( BTN_SCROLL_UP, x1 - S( 50 ), y0 + S( 14 ), x1 - S( 14 ), y0 + S( 44 ), L"\x25B2" );
			TextButton( BTN_SCROLL_DOWN, x1 - S( 50 ), y1 - S( 86 ), x1 - S( 14 ), y1 - S( 56 ), L"\x25BC" );
		}
		wchar_t w[128];
		Localize( "#HL2RPM_UI_Reader_Close", w, sizeof( w ) );
		DrawText( fSmall, x0 + pad, y1 - S( 34 ), Fade( clrInk, 0.6f ), w );
		Localize( "#HL2RPM_UI_BR_Close", w, sizeof( w ) );
		const int bx0 = x1 - pad - S( 140 ), by0 = y1 - S( 46 ), bx1 = x1 - pad, by1 = y1 - S( 14 );
		if ( m_bTerminal )
			TextButton( BTN_READER_CLOSE, bx0, by0, bx1, by1, w );
		else
		{
			// на бумаге - кнопка чернилами
			const bool bHover = Button( BTN_READER_CLOSE, bx0, by0, bx1, by1 );
			HFont fBtn = GetFont( FONT_HEADER );
			DrawRect( bx0, by0, bx1, by1, Fade( clrInk, bHover ? 0.16f : 0.07f ) );
			DrawFrame( bx0, by0, bx1, by1, Fade( clrInk, bHover ? 0.8f : 0.45f ) );
			DrawText( fBtn, ( bx0 + bx1 - TextWidth( fBtn, w ) ) / 2, ( by0 + by1 - FontTall( fBtn ) ) / 2, clrInk, w );
		}
	}

private:
	wchar_t m_wTitle[128];
	wchar_t m_wText[4096];
	bool m_bTerminal;
	int m_iScroll;
};

// ---------------------------------------------------------------------------
class CRPGSleep : public CRPGScreen
{
	DECLARE_CLASS_SIMPLE( CRPGSleep, CRPGScreen );
public:
	CRPGSleep() : BaseClass( "RPGSleep" ), m_iHours( 8 ) {}
	void Show( int iHours )
	{
		m_iHours = clamp( iHours > 0 ? iHours : 8, 1, 24 );
		Open();
	}
protected:
	virtual void OnKey( KeyCode code )
	{
		if ( code == KEY_LEFT || code == KEY_A || code == KEY_MINUS )
			m_iHours = Max( 1, m_iHours - 1 );
		else if ( code == KEY_RIGHT || code == KEY_D || code == KEY_EQUAL )
			m_iHours = Min( 24, m_iHours + 1 );
		else if ( code == KEY_ENTER || code == KEY_SPACE || code == KEY_E )
			Sleep();
	}
	virtual void OnWheel( int delta ) { m_iHours = clamp( m_iHours + delta, 1, 24 ); }
	virtual void OnButton( int id, bool bRight )
	{
		if ( id == BTN_SLEEP_MINUS )
			m_iHours = Max( 1, m_iHours - ( bRight ? 4 : 1 ) );
		else if ( id == BTN_SLEEP_PLUS )
			m_iHours = Min( 24, m_iHours + ( bRight ? 4 : 1 ) );
		else if ( id == BTN_SLEEP_GO )
			Sleep();
		else if ( id == BTN_SLEEP_CANCEL )
			Close();
	}
	void Sleep()
	{
		engine->ClientCmd( CFmtStr( "rpg_sleep %d\n", m_iHours ) );
		Close();
	}
	virtual void PaintScreen()
	{
		DrawDimmer( 200 );
		const int sw = ScreenW(), sh = ScreenH();
		const int W = S( 620 ), H = S( 380 );
		const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
		DrawWindow( x0, y0, x1, y1 );
		HFont fTitle = GetFont( FONT_TITLE ), fHuge = GetFont( FONT_HUGE ), fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL );
		wchar_t w[256], w2[64], w3[64];
		Localize( "#HL2RPM_UI_Sleep_Title", w, sizeof( w ) );
		DrawText( fTitle, x0 + S( 30 ), y0 + S( 24 ), clrAmber, w );
		// сейчас -> проснуться в
		float flHours = 12.0f, flRise, flSet;
		const bool bTime = RPG_GetWorldQuery() && RPG_GetWorldQuery()->GetTimeOfDay( flHours, flRise, flSet );
		const float flWake = fmodf( flHours + m_iHours, 24.0f );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%02d:%02d", (int)flHours % 24, (int)( ( flHours - floorf( flHours ) ) * 60 ) );
		V_snwprintf( w3, ARRAYSIZE( w3 ), L"%02d:%02d", (int)flWake % 24, (int)( ( flWake - floorf( flWake ) ) * 60 ) );
		if ( bTime )
		{
			LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_Sleep_Wake", "Now %s1 - wake up at %s2", w2, w3 );
			DrawText( fBody, x0 + S( 30 ), y0 + S( 30 ) + FontTall( fTitle ), clrTextDim, w );
		}
		// часы
		const int cx = ( x0 + x1 ) / 2, cy = y0 + S( 190 );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", m_iHours );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_Sleep_Hours", "%s1 h", w2 );
		DrawText( fHuge, cx - TextWidth( fHuge, w ) / 2, cy - FontTall( fHuge ) / 2, clrText, w );
		TextButton( BTN_SLEEP_MINUS, cx - S( 190 ), cy - S( 24 ), cx - S( 130 ), cy + S( 24 ), L"\x2212", m_iHours > 1 );
		TextButton( BTN_SLEEP_PLUS, cx + S( 130 ), cy - S( 24 ), cx + S( 190 ), cy + S( 24 ), L"+", m_iHours < 24 );
		// комендантский час
		if ( bTime && ( flWake >= 22.0f || flWake < 5.0f ) )
		{
			Localize( "#HL2RPM_UI_Sleep_Curfew", w, sizeof( w ) );
			DrawText( fSmall, cx - TextWidth( fSmall, w ) / 2, cy + S( 44 ), clrAmber, w );
		}
		Localize( "#HL2RPM_UI_Sleep_Go", w, sizeof( w ) );
		TextButton( BTN_SLEEP_GO, x1 - S( 250 ), y1 - S( 70 ), x1 - S( 30 ), y1 - S( 26 ), w, true, true );
		Localize( "#HL2RPM_UI_Sleep_Cancel", w, sizeof( w ) );
		TextButton( BTN_SLEEP_CANCEL, x0 + S( 30 ), y1 - S( 70 ), x0 + S( 200 ), y1 - S( 26 ), w );
	}
private:
	int m_iHours;
};

// ---------------------------------------------------------------------------
// Трамвай: маршрут по открытым остановкам (locations.txt "travel" 1) -> rpg_travel <локация>
// ---------------------------------------------------------------------------
class CRPGTravel : public CRPGScreen
{
	DECLARE_CLASS_SIMPLE( CRPGTravel, CRPGScreen );
public:
	CRPGTravel() : BaseClass( "RPGTravel" ) {}
	void Show()
	{
		if ( !IsOpen() )
			Open();
		PlayUISound( "buttons/combine_button1.wav" );
	}
protected:
	virtual void OnKey( KeyCode code )
	{
		if ( code == KEY_E )
			Close();
	}
	virtual void OnButton( int id, bool bRight )
	{
		if ( id == BTN_TRAVEL_CANCEL )
		{
			Close();
			return;
		}
		const int i = id - BTN_TRAVEL_STOP;
		if ( i >= 0 && i < RPGData().GetLocationCount() )
		{
			engine->ClientCmd( CFmtStr( "rpg_travel %s\n", RPGData().GetLocation( i ).id.Get() ) );
			PlayUISound( "ambient/levels/labs/electric_explosion1.wav" );
			Close();
		}
	}
	virtual void PaintScreen()
	{
		DrawDimmer( 200 );
		const int sw = ScreenW(), sh = ScreenH();
		const int W = S( 640 ), H = S( 520 );
		const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
		DrawWindow( x0, y0, x1, y1 );
		HFont fTitle = GetFont( FONT_TITLE ), fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL );
		wchar_t w[256];
		Localize( "#HL2RPM_UI_TR_Title", w, sizeof( w ) );
		DrawText( fTitle, x0 + S( 30 ), y0 + S( 24 ), clrAmber, w );
		Localize( "#HL2RPM_UI_TR_Time", w, sizeof( w ) );
		DrawText( fSmall, x0 + S( 30 ), y0 + S( 30 ) + FontTall( fTitle ), clrTextDim, w );

		const CRPGCharacterData &c = RPGClient().Data();
		char szMap[128];
		Q_FileBase( engine->GetLevelName(), szMap, sizeof( szMap ) );
		const int rowH = S( 44 );
		int y = y0 + S( 110 ), n = 0;
		for ( int i = 0; i < RPGData().GetLocationCount(); ++i )
		{
			const RPGLocationDef_t &loc = RPGData().GetLocation( i );
			if ( !loc.travel || !c.HasLocation( loc.id.Get() ) )
				continue;
			const bool bHere = !Q_stricmp( loc.map.Get(), szMap );
			Localize( loc.name.Get(), w, sizeof( w ) );
			if ( bHere )
			{
				DrawRect( x0 + S( 30 ), y, x1 - S( 30 ), y + rowH - S( 6 ), Fade( clrAccentDim, 0.4f ) );
				DrawText( fBody, x0 + S( 46 ), y + ( rowH - S( 6 ) - FontTall( fBody ) ) / 2, clrText, w );
				wchar_t wh[64];
				Localize( "#HL2RPM_UI_TR_Here", wh, sizeof( wh ) );
				DrawTextRight( fSmall, x1 - S( 46 ), y + ( rowH - S( 6 ) - FontTall( fSmall ) ) / 2, clrAccent, wh );
			}
			else
				TextButton( BTN_TRAVEL_STOP + i, x0 + S( 30 ), y, x1 - S( 30 ), y + rowH - S( 6 ), w );
			y += rowH;
			if ( ++n >= 7 )
				break;
		}
		if ( n == 0 )
		{
			Localize( "#HL2RPM_UI_TR_None", w, sizeof( w ) );
			DrawText( fBody, x0 + S( 30 ), y, clrTextFaint, w );
		}
		Localize( "#HL2RPM_UI_Sleep_Cancel", w, sizeof( w ) );
		TextButton( BTN_TRAVEL_CANCEL, x0 + S( 30 ), y1 - S( 70 ), x0 + S( 200 ), y1 - S( 26 ), w );
	}
};

// ---------------------------------------------------------------------------
class CRPGReaderSystem : public CAutoGameSystem, public IRPGClientListener
{
public:
	CRPGReaderSystem() : CAutoGameSystem( "CRPGReaderSystem" ), m_pReader( NULL ), m_pSleep( NULL ), m_pTravel( NULL ) {}
	virtual bool Init()
	{
		RPGClient().AddListener( this );
		return true;
	}
	virtual void Shutdown()
	{
		RPGClient().RemoveListener( this );
	}
	virtual void LevelInitPostEntity()
	{
		if ( !m_pReader )
			m_pReader = new CRPGReader();
		if ( !m_pSleep )
			m_pSleep = new CRPGSleep();
		if ( !m_pTravel )
			m_pTravel = new CRPGTravel();
	}
	virtual void LevelShutdownPreEntity()
	{
		if ( m_pReader && m_pReader->IsOpen() )
			m_pReader->Close();
		if ( m_pSleep && m_pSleep->IsOpen() )
			m_pSleep->Close();
		if ( m_pTravel && m_pTravel->IsOpen() )
			m_pTravel->Close();
	}
	virtual bool OnRPGOpenUI( const char *pszScreen, const char *pszParam, int iSource )
	{
		LevelInitPostEntity();
		if ( !Q_stricmp( pszScreen, "read" ) || !Q_stricmp( pszScreen, "note" ) || !Q_stricmp( pszScreen, "terminal" ) )
		{
			m_pReader->Show( pszScreen, pszParam ? pszParam : "" );
			return true;
		}
		if ( !Q_stricmp( pszScreen, "travel" ) )
		{
			m_pTravel->Show();
			return true;
		}
		if ( !Q_stricmp( pszScreen, "sleep" ) )
		{
			m_pSleep->Show( pszParam ? atoi( pszParam ) : 8 );
			return true;
		}
		return false;
	}
private:
	CRPGReader *m_pReader;
	CRPGSleep *m_pSleep;
	CRPGTravel *m_pTravel;
};
static CRPGReaderSystem g_RPGReaderSystem;

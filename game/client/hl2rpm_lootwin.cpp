//========= HL2RPM ============================================================//
//
// The loot / trade window of the new look (M6, hl2rpm_ui.h): two columns - the
// player on the left, the container / corpse / vendor on the right; a click (or
// E / Enter) moves one item to the other side, R takes everything; in trade the
// rows show prices and moving is buying / selling (the server decides: loot_system.cpp).
//
// The data stays with the old loot panel (vgui_lootpanel.cpp: the messages of the
// server go there); while this window is open that panel is hidden.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include "hl2rpm_itempreview.h"
#include "vgui_lootpanel.h"
#include "vgui_inventorypanel.h"
#include "iclientmode.h"
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui_controls/Panel.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

extern ConVar cl_rpm_menu;

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

enum LootHot_t { LH_ROW, LH_TAKEALL, LH_PUTALL, LH_CLOSE };

struct LootRow_t
{
	CUtlString cls;
	int slot;			// the first slot of the group (what one click moves)
	int count;
	int price;			// trade: price of one, -1 not for sale
	float weight;
};

class CRPMLootWindow : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMLootWindow, Panel );
public:
	CRPMLootWindow( Panel *parent ) : BaseClass( parent, "RPMLootWindow" )
	{
		SetPaintBackgroundEnabled( false );
		SetVisible( false );
		m_pData = NULL;
		m_iCol = 1;
		m_iSel[0] = m_iSel[1] = 0;
		m_iScroll[0] = m_iScroll[1] = 0;
		m_flOpenTime = m_flSelTime = 0.0f;
	}

	virtual void ApplySchemeSettings( IScheme *s )
	{
		BaseClass::ApplySchemeSettings( s );
		LoadFonts( s );
	}

	void Show( CVGuiLootPanel *pData )
	{
		m_pData = pData;
		if ( !IsVisible() )
		{
			if ( g_pClientMode && g_pClientMode->GetViewport() && GetParent() != g_pClientMode->GetViewport() )
				SetParent( g_pClientMode->GetViewport() );
			int sw, sh;
			surface()->GetScreenSize( sw, sh );
			SetBounds( 0, 0, sw, sh );
			SetVisible( true );
			MakePopup();
			SetZPos( 1500 );
			SetKeyBoardInputEnabled( true );
			SetMouseInputEnabled( true );
			MoveToFront();
			RequestFocus();
			surface()->SetCursor( dc_arrow );
			engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
			m_flOpenTime = m_flSelTime = gpGlobals->realtime;
			m_iCol = 1;
			m_iSel[0] = m_iSel[1] = 0;
			m_iScroll[0] = m_iScroll[1] = 0;
			const char *snd = pData->GetSoundOpen();
			surface()->PlaySound( snd[0] ? snd : "npc/combine_soldier/zipline_hitground2.wav" );
		}
		BuildRows();
		// the side that was emptied: go to the other one
		if ( !m_Rows[m_iCol].Count() && m_Rows[!m_iCol].Count() )
			m_iCol = !m_iCol;
	}

	void Hide()
	{
		if ( !IsVisible() )
			return;
		SetVisible( false );
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( false );
		engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
		const char *snd = m_pData ? m_pData->GetSoundClose() : "";
		surface()->PlaySound( snd[0] ? snd : "npc/combine_soldier/zipline_hitground1.wav" );
	}

	//---------------------------------------------------------------------
	void BuildRows()
	{
		for ( int c = 0; c < 2 && m_pData; c++ )
		{
			m_Rows[c].RemoveAll();
			const CUtlVector<CUtlString> &items = c ? m_pData->GetRightItems() : m_pData->GetLeftItems();
			const CUtlVector<int> &prices = c ? m_pData->GetRightPrices() : m_pData->GetLeftPrices();
			for ( int i = 0; i < items.Count(); i++ )
			{
				char cls[128];
				int count = 1;
				bool bExplicit = false;
				InventoryClient_ParseEntry( items[i].Get(), cls, sizeof( cls ), count, bExplicit );
				if ( !cls[0] )
					continue;
				bool stack = InventoryClient_IsAmmo( cls ) || RPMItems::IsStackable( cls );
				int n = stack && bExplicit ? Max( 1, count ) : 1;
				int price = m_pData->IsTrade() && i < prices.Count() ? prices[i] : -1;
				LootRow_t *r = NULL;
				// identical things in one row (stacks stay apart: a click moves a whole stack)
				if ( !stack )
					for ( int j = 0; j < m_Rows[c].Count(); j++ )
						if ( !Q_stricmp( m_Rows[c][j].cls, cls ) && m_Rows[c][j].price == price )
							r = &m_Rows[c][j];
				if ( r )
				{
					r->count += n;
					r->weight += InventoryClient_EntryWeight( items[i].Get() );
					continue;
				}
				r = &m_Rows[c][m_Rows[c].AddToTail()];
				r->cls = cls;
				r->slot = i;
				r->count = n;
				r->price = price;
				r->weight = InventoryClient_EntryWeight( items[i].Get() );
			}
			m_iSel[c] = clamp( m_iSel[c], 0, Max( 0, m_Rows[c].Count() - 1 ) );
		}
	}

	void Layout( int &x, int &y, int &w, int &h )
	{
		int sw, sh;
		GetSize( sw, sh );
		w = Min( Sc( 1180 ), sw - Sc( 100 ) );
		h = Min( Sc( 640 ), sh - Sc( 100 ) );
		x = ( sw - w ) / 2;
		y = ( sh - h ) / 2;
	}

	void ColumnRect( int c, int &cx, int &cy, int &cw, int &ch )
	{
		int x, y, w, h;
		Layout( x, y, w, h );
		cw = ( w - Sc( 36 ) * 2 - Sc( 40 ) ) / 2;
		cx = x + Sc( 36 ) + c * ( cw + Sc( 40 ) );
		cy = y + Sc( 96 );
		ch = h - Sc( 96 ) - Sc( 170 );
	}

	int RowH() { return Sc( 34 ); }
	int VisibleRows() { int cx, cy, cw, ch; ColumnRect( 0, cx, cy, cw, ch ); return Max( 1, ch / RowH() ); }

	//---------------------------------------------------------------------
	virtual void Paint()
	{
		if ( !m_pData )
			return;
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		m_Hot.Begin( mx, my );
		BuildRows();
		const Palette_t &p = Pal();
		int sw, sh;
		GetSize( sw, sh );
		float open = Ramp( m_flOpenTime, 0.2f );
		Rect( 0, 0, sw, sh, Color( 0, 0, 0, (int)( 150 * open ) ) );
		int x, y, w, h;
		Layout( x, y, w, h );
		int ph = Max( Sc( 8 ), (int)( h * open ) );
		Plate( x, y + ( h - ph ) / 2, w, ph, 247, Sc( 10 ) );
		FrameBox( x, y + ( h - ph ) / 2, w, ph, Sc( 18 ) );
		if ( open < 0.6f )
			return;
		float a = Ramp( m_flOpenTime, 0.2f, 0.12f );
		bool trade = m_pData->IsTrade();
		const char *cur = m_pData->GetCurrency()[0] ? m_pData->GetCurrency() : RPMItems::Name( "rpm_credits" );

		// the two headers
		for ( int c = 0; c < 2; c++ )
		{
			int cx, cy, cw, ch;
			ColumnRect( c, cx, cy, cw, ch );
			const char *title = c == 0 ? TR( "You", "Вы" ) : ( m_pData->GetDisplayName()[0] ? m_pData->GetDisplayName() : TR( "Container", "Контейнер" ) );
			Spaced( F_TAB, cx, y + Sc( 24 ), Fade( c == m_iCol ? p.accent : p.dim, a ), title, Sc( 3 ), c == m_iCol, 0 );
			if ( trade )
				Text( F_SMALL, cx + cw - Sc( 8 ), y + Sc( 30 ), Fade( p.dim, a ),
					CFmtStr( "%d %s", c ? m_pData->GetVendorMoney() : m_pData->GetPlayerMoney(), cur ), false, 2 );
			Line( cx - Sc( 6 ), y + Sc( 64 ), cw + Sc( 12 ), Fade( c == m_iCol ? p.faint : Color( p.accent.r(), p.accent.g(), p.accent.b(), 30 ), a ) );
			PaintColumn( c, a );
		}

		// the card of the selected thing
		PaintCard( x, y, w, h, a );

		// buttons and keys
		int by = y + h - Sc( 66 );
		if ( !trade )
		{
			m_Hot.Button( x + Sc( 36 ), by, Sc( 220 ), Sc( 32 ), TR( "Take all [R]", "Взять всё [R]" ), LH_TAKEALL, 0, m_Rows[1].Count() > 0 );
			m_Hot.Button( x + Sc( 270 ), by, Sc( 220 ), Sc( 32 ), TR( "Put all", "Положить всё" ), LH_PUTALL, 0, m_Rows[0].Count() > 0 );
		}
		m_Hot.Button( x + w - Sc( 36 ) - Sc( 200 ), by, Sc( 200 ), Sc( 32 ), TR( "Close [Esc]", "Закрыть [Esc]" ), LH_CLOSE );
		const char *keys = trade ? TR( "Click / E - buy or sell    \xc2\xb7    \xe2\x86\x90 \xe2\x86\x92 - side    \xc2\xb7    \xe2\x86\x91 \xe2\x86\x93 - list    \xc2\xb7    Esc - close",
			"Щелчок / E — купить или продать    ·    ← → — сторона    ·    ↑ ↓ — список    ·    Esc — закрыть" )
			: TR( "Click / E - move    \xc2\xb7    R - take all    \xc2\xb7    \xe2\x86\x90 \xe2\x86\x92 - side    \xc2\xb7    Esc - close",
				"Щелчок / E — переложить    ·    R — взять всё    ·    ← → — сторона    ·    Esc — закрыть" );
		Text( F_SMALL, x + w / 2, y + h - Sc( 26 ), Fade( p.grey, a ), keys, false, 1 );

		static CHoverSound s_Hover;
		s_Hover.Update( m_Hot.Hovered() );
	}

	void PaintColumn( int c, float a )
	{
		const Palette_t &p = Pal();
		int cx, cy, cw, ch;
		ColumnRect( c, cx, cy, cw, ch );
		int rows = VisibleRows();
		int maxScroll = Max( 0, m_Rows[c].Count() - rows );
		m_iScroll[c] = clamp( m_iScroll[c], 0, maxScroll );
		int first = m_iScroll[c];
		bool trade = m_pData->IsTrade();
		int money = c ? m_pData->GetPlayerMoney() : m_pData->GetVendorMoney();	// who pays for this side
		if ( !m_Rows[c].Count() )
			Text( F_TEXT, cx + Sc( 12 ), cy, Fade( p.grey, a ), TR( "Empty.", "Пусто." ) );
		for ( int i = first; i < m_Rows[c].Count() && i < first + rows; i++ )
		{
			const LootRow_t &r = m_Rows[c][i];
			int ry = cy + ( i - first ) * RowH();
			bool sel = c == m_iCol && i == m_iSel[c];
			if ( sel )
				Highlight( cx - Sc( 8 ), ry - Sc( 4 ), cw + Sc( 16 ), RowH() - Sc( 4 ), 1.0f - ( gpGlobals->realtime - m_flSelTime ) / 0.35f, (int)( 255 * a ) );
			bool blocked = trade && ( r.price < 0 || r.price > money );
			Color col = sel ? p.text : ( trade && r.price < 0 ) ? p.grey : p.accent;
			int th = surface()->GetFontTall( Font( F_TEXT ) );
			int ty = ry + ( RowH() - Sc( 8 ) - th ) / 2;
			CUtlString label = RPMItems::Name( r.cls );
			if ( r.count > 1 )
				label.Append( CFmtStr( " (%d)", r.count ) );
			CUtlString value;
			if ( trade )
				value = r.price < 0 ? CUtlString( TR( "-", "—" ) ) : CUtlString( CFmtStr( "%d", r.price ) );
			else
				value.Format( "%.1f", r.weight );
			int vw = TextWidth( F_TEXT, value ) + Sc( 16 );
			Text( F_TEXT, cx + Sc( 8 ), ty, Fade( col, a ), label, sel, 0, cw - Sc( 16 ) - vw );
			Text( F_TEXT, cx + cw - Sc( 8 ), ty, Fade( blocked && r.price >= 0 ? p.bad : col, a ), value, false, 2 );
			m_Hot.Add( cx - Sc( 8 ), ry - Sc( 4 ), cw + Sc( 16 ), RowH(), LH_ROW, c * 10000 + i );
		}
		if ( first > 0 )
			Arrow( cx + cw / 2, cy - Sc( 14 ), Sc( 5 ), true, Fade( p.dim, a ) );
		if ( first + rows < m_Rows[c].Count() )
			Arrow( cx + cw / 2, cy + rows * RowH(), Sc( 5 ), false, Fade( p.dim, a ) );
	}

	void PaintCard( int x, int y, int w, int h, float a )
	{
		const Palette_t &p = Pal();
		int cx, cy, cw, ch;
		ColumnRect( 0, cx, cy, cw, ch );
		int top = cy + ch + Sc( 6 ), bx = x + Sc( 30 ), bw = w - Sc( 60 );
		Line( bx, top, bw, Fade( p.faint, a ) );
		const LootRow_t *r = Selected();
		if ( !r )
			return;
		float ca = a * Ramp( m_flSelTime, 0.15f );
		const RPMItemDef_t *d = RPMItems::Find( r->cls );
		const char *desc = RPMItems::Desc( r->cls );
		int ty = top + Sc( 14 );
		// the model, turning, at the left of the strip
		const int mw = Sc( 124 ), mh = y + h - Sc( 72 ) - top - Sc( 4 );
		if ( RPMItemPreview::Paint( RPMItems::Model( r->cls ), bx + Sc( 4 ), top + Sc( 4 ), mw, mh,
			RPMItemPreview::YAW_START + ( gpGlobals->realtime - m_flSelTime ) * 30.0f, RPMItemPreview::PITCH_START, 1.0f, (int)( 255 * ca ), p.accent ) )
		{
			bx += mw + Sc( 12 );
			bw -= mw + Sc( 12 );
		}
		Spaced( F_SMALL, bx + Sc( 6 ), ty, Fade( p.accent, ca ), RPMItems::Name( r->cls ), Sc( 2 ), true );
		CUtlString facts;
		facts.Format( TR( "Weight %.1f kg", "Вес %.1f кг" ), r->count > 1 ? r->weight / r->count : r->weight );
		if ( RPMItems::Value( r->cls ) > 0 )
			facts.Append( CFmtStr( TR( "    \xc2\xb7    Value %d", "    ·    Цена %d" ), RPMItems::Value( r->cls ) ) );
		if ( d && d->damage > 0 )
			facts.Append( CFmtStr( TR( "    \xc2\xb7    Damage %.0f", "    ·    Урон %.0f" ), d->damage ) );
		if ( d && d->heal > 0 )
			facts.Append( CFmtStr( TR( "    \xc2\xb7    Heals %d", "    ·    Лечит %d" ), d->heal ) );
		Text( F_SMALL, bx + bw - Sc( 6 ), ty, Fade( p.dim, ca ), facts, false, 2 );
		if ( desc[0] )
			Wrap( F_SMALL, bx + Sc( 6 ), ty + Sc( 26 ), bw - Sc( 12 ), Fade( p.text, ca ), desc, 2 );
	}

	const LootRow_t *Selected()
	{
		int s = m_iSel[m_iCol];
		return s >= 0 && s < m_Rows[m_iCol].Count() ? &m_Rows[m_iCol][s] : NULL;
	}

	//---------------------------------------------------------------------
	void Select( int c, int i )
	{
		if ( c != m_iCol || i != m_iSel[c] )
		{
			m_iCol = c;
			m_iSel[c] = i;
			m_flSelTime = gpGlobals->realtime;
		}
		int rows = VisibleRows();
		if ( i < m_iScroll[c] )
			m_iScroll[c] = i;
		else if ( i >= m_iScroll[c] + rows )
			m_iScroll[c] = i - rows + 1;
	}

	// one row to the other side: take / put, buy / sell
	void Move()
	{
		const LootRow_t *r = Selected();
		if ( !r )
			return;
		if ( m_pData->IsTrade() )
		{
			int money = m_iCol ? m_pData->GetPlayerMoney() : m_pData->GetVendorMoney();
			if ( r->price < 0 || r->price > money )
			{
				Sound( SND_DENY );
				return;
			}
		}
		engine->ClientCmd( CFmtStr( "%s %d\n", m_iCol ? "loot_take_slot" : "loot_put_slot", r->slot ) );
		Sound( SND_CLICK );
	}

	void Act( int action, int arg )
	{
		switch ( action )
		{
		case LH_ROW:
		{
			int c = arg / 10000, i = arg % 10000;
			Select( c, i );
			Move();
			break;
		}
		case LH_TAKEALL: engine->ClientCmd( "loot_take_all\n" ); Sound( SND_CLICK ); break;
		case LH_PUTALL: engine->ClientCmd( "loot_put_all\n" ); Sound( SND_CLICK ); break;
		case LH_CLOSE: Hide(); break;
		}
	}

	virtual void OnMousePressed( MouseCode code )
	{
		if ( code != MOUSE_LEFT )
			return;
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		const Hot_t *hot = m_Hot.Hit( mx, my );
		if ( hot )
			Act( hot->action, hot->arg );
	}

	virtual void OnCursorMoved( int x, int y )
	{
		const Hot_t *hot = m_Hot.Hit( x, y );
		if ( hot && hot->action == LH_ROW )
			Select( hot->arg / 10000, hot->arg % 10000 );
	}

	virtual void OnMouseWheeled( int delta )
	{
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		int cx, cy, cw, ch;
		ColumnRect( 1, cx, cy, cw, ch );
		int c = mx >= cx - Sc( 20 ) ? 1 : 0;
		m_iScroll[c] = clamp( m_iScroll[c] - delta, 0, Max( 0, m_Rows[c].Count() - VisibleRows() ) );
	}

	virtual void OnKeyCodePressed( KeyCode code )
	{
		// E (use) and Tab are pressed, not repeated: holding E does not empty the box
		if ( code == KEY_E || code == KEY_ENTER || code == KEY_PAD_ENTER || code == KEY_SPACE )
		{
			Move();
			return;
		}
		if ( code == KEY_TAB )
		{
			Hide();
			return;
		}
		if ( code == KEY_R && !m_pData->IsTrade() )
		{
			Act( LH_TAKEALL, 0 );
			return;
		}
		BaseClass::OnKeyCodePressed( code );
	}

	virtual void OnKeyCodeTyped( KeyCode code )
	{
		switch ( code )
		{
		case KEY_ESCAPE:
			Hide();
			engine->ClientCmd_Unrestricted( "gameui_hide\n" );
			engine->ClientCmd_Unrestricted( "wait;gameui_hide\n" );
			return;
		case KEY_LEFT: case KEY_A:
			if ( m_Rows[0].Count() )
				Select( 0, m_iSel[0] );
			return;
		case KEY_RIGHT: case KEY_D:
			if ( m_Rows[1].Count() )
				Select( 1, m_iSel[1] );
			return;
		case KEY_UP: case KEY_W:
			if ( m_Rows[m_iCol].Count() )
				Select( m_iCol, Max( 0, m_iSel[m_iCol] - 1 ) );
			return;
		case KEY_DOWN: case KEY_S:
			if ( m_Rows[m_iCol].Count() )
				Select( m_iCol, Min( m_Rows[m_iCol].Count() - 1, m_iSel[m_iCol] + 1 ) );
			return;
		case KEY_E: case KEY_ENTER: case KEY_PAD_ENTER: case KEY_SPACE: case KEY_TAB: case KEY_R:
			return;
		default: break;
		}
		BaseClass::OnKeyCodeTyped( code );
	}

	// tests: rpm_loot_key
	void SimulateKey( KeyCode code ) { BuildRows(); OnKeyCodeTyped( code ); OnKeyCodePressed( code ); }

	CVGuiLootPanel *m_pData;
	CUtlVector<LootRow_t> m_Rows[2];
	int m_iCol;
	int m_iSel[2], m_iScroll[2];
	float m_flOpenTime, m_flSelTime;
	CHotList m_Hot;
};

static CRPMLootWindow *s_pLootWin = NULL;

bool RPMLoot_Show( CVGuiLootPanel *pData )
{
	if ( !cl_rpm_menu.GetBool() )
		return false;
	if ( !s_pLootWin )
		s_pLootWin = new CRPMLootWindow( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
	s_pLootWin->Show( pData );
	return true;
}

void RPMLoot_Hide()
{
	if ( s_pLootWin )
		s_pLootWin->Hide();
}

bool RPMLoot_IsOpen()
{
	return s_pLootWin && s_pLootWin->IsVisible();
}

class CRPMLootWinSystem : public CAutoGameSystem
{
public:
	CRPMLootWinSystem() : CAutoGameSystem( "CRPMLootWinSystem" ) {}
	virtual void LevelShutdownPreEntity() { RPMLoot_Hide(); }
};
static CRPMLootWinSystem g_RPMLootWinSystem;

CON_COMMAND( rpm_loot_key, "HL2RPM: press a key of the loot window (tests): up down left right e r esc" )
{
	if ( !RPMLoot_IsOpen() || args.ArgC() < 2 )
		return;
	static const struct { const char *name; KeyCode code; } keys[] = { { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT },
		{ "right", KEY_RIGHT }, { "e", KEY_E }, { "r", KEY_R }, { "esc", KEY_ESCAPE } };
	for ( int i = 0; i < ARRAYSIZE( keys ); i++ )
		if ( !Q_stricmp( args[1], keys[i].name ) )
			s_pLootWin->SimulateKey( keys[i].code );
}

//========= HL2RPM ============//
//
// RPG-интерфейс: торговля (RPG_SPEC §9, Part V/11 GN-TR). Две колонки - товар торговца и имущество
// героя; цены - по навыку Торговли, репутации фракции торговца и торгу (эффект haggle), касса торговца.
// Цены, склад и касса считаются тем же кодом, что и на сервере (CRPGCharacterData::GetTrader*), поэтому
// экран показывает ровно то, что спишет сделка. Открывается эффектом open_barter (диалог, logic_rpg
// OpenBarter); сделки - rpg_barter_buy / rpg_barter_sell <classname> [количество].
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_formulas.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

// rpg_bracelet.cpp
void RPGBracelet_ItemName( const char *pszClass, wchar_t *pOut, int nBytes );

enum
{
	BTN_BT_LEAVE = 1,
	BTN_BT_BUY = 1000,		// + строка товара
	BTN_BT_SELL = 3000,		// + строка имущества
};

class CRPGBarter : public CRPGScreen, public IRPGClientListener
{
	DECLARE_CLASS_SIMPLE( CRPGBarter, CRPGScreen );
public:
	CRPGBarter() : BaseClass( "RPGBarter" )
	{
		m_iScrollL = m_iScrollR = 0;
		m_Msg[0] = 0;
		m_flMsgTime = -100.0f;
		m_bMsgBad = false;
		m_bHaveInventory = false;
		RPGClient().AddListener( this );
	}
	~CRPGBarter() { RPGClient().RemoveListener( this ); }

	void Show( const char *pszTrader )
	{
		m_Trader = pszTrader ? pszTrader : "";
		m_iScrollL = m_iScrollR = 0;
		m_Msg[0] = 0;
		m_bHaveInventory = false;
		if ( !IsOpen() )
			Open();
		engine->ClientCmd( "inventory_request\n" );
	}

	// "class:bullets[:clip1:clip2:ammoIdx:ammoCount]" -> строки по классу
	void SetInventory( const CUtlVector<CUtlString> &items )
	{
		m_Own.RemoveAll();
		for ( int i = 0; i < items.Count(); ++i )
		{
			char szClass[64];
			Q_strncpy( szClass, items[i].Get(), sizeof( szClass ) );
			char *pColon = Q_strstr( szClass, ":" );
			if ( pColon )
				*pColon = 0;
			if ( !szClass[0] )
				continue;
			int j = 0;
			for ( ; j < m_Own.Count(); ++j )
			{
				if ( !Q_stricmp( m_Own[j].classname.Get(), szClass ) )
					break;
			}
			if ( j == m_Own.Count() )
			{
				RPGTraderItem_t &it = m_Own[m_Own.AddToTail()];
				it.classname = szClass;
				it.count = 0;
			}
			++m_Own[j].count;
		}
		m_bHaveInventory = true;
	}

	// ---- IRPGClientListener ----
	virtual void OnRPGNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra )
	{
		if ( !IsOpen() || iKind != RPG_NOTIFY_FAIL || !pszToken )
			return;
		Localize( pszToken, m_Msg, sizeof( m_Msg ) );
		m_flMsgTime = gpGlobals->curtime;
		m_bMsgBad = true;
	}

protected:
	virtual void OnOpened()
	{
		engine->ClientCmd( "inventory_ui_open 1\n" );
		PlayUISound( "ambient/levels/labs/coinslot1.wav" );
	}
	virtual void OnClosed()
	{
		engine->ClientCmd( "inventory_ui_open 0\n" );
	}
	virtual void OnKey( KeyCode code )
	{
		if ( code == KEY_E || code == KEY_TAB )
			Close();
	}
	virtual void OnWheel( int delta )
	{
		if ( MouseX() < ScreenW() / 2 )
			m_iScrollL = Max( 0, m_iScrollL - delta );
		else
			m_iScrollR = Max( 0, m_iScrollR - delta );
	}
	virtual void OnButton( int id, bool bRight )
	{
		const int count = bRight ? 5 : 1;
		if ( id == BTN_BT_LEAVE )
		{
			Close();
			return;
		}
		if ( id >= BTN_BT_BUY && id < BTN_BT_BUY + m_Goods.Count() )
		{
			engine->ClientCmd( CFmtStr( "rpg_barter_buy %s %d\n", m_Goods[id - BTN_BT_BUY].classname.Get(), count ) );
			PlayUISound( "ambient/levels/labs/coinslot1.wav" );
			m_Msg[0] = 0;
		}
		else if ( id >= BTN_BT_SELL && id < BTN_BT_SELL + m_Own.Count() )
		{
			engine->ClientCmd( CFmtStr( "rpg_barter_sell %s %d\n", m_Own[id - BTN_BT_SELL].classname.Get(), count ) );
			PlayUISound( "ambient/levels/labs/coinslot1.wav" );
			m_Msg[0] = 0;
		}
	}

	virtual void PaintScreen();

private:
	void PaintColumn( int x0, int y0, int x1, int y1, bool bGoods, const RPGTraderDef_t &t, int &iScroll );
	static void FormatCredits( int n, wchar_t *pOut, int nBytes )
	{
		wchar_t wNum[16];
		V_snwprintf( wNum, ARRAYSIZE( wNum ), L"%d", n );
		LocalizeFormat( pOut, nBytes, "#HL2RPM_UI_HUD_Credits", "%s1 cr.", wNum );
	}

	CUtlString m_Trader;
	CUtlVector<RPGTraderItem_t> m_Goods;	// склад торговца (на этот кадр)
	CUtlVector<RPGTraderItem_t> m_Own;		// имущество героя
	bool m_bHaveInventory;
	int m_iScrollL, m_iScrollR;
	wchar_t m_Msg[192];
	float m_flMsgTime;
	bool m_bMsgBad;
	CUtlString m_HoverClass;
};

void CRPGBarter::PaintColumn( int x0, int y0, int x1, int y1, bool bGoods, const RPGTraderDef_t &t, int &iScroll )
{
	const CRPGCharacterData &c = RPGClient().Data();
	const CUtlVector<RPGTraderItem_t> &list = bGoods ? m_Goods : m_Own;
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fMono = GetFont( FONT_MONO ), fMonoS = GetFont( FONT_MONO_SMALL );
	wchar_t w[192], w2[64];

	Localize( bGoods ? "#HL2RPM_UI_BT_Goods" : "#HL2RPM_UI_BT_Yours", w, sizeof( w ) );
	DrawText( fHead, x0, y0, clrText, w );
	Localize( "#HL2RPM_UI_BT_Price", w, sizeof( w ) );
	DrawTextRight( fMonoS, x1 - S( 12 ), y0 + FontTall( fHead ) - FontTall( fMonoS ), clrTextDim, w );
	const int top = y0 + FontTall( fHead ) + S( 10 );
	DrawLine( x0, top - S( 4 ), x1, top - S( 4 ), clrLine );

	const int rowH = S( 36 );
	const int nVisible = Max( 1, ( y1 - top ) / rowH );
	iScroll = clamp( iScroll, 0, Max( 0, list.Count() - nVisible ) );
	if ( list.Count() == 0 )
	{
		Localize( bGoods ? "#HL2RPM_UI_BT_SoldOut" : ( m_bHaveInventory ? "#HL2RPM_UI_BT_NothingToSell" : "#HL2RPM_UI_BT_Loading" ), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), top + S( 8 ), clrTextFaint, w );
		return;
	}
	const int cash = c.GetTraderCredits( t );
	for ( int r = 0; r < nVisible && iScroll + r < list.Count(); ++r )
	{
		const int i = iScroll + r;
		const RPGTraderItem_t &it = list[i];
		const int ry0 = top + r * rowH, ry1 = ry0 + rowH - S( 2 );
		const int price = bGoods ? c.GetTraderBuyPrice( t, it.classname.Get() ) : c.GetTraderSellPrice( t, it.classname.Get() );
		const bool bTradeable = price > 0;
		const bool bAfford = bGoods ? ( bTradeable && c.m_iCredits >= price ) : ( bTradeable && cash >= price );
		const int id = ( bGoods ? BTN_BT_BUY : BTN_BT_SELL ) + i;
		const bool bHover = Button( id, x0, ry0, x1, ry1, bTradeable );
		if ( bHover || MouseIn( x0, ry0, x1, ry1 ) )
		{
			DrawRect( x0, ry0, x1, ry1, clrHover );
			m_HoverClass = it.classname;
		}
		const int ty = ry0 + ( rowH - FontTall( fBody ) ) / 2 - S( 1 );
		RPGBracelet_ItemName( it.classname.Get(), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), ty, bTradeable ? clrText : clrTextFaint, w );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"\x00D7%d", it.count );
		DrawTextRight( fMonoS, x1 - S( 130 ), ty + ( FontTall( fBody ) - FontTall( fMonoS ) ) / 2, clrTextDim, w2 );
		if ( bTradeable )
		{
			V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", price );
			DrawTextRight( fMono, x1 - S( 12 ), ty, bAfford ? ( bGoods ? clrAmber : clrGood ) : clrBad, w2 );
		}
		else
			DrawTextRight( fMono, x1 - S( 12 ), ty, clrTextFaint, L"\x2014" );
	}
	// полоса прокрутки
	if ( list.Count() > nVisible )
	{
		const int h = y1 - top;
		const int bh = Max( S( 20 ), h * nVisible / list.Count() );
		const int by = top + ( h - bh ) * iScroll / Max( 1, list.Count() - nVisible );
		DrawRect( x1 + S( 4 ), top, x1 + S( 7 ), y1, Fade( clrLine, 0.5f ) );
		DrawRect( x1 + S( 4 ), by, x1 + S( 7 ), by + bh, clrAccentDim );
	}
}

void CRPGBarter::PaintScreen()
{
	const RPGTraderDef_t *pDef = RPGData().FindTrader( m_Trader.Get() );
	if ( !pDef )
	{
		Close();
		return;
	}
	const CRPGCharacterData &c = RPGClient().Data();
	c.GetTraderStockList( *pDef, m_Goods );
	m_HoverClass.Clear();

	DrawDimmer( 190 );
	const int sw = ScreenW(), sh = ScreenH();
	const int W = Min( sw - S( 60 ), S( 1240 ) ), H = Min( sh - S( 60 ), S( 720 ) );
	const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
	DrawWindow( x0, y0, x1, y1 );
	HFont fTitle = GetFont( FONT_TITLE ), fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ),
		fSmall = GetFont( FONT_BODY_SMALL ), fMonoS = GetFont( FONT_MONO_SMALL );
	wchar_t w[512], w2[64];

	// ---- шапка: торговец, касса ----
	Localize( "#HL2RPM_UI_BT_Title", w, sizeof( w ) );
	DrawText( fMonoS, x0 + S( 30 ), y0 + S( 22 ), clrAccent, w );
	Localize( pDef->name.Get(), w, sizeof( w ) );
	DrawText( fTitle, x0 + S( 30 ), y0 + S( 22 ) + FontTall( fMonoS ) + S( 2 ), clrAmber, w );
	FormatCredits( c.GetTraderCredits( *pDef ), w2, sizeof( w2 ) );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BT_TraderCash", "Till: %s1", w2 );
	DrawTextRight( fHead, x1 - S( 30 ), y0 + S( 30 ), clrText, w );
	// условия: навык Торговли, множители, торг
	const int barter = c.GetSkill( "barter" );
	const float flR = RPGFormulas::FactionPriceBonus( c.GetFactionTier( pDef->faction.Get() ) );
	const int haggle = c.GetTraderHaggle( *pDef );
	const float flBuy = Max( 1.0f, pDef->buyMult * RPGFormulas::BuyPriceMult( barter, flR ) * ( 1.0f - 0.01f * haggle ) );
	const float flSell = Min( 0.9f, pDef->sellMult * RPGFormulas::SellPriceMult( barter, flR ) * ( 1.0f + 0.01f * haggle ) );
	{
		char sz[96];
		Q_snprintf( sz, sizeof( sz ), "%d", barter );
		wchar_t wSkill[16], wRates[64];
		g_pVGuiLocalize->ConvertANSIToUnicode( sz, wSkill, sizeof( wSkill ) );
		V_snwprintf( wRates, ARRAYSIZE( wRates ), L"\x00D7%.2f / \x00D7%.2f", flBuy, flSell );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BT_Terms", "Barter %s1  ·  buy / sell %s2", wSkill, wRates );
		DrawTextRight( fSmall, x1 - S( 30 ), y0 + S( 30 ) + FontTall( fHead ) + S( 4 ), haggle > 0 ? clrGood : ( haggle < 0 ? clrBad : clrTextDim ), w );
	}
	DrawLine( x0 + S( 30 ), y0 + S( 104 ), x1 - S( 30 ), y0 + S( 104 ), clrLine );

	// ---- колонки ----
	const int mid = ( x0 + x1 ) / 2;
	const int colTop = y0 + S( 118 ), colBot = y1 - S( 178 );
	PaintColumn( x0 + S( 30 ), colTop, mid - S( 24 ), colBot, true, *pDef, m_iScrollL );
	PaintColumn( mid + S( 24 ), colTop, x1 - S( 40 ), colBot, false, *pDef, m_iScrollR );
	DrawLine( mid, colTop, mid, colBot, Fade( clrLine, 0.6f ) );

	// ---- низ: описание предмета под курсором, кредиты героя, подсказка ----
	const int fy = colBot + S( 16 );
	DrawLine( x0 + S( 30 ), fy - S( 6 ), x1 - S( 30 ), fy - S( 6 ), clrLine );
	if ( !m_HoverClass.IsEmpty() )
	{
		RPGBracelet_ItemName( m_HoverClass.Get(), w, sizeof( w ) );
		DrawText( fHead, x0 + S( 30 ), fy, clrAmber, w );
		const RPGItemDef_t *pItem = RPGData().FindItem( m_HoverClass.Get() );
		const char *pszDesc = pItem && pItem->desc.Length() ? pItem->desc.Get() : CFmtStr( "#RPG_Item_%s_Desc", m_HoverClass.Get() ).Access();
		if ( pszDesc[0] == '#' && g_pVGuiLocalize->Find( pszDesc + 1 ) )
		{
			Localize( pszDesc, w, sizeof( w ) );
			CUtlVector<Span_t> lines;
			const int wrapW = mid - S( 24 ) - ( x0 + S( 30 ) );
			WrapText( fSmall, w, wrapW, lines );
			for ( int i = 0; i < lines.Count() && i < 3; ++i )
				DrawText( fSmall, x0 + S( 30 ), fy + FontTall( fHead ) + S( 4 ) + i * FontTall( fSmall ), clrTextDim, w + lines[i].start, lines[i].end - lines[i].start );
		}
		if ( pItem && pItem->value <= 0 )
		{
			Localize( "#HL2RPM_UI_BT_NotForTrade", w, sizeof( w ) );
			DrawText( fSmall, x0 + S( 30 ), fy + FontTall( fHead ) + S( 8 ) + 3 * FontTall( fSmall ), clrTextFaint, w );
		}
		else if ( pItem && pItem->contraband > 0 && !pDef->acceptsContraband )
		{
			Localize( "#HL2RPM_UI_BT_NoContraband", w, sizeof( w ) );
			DrawText( fSmall, x0 + S( 30 ), fy + FontTall( fHead ) + S( 8 ) + 3 * FontTall( fSmall ), clrBad, w );
		}
	}
	// кредиты героя
	Localize( "#HL2RPM_UI_BT_YourCredits", w, sizeof( w ) );
	DrawText( fMonoS, mid + S( 24 ), fy, clrTextDim, w );
	FormatCredits( c.m_iCredits, w2, sizeof( w2 ) );
	DrawText( GetFont( FONT_HUGE ), mid + S( 24 ), fy + FontTall( fMonoS ) + S( 2 ), clrText, w2 );
	// отказ сервера (не хватает кредитов, касса пуста...)
	if ( m_Msg[0] && gpGlobals->curtime - m_flMsgTime < 4.0f )
		DrawText( fBody, mid + S( 24 ), y1 - S( 92 ), m_bMsgBad ? clrBad : clrGood, m_Msg );
	Localize( "#HL2RPM_UI_BT_Hint", w, sizeof( w ) );
	DrawText( fSmall, x0 + S( 30 ), y1 - S( 44 ), clrTextFaint, w );
	Localize( "#HL2RPM_UI_BT_Leave", w, sizeof( w ) );
	TextButton( BTN_BT_LEAVE, x1 - S( 230 ), y1 - S( 66 ), x1 - S( 30 ), y1 - S( 24 ), w, true, true );
}

// ---------------------------------------------------------------------------
static CRPGBarter *s_pBarter = NULL;

bool RPGBarter_WantsInventory()
{
	return s_pBarter && s_pBarter->IsOpen();
}

void RPGBarter_OnInventory( const CUtlVector<CUtlString> &items )
{
	if ( s_pBarter )
		s_pBarter->SetInventory( items );
}

class CRPGBarterSystem : public CAutoGameSystem, public IRPGClientListener
{
public:
	CRPGBarterSystem() : CAutoGameSystem( "CRPGBarterSystem" ) {}
	virtual bool Init()
	{
		RPGClient().AddListener( this );
		return true;
	}
	virtual void Shutdown()
	{
		RPGClient().RemoveListener( this );
	}
	virtual void LevelShutdownPreEntity()
	{
		if ( s_pBarter && s_pBarter->IsOpen() )
			s_pBarter->Close();
	}
	virtual bool OnRPGOpenUI( const char *pszScreen, const char *pszParam, int iSource )
	{
		if ( Q_stricmp( pszScreen, "barter" ) )
			return false;
		if ( !RPGData().FindTrader( pszParam ? pszParam : "" ) )
		{
			DevWarning( "[rpg] barter: unknown trader '%s'\n", pszParam ? pszParam : "" );
			return false;
		}
		if ( !s_pBarter )
			s_pBarter = new CRPGBarter();
		s_pBarter->Show( pszParam );
		return true;
	}
};
static CRPGBarterSystem g_RPGBarterSystem;

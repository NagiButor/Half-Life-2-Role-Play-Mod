//========= HL2RPM ============================================================//
//
// The character of the RPG on the client: the state the server sends (user message
// "RPMChar", hl2rpm_char.cpp) and one window in three modes - character creation,
// level up (skill points and a perk), the character sheet. Painted in the look of the
// dialog window; the menus of M6 will take it over.
//
//   rpm_char_open create|levelup|sheet     (the server opens creation / level up itself)
//   rpm_character                          the sheet (bind it to a key)
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "iclientmode.h"
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/TextEntry.h>
#include "KeyValues.h"
#include "tier1/fmtstr.h"
#include "usermessages.h"
#include "hl2rpm_ui.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

extern bool HL2RPM_IsDialogOpen();

//=============================================================================
// state from the server
//=============================================================================
static KeyValues *s_pCharState = NULL;
static CUtlString s_CharAccum;

static KeyValues *CharState()
{
	if ( !s_pCharState )
		s_pCharState = new KeyValues( "Char" );
	return s_pCharState;
}

// the state for other windows (the M6 UI)
KeyValues *RPMChar_ClientState()
{
	return CharState();
}

static void __MsgFunc_RPMChar( bf_read &msg )
{
	int flags = msg.ReadByte();
	char buf[256];
	msg.ReadString( buf, sizeof( buf ) );
	if ( flags & 1 )
		s_CharAccum.Clear();
	s_CharAccum.Append( buf );
	if ( flags & 2 )
	{
		KeyValues *k = new KeyValues( "Char" );
		if ( k->LoadFromBuffer( "RPMChar", s_CharAccum.Get() ) )
		{
			if ( s_pCharState )
				s_pCharState->deleteThis();
			s_pCharState = k;
		}
		else
			k->deleteThis();
		s_CharAccum.Clear();
	}
}

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

static bool ListHas( const char *pszList, const char *pszItem )
{
	CUtlStringList parts;
	V_SplitString( pszList ? pszList : "", ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
		if ( !Q_stricmp( parts[i], pszItem ) )
			return true;
	return false;
}

// effect of one perk / trait definition (rank times) on an attribute or skill
static int DefEffect( KeyValues *pDef, int times, const char *type, const char *id )
{
	float sum = 0;
	KeyValues *e = pDef ? pDef->FindKey( "effects" ) : NULL;
	for ( KeyValues *x = e ? e->GetFirstTrueSubKey() : NULL; x; x = x->GetNextTrueSubKey() )
		if ( !Q_stricmp( x->GetString( "type" ), type ) && ( !id || !Q_stricmp( x->GetString( "id" ), id ) ) )
			sum += x->GetFloat( "value" ) * times;
	return (int)sum;
}

//=============================================================================
// the window
//=============================================================================
enum CharMode_t { MODE_CREATE, MODE_LEVELUP, MODE_SHEET };

// the game stands still while the character is created or levelled up (like the menus of New Vegas):
// NPCs do not walk up and start a dialog, nobody attacks
ConVar cl_rpm_char_pause( "cl_rpm_char_pause", "1", FCVAR_ARCHIVE, "HL2RPM: pause the game while the character creation / level up window is open" );
enum HotAction_t { HOT_NONE, HOT_STAT_MINUS, HOT_STAT_PLUS, HOT_TAG, HOT_TRAIT, HOT_SKILL_MINUS, HOT_SKILL_PLUS, HOT_PERK,
	HOT_DONE, HOT_LATER, HOT_RESET, HOT_TO_LEVELUP, HOT_INFO };

struct Hot_t
{
	int x, y, w, h;
	int action;
	int arg;
	CUtlString info;		// id whose description shows on hover
};

class CRPMCharPanel : public Frame
{
	DECLARE_CLASS_SIMPLE( CRPMCharPanel, Frame );
public:
	CRPMCharPanel( Panel *parent ) : BaseClass( parent, "RPMCharPanel" )
	{
		SetProportional( false );
		SetTitle( "", false );
		SetTitleBarVisible( false );
		SetMenuButtonVisible( false );
		SetCloseButtonVisible( false );
		SetMinimizeButtonVisible( false );
		SetMaximizeButtonVisible( false );
		SetSizeable( false );
		SetMoveable( false );
		SetVisible( false );
		SetPaintBackgroundEnabled( true );
		m_pName = new TextEntry( this, "CharName" );
		m_pName->SetMaximumCharCount( 32 );
		m_pName->SetVisible( false );
		m_hFont = m_hFontBig = m_hFontSmall = INVALID_FONT;
		m_iOX = m_iOY = 0;
		m_flOpenTime = 0.0f;
		m_iMode = MODE_SHEET;
		m_iHover = -1;
		m_iPerkSel = -1;
	}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		RPMUI::LoadFonts( pScheme );
		m_hFont = RPMUI::Font( RPMUI::F_TEXT );
		m_hFontSmall = RPMUI::Font( RPMUI::F_SMALL );
		m_hFontBig = RPMUI::Font( RPMUI::F_TAB );
		SetBgColor( Color( 0, 0, 0, 0 ) );
		SetBorder( NULL );
		m_pName->SetFont( m_hFont );
		m_pName->SetBgColor( Color( 0, 0, 0, 0 ) );
		m_pName->SetFgColor( RPMUI::Pal().text );
		m_pName->SetSelectionBgColor( RPMUI::Pal().bar );
		m_pName->SetBorder( NULL );
	}

	void Open( int mode )
	{
		m_iMode = mode;
		if ( g_pClientMode && g_pClientMode->GetViewport() && GetParent() != g_pClientMode->GetViewport() )
			SetParent( g_pClientMode->GetViewport() );
		int sx, sy;
		surface()->GetScreenSize( sx, sy );
		SetBounds( 0, 0, sx, sy );
		UpdateOrigin();
		m_flOpenTime = gpGlobals->realtime;
		KeyValues *st = CharState();
		if ( mode == MODE_CREATE )
		{
			for ( int i = 0; i < RPM_STAT_COUNT; i++ )
			{
				int v = st->FindKey( "stats_base", true )->GetInt( RPMRPG::StatId( i ), (int)RPMRPG::Rule( "stat_start" ) );
				m_Stats[i] = st->GetInt( "created" ) ? v : (int)RPMRPG::Rule( "stat_start" );
			}
			for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
				m_Tags[i] = st->GetInt( "created" ) && ListHas( st->GetString( "tags" ), RPMRPG::SkillId( i ) );
			m_Traits.RemoveAll();
			if ( st->GetInt( "created" ) )
			{
				CUtlStringList parts;
				V_SplitString( st->GetString( "traits" ), ";", parts );
				for ( int i = 0; i < parts.Count(); i++ )
					if ( parts[i][0] )
						m_Traits.AddToTail( parts[i] );
			}
			const char *name = st->GetString( "name" );
			m_pName->SetText( name[0] ? name : TR( "Citizen", "Гражданин" ) );
			m_pName->SetBounds( X( 150 ) + RPMUI::Sc( 6 ), Y( 52 ), S( 316 ), S( 30 ) );
			m_pName->SetFgColor( RPMUI::Pal().text );
			m_pName->SetVisible( true );
		}
		else
			m_pName->SetVisible( false );
		for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
			m_Add[i] = 0;
		m_iPerkSel = -1;
		m_Info.Clear();
		SetVisible( true );
		SetAlpha( 255 );
		DisableFadeEffect();
		if ( ( mode == MODE_CREATE || mode == MODE_LEVELUP ) && cl_rpm_char_pause.GetBool() && !m_bPaused )
		{
			engine->ClientCmd_Unrestricted( "setpause nomsg\n" );
			m_bPaused = true;
		}
		MakePopup();
		SetZPos( 2000 );		// over the dialog window (1000)
		SetKeyBoardInputEnabled( true );
		SetMouseInputEnabled( true );
		MoveToFront();
		RequestFocus();
		surface()->SetCursor( dc_arrow );
	}

	void Close()
	{
		SetVisible( false );
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( false );
		if ( m_bPaused )
		{
			// after the apply command of Done(): the commands run in order
			engine->ClientCmd_Unrestricted( "unpause nomsg\n" );
			m_bPaused = false;
		}
	}

	int Mode() const { return m_iMode; }

	virtual void OnThink()
	{
		BaseClass::OnThink();
		// a dialog started by an NPC while the window is open must not cover it
		if ( IsVisible() && HL2RPM_IsDialogOpen() && GetAlpha() == 255 )
			MoveToFront();
	}

	virtual void OnKeyCodeTyped( KeyCode code )
	{
		if ( code == KEY_ESCAPE )
		{
			Close();
			return;
		}
		BaseClass::OnKeyCodeTyped( code );
	}

	//---------------------------------------------------------------------
	// drawing helpers: coordinates are of a 1120 x 700 window at a 900 pixel
	// tall screen; X / Y / S turn them into pixels of this screen
	//---------------------------------------------------------------------
	int S( int v ) { return RPMUI::Sc( (float)v ); }
	int X( int v ) { return m_iOX + S( v ); }
	int Y( int v ) { return m_iOY + S( v ); }

	void WinRect( int &x, int &y, int &w, int &h )
	{
		int sw, sh;
		GetSize( sw, sh );
		w = Min( S( 1120 ), sw - S( 60 ) );
		h = Min( S( 700 ), sh - S( 60 ) );
		x = ( sw - w ) / 2;
		y = ( sh - h ) / 2;
	}

	void UpdateOrigin()
	{
		int w, h;
		WinRect( m_iOX, m_iOY, w, h );
	}

	// the size of the window in layout units
	void WinSize( int &w, int &h )
	{
		int x, y;
		WinRect( x, y, w, h );
		float s = RPMUI::Scale();
		w = (int)( w / s );
		h = (int)( h / s );
	}

	int Text( HFont f, int x, int y, Color c, const char *utf8, int maxW = 0 )
	{
		x = X( x );
		y = Y( y );
		maxW = maxW > 0 ? S( maxW ) : 0;
		wchar_t w[1024];
		g_pVGuiLocalize->ConvertANSIToUnicode( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( f, w, tw, th );
		if ( maxW > 0 && tw > maxW )
		{
			// cut with an ellipsis
			int n = V_wcslen( w );
			while ( n > 1 )
			{
				w[--n] = 0;
				w[n - 1] = L'.';
				surface()->GetTextSize( f, w, tw, th );
				if ( tw <= maxW )
					break;
			}
		}
		surface()->DrawSetTextFont( f );
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( w, V_wcslen( w ) );
		return (int)( tw / RPMUI::Scale() );
	}

	// word-wrapped text in a box; returns the height used
	int TextWrap( HFont f, int x, int y, int w, Color c, const char *utf8 )
	{
		x = X( x );
		y = Y( y );
		w = S( w );
		wchar_t text[2048];
		g_pVGuiLocalize->ConvertANSIToUnicode( utf8, text, sizeof( text ) );
		int lineH = surface()->GetFontTall( f ) + S( 2 );
		int cy = y;
		int len = V_wcslen( text );
		int start = 0;
		while ( start < len )
		{
			int lastSpace = -1, i = start, width = 0;
			for ( ; i < len && text[i] != L'\n'; i++ )
			{
				width += surface()->GetCharacterWidth( f, text[i] );
				if ( text[i] == L' ' )
					lastSpace = i;
				if ( width > w && lastSpace > start )
				{
					i = lastSpace;
					break;
				}
			}
			surface()->DrawSetTextFont( f );
			surface()->DrawSetTextColor( c );
			surface()->DrawSetTextPos( x, cy );
			surface()->DrawPrintText( text + start, i - start );
			cy += lineH;
			start = i + 1;
		}
		return (int)( ( cy - y ) / RPMUI::Scale() );
	}

	int AddHot( int x, int y, int w, int h, int action, int arg, const char *info = "" )
	{
		Hot_t hot;
		hot.x = X( x ); hot.y = Y( y ); hot.w = S( w ); hot.h = S( h );
		hot.action = action; hot.arg = arg;
		hot.info = info;
		return m_Hot.AddToTail( hot );
	}

	// in layout units
	bool IsHover( int x, int y, int w, int h )
	{
		x = X( x ); y = Y( y ); w = S( w ); h = S( h );
		return m_iMouseX >= x && m_iMouseX < x + w && m_iMouseY >= y && m_iMouseY < y + h;
	}

	void Button( int x, int y, int w, const char *label, int action, int arg = 0, bool bEnabled = true )
	{
		using namespace RPMUI;
		const Palette_t &p = Pal();
		const int h = 32;
		bool hover = bEnabled && IsHover( x, y, w, h );
		if ( hover )
			Highlight( X( x ), Y( y ), S( w ), S( h ), 0.0f );
		else
			Rect( X( x ), Y( y ), S( w ), S( h ), Color( p.plate.r(), p.plate.g(), p.plate.b(), 200 ) );
		Color fc = bEnabled ? p.accent : p.grey;
		surface()->DrawSetColor( fc.r(), fc.g(), fc.b(), bEnabled ? 150 : 70 );
		surface()->DrawOutlinedRect( X( x ), Y( y ), X( x ) + S( w ), Y( y ) + S( h ) );
		int th = surface()->GetFontTall( m_hFont );
		RPMUI::Text( F_TEXT, X( x ) + S( w ) / 2, Y( y ) + ( S( h ) - th ) / 2, hover ? p.text : fc, label, hover, 1 );
		if ( bEnabled )
			AddHot( x, y, w, h, action, arg );
	}

	void SmallButton( int x, int y, const char *label, int action, int arg, bool bEnabled )
	{
		using namespace RPMUI;
		const Palette_t &p = Pal();
		const int s = 22;
		bool hover = bEnabled && IsHover( x, y, s, s );
		if ( hover )
			Highlight( X( x ), Y( y ), S( s ), S( s ), 0.0f );
		Color fc = bEnabled ? p.accent : p.grey;
		surface()->DrawSetColor( fc.r(), fc.g(), fc.b(), bEnabled ? 160 : 60 );
		surface()->DrawOutlinedRect( X( x ), Y( y ), X( x ) + S( s ), Y( y ) + S( s ) );
		int th = surface()->GetFontTall( m_hFont );
		RPMUI::Text( F_TEXT, X( x ) + S( s ) / 2, Y( y ) + ( S( s ) - th ) / 2, hover ? p.text : fc, label, false, 1 );
		if ( bEnabled )
			AddHot( x, y, s, s, action, arg );
	}

	// a small square: filled when on (tagged skill, chosen trait / perk)
	void Marker( int x, int y, bool on )
	{
		RPMUI::Marker( X( x ), Y( y + 6 ), S( 11 ), on, RPMUI::Pal().accent );
	}

	void Header( int x, int y, const char *text )
	{
		using namespace RPMUI;
		Spaced( F_TAB, X( x ), Y( y ), Pal().accent, text, S( 3 ), true );
		Line( X( x ), Y( y + 28 ), S( 300 ), Pal().faint );
	}

	// a row that can be hovered / clicked: the red bar of the dialog options
	void Row( int x, int y, int w, bool bSelected, int action, int arg, const char *info )
	{
		const int h = 26;
		bool hover = IsHover( x, y, w, h );
		if ( bSelected )
			RPMUI::Highlight( X( x ), Y( y ), S( w ), S( h ), 0.0f );
		else if ( hover )
			RPMUI::Rect( X( x ), Y( y ), S( w ), S( h ), RPMUI::Fade( RPMUI::Pal().bar, 0.45f ) );
		AddHot( x, y, w, h, action, arg, info );
	}

	//---------------------------------------------------------------------
	virtual void PaintBackground()
	{
		using namespace RPMUI;
		int sw, sh;
		GetSize( sw, sh );
		float open = Ramp( m_flOpenTime, 0.22f );
		Rect( 0, 0, sw, sh, Color( 0, 0, 0, (int)( 170 * open ) ) );
		int x, y, w, h;
		WinRect( x, y, w, h );
		int ph = Max( Sc( 8 ), (int)( h * open ) );
		Plate( x, y + ( h - ph ) / 2, w, ph, 247, Sc( 10 ) );
		FrameBox( x, y + ( h - ph ) / 2, w, ph, Sc( 18 ) );
	}

	virtual void Paint()
	{
		m_Hot.RemoveAll();
		input()->GetCursorPos( m_iMouseX, m_iMouseY );
		ScreenToLocal( m_iMouseX, m_iMouseY );
		if ( m_hFont == INVALID_FONT || RPMUI::Ramp( m_flOpenTime, 0.22f ) < 0.6f )
			return;
		UpdateOrigin();
		if ( m_iMode == MODE_CREATE )
			PaintCreate();
		else if ( m_iMode == MODE_LEVELUP )
			PaintLevelUp();
		else
			PaintSheet();
		// the description of what is under the mouse
		int w, h;
		WinSize( w, h );
		const char *info = m_Info.Get();
		for ( int i = 0; i < m_Hot.Count(); i++ )
			if ( m_Hot[i].info.Length() && IsHover( m_Hot[i].x, m_Hot[i].y, m_Hot[i].w, m_Hot[i].h ) )
				info = m_Hot[i].info.Get();
		if ( info && info[0] )
		{
			int bx = 20, by = h - 150, bw = w - 40;
			RPMUI::FrameBox( X( bx ), Y( by ), S( bw ), S( 90 ), S( 10 ) );
			RPMUI::Spaced( RPMUI::F_SMALL, X( bx + 12 ), Y( by + 8 ), RPMUI::Pal().accent, RPMRPG::Name( info ), S( 2 ), true );
			CUtlString d = RPMRPG::Desc( info );
			KeyValues *perk = RPMRPG::Perk( info );
			if ( perk )
				d.Append( CFmtStr( "  %s", PerkReqText( info ).Get() ) );
			TextWrap( m_hFontSmall, bx + 10, by + 30, bw - 20, RPMUI::Pal().text, d );
		}
	}

	//---------------------------------------------------------------------
	// creation
	//---------------------------------------------------------------------
	int StatSum()
	{
		int s = 0;
		for ( int i = 0; i < RPM_STAT_COUNT; i++ )
			s += m_Stats[i];
		return s;
	}

	int TagCount()
	{
		int n = 0;
		for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
			n += m_Tags[i] ? 1 : 0;
		return n;
	}

	int TraitEffect( const char *type, const char *id )
	{
		int sum = 0;
		for ( int i = 0; i < m_Traits.Count(); i++ )
			sum += DefEffect( RPMRPG::Trait( m_Traits[i] ), 1, type, id );
		return sum;
	}

	int PreviewStat( int i )
	{
		return clamp( m_Stats[i] + TraitEffect( "stat", RPMRPG::StatId( i ) ), 1, 10 );
	}

	int PreviewSkill( int i )
	{
		const char *id = RPMRPG::SkillId( i );
		int gov = RPMRPG::StatIndex( RPMRPG::SkillStat( id ) );
		float div = Max( 1.0f, RPMRPG::Rule( "skill_luck_div" ) );
		int v = (int)RPMRPG::Rule( "skill_base" ) + (int)( RPMRPG::Rule( "skill_per_stat" ) * ( gov >= 0 ? PreviewStat( gov ) : 5 ) )
			+ (int)ceilf( PreviewStat( RPMRPG::StatIndex( "lck" ) ) / div );
		if ( m_Tags[i] )
			v += (int)RPMRPG::Rule( "tag_bonus" );
		v += TraitEffect( "skill", id ) + TraitEffect( "skills", NULL );
		return clamp( v, 0, (int)RPMRPG::Rule( "skill_max" ) );
	}

	bool HasTrait( const char *id )
	{
		for ( int i = 0; i < m_Traits.Count(); i++ )
			if ( !Q_stricmp( m_Traits[i], id ) )
				return true;
		return false;
	}

	void PaintCreate()
	{
		int w, h;
		WinSize( w, h );
		RPMUI::Spaced( RPMUI::F_TITLE, X( 20 ), Y( 12 ), RPMUI::Pal().accent, TR( "CHARACTER", "СОЗДАНИЕ ПЕРСОНАЖА" ), S( 4 ), true );
		Text( m_hFont, 20, 56, RPMUI::Pal().accent, TR( "Name:", "Имя:" ) );
		RPMUI::FrameBox( X( 148 ), Y( 48 ), S( 326 ), S( 36 ), S( 8 ) );
		const int total = (int)RPMRPG::Rule( "stat_total" ), mn = (int)RPMRPG::Rule( "stat_min" ), mx = (int)RPMRPG::Rule( "stat_max" );
		const int left = total - StatSum();
		// attributes
		int x = 20, y = 100;
		Header( x, y, TR( "ATTRIBUTES", "ХАРАКТЕРИСТИКИ" ) );
		y += 36;
		for ( int i = 0; i < RPM_STAT_COUNT; i++, y += 30 )
		{
			const char *id = RPMRPG::StatId( i );
			Row( x, y, 300, false, HOT_INFO, i, id );
			Text( m_hFont, x + 6, y + 2, RPMUI::Pal().accent, RPMRPG::Name( id ), 170 );
			int v = PreviewStat( i );
			Text( m_hFont, x + 196, y + 2, RPMUI::Pal().text, CFmtStr( "%2d", v ) );
			SmallButton( x + 236, y + 1, "-", HOT_STAT_MINUS, i, m_Stats[i] > mn );
			SmallButton( x + 266, y + 1, "+", HOT_STAT_PLUS, i, m_Stats[i] < mx && left > 0 );
		}
		Text( m_hFont, x + 6, y + 8, left > 0 ? RPMUI::Pal().good : RPMUI::Pal().grey,
			CFmtStr( TR( "Points left: %d", "Осталось очков: %d" ), left ) );
		// skills
		x = 350;
		y = 100;
		const int tags = (int)RPMRPG::Rule( "tags" );
		Header( x, y, CFmtStr( TR( "SKILLS - TAG %d", "НАВЫКИ - ОТМЕТЬТЕ %d" ), tags ) );
		y += 36;
		for ( int i = 0; i < RPM_SKILL_COUNT; i++, y += 26 )
		{
			const char *id = RPMRPG::SkillId( i );
			Row( x, y, 330, m_Tags[i], HOT_TAG, i, id );
			Marker( x + 6, y, m_Tags[i] );
			Text( m_hFont, x + 24, y + 2, RPMUI::Pal().accent, RPMRPG::Name( id ), 240 );
			Text( m_hFont, x + 290, y + 2, RPMUI::Pal().text, CFmtStr( "%3d", PreviewSkill( i ) ) );
		}
		Text( m_hFont, x + 6, y + 8, TagCount() == tags ? RPMUI::Pal().grey : RPMUI::Pal().good,
			CFmtStr( TR( "Tagged: %d of %d (+%d)", "Отмечено: %d из %d (+%d)" ), TagCount(), tags, (int)RPMRPG::Rule( "tag_bonus" ) ) );
		// traits
		x = 700;
		y = 100;
		const int maxTraits = (int)RPMRPG::Rule( "traits_max" );
		Header( x, y, CFmtStr( TR( "TRAITS - UP TO %d", "ЧЕРТЫ - ДО %d" ), maxTraits ) );
		y += 36;
		int i = 0;
		for ( KeyValues *t = RPMRPG::Traits()->GetFirstTrueSubKey(); t && y < h - 190; t = t->GetNextTrueSubKey(), i++, y += 26 )
		{
			bool on = HasTrait( t->GetName() );
			Row( x, y, w - x - 20, on, HOT_TRAIT, i, t->GetName() );
			Marker( x + 6, y, on );
			Text( m_hFont, x + 24, y + 2, RPMUI::Pal().accent, RPMRPG::Name( t->GetName() ), w - x - 50 );
		}
		if ( !i )
			Text( m_hFontSmall, x + 6, y, RPMUI::Pal().grey, TR( "No traits in this game.", "В этой игре черт нет." ) );
		// buttons
		char name[64];
		m_pName->GetText( name, sizeof( name ) );
		Button( w - 200, h - 48, 180, TR( "Done", "Готово" ), HOT_DONE, 0, name[0] != 0 && left >= 0 );
		Button( w - 390, h - 48, 180, TR( "Reset", "Сброс" ), HOT_RESET );
		Button( 20, h - 48, 160, TR( "Later", "Позже" ), HOT_LATER );
	}

	//---------------------------------------------------------------------
	// level up
	//---------------------------------------------------------------------
	int PointsLeft()
	{
		int n = CharState()->GetInt( "points" );
		for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
			n -= m_Add[i];
		return n;
	}

	int CurSkill( int i ) { return CharState()->FindKey( "skills", true )->GetInt( RPMRPG::SkillId( i ) ); }

	CUtlString PerkReqText( const char *id )
	{
		KeyValues *d = RPMRPG::Perk( id );
		CUtlString s;
		if ( !d )
			return s;
		s.Format( TR( "[Level %d", "[Уровень %d" ), d->GetInt( "level", 1 ) );
		KeyValues *req = d->FindKey( "req" );
		for ( KeyValues *r = req ? req->GetFirstValue() : NULL; r; r = r->GetNextValue() )
			s.Append( CFmtStr( ", %s %d", RPMRPG::Name( r->GetName() ), r->GetInt() ) );
		int ranks = Max( 1, d->GetInt( "ranks", 1 ) );
		if ( ranks > 1 )
			s.Append( CFmtStr( TR( ", rank %d of %d", ", ранг %d из %d" ), CharState()->FindKey( "perks", true )->GetInt( id ) + 1, ranks ) );
		s.Append( "]" );
		return s;
	}

	bool PerkAvailable( const char *id )
	{
		KeyValues *d = RPMRPG::Perk( id );
		KeyValues *st = CharState();
		if ( !d || st->FindKey( "perks", true )->GetInt( id ) >= Max( 1, d->GetInt( "ranks", 1 ) ) )
			return false;
		if ( st->GetInt( "level", 1 ) < d->GetInt( "level", 1 ) )
			return false;
		KeyValues *req = d->FindKey( "req" );
		for ( KeyValues *r = req ? req->GetFirstValue() : NULL; r; r = r->GetNextValue() )
		{
			int si = RPMRPG::SkillIndex( r->GetName() );
			int have = si >= 0 ? CurSkill( si ) + m_Add[si] : st->FindKey( "stats", true )->GetInt( r->GetName(), 5 );
			if ( have < r->GetInt() )
				return false;
		}
		return true;
	}

	void PaintLevelUp()
	{
		int w, h;
		WinSize( w, h );
		KeyValues *st = CharState();
		RPMUI::Spaced( RPMUI::F_TITLE, X( 20 ), Y( 12 ), RPMUI::Pal().accent, CFmtStr( TR( "LEVEL %d", "НОВЫЙ УРОВЕНЬ: %d" ), st->GetInt( "level", 1 ) ), S( 4 ), true );
		const int left = PointsLeft();
		const int skillMax = (int)RPMRPG::Rule( "skill_max" );
		int x = 20, y = 60;
		Header( x, y, CFmtStr( TR( "SKILL POINTS: %d", "ОЧКИ НАВЫКОВ: %d" ), left ) );
		y += 36;
		for ( int i = 0; i < RPM_SKILL_COUNT; i++, y += 28 )
		{
			const char *id = RPMRPG::SkillId( i );
			Row( x, y, 440, false, HOT_INFO, i, id );
			bool tag = ListHas( st->GetString( "tags" ), id );
			Marker( x + 6, y, tag );
			Text( m_hFont, x + 24, y + 2, RPMUI::Pal().accent, RPMRPG::Name( id ), 220 );
			int v = CurSkill( i ) + m_Add[i];
			Text( m_hFont, x + 250, y + 2, m_Add[i] ? RPMUI::Pal().good : RPMUI::Pal().text,
				m_Add[i] ? CFmtStr( "%3d (+%d)", v, m_Add[i] ).Access() : CFmtStr( "%3d", v ).Access() );
			SmallButton( x + 370, y + 1, "-", HOT_SKILL_MINUS, i, m_Add[i] > 0 );
			SmallButton( x + 400, y + 1, "+", HOT_SKILL_PLUS, i, left > 0 && v < skillMax );
		}
		// perks
		x = 490;
		y = 60;
		const int pending = st->GetInt( "perks_pending" );
		Header( x, y, pending ? TR( "CHOOSE A PERK", "ВЫБЕРИТЕ ПЕРК" ) : TR( "PERKS", "ПЕРКИ" ) );
		y += 36;
		if ( !pending )
			Text( m_hFontSmall, x + 6, y, RPMUI::Pal().grey, TR( "A new perk comes with the next even level.", "Новый перк — на следующем чётном уровне." ) );
		else
		{
			m_PerkIds.RemoveAll();
			for ( KeyValues *p = RPMRPG::Perks()->GetFirstTrueSubKey(); p; p = p->GetNextTrueSubKey() )
				m_PerkIds.AddToTail( p->GetName() );
			// available first
			for ( int pass = 0; pass < 2; pass++ )
				for ( int i = 0; i < m_PerkIds.Count() && y < h - 190; i++ )
				{
					bool ok = PerkAvailable( m_PerkIds[i] );
					if ( ok != ( pass == 0 ) )
						continue;
					Row( x, y, w - x - 20, m_iPerkSel == i, ok ? HOT_PERK : HOT_INFO, i, m_PerkIds[i] );
					Marker( x + 6, y, m_iPerkSel == i );
					Text( m_hFont, x + 24, y + 2, ok ? RPMUI::Pal().accent : RPMUI::Pal().grey, RPMRPG::Name( m_PerkIds[i] ), w - x - 50 );
					y += 26;
				}
			if ( !m_PerkIds.Count() )
				Text( m_hFontSmall, x + 6, y, RPMUI::Pal().grey, TR( "No perks in this game.", "В этой игре перков нет." ) );
		}
		Button( w - 200, h - 48, 180, TR( "Done", "Готово" ), HOT_DONE );
		Button( 20, h - 48, 160, TR( "Later", "Позже" ), HOT_LATER );
	}

	//---------------------------------------------------------------------
	// the sheet
	//---------------------------------------------------------------------
	void PaintSheet()
	{
		int w, h;
		WinSize( w, h );
		KeyValues *st = CharState();
		const char *name = st->GetString( "name" );
		RPMUI::Spaced( RPMUI::F_TITLE, X( 20 ), Y( 12 ), RPMUI::Pal().accent, name[0] ? name : TR( "CHARACTER", "ПЕРСОНАЖ" ), S( 4 ), true );
		int level = st->GetInt( "level", 1 ), xp = st->GetInt( "xp" ), prev = st->GetInt( "xp_prev" ), next = st->GetInt( "xp_next" );
		Text( m_hFont, 20, 50, RPMUI::Pal().accent, next ? CFmtStr( TR( "Level %d   XP %d / %d", "Уровень %d   Опыт %d / %d" ), level, xp, next ).Access()
			: CFmtStr( TR( "Level %d (max)   XP %d", "Уровень %d (предел)   Опыт %d" ), level, xp ).Access() );
		int bx = 20, by = 76, bw = 420;
		float frac = next > prev ? clamp( float( xp - prev ) / float( next - prev ), 0.0f, 1.0f ) : 1.0f;
		RPMUI::Bar( X( bx ), Y( by ), S( bw ), S( 8 ), frac, RPMUI::Pal().accent );
		Text( m_hFont, 470, 50, RPMUI::Pal().text, CFmtStr( TR( "Health %d   Carry %.0f kg", "Здоровье %d   Унести %.0f кг" ), st->GetInt( "hp" ), st->GetFloat( "carry" ) ) );
		int x = 20, y = 100;
		Header( x, y, TR( "ATTRIBUTES", "ХАРАКТЕРИСТИКИ" ) );
		y += 36;
		for ( int i = 0; i < RPM_STAT_COUNT; i++, y += 26 )
		{
			const char *id = RPMRPG::StatId( i );
			Row( x, y, 260, false, HOT_INFO, i, id );
			Text( m_hFont, x + 6, y + 2, RPMUI::Pal().accent, RPMRPG::Name( id ), 190 );
			Text( m_hFont, x + 220, y + 2, RPMUI::Pal().text, CFmtStr( "%2d", st->FindKey( "stats", true )->GetInt( id, 5 ) ) );
		}
		x = 300;
		y = 100;
		Header( x, y, TR( "SKILLS", "НАВЫКИ" ) );
		y += 36;
		for ( int i = 0; i < RPM_SKILL_COUNT; i++, y += 26 )
		{
			const char *id = RPMRPG::SkillId( i );
			Row( x, y, 320, false, HOT_INFO, i, id );
			bool tag = ListHas( st->GetString( "tags" ), id );
			Marker( x + 6, y, tag );
			Text( m_hFont, x + 24, y + 2, RPMUI::Pal().accent, RPMRPG::Name( id ), 230 );
			Text( m_hFont, x + 270, y + 2, RPMUI::Pal().text, CFmtStr( "%3d", CurSkill( i ) ) );
		}
		x = 650;
		y = 100;
		Header( x, y, TR( "PERKS AND TRAITS", "ПЕРКИ И ЧЕРТЫ" ) );
		y += 36;
		int n = 0;
		KeyValues *perks = st->FindKey( "perks", true );
		for ( KeyValues *p = perks->GetFirstValue(); p && y < h - 300; p = p->GetNextValue(), y += 26, n++ )
		{
			Row( x, y, w - x - 20, false, HOT_INFO, 0, p->GetName() );
			Text( m_hFont, x + 6, y + 2, RPMUI::Pal().accent, p->GetInt() > 1 ? CFmtStr( "%s (%d)", RPMRPG::Name( p->GetName() ), p->GetInt() ).Access()
				: RPMRPG::Name( p->GetName() ), w - x - 40 );
		}
		CUtlStringList traits;
		V_SplitString( st->GetString( "traits" ), ";", traits );
		for ( int i = 0; i < traits.Count() && y < h - 300; i++ )
		{
			if ( !traits[i][0] )
				continue;
			Row( x, y, w - x - 20, false, HOT_INFO, 0, traits[i] );
			Text( m_hFont, x + 6, y + 2, RPMUI::Pal().dim, CFmtStr( TR( "Trait: %s", "Черта: %s" ), RPMRPG::Name( traits[i] ) ), w - x - 40 );
			y += 26;
			n++;
		}
		if ( !n )
			Text( m_hFontSmall, x + 6, y, RPMUI::Pal().grey, TR( "None yet.", "Пока нет." ) );
		// reputation
		KeyValues *rep = st->FindKey( "rep", true );
		if ( rep->GetFirstValue() )
		{
			y = Max( y + 10, h - 300 );
			Header( x, y, TR( "REPUTATION", "РЕПУТАЦИЯ" ) );
			y += 36;
			for ( KeyValues *r = rep->GetFirstValue(); r && y < h - 170; r = r->GetNextValue(), y += 24 )
				Text( m_hFont, x + 6, y, r->GetInt() < 0 ? RPMUI::Pal().bad : RPMUI::Pal().good, CFmtStr( "%s  %+d", r->GetName(), r->GetInt() ) );
		}
		bool pending = st->GetInt( "points" ) > 0 || st->GetInt( "perks_pending" ) > 0;
		if ( pending )
			Button( w - 420, h - 48, 200, TR( "Level up", "Повысить уровень" ), HOT_TO_LEVELUP );
		Button( w - 200, h - 48, 180, TR( "Close", "Закрыть" ), HOT_LATER );
	}

	//---------------------------------------------------------------------
	virtual void OnMousePressed( MouseCode code )
	{
		if ( code != MOUSE_LEFT )
		{
			BaseClass::OnMousePressed( code );
			return;
		}
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		for ( int i = m_Hot.Count() - 1; i >= 0; i-- )
		{
			const Hot_t &hot = m_Hot[i];
			if ( mx >= hot.x && mx < hot.x + hot.w && my >= hot.y && my < hot.y + hot.h )
			{
				Act( hot.action, hot.arg, hot.info );
				surface()->PlaySound( "ui/buttonclick.wav" );
				return;
			}
		}
	}

	void Act( int action, int arg, const CUtlString &info )
	{
		const int mx = (int)RPMRPG::Rule( "stat_max" ), mn = (int)RPMRPG::Rule( "stat_min" );
		switch ( action )
		{
		case HOT_STAT_MINUS: if ( m_Stats[arg] > mn ) m_Stats[arg]--; break;
		case HOT_STAT_PLUS: if ( m_Stats[arg] < mx && StatSum() < (int)RPMRPG::Rule( "stat_total" ) ) m_Stats[arg]++; break;
		case HOT_TAG:
			if ( m_Tags[arg] )
				m_Tags[arg] = false;
			else if ( TagCount() < (int)RPMRPG::Rule( "tags" ) )
				m_Tags[arg] = true;
			break;
		case HOT_TRAIT:
		{
			if ( HasTrait( info ) )
			{
				for ( int i = 0; i < m_Traits.Count(); i++ )
					if ( !Q_stricmp( m_Traits[i], info ) )
						m_Traits.Remove( i-- );
			}
			else if ( m_Traits.Count() < (int)RPMRPG::Rule( "traits_max" ) )
				m_Traits.AddToTail( info );
			break;
		}
		case HOT_SKILL_MINUS: if ( m_Add[arg] > 0 ) m_Add[arg]--; break;
		case HOT_SKILL_PLUS: if ( PointsLeft() > 0 ) m_Add[arg]++; break;
		case HOT_PERK: m_iPerkSel = ( m_iPerkSel == arg ) ? -1 : arg; break;
		case HOT_RESET:
			for ( int i = 0; i < RPM_STAT_COUNT; i++ )
				m_Stats[i] = (int)RPMRPG::Rule( "stat_start" );
			for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
				m_Tags[i] = false;
			m_Traits.RemoveAll();
			break;
		case HOT_TO_LEVELUP: Open( MODE_LEVELUP ); break;
		case HOT_LATER: Close(); break;
		case HOT_INFO: m_Info = info; break;
		case HOT_DONE: Done(); break;
		}
	}

	static CUtlString Quote( const char *s )
	{
		CUtlString out;
		for ( ; *s; s++ )
			if ( *s != '"' && *s != ';' && *s != '\n' && *s != '\r' )
			{
				char c[2] = { *s, 0 };
				out.Append( c );
			}
		return out;
	}

	void Done()
	{
		if ( m_iMode == MODE_CREATE )
		{
			char name[64];
			m_pName->GetText( name, sizeof( name ) );
			CUtlString stats, tags, traits;
			for ( int i = 0; i < RPM_STAT_COUNT; i++ )
				stats.Append( CFmtStr( "%s%s=%d", i ? ";" : "", RPMRPG::StatId( i ), m_Stats[i] ) );
			for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
				if ( m_Tags[i] )
					tags.Append( CFmtStr( "%s%s", tags.IsEmpty() ? "" : ";", RPMRPG::SkillId( i ) ) );
			for ( int i = 0; i < m_Traits.Count(); i++ )
				traits.Append( CFmtStr( "%s%s", i ? ";" : "", m_Traits[i].Get() ) );
			engine->ClientCmd_Unrestricted( CFmtStr( "rpm_char_create_apply \"%s\" \"%s\" \"%s\" \"%s\"\n", Quote( name ).Get(), stats.Get(), tags.Get(), traits.Get() ) );
		}
		else if ( m_iMode == MODE_LEVELUP )
		{
			CUtlString add;
			for ( int i = 0; i < RPM_SKILL_COUNT; i++ )
				if ( m_Add[i] > 0 )
					add.Append( CFmtStr( "%s%s=%d", add.IsEmpty() ? "" : ";", RPMRPG::SkillId( i ), m_Add[i] ) );
			const char *perk = ( m_iPerkSel >= 0 && m_iPerkSel < m_PerkIds.Count() ) ? m_PerkIds[m_iPerkSel].Get() : "";
			engine->ClientCmd_Unrestricted( CFmtStr( "rpm_char_level_apply \"%s\" \"%s\"\n", add.Get(), perk ) );
		}
		Close();
	}

	int m_iMode;
	bool m_bPaused = false;		// this window paused the game (cl_rpm_char_pause)
	TextEntry *m_pName;
	HFont m_hFont, m_hFontBig, m_hFontSmall;
	CUtlVector<Hot_t> m_Hot;
	int m_iMouseX, m_iMouseY;
	int m_iOX, m_iOY;		// the window on the screen
	float m_flOpenTime;
	int m_iHover;
	CUtlString m_Info;
	int m_Stats[RPM_STAT_COUNT];
	bool m_Tags[RPM_SKILL_COUNT];
	CUtlVector<CUtlString> m_Traits;
	int m_Add[RPM_SKILL_COUNT];
	CUtlVector<CUtlString> m_PerkIds;
	int m_iPerkSel;
};

static CRPMCharPanel *s_pCharPanel = NULL;
static int s_iPendingMode = -1;

static void OpenCharPanel( int mode )
{
	if ( !s_pCharPanel )
		s_pCharPanel = new CRPMCharPanel( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
	s_pCharPanel->Open( mode );
}

class CRPMCharClientSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMCharClientSystem() : CAutoGameSystemPerFrame( "CRPMCharClientSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		static bool s_bHooked = false;
		if ( !s_bHooked && usermessages->LookupUserMessage( "RPMChar" ) != -1 )
		{
			usermessages->HookMessage( "RPMChar", __MsgFunc_RPMChar );
			s_bHooked = true;
		}
	}

	virtual void LevelShutdownPreEntity()
	{
		if ( s_pCharPanel )
			s_pCharPanel->Close();
		s_iPendingMode = -1;
	}

	virtual void Update( float frametime )
	{
		// a window the server asked for while a dialog was open: after the dialog
		// (not over a window the player has open; a level up with nothing left to spend is dropped)
		bool bBusy = s_pCharPanel && s_pCharPanel->IsVisible();
		if ( s_iPendingMode >= 0 && !HL2RPM_IsDialogOpen() && !bBusy && engine->IsInGame() )
		{
			int mode = s_iPendingMode;
			s_iPendingMode = -1;
			if ( mode != MODE_LEVELUP || CharState()->GetInt( "points" ) > 0 || CharState()->GetInt( "perks_pending" ) > 0 )
				OpenCharPanel( mode );
		}
	}
};

static CRPMCharClientSystem g_RPMCharClientSystem;

extern bool RPMMenu_Toggle( int tab );
extern bool RPMMenu_IsOpen();

CON_COMMAND( rpm_char_open, "HL2RPM: the character window: create | levelup | sheet" )
{
	const char *m = args.ArgC() > 1 ? args[1] : "sheet";
	if ( !Q_stricmp( m, "close" ) )
	{
		if ( s_pCharPanel )
			s_pCharPanel->Close();
		return;
	}
	int mode = !Q_stricmp( m, "create" ) ? MODE_CREATE : !Q_stricmp( m, "levelup" ) ? MODE_LEVELUP : MODE_SHEET;
	// the sheet is the character tab of the menu (hl2rpm_menu.cpp)
	if ( mode == MODE_SHEET && !RPMMenu_IsOpen() && RPMMenu_Toggle( 0 ) )
		return;
	bool bCreating = s_pCharPanel && s_pCharPanel->IsVisible() && s_pCharPanel->Mode() == MODE_CREATE;
	if ( mode != MODE_CREATE && ( HL2RPM_IsDialogOpen() || bCreating ) )
	{
		s_iPendingMode = mode;
		return;
	}
	OpenCharPanel( mode );
}

CON_COMMAND( rpm_character, "HL2RPM: the character sheet (attributes, skills, perks; level up)" )
{
	if ( RPMMenu_Toggle( 0 ) )
		return;
	if ( s_pCharPanel && s_pCharPanel->IsVisible() && s_pCharPanel->Mode() == MODE_SHEET )
	{
		s_pCharPanel->Close();
		return;
	}
	OpenCharPanel( MODE_SHEET );
}

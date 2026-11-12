//========= HL2RPM ============================================================//
//
// Reading window of notes / books of the item database (rpm_note_open <id>,
// sent by the server when a note is used in the inventory).
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_items.h"
#include "ienginevgui.h"
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/RichText.h>
#include <vgui_controls/Button.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

class CRPMNotePanel : public Frame
{
	DECLARE_CLASS_SIMPLE( CRPMNotePanel, Frame );
public:
	CRPMNotePanel() : Frame( NULL, "RPMNotePanel" )
	{
		SetParent( enginevgui->GetPanel( PANEL_CLIENTDLL ) );
		SetScheme( "ClientScheme" );
		SetTitleBarVisible( false );
		SetSizeable( false );
		SetMoveable( false );
		SetProportional( false );
		m_pTitle = new Label( this, "Title", "" );
		m_pText = new RichText( this, "Text" );
		m_pClose = new Button( this, "Close", "X", this, "close" );
	}

	void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		int w = Max( 520, sw * 4 / 10 ), h = Max( 420, sh * 6 / 10 );
		SetBounds( ( sw - w ) / 2, ( sh - h ) / 2, w, h );
		SetBgColor( Color( 0, 0, 0, 225 ) );
		SetPaintBackgroundEnabled( true );
		// Trebuchet24 of ClientScheme has only Basic Latin glyphs - Russian titles were empty
		HFont big = pScheme->GetFont( "CloseCaption_Bold", false );
		HFont txt = pScheme->GetFont( "Default", false );
		m_pTitle->SetFont( big );
		m_pTitle->SetPaintBackgroundEnabled( false );
		m_pTitle->SetFgColor( Color( 255, 196, 37, 255 ) );
		m_pTitle->SetBounds( 18, 12, w - 80, 34 );
		m_pText->SetFont( txt );
		m_pText->SetFgColor( Color( 255, 214, 120, 255 ) );
		m_pText->SetBgColor( Color( 0, 0, 0, 0 ) );
		m_pText->SetBorder( NULL );
		m_pText->SetBounds( 18, 54, w - 36, h - 72 );
		m_pClose->SetBounds( w - 44, 12, 28, 28 );
		m_pClose->SetFgColor( Color( 255, 196, 37, 255 ) );
		m_pClose->SetDefaultColor( Color( 255, 196, 37, 255 ), Color( 0, 0, 0, 0 ) );
		m_pClose->SetArmedColor( Color( 255, 220, 120, 255 ), Color( 180, 50, 40, 200 ) );
	}

	void PaintBackground()
	{
		int w, h;
		GetSize( w, h );
		surface()->DrawSetColor( 0, 0, 0, 225 );
		surface()->DrawFilledRect( 0, 0, w, h );
		surface()->DrawSetColor( 255, 196, 37, 200 );
		surface()->DrawOutlinedRect( 0, 0, w, h );
		surface()->DrawOutlinedRect( 4, 4, w - 4, h - 4 );
		surface()->DrawFilledRect( 18, 48, w - 18, 49 );
	}

	void Open( const char *id )
	{
		const RPMItemDef_t *d = RPMItems::Find( id );
		m_pTitle->SetText( RPMItems::Name( id ) );
		const char *text = d ? RPMItems::Text( d, "text" ) : "";
		m_pText->SetText( "" );
		m_pText->SetVerticalScrollbar( Q_strlen( text ) > 900 );		// the stock scrollbar is a white bar - only for long texts
		m_pText->InsertString( text[0] ? text : ( d ? RPMItems::Text( d, "desc" ) : "" ) );
		m_pText->GotoTextStart();
		InvalidateLayout( true, true );
		SetVisible( true );
		MakePopup();
		SetMouseInputEnabled( true );
		SetKeyBoardInputEnabled( true );
		RequestFocus();
		surface()->PlaySound( "ui/buttonclick.wav" );
	}

	void OnCommand( const char *cmd )
	{
		if ( !Q_stricmp( cmd, "close" ) )
		{
			Close();
			return;
		}
		BaseClass::OnCommand( cmd );
	}

	void OnKeyCodePressed( KeyCode code )
	{
		if ( code == KEY_ESCAPE || code == KEY_E || code == KEY_ENTER || code == KEY_TAB )
		{
			Close();
			return;
		}
		BaseClass::OnKeyCodePressed( code );
	}

	void Close()
	{
		SetVisible( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
	}

private:
	Label *m_pTitle;
	RichText *m_pText;
	Button *m_pClose;
};

static CRPMNotePanel *g_pNotePanel = NULL;

CON_COMMAND( rpm_note_open, "HL2RPM: read a note of the item database (internal)" )
{
	if ( args.ArgC() < 2 )
		return;
	if ( !g_pNotePanel )
		g_pNotePanel = new CRPMNotePanel();
	g_pNotePanel->Open( args[1] );
}

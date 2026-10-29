//========= HL2RPM ============//
//
// RPG-интерфейс: основа полноэкранных экранов (см. rpg_screen.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_screen.h"
#include "iclientmode.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

int CRPGScreen::s_nOpen = 0;

// открытые экраны по порядку: закрыли верхний (документ поверх браслета) - фокус нижнему
static CUtlVector<CRPGScreen *> s_OpenStack;

bool RPGScreen_AnyOpen()
{
	return CRPGScreen::s_nOpen > 0;
}

extern bool HL2RPM_IsDialogOpen();	// vgui_dialogpanel.cpp

ConVar cl_rpg_ui_debug( "cl_rpg_ui_debug", "0", FCVAR_CLIENTDLL, "RPG UI: print button clicks of full-screen RPG screens" );

CRPGScreen::CRPGScreen( const char *pszName ) : BaseClass( NULL, pszName )
{
	if ( g_pClientMode && g_pClientMode->GetViewport() )
		SetParent( g_pClientMode->GetViewport() );
	SetProportional( false );
	SetVisible( false );
	SetPaintBackgroundEnabled( false );
	m_iHover = m_iPressed = -1;
	m_iMouseX = m_iMouseY = 0;
	m_bOpen = false;
	surface()->GetScreenSize( m_iScreenW, m_iScreenH );
	ivgui()->AddTickSignal( GetVPanel(), 0 );
}

void CRPGScreen::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	LoadFonts( pScheme );
	surface()->GetScreenSize( m_iScreenW, m_iScreenH );
	SetBounds( 0, 0, m_iScreenW, m_iScreenH );
}

void CRPGScreen::Open()
{
	surface()->GetScreenSize( m_iScreenW, m_iScreenH );
	SetBounds( 0, 0, m_iScreenW, m_iScreenH );
	const bool bWasOpen = m_bOpen;
	m_bOpen = true;
	SetVisible( true );
	MakePopup( false, false );
	SetZPos( 950 );
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );
	RequestFocus();
	MoveToFront();
	s_OpenStack.FindAndRemove( this );
	s_OpenStack.AddToTail( this );
	if ( !bWasOpen )
	{
		++s_nOpen;
		engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
		PlayUISound( "ui/buttonclick.wav" );
		OnOpened();
	}
}

void CRPGScreen::Close()
{
	if ( !m_bOpen )
		return;
	m_bOpen = false;
	SetVisible( false );
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( false );
	--s_nOpen;
	s_OpenStack.FindAndRemove( this );
	// под экраном может остаться открытый диалог - Esc тогда всё ещё его
	if ( s_nOpen <= 0 && !HL2RPM_IsDialogOpen() )
		engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
	PlayUISound( "ui/buttonclickrelease.wav" );
	OnClosed();
	if ( s_OpenStack.Count() )
	{
		CRPGScreen *pTop = s_OpenStack.Tail();
		pTop->MoveToFront();
		pTop->RequestFocus();
	}
}

void CRPGScreen::DrawDimmer( int alpha )
{
	DrawRect( 0, 0, m_iScreenW, m_iScreenH, Color( 4, 6, 8, alpha ) );
}

bool CRPGScreen::Button( int id, int x0, int y0, int x1, int y1, bool bEnabled )
{
	Btn_t b = { id, x0, y0, x1, y1, bEnabled };
	m_Buttons.AddToTail( b );
	return bEnabled && m_iMouseX >= x0 && m_iMouseX < x1 && m_iMouseY >= y0 && m_iMouseY < y1;
}

bool CRPGScreen::TextButton( int id, int x0, int y0, int x1, int y1, const wchar_t *pText, bool bEnabled, bool bPrimary )
{
	const bool bHover = Button( id, x0, y0, x1, y1, bEnabled );
	Color fill = bPrimary ? Color( 110, 190, 255, bHover ? 90 : 46 ) : Color( 110, 190, 255, bHover ? 46 : 14 );
	if ( !bEnabled )
		fill = Color( 60, 66, 72, 40 );
	DrawRect( x0, y0, x1, y1, fill );
	DrawFrame( x0, y0, x1, y1, bEnabled ? ( bHover ? clrAccent : clrLine ) : Color( 80, 86, 92, 120 ) );
	HFont f = GetFont( FONT_HEADER );
	const int tw = TextWidth( f, pText );
	DrawText( f, ( x0 + x1 - tw ) / 2, ( y0 + y1 - FontTall( f ) ) / 2, bEnabled ? ( bHover ? Color( 255, 255, 255, 255 ) : clrText ) : clrTextFaint, pText );
	return bHover;
}

void CRPGScreen::Paint()
{
	if ( !m_bOpen )
		return;
	m_Buttons.RemoveAll();
	PaintScreen();
	// наведение - по кнопкам этого кадра
	int hover = -1;
	for ( int i = m_Buttons.Count() - 1; i >= 0; --i )
	{
		const Btn_t &b = m_Buttons[i];
		if ( b.bEnabled && m_iMouseX >= b.x0 && m_iMouseX < b.x1 && m_iMouseY >= b.y0 && m_iMouseY < b.y1 )
		{
			hover = b.id;
			break;
		}
	}
	if ( hover != m_iHover && hover >= 0 )
		PlayUISound( "ui/buttonrollover.wav" );
	m_iHover = hover;
}

void CRPGScreen::OnCursorMoved( int x, int y )
{
	m_iMouseX = x;
	m_iMouseY = y;
}

void CRPGScreen::OnMousePressed( MouseCode code )
{
	m_iPressed = m_iHover;
	if ( cl_rpg_ui_debug.GetBool() )
		Msg( "[rpg ui] %s: press %d at %d,%d -> button %d\n", GetName(), (int)code, m_iMouseX, m_iMouseY, m_iHover );
	if ( m_iHover >= 0 && ( code == MOUSE_LEFT || code == MOUSE_RIGHT ) )
	{
		PlayUISound( "ui/buttonclick.wav" );
		OnButton( m_iHover, code == MOUSE_RIGHT );
		// кнопка забирает фокус у текстового поля (клавиши экрана); после Close - уже нельзя
		if ( m_bOpen && IsKeyBoardInputEnabled() )
			RequestFocus();
	}
}

void CRPGScreen::OnMouseReleased( MouseCode code )
{
	m_iPressed = -1;
}

void CRPGScreen::OnMouseWheeled( int delta )
{
	OnWheel( delta );
}

void CRPGScreen::OnKeyCodePressed( KeyCode code )
{
	if ( code == KEY_ESCAPE && CloseOnEscape() )
	{
		Close();
		return;
	}
	OnKey( code );
}

void CRPGScreen::OnTick()
{
	if ( !m_bOpen )
		return;
	int w, h;
	surface()->GetScreenSize( w, h );
	if ( w != m_iScreenW || h != m_iScreenH )
	{
		m_iScreenW = w;
		m_iScreenH = h;
		SetBounds( 0, 0, w, h );
	}
	Think();
}

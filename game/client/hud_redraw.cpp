//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
//
// hud_redraw.cpp
//
#include "cbase.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "itextmessage.h"
#include "vgui_basepanel.h"
#include "hud_crosshair.h"
#include <vgui/ISurface.h>

#if defined( REPLAY_ENABLED )
#include "replay/ienginereplay.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//For progress bar orientations
const int CHud::HUDPB_HORIZONTAL = 0;
const int CHud::HUDPB_VERTICAL = 1;
const int CHud::HUDPB_HORIZONTAL_INV = 2;

// Called when a ConVar changes value
static void FovChanged_Callback( IConVar *pConVar, const char *pOldString, float flOldValue )
{
	ConVarRef var( pConVar );
	if ( engine->IsInGame() )
	{
		engine->ServerCmd( VarArgs( "fov %f\n", var.GetFloat() ) );
	}
}

static ConVar fov_watcher( "_fov", "0", 0, "Automates fov command to server.", FovChanged_Callback );

//-----------------------------------------------------------------------------
// Purpose: Think
//-----------------------------------------------------------------------------
// HL2RPM: stock elements that are gone. A suit has one HUD (hl2rpm_suithud.cpp); no weapon is selected, one is
// equipped from the inventory (hl2rpm_weapons.cpp); what is picked up shows as a notification of the left, with
// or without a suit (the icons of HudHistoryResource were there even with no suit on); the captions are cut out
// (hud_closecaption.cpp); the rest is multiplayer.
static bool RPMHud_IsCut( const CUtlVector<CHudElement *> &list, CHudElement *pElement )
{
	static const char *s_pszCut[] =
	{
		"CHudHealth", "CHudBattery", "CHudAmmo", "CHudSecondaryAmmo", "CHudSuitPower", "CHudFlashlight", "CHudPosture",
		"CHudWeaponSelection", "CHudHistoryResource", "CHudCloseCaption", "CHudHintKeyDisplay",
		"CHudDeathNotice", "CHudVoiceSelfStatus", "CHudVoiceStatus",
	};
	static CUtlVector<CHudElement *> s_Cut;
	static int s_nKnown = -1;
	if ( s_nKnown != list.Count() )
	{
		s_nKnown = list.Count();
		s_Cut.RemoveAll();
		for ( int i = 0; i < list.Count(); i++ )
			for ( int j = 0; j < ARRAYSIZE( s_pszCut ); j++ )
				if ( !Q_stricmp( list[i]->GetName(), s_pszCut[j] ) )
					s_Cut.AddToTail( list[i] );
	}
	return s_Cut.Find( pElement ) != s_Cut.InvalidIndex();
}

void CHud::Think(void)
{
#if defined( REPLAY_ENABLED )
	// Don't draw this
	extern IEngineClientReplay *g_EngineClientReplay;
	const bool bPlayingReplay = g_pEngineClientReplay && g_pEngineClientReplay->IsPlayingReplayDemo();
#endif

	// Determine the visibility of all hud elements
	for ( int i = 0; i < m_HudList.Size(); i++ )
	{
		// Visible?
		bool visible = !RPMHud_IsCut( m_HudList, m_HudList[i] ) && m_HudList[i]->ShouldDraw();

#if defined( REPLAY_ENABLED )
		visible = visible && !bPlayingReplay;
#endif

		m_HudList[i]->SetActive( visible );

		// If it's a vgui panel, hide/show as appropriate
		vgui::Panel *pPanel = dynamic_cast<vgui::Panel*>(m_HudList[i]);
		if ( pPanel && pPanel->IsVisible() != visible )
		{
			pPanel->SetVisible( visible );
		}
		else if ( !pPanel )
		{
			// All HUD elements should now derive from vgui!!!
			Assert( 0 );
		}

		if ( visible )
		{
			m_HudList[i]->ProcessInput();
		}
	}

	// Let the active weapon at the keybits
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->HandleInput();
	}

	if ( ( m_flScreenShotTime > 0 ) && ( m_flScreenShotTime < gpGlobals->curtime ) )
	{
		if ( !IsX360() )
		{
			engine->ClientCmd( "screenshot" );
		}

		m_flScreenShotTime = -1;
	}
}

//-----------------------------------------------------------------------------
// Purpose:  The percentage passed in is expected and clamped to 0.0f to 1.0f
// Input  : x - 
//			y - 
//			width - 
//			height - 
//			percentage - 
//			clr - 
//			type - 
//-----------------------------------------------------------------------------
void CHud::DrawProgressBar( int x, int y, int width, int height, float percentage, Color& clr, unsigned char type )
{
	//Clamp our percentage
	percentage = MIN( 1.0f, percentage );
	percentage = MAX( 0.0f, percentage );

	Color lowColor = clr;
	lowColor[ 0 ] /= 2;
	lowColor[ 1 ] /= 2;
	lowColor[ 2 ] /= 2;

	//Draw a vertical progress bar
	if ( type == HUDPB_VERTICAL )
	{
		int	barOfs = height * percentage;

		surface()->DrawSetColor( lowColor );
		surface()->DrawFilledRect( x, y, x + width, y + barOfs );

		surface()->DrawSetColor( clr );
		surface()->DrawFilledRect( x, y + barOfs, x + width, y + height );
	}
	else if ( type == HUDPB_HORIZONTAL )
	{
		int	barOfs = width * percentage;

		surface()->DrawSetColor( lowColor );
		surface()->DrawFilledRect( x, y, x + barOfs, y +  height );

		surface()->DrawSetColor( clr );
		surface()->DrawFilledRect( x + barOfs, y, x + width, y + height );
	}
	else if ( type == HUDPB_HORIZONTAL_INV )
	{
		int	barOfs = width * percentage;

		surface()->DrawSetColor( clr );
		surface()->DrawFilledRect( x, y, x + barOfs, y + height );

		surface()->DrawSetColor( lowColor );
		surface()->DrawFilledRect( x + barOfs, y, x + width, y +  height );
	}
}

//-----------------------------------------------------------------------------
// Purpose:  The percentage passed in is expected and clamped to 0.0f to 1.0f
// Input  : x - 
//			y - 
//			*icon - 
//			percentage - 
//			clr - 
//			type - 
//-----------------------------------------------------------------------------
void CHud::DrawIconProgressBar( int x, int y, CHudTexture *icon, CHudTexture *icon2, float percentage, Color& clr, int type )
{
	if ( icon == NULL )
		return;

	//Clamp our percentage
	percentage = MIN( 1.0f, percentage );
	percentage = MAX( 0.0f, percentage );

	int	height = icon->Height();
	int	width  = icon->Width();

	//Draw a vertical progress bar
	if ( type == HUDPB_VERTICAL )
	{
		int	barOfs = height * percentage;

		icon2->DrawSelfCropped( 
			x, y,  // Pos
			0, 0, width, barOfs, // Cropped subrect
			clr );

		icon->DrawSelfCropped( 
			x, y + barOfs, 
			0, barOfs, width, height - barOfs, // Cropped subrect
			clr );
	}
	else if ( type == HUDPB_HORIZONTAL )
	{
		int	barOfs = width * percentage;

		icon2->DrawSelfCropped( 
			x, y,  // Pos
			0, 0, barOfs, height, // Cropped subrect
			clr );

		icon->DrawSelfCropped( 
			x + barOfs, y, 
			barOfs, 0, width - barOfs, height, // Cropped subrect
			clr );
	}
}



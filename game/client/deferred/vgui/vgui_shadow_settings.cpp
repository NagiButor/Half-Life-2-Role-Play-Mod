
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "ienginevgui.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Label.h>

#include "tier0/memdbgon.h"

using namespace vgui;

extern ConVar r_csm_quality;

// ---------------------------------------------------------------
// Shadow settings panel – opened via "deferred_shadow_settings"
// console command or from the game menu.
// ---------------------------------------------------------------
class CDeferredShadowSettings : public Frame
{
	DECLARE_CLASS_SIMPLE( CDeferredShadowSettings, Frame );

public:
	CDeferredShadowSettings( VPANEL parent );
	~CDeferredShadowSettings() {}

	virtual void Activate();
	virtual void OnCommand( const char *command );

private:
	ComboBox *m_pCSMQuality;
	int m_iOrigCSMQuality;
};

static CDeferredShadowSettings *g_pShadowSettingsPanel = NULL;

// ---------------------------------------------------------------
CDeferredShadowSettings::CDeferredShadowSettings( VPANEL parent )
	: BaseClass( NULL, "DeferredShadowSettings" )
{
	SetParent( parent );
	SetTitle( "Shadow Settings", true );
	SetSizeable( false );
	SetMoveable( true );
	SetDeleteSelfOnClose( false );
	SetVisible( false );
	SetCloseButtonVisible( true );

	SetSize( 340, 160 );
	MoveToCenterOfScreen();

	// --- Sun Shadow Quality ---
	Label *pCSMLabel = new Label( this, "CSMQualityLabel", "Sun Shadow Quality:" );
	pCSMLabel->SetBounds( 20, 36, 150, 24 );

	m_pCSMQuality = new ComboBox( this, "CSMQualityCombo", 5, false );
	m_pCSMQuality->AddItem( "Very Low",  new KeyValues( "q", "val", "0" ) );
	m_pCSMQuality->AddItem( "Low",       new KeyValues( "q", "val", "1" ) );
	m_pCSMQuality->AddItem( "Medium",    new KeyValues( "q", "val", "2" ) );
	m_pCSMQuality->AddItem( "High",      new KeyValues( "q", "val", "3" ) );
	m_pCSMQuality->AddItem( "Ultra",     new KeyValues( "q", "val", "4" ) );
	m_pCSMQuality->SetBounds( 180, 36, 132, 24 );

	// --- Buttons ---
	Button *pOK = new Button( this, "OKButton", "OK", this, "OK" );
	pOK->SetBounds( 144, 100, 80, 26 );

	Button *pCancel = new Button( this, "CancelButton", "Cancel", this, "Cancel" );
	pCancel->SetBounds( 234, 100, 80, 26 );

	m_iOrigCSMQuality = 4;
}

// ---------------------------------------------------------------
void CDeferredShadowSettings::Activate()
{
	BaseClass::Activate();
	MakePopup();
	MoveToFront();
	RequestFocus();

	m_iOrigCSMQuality = r_csm_quality.GetInt();
	m_pCSMQuality->ActivateItemByRow( clamp( m_iOrigCSMQuality, 0, 4 ) );
}

// ---------------------------------------------------------------
void CDeferredShadowSettings::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "OK" ) )
	{
		int idx = m_pCSMQuality->GetActiveItem();
		if ( idx >= 0 )
		{
			KeyValues *pKV = m_pCSMQuality->GetItemUserData( idx );
			if ( pKV )
				r_csm_quality.SetValue( pKV->GetInt( "val", 4 ) );
		}
		Close();
	}
	else if ( !Q_stricmp( command, "Cancel" ) )
	{
		r_csm_quality.SetValue( m_iOrigCSMQuality );
		Close();
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

// ---------------------------------------------------------------
CON_COMMAND( deferred_shadow_settings, "Open the deferred shadow settings panel" )
{
	if ( !g_pShadowSettingsPanel )
	{
		VPANEL parent = enginevgui->GetPanel( PANEL_GAMEUIDLL );
		if ( !parent )
			parent = enginevgui->GetPanel( PANEL_CLIENTDLL );
		if ( !parent )
		{
			Warning( "deferred_shadow_settings: no suitable VGUI parent found.\n" );
			return;
		}
		g_pShadowSettingsPanel = new CDeferredShadowSettings( parent );
	}
	g_pShadowSettingsPanel->Activate();
}


#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "ienginevgui.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/MessageBox.h>

#include "tier0/memdbgon.h"

using namespace vgui;

extern ConVar r_csm_quality;
extern ConVar r_deferred_shadow_quality_pointspot;

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
	ComboBox *m_pPointSpotQuality;
	int m_iOrigCSMQuality;
	int m_iOrigPointSpotQuality;
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

	SetSize( 340, 200 );
	MoveToCenterOfScreen();

	// --- Sun Shadow Quality ---
	Label *pCSMLabel = new Label( this, "CSMQualityLabel", "Sun Shadow Quality:" );
	pCSMLabel->SetBounds( 20, 36, 150, 24 );

	m_pCSMQuality = new ComboBox( this, "CSMQualityCombo", 5, false );
	m_pCSMQuality->AddItem( "Very Low",  new KeyValues( "q", "val", "0" ) );
	m_pCSMQuality->AddItem( "Low",       new KeyValues( "q", "val", "1" ) );
	m_pCSMQuality->AddItem( "Medium",    new KeyValues( "q", "val", "2" ) );
	m_pCSMQuality->AddItem( "High",      new KeyValues( "q", "val", "3" ) );
	m_pCSMQuality->AddItem( "Very High", new KeyValues( "q", "val", "4" ) );
	m_pCSMQuality->AddItem( "Ultra",     new KeyValues( "q", "val", "5" ) );
	m_pCSMQuality->SetBounds( 180, 36, 132, 24 );

	Label *pPointSpotLabel = new Label( this, "PointSpotShadowQualityLabel", "Point/Spot Shadow Quality:" );
	pPointSpotLabel->SetBounds( 20, 68, 160, 24 );

	m_pPointSpotQuality = new ComboBox( this, "PointSpotShadowQualityCombo", 6, false );
	m_pPointSpotQuality->AddItem( "Very Low",  new KeyValues( "q", "val", "0" ) );
	m_pPointSpotQuality->AddItem( "Low",       new KeyValues( "q", "val", "1" ) );
	m_pPointSpotQuality->AddItem( "Medium",    new KeyValues( "q", "val", "2" ) );
	m_pPointSpotQuality->AddItem( "High",      new KeyValues( "q", "val", "3" ) );
	m_pPointSpotQuality->AddItem( "Very High", new KeyValues( "q", "val", "4" ) );
	m_pPointSpotQuality->AddItem( "Ultra",     new KeyValues( "q", "val", "5" ) );
	m_pPointSpotQuality->SetBounds( 180, 68, 132, 24 );

	// --- Buttons ---
	Button *pOK = new Button( this, "OKButton", "OK", this, "OK" );
	pOK->SetBounds( 144, 134, 80, 26 );

	Button *pCancel = new Button( this, "CancelButton", "Cancel", this, "Cancel" );
	pCancel->SetBounds( 234, 134, 80, 26 );

	m_iOrigCSMQuality = 5;
	m_iOrigPointSpotQuality = 5;
}

// ---------------------------------------------------------------
void CDeferredShadowSettings::Activate()
{
	BaseClass::Activate();
	MakePopup();
	MoveToFront();
	RequestFocus();

	m_iOrigCSMQuality = r_csm_quality.GetInt();
	m_iOrigPointSpotQuality = r_deferred_shadow_quality_pointspot.GetInt();
	m_pCSMQuality->ActivateItemByRow( clamp( m_iOrigCSMQuality, 0, 5 ) );
	m_pPointSpotQuality->ActivateItemByRow( clamp( m_iOrigPointSpotQuality, 0, 5 ) );
}

// ---------------------------------------------------------------
void CDeferredShadowSettings::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "OK" ) )
	{
		int newCSMQuality = m_iOrigCSMQuality;
		int newPointSpotQuality = m_iOrigPointSpotQuality;

		int idx = m_pCSMQuality->GetActiveItem();
		if ( idx >= 0 )
		{
			KeyValues *pKV = m_pCSMQuality->GetItemUserData( idx );
			if ( pKV )
				newCSMQuality = pKV->GetInt( "val", 5 );
		}

		idx = m_pPointSpotQuality->GetActiveItem();
		if ( idx >= 0 )
		{
			KeyValues *pKV = m_pPointSpotQuality->GetItemUserData( idx );
			if ( pKV )
				newPointSpotQuality = pKV->GetInt( "val", 5 );
		}

		r_csm_quality.SetValue( newCSMQuality );
		r_deferred_shadow_quality_pointspot.SetValue( newPointSpotQuality );

		const bool bSettingsChanged = newCSMQuality != m_iOrigCSMQuality || newPointSpotQuality != m_iOrigPointSpotQuality;
		if ( bSettingsChanged )
		{
			vgui::MessageBox *pRestartWarning = new vgui::MessageBox(
				"Restart Required",
				"For full shadow quality changes to take effect, restart the game.",
				this );
			pRestartWarning->AddActionSignalTarget( this );
			pRestartWarning->SetCommand( "ShadowRestartWarningAck" );
			pRestartWarning->DoModal();
			return;
		}

		Close();
	}
	else if ( !Q_stricmp( command, "ShadowRestartWarningAck" ) )
	{
		Close();
	}
	else if ( !Q_stricmp( command, "Cancel" ) )
	{
		r_csm_quality.SetValue( m_iOrigCSMQuality );
		r_deferred_shadow_quality_pointspot.SetValue( m_iOrigPointSpotQuality );
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

#include "cbase.h"
#include "deferred/vgui/vgui_editor_timecycle.h"

#include "timecycle/env_timecycle.h"

#include "vgui_controls/ComboBox.h"
#include "vgui_controls/Label.h"

#include "tier0/memdbgon.h"

using namespace vgui;

namespace
{
	const char *s_pszTimeScales[] =
	{
		"1",
		"5",
		"10",
		"100",
		"500",
		"1000",
		"2000",
	};
}

CVGUILightEditor_Timecycle::CVGUILightEditor_Timecycle( Panel *pParent )
	: BaseClass( pParent, "LightEditorTimecycle" )
{
	m_bRefreshing = false;
	m_iLastDisplayedHour = -1;
	m_iLastDisplayedMinute = -1;

	SetDeleteSelfOnClose( false );
	SetSizeable( false );
	SetMoveable( true );
	SetMenuButtonVisible( false );
	SetMinimizeButtonVisible( false );
	SetMaximizeButtonVisible( false );
	SetMinimizeToSysTrayButtonVisible( false );
	SetCloseButtonVisible( true );
	SetTitle( "Timecycle and weather editor", false );

	m_pLabelCurrentTimeTitle = new Label( this, "label_current_time_title", "Current time" );
	m_pLabelCurrentTimeValue = new Label( this, "label_current_time_value", "12:00" );
	m_pLabelTimeOfDay = new Label( this, "label_time_of_day", "Time of day" );
	m_pLabelTimeScale = new Label( this, "label_time_scale", "Time speed" );
	m_pLabelWeatherPreset = new Label( this, "label_weather_preset", "Weather preset" );

	m_pComboTimeOfDay = new ComboBox( this, "combo_time_of_day", 8, false );
	m_pComboTimeScale = new ComboBox( this, "combo_time_scale", 7, false );
	m_pComboWeatherPreset = new ComboBox( this, "combo_weather_preset", 4, false );

	PopulateCombos();
	RefreshFromTimecycle();
	RefreshCurrentTimeDisplay();

	MakeReadyForUse();
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	SetSize( 320, 228 );
	SetVisible( false );
}

CVGUILightEditor_Timecycle::~CVGUILightEditor_Timecycle()
{
}

void CVGUILightEditor_Timecycle::OpenEditor()
{
	RefreshFromTimecycle();
	RefreshCurrentTimeDisplay();
	SetVisible( true );
	MoveToFront();
	Activate();
}

void CVGUILightEditor_Timecycle::OnThink()
{
	BaseClass::OnThink();

	if ( IsVisible() )
		RefreshCurrentTimeDisplay();
}

void CVGUILightEditor_Timecycle::PerformLayout()
{
	const int iFrameWide = 320;
	const int iFrameTall = 228;
	const int iMarginX = 16;
	const int iLabelWide = iFrameWide - iMarginX * 2;
	const int iComboWide = iLabelWide;
	const int iComboTall = 24;
	const int iLabelTall = 18;
	const int iContentTop = 62;
	const int iBlockGap = 52;

	if ( GetWide() != iFrameWide || GetTall() != iFrameTall )
		SetSize( iFrameWide, iFrameTall );

	BaseClass::PerformLayout();

	int x, y;
	GetPos( x, y );
	if ( x == 0 && y == 0 )
	{
		int sw, sh;
		engine->GetScreenSize( sw, sh );
		SetPos( 240, Max( 32, sh / 2 - iFrameTall / 2 ) );
	}

	m_pLabelCurrentTimeTitle->SetBounds( iMarginX, 34, 110, iLabelTall );
	m_pLabelCurrentTimeValue->SetBounds( iFrameWide - iMarginX - 74, 34, 74, iLabelTall );
	m_pLabelCurrentTimeValue->SetContentAlignment( Label::a_east );

	m_pLabelTimeOfDay->SetBounds( iMarginX, iContentTop, iLabelWide, iLabelTall );
	m_pComboTimeOfDay->SetBounds( iMarginX, iContentTop + 18, iComboWide, iComboTall );

	m_pLabelTimeScale->SetBounds( iMarginX, iContentTop + iBlockGap, iLabelWide, iLabelTall );
	m_pComboTimeScale->SetBounds( iMarginX, iContentTop + iBlockGap + 18, iComboWide, iComboTall );

	m_pLabelWeatherPreset->SetBounds( iMarginX, iContentTop + iBlockGap * 2, iLabelWide, iLabelTall );
	m_pComboWeatherPreset->SetBounds( iMarginX, iContentTop + iBlockGap * 2 + 18, iComboWide, iComboTall );
}

void CVGUILightEditor_Timecycle::OnTextChanged( Panel *panel )
{
	if ( m_bRefreshing )
		return;

	if ( panel == m_pComboTimeOfDay )
	{
		ApplyTimeOfDay();
	}
	else if ( panel == m_pComboTimeScale )
	{
		ApplyTimeScale();
	}

	RequestFocus();
}

void CVGUILightEditor_Timecycle::PopulateCombos()
{
	for ( int i = 0; i <= 24; i++ )
	{
		char szValue[8];
		Q_snprintf( szValue, sizeof( szValue ), "%d", i );
		m_pComboTimeOfDay->AddItem( szValue, new KeyValues( "item", "value", szValue ) );
	}

	for ( int i = 0; i < ARRAYSIZE( s_pszTimeScales ); i++ )
	{
		m_pComboTimeScale->AddItem( s_pszTimeScales[i], new KeyValues( "item", "value", s_pszTimeScales[i] ) );
	}

	m_pComboWeatherPreset->AddItem( "Clear", new KeyValues( "item", "value", "Clear" ) );
	m_pComboWeatherPreset->SilentActivateItemByRow( 0 );
}

void CVGUILightEditor_Timecycle::RefreshFromTimecycle()
{
	m_bRefreshing = true;

	CEnvTimecycle *pTimecycle = GetTimecycle();

	int iTimeOfDay = 12;
	if ( pTimecycle != NULL )
		iTimeOfDay = clamp( (int)( pTimecycle->GetTimeOfDayHours() + 0.5f ), 0, 24 );

	char szTime[8];
	Q_snprintf( szTime, sizeof( szTime ), "%d", iTimeOfDay );
	SelectItemByValue( m_pComboTimeOfDay, szTime );

	const char *pszBestSpeed = "10";
	if ( pTimecycle != NULL )
	{
		float flBestDelta = FLT_MAX;
		for ( int i = 0; i < ARRAYSIZE( s_pszTimeScales ); i++ )
		{
			const float flCurrentSpeed = (float)atof( s_pszTimeScales[i] );
			const float flDelta = fabsf( flCurrentSpeed - pTimecycle->GetTimeScale() );
			if ( flDelta < flBestDelta )
			{
				flBestDelta = flDelta;
				pszBestSpeed = s_pszTimeScales[i];
			}
		}
	}

	SelectItemByValue( m_pComboTimeScale, pszBestSpeed );
	m_pComboWeatherPreset->SilentActivateItemByRow( 0 );

	m_bRefreshing = false;
}

void CVGUILightEditor_Timecycle::RefreshCurrentTimeDisplay()
{
	CEnvTimecycle *pTimecycle = GetTimecycle();
	const float flHours = pTimecycle ? pTimecycle->GetTimeOfDayHours() : 12.0f;

	int iHour = (int)floorf( flHours );
	float flMinutes = ( flHours - iHour ) * 60.0f;
	int iMinute = (int)floorf( flMinutes + 0.5f );

	if ( iMinute >= 60 )
	{
		iMinute = 0;
		iHour++;
	}

	while ( iHour < 0 )
		iHour += 24;
	while ( iHour >= 24 )
		iHour -= 24;

	if ( iHour == m_iLastDisplayedHour && iMinute == m_iLastDisplayedMinute )
		return;

	m_iLastDisplayedHour = iHour;
	m_iLastDisplayedMinute = iMinute;

	char szTime[16];
	Q_snprintf( szTime, sizeof( szTime ), "%02d:%02d", iHour, iMinute );
	m_pLabelCurrentTimeValue->SetText( szTime );

	char szHour[8];
	Q_snprintf( szHour, sizeof( szHour ), "%d", iHour );

	m_bRefreshing = true;
	SelectItemByValue( m_pComboTimeOfDay, szHour );
	m_bRefreshing = false;
}

void CVGUILightEditor_Timecycle::ApplyTimeOfDay()
{
	KeyValues *pData = m_pComboTimeOfDay->GetActiveItemUserData();
	if ( pData == NULL )
		return;

	engine->ClientCmd( VarArgs( "sv_timecycle_set_time %s", pData->GetString( "value", "12" ) ) );
}

void CVGUILightEditor_Timecycle::ApplyTimeScale()
{
	KeyValues *pData = m_pComboTimeScale->GetActiveItemUserData();
	if ( pData == NULL )
		return;

	engine->ClientCmd( VarArgs( "sv_timecycle_set_speed %s", pData->GetString( "value", "10" ) ) );
}

void CVGUILightEditor_Timecycle::SelectItemByValue( ComboBox *pCombo, const char *pszValue )
{
	if ( pCombo == NULL || pszValue == NULL )
		return;

	for ( int i = 0; i < pCombo->GetItemCount(); i++ )
	{
		const int iItemID = pCombo->GetItemIDFromRow( i );
		KeyValues *pData = pCombo->GetItemUserData( iItemID );
		if ( pData != NULL && !Q_stricmp( pData->GetString( "value", "" ), pszValue ) )
		{
			pCombo->SilentActivateItem( iItemID );
			return;
		}
	}

	pCombo->SilentActivateItemByRow( 0 );
}

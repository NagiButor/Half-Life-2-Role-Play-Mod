#include "cbase.h"
#include "deferred/vgui/vgui_editor_timecycle.h"

#include "timecycle/env_timecycle.h"
#include "weather/env_weather.h"
#include "weather/c_weather_system.h"

#include "vgui/ILocalize.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/Button.h"

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

	// weather transition time in real seconds
	struct WeatherTransition_t
	{
		const char *pszLabel;
		const char *pszSeconds;
	};

	const WeatherTransition_t s_WeatherTransitions[] =
	{
		{ "#HL2RPM_TCE_Instant", "0" },
		{ "#HL2RPM_TCE_10s", "10" },
		{ "#HL2RPM_TCE_30s", "30" },
		{ "#HL2RPM_TCE_60s", "60" },
		{ "#HL2RPM_TCE_180s", "180" },
	};

	const int FRAME_WIDE = 340;
	const int FRAME_TALL = 404;
}

CVGUILightEditor_Timecycle::CVGUILightEditor_Timecycle( Panel *pParent )
	: BaseClass( pParent, "LightEditorTimecycle" )
{
	m_bRefreshing = false;
	m_iLastDisplayedHour = -1;
	m_iLastDisplayedMinute = -1;
	m_iLastWeatherTarget = -1;
	m_iLastWeatherPercent = -1;

	SetDeleteSelfOnClose( false );
	SetSizeable( false );
	SetMoveable( true );
	SetMenuButtonVisible( false );
	SetMinimizeButtonVisible( false );
	SetMaximizeButtonVisible( false );
	SetMinimizeToSysTrayButtonVisible( false );
	SetCloseButtonVisible( true );
	SetTitle( "#HL2RPM_TCE_Title", false );

	m_pLabelCurrentTimeTitle = new Label( this, "label_current_time_title", "#HL2RPM_TCE_CurrentTime" );
	m_pLabelCurrentTimeValue = new Label( this, "label_current_time_value", "12:00" );
	m_pLabelTimeOfDay = new Label( this, "label_time_of_day", "#HL2RPM_TCE_TimeOfDay" );
	m_pLabelTimeScale = new Label( this, "label_time_scale", "#HL2RPM_TCE_TimeSpeed" );
	m_pLabelWeatherState = new Label( this, "label_weather_state", "" );
	m_pLabelWeatherPreset = new Label( this, "label_weather_preset", "#HL2RPM_TCE_WeatherPreset" );
	m_pLabelWeatherTransition = new Label( this, "label_weather_transition", "#HL2RPM_TCE_Transition" );

	m_pComboTimeOfDay = new ComboBox( this, "combo_time_of_day", 8, false );
	m_pComboTimeScale = new ComboBox( this, "combo_time_scale", 7, false );
	m_pComboWeatherPreset = new ComboBox( this, "combo_weather_preset", WEATHER_PRESET_COUNT, false );
	m_pComboWeatherTransition = new ComboBox( this, "combo_weather_transition", ARRAYSIZE( s_WeatherTransitions ), false );

	m_pCheckAutoWeather = new CheckButton( this, "check_auto_weather", "#HL2RPM_TCE_AutoWeather" );
	m_pButtonNextWeather = new Button( this, "button_next_weather", "#HL2RPM_TCE_NextWeather", this, "NextWeather" );
	m_pButtonLightning = new Button( this, "button_lightning", "#HL2RPM_TCE_Lightning", this, "Lightning" );

	PopulateCombos();
	RefreshFromTimecycle();
	RefreshCurrentTimeDisplay();
	RefreshWeatherDisplay();

	MakeReadyForUse();
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	SetSize( FRAME_WIDE, FRAME_TALL );
	SetVisible( false );
}

CVGUILightEditor_Timecycle::~CVGUILightEditor_Timecycle()
{
}

void CVGUILightEditor_Timecycle::OpenEditor()
{
	RefreshFromTimecycle();
	RefreshCurrentTimeDisplay();
	m_iLastWeatherTarget = -1;
	RefreshWeatherDisplay();
	SetVisible( true );
	MoveToFront();
	Activate();
}

void CVGUILightEditor_Timecycle::OnThink()
{
	BaseClass::OnThink();

	if ( IsVisible() )
	{
		RefreshCurrentTimeDisplay();
		RefreshWeatherDisplay();
	}
}

void CVGUILightEditor_Timecycle::PerformLayout()
{
	const int iMarginX = 16;
	const int iLabelWide = FRAME_WIDE - iMarginX * 2;
	const int iComboTall = 24;
	const int iLabelTall = 18;
	const int iBlockGap = 50;

	if ( GetWide() != FRAME_WIDE || GetTall() != FRAME_TALL )
		SetSize( FRAME_WIDE, FRAME_TALL );

	BaseClass::PerformLayout();

	int x, y;
	GetPos( x, y );
	if ( x == 0 && y == 0 )
	{
		int sw, sh;
		engine->GetScreenSize( sw, sh );
		SetPos( 240, Max( 32, sh / 2 - FRAME_TALL / 2 ) );
	}

	m_pLabelCurrentTimeTitle->SetBounds( iMarginX, 34, 160, iLabelTall );
	m_pLabelCurrentTimeValue->SetBounds( FRAME_WIDE - iMarginX - 74, 34, 74, iLabelTall );
	m_pLabelCurrentTimeValue->SetContentAlignment( Label::a_east );

	int yy = 60;
	m_pLabelTimeOfDay->SetBounds( iMarginX, yy, iLabelWide, iLabelTall );
	m_pComboTimeOfDay->SetBounds( iMarginX, yy + 18, iLabelWide, iComboTall );
	yy += iBlockGap;

	m_pLabelTimeScale->SetBounds( iMarginX, yy, iLabelWide, iLabelTall );
	m_pComboTimeScale->SetBounds( iMarginX, yy + 18, iLabelWide, iComboTall );
	yy += iBlockGap + 14;

	m_pLabelWeatherState->SetBounds( iMarginX, yy, iLabelWide, iLabelTall );
	yy += 26;

	m_pLabelWeatherPreset->SetBounds( iMarginX, yy, iLabelWide, iLabelTall );
	m_pComboWeatherPreset->SetBounds( iMarginX, yy + 18, iLabelWide, iComboTall );
	yy += iBlockGap;

	m_pLabelWeatherTransition->SetBounds( iMarginX, yy, iLabelWide, iLabelTall );
	m_pComboWeatherTransition->SetBounds( iMarginX, yy + 18, iLabelWide, iComboTall );
	yy += iBlockGap + 4;

	m_pCheckAutoWeather->SetBounds( iMarginX - 4, yy, iLabelWide, 24 );
	yy += 34;

	const int iButtonWide = ( iLabelWide - 8 ) / 2;
	m_pButtonNextWeather->SetBounds( iMarginX, yy, iButtonWide, 26 );
	m_pButtonLightning->SetBounds( iMarginX + iButtonWide + 8, yy, iButtonWide, 26 );
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
	else if ( panel == m_pComboWeatherPreset )
	{
		ApplyWeather();
	}

	RequestFocus();
}

void CVGUILightEditor_Timecycle::OnCheckButtonChecked( Panel *panel )
{
	if ( m_bRefreshing || panel != m_pCheckAutoWeather )
		return;

	engine->ClientCmd_Unrestricted( VarArgs( "sv_weather_auto %d\n", m_pCheckAutoWeather->IsSelected() ? 1 : 0 ) );
}

void CVGUILightEditor_Timecycle::OnCommand( const char *pszCommand )
{
	if ( !Q_stricmp( pszCommand, "NextWeather" ) )
	{
		engine->ClientCmd_Unrestricted( "sv_weather_next\n" );
		return;
	}
	if ( !Q_stricmp( pszCommand, "Lightning" ) )
	{
		engine->ClientCmd_Unrestricted( "cl_weather_lightning\n" );
		return;
	}

	BaseClass::OnCommand( pszCommand );
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

	for ( int i = 0; i < WEATHER_PRESET_COUNT; i++ )
	{
		const WeatherPresetInfo_t &info = GetWeatherPresetInfo( i );
		m_pComboWeatherPreset->AddItem( info.pszDisplayName, new KeyValues( "item", "value", info.pszName ) );
	}
	m_pComboWeatherPreset->SilentActivateItemByRow( 0 );

	for ( int i = 0; i < ARRAYSIZE( s_WeatherTransitions ); i++ )
	{
		m_pComboWeatherTransition->AddItem( s_WeatherTransitions[i].pszLabel,
			new KeyValues( "item", "value", s_WeatherTransitions[i].pszSeconds ) );
	}
	m_pComboWeatherTransition->SilentActivateItemByRow( 2 );	// 30 s
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

	// weather: current target preset and the automatic cycle
	CEnvWeather *pWeather = GetWeatherEntity();
	const int iTarget = pWeather ? pWeather->GetTargetPreset() : GetWeatherSystem()->GetTargetPreset();
	SelectItemByValue( m_pComboWeatherPreset, GetWeatherPresetInfo( iTarget ).pszName );
	m_pCheckAutoWeather->SetSelected( pWeather ? pWeather->IsAutomatic() : false );
	m_pCheckAutoWeather->SetEnabled( pWeather != NULL );

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

void CVGUILightEditor_Timecycle::RefreshWeatherDisplay()
{
	C_WeatherSystem *pSys = GetWeatherSystem();
	const int iTarget = pSys->GetTargetPreset();
	const float flBlend = pSys->GetTransitionProgress();
	const int iPercent = ( flBlend < 0.999f ) ? (int)( flBlend * 100.0f ) : 100;

	if ( iTarget == m_iLastWeatherTarget && iPercent == m_iLastWeatherPercent )
		return;

	// the preset changed by itself (automatic weather or a map input): follow it in the combo
	if ( iTarget != m_iLastWeatherTarget && m_iLastWeatherTarget >= 0 )
	{
		m_bRefreshing = true;
		SelectItemByValue( m_pComboWeatherPreset, GetWeatherPresetInfo( iTarget ).pszName );
		CEnvWeather *pWeather = GetWeatherEntity();
		if ( pWeather )
			m_pCheckAutoWeather->SetSelected( pWeather->IsAutomatic() );
		m_bRefreshing = false;
	}

	m_iLastWeatherTarget = iTarget;
	m_iLastWeatherPercent = iPercent;

	const wchar_t *pwszTitle = g_pVGuiLocalize->Find( "#HL2RPM_TCE_WeatherNow" );
	const wchar_t *pwszName = g_pVGuiLocalize->Find( GetWeatherPresetInfo( iTarget ).pszDisplayName );
	wchar_t wszName[64];
	if ( !pwszName )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( GetWeatherPresetInfo( iTarget ).pszName, wszName, sizeof( wszName ) );
		pwszName = wszName;
	}

	wchar_t wszText[256];
	if ( !pSys->IsActive() )
		V_snwprintf( wszText, ARRAYSIZE( wszText ), L"%ls -", pwszTitle ? pwszTitle : L"Weather:" );
	else if ( iPercent < 100 )
		V_snwprintf( wszText, ARRAYSIZE( wszText ), L"%ls -> %ls (%d%%)", pwszTitle ? pwszTitle : L"Weather:", pwszName, iPercent );
	else
		V_snwprintf( wszText, ARRAYSIZE( wszText ), L"%ls %ls", pwszTitle ? pwszTitle : L"Weather:", pwszName );
	m_pLabelWeatherState->SetText( wszText );
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

void CVGUILightEditor_Timecycle::ApplyWeather()
{
	KeyValues *pData = m_pComboWeatherPreset->GetActiveItemUserData();
	if ( pData == NULL )
		return;

	KeyValues *pTransition = m_pComboWeatherTransition->GetActiveItemUserData();
	const char *pszSeconds = pTransition ? pTransition->GetString( "value", "30" ) : "30";
	engine->ClientCmd_Unrestricted( VarArgs( "sv_weather %s %s\n", pData->GetString( "value", "fair" ), pszSeconds ) );
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

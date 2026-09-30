//========= HL2RPM ============================================================//
//
// Purpose: "Graphics" page inside the standard Options dialog (GameUI).
//
// GameUI.dll is closed source, so the page is injected at runtime: a hidden
// ticking panel watches for the "OptionsDialog" panel and calls its (virtual)
// PropertyDialog::AddPage with our page. The vgui_controls headers of Mapbase
// are ABI identical to the ones GameUI was built with (only an access
// specifier and a const differ in Panel.h), and all further communication
// with the dialog (ResetData / ApplyChanges / ApplyButtonEnable) goes through
// VGUI messages.
//
// The page drives the renderer settings added by the deferred renderer and the
// weather system. Engine settings (resolution, AA, texture detail...) stay on
// the Video tab.
//
//=============================================================================//

#include "cbase.h"
#include "ienginevgui.h"
#include "materialsystem/imaterialsystem.h"

#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertyDialog.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/CheckButton.h>

#include "tier0/memdbgon.h"

using namespace vgui;

// ---------------------------------------------------------------------------
// Settings and presets
// ---------------------------------------------------------------------------
enum GfxSetting_e
{
	GFX_SUN_SHADOWS = 0,	// r_csm_quality 0..5
	GFX_LAMP_SHADOWS,		// r_deferred_shadow_quality_pointspot 0..5
	GFX_CLOUDS,				// r_weather_clouds 0..3
	GFX_SHAFTS,				// r_weather_sunshafts 0..3
	GFX_RAIN,				// r_weather_rain_density (0.5 / 1 / 1.5)
	GFX_WETNESS,			// r_weather_wetness 0/1
	GFX_SSAO,				// r_deferred_ssao 0/1

	GFX_SETTING_COUNT
};

static const char *s_pszGfxConVars[GFX_SETTING_COUNT] =
{
	"r_csm_quality",
	"r_deferred_shadow_quality_pointspot",
	"r_weather_clouds",
	"r_weather_sunshafts",
	"r_weather_rain_density",
	"r_weather_wetness",
	"r_deferred_ssao",
};

static const char *s_pszGfxLabels[GFX_SETTING_COUNT] =
{
	"#HL2RPM_Gfx_SunShadows",
	"#HL2RPM_Gfx_LampShadows",
	"#HL2RPM_Gfx_Clouds",
	"#HL2RPM_Gfx_SunShafts",
	"#HL2RPM_Gfx_Rain",
	"#HL2RPM_Gfx_Wetness",
	"#HL2RPM_Gfx_SSAO",
};

struct GfxChoice_t
{
	const char *pszLabel;
	float flValue;
};

static const GfxChoice_t s_ShadowChoices[] =
{
	{ "#HL2RPM_Gfx_Q_VeryLow", 0 }, { "#HL2RPM_Gfx_Q_Low", 1 }, { "#HL2RPM_Gfx_Q_Medium", 2 },
	{ "#HL2RPM_Gfx_Q_High", 3 }, { "#HL2RPM_Gfx_Q_VeryHigh", 4 }, { "#HL2RPM_Gfx_Q_Ultra", 5 },
};
static const GfxChoice_t s_EffectChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_Q_Low", 1 }, { "#HL2RPM_Gfx_Q_Medium", 2 }, { "#HL2RPM_Gfx_Q_High", 3 },
};
static const GfxChoice_t s_RainChoices[] =
{
	{ "#HL2RPM_Gfx_Rain_Low", 0.5f }, { "#HL2RPM_Gfx_Rain_Medium", 1.0f }, { "#HL2RPM_Gfx_Rain_High", 1.5f },
};
static const GfxChoice_t s_OnOffChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_On", 1 },
};

struct GfxChoiceList_t
{
	const GfxChoice_t *pChoices;
	int nChoices;
};

static const GfxChoiceList_t s_GfxChoices[GFX_SETTING_COUNT] =
{
	{ s_ShadowChoices, ARRAYSIZE( s_ShadowChoices ) },
	{ s_ShadowChoices, ARRAYSIZE( s_ShadowChoices ) },
	{ s_EffectChoices, ARRAYSIZE( s_EffectChoices ) },
	{ s_EffectChoices, ARRAYSIZE( s_EffectChoices ) },
	{ s_RainChoices, ARRAYSIZE( s_RainChoices ) },
	{ s_OnOffChoices, ARRAYSIZE( s_OnOffChoices ) },
	{ s_OnOffChoices, ARRAYSIZE( s_OnOffChoices ) },
};

// Overall presets. The costs that matter most on the GPU are the sun shadow
// cascades (re-rendering the scene up to 4 times), the lamp shadow map size,
// and the cloud raymarch resolution.
static const char *s_pszPresetLabels[] =
{
	"#HL2RPM_Gfx_Q_VeryLow",
	"#HL2RPM_Gfx_Q_Low",
	"#HL2RPM_Gfx_Q_Medium",
	"#HL2RPM_Gfx_Q_High",
	"#HL2RPM_Gfx_Q_Ultra",
};
#define GFX_PRESET_COUNT ARRAYSIZE( s_pszPresetLabels )
#define GFX_PRESET_CUSTOM GFX_PRESET_COUNT

static const float s_flPresets[GFX_PRESET_COUNT][GFX_SETTING_COUNT] =
{
	//	sun	lamp clouds shafts rain wet	ssao
	{	0,	1,	1,	0,	0.5f,	0,	0	},	// very low
	{	1,	2,	1,	1,	0.5f,	1,	0	},	// low
	{	2,	3,	2,	1,	1.0f,	1,	1	},	// medium
	{	3,	4,	2,	2,	1.0f,	1,	1	},	// high
	{	5,	5,	3,	3,	1.5f,	1,	1	},	// ultra
};

static int FindChoice( int iSetting, float flValue )
{
	const GfxChoiceList_t &list = s_GfxChoices[iSetting];
	int iBest = 0;
	float flBest = FLT_MAX;
	for ( int i = 0; i < list.nChoices; i++ )
	{
		const float d = fabsf( list.pChoices[i].flValue - flValue );
		if ( d < flBest )
		{
			flBest = d;
			iBest = i;
		}
	}
	return iBest;
}

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------
class CHL2RPMGraphicsPage : public PropertyPage
{
	DECLARE_CLASS_SIMPLE( CHL2RPMGraphicsPage, PropertyPage );

public:
	CHL2RPMGraphicsPage( Panel *pParent );

	virtual void OnResetData();
	virtual void OnApplyChanges();
	virtual void PerformLayout();

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

private:
	void LoadFromConVars();
	void SelectPreset( int iPreset );
	int DetectPreset() const;
	float GetChoiceValue( int iSetting ) const;
	void SetDirty();

	ComboBox *m_pPreset;
	Label *m_pPresetLabel;
	ComboBox *m_pCombos[GFX_SETTING_COUNT];
	Label *m_pLabels[GFX_SETTING_COUNT];
	Label *m_pGPULabel;
	Label *m_pNoteLabel;
	Label *m_pHintLabel;

	bool m_bLoading;
	int m_iLoadedLampShadows;
};

CHL2RPMGraphicsPage::CHL2RPMGraphicsPage( Panel *pParent ) : BaseClass( pParent, "HL2RPMGraphicsPage" )
{
	m_bLoading = true;
	m_iLoadedLampShadows = -1;

	m_pPresetLabel = new Label( this, "PresetLabel", "#HL2RPM_Gfx_Preset" );
	m_pPreset = new ComboBox( this, "PresetCombo", GFX_PRESET_COUNT + 1, false );
	for ( int i = 0; i < GFX_PRESET_COUNT; i++ )
		m_pPreset->AddItem( s_pszPresetLabels[i], new KeyValues( "preset", "index", i ) );
	m_pPreset->AddItem( "#HL2RPM_Gfx_Q_Custom", new KeyValues( "preset", "index", GFX_PRESET_CUSTOM ) );

	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
	{
		m_pLabels[s] = new Label( this, VarArgs( "Label%d", s ), s_pszGfxLabels[s] );
		const GfxChoiceList_t &list = s_GfxChoices[s];
		m_pCombos[s] = new ComboBox( this, VarArgs( "Combo%d", s ), list.nChoices, false );
		for ( int c = 0; c < list.nChoices; c++ )
			m_pCombos[s]->AddItem( list.pChoices[c].pszLabel, new KeyValues( "choice", "index", c ) );
	}

	// which graphics card renders the game (laptops with two GPUs often start on the slow one)
	char szGPU[256] = "?";
	const int iAdapter = materials->GetCurrentAdapter();
	if ( iAdapter >= 0 && iAdapter < materials->GetDisplayAdapterCount() )
	{
		MaterialAdapterInfo_t info;
		materials->GetDisplayAdapterInfo( iAdapter, info );
		Q_strncpy( szGPU, info.m_pDriverName, sizeof( szGPU ) );
	}
	wchar_t wszGPU[256];
	g_pVGuiLocalize->ConvertANSIToUnicode( szGPU, wszGPU, sizeof( wszGPU ) );
	wchar_t wszText[512];
	const wchar_t *pwszPrefix = g_pVGuiLocalize->Find( "#HL2RPM_Gfx_GPU" );
	V_snwprintf( wszText, ARRAYSIZE( wszText ), L"%ls %ls", pwszPrefix ? pwszPrefix : L"GPU:", wszGPU );
	m_pGPULabel = new Label( this, "GPULabel", wszText );

	m_pNoteLabel = new Label( this, "NoteLabel", "#HL2RPM_Gfx_RestartNote" );
	m_pNoteLabel->SetVisible( false );
	m_pHintLabel = new Label( this, "HintLabel", "#HL2RPM_Gfx_Hint" );

	LoadFromConVars();
	m_bLoading = false;
}

void CHL2RPMGraphicsPage::PerformLayout()
{
	BaseClass::PerformLayout();

	const int x0 = 20;
	const int iLabelWide = 230;
	const int iComboWide = Min( 190, GetWide() - x0 * 2 - iLabelWide );
	const int x1 = x0 + iLabelWide;
	const int iRow = 30;
	int y = 18;

	m_pPresetLabel->SetBounds( x0, y, iLabelWide, 24 );
	m_pPreset->SetBounds( x1, y, iComboWide, 24 );
	y += iRow + 14;

	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
	{
		m_pLabels[s]->SetBounds( x0 + 12, y, iLabelWide - 12, 24 );
		m_pCombos[s]->SetBounds( x1, y, iComboWide, 24 );
		y += iRow;
	}

	y += 8;
	m_pNoteLabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
	y += 24;
	m_pGPULabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
	y += 22;
	m_pHintLabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
}

void CHL2RPMGraphicsPage::LoadFromConVars()
{
	m_bLoading = true;
	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
	{
		ConVarRef var( s_pszGfxConVars[s] );
		const float flValue = var.IsValid() ? var.GetFloat() : 0.0f;
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, flValue ) );
	}
	m_iLoadedLampShadows = (int)GetChoiceValue( GFX_LAMP_SHADOWS );
	m_pPreset->SilentActivateItemByRow( DetectPreset() );
	m_pNoteLabel->SetVisible( false );
	m_bLoading = false;
}

float CHL2RPMGraphicsPage::GetChoiceValue( int iSetting ) const
{
	const GfxChoiceList_t &list = s_GfxChoices[iSetting];
	const int iRow = clamp( m_pCombos[iSetting]->GetActiveItem(), 0, list.nChoices - 1 );
	KeyValues *pData = m_pCombos[iSetting]->GetItemUserData( m_pCombos[iSetting]->GetActiveItem() );
	const int iChoice = pData ? clamp( pData->GetInt( "index", iRow ), 0, list.nChoices - 1 ) : iRow;
	return list.pChoices[iChoice].flValue;
}

int CHL2RPMGraphicsPage::DetectPreset() const
{
	for ( int p = 0; p < GFX_PRESET_COUNT; p++ )
	{
		bool bMatch = true;
		for ( int s = 0; s < GFX_SETTING_COUNT && bMatch; s++ )
			bMatch = fabsf( GetChoiceValue( s ) - s_flPresets[p][s] ) < 0.01f;
		if ( bMatch )
			return p;
	}
	return GFX_PRESET_CUSTOM;
}

void CHL2RPMGraphicsPage::SelectPreset( int iPreset )
{
	if ( iPreset < 0 || iPreset >= GFX_PRESET_COUNT )
		return;

	m_bLoading = true;
	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, s_flPresets[iPreset][s] ) );
	m_bLoading = false;
}

void CHL2RPMGraphicsPage::SetDirty()
{
	m_pNoteLabel->SetVisible( (int)GetChoiceValue( GFX_LAMP_SHADOWS ) != m_iLoadedLampShadows );
	PostActionSignal( new KeyValues( "ApplyButtonEnable" ) );
}

void CHL2RPMGraphicsPage::OnTextChanged( Panel *panel )
{
	if ( m_bLoading )
		return;

	if ( panel == m_pPreset )
	{
		KeyValues *pData = m_pPreset->GetActiveItemUserData();
		const int iPreset = pData ? pData->GetInt( "index", GFX_PRESET_CUSTOM ) : GFX_PRESET_CUSTOM;
		if ( iPreset != GFX_PRESET_CUSTOM )
			SelectPreset( iPreset );
		SetDirty();
		return;
	}

	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
	{
		if ( panel == m_pCombos[s] )
		{
			// a single setting was changed by hand: that's a custom preset now
			m_pPreset->SilentActivateItemByRow( DetectPreset() );
			SetDirty();
			return;
		}
	}
}

void CHL2RPMGraphicsPage::OnResetData()
{
	LoadFromConVars();
}

void CHL2RPMGraphicsPage::OnApplyChanges()
{
	for ( int s = 0; s < GFX_SETTING_COUNT; s++ )
	{
		ConVarRef var( s_pszGfxConVars[s] );
		if ( !var.IsValid() )
			continue;

		const float flValue = GetChoiceValue( s );
		if ( fabsf( var.GetFloat() - flValue ) > 0.001f )
			var.SetValue( flValue );
	}
	m_iLoadedLampShadows = (int)GetChoiceValue( GFX_LAMP_SHADOWS );
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
}

// ---------------------------------------------------------------------------
// Injector: waits for the Options dialog and adds the page to it
// ---------------------------------------------------------------------------
static ConVar cl_options_graphics_select( "cl_options_graphics_select", "0", 0, "Dev: open the Options dialog on the Graphics page" );
static ConVar cl_options_graphics_page( "cl_options_graphics_page", "1", FCVAR_ARCHIVE, "Add the HL2RPM Graphics page to the Options dialog" );

class CHL2RPMOptionsInjector : public Panel
{
	DECLARE_CLASS_SIMPLE( CHL2RPMOptionsInjector, Panel );

public:
	CHL2RPMOptionsInjector( VPANEL parent ) : BaseClass( NULL, "HL2RPMOptionsInjector" )
	{
		SetParent( parent );
		SetVisible( false );
		SetSize( 0, 0 );
		m_hLastDialog = 0;
		ivgui()->AddTickSignal( GetVPanel(), 250 );
	}

	virtual void OnTick()
	{
		BaseClass::OnTick();

		if ( !cl_options_graphics_page.GetBool() )
			return;

		VPANEL hDialog = FindPanel( enginevgui->GetPanel( PANEL_GAMEUIDLL ), "OptionsDialog", 0 );
		if ( !hDialog || hDialog == m_hLastDialog )
			return;

		// already has our page (the dialog handle can be reused after a re-create)
		if ( FindPanel( hDialog, "HL2RPMGraphicsPage", 0 ) )
		{
			m_hLastDialog = hDialog;
			return;
		}

		Panel *pPanel = ipanel()->GetPanel( hDialog, "GameUI" );
		if ( !pPanel )
			return;

		m_hLastDialog = hDialog;
		PropertyDialog *pDialog = static_cast< PropertyDialog * >( pPanel );
		CHL2RPMGraphicsPage *pPage = new CHL2RPMGraphicsPage( NULL );
		pPage->AddActionSignalTarget( hDialog );
		pDialog->AddPage( pPage, "#HL2RPM_Options_Graphics" );

		if ( cl_options_graphics_select.GetBool() && pDialog->GetPropertySheet() )
		{
			pDialog->GetPropertySheet()->SetActivePage( pPage );

			// the dialog opened from a console command starts off screen: center it
			int sw, sh, dw, dh;
			surface()->GetScreenSize( sw, sh );
			ipanel()->GetSize( hDialog, dw, dh );
			ipanel()->SetPos( hDialog, ( sw - dw ) / 2, ( sh - dh ) / 2 );
		}
	}

private:
	static VPANEL FindPanel( VPANEL hParent, const char *pszName, int iDepth )
	{
		if ( !hParent || iDepth > 5 )
			return 0;

		const int nChildren = ipanel()->GetChildCount( hParent );
		for ( int i = 0; i < nChildren; i++ )
		{
			VPANEL hChild = ipanel()->GetChild( hParent, i );
			if ( !Q_stricmp( ipanel()->GetName( hChild ), pszName ) )
				return hChild;
		}
		for ( int i = 0; i < nChildren; i++ )
		{
			VPANEL hFound = FindPanel( ipanel()->GetChild( hParent, i ), pszName, iDepth + 1 );
			if ( hFound )
				return hFound;
		}
		return 0;
	}

	VPANEL m_hLastDialog;
};

static CHL2RPMOptionsInjector *g_pOptionsInjector = NULL;

void HL2RPM_CreateOptionsInjector()
{
	if ( g_pOptionsInjector )
		return;

	VPANEL parent = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
	if ( !parent )
		return;
	g_pOptionsInjector = new CHL2RPMOptionsInjector( parent );
}

void HL2RPM_DestroyOptionsInjector()
{
	if ( g_pOptionsInjector )
	{
		g_pOptionsInjector->MarkForDeletion();
		g_pOptionsInjector = NULL;
	}
}

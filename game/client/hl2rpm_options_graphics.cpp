//========= HL2RPM ============================================================//
//
// Purpose: "Graphics" and "Effects" pages inside the standard Options dialog (GameUI).
//
// GameUI.dll is closed source, so the pages are injected at runtime: a hidden
// ticking panel watches for the "OptionsDialog" panel and calls its (virtual)
// PropertyDialog::AddPage with our pages. The vgui_controls headers of Mapbase
// are ABI identical to the ones GameUI was built with (only an access
// specifier and a const differ in Panel.h), and all further communication
// with the dialog (ResetData / ApplyChanges / ApplyButtonEnable) goes through
// VGUI messages.
//
// The pages drive the renderer settings added by the deferred renderer, the
// weather system and the post-processing. Engine settings (resolution, AA,
// texture detail...) stay on the Video tab. Both pages are the same class,
// filled from a table (GfxPageDesc_t): the settings, their choices and presets.
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
// Choices
// ---------------------------------------------------------------------------
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
static const GfxChoice_t s_SSRChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_On", 1 }, { "#HL2RPM_Gfx_Q_High", 2 },
};
static const GfxChoice_t s_AAChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_AA_FXAA", 1 }, { "#HL2RPM_Gfx_AA_TAA", 2 },
};
static const GfxChoice_t s_DoFChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_DoF_Zoom", 1 }, { "#HL2RPM_Gfx_DoF_Always", 2 },
};
static const GfxChoice_t s_SharpenChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_Q_Low", 0.2f }, { "#HL2RPM_Gfx_Q_Medium", 0.4f }, { "#HL2RPM_Gfx_Q_High", 0.65f },
};

struct GfxChoiceList_t
{
	const GfxChoice_t *pChoices;
	int nChoices;
};
#define CHOICES( a ) { a, ARRAYSIZE( a ) }

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
#define GFX_MAX_SETTINGS 16

// ---------------------------------------------------------------------------
// Page "Graphics": shadows, weather, lighting quality
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
	GFX_SOFT_SHADOWS,		// r_csm_pcss 0/1
	GFX_BOUNCE,				// r_deferred_bounce 0/1
	GFX_WATER,				// r_deferred_water_views 0/1
	GFX_AA,					// r_deferred_aa 0 none / 1 FXAA / 2 TAA

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
	"r_csm_pcss",
	"r_deferred_bounce",
	"r_deferred_water_views",
	"r_deferred_aa",
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
	"#HL2RPM_Gfx_SoftShadows",
	"#HL2RPM_Gfx_Bounce",
	"#HL2RPM_Gfx_WaterReflect",
	"#HL2RPM_Gfx_AA",
};

static const GfxChoiceList_t s_GfxChoices[GFX_SETTING_COUNT] =
{
	CHOICES( s_ShadowChoices ),
	CHOICES( s_ShadowChoices ),
	CHOICES( s_EffectChoices ),
	CHOICES( s_EffectChoices ),
	CHOICES( s_RainChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_AAChoices ),
};

// Overall presets. The costs that matter most on the GPU are the sun shadow
// cascades (re-rendering the scene up to 4 times), the lamp shadow map size,
// and the cloud raymarch resolution.
static const float s_flGfxPresets[GFX_PRESET_COUNT][GFX_SETTING_COUNT] =
{
	//	sun	lamp clouds shafts rain wet	ssao soft bounce water aa
	{	0,	1,	1,	0,	0.5f,	0,	0,	0,	0,	0,	0	},	// very low
	{	1,	2,	1,	1,	0.5f,	1,	0,	0,	1,	0,	2	},	// low
	{	2,	3,	2,	1,	1.0f,	1,	1,	1,	1,	1,	2	},	// medium
	{	3,	4,	2,	2,	1.0f,	1,	1,	1,	1,	1,	2	},	// high
	{	5,	5,	3,	3,	1.5f,	1,	1,	1,	1,	1,	2	},	// ultra
};

// ---------------------------------------------------------------------------
// Page "Effects": indirect light and the post-processing of the frame
// ---------------------------------------------------------------------------
enum FxSetting_e
{
	FX_GI = 0,				// r_deferred_gi 0/1
	FX_CONTACT,				// r_deferred_contact_shadows 0/1
	FX_SSR,					// r_weather_ssr 0..2
	FX_SOFTPARTICLES,		// r_deferred_soft_particles 0/1
	FX_MOTES,				// r_weather_motes 0/1
	FX_POST,				// r_hl2rpm_post 0/1
	FX_EXPOSURE,			// r_hl2rpm_exposure 0/1
	FX_BLOOM,				// r_hl2rpm_bloom 0/1
	FX_GRADE,				// r_hl2rpm_grade 0/1
	FX_LENS,				// r_hl2rpm_lens 0/1
	FX_DOF,					// r_hl2rpm_dof 0 off / 1 zoom and dialogs / 2 always
	FX_SHARPEN,				// r_hl2rpm_sharpen 0 .. 0.65

	FX_SETTING_COUNT
};

static const char *s_pszFxConVars[FX_SETTING_COUNT] =
{
	"r_deferred_gi",
	"r_deferred_contact_shadows",
	"r_weather_ssr",
	"r_deferred_soft_particles",
	"r_weather_motes",
	"r_hl2rpm_post",
	"r_hl2rpm_exposure",
	"r_hl2rpm_bloom",
	"r_hl2rpm_grade",
	"r_hl2rpm_lens",
	"r_hl2rpm_dof",
	"r_hl2rpm_sharpen",
};

static const char *s_pszFxLabels[FX_SETTING_COUNT] =
{
	"#HL2RPM_Gfx_GI",
	"#HL2RPM_Gfx_Contact",
	"#HL2RPM_Gfx_SSR",
	"#HL2RPM_Gfx_SoftParticles",
	"#HL2RPM_Gfx_Motes",
	"#HL2RPM_Gfx_Post",
	"#HL2RPM_Gfx_Exposure",
	"#HL2RPM_Gfx_Bloom",
	"#HL2RPM_Gfx_Grade",
	"#HL2RPM_Gfx_Lens",
	"#HL2RPM_Gfx_DoF",
	"#HL2RPM_Gfx_Sharpen",
};

static const GfxChoiceList_t s_FxChoices[FX_SETTING_COUNT] =
{
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_SSRChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_DoFChoices ),
	CHOICES( s_SharpenChoices ),
};

// (the GI costs CPU time for its traces and ~0.5 ms on the GPU; the post-processing ~1 ms)
static const float s_flFxPresets[GFX_PRESET_COUNT][FX_SETTING_COUNT] =
{
	//	gi	contact ssr soft motes post exp bloom grade lens dof sharpen
	{	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0		},	// very low
	{	1,	0,	0,	1,	0,	1,	1,	0,	1,	0,	0,	0.2f	},	// low
	{	1,	0,	1,	1,	1,	1,	1,	1,	1,	0,	1,	0.4f	},	// medium
	{	1,	1,	1,	1,	1,	1,	1,	1,	1,	1,	1,	0.4f	},	// high
	{	1,	1,	2,	1,	1,	1,	1,	1,	1,	1,	1,	0.4f	},	// ultra
};

struct GfxPageDesc_t
{
	const char *pszPanelName;		// also how the injector finds an already added page
	const char *pszTitle;
	const char *pszPresetLabel;
	int nSettings;
	const char * const *ppszConVars;
	const char * const *ppszLabels;
	const GfxChoiceList_t *pChoices;
	const float *pflPresets;		// [ GFX_PRESET_COUNT ][ nSettings ]
	int iRestartSetting;			// a change of this one needs a restart (-1: none)
	const char *pszHint;
};

static const GfxPageDesc_t s_GraphicsPage =
{
	"HL2RPMGraphicsPage", "#HL2RPM_Options_Graphics", "#HL2RPM_Gfx_Preset",
	GFX_SETTING_COUNT, s_pszGfxConVars, s_pszGfxLabels, s_GfxChoices, &s_flGfxPresets[0][0],
	GFX_LAMP_SHADOWS, "#HL2RPM_Gfx_Hint",
};

static const GfxPageDesc_t s_EffectsPage =
{
	"HL2RPMEffectsPage", "#HL2RPM_Options_Effects", "#HL2RPM_Fx_Preset",
	FX_SETTING_COUNT, s_pszFxConVars, s_pszFxLabels, s_FxChoices, &s_flFxPresets[0][0],
	-1, "#HL2RPM_Fx_Hint",
};

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------
class CHL2RPMGraphicsPage : public PropertyPage
{
	DECLARE_CLASS_SIMPLE( CHL2RPMGraphicsPage, PropertyPage );

public:
	CHL2RPMGraphicsPage( Panel *pParent, const GfxPageDesc_t &desc );

	virtual void OnResetData();
	virtual void OnApplyChanges();
	virtual void PerformLayout();

	// GameUI's options sheet fades pages in: it sets the new page's alpha to 0 and
	// animates it with GameUI's AnimationController, which can't resolve panels of
	// another module (client.dll) -> our page stayed fully transparent ("empty"
	// Graphics tab) whenever the tab was clicked instead of opened by our code.
	virtual void OnPageShow();
	virtual void OnThink();

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

private:
	int FindChoice( int iSetting, float flValue ) const;
	float GetPreset( int iPreset, int iSetting ) const { return m_Desc.pflPresets[ iPreset * m_Desc.nSettings + iSetting ]; }
	void LoadFromConVars();
	void SelectPreset( int iPreset );
	int DetectPreset() const;
	float GetChoiceValue( int iSetting ) const;
	void SetDirty();

	const GfxPageDesc_t &m_Desc;

	ComboBox *m_pPreset;
	Label *m_pPresetLabel;
	ComboBox *m_pCombos[GFX_MAX_SETTINGS];
	Label *m_pLabels[GFX_MAX_SETTINGS];
	Label *m_pGPULabel;
	Label *m_pNoteLabel;
	Label *m_pHintLabel;

	bool m_bLoading;
	float m_flLoadedRestartValue;
};

CHL2RPMGraphicsPage::CHL2RPMGraphicsPage( Panel *pParent, const GfxPageDesc_t &desc ) : BaseClass( pParent, desc.pszPanelName ), m_Desc( desc )
{
	m_bLoading = true;
	m_flLoadedRestartValue = -1.0f;

	m_pPresetLabel = new Label( this, "PresetLabel", m_Desc.pszPresetLabel );
	m_pPreset = new ComboBox( this, "PresetCombo", GFX_PRESET_COUNT + 1, false );
	for ( int i = 0; i < GFX_PRESET_COUNT; i++ )
		m_pPreset->AddItem( s_pszPresetLabels[i], new KeyValues( "preset", "index", i ) );
	m_pPreset->AddItem( "#HL2RPM_Gfx_Q_Custom", new KeyValues( "preset", "index", GFX_PRESET_CUSTOM ) );

	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		m_pLabels[s] = new Label( this, VarArgs( "Label%d", s ), m_Desc.ppszLabels[s] );
		const GfxChoiceList_t &list = m_Desc.pChoices[s];
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
	m_pHintLabel = new Label( this, "HintLabel", m_Desc.pszHint );

	LoadFromConVars();
	m_bLoading = false;
}

int CHL2RPMGraphicsPage::FindChoice( int iSetting, float flValue ) const
{
	const GfxChoiceList_t &list = m_Desc.pChoices[iSetting];
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

void CHL2RPMGraphicsPage::PerformLayout()
{
	BaseClass::PerformLayout();

	const int x0 = 20;
	const int iLabelWide = 230;
	const int iComboWide = Min( 190, GetWide() - x0 * 2 - iLabelWide );
	const int x1 = x0 + iLabelWide;
	// the page is only ~300 px tall: fit all rows plus the three info lines
	const int iFooter = 3 * 20 + 8;
	const int iRow = clamp( ( GetTall() - 12 - 34 - iFooter ) / Max( 1, m_Desc.nSettings ), 22, 30 );
	int y = 12;

	m_pPresetLabel->SetBounds( x0, y, iLabelWide, 22 );
	m_pPreset->SetBounds( x1, y, iComboWide, 22 );
	y += 34;

	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		m_pLabels[s]->SetBounds( x0 + 12, y, iLabelWide - 12, 22 );
		m_pCombos[s]->SetBounds( x1, y, iComboWide, 22 );
		y += iRow;
	}

	y += 6;
	m_pNoteLabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
	y += 20;
	m_pGPULabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
	y += 20;
	m_pHintLabel->SetBounds( x0, y, GetWide() - x0 * 2, 20 );
}

void CHL2RPMGraphicsPage::OnPageShow()
{
	BaseClass::OnPageShow();
	SetAlpha( 255 );
	InvalidateLayout();
}

void CHL2RPMGraphicsPage::OnThink()
{
	BaseClass::OnThink();
	if ( IsVisible() && GetAlpha() < 255 )
		SetAlpha( 255 );
}

void CHL2RPMGraphicsPage::LoadFromConVars()
{
	m_bLoading = true;
	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		ConVarRef var( m_Desc.ppszConVars[s] );
		const float flValue = var.IsValid() ? var.GetFloat() : 0.0f;
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, flValue ) );
	}
	m_flLoadedRestartValue = ( m_Desc.iRestartSetting >= 0 ) ? GetChoiceValue( m_Desc.iRestartSetting ) : -1.0f;
	m_pPreset->SilentActivateItemByRow( DetectPreset() );
	m_pNoteLabel->SetVisible( false );
	m_bLoading = false;
}

float CHL2RPMGraphicsPage::GetChoiceValue( int iSetting ) const
{
	const GfxChoiceList_t &list = m_Desc.pChoices[iSetting];
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
		for ( int s = 0; s < m_Desc.nSettings && bMatch; s++ )
			bMatch = fabsf( GetChoiceValue( s ) - GetPreset( p, s ) ) < 0.01f;
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
	for ( int s = 0; s < m_Desc.nSettings; s++ )
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, GetPreset( iPreset, s ) ) );
	m_bLoading = false;
}

void CHL2RPMGraphicsPage::SetDirty()
{
	m_pNoteLabel->SetVisible( m_Desc.iRestartSetting >= 0 && fabsf( GetChoiceValue( m_Desc.iRestartSetting ) - m_flLoadedRestartValue ) > 0.001f );
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

	for ( int s = 0; s < m_Desc.nSettings; s++ )
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
	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		ConVarRef var( m_Desc.ppszConVars[s] );
		if ( !var.IsValid() )
			continue;

		const float flValue = GetChoiceValue( s );
		if ( fabsf( var.GetFloat() - flValue ) > 0.001f )
			var.SetValue( flValue );
	}
	if ( m_Desc.iRestartSetting >= 0 )
		m_flLoadedRestartValue = GetChoiceValue( m_Desc.iRestartSetting );
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
}

// ---------------------------------------------------------------------------
// Injector: waits for the Options dialog and adds the pages to it
// ---------------------------------------------------------------------------
static ConVar cl_options_graphics_select( "cl_options_graphics_select", "0", 0, "Dev: open the Options dialog on the Graphics page (2 = the Effects page)" );
static ConVar cl_options_graphics_page( "cl_options_graphics_page", "1", FCVAR_ARCHIVE, "Add the HL2RPM Graphics and Effects pages to the Options dialog" );

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

		// already has our pages (the dialog handle can be reused after a re-create)
		if ( FindPanel( hDialog, s_GraphicsPage.pszPanelName, 0 ) )
		{
			m_hLastDialog = hDialog;
			return;
		}

		Panel *pPanel = ipanel()->GetPanel( hDialog, "GameUI" );
		if ( !pPanel )
			return;

		m_hLastDialog = hDialog;
		PropertyDialog *pDialog = static_cast< PropertyDialog * >( pPanel );
		CHL2RPMGraphicsPage *pPage = new CHL2RPMGraphicsPage( NULL, s_GraphicsPage );
		pPage->AddActionSignalTarget( hDialog );
		pDialog->AddPage( pPage, s_GraphicsPage.pszTitle );

		CHL2RPMGraphicsPage *pEffects = new CHL2RPMGraphicsPage( NULL, s_EffectsPage );
		pEffects->AddActionSignalTarget( hDialog );
		pDialog->AddPage( pEffects, s_EffectsPage.pszTitle );

		// the tabs are laid out in one row without scrolling: two more tabs don't fit into the
		// dialog's width (the last one was cut off - unreachable). Wider, still centered.
		{
			int dx, dy, dw, dh;
			ipanel()->GetPos( hDialog, dx, dy );
			ipanel()->GetSize( hDialog, dw, dh );
			const int iExtra = 150;
			ipanel()->SetSize( hDialog, dw + iExtra, dh );
			ipanel()->SetPos( hDialog, Max( 0, dx - iExtra / 2 ), dy );
			pPanel->InvalidateLayout( true );
		}

		if ( cl_options_graphics_select.GetBool() && pDialog->GetPropertySheet() )
		{
			pDialog->GetPropertySheet()->SetActivePage( ( cl_options_graphics_select.GetInt() == 2 ) ? pEffects : pPage );

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

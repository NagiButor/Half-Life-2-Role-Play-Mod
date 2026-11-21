//========= HL2RPM ============================================================//
//
// Purpose: "Graphics", "Effects" and "Sound and interface" pages inside the
// standard Options dialog (GameUI).
//
// GameUI.dll is closed source, so the pages are injected at runtime: a hidden
// ticking panel watches for the "OptionsDialog" panel and calls its (virtual)
// PropertyDialog::AddPage with our pages. The vgui_controls headers of Mapbase
// are ABI identical to the ones GameUI was built with (only an access
// specifier and a const differ in Panel.h), and all further communication
// with the dialog (ResetData / ApplyChanges / ApplyButtonEnable) goes through
// VGUI messages.
//
// The pages drive the renderer, weather, post-processing and engine video
// settings (with Video -> Advanced removed). Every setting in Graphics and
// Effects displays a concise explanation of what it does.
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

static const GfxChoice_t s_RendererChoices[] =
{
	{ "#HL2RPM_Gfx_Rend_Deferred", 1 }, { "#HL2RPM_Gfx_Rend_Forward", 0 },
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
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_AA_FXAA", 1 },
};
static const GfxChoice_t s_DoFChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_DoF_Zoom", 1 }, { "#HL2RPM_Gfx_DoF_Always", 2 },
};
// HL2RPM: the 3D view drawn smaller and stretched to the screen (r_hl2rpm_renderscale)
static const GfxChoice_t s_RenderScaleChoices[] =
{
	{ "100%", 1.0f }, { "85%", 0.85f }, { "75%", 0.75f }, { "67%", 0.667f }, { "50%", 0.5f },
};
static const GfxChoice_t s_SharpenChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_Q_Low", 0.2f }, { "#HL2RPM_Gfx_Q_Medium", 0.4f }, { "#HL2RPM_Gfx_Q_High", 0.65f },
};
static const GfxChoice_t s_TextureChoices[] =
{
	{ "#HL2RPM_Gfx_Q_Low", 2 }, { "#HL2RPM_Gfx_Q_Medium", 1 }, { "#HL2RPM_Gfx_Q_High", 0 }, { "#HL2RPM_Gfx_Q_VeryHigh", -1 },
};
static const GfxChoice_t s_ModelChoices[] =
{
	{ "#HL2RPM_Gfx_Q_Low", 2 }, { "#HL2RPM_Gfx_Q_Medium", 1 }, { "#HL2RPM_Gfx_Q_High", 0 },
};
static const GfxChoice_t s_FilteringChoices[] =
{
	{ "#HL2RPM_Gfx_Filt_Bilinear", 0 }, { "#HL2RPM_Gfx_Filt_Trilinear", 1 },
	{ "#HL2RPM_Gfx_Filt_Aniso2", 2 }, { "#HL2RPM_Gfx_Filt_Aniso4", 4 },
	{ "#HL2RPM_Gfx_Filt_Aniso8", 8 }, { "#HL2RPM_Gfx_Filt_Aniso16", 16 },
};
static const GfxChoice_t s_MulticoreChoices[] =
{
	{ "#HL2RPM_Gfx_Multi_Auto", -1 }, { "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Gfx_On", 2 },
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
#define GFX_MAX_SETTINGS 24

// ---------------------------------------------------------------------------
// Page "Graphics": shadows, weather, lighting, textures, filtering, display
// ---------------------------------------------------------------------------
enum GfxSetting_e
{
	// Non-preset renderer mode at the top
	GFX_RENDERER = 0,		// r_deferred 1 Deferred / 0 Forward
	// Preset settings
	GFX_SUN_SHADOWS,		// r_csm_quality 0..5
	GFX_LAMP_SHADOWS,		// r_deferred_shadow_quality_pointspot 0..5
	GFX_CLOUDS,				// r_weather_clouds 0..3
	GFX_SHAFTS,				// r_weather_sunshafts 0..3
	GFX_RAIN,				// r_weather_rain_density (0.5 / 1 / 1.5)
	GFX_WETNESS,			// r_weather_wetness 0/1
	GFX_SSAO,				// r_deferred_ssao 0/1
	GFX_SOFT_SHADOWS,		// r_csm_pcss 0/1
	GFX_BOUNCE,				// r_deferred_bounce 0/1
	GFX_WATER,				// r_deferred_water_views 0/1
	GFX_AA,					// r_deferred_aa 0 none / 1 FXAA
	GFX_PBR,				// r_deferred_pbr 0/1
	GFX_RENDERSCALE,		// r_hl2rpm_renderscale 1 / 0.85 / 0.75 / 0.667 / 0.5
	GFX_TEXTURES,			// mat_picmip 2 / 1 / 0 / -1
	GFX_MODELS,				// r_rootlod 2 / 1 / 0
	GFX_FILTERING,			// mat_trilinear / mat_forceaniso (0..16)
	// Non-preset settings below (do not affect overall preset detection)
	GFX_VSYNC,				// mat_vsync 0/1
	GFX_MULTICORE,			// mat_queue_mode -1 / 0 / 2

	GFX_SETTING_COUNT
};
#define GFX_PRESET_SETTINGS ( GFX_VSYNC - GFX_SUN_SHADOWS )

static const char *s_pszGfxConVars[GFX_SETTING_COUNT] =
{
	"r_deferred",
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
	"r_deferred_pbr",
	"r_hl2rpm_renderscale",
	"mat_picmip",
	"r_rootlod",
	"mat_forceaniso",
	"mat_vsync",
	"mat_queue_mode",
};

static const char *s_pszGfxLabels[GFX_SETTING_COUNT] =
{
	"#HL2RPM_Gfx_Renderer",
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
	"#HL2RPM_Gfx_PBR",
	"#HL2RPM_Gfx_RenderScale",
	"#HL2RPM_Gfx_Textures",
	"#HL2RPM_Gfx_Models",
	"#HL2RPM_Gfx_Filtering",
	"#HL2RPM_Gfx_VSync",
	"#HL2RPM_Gfx_Multicore",
};

static const char *s_pszGfxDescs[GFX_SETTING_COUNT] =
{
	"#HL2RPM_Gfx_Desc_Renderer",
	"#HL2RPM_Gfx_Desc_SunShadows",
	"#HL2RPM_Gfx_Desc_LampShadows",
	"#HL2RPM_Gfx_Desc_Clouds",
	"#HL2RPM_Gfx_Desc_SunShafts",
	"#HL2RPM_Gfx_Desc_Rain",
	"#HL2RPM_Gfx_Desc_Wetness",
	"#HL2RPM_Gfx_Desc_SSAO",
	"#HL2RPM_Gfx_Desc_SoftShadows",
	"#HL2RPM_Gfx_Desc_Bounce",
	"#HL2RPM_Gfx_Desc_WaterReflect",
	"#HL2RPM_Gfx_Desc_AA",
	"#HL2RPM_Gfx_Desc_PBR",
	"#HL2RPM_Gfx_Desc_RenderScale",
	"#HL2RPM_Gfx_Desc_Textures",
	"#HL2RPM_Gfx_Desc_Models",
	"#HL2RPM_Gfx_Desc_Filtering",
	"#HL2RPM_Gfx_Desc_VSync",
	"#HL2RPM_Gfx_Desc_Multicore",
};

static const GfxChoiceList_t s_GfxChoices[GFX_SETTING_COUNT] =
{
	CHOICES( s_RendererChoices ),
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
	CHOICES( s_OnOffChoices ),
	CHOICES( s_RenderScaleChoices ),
	CHOICES( s_TextureChoices ),
	CHOICES( s_ModelChoices ),
	CHOICES( s_FilteringChoices ),
	CHOICES( s_OnOffChoices ),
	CHOICES( s_MulticoreChoices ),
};

// Overall presets. The costs that matter most on the GPU are the sun shadow
// cascades (re-rendering the scene up to 4 times), the lamp shadow map size,
// and the cloud raymarch resolution.
static const float s_flGfxPresets[GFX_PRESET_COUNT][GFX_PRESET_SETTINGS] =
{
	//	sun	lamp clouds shafts rain wet	ssao soft bounce water aa	pbr	scale	tex	mdl	filt
	{	0,	1,	1,	0,	0.5f,	0,	0,	0,	0,	0,	0,	0,	0.5f,	2,	2,	0	},	// very low
	{	1,	2,	1,	1,	0.5f,	1,	0,	0,	1,	0,	1,	1,	0.667f,	1,	1,	1	},	// low
	{	2,	3,	2,	1,	1.0f,	1,	1,	1,	1,	1,	1,	1,	0.75f,	0,	0,	4	},	// medium
	{	3,	4,	2,	2,	1.0f,	1,	1,	1,	1,	1,	1,	1,	0.75f,	0,	0,	8	},	// high
	{	5,	5,	3,	3,	1.5f,	1,	1,	1,	1,	1,	1,	1,	0.75f,	-1,	0,	16	},	// ultra
};

// ---------------------------------------------------------------------------
// Page "Effects": lighting effects, post-processing and FPS counter
// ---------------------------------------------------------------------------
enum FxSetting_e
{
	FX_CONTACT = 0,			// r_deferred_contact_shadows 0/1
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
	// Non-preset settings below
	FX_MOTIONBLUR,			// mat_motion_blur_enabled 0/1
	FX_FPS,					// cl_rpm_fps 0/1

	FX_SETTING_COUNT
};
#define FX_PRESET_SETTINGS FX_MOTIONBLUR

static const char *s_pszFxConVars[FX_SETTING_COUNT] =
{
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
	"mat_motion_blur_enabled",
	"cl_rpm_fps",
};

static const char *s_pszFxLabels[FX_SETTING_COUNT] =
{
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
	"#HL2RPM_Gfx_MotionBlur",
	"#HL2RPM_Gfx_FPS",
};

static const char *s_pszFxDescs[FX_SETTING_COUNT] =
{
	"#HL2RPM_Gfx_Desc_Contact",
	"#HL2RPM_Gfx_Desc_SSR",
	"#HL2RPM_Gfx_Desc_SoftParticles",
	"#HL2RPM_Gfx_Desc_Motes",
	"#HL2RPM_Gfx_Desc_Post",
	"#HL2RPM_Gfx_Desc_Exposure",
	"#HL2RPM_Gfx_Desc_Bloom",
	"#HL2RPM_Gfx_Desc_Grade",
	"#HL2RPM_Gfx_Desc_Lens",
	"#HL2RPM_Gfx_Desc_DoF",
	"#HL2RPM_Gfx_Desc_Sharpen",
	"#HL2RPM_Gfx_Desc_MotionBlur",
	"#HL2RPM_Gfx_Desc_FPS",
};

static const GfxChoiceList_t s_FxChoices[FX_SETTING_COUNT] =
{
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
	CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ),
};

// (the post-processing costs ~1 ms on the GPU)
static const float s_flFxPresets[GFX_PRESET_COUNT][FX_PRESET_SETTINGS] =
{
	//	contact ssr soft motes post exp bloom grade lens dof sharpen
	{	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0		},	// very low
	{	0,	0,	1,	0,	1,	1,	0,	1,	0,	0,	0.2f	},	// low
	{	0,	1,	1,	1,	1,	1,	1,	1,	0,	1,	0.4f	},	// medium
	{	1,	1,	1,	1,	1,	1,	1,	1,	1,	1,	0.4f	},	// high
	{	1,	2,	1,	1,	1,	1,	1,	1,	1,	1,	0.4f	},	// ultra
};

struct GfxPageDesc_t
{
	const char *pszPanelName;		// also how the injector finds an already added page
	const char *pszTitle;
	const char *pszPresetLabel;
	int nSettings;
	int iPresetStart;				// first setting included in presets
	int nPresetSettings;			// number of settings included in presets (0 if none)
	const char * const *ppszConVars;
	const char * const *ppszLabels;
	const char * const *ppszDescs;	// optional per-setting descriptions (NULL if none)
	const GfxChoiceList_t *pChoices;
	const float *pflPresets;		// [ GFX_PRESET_COUNT ][ nPresetSettings ]
	uint32 nRestartMask;			// bitmask of settings whose change needs a restart
	const char *pszHint;
};

static const GfxPageDesc_t s_GraphicsPage =
{
	"HL2RPMGraphicsPage", "#HL2RPM_Options_Graphics", "#HL2RPM_Gfx_Preset",
	GFX_SETTING_COUNT, GFX_SUN_SHADOWS, GFX_PRESET_SETTINGS, s_pszGfxConVars, s_pszGfxLabels, s_pszGfxDescs, s_GfxChoices, &s_flGfxPresets[0][0],
	( 1u << GFX_RENDERER ) | ( 1u << GFX_LAMP_SHADOWS ), "#HL2RPM_Gfx_Hint",
};

static const GfxPageDesc_t s_EffectsPage =
{
	"HL2RPMEffectsPage", "#HL2RPM_Options_Effects", "#HL2RPM_Fx_Preset",
	FX_SETTING_COUNT, 0, FX_PRESET_SETTINGS, s_pszFxConVars, s_pszFxLabels, s_pszFxDescs, s_FxChoices, &s_flFxPresets[0][0],
	0, "#HL2RPM_Fx_Hint",
};

// ---------------------------------------------------------------------------
// Sound and interface page (no presets): the language, the music of the mod, subtitles, the heart
// and breath of a wounded man, the HUD helpers, the pause of the character windows, FOV
// ---------------------------------------------------------------------------
static const GfxChoice_t s_VolumeChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "25 %", 0.25f }, { "50 %", 0.5f }, { "75 %", 0.75f }, { "100 %", 1.0f },
};
static const GfxChoice_t s_SwayChoices[] =
{
	{ "#HL2RPM_Gfx_Off", 0 }, { "#HL2RPM_Snd_Weak", 1 }, { "#HL2RPM_Snd_Strong", 2 },
};
static const GfxChoice_t s_LanguageChoices[] =
{
	{ "#HL2RPM_Lang_Auto", 0 }, { "#HL2RPM_Lang_Russian", 1 }, { "English", 2 },
};
static const GfxChoice_t s_FOVChoices[] =
{
	{ "75\xC2\xB0", 75.0f }, { "80\xC2\xB0", 80.0f }, { "85\xC2\xB0", 85.0f }, { "90\xC2\xB0", 90.0f },
};

static const char *s_pszSndConVars[] =
{
	"cl_rpm_language", "snd_rpm_music", "cl_rpm_subtitles", "cl_vitals_breath_volume", "cl_vitals_heartbeat_volume",
	"cl_vitals_screen", "cl_vitals_sway", "cl_rpm_prompt", "cl_rpm_markers", "cl_rpm_suitboot", "cl_rpm_char_pause", "cl_rpm_menu",
	"fov_desired",
};
static const char *s_pszSndLabels[] =
{
	"#HL2RPM_Snd_Language", "#HL2RPM_Snd_Music", "#HL2RPM_Snd_Subtitles", "#HL2RPM_Snd_Breath", "#HL2RPM_Snd_Heart",
	"#HL2RPM_Snd_HurtScreen", "#HL2RPM_Snd_HurtSway", "#HL2RPM_Snd_Prompt", "#HL2RPM_Snd_Markers", "#HL2RPM_Snd_SuitBoot",
	"#HL2RPM_Snd_CharPause", "#HL2RPM_Snd_NewWindows",
	"#HL2RPM_Snd_FOV",
};
static const GfxChoiceList_t s_SndChoices[] =
{
	CHOICES( s_LanguageChoices ), CHOICES( s_VolumeChoices ), CHOICES( s_OnOffChoices ), CHOICES( s_VolumeChoices ),
	CHOICES( s_VolumeChoices ), CHOICES( s_OnOffChoices ), CHOICES( s_SwayChoices ), CHOICES( s_OnOffChoices ),
	CHOICES( s_OnOffChoices ), CHOICES( s_OnOffChoices ), CHOICES( s_OnOffChoices ), CHOICES( s_OnOffChoices ),
	CHOICES( s_FOVChoices ),
};
COMPILE_TIME_ASSERT( ARRAYSIZE( s_pszSndConVars ) == ARRAYSIZE( s_pszSndLabels ) && ARRAYSIZE( s_pszSndConVars ) == ARRAYSIZE( s_SndChoices ) );

static const GfxPageDesc_t s_SoundPage =
{
	"HL2RPMSoundPage", "#HL2RPM_Options_Sound", NULL,
	ARRAYSIZE( s_pszSndConVars ), 0, 0, s_pszSndConVars, s_pszSndLabels, NULL, s_SndChoices, NULL,
	0, "#HL2RPM_Snd_Hint",
};

// ---------------------------------------------------------------------------
// Controls of the page.
//
// Any key typed while the Options dialog has the focus goes to GameUI's
// Frame::OnKeyTyped -> FocusNavGroup::FindPanelByHotkey -> PropertySheet::HasHotkey,
// which runs  _activePage->GetChild( i )->HasHotkey( key )  without a NULL check.
// Panel::GetChild is ipanel()->GetPanel( child, <module of the caller> ): for panels
// created by client.dll it returned NULL inside GameUI, so pressing ESC (or any other
// key) with one of our pages active crashed in GameUI.dll (NULL read at GameUI+0x10a6ff).
// The direct children of the page therefore answer GameUI's module name: GameUI gets
// the panel and calls its virtual HasHotkey, which is all it does with it.
// (During construction the base class' GetModuleName is still in effect, so the page
// registers the child as its own as usual.)
// ---------------------------------------------------------------------------
static const char s_szGameUIModule[] = "GameUI";	// what GameUI's panels answer (IPanel::GetPanel compares it)

template< class T >
class CGameUIVisible : public T
{
public:
	template< class A1, class A2, class A3 >
	CGameUIVisible( A1 a1, A2 a2, A3 a3 ) : T( a1, a2, a3 ) {}
	template< class A1, class A2, class A3, class A4 >
	CGameUIVisible( A1 a1, A2 a2, A3 a3, A4 a4 ) : T( a1, a2, a3, a4 ) {}

	virtual const char *GetModuleName() { return s_szGameUIModule; }
};

typedef CGameUIVisible< Label > CGfxLabel;
typedef CGameUIVisible< ComboBox > CGfxComboBox;

class CGfxStyledLabel : public CGfxLabel
{
public:
	CGfxStyledLabel( Panel *pParent, const char *pszName, const char *pszText, const char *pszFont, Color col )
		: CGfxLabel( pParent, pszName, pszText ), m_pszFont( pszFont ), m_Color( col ) {}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		CGfxLabel::ApplySchemeSettings( pScheme );
		if ( m_pszFont )
		{
			HFont hFont = pScheme->GetFont( m_pszFont, false );
			if ( hFont != INVALID_FONT )
				SetFont( hFont );
		}
		SetFgColor( m_Color );
	}

private:
	const char *m_pszFont;
	Color m_Color;
};

static bool IsEngineVideoConVar( const char *pszName )
{
	return !Q_stricmp( pszName, "mat_picmip" )
		|| !Q_stricmp( pszName, "r_rootlod" )
		|| !Q_stricmp( pszName, "mat_forceaniso" )
		|| !Q_stricmp( pszName, "mat_vsync" )
		|| !Q_stricmp( pszName, "mat_queue_mode" )
		|| !Q_stricmp( pszName, "mat_motion_blur_enabled" );
}

static float ReadFilteringValue()
{
	ConVarRef aniso( "mat_forceaniso" );
	if ( aniso.IsValid() && aniso.GetInt() >= 2 )
		return (float)aniso.GetInt();
	ConVarRef tri( "mat_trilinear" );
	return ( tri.IsValid() && tri.GetBool() ) ? 1.0f : 0.0f;
}

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

	// nothing here has a hotkey; the base class walks the children through GetChild,
	// which no longer returns them to client.dll (see CGameUIVisible)
	virtual Panel *HasHotkey( wchar_t key ) { return NULL; }

	// GameUI's options sheet fades pages in: it sets the new page's alpha to 0 and
	// animates it with GameUI's AnimationController, which can't resolve panels of
	// another module (client.dll) -> our page stayed fully transparent ("empty"
	// Graphics tab) whenever the tab was clicked instead of opened by our code.
	virtual void OnPageShow();
	virtual void OnThink();

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

private:
	int FindChoice( int iSetting, float flValue ) const;
	float GetPreset( int iPreset, int iSetting ) const { return m_Desc.pflPresets[ iPreset * m_Desc.nPresetSettings + ( iSetting - m_Desc.iPresetStart ) ]; }
	void LoadFromConVars();
	void SelectPreset( int iPreset );
	int DetectPreset() const;
	float GetChoiceValue( int iSetting ) const;
	bool HasRestartChange() const;
	void SetDirty();

	const GfxPageDesc_t &m_Desc;

	ComboBox *m_pPreset;
	Label *m_pPresetLabel;
	ComboBox *m_pCombos[GFX_MAX_SETTINGS];
	Label *m_pLabels[GFX_MAX_SETTINGS];
	Label *m_pDescLabels[GFX_MAX_SETTINGS];
	Label *m_pGPULabel;
	Label *m_pNoteLabel;
	Label *m_pHintLabel;

	bool m_bLoading;
	float m_flLoadedValues[GFX_MAX_SETTINGS];
};

CHL2RPMGraphicsPage::CHL2RPMGraphicsPage( Panel *pParent, const GfxPageDesc_t &desc ) : BaseClass( pParent, desc.pszPanelName ), m_Desc( desc )
{
	m_bLoading = true;
	Q_memset( m_flLoadedValues, 0, sizeof( m_flLoadedValues ) );

	m_pPresetLabel = new CGfxLabel( this, "PresetLabel", m_Desc.pszPresetLabel ? m_Desc.pszPresetLabel : "" );
	m_pPreset = new CGfxComboBox( this, "PresetCombo", GFX_PRESET_COUNT + 1, false );
	m_pPreset->AddActionSignalTarget( this );
	for ( int i = 0; i < GFX_PRESET_COUNT; i++ )
		m_pPreset->AddItem( s_pszPresetLabels[i], new KeyValues( "preset", "index", i ) );
	m_pPreset->AddItem( "#HL2RPM_Gfx_Q_Custom", new KeyValues( "preset", "index", GFX_PRESET_CUSTOM ) );

	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		m_pLabels[s] = new CGfxLabel( this, VarArgs( "Label%d", s ), m_Desc.ppszLabels[s] );
		const GfxChoiceList_t &list = m_Desc.pChoices[s];
		m_pCombos[s] = new CGfxComboBox( this, VarArgs( "Combo%d", s ), list.nChoices, false );
		m_pCombos[s]->AddActionSignalTarget( this );
		for ( int c = 0; c < list.nChoices; c++ )
			m_pCombos[s]->AddItem( list.pChoices[c].pszLabel, new KeyValues( "choice", "index", c ) );

		if ( m_Desc.ppszDescs && m_Desc.ppszDescs[s] )
			m_pDescLabels[s] = new CGfxStyledLabel( this, VarArgs( "Desc%d", s ), m_Desc.ppszDescs[s], "DefaultSmall", Color( 175, 175, 175, 255 ) );
		else
			m_pDescLabels[s] = NULL;
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
	m_pGPULabel = new CGfxStyledLabel( this, "GPULabel", "", "DefaultSmall", Color( 180, 180, 180, 255 ) );
	m_pGPULabel->SetText( wszText );

	m_pNoteLabel = new CGfxStyledLabel( this, "NoteLabel", "#HL2RPM_Gfx_RestartNote", "DefaultSmall", Color( 255, 195, 90, 255 ) );
	m_pNoteLabel->SetVisible( false );
	m_pHintLabel = new CGfxStyledLabel( this, "HintLabel", m_Desc.pszHint, "DefaultSmall", Color( 195, 195, 175, 255 ) );
	if ( !m_Desc.pflPresets )
	{
		m_pPresetLabel->SetVisible( false );
		m_pPreset->SetVisible( false );
		m_pGPULabel->SetVisible( false );
	}

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

	const bool bHasDescs = ( m_Desc.ppszDescs != NULL );
	const int x0 = 16;
	const int iLabelWide = bHasDescs ? 235 : 260;
	const int iComboWide = bHasDescs ? 175 : Min( 190, GetWide() - x0 * 2 - iLabelWide );
	const int x1 = x0 + iLabelWide;
	const int x2 = x1 + iComboWide + 12;
	const int iDescWide = Max( 100, GetWide() - x2 - 12 );

	const int iHeader = m_Desc.pflPresets ? 30 : 0;
	const int iFooter = ( m_Desc.pflPresets ? 42 : 24 ) + 6;
	const int iRow = clamp( ( GetTall() - 10 - iHeader - iFooter ) / Max( 1, m_Desc.nSettings ), 20, 30 );
	const int iCtrlH = Min( 21, iRow - 1 );
	int y = 10;

	if ( m_Desc.pflPresets )
	{
		m_pPresetLabel->SetBounds( x0, y, iLabelWide, 21 );
		m_pPreset->SetBounds( x1, y, iComboWide, 21 );
		y += iHeader;
	}

	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		m_pLabels[s]->SetBounds( x0 + 8, y, iLabelWide - 8, iCtrlH );
		m_pCombos[s]->SetBounds( x1, y, iComboWide, iCtrlH );
		if ( m_pDescLabels[s] )
			m_pDescLabels[s]->SetBounds( x2, y, iDescWide, iCtrlH );
		y += iRow;
	}

	y += 6;
	if ( m_Desc.pflPresets )
	{
		const int iHalf = ( GetWide() - x0 * 2 ) / 2;
		m_pGPULabel->SetBounds( x0, y, iHalf, 18 );
		m_pNoteLabel->SetBounds( x0 + iHalf + 8, y, GetWide() - x0 * 2 - iHalf - 8, 18 );
		y += 20;
	}
	m_pHintLabel->SetBounds( x0, y, GetWide() - x0 * 2, 18 );
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
		float flValue = 0.0f;
		if ( !Q_stricmp( m_Desc.ppszConVars[s], "mat_forceaniso" ) )
		{
			flValue = ReadFilteringValue();
		}
		else
		{
			ConVarRef var( m_Desc.ppszConVars[s] );
			flValue = var.IsValid() ? var.GetFloat() : 0.0f;
		}
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, flValue ) );
		m_flLoadedValues[s] = GetChoiceValue( s );
	}
	if ( m_Desc.pflPresets )
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
	for ( int p = 0; m_Desc.pflPresets && p < GFX_PRESET_COUNT; p++ )
	{
		bool bMatch = true;
		for ( int s = m_Desc.iPresetStart; s < m_Desc.iPresetStart + m_Desc.nPresetSettings && bMatch; s++ )
			bMatch = fabsf( GetChoiceValue( s ) - GetPreset( p, s ) ) < 0.01f;
		if ( bMatch )
			return p;
	}
	return GFX_PRESET_CUSTOM;
}

void CHL2RPMGraphicsPage::SelectPreset( int iPreset )
{
	if ( iPreset < 0 || iPreset >= GFX_PRESET_COUNT || !m_Desc.pflPresets )
		return;

	m_bLoading = true;
	for ( int s = m_Desc.iPresetStart; s < m_Desc.iPresetStart + m_Desc.nPresetSettings; s++ )
		m_pCombos[s]->SilentActivateItemByRow( FindChoice( s, GetPreset( iPreset, s ) ) );
	m_bLoading = false;
}

bool CHL2RPMGraphicsPage::HasRestartChange() const
{
	if ( !m_Desc.nRestartMask )
		return false;
	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		if ( ( m_Desc.nRestartMask & ( 1u << s ) ) && fabsf( GetChoiceValue( s ) - m_flLoadedValues[s] ) > 0.001f )
			return true;
	}
	return false;
}

void CHL2RPMGraphicsPage::SetDirty()
{
	m_pNoteLabel->SetVisible( HasRestartChange() );
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
			if ( m_Desc.pflPresets && s >= m_Desc.iPresetStart && s < m_Desc.iPresetStart + m_Desc.nPresetSettings )
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
	bool bVideoChanged = false;
	for ( int s = 0; s < m_Desc.nSettings; s++ )
	{
		const char *pszName = m_Desc.ppszConVars[s];
		const float flValue = GetChoiceValue( s );

		if ( !Q_stricmp( pszName, "mat_forceaniso" ) )
		{
			const float flCur = ReadFilteringValue();
			if ( fabsf( flCur - flValue ) > 0.01f )
			{
				const int iVal = (int)( flValue + 0.5f );
				engine->ClientCmd_Unrestricted( VarArgs( "mat_trilinear %d\n", iVal == 1 ? 1 : 0 ) );
				engine->ClientCmd_Unrestricted( VarArgs( "mat_forceaniso %d\n", Max( 1, iVal ) ) );
				bVideoChanged = true;
			}
			continue;
		}

		ConVarRef var( pszName );
		if ( !var.IsValid() )
			continue;

		if ( fabsf( var.GetFloat() - flValue ) > 0.001f )
		{
			if ( IsEngineVideoConVar( pszName ) )
			{
				const int iVal = (int)( flValue >= 0.0f ? ( flValue + 0.5f ) : ( flValue - 0.5f ) );
				engine->ClientCmd_Unrestricted( VarArgs( "%s %d\n", pszName, iVal ) );
				bVideoChanged = true;
			}
			else if ( var.IsFlagSet( FCVAR_REPLICATED ) )
			{
				const int iVal = (int)( flValue >= 0.0f ? ( flValue + 0.5f ) : ( flValue - 0.5f ) );
				engine->ClientCmd_Unrestricted( VarArgs( "%s %d\n", pszName, iVal ) );
			}
			else
			{
				var.SetValue( flValue );
			}
		}
	}
	m_pNoteLabel->SetVisible( HasRestartChange() );
	if ( bVideoChanged )
		engine->ClientCmd_Unrestricted( "mat_savechanges\n" );
	engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
}

// ---------------------------------------------------------------------------
// Injector: waits for the Options dialog and adds the pages to it
// ---------------------------------------------------------------------------
static ConVar cl_options_graphics_select( "cl_options_graphics_select", "0", 0, "Dev: open the Options dialog on the Graphics page (2 = the Effects page, 3 = Sound and interface)" );
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

		Panel *pPanel = ipanel()->GetPanel( hDialog, s_szGameUIModule );
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

		CHL2RPMGraphicsPage *pSound = new CHL2RPMGraphicsPage( NULL, s_SoundPage );
		pSound->AddActionSignalTarget( hDialog );
		pDialog->AddPage( pSound, s_SoundPage.pszTitle );

		// Wider for inline setting descriptions, taller for the 19 Graphics rows;
		// clamp to the screen and center so it never clips at 720p or from console.
		{
			int sw, sh, dw, dh;
			surface()->GetScreenSize( sw, sh );
			ipanel()->GetSize( hDialog, dw, dh );
			const int iNewW = Min( sw - 32, dw + 500 );
			const int iNewH = Min( sh - 32, dh + 185 );
			ipanel()->SetSize( hDialog, iNewW, iNewH );
			ipanel()->SetPos( hDialog, Max( 0, ( sw - iNewW ) / 2 ), Max( 0, ( sh - iNewH ) / 2 ) );
			pPanel->InvalidateLayout( true );
		}

		if ( cl_options_graphics_select.GetBool() && pDialog->GetPropertySheet() )
		{
			const int iSel = cl_options_graphics_select.GetInt();
			pDialog->GetPropertySheet()->SetActivePage( iSel == 3 ? pSound : iSel == 2 ? pEffects : pPage );
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

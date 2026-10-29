//========= HL2RPM ============//
//
// Язык игры: Настройки -> Игра -> Язык («как в Steam» / русский / English), cl_hl2rpm_language.
// Source берёт язык у Steam один раз при запуске (cl_language); здесь он меняется в любой момент:
//   - cl_language (USERINFO: по нему сервер берёт озвучку vo/hl2rpm/<язык>/, субтитры - словарь
//     closecaption_<язык>.dat), реестр HKCU\Software\Valve\Source\Language (по нему ILocalize
//     раскрывает %language%), конвар english;
//   - заново читаются строки движка, GameUI, HL2, эпизодов, Mapbase и мода (токены заменяются);
//   - уже созданные надписи VGUI (главное меню, вкладки и кнопки настроек, HUD) обновляются по своему
//     токену (TextImage::GetUnlocalizedTextSymbol - текст был задан токеном); таблички в мире
//     (vgui_text_display) перерисовываются. Экраны RPG локализуют текст при каждой отрисовке.
// Выбор хранится в cfg/hl2rpm_language.txt: config.cfg исполняется уже после того, как GameUI
// построил меню, поэтому язык применяется в CHLClient::Init (HL2RPM_Language_Init).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_language.h"
#include "filesystem.h"
#include "ienginevgui.h"
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/ISystem.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/TextImage.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

#define LANGUAGE_FILE "cfg/hl2rpm_language.txt"

static char s_szSteamLanguage[64] = "english";
static bool s_bInitialized = false;
static bool s_bApplying = false;

static void OnLanguageChanged( IConVar *pVar, const char *pszOld, float flOld );
ConVar cl_hl2rpm_language( "cl_hl2rpm_language", "0", 0, "Game language: 0 - as in Steam, 1 - Russian, 2 - English (Options -> Game)", OnLanguageChanged );

const char *HL2RPM_Language_Get()
{
	switch ( cl_hl2rpm_language.GetInt() )
	{
	case 1: return "russian";
	case 2: return "english";
	default: return s_szSteamLanguage;
	}
}

// ---------------------------------------------------------------------------
// Строки
// ---------------------------------------------------------------------------
static void ReloadLocalization()
{
	static const char *s_pszFiles[][2] =
	{
		{ "resource/valve_%language%.txt", "GAME" },
		{ "resource/platform_%language%.txt", "PLATFORM" },
		{ "resource/vgui_%language%.txt", "PLATFORM" },
		{ "resource/gameui_%language%.txt", "GAME" },
		{ "resource/hl2_%language%.txt", "GAME" },
		{ "resource/episodic_%language%.txt", "GAME" },
		{ "resource/ep2_%language%.txt", "GAME" },
		{ "resource/chat_%language%.txt", "GAME" },
		{ "resource/mapbase_%language%.txt", "GAME" },
		{ "resource/closecaption_%language%.txt", "GAME" },
		{ "resource/hl2rpm_%language%.txt", "GAME" },
		{ "resource/hl2rpm_rpg_%language%.txt", "GAME" },
		{ "resource/hl2rpm_dialog_%language%.txt", "GAME" },
	};
	for ( int i = 0; i < ARRAYSIZE( s_pszFiles ); ++i )
		g_pVGuiLocalize->AddFile( s_pszFiles[i][0], s_pszFiles[i][1], true );
}

// ---------------------------------------------------------------------------
// Готовые надписи VGUI
// ---------------------------------------------------------------------------
// классы, производные от Label (по имени: dynamic_cast между модулями не работает)
static const char *s_pszLabelClasses[] =
{
	"Label", "Button", "ToggleButton", "CheckButton", "RadioButton", "MenuItem", "MenuButton", "URLLabel",
	"PageTab", "CGameMenuItem", "CCvarToggleCheckButton", "CLabeledCommandComboBox", "CURLButton",
	"CBitmapImageButton", "CGameMenuButton", NULL
};

static bool IsLabelClass( const char *pszClass )
{
	if ( !pszClass )
		return false;
	for ( int i = 0; s_pszLabelClasses[i]; ++i )
	{
		if ( !Q_stricmp( pszClass, s_pszLabelClasses[i] ) )
			return true;
	}
	return false;
}

// IPanel::GetPanel отдаёт панель, только если назван её модуль: перебрать известные
static Panel *PanelOf( VPANEL vp )
{
	static const char *s_pszModules[] = { "GameUI", "CLIENT", "ClientDLL", "BaseUI", "ServerBrowser", NULL };
	for ( int i = 0; s_pszModules[i]; ++i )
	{
		Panel *p = ipanel()->GetPanel( vp, s_pszModules[i] );
		if ( p )
			return p;
	}
	return NULL;
}

static int RelabelTree( VPANEL vp, int iDepth )
{
	if ( !vp || iDepth > 40 )
		return 0;
	int n = 0;
	if ( IsLabelClass( ipanel()->GetClassName( vp ) ) )
	{
		Panel *pPanel = PanelOf( vp );
		if ( pPanel )
		{
			// Label::GetText отдаёт уже переведённый текст; ключ перевода хранит TextImage надписи
			// (GetTextImage и GetUnlocalizedTextSymbol - виртуальные: годятся и для панелей GameUI)
			Label *pLabel = static_cast< Label * >( pPanel );
			TextImage *pImage = pLabel->GetTextImage();
			const StringIndex_t sym = pImage ? pImage->GetUnlocalizedTextSymbol() : INVALID_LOCALIZE_STRING_INDEX;
			const char *pszToken = ( sym != INVALID_LOCALIZE_STRING_INDEX ) ? g_pVGuiLocalize->GetNameByIndex( sym ) : NULL;
			if ( pszToken && pszToken[0] )
			{
				char sz[256];
				Q_snprintf( sz, sizeof( sz ), "#%s", pszToken );
				pLabel->SetText( sz );
				++n;
			}
		}
	}
	const int nChildren = ipanel()->GetChildCount( vp );
	for ( int i = 0; i < nChildren; ++i )
		n += RelabelTree( ipanel()->GetChild( vp, i ), iDepth + 1 );
	if ( n )
	{
		// меню и списки подгоняют ширину под текст
		Panel *pPanel = PanelOf( vp );
		if ( pPanel )
			pPanel->InvalidateLayout();
	}
	return n;
}

extern void VGuiTextDisplay_RefreshAll();
extern ConVar cc_lang;
extern void OnCaptionLanguageChanged( IConVar *pConVar, const char *pOldString, float flOldValue );

static void ApplyLanguage( bool bRelabel )
{
	const char *pszLang = HL2RPM_Language_Get();
	s_bApplying = true;
	ConVarRef cl_language( "cl_language" );
	if ( cl_language.IsValid() && Q_stricmp( cl_language.GetString(), pszLang ) )
		cl_language.SetValue( pszLang );
	vgui::system()->SetRegistryString( "HKEY_CURRENT_USER\\Software\\Valve\\Source\\Language", pszLang );
	ConVarRef english( "english" );
	if ( english.IsValid() )
		english.SetValue( !Q_stricmp( pszLang, "english" ) ? 1 : 0 );
	ReloadLocalization();
	// субтитры: словарь closecaption_<язык>.dat (cc_lang пуст - язык интерфейса)
	OnCaptionLanguageChanged( &cc_lang, cc_lang.GetString(), 0.0f );
	if ( bRelabel )
	{
		int n = 0;
		static const VGuiPanel_t s_Roots[] = { PANEL_ROOT, PANEL_GAMEUIDLL, PANEL_CLIENTDLL, PANEL_INGAMESCREENS, PANEL_CLIENTDLL_TOOLS };
		for ( int i = 0; i < ARRAYSIZE( s_Roots ); ++i )
		{
			VPANEL root = enginevgui->GetPanel( s_Roots[i] );
			if ( root && ( i == 0 || root != enginevgui->GetPanel( PANEL_ROOT ) ) )
				n += RelabelTree( root, 0 );
		}
		VGuiTextDisplay_RefreshAll();
		DevMsg( "[language] %s: %d labels updated\n", pszLang, n );
	}
	s_bApplying = false;
}

static void SaveChoice()
{
	FileHandle_t f = g_pFullFileSystem->Open( LANGUAGE_FILE, "wb", "MOD" );
	if ( !f )
		return;
	g_pFullFileSystem->FPrintf( f, "%d\n", cl_hl2rpm_language.GetInt() );
	g_pFullFileSystem->Close( f );
}

static void OnLanguageChanged( IConVar *pVar, const char *pszOld, float flOld )
{
	if ( !s_bInitialized || s_bApplying )
		return;
	const int v = clamp( cl_hl2rpm_language.GetInt(), 0, 2 );
	if ( v != cl_hl2rpm_language.GetInt() )
	{
		cl_hl2rpm_language.SetValue( v );
		return;
	}
	ApplyLanguage( true );
	SaveChoice();
	Msg( "[language] game language: %s\n", HL2RPM_Language_Get() );
}

void HL2RPM_Language_Init()
{
	// язык, который выбрал Steam (cl_language выставлен до загрузки client.dll)
	ConVarRef cl_language( "cl_language" );
	if ( cl_language.IsValid() && cl_language.GetString()[0] )
		Q_strncpy( s_szSteamLanguage, cl_language.GetString(), sizeof( s_szSteamLanguage ) );
	int iSaved = 0;
	FileHandle_t f = g_pFullFileSystem->Open( LANGUAGE_FILE, "rb", "MOD" );
	if ( f )
	{
		char sz[16] = "";
		g_pFullFileSystem->ReadLine( sz, sizeof( sz ), f );
		g_pFullFileSystem->Close( f );
		iSaved = clamp( atoi( sz ), 0, 2 );
	}
	s_bApplying = true;
	cl_hl2rpm_language.SetValue( iSaved );
	s_bApplying = false;
	s_bInitialized = true;
	if ( iSaved != 0 && Q_stricmp( HL2RPM_Language_Get(), s_szSteamLanguage ) )
		ApplyLanguage( true );		// GameUI уже построил меню на языке Steam
	DevMsg( "[language] Steam: %s, game: %s\n", s_szSteamLanguage, HL2RPM_Language_Get() );
}

CON_COMMAND( cl_hl2rpm_language_refresh, "Re-read the localization files of the current game language and update the menus" )
{
	ApplyLanguage( true );
}

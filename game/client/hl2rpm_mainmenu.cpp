//========= HL2RPM ============================================================//
//
// The main menu and the pause menu, after the user's concept (Portal 2 style): a dark worn
// wall, a painted lambda in a ring, "HALF-LIFE 2 / ROLE PLAY MOD", a column of items with a
// grey bar under the one the mouse is on, nested pages (new game -> difficulty, "are you
// sure?"), a footer with a hint, our own loading screen and the menu music.
//
// The panel lives in GameUI's root panel, above GameUI's own (hidden) menu and title and
// below its dialogs (options, load, save - they are popups). Its items run GameUI's
// commands ("gamemenucommand OpenOptionsDialog") or engine commands.
//
// Art: hl2rpm_extras/tools/make_menu_assets.py -> materials/vgui/hl2rpm/menu/*.
// Music: sound/music/hl2rpm/menu/hl2rpm_menu_theme.mp3 (hl2rpm_extras/tools/make_menu_music.py).
// cl_rpm_mainmenu 0 brings the stock menu back.
//
//=============================================================================//
#include "cbase.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/IScheme.h>
#include <vgui/IInput.h>
#include <vgui/ILocalize.h>
#include "ienginevgui.h"
#include "inputsystem/iinputsystem.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"
#include "engine/IEngineSound.h"
#include "hl2rpm_items.h"
#include "GameUI/IGameUI.h"
#include "tier0/icommandline.h"
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static ConVar cl_rpm_mainmenu( "cl_rpm_mainmenu", "1", FCVAR_ARCHIVE, "HL2RPM: the new main / pause menu (0 = the stock GameUI menu)" );
static ConVar snd_rpm_menu_music( "snd_rpm_menu_music", "1", FCVAR_ARCHIVE, "HL2RPM: music in the main menu" );
static ConVar snd_rpm_menu_music_volume( "snd_rpm_menu_music_volume", "0.9", FCVAR_ARCHIVE, "HL2RPM: the volume of the menu music (x snd_musicvolume)" );
static ConVar cl_rpm_mainmenu_debug( "cl_rpm_mainmenu_debug", "0", 0, "HL2RPM: the main menu prints its state and the GameUI panels it hides" );
static ConVar cl_rpm_mainmenu_loading( "cl_rpm_mainmenu_loading", "1", FCVAR_ARCHIVE, "HL2RPM: our loading screen instead of GameUI's loading dialog" );

static ConVar cl_rpm_mainmenu_grain( "cl_rpm_mainmenu_grain", "4", 0, "HL2RPM: the strength of the film grain of the menu (0..255)" );
static ConVar cl_rpm_mainmenu_spinner_speed( "cl_rpm_mainmenu_spinner_speed", "0.6", 0, "HL2RPM: the turns per second of the valve wheel of the loading screen" );

#define MENU_MUSIC		"music/hl2rpm/menu/hl2rpm_menu_theme.mp3"
static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

static float Now() { return (float)Plat_FloatTime(); }
static float MenuClamp01( float x ) { return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x ); }
static float EaseOut( float t ) { t = MenuClamp01( t ); return 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t ); }
static float MenuApproach( float cur, float target, float speed ) { return cur < target ? MIN( target, cur + speed ) : MAX( target, cur - speed ); }

enum MenuPage_t { PAGE_ROOT, PAGE_NEWGAME, PAGE_CONFIRM_QUIT, PAGE_CONFIRM_MENU, PAGE_CONFIRM_NEW };
enum MenuAction_t
{
	ACT_CONTINUE, ACT_RESUME, ACT_NEWGAME, ACT_LOAD, ACT_SAVE, ACT_OPTIONS, ACT_MAINMENU, ACT_QUIT,
	ACT_SKILL, ACT_BACK, ACT_YES, ACT_NO,
};

struct MenuItem_t
{
	const char *label;
	const char *hint;
	int action;
	int arg;
	float hover;		// 0..1, eases toward the mouse
};

// the lines of the wall (fractions of the screen: the concept, 1672 x 941)
struct GridLine_t { float x0, y0, x1, y1; int alpha; };
static const GridLine_t s_GridLines[] =
{
	{ 0.057f, 0.035f, 0.057f, 0.965f, 20 },
	{ 0.565f, 0.115f, 0.565f, 0.835f, 13 },
	{ 0.560f, 0.083f, 0.985f, 0.083f, 17 },
	{ 0.030f, 0.792f, 0.985f, 0.792f, 19 },
	{ 0.075f, 0.335f, 0.470f, 0.335f, 9 },
	{ 0.985f, 0.060f, 0.985f, 0.940f, 10 },
};
static const float s_GridCrosses[][2] = { { 0.933f, 0.083f }, { 0.446f, 0.792f }, { 0.933f, 0.792f }, { 0.057f, 0.083f }, { 0.565f, 0.835f } };

#define NUM_MOTES 70
struct Mote_t { float x, y, depth, size, speed, phase, drift; };

class CRPMMainMenu : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMMainMenu, Panel );
public:
	CRPMMainMenu( VPANEL parent );
	virtual ~CRPMMainMenu();

	virtual void ApplySchemeSettings( IScheme *pScheme );
	virtual void OnTick();
	virtual void Paint();
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMousePressed( MouseCode code );
	virtual void OnMouseWheeled( int delta ) {}

	void RestoreStockMenu();

private:
	enum State_t { ST_HIDDEN, ST_MAIN, ST_PAUSE, ST_LOADING };

	void BuildPage( int page );
	void GoPage( int page );
	void Activate( int index );
	void Back();
	int ItemAt( int x, int y );
	void Command( const char *cmd );
	void FindNewestSave();
	void HideStockPanels( bool bHide );
	void UpdateDialogs();
	void UpdateKeys();
	void UpdateMusic( float dt );
	float LoadingProgress( bool bHideDialog );

	int Tex( int &id, const char *name );
	void DrawWall( int w, int h, float t );
	void DrawPause( int w, int h );
	void DrawItems( int w, int h, float t );
	void DrawFooter( int w, int h, float t );
	void DrawLoading( int w, int h, float t );
	void DrawLoadingWheel( int w, int h, float t );
	void DrawText( HFont font, int x, int y, Color c, const char *utf8, int tracking = 0 );
	int TextWide( HFont font, const char *utf8, int tracking = 0 );

	State_t m_eState;
	bool m_bWasVisible;
	bool m_bDialogOpen;
	bool m_bStockHidden;
	float m_flShowTime;		// the menu came up (fade in)
	float m_flPageTime;		// the page changed (items come in)
	float m_flLastThink;
	int m_iPage;
	int m_iHover;			// item under the mouse / chosen with the keys, -1 none
	bool m_bKeyNav;			// the keys moved the selection: the mouse doesn't take it back until it moves
	int m_iMouseX, m_iMouseY;
	float m_flParX, m_flParY;	// parallax (smoothed mouse)
	CUtlVector< MenuItem_t > m_Items;
	char m_szHeader[128];

	// keys
	bool m_bKeyDown[6];
	float m_flKeyRepeat;

	// newest save (continue)
	char m_szNewestSave[MAX_PATH];
	char m_szNewestHint[192];

	// flicker of the emblem
	float m_flNextFlicker, m_flFlickerEnd;

	// music
	float m_flMusicVol;
	float m_flMusicStarted;
	float m_flMusicLength;
	bool m_bMusicOn;

	// loading
	float m_flLoadShown;
	float m_flLoadProgress;
	int m_iTip;

	Mote_t m_Motes[NUM_MOTES];

	HFont m_hFontItem, m_hFontSmall, m_hFontHeader, m_hFontLoading;
	int m_iTexBg, m_iTexEmblem, m_iTexValve, m_iTexTitle, m_iTexGrain, m_iTexVignette, m_iTexMote, m_iTexIcon, m_iTexWhite;
};

static CRPMMainMenu *s_pMainMenu = NULL;
static IGameUI *s_pGameUI = NULL;		// told about our menu (SetMainMenuOverride)

CRPMMainMenu::CRPMMainMenu( VPANEL parent ) : BaseClass( NULL, "HL2RPMMainMenu" )
{
	SetParent( parent );
	SetScheme( scheme()->LoadSchemeFromFile( "resource/ClientScheme.res", "ClientScheme" ) );
	SetProportional( false );
	SetZPos( 50 );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( true );
	SetVisible( false );

	m_eState = ST_HIDDEN;
	m_bWasVisible = false;
	m_bDialogOpen = false;
	m_bStockHidden = false;
	m_flShowTime = m_flPageTime = m_flLastThink = 0.0f;
	m_iPage = PAGE_ROOT;
	m_iHover = -1;
	m_bKeyNav = false;
	m_iMouseX = m_iMouseY = 0;
	m_flParX = m_flParY = 0.0f;
	m_szHeader[0] = 0;
	memset( m_bKeyDown, 0, sizeof( m_bKeyDown ) );
	m_flKeyRepeat = 0.0f;
	m_szNewestSave[0] = m_szNewestHint[0] = 0;
	m_flNextFlicker = 8.0f;
	m_flFlickerEnd = 0.0f;
	m_flMusicVol = 0.0f;
	m_flMusicStarted = 0.0f;
	m_flMusicLength = 0.0f;
	m_bMusicOn = false;
	m_flLoadShown = 0.0f;
	m_flLoadProgress = 0.0f;
	m_iTip = 0;
	m_hFontItem = m_hFontSmall = m_hFontHeader = m_hFontLoading = INVALID_FONT;
	m_iTexBg = m_iTexEmblem = m_iTexValve = m_iTexTitle = m_iTexGrain = m_iTexVignette = m_iTexMote = m_iTexIcon = m_iTexWhite = -1;

	for ( int i = 0; i < NUM_MOTES; i++ )
	{
		Mote_t &m = m_Motes[i];
		m.x = RandomFloat( 0, 1 );
		m.y = RandomFloat( 0, 1 );
		m.depth = RandomFloat( 0.25f, 1.0f );
		m.size = RandomFloat( 0.6f, 1.6f ) * ( i % 9 == 0 ? 3.5f : 1.0f );		// a few near, out of focus
		m.speed = RandomFloat( 0.004f, 0.012f );
		m.phase = RandomFloat( 0, 6.283f );
		m.drift = RandomFloat( -0.004f, 0.006f );
	}

	ivgui()->AddTickSignal( GetVPanel(), 0 );
}

CRPMMainMenu::~CRPMMainMenu()
{
	if ( m_bMusicOn )
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, 0.0f, PITCH_NORM, SND_STOP );
}

void CRPMMainMenu::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	m_hFontItem = pScheme->GetFont( "RPMMenu_Item", true );
	m_hFontSmall = pScheme->GetFont( "RPMMenu_Small", true );
	m_hFontHeader = pScheme->GetFont( "RPMMenu_Header", true );
	m_hFontLoading = pScheme->GetFont( "RPMMenu_Loading", true );
	SetPaintBackgroundEnabled( false );
}

int CRPMMainMenu::Tex( int &id, const char *name )
{
	if ( id == -1 )
	{
		id = surface()->CreateNewTextureID();
		surface()->DrawSetTextureFile( id, name, true, false );
	}
	return id;
}

//-----------------------------------------------------------------------------
// text (UTF-8, with optional letter spacing)
//-----------------------------------------------------------------------------
int CRPMMainMenu::TextWide( HFont font, const char *utf8, int tracking )
{
	wchar_t w[256];
	V_UTF8ToUnicode( utf8, w, sizeof( w ) );
	if ( !tracking )
	{
		int tw, th;
		surface()->GetTextSize( font, w, tw, th );
		return tw;
	}
	int total = 0;
	for ( int i = 0; w[i]; i++ )
		total += surface()->GetCharacterWidth( font, w[i] ) + ( w[i + 1] ? tracking : 0 );
	return total;
}

void CRPMMainMenu::DrawText( HFont font, int x, int y, Color c, const char *utf8, int tracking )
{
	wchar_t w[256];
	V_UTF8ToUnicode( utf8, w, sizeof( w ) );
	surface()->DrawSetTextFont( font );
	surface()->DrawSetTextColor( c );
	if ( !tracking )
	{
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( w, wcslen( w ) );
		return;
	}
	for ( int i = 0; w[i]; i++ )
	{
		surface()->DrawSetTextPos( x, y );
		surface()->DrawUnicodeChar( w[i] );
		x += surface()->GetCharacterWidth( font, w[i] ) + tracking;
	}
}

//-----------------------------------------------------------------------------
// pages
//-----------------------------------------------------------------------------
void CRPMMainMenu::BuildPage( int page )
{
	m_Items.RemoveAll();
	m_szHeader[0] = 0;
	const bool bInGame = m_eState == ST_PAUSE;
	MenuItem_t it;
	it.hover = 0.0f;
	it.arg = 0;
#define ADD( l, h, a, g ) { it.label = ( l ); it.hint = ( h ); it.action = ( a ); it.arg = ( g ); m_Items.AddToTail( it ); }
	switch ( page )
	{
	case PAGE_ROOT:
		if ( bInGame )
		{
			ADD( TR( "RESUME GAME", "ВЕРНУТЬСЯ В ИГРУ" ), "", ACT_RESUME, 0 );
			ADD( TR( "SAVE GAME", "СОХРАНИТЬ" ), TR( "Save your progress.", "Сохранить прогресс." ), ACT_SAVE, 0 );
			ADD( TR( "LOAD GAME", "ЗАГРУЗИТЬ" ), TR( "Load a saved game.", "Загрузить сохранённую игру." ), ACT_LOAD, 0 );
			ADD( TR( "NEW GAME", "НОВАЯ ИГРА" ), TR( "Start from the beginning.", "Начать игру с самого начала." ), ACT_NEWGAME, 0 );
			ADD( TR( "OPTIONS", "НАСТРОЙКИ" ), TR( "Video, audio, controls, language.", "Графика, звук, управление, язык." ), ACT_OPTIONS, 0 );
			ADD( TR( "MAIN MENU", "ГЛАВНОЕ МЕНЮ" ), TR( "Leave the game for the main menu.", "Выйти из игры в главное меню." ), ACT_MAINMENU, 0 );
			ADD( TR( "QUIT", "ВЫХОД" ), TR( "Close the game.", "Закрыть игру." ), ACT_QUIT, 0 );
		}
		else
		{
			if ( m_szNewestSave[0] )
				ADD( TR( "CONTINUE", "ПРОДОЛЖИТЬ" ), m_szNewestHint, ACT_CONTINUE, 0 );
			ADD( TR( "NEW GAME", "НОВАЯ ИГРА" ), TR( "Start from the beginning.", "Начать игру с самого начала." ), ACT_NEWGAME, 0 );
			ADD( TR( "LOAD GAME", "ЗАГРУЗИТЬ" ), TR( "Load a saved game.", "Загрузить сохранённую игру." ), ACT_LOAD, 0 );
			ADD( TR( "OPTIONS", "НАСТРОЙКИ" ), TR( "Video, audio, controls, language.", "Графика, звук, управление, язык." ), ACT_OPTIONS, 0 );
			ADD( TR( "QUIT", "ВЫХОД" ), TR( "Close the game.", "Закрыть игру." ), ACT_QUIT, 0 );
		}
		break;
	case PAGE_NEWGAME:
		Q_strncpy( m_szHeader, TR( "NEW GAME  /  DIFFICULTY", "НОВАЯ ИГРА  /  СЛОЖНОСТЬ" ), sizeof( m_szHeader ) );
		ADD( TR( "EASY", "ЛЁГКАЯ" ), TR( "Enemies are weaker and hit softer. For the story.", "Враги слабее и бьют мягче. Для сюжета." ), ACT_SKILL, 1 );
		ADD( TR( "NORMAL", "ОБЫЧНАЯ" ), TR( "As Half-Life 2 was meant to be played.", "Так, как задумана Half-Life 2." ), ACT_SKILL, 2 );
		ADD( TR( "HARD", "СЛОЖНАЯ" ), TR( "Enemies are tougher, ammo and medkits give less.", "Враги крепче, патроны и аптечки дают меньше." ), ACT_SKILL, 3 );
		ADD( TR( "BACK", "НАЗАД" ), "", ACT_BACK, 0 );
		break;
	case PAGE_CONFIRM_QUIT:
		Q_strncpy( m_szHeader, TR( "QUIT THE GAME?", "ВЫЙТИ ИЗ ИГРЫ?" ), sizeof( m_szHeader ) );
		ADD( TR( "YES", "ДА" ), bInGame ? TR( "Unsaved progress will be lost.", "Несохранённый прогресс будет потерян." ) : "", ACT_YES, ACT_QUIT );
		ADD( TR( "NO", "НЕТ" ), "", ACT_NO, 0 );
		break;
	case PAGE_CONFIRM_MENU:
		Q_strncpy( m_szHeader, TR( "LEAVE FOR THE MAIN MENU?", "ВЫЙТИ В ГЛАВНОЕ МЕНЮ?" ), sizeof( m_szHeader ) );
		ADD( TR( "YES", "ДА" ), TR( "Unsaved progress will be lost.", "Несохранённый прогресс будет потерян." ), ACT_YES, ACT_MAINMENU );
		ADD( TR( "NO", "НЕТ" ), "", ACT_NO, 0 );
		break;
	case PAGE_CONFIRM_NEW:
		Q_strncpy( m_szHeader, TR( "START A NEW GAME?", "НАЧАТЬ НОВУЮ ИГРУ?" ), sizeof( m_szHeader ) );
		ADD( TR( "YES", "ДА" ), TR( "Unsaved progress will be lost.", "Несохранённый прогресс будет потерян." ), ACT_YES, ACT_NEWGAME );
		ADD( TR( "NO", "НЕТ" ), "", ACT_NO, 0 );
		break;
	}
#undef ADD
}

void CRPMMainMenu::GoPage( int page )
{
	m_iPage = page;
	BuildPage( page );
	m_flPageTime = Now();
	m_iHover = m_bKeyNav ? 0 : -1;
}

void CRPMMainMenu::Command( const char *cmd )
{
	if ( cl_rpm_mainmenu_debug.GetBool() )
		Msg( "[mainmenu] %s\n", cmd );
	engine->ClientCmd_Unrestricted( cmd );
}

void CRPMMainMenu::Back()
{
	if ( m_iPage == PAGE_ROOT )
		return;
	surface()->PlaySound( "ui/buttonclickrelease.wav" );
	GoPage( PAGE_ROOT );
}

void CRPMMainMenu::Activate( int index )
{
	if ( index < 0 || index >= m_Items.Count() || m_eState == ST_LOADING )
		return;
	const MenuItem_t it = m_Items[index];
	surface()->PlaySound( "ui/buttonclickrelease.wav" );
	switch ( it.action )
	{
	case ACT_CONTINUE:
		if ( m_szNewestSave[0] )
			Command( CFmtStr( "progress_enable\nload %s\n", m_szNewestSave ) );
		break;
	case ACT_RESUME:
		Command( "gamemenucommand ResumeGame\n" );
		break;
	case ACT_NEWGAME:
		GoPage( m_eState == ST_PAUSE ? PAGE_CONFIRM_NEW : PAGE_NEWGAME );
		break;
	case ACT_LOAD:
		Command( "gamemenucommand OpenLoadGameDialog\n" );
		break;
	case ACT_SAVE:
		Command( "gamemenucommand OpenSaveGameDialog\n" );
		break;
	case ACT_OPTIONS:
		Command( "gamemenucommand OpenOptionsDialog\n" );
		break;
	case ACT_MAINMENU:
		GoPage( PAGE_CONFIRM_MENU );
		break;
	case ACT_QUIT:
		GoPage( PAGE_CONFIRM_QUIT );
		break;
	case ACT_SKILL:
		// as GameUI's new game dialog: the map of the first chapter (the CK writes cfg/chapter1.cfg)
		Command( CFmtStr( "disconnect\ndeathmatch 0\nskill %d\nprogress_enable\nexec chapter1\n", it.arg ) );
		GoPage( PAGE_ROOT );
		break;
	case ACT_BACK:
	case ACT_NO:
		GoPage( PAGE_ROOT );
		break;
	case ACT_YES:
		if ( it.arg == ACT_QUIT )
			Command( "quit\n" );
		else if ( it.arg == ACT_MAINMENU )
		{
			Command( "disconnect\n" );
			GoPage( PAGE_ROOT );
		}
		else if ( it.arg == ACT_NEWGAME )
			GoPage( PAGE_NEWGAME );
		break;
	}
}

//-----------------------------------------------------------------------------
// the newest save: "continue"
//-----------------------------------------------------------------------------
void CRPMMainMenu::FindNewestSave()
{
	m_szNewestSave[0] = m_szNewestHint[0] = 0;
	long newest = 0;
	FileFindHandle_t h;
	for ( const char *f = g_pFullFileSystem->FindFirstEx( "save/*.sav", "MOD", &h ); f; f = g_pFullFileSystem->FindNext( h ) )
	{
		long t = g_pFullFileSystem->GetFileTime( CFmtStr( "save/%s", f ), "MOD" );
		if ( t > newest )
		{
			newest = t;
			Q_StripExtension( f, m_szNewestSave, sizeof( m_szNewestSave ) );
		}
	}
	g_pFullFileSystem->FindClose( h );
	if ( m_szNewestSave[0] )
	{
		time_t tt = (time_t)newest;
		struct tm *lt = localtime( &tt );
		char when[64] = "";
		if ( lt )
			strftime( when, sizeof( when ), "%d.%m.%Y  %H:%M", lt );
		Q_snprintf( m_szNewestHint, sizeof( m_szNewestHint ), "%s  %s  -  %s", TR( "The last save:", "Последнее сохранение:" ), m_szNewestSave, when );
	}
}

//-----------------------------------------------------------------------------
// GameUI's own menu and title stay hidden while ours is on
//-----------------------------------------------------------------------------
static VPANEL FindChild( VPANEL parent, const char *name )
{
	if ( !parent )
		return 0;
	for ( int i = 0; i < ipanel()->GetChildCount( parent ); i++ )
	{
		VPANEL c = ipanel()->GetChild( parent, i );
		if ( !Q_stricmp( ipanel()->GetName( c ), name ) )
			return c;
	}
	return 0;
}

void CRPMMainMenu::HideStockPanels( bool bHide )
{
	VPANEL base = FindChild( GetVParent(), "BaseGameUIPanel" );
	if ( !base )
		return;
	static const char *s_pszNames[] = { "GameMenu", "GameMenuButton", "GameMenuButton2", "GameLogo" };
	for ( int i = 0; i < ARRAYSIZE( s_pszNames ); i++ )
	{
		VPANEL p = FindChild( base, s_pszNames[i] );
		if ( p && ipanel()->IsVisible( p ) == bHide )
			ipanel()->SetVisible( p, !bHide );
		// the title "buttons" (the game's title in ClientTitleFont) come back on every activation: no text
		if ( p && bHide && !m_bStockHidden && i > 0 )
			ivgui()->PostMessage( p, new KeyValues( "SetText", "text", "" ), 0 );
	}
	m_bStockHidden = bHide;
}

void CRPMMainMenu::RestoreStockMenu()
{
	if ( m_bStockHidden )
		HideStockPanels( false );
}

// a dialog of GameUI (options, load, save, a message box) or the console is up: our menu steps back
void CRPMMainMenu::UpdateDialogs()
{
	m_bDialogOpen = false;
	VPANEL root = GetVParent();
	VPANEL base = FindChild( root, "BaseGameUIPanel" );
	VPANEL lists[2] = { root, base };
	for ( int l = 0; l < 2 && !m_bDialogOpen; l++ )
	{
		if ( !lists[l] )
			continue;
		for ( int i = 0; i < ipanel()->GetChildCount( lists[l] ); i++ )
		{
			VPANEL c = ipanel()->GetChild( lists[l], i );
			if ( c == GetVPanel() || !ipanel()->IsVisible( c ) || !ipanel()->IsPopup( c ) )
				continue;
			const char *name = ipanel()->GetName( c );
			if ( !Q_stricmp( name, "GameMenu" ) || !Q_stricmp( name, "LoadingDialog" ) )
				continue;
			m_bDialogOpen = true;
			if ( cl_rpm_mainmenu_debug.GetInt() > 1 )
				Msg( "[mainmenu] dialog up: %s\n", name );
			break;
		}
	}
	if ( engine->Con_IsVisible() )
		m_bDialogOpen = true;
}

//-----------------------------------------------------------------------------
// keys: up / down, enter, backspace (escape belongs to the engine: it closes the pause menu)
//-----------------------------------------------------------------------------
void CRPMMainMenu::UpdateKeys()
{
	static const ButtonCode_t s_Keys[6] = { KEY_UP, KEY_DOWN, KEY_ENTER, KEY_PAD_ENTER, KEY_BACKSPACE, KEY_SPACE };
	bool down[6];
	for ( int i = 0; i < 6; i++ )
		down[i] = inputsystem->IsButtonDown( s_Keys[i] );
	const bool bActive = !m_bDialogOpen && m_eState != ST_LOADING && m_Items.Count();
	if ( bActive )
	{
		const int n = m_Items.Count();
		bool step = false;
		int dir = 0;
		if ( down[0] && !m_bKeyDown[0] ) { dir = -1; step = true; m_flKeyRepeat = Now() + 0.4f; }
		else if ( down[1] && !m_bKeyDown[1] ) { dir = 1; step = true; m_flKeyRepeat = Now() + 0.4f; }
		else if ( ( down[0] || down[1] ) && Now() > m_flKeyRepeat ) { dir = down[0] ? -1 : 1; step = true; m_flKeyRepeat = Now() + 0.11f; }
		if ( step )
		{
			m_bKeyNav = true;
			m_iHover = m_iHover < 0 ? ( dir > 0 ? 0 : n - 1 ) : ( m_iHover + dir + n ) % n;
			surface()->PlaySound( "ui/buttonrollover.wav" );
		}
		if ( ( ( down[2] && !m_bKeyDown[2] ) || ( down[3] && !m_bKeyDown[3] ) || ( down[5] && !m_bKeyDown[5] ) ) && m_iHover >= 0 )
			Activate( m_iHover );
		else if ( down[4] && !m_bKeyDown[4] )
			Back();
	}
	memcpy( m_bKeyDown, down, sizeof( down ) );
}

//-----------------------------------------------------------------------------
// the music of the main menu: plays while the main menu is up, fades out when a game loads
//-----------------------------------------------------------------------------
void CRPMMainMenu::UpdateMusic( float dt )
{
	const bool bWant = snd_rpm_menu_music.GetBool() && m_eState == ST_MAIN && IsVisible();
	const float target = bWant ? 1.0f : 0.0f;
	const float before = m_flMusicVol;
	m_flMusicVol = MenuApproach( m_flMusicVol, target, dt / ( bWant ? 2.5f : 1.2f ) );
	const float vol = m_flMusicVol * snd_rpm_menu_music_volume.GetFloat();

	if ( bWant && !m_bMusicOn )
	{
		if ( !g_pFullFileSystem->FileExists( "sound/" MENU_MUSIC, "GAME" ) )
			return;
		enginesound->PrecacheSound( "#" MENU_MUSIC, true );
		m_flMusicLength = enginesound->GetSoundDuration( MENU_MUSIC );
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, MAX( vol, 0.01f ), PITCH_NORM, 0 );
		m_flMusicStarted = Now();
		m_bMusicOn = true;
		return;
	}
	if ( !m_bMusicOn )
		return;
	if ( m_flMusicVol <= 0.0f && !bWant )
	{
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, 0.0f, PITCH_NORM, SND_STOP );
		m_bMusicOn = false;
		return;
	}
	// the track ended: once more (it is written to loop)
	if ( m_flMusicLength > 1.0f && Now() - m_flMusicStarted > m_flMusicLength + 0.05f )
	{
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, 0.0f, PITCH_NORM, SND_STOP );
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, MAX( vol, 0.01f ), PITCH_NORM, 0 );
		m_flMusicStarted = Now();
		return;
	}
	if ( fabs( before - m_flMusicVol ) > 0.0001f )
		enginesound->EmitAmbientSound( "#" MENU_MUSIC, MAX( vol, 0.001f ), PITCH_NORM, SND_CHANGE_VOL );
}

//-----------------------------------------------------------------------------
// GameUI's loading dialog: its progress bar is read (the bar of vgui_controls of the same
// SDK) and the dialog is hidden - our loading screen draws the bar
//-----------------------------------------------------------------------------
float CRPMMainMenu::LoadingProgress( bool bHideDialog )
{
	VPANEL base = FindChild( GetVParent(), "BaseGameUIPanel" );
	VPANEL dlg = FindChild( base, "LoadingDialog" );
	if ( !dlg )
		dlg = FindChild( GetVParent(), "LoadingDialog" );
	if ( !dlg )
		return -1.0f;
	float progress = -1.0f;
	VPANEL bar = FindChild( dlg, "Progress" );
	Panel *pBar = bar ? ipanel()->GetPanel( bar, "GameUI" ) : NULL;
	if ( pBar )
	{
		KeyValues *kv = new KeyValues( "settings" );
		pBar->GetSettings( kv );
		progress = kv->GetFloat( "progress", -1.0f );
		kv->deleteThis();
		if ( progress < 0.0f || progress > 1.0f )
			progress = -1.0f;
	}
	if ( bHideDialog && progress >= 0.0f && ipanel()->IsVisible( dlg ) )
		ipanel()->SetVisible( dlg, false );
	return progress;
}

//-----------------------------------------------------------------------------
void CRPMMainMenu::OnTick()
{
	const float now = Now();
	float dt = m_flLastThink > 0.0f ? now - m_flLastThink : 0.0f;
	dt = clamp( dt, 0.0f, 0.1f );
	m_flLastThink = now;

	int sw, sh;
	surface()->GetScreenSize( sw, sh );
	SetBounds( 0, 0, sw, sh );

	if ( !cl_rpm_mainmenu.GetBool() )
	{
		RestoreStockMenu();
		if ( IsVisible() )
			SetVisible( false );
		m_eState = ST_HIDDEN;
		UpdateMusic( dt );
		return;
	}

	const bool bUI = enginevgui->IsGameUIVisible();
	State_t st = ST_HIDDEN;
	if ( bUI )
	{
		if ( engine->IsDrawingLoadingImage() || ( engine->IsConnected() && !engine->IsInGame() ) )
			st = ST_LOADING;
		else if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() )
			st = ST_PAUSE;
		else
			st = ST_MAIN;
	}
	if ( bUI )
		HideStockPanels( true );

	const bool bShow = st != ST_HIDDEN;
	if ( bShow != IsVisible() )
		SetVisible( bShow );
	// GameUI moves the menu override to the back when it "hides" its menu: in front of its title again
	if ( bShow )
	{
		VPANEL root = GetVParent();
		const int n = ipanel()->GetChildCount( root );
		if ( n && ipanel()->GetChild( root, n - 1 ) != GetVPanel() )
			ipanel()->MoveToFront( GetVPanel() );
	}
	if ( st != m_eState )
	{
		const State_t old = m_eState;
		m_eState = st;
		if ( cl_rpm_mainmenu_debug.GetBool() )
			Msg( "[mainmenu] state %d -> %d\n", old, st );
		if ( st == ST_MAIN || st == ST_PAUSE )
		{
			if ( old == ST_HIDDEN || old == ST_LOADING || ( old == ST_PAUSE ) != ( st == ST_PAUSE ) )
				m_flShowTime = now;
			FindNewestSave();
			m_bKeyNav = false;
			GoPage( PAGE_ROOT );
		}
		if ( st == ST_LOADING )
		{
			m_flLoadShown = now;
			m_flLoadProgress = 0.0f;
			m_iTip = RandomInt( 0, 1000 );
		}
	}
	// the language may change in the options: the page follows
	static bool s_bRussian = IsRussian();
	if ( s_bRussian != IsRussian() )
	{
		s_bRussian = IsRussian();
		FindNewestSave();
		BuildPage( m_iPage );
	}

	if ( bShow )
	{
		UpdateDialogs();
		UpdateKeys();
		if ( st == ST_LOADING )
		{
			float p = LoadingProgress( cl_rpm_mainmenu_loading.GetBool() );
			if ( p >= 0.0f )
				m_flLoadProgress = MAX( m_flLoadProgress, p );
		}
		// parallax follows the mouse, slowly
		const float tx = sw > 0 ? ( m_iMouseX / (float)sw - 0.5f ) : 0.0f;
		const float ty = sh > 0 ? ( m_iMouseY / (float)sh - 0.5f ) : 0.0f;
		const float k = 1.0f - expf( -dt * 2.2f );
		m_flParX += ( tx - m_flParX ) * k;
		m_flParY += ( ty - m_flParY ) * k;
		for ( int i = 0; i < m_Items.Count(); i++ )
			m_Items[i].hover = MenuApproach( m_Items[i].hover, i == m_iHover && !m_bDialogOpen ? 1.0f : 0.0f, dt * ( i == m_iHover ? 9.0f : 5.0f ) );
		for ( int i = 0; i < NUM_MOTES; i++ )
		{
			Mote_t &m = m_Motes[i];
			m.y -= m.speed * dt * m.depth;
			m.x += ( m.drift + 0.003f * sinf( now * 0.4f + m.phase ) ) * dt;
			if ( m.y < -0.05f ) { m.y = 1.05f; m.x = RandomFloat( 0, 1 ); }
			if ( m.x < -0.05f ) m.x = 1.05f;
			if ( m.x > 1.05f ) m.x = -0.05f;
		}
		if ( now > m_flNextFlicker )
		{
			m_flFlickerEnd = now + RandomFloat( 0.12f, 0.35f );
			m_flNextFlicker = now + RandomFloat( 7.0f, 16.0f );
		}
	}
	UpdateMusic( dt );
	m_bWasVisible = bShow;
}

int CRPMMainMenu::ItemAt( int x, int y )
{
	const int sw = GetWide(), sh = GetTall();
	const int barX = (int)( sw * 0.078f ), barW = (int)( sw * 0.37f );
	const float top = sh * 0.374f, pitch = sh * 0.0575f, barH = sh * 0.055f;
	if ( x < barX || x >= barX + barW || m_eState == ST_LOADING || m_bDialogOpen )
		return -1;
	for ( int i = 0; i < m_Items.Count(); i++ )
	{
		const float y0 = top + i * pitch;
		if ( y >= y0 && y < y0 + barH )
			return i;
	}
	return -1;
}

void CRPMMainMenu::OnCursorMoved( int x, int y )
{
	const bool bMoved = abs( x - m_iMouseX ) + abs( y - m_iMouseY ) > 2;
	m_iMouseX = x;
	m_iMouseY = y;
	if ( m_bKeyNav && !bMoved )
		return;
	m_bKeyNav = false;
	const int hover = ItemAt( x, y );
	if ( hover != m_iHover && hover >= 0 )
		surface()->PlaySound( "ui/buttonrollover.wav" );
	m_iHover = hover;
}

void CRPMMainMenu::OnMousePressed( MouseCode code )
{
	if ( m_bDialogOpen || m_eState == ST_LOADING )
		return;
	if ( code == MOUSE_LEFT )
	{
		// (a click without a move before it - the item under the cursor now)
		int x, y;
		input()->GetCursorPosition( x, y );
		ScreenToLocal( x, y );
		const int at = ItemAt( x, y );
		if ( at >= 0 )
		{
			m_iHover = at;
			Activate( at );
		}
	}
	else if ( code == MOUSE_RIGHT )
		Back();
}

//-----------------------------------------------------------------------------
// painting
//-----------------------------------------------------------------------------
void CRPMMainMenu::DrawWall( int w, int h, float t )
{
	// the wall: "cover" the screen with the 16:9 picture, a little bigger for the parallax
	const float aspect = 16.0f / 9.0f;
	float bw = w * 1.03f, bh = bw / aspect;
	if ( bh < h * 1.03f ) { bh = h * 1.03f; bw = bh * aspect; }
	float bx = ( w - bw ) * 0.5f - m_flParX * w * 0.006f, by = ( h - bh ) * 0.5f - m_flParY * h * 0.006f;
	surface()->DrawSetColor( 255, 255, 255, 255 );
	surface()->DrawSetTexture( Tex( m_iTexBg, "vgui/hl2rpm/menu/bg" ) );
	surface()->DrawTexturedRect( (int)bx, (int)by, (int)( bx + bw ), (int)( by + bh ) );

	// the lines on the wall
	const int lw = MAX( 1, h / 900 );
	const float gx = -m_flParX * w * 0.004f, gy = -m_flParY * h * 0.004f;
	for ( int i = 0; i < ARRAYSIZE( s_GridLines ); i++ )
	{
		const GridLine_t &g = s_GridLines[i];
		surface()->DrawSetColor( 255, 255, 255, g.alpha );
		int x0 = (int)( g.x0 * w + gx ), y0 = (int)( g.y0 * h + gy ), x1 = (int)( g.x1 * w + gx ), y1 = (int)( g.y1 * h + gy );
		if ( x0 == x1 )
			surface()->DrawFilledRect( x0, y0, x0 + lw, y1 );
		else
			surface()->DrawFilledRect( x0, y0, x1, y0 + lw );
	}
	const int cs = (int)( h * 0.011f );
	for ( int i = 0; i < ARRAYSIZE( s_GridCrosses ); i++ )
	{
		int cx = (int)( s_GridCrosses[i][0] * w + gx ), cy = (int)( s_GridCrosses[i][1] * h + gy );
		surface()->DrawSetColor( 210, 210, 210, 70 );
		surface()->DrawFilledRect( cx - cs, cy, cx + cs + lw, cy + lw );
		surface()->DrawFilledRect( cx, cy - cs, cx + lw, cy + cs + lw );
	}

	// the emblem: breathes, flickers now and then like a bad lamp on it
	const float es = h * 0.86f;
	const float ecx = w - h * ( aspect * 0.275f ) - m_flParX * w * 0.014f, ecy = h * 0.47f - m_flParY * h * 0.014f;
	float breathe = 0.5f + 0.5f * sinf( t * 0.55f );
	float flick = 1.0f;
	if ( t < m_flFlickerEnd )
		flick = 0.82f + 0.18f * ( sinf( t * 90.0f ) > 0.0f ? 1.0f : 0.0f );
	const float fade = EaseOut( ( t - m_flShowTime ) / 1.6f );
	const int ex0 = (int)( ecx - es * 0.5f ), ey0 = (int)( ecy - es * 0.47f );
	// (the sprayed graffiti of the user: no glow around it - an additive halo read as a stray ring)
	surface()->DrawSetColor( 255, 255, 255, (int)( ( 222 + 18 * breathe ) * flick * fade ) );
	surface()->DrawSetTexture( Tex( m_iTexEmblem, "vgui/hl2rpm/menu/emblem" ) );
	surface()->DrawTexturedRect( ex0, ey0, (int)( ex0 + es ), (int)( ey0 + es ) );

	// dust in the air, in front of the wall
	surface()->DrawSetTexture( Tex( m_iTexMote, "vgui/hl2rpm/menu/mote" ) );
	for ( int i = 0; i < NUM_MOTES; i++ )
	{
		const Mote_t &m = m_Motes[i];
		const float px = m.x * w - m_flParX * w * 0.03f * m.depth, py = m.y * h - m_flParY * h * 0.03f * m.depth;
		const float r = h * 0.0022f * m.size * ( 0.6f + m.depth );
		const float tw = 0.55f + 0.45f * sinf( t * 0.7f + m.phase * 3.0f );
		int a = (int)( ( m.size > 3.0f ? 18 : 55 ) * m.depth * tw * fade );
		surface()->DrawSetColor( 255, 250, 240, a );
		surface()->DrawTexturedRect( (int)( px - r ), (int)( py - r ), (int)( px + r + 1 ), (int)( py + r + 1 ) );
	}
}

void CRPMMainMenu::DrawPause( int w, int h )
{
	// the game stays behind: darkened, darker on the left under the menu
	const float fade = EaseOut( ( Now() - m_flShowTime ) / 0.35f );
	surface()->DrawSetColor( 0, 0, 0, (int)( 178 * fade ) );		// (linear blending: needs more than it seems)
	surface()->DrawFilledRect( 0, 0, w, h );
	surface()->DrawSetColor( 0, 0, 0, 255 );
	surface()->DrawFilledRectFade( 0, 0, (int)( w * 0.66f ), h, (unsigned)( 235 * fade ), 0, true );
	// a line along the column, as on the wall
	surface()->DrawSetColor( 255, 255, 255, (int)( 22 * fade ) );
	surface()->DrawFilledRect( (int)( w * 0.057f ), (int)( h * 0.035f ), (int)( w * 0.057f ) + MAX( 1, h / 900 ), (int)( h * 0.965f ) );
}

void CRPMMainMenu::DrawItems( int w, int h, float t )
{
	const float show = EaseOut( ( t - m_flShowTime ) / 0.45f );
	const float dim = m_bDialogOpen ? 0.2f : 1.0f;		// (blending is linear: a low alpha still reads bright)

	// the title
	const int tw = (int)( w * 0.50f ), th = tw / 4;
	const int tx = (int)( w * 0.045f - m_flParX * w * 0.003f ), ty = (int)( h * 0.13f - m_flParY * h * 0.003f ) - (int)( ( 1.0f - show ) * h * 0.01f );
	surface()->DrawSetColor( 255, 255, 255, (int)( 255 * show * ( m_bDialogOpen ? 0.4f : 1.0f ) ) );
	surface()->DrawSetTexture( Tex( m_iTexTitle, "vgui/hl2rpm/menu/title" ) );
	surface()->DrawTexturedRect( tx, ty, tx + tw, ty + th );

	const int barX = (int)( w * 0.078f ), barW = (int)( w * 0.37f );
	const float top = h * 0.374f, pitch = h * 0.0575f, barH = h * 0.055f;
	const int textX = barX + (int)( w * 0.0145f );
	const int fontTall = surface()->GetFontTall( m_hFontItem );

	// the header of a nested page
	if ( m_szHeader[0] )
	{
		const float a = EaseOut( ( t - m_flPageTime ) / 0.3f ) * show * dim;
		const int hy = (int)( top - h * 0.052f );
		DrawText( m_hFontHeader, textX, hy, Color( 150, 150, 150, (int)( 255 * a ) ), m_szHeader, (int)( h * 0.0035f ) );
	}

	for ( int i = 0; i < m_Items.Count(); i++ )
	{
		const MenuItem_t &it = m_Items[i];
		const float in = EaseOut( ( t - m_flPageTime - i * 0.045f ) / 0.32f );
		const float a = in * show * dim;
		if ( a <= 0.0f )
			continue;
		const int y0 = (int)( top + i * pitch );
		const int slide = (int)( ( 1.0f - in ) * w * -0.018f );
		const float hv = EaseOut( it.hover );
		if ( hv > 0.001f )
		{
			// the bar grows from the left, a lighter edge at its left end
			const int bw = (int)( barW * ( 0.35f + 0.65f * hv ) );
			surface()->DrawSetColor( 92, 92, 92, (int)( 205 * hv * a ) );
			surface()->DrawFilledRect( barX + slide, y0, barX + slide + bw, y0 + (int)barH );
			surface()->DrawSetColor( 255, 255, 255, (int)( 18 * hv * a ) );
			surface()->DrawFilledRectFade( barX + slide, y0, barX + slide + bw, y0 + (int)barH, 255, 0, true );
			surface()->DrawSetColor( 230, 230, 230, (int)( 200 * hv * a ) );
			surface()->DrawFilledRect( barX + slide, y0, barX + slide + MAX( 2, h / 450 ), y0 + (int)barH );
		}
		const int c = (int)( 196 + 59 * hv );
		const int ty0 = y0 + (int)( ( barH - fontTall ) * 0.5f );
		DrawText( m_hFontItem, textX + slide + (int)( hv * w * 0.003f ), ty0, Color( c, c, c, (int)( 255 * a ) ), it.label );
	}
}

void CRPMMainMenu::DrawFooter( int w, int h, float t )
{
	const float show = EaseOut( ( t - m_flShowTime - 0.3f ) / 0.6f ) * ( m_bDialogOpen ? 0.2f : 1.0f );
	const int is = (int)( h * 0.034f );
	const int ix = (int)( w * 0.083f ), iy = (int)( MAX( h * 0.757f, h * ( 0.374f + 0.0575f * m_Items.Count() + 0.03f ) ) ) - is / 2;
	surface()->DrawSetColor( 200, 200, 200, (int)( 210 * show ) );
	surface()->DrawSetTexture( Tex( m_iTexIcon, "vgui/hl2rpm/menu/icon" ) );
	surface()->DrawTexturedRect( ix, iy, ix + is, iy + is );
	const char *hint = ( m_iHover >= 0 && m_iHover < m_Items.Count() && m_Items[m_iHover].hint[0] ) ? m_Items[m_iHover].hint : "Half-Life 2: Role-Playing Mod";
	const int th = surface()->GetFontTall( m_hFontSmall );
	DrawText( m_hFontSmall, ix + is + (int)( w * 0.009f ), iy + ( is - th ) / 2, Color( 175, 175, 175, (int)( 255 * show ) ), hint );
}

void CRPMMainMenu::DrawLoading( int w, int h, float t )
{
	const float a = EaseOut( ( t - m_flLoadShown ) / 0.4f );
	// the title stays, the menu gives way to the progress bar and a tip
	const int tw = (int)( w * 0.50f ), th = tw / 4;
	surface()->DrawSetColor( 255, 255, 255, 255 );
	surface()->DrawSetTexture( Tex( m_iTexTitle, "vgui/hl2rpm/menu/title" ) );
	surface()->DrawTexturedRect( (int)( w * 0.045f ), (int)( h * 0.13f ), (int)( w * 0.045f ) + tw, (int)( h * 0.13f ) + th );

	// the bar
	const int x = (int)( w * 0.092f ), y = (int)( h * 0.62f );
	const int by = y + surface()->GetFontTall( m_hFontLoading ) + (int)( h * 0.018f );
	const int bw = (int)( w * 0.36f ), bh = MAX( 3, (int)( h * 0.005f ) );
	surface()->DrawSetColor( 255, 255, 255, (int)( 28 * a ) );
	surface()->DrawFilledRect( x, by, x + bw, by + bh );
	const float p = MenuClamp01( m_flLoadProgress );
	surface()->DrawSetColor( 205, 205, 205, (int)( 230 * a ) );
	surface()->DrawFilledRect( x, by, x + (int)( bw * p ), by + bh );
	// a light running along the bar: something is happening even when the progress waits
	const float run = fmodf( t * 0.45f, 1.0f );
	const int rx = x + (int)( bw * run );
	surface()->DrawSetColor( 255, 255, 255, (int)( 60 * a ) );
	surface()->DrawFilledRectFade( MAX( x, rx - (int)( w * 0.04f ) ), by, rx, by + bh, 0, 255, true );

	// a tip about the game
	static const char *s_Tips[][2] =
	{
		{ "Hold R or press H to put your weapon away. Citizens talk more readily to an unarmed man.", "Удерживайте R или нажмите H, чтобы убрать оружие. С безоружным горожане говорят охотнее." },
		{ "Tab opens the character menu: skills, inventory, quests and notes.", "Tab открывает меню персонажа: навыки, инвентарь, задания и записки." },
		{ "A bobby pin and a steady hand open most locks. A broken pin is spent.", "Шпилька и твёрдая рука откроют большинство замков. Сломанная шпилька пропадает." },
		{ "Taking from a container that has an owner is theft, if someone sees it.", "Взять из чужого ящика - кража, если кто-то это увидит." },
		{ "Suits protect you and change the HUD. Without one you only feel your pulse.", "Костюм защищает и меняет интерфейс. Без него вы чувствуете лишь свой пульс." },
		{ "A hacked terminal can open doors, switch off force fields and turn turrets.", "Взломанный терминал откроет двери, отключит силовые поля и перенастроит турели." },
		{ "Speech checks in dialogs are shown in green when you can pass them.", "Проверки навыков в диалогах подсвечены зелёным, если вы их пройдёте." },
		{ "Sit on a bench or a bed with E: time to rest and look around.", "На скамейку или кровать можно сесть клавишей E: время отдохнуть и осмотреться." },
	};
	const int tip = m_iTip % ARRAYSIZE( s_Tips );
	const char *s = IsRussian() ? s_Tips[tip][1] : s_Tips[tip][0];
	DrawText( m_hFontSmall, x, by + (int)( h * 0.03f ), Color( 160, 160, 160, (int)( 255 * a ) ), s );
}

// bottom right: the red valve wheel turning (the user's reference: the wheel of Valve's logo) and
// LOADING - drawn last, over the vignette (its dark corner would hide them). The screen is repainted
// only when the engine reports progress: no fade-in (it would stay half way), and the frame of the
// wheel comes from the clock, not from a counter.
void CRPMMainMenu::DrawLoadingWheel( int w, int h, float t )
{
	const float a = 1.0f;
	// the red valve wheel turning (the user's reference: the wheel of Valve's logo)
	// and LOADING. The wheel is a sheet of 32 frames rendered in Blender (make_loading_valve.py):
	// a sixth of a turn - its six lobes make the loop seamless. The screen is repainted only
	// when the engine reports progress, so the frame comes from the clock, not from a counter.
	{
		const int vs = (int)( h * 0.1f );
		const int vx = w - (int)( h * 0.05f ) - vs, vy = h - (int)( h * 0.045f ) - vs;
		const float turns = t * cl_rpm_mainmenu_spinner_speed.GetFloat() * 6.0f;		// sixths of a turn
		const int frame = ( (int)( ( turns - floorf( turns ) ) * 32.0f ) ) & 31;
		const float u0 = ( frame % 8 ) / 8.0f, v0 = ( frame / 8 ) / 4.0f;
		surface()->DrawSetColor( 255, 255, 255, (int)( 255 * a ) );
		surface()->DrawSetTexture( Tex( m_iTexValve, "vgui/hl2rpm/menu/loading_valve" ) );
		surface()->DrawTexturedSubRect( vx, vy, vx + vs, vy + vs, u0, v0, u0 + 1.0f / 8.0f, v0 + 1.0f / 4.0f );

		// the word ends where the dots begin: the dots (0..3) do not move it
		char dots[4] = "...";
		dots[ ( (int)( t * 2.5f ) ) % 4 ] = 0;
		const char *word = TR( "LOADING", "ЗАГРУЗКА" );
		const int spacing = (int)( h * 0.004f );
		const int dotsW = TextWide( m_hFontLoading, "...", spacing );
		const int lw = TextWide( m_hFontLoading, word, spacing );
		const int lh = surface()->GetFontTall( m_hFontLoading );
		const int lx = vx - (int)( h * 0.02f ) - dotsW - lw, ly = vy + ( vs - lh ) / 2;
		DrawText( m_hFontLoading, lx, ly, Color( 215, 215, 215, (int)( 255 * a ) ), word, spacing );
		DrawText( m_hFontLoading, lx + lw + spacing, ly, Color( 215, 215, 215, (int)( 255 * a ) ), dots, spacing );
	}

}

void CRPMMainMenu::Paint()
{
	const int w = GetWide(), h = GetTall();
	const float t = Now();
	if ( m_eState == ST_HIDDEN )
		return;

	if ( m_eState == ST_PAUSE )
		DrawPause( w, h );
	else
		DrawWall( w, h, t );

	if ( m_eState == ST_LOADING )
		DrawLoading( w, h, t );
	else
	{
		DrawItems( w, h, t );
		DrawFooter( w, h, t );
	}

	// the dark corners and a film grain over everything
	if ( m_eState != ST_PAUSE )
	{
		surface()->DrawSetColor( 255, 255, 255, 255 );
		surface()->DrawSetTexture( Tex( m_iTexVignette, "vgui/hl2rpm/menu/vignette" ) );
		surface()->DrawTexturedRect( 0, 0, w, h );
	}
	const float gs = h / 300.0f;		// grain texels per screen pixel
	const float ox = RandomFloat( 0, 1 ), oy = RandomFloat( 0, 1 );
	surface()->DrawSetColor( 255, 255, 255, (int)( cl_rpm_mainmenu_grain.GetFloat() * ( m_eState == ST_PAUSE ? 0.65f : 1.0f ) ) );
	surface()->DrawSetTexture( Tex( m_iTexGrain, "vgui/hl2rpm/menu/grain" ) );
	surface()->DrawTexturedSubRect( 0, 0, w, h, ox, oy, ox + w / ( 256.0f * gs ), oy + h / ( 256.0f * gs ) );
	if ( m_eState == ST_LOADING )
		DrawLoadingWheel( w, h, t );
}

//-----------------------------------------------------------------------------
// created after GameUI (CHLClient::PostInit), removed before VGui_Shutdown
//-----------------------------------------------------------------------------
void RPMMainMenu_Create()
{
	if ( s_pMainMenu )
		return;
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	if ( !root )
		return;
	s_pMainMenu = new CRPMMainMenu( root );

	// GameUI's own way to replace its menu (TF2 does it): its menu items go, GameUI keeps our panel
	// visible and in front when the menu opens. Without it GameUI shows its CGameMenu again and again.
	CSysModule *pModule = g_pFullFileSystem->LoadModule( "GameUI", "EXECUTABLE_PATH", false );
	CreateInterfaceFn factory = pModule ? Sys_GetFactory( pModule ) : NULL;
	IGameUI *pGameUI = factory ? (IGameUI *)factory( GAMEUI_INTERFACE_VERSION, NULL ) : NULL;
	if ( pGameUI && cl_rpm_mainmenu.GetBool() && !CommandLine()->FindParm( "-stockmenu" ) )
	{
		pGameUI->SetMainMenuOverride( s_pMainMenu->GetVPanel() );
		s_pGameUI = pGameUI;
		DevMsg( "[mainmenu] GameUI menu replaced\n" );
	}
}

void RPMMainMenu_Destroy()
{
	if ( !s_pMainMenu )
		return;
	// GameUI outlives client.dll: it must forget our panel, or it calls into it at exit (EIP 0)
	if ( s_pGameUI )
	{
		s_pGameUI->SetMainMenuOverride( 0 );
		s_pGameUI = NULL;
	}
	s_pMainMenu->RestoreStockMenu();
	s_pMainMenu->SetParent( (VPANEL)NULL );
	delete s_pMainMenu;
	s_pMainMenu = NULL;
}

CON_COMMAND( cl_rpm_mainmenu_dump, "HL2RPM: the panels of GameUI's root (names, popup, visible)" )
{
	VPANEL root = enginevgui->GetPanel( PANEL_GAMEUIDLL );
	for ( int i = 0; i < ipanel()->GetChildCount( root ); i++ )
	{
		VPANEL c = ipanel()->GetChild( root, i );
		Msg( "%s popup %d visible %d\n", ipanel()->GetName( c ), ipanel()->IsPopup( c ), ipanel()->IsVisible( c ) );
		for ( int j = 0; j < ipanel()->GetChildCount( c ); j++ )
		{
			VPANEL d = ipanel()->GetChild( c, j );
			Msg( "    %s popup %d visible %d\n", ipanel()->GetName( d ), ipanel()->IsPopup( d ), ipanel()->IsVisible( d ) );
		}
	}
}

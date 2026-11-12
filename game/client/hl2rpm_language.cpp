//========= HL2RPM ============================================================//
//
// The language of the game, from the settings ("Sound and interface"), whatever Steam says:
//
//   cl_rpm_language 0 - as in Steam, 1 - Russian, 2 - English
//
// The RPG (items, skills, windows, dialogs, quests, NPC names) asks RPMItems::Language(),
// which reads dialog_language: it changes at once. The text of the engine's own windows
// comes from localization files chosen by the Steam language at the start (the engine
// writes it into the registry every time, %language% of ILocalize reads it there), and a
// Label keeps the text it was given. So:
//
//   - the tokens of the chosen language are loaded over the others on a change: everything
//     that asks for a token afterwards gets the new text;
//   - at the start they are loaded in CHLClient::Init - config.cfg hasn't run by then, the
//     value is read from it;
//   - GameUI builds its main menu before the client is up, once: its items are told their
//     tokens again (the "SetText" message of a Label, the tokens come from gamemenu.res).
//     Stock windows made on demand (Options, New Game) take the tokens of the moment; one
//     that is already open keeps its text until the next start.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_items.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "tier1/fmtstr.h"
#include "ienginevgui.h"
#include "KeyValues.h"
#include <vgui/ILocalize.h>
#include <vgui/IPanel.h>
#include <vgui/IVGui.h>
#include <vgui_controls/Controls.h>
#include "tier3/tier3.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static const char *LanguageOfSetting( int i )
{
	switch ( i )
	{
	case 1: return "russian";
	case 2: return "english";
	}
	return "";
}

static void LoadTokens( const char *lang )
{
	static const char *s_pszFiles[] = { "platform", "vgui", "valve", "gameui", "chat", "hl2", "mapbase", "hl2rpm" };
	for ( int i = 0; i < ARRAYSIZE( s_pszFiles ); i++ )
	{
		CFmtStr file( "resource/%s_%s.txt", s_pszFiles[i], lang );
		const bool bExists = g_pFullFileSystem->FileExists( file );		// (platform_ and vgui_ are off the GAME path)
		if ( bExists )
			g_pVGuiLocalize->AddFile( file, NULL, true );
		DevMsg( "[language] %s: %s\n", file.Access(), bExists ? "loaded" : "no file" );
	}
}

static vgui::VPANEL FindPanel( vgui::VPANEL parent, const char *pszName )
{
	const int n = vgui::ipanel()->GetChildCount( parent );
	for ( int i = 0; i < n; i++ )
	{
		vgui::VPANEL child = vgui::ipanel()->GetChild( parent, i );
		if ( !Q_stricmp( vgui::ipanel()->GetName( child ), pszName ) )
			return child;
		vgui::VPANEL found = FindPanel( child, pszName );
		if ( found )
			return found;
	}
	return 0;
}

// the items of the main menu are named after their labels ("name" of gamemenu.res, if an item has one)
static void RefreshGameMenu()
{
	vgui::VPANEL menu = FindPanel( enginevgui->GetPanel( PANEL_GAMEUIDLL ), "GameMenu" );
	DevMsg( "[language] the main menu: %s\n", menu ? "found" : "not found" );
	if ( !menu )
		return;
	KeyValues *pKV = new KeyValues( "GameMenu" );
	if ( pKV->LoadFromFile( g_pFullFileSystem, "resource/gamemenu.res", "GAME" ) )
	{
		for ( KeyValues *pItem = pKV->GetFirstTrueSubKey(); pItem; pItem = pItem->GetNextTrueSubKey() )
		{
			const char *label = pItem->GetString( "label" );
			vgui::VPANEL item = label[0] ? FindPanel( menu, pItem->GetString( "name", label ) ) : 0;
			DevMsg( "[language]   %s %s: %s\n", pItem->GetName(), label, item ? "ok" : "no such item" );
			if ( item )
				vgui::ivgui()->PostMessage( item, new KeyValues( "SetText", "text", label ), 0 );
		}
	}
	pKV->deleteThis();
}

static void LanguageChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	ConVarRef setting( var->GetName() );
	const char *lang = LanguageOfSetting( setting.GetInt() );
	ConVarRef dialog_language( "dialog_language" );
	if ( dialog_language.IsValid() && Q_stricmp( dialog_language.GetString(), lang ) )
		dialog_language.SetValue( lang );
	LoadTokens( RPMItems::Language() );
	RefreshGameMenu();
	// the names of the NPCs of the map layer
	if ( engine->IsInGame() )
		engine->ClientCmd_Unrestricted( "rpm_world_reload\n" );
}

ConVar cl_rpm_language( "cl_rpm_language", "0", FCVAR_ARCHIVE, "HL2RPM: the language of the game: 0 as in Steam, 1 Russian, 2 English", LanguageChanged );

// CHLClient::Init
void RPMLanguage_LoadAtStart()
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !g_pFullFileSystem->ReadFile( "cfg/config.cfg", "MOD", buf ) )
		return;
	buf.PutChar( 0 );
	const char *p = Q_stristr( (const char *)buf.Base(), "cl_rpm_language" );
	if ( !p )
		return;
	p += Q_strlen( "cl_rpm_language" );
	while ( *p == ' ' || *p == '\t' || *p == '"' )
		p++;
	const char *lang = LanguageOfSetting( atoi( p ) );
	DevMsg( "[language] at the start: %s\n", lang );
	if ( lang[0] )
	{
		LoadTokens( lang );
		RefreshGameMenu();
	}
}

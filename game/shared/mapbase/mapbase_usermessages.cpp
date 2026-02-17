//========= Mapbase - https://github.com/mapbase-source/source-sdk-2013 ============//
//
// Purpose: Mapbase-specific user messages.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "usermessages.h"
#ifdef CLIENT_DLL
#include "hud_macros.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
// Forward-declare message handler so HOOK_MESSAGE can reference it.
void __MsgFunc_InventoryPickup( bf_read &msg );

void HookMapbaseUserMessages( void )
{
	// VScript
	//HOOK_MESSAGE( ScriptMsg ); // Hooked in CNetMsgScriptHelper

	//HOOK_MESSAGE( ShowMenuComplex ); // Hooked in CHudMenu

	// Ensure we have a client-side hook for InventoryPickup to safely consume
	// the message if the server sends it but the HUD doesn't need to act.
	HOOK_MESSAGE( InventoryPickup );
}
#endif

#ifdef CLIENT_DLL
// No-op handler for InventoryPickup. Reads nothing and silently ignores the message.
void __MsgFunc_InventoryPickup( bf_read &msg )
{
    // intentionally empty
}
#endif

void RegisterMapbaseUserMessages( void )
{
	// VScript
	usermessages->Register( "ScriptMsg", -1 ); // CNetMsgScriptHelper

	usermessages->Register( "ShowMenuComplex", -1 ); // CHudMenu

	// Register inventory pickup message so clients know about it if server sends it
	// (fixes "Unregistered message 'InventoryPickup'" errors when picking up items)
	usermessages->Register( "InventoryPickup", -1 );
	if ( usermessages->LookupUserMessage( "Inventory_Update" ) == -1 )
		usermessages->Register( "Inventory_Update", -1 );
	if ( usermessages->LookupUserMessage( "Quests_Update" ) == -1 )
		usermessages->Register( "Quests_Update", -1 );

#ifdef CLIENT_DLL
	// TODO: Better placement?
	HookMapbaseUserMessages();
#endif
}

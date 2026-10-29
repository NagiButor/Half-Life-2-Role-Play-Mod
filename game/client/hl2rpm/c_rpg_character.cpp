//========= HL2RPM ============//
//
// RPG-система (клиент): зеркало персонажа, приём RPG_State/RPG_Notify, локализация RPG,
// текст уведомлений (лента HUD - rpg_hud.cpp, консоль), тема HUD по костюму.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_formulas.h"
#include "hl2rpm/rpg_quest_data.h"
#include "c_baseplayer.h"
#include "hud.h"
#include "usermessages.h"
#include "timecycle/env_timecycle.h"
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_rpg_notify( "cl_rpg_notify", "0", FCVAR_CLIENTDLL, "RPG: also show notifications as ui_toast (the HUD feed - hl2rpm/rpg_hud.cpp - shows them anyway)" );
ConVar cl_rpg_debug( "cl_rpg_debug", "0", FCVAR_CLIENTDLL, "RPG: print received RPG_State records" );

static C_RPGCharacter g_RPGClient;

C_RPGCharacter &RPGClient()
{
	return g_RPGClient;
}

// ---------------------------------------------------------------------------
// Локализация
// ---------------------------------------------------------------------------
const char *RPG_LocalizeUTF8( const char *pszToken, char *pszOut, int nOutSize, const char *pszFallback )
{
	pszOut[0] = '\0';
	if ( !pszToken || !pszToken[0] )
	{
		Q_strncpy( pszOut, pszFallback ? pszFallback : "", nOutSize );
		return pszOut;
	}
	const wchar_t *pw = ( pszToken[0] == '#' && g_pVGuiLocalize ) ? g_pVGuiLocalize->Find( pszToken + 1 ) : NULL;
	if ( pw )
		g_pVGuiLocalize->ConvertUnicodeToANSI( pw, pszOut, nOutSize );
	else if ( pszFallback )
		Q_strncpy( pszOut, pszFallback, nOutSize );
	else
		Q_strncpy( pszOut, pszToken[0] == '#' ? pszToken + 1 : pszToken, nOutSize );
	return pszOut;
}

// токен-формат с %s1 %s2 (UTF-8)
static void FormatToken( char *pszOut, int nOutSize, const char *pszToken, const char *pszFallbackFmt, const char *p1 = "", const char *p2 = "" )
{
	wchar_t wFmt[512], w1[256], w2[256], wOut[1024];
	const wchar_t *pFmt = ( g_pVGuiLocalize && pszToken && pszToken[0] == '#' ) ? g_pVGuiLocalize->Find( pszToken + 1 ) : NULL;
	if ( !pFmt )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( pszFallbackFmt ? pszFallbackFmt : "%s1", wFmt, sizeof( wFmt ) );
		pFmt = wFmt;
	}
	g_pVGuiLocalize->ConvertANSIToUnicode( p1 ? p1 : "", w1, sizeof( w1 ) );
	g_pVGuiLocalize->ConvertANSIToUnicode( p2 ? p2 : "", w2, sizeof( w2 ) );
	g_pVGuiLocalize->ConstructString( wOut, sizeof( wOut ), pFmt, 2, w1, w2 );
	g_pVGuiLocalize->ConvertUnicodeToANSI( wOut, pszOut, nOutSize );
}

static void ItemName( const char *pszClass, char *pszOut, int nOutSize )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
	RPG_LocalizeUTF8( pDef ? pDef->name.Get() : CFmtStr( "#RPG_Item_%s", pszClass ).Access(), pszOut, nOutSize, pszClass );
}

// ---------------------------------------------------------------------------
// Что клиент может проверить в условиях (для интерфейса: требования перков и т.п.)
// ---------------------------------------------------------------------------
class CRPGClientWorldQuery : public IRPGWorldQuery
{
public:
	virtual int CountItem( C_BasePlayer *pPlayer, const char *pszClassname ) { return -1; }
	virtual int GetQuestState( C_BasePlayer *pPlayer, const char *pszQuest, int &iStage )
	{
		// квесты v2 - во флагах персонажа (старые - только на сервере)
		if ( !RPGQuestData().Find( pszQuest ) )
			return -1;
		iStage = RPGQuest_GetStage( RPGClient().Data(), pszQuest );
		return RPGQuest_GetState( RPGClient().Data(), pszQuest );
	}
	virtual const char *GetNPCName( C_BaseEntity *pNPC ) { return pNPC ? pNPC->GetEntityName() : ""; }
	virtual int GetNPCStartDisposition( C_BaseEntity *pNPC ) { return 0; }
	virtual bool GetTimeOfDay( float &flHours, float &flSunrise, float &flSunset )
	{
		flSunrise = 6.0f;
		flSunset = 18.0f;
		CEnvTimecycle *pTC = GetTimecycle();
		if ( !pTC )
			return false;
		flHours = pTC->GetTimeOfDayHours();
		return true;
	}
	virtual int GetSuitType( C_BasePlayer *pPlayer ) { return RPGClient().GetSuitType(); }
};
static CRPGClientWorldQuery s_ClientWorldQuery;

RPGEvalContext_t RPG_MakeEvalContext( C_BasePlayer *pPlayer, C_BaseEntity *pNPC )
{
	RPGEvalContext_t ctx;
	ctx.pChar = &g_RPGClient.Data();
	ctx.pPlayer = pPlayer ? pPlayer : C_BasePlayer::GetLocalPlayer();
	ctx.pNPC = pNPC;
	return ctx;
}

// ===========================================================================
// C_RPGCharacter
// ===========================================================================
C_RPGCharacter::C_RPGCharacter()
{
	m_bHasState = false;
	m_bInFullUpdate = false;
}

void C_RPGCharacter::Reset()
{
	m_Data.Reset();
	m_bHasState = false;
}

int C_RPGCharacter::GetSuitType() const
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	return pPlayer ? pPlayer->m_Local.m_iSuitType : RPG_SUIT_CITIZEN;
}

void C_RPGCharacter::AddListener( IRPGClientListener *pListener )
{
	if ( pListener && m_Listeners.Find( pListener ) == m_Listeners.InvalidIndex() )
		m_Listeners.AddToTail( pListener );
}

void C_RPGCharacter::RemoveListener( IRPGClientListener *pListener )
{
	m_Listeners.FindAndRemove( pListener );
}

void C_RPGCharacter::OnStateMessage( bf_read &msg )
{
	char szKey[128], szValue[RPG_STRING_VALUE_MAX + 8];
	while ( msg.GetNumBytesLeft() > 0 )
	{
		RPGRecord_t rec;
		rec.type = msg.ReadByte();
		msg.ReadString( szKey, sizeof( szKey ) );
		rec.key = szKey;
		if ( RPGRecordHasString( rec.type ) )
		{
			msg.ReadString( szValue, sizeof( szValue ) );
			rec.sValue = szValue;
		}
		else
			rec.iValue = msg.ReadLong();

		if ( msg.IsOverflowed() )
			break;
		if ( cl_rpg_debug.GetBool() )
			Msg( "[rpg client] rec %d %s %d \"%s\"\n", rec.type, rec.key.Get(), rec.iValue, rec.sValue.Get() );

		if ( rec.type == RPG_REC_FULL_BEGIN )
		{
			// новое полное состояние: всё с нуля (данные - те же scripts/rpg, что у сервера)
			RPGData().EnsureLoaded();
			m_Data.Reset();
			m_bInFullUpdate = true;
			continue;
		}
		if ( rec.type == RPG_REC_FULL_END )
		{
			m_bInFullUpdate = false;
			m_bHasState = true;
			for ( int i = 0; i < m_Listeners.Count(); ++i )
				m_Listeners[i]->OnRPGFullUpdate();
			continue;
		}
		m_Data.ApplyRecord( rec );
		if ( !m_bInFullUpdate )
		{
			for ( int i = 0; i < m_Listeners.Count(); ++i )
				m_Listeners[i]->OnRPGStateChanged( rec.type, rec.key.Get() );
		}
	}
}

void C_RPGCharacter::OpenUI( const char *pszScreen, const char *pszParam, int iSource )
{
	for ( int i = 0; i < m_Listeners.Count(); ++i )
	{
		if ( m_Listeners[i]->OnRPGOpenUI( pszScreen, pszParam, iSource ) )
			return;
	}
	DefaultOpenUI( pszScreen, pszParam, iSource );
}

void C_RPGCharacter::OnNotifyMessage( bf_read &msg )
{
	char szToken[128], szExtra[160];
	const int iKind = msg.ReadByte();
	msg.ReadString( szToken, sizeof( szToken ) );
	const int iValue = msg.ReadLong();
	msg.ReadString( szExtra, sizeof( szExtra ) );

	if ( iKind == RPG_NOTIFY_OPEN_UI )
	{
		OpenUI( szToken, szExtra, iValue );
		return;
	}

	for ( int i = 0; i < m_Listeners.Count(); ++i )
		m_Listeners[i]->OnRPGNotify( iKind, szToken, iValue, szExtra );
	ShowTemporaryNotify( iKind, szToken, iValue, szExtra );
}

// ---------------------------------------------------------------------------
// Текст уведомлений: лента HUD (rpg_hud.cpp) и консоль; ui_toast - по cl_rpg_notify
// ---------------------------------------------------------------------------
static void Toast( const char *pszText )
{
	if ( !pszText || !pszText[0] )
		return;
	// ui_toast берёт текст в кавычках: кавычки и переводы строк убираем
	char szSafe[512];
	int j = 0;
	for ( int i = 0; pszText[i] && j < (int)sizeof( szSafe ) - 1; ++i )
	{
		char c = pszText[i];
		if ( c == '"' || c == '\n' || c == '\r' || c == ';' )
			c = ' ';
		szSafe[j++] = c;
	}
	szSafe[j] = '\0';
	engine->ClientCmd_Unrestricted( CFmtStr( "ui_toast \"rpg\" \"%s\" \"\"\n", szSafe ) );
}

void C_RPGCharacter::ShowTemporaryNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra )
{
	char szText[512];
	if ( !RPG_FormatNotify( iKind, pszToken, iValue, pszExtra, szText, sizeof( szText ) ) )
		return;
	Msg( "[rpg] %s\n", szText );
	if ( cl_rpg_notify.GetBool() )
		Toast( szText );
}

bool RPG_FormatNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra, char *szText, int nText )
{
	char sz1[256], sz2[256];
	szText[0] = '\0';
	switch ( iKind )
	{
	case RPG_NOTIFY_XP:
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_XP", "+%s1 XP", CFmtStr( "%d", iValue ) );
		break;
	case RPG_NOTIFY_LEVELUP:
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_LevelUp", "Level %s1", CFmtStr( "%d", iValue ) );
		break;
	case RPG_NOTIFY_REP:
	{
		const RPGFactionDef_t *pDef = RPGData().FindFaction( pszToken );
		RPG_LocalizeUTF8( pDef ? pDef->name.Get() : CFmtStr( "#RPG_Faction_%s", pszToken ).Access(), sz1, sizeof( sz1 ), pszToken );
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Rep", "%s1 %s2", sz1, CFmtStr( "%+d", iValue ) );
		break;
	}
	case RPG_NOTIFY_CIVIL:
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Civil", "Civil status %s1", CFmtStr( "%+d", iValue ) );
		if ( pszExtra && !Q_stricmp( pszExtra, "tier" ) )
		{
			RPG_LocalizeUTF8( CFmtStr( "#RPG_Civil_%s", pszToken ).Access(), sz1, sizeof( sz1 ), pszToken );
			Q_strncat( szText, CFmtStr( ": %s", sz1 ).Access(), nText );
		}
		break;
	case RPG_NOTIFY_SUSPICION:
		RPG_LocalizeUTF8( CFmtStr( "#HL2RPM_UI_Suspicion_%s", pszToken ).Access(), sz1, sizeof( sz1 ), pszToken );
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Suspicion", "Suspicion: %s1", sz1 );
		break;
	case RPG_NOTIFY_LOCATION:
	{
		const RPGLocationDef_t *pDef = RPGData().FindLocation( pszToken );
		RPG_LocalizeUTF8( pDef ? pDef->name.Get() : CFmtStr( "#RPG_Loc_%s", pszToken ).Access(), sz1, sizeof( sz1 ), pszToken );
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Location", "Location discovered: %s1", sz1 );
		break;
	}
	case RPG_NOTIFY_ITEM:
		ItemName( pszToken, sz1, sizeof( sz1 ) );
		FormatToken( szText, nText, iValue >= 0 ? "#HL2RPM_UI_Notify_ItemGot" : "#HL2RPM_UI_Notify_ItemLost",
			iValue >= 0 ? "Received: %s1 x%s2" : "Handed over: %s1 x%s2", sz1, CFmtStr( "%d", abs( iValue ) ) );
		break;
	case RPG_NOTIFY_NOTE:
		RPG_LocalizeUTF8( CFmtStr( "#NOTE_%s_TITLE", pszToken ).Access(), sz1, sizeof( sz1 ), pszToken );
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Note", "New note: %s1", sz1 );
		break;
	case RPG_NOTIFY_CREDITS:
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Credits", "%s1 cr.", CFmtStr( "%+d", iValue ) );
		break;
	case RPG_NOTIFY_HUMANITY:
		Msg( "[rpg] humanity %+d (%s)\n", iValue, pszToken );	// скрытая шкала: только консоль
		return false;
	case RPG_NOTIFY_PERK:
	{
		const RPGPerkDef_t *pDef = RPGData().FindPerk( pszToken );
		RPG_LocalizeUTF8( pDef ? pDef->name.Get() : pszToken, sz1, sizeof( sz1 ), pszToken );
		FormatToken( szText, nText, "#HL2RPM_UI_Notify_Perk", "Perk: %s1", sz1 );
		break;
	}
	case RPG_NOTIFY_CHECK:
		FormatToken( szText, nText, iValue ? "#HL2RPM_UI_Notify_CheckPass" : "#HL2RPM_UI_Notify_CheckFail",
			iValue ? "[%s1] success" : "[%s1] failed", pszToken );
		break;
	case RPG_NOTIFY_QUEST:
	{
		// extra: started / stage / completed / failed / "progress <цель> <сейчас> <нужно>"
		RPGQuestData().EnsureLoaded();
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszToken );
		RPG_LocalizeUTF8( pDef ? pDef->title.Get() : pszToken, sz1, sizeof( sz1 ), pszToken );
		const char *pszWhat = pszExtra ? pszExtra : "";
		if ( !Q_strnicmp( pszWhat, "progress", 8 ) )
		{
			int iObj = 0, iCur = 0, iNeed = 0;
			sscanf( pszWhat + 8, "%d %d %d", &iObj, &iCur, &iNeed );
			const int iStage = RPGQuest_GetStage( g_RPGClient.Data(), pszToken );
			if ( pDef && iStage >= 0 && iStage < pDef->stages.Count() && iObj >= 0 && iObj < pDef->stages[iStage].objectives.Count() &&
				!pDef->stages[iStage].objectives[iObj].title.IsEmpty() )
				RPG_LocalizeUTF8( pDef->stages[iStage].objectives[iObj].title.Get(), sz1, sizeof( sz1 ), sz1 );
			FormatToken( szText, nText, "#HL2RPM_UI_Notify_QuestProgress", "%s1: %s2", sz1, CFmtStr( "%d/%d", iCur, iNeed ) );
		}
		else if ( !Q_stricmp( pszWhat, "completed" ) )
			FormatToken( szText, nText, "#HL2RPM_UI_Notify_QuestDone", "Task completed: %s1", sz1 );
		else if ( !Q_stricmp( pszWhat, "failed" ) )
			FormatToken( szText, nText, "#HL2RPM_UI_Notify_QuestFailed", "Task failed: %s1", sz1 );
		else if ( !Q_stricmp( pszWhat, "started" ) )
			FormatToken( szText, nText, "#HL2RPM_UI_Notify_QuestStarted", "New task: %s1", sz1 );
		else
			FormatToken( szText, nText, "#HL2RPM_UI_Notify_QuestStage", "Task updated: %s1", sz1 );
		break;
	}
	case RPG_NOTIFY_ZONE:
		RPG_LocalizeUTF8( iValue == RPG_ZONE_RESTRICTED ? "#HL2RPM_UI_Notify_ZoneRestricted" : ( iValue ? "#HL2RPM_UI_Notify_ZoneEnter" : "#HL2RPM_UI_Notify_ZoneLeave" ),
			szText, nText, iValue == RPG_ZONE_RESTRICTED ? "Restricted zone" : ( iValue ? "Alliance control zone" : "You left the control zone" ) );
		break;
	case RPG_NOTIFY_LAW:
	{
		// изъятое: классы через запятую -> имена
		char szItems[256] = "";
		if ( pszExtra && pszExtra[0] )
		{
			CUtlStringList parts;
			V_SplitString( pszExtra, ",", parts );
			for ( int i = 0; i < parts.Count(); ++i )
			{
				ItemName( parts[i], sz1, sizeof( sz1 ) );
				if ( i )
					Q_strncat( szItems, ", ", sizeof( szItems ) );
				Q_strncat( szItems, sz1, sizeof( szItems ) );
			}
		}
		const char *pszKey = pszToken ? pszToken : "";
		if ( !Q_stricmp( pszKey, "arrested" ) || !Q_stricmp( pszKey, "frisk_found" ) )
		{
			FormatToken( szText, nText, CFmtStr( "#HL2RPM_UI_Law_%s", pszKey ), "Fine: %s1 cr.", CFmtStr( "%d", iValue ) );
			if ( szItems[0] )
			{
				FormatToken( sz2, sizeof( sz2 ), "#HL2RPM_UI_Law_Confiscated", "Confiscated: %s1", szItems );
				Q_strncat( szText, " ", nText );
				Q_strncat( szText, sz2, nText );
			}
		}
		else
			RPG_LocalizeUTF8( CFmtStr( "#HL2RPM_UI_Law_%s", pszKey ), szText, nText, pszKey );
		break;
	}
	case RPG_NOTIFY_FAIL:
	case RPG_NOTIFY_GENERIC:
		Q_strncpy( sz2, pszExtra ? pszExtra : "", sizeof( sz2 ) );
		if ( pszExtra && pszExtra[0] && RPGData().FindItem( pszExtra ) )
			ItemName( pszExtra, sz2, sizeof( sz2 ) );
		FormatToken( szText, nText, pszToken, pszToken && pszToken[0] == '#' ? CFmtStr( "%s %%s1", pszToken + 1 ).Access() : pszToken, sz2 );
		break;
	default:
		return false;
	}
	return szText[0] != '\0';
}

void C_RPGCharacter::DefaultOpenUI( const char *pszScreen, const char *pszParam, int iSource )
{
	char sz[512];
	// экранов ещё нет: сон - сразу на предложенные часы, записки - заголовок в ленту, остальное - подсказка в консоль
	if ( !Q_stricmp( pszScreen, "sleep" ) )
	{
		Msg( "[rpg] sleep requested (%s h) - no sleep screen yet, sleeping the default time\n", pszParam );
		engine->ClientCmd( CFmtStr( "rpg_sleep %s\n", pszParam && pszParam[0] ? pszParam : "8" ) );
		return;
	}
	if ( !Q_stricmp( pszScreen, "note" ) || !Q_stricmp( pszScreen, "terminal" ) )
	{
		RPG_LocalizeUTF8( CFmtStr( "#NOTE_%s_TITLE", pszParam ).Access(), sz, sizeof( sz ), pszParam );
		Toast( sz );
		RPG_LocalizeUTF8( CFmtStr( "#NOTE_%s_TEXT", pszParam ).Access(), sz, sizeof( sz ), "" );
		if ( sz[0] )
			Msg( "[rpg] %s\n", sz );
		return;
	}
	if ( !Q_stricmp( pszScreen, "read" ) )
	{
		ItemName( pszParam, sz, sizeof( sz ) );
		Toast( sz );
		return;
	}
	Msg( "[rpg] UI screen '%s' requested (param '%s', source %d) - the screen is not implemented yet\n", pszScreen, pszParam, iSource );
	if ( !Q_stricmp( pszScreen, "chargen" ) )
		Msg( "[rpg]   rpg_chargen_commit \"<name>\" LOV ANA MIG BDI DUH AVT <tag1> <tag2> <tag3> [TR-xx] [TR-xx]  (sum %d)\n", RPG_CHARGEN_POINTS );
	else if ( !Q_stricmp( pszScreen, "levelup" ) )
		Msg( "[rpg]   skill points %d, perk picks %d: rpg_skill_add <skill> <n>, rpg_perk_pick <PK-xx>\n", m_Data.m_iSkillPoints, m_Data.m_iPerkPicks );
}

// ---------------------------------------------------------------------------
// Сообщения
// ---------------------------------------------------------------------------
static void __MsgFunc_RPG_State( bf_read &msg )
{
	g_RPGClient.OnStateMessage( msg );
}

static void __MsgFunc_RPG_Notify( bf_read &msg )
{
	g_RPGClient.OnNotifyMessage( msg );
}

// ---------------------------------------------------------------------------
// Система клиента
// ---------------------------------------------------------------------------
class CRPGClientSystem : public CAutoGameSystemPerFrame
{
public:
	CRPGClientSystem() : CAutoGameSystemPerFrame( "CRPGClientSystem" ), m_bRequested( false ) {}

	virtual bool Init()
	{
		// тексты RPG и диалогов (их пишет build_content.py; файла может не быть)
		if ( g_pVGuiLocalize )
		{
			g_pVGuiLocalize->AddFile( "resource/hl2rpm_rpg_%language%.txt", "GAME", true );
			g_pVGuiLocalize->AddFile( "resource/hl2rpm_dialog_%language%.txt", "GAME", true );
		}
		usermessages->HookMessage( "RPG_State", __MsgFunc_RPG_State );
		usermessages->HookMessage( "RPG_Notify", __MsgFunc_RPG_Notify );
		RPG_SetWorldQuery( &s_ClientWorldQuery );
		return true;
	}

	virtual void LevelInitPreEntity()
	{
		RPGData().Load();
		RPGQuestData().Load();
		RPG_SetWorldQuery( &s_ClientWorldQuery );
		m_bRequested = false;
	}

	virtual void LevelShutdownPostEntity()
	{
		m_bRequested = false;
	}

	virtual void Update( float frametime )
	{
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer )
			return;
		// один раз на карту: полное состояние (сервер тоже шлёт его сам после загрузки)
		if ( !m_bRequested )
		{
			m_bRequested = true;
			engine->ClientCmd( "rpg_request_state\n" );
		}
		// тема HUD по сетевому типу костюма: переживает загрузку и hud_reloadscheme
		const int iSuit = pPlayer->m_Local.m_iSuitType;
		const bool bCombine = ( iSuit == RPG_SUIT_CP || iSuit == RPG_SUIT_OVERWATCH );
		if ( gHUD.m_bCombineSuitActive != bCombine )
			gHUD.ApplyCombineSuitStyle( bCombine, false );
	}

private:
	bool m_bRequested;
};
static CRPGClientSystem g_RPGClientSystem;

// ---------------------------------------------------------------------------
// Команды клиента
// ---------------------------------------------------------------------------
CON_COMMAND_F( cl_rpg_dump, "RPG: print the client mirror of the character", FCVAR_CLIENTDLL )
{
	const C_RPGCharacter &c = RPGClient();
	Msg( "[rpg client] state %s, suit type %d\n", c.HasState() ? "received" : "NOT received", c.GetSuitType() );
	c.Data().Print( "[rpg client]" );
	char szName[128];
	RPG_LocalizeUTF8( c.GetName(), szName, sizeof( szName ) );
	Msg( "[rpg client] name (localized): %s\n", szName );
}

CON_COMMAND_F( cl_rpg_reload, "RPG: reload scripts/rpg/*.txt on the client", FCVAR_CLIENTDLL )
{
	RPGData().Load();
}

CON_COMMAND_F( cl_rpg_open_ui, "RPG test: open a UI screen as if the server asked: cl_rpg_open_ui <chargen|levelup|note|read|terminal|sleep> [param]", FCVAR_CLIENTDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() > 1 )
		g_RPGClient.OpenUI( args[1], args.ArgC() > 2 ? args[2] : "", -1 );
}

CON_COMMAND_F( cl_rpg_test, "RPG: test a condition against the client mirror: cl_rpg_test \"ANA>=5, PK-02\"", FCVAR_CLIENTDLL )
{
	if ( args.ArgC() < 2 )
		return;
	RPGConditionList_t list;
	list.ParseExpression( args.ArgS() );
	RPGEvalResult_t res = list.Evaluate( RPG_MakeEvalContext( NULL ) );
	Msg( "[rpg client] test \"%s\": %s%s %s\n", args.ArgS(), res.met ? "TRUE" : "FALSE", res.unknown ? " (partly unknown on the client)" : "", res.reason.Get() );
}

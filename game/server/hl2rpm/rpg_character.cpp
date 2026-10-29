//========= HL2RPM ============//
//
// RPG-система (сервер): персонаж, сеть, сохранение, подозрение, XP за убийства.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_law.h"
#include "hl2rpm/rpg_formulas.h"
#include "hl2rpm/rpg_entities.h"
#include "isaverestore.h"
#include "saverestore.h"
#include "recipientfilter.h"
#include "GameEventListener.h"
#include "ai_basenpc.h"
#include "ai_basenpc.h"
#include "player.h"
#include "inventory_system.h"
#include "quest_system.h"
#include "hl2rpm/rpg_quests.h"
#include "globalstate.h"
#include "timecycle/env_timecycle.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar rpg_debug( "rpg_debug", "0", FCVAR_GAMEDLL, "RPG: 1 - print character changes and notifications, 2 - also network records" );
ConVar rpg_suspicion_enable( "rpg_suspicion_enable", "1", FCVAR_GAMEDLL | FCVAR_CHEAT, "RPG: suspicion simulation (control zones, Civil Protection)" );
ConVar rpg_suspicion_watch_dist( "rpg_suspicion_watch_dist", "1024", FCVAR_GAMEDLL, "RPG: distance at which CP/scanners notice the player" );
ConVar rpg_suspicion_run_speed( "rpg_suspicion_run_speed", "230", FCVAR_GAMEDLL, "RPG: speed that counts as running (HL2 normal 190, sprint 320)" );
ConVar rpg_suspicion_alarm_clear( "rpg_suspicion_alarm_clear", "25", FCVAR_GAMEDLL, "RPG: the alarm ends when suspicion drops below this value" );
ConVar rpg_suspicion_debug( "rpg_suspicion_debug", "0", FCVAR_GAMEDLL, "RPG: print suspicion sources once a second" );

static CRPGCharacter g_RPGCharacter;
static CUtlVector<IRPGListener *> g_RPGListeners;

void RPG_AddListener( IRPGListener *pListener )
{
	if ( pListener && g_RPGListeners.Find( pListener ) == g_RPGListeners.InvalidIndex() )
		g_RPGListeners.AddToTail( pListener );
}

void RPG_RemoveListener( IRPGListener *pListener )
{
	g_RPGListeners.FindAndRemove( pListener );
}

#define FOR_EACH_RPG_LISTENER( call ) \
	for ( int _l = 0; _l < g_RPGListeners.Count(); ++_l ) { g_RPGListeners[_l]->call; }

CBasePlayer *RPG_GetPlayer()
{
	return UTIL_GetLocalPlayer();
}

CRPGCharacter *RPG_GetCharacter( CBasePlayer *pPlayer )
{
	return &g_RPGCharacter;
}

RPGEvalContext_t RPG_MakeEvalContext( CBasePlayer *pPlayer, CBaseEntity *pNPC )
{
	RPGEvalContext_t ctx;
	ctx.pChar = &g_RPGCharacter.Data();
	ctx.pPlayer = pPlayer ? pPlayer : RPG_GetPlayer();
	ctx.pNPC = pNPC;
	return ctx;
}

bool RPG_TestCondition( const char *pszExpr, CBasePlayer *pPlayer, CBaseEntity *pNPC, CUtlString *pReason )
{
	return RPG_TestCondition( pszExpr, RPG_MakeEvalContext( pPlayer, pNPC ), pReason );
}

void RPG_ApplyEffects( const char *pszEffects, CBasePlayer *pPlayer, CBaseEntity *pNPC, CBaseEntity *pCaller )
{
	if ( !pszEffects || !pszEffects[0] )
		return;
	RPGEffectList_t list;
	list.ParseString( pszEffects );
	list.Apply( pPlayer, pNPC, pCaller );
}

void RPG_QuestEvent( CBasePlayer *pPlayer, const char *pszType, const char *pszTarget )
{
	if ( !pszType || !pszTarget )
		return;
	if ( !pPlayer )
		pPlayer = RPG_GetPlayer();
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] quest event %s:%s\n", pszType, pszTarget );
	FOR_EACH_RPG_LISTENER( OnRPGQuestEvent( pPlayer, pszType, pszTarget ) );
}

void RPG_FireItemsChanged( CBasePlayer *pPlayer, const char *pszClassname, int iDelta )
{
	FOR_EACH_RPG_LISTENER( OnRPGItemsChanged( pPlayer, pszClassname, iDelta ) );
}

static CUtlString s_BarterTrader;
static EHANDLE s_hBarterNPC;
static bool s_bBarterHadNPC = false;

void RPG_OpenUI( CBasePlayer *pPlayer, const char *pszScreen, const char *pszParam, CBaseEntity *pSource )
{
	if ( pszScreen && !Q_stricmp( pszScreen, "barter" ) )
	{
		s_BarterTrader = pszParam ? pszParam : "";
		s_hBarterNPC = pSource;
		s_bBarterHadNPC = pSource != NULL;
	}
	g_RPGCharacter.Notify( RPG_NOTIFY_OPEN_UI, pszScreen, pSource ? pSource->entindex() : 0, pszParam );
}

// ---------------------------------------------------------------------------
// Торговля: сессия - последний открытый экран «barter»; покупка и продажа проверяются здесь
// (цены и склад - CRPGCharacterData::GetTrader*, те же, что показывает экран клиента)
// ---------------------------------------------------------------------------
#define RPG_BARTER_MAX_DIST	320.0f

const char *RPG_GetBarterTrader( CBasePlayer *pPlayer, CBaseEntity **ppNPC )
{
	if ( ppNPC )
		*ppNPC = NULL;
	if ( s_BarterTrader.IsEmpty() )
		return NULL;
	CBaseEntity *pNPC = s_hBarterNPC.Get();
	if ( s_bBarterHadNPC )
	{
		// торговец ушёл, умер или игрок отошёл - сделка закрыта
		if ( !pNPC || !pNPC->IsAlive() || !pPlayer ||
			 ( pNPC->WorldSpaceCenter() - pPlayer->WorldSpaceCenter() ).LengthSqr() > RPG_BARTER_MAX_DIST * RPG_BARTER_MAX_DIST )
			return NULL;
	}
	if ( ppNPC )
		*ppNPC = pNPC;
	return s_BarterTrader.Get();
}

static const RPGTraderDef_t *BarterSession( CBasePlayer *pPlayer, CUtlString *pError )
{
	const char *pszTrader = RPG_GetBarterTrader( pPlayer );
	const RPGTraderDef_t *pDef = pszTrader ? RPGData().FindTrader( pszTrader ) : NULL;
	if ( !pDef && pError )
		*pError = "#HL2RPM_UI_Fail_BarterClosed";
	return pDef;
}

static void SetTraderCredits( CRPGCharacter *pChar, const RPGTraderDef_t &t, int iCredits )
{
	char szFlag[96];
	CRPGCharacterData::TraderCreditsFlag( t.id.Get(), szFlag, sizeof( szFlag ) );
	pChar->SetFlag( szFlag, Max( 0, iCredits ) + 1 );
}

static void SetTraderStock( CRPGCharacter *pChar, const RPGTraderDef_t &t, const char *pszClass, int iCount )
{
	char szFlag[128];
	CRPGCharacterData::TraderStockFlag( t.id.Get(), pszClass, szFlag, sizeof( szFlag ) );
	pChar->SetFlag( szFlag, Max( 0, iCount ) + 1 );
}

bool RPG_BarterBuy( CBasePlayer *pPlayer, const char *pszClass, int count, CUtlString *pError )
{
	const RPGTraderDef_t *pDef = BarterSession( pPlayer, pError );
	if ( !pDef || !pszClass || !pszClass[0] || count <= 0 )
		return false;
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	const CRPGCharacterData &c = pChar->Data();
	const int stock = c.GetTraderStock( *pDef, pszClass );
	const int price = c.GetTraderBuyPrice( *pDef, pszClass );
	if ( price < 0 || stock <= 0 )
	{
		if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterNoStock";
		return false;
	}
	count = Min( count, stock );
	if ( c.m_iCredits < price * count )
	{
		count = c.m_iCredits / price;
		if ( count <= 0 )
		{
			if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterNoCredits";
			return false;
		}
	}
	const int total = price * count;
	// без уведомлений: итог сделки виден на экране торговли, лента HUD не копит десятки строк
	pChar->AddCredits( -total, false );
	SetTraderCredits( pChar, *pDef, c.GetTraderCredits( *pDef ) + total );
	SetTraderStock( pChar, *pDef, pszClass, stock - count );
	RPG_GiveItem( pPlayer, pszClass, count, false );
	return true;
}

bool RPG_BarterSell( CBasePlayer *pPlayer, const char *pszClass, int count, CUtlString *pError )
{
	const RPGTraderDef_t *pDef = BarterSession( pPlayer, pError );
	if ( !pDef || !pszClass || !pszClass[0] || count <= 0 )
		return false;
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	const CRPGCharacterData &c = pChar->Data();
	const int price = c.GetTraderSellPrice( *pDef, pszClass );
	if ( price < 0 )
	{
		if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterWontBuy";
		return false;
	}
	// продаётся только то, что лежит в инвентаре: не надетый костюм и не оружие в руках
	CUtlVector<CUtlString> items;
	InventorySystem::GetInventoryItemStringsForPlayer( pPlayer, items );
	const int len = Q_strlen( pszClass );
	int have = 0;
	for ( int i = 0; i < items.Count(); ++i )
	{
		const char *psz = items[i].Get();
		if ( !Q_strnicmp( psz, pszClass, len ) && ( psz[len] == ':' || psz[len] == '\0' ) )
			++have;
	}
	count = Min( count, have );
	if ( count <= 0 )
	{
		if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterNoItem";
		return false;
	}
	const int cash = c.GetTraderCredits( *pDef );
	if ( cash < price * count )
	{
		count = cash / price;
		if ( count <= 0 )
		{
			if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterTraderBroke";
			return false;
		}
	}
	count = RPG_TakeItem( pPlayer, pszClass, count, false );
	if ( count <= 0 )
	{
		if ( pError ) *pError = "#HL2RPM_UI_Fail_BarterNoItem";
		return false;
	}
	const int total = price * count;
	pChar->AddCredits( total, false );
	SetTraderCredits( pChar, *pDef, cash - total );
	SetTraderStock( pChar, *pDef, pszClass, c.GetTraderStock( *pDef, pszClass ) + count );
	return true;
}

// ---------------------------------------------------------------------------
// NPC
// ---------------------------------------------------------------------------
const char *RPG_GetNPCFaction( CBaseEntity *pEnt )
{
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	return pNPC ? pNPC->GetRPGFaction() : "";
}

const char *RPG_GetNPCTrader( CBaseEntity *pEnt )
{
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	return pNPC ? pNPC->GetRPGTrader() : "";
}

bool RPG_IsWatcher( CBaseEntity *pEnt )
{
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	if ( !pNPC || !pNPC->IsAlive() )
		return false;
	const char *pszFaction = pNPC->GetRPGFaction();
	if ( pszFaction && pszFaction[0] )
		return !Q_stricmp( pszFaction, "cp" ) || !Q_stricmp( pszFaction, "overwatch" );
	static const char *s_pszWatcherClasses[] = { "npc_metropolice", "npc_combine_s", "npc_cscanner", "npc_clawscanner", "npc_combine_camera", NULL };
	for ( int i = 0; s_pszWatcherClasses[i]; ++i )
	{
		if ( pNPC->ClassMatches( s_pszWatcherClasses[i] ) )
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Костюм
// ---------------------------------------------------------------------------
int RPG_SuitTypeForClass( const char *pszSuitClass, bool bWearingSuit )
{
	if ( !bWearingSuit )
		return RPG_SUIT_CITIZEN;
	if ( !pszSuitClass || !pszSuitClass[0] || !Q_stricmp( pszSuitClass, "item_suit" ) )
		return RPG_SUIT_HEV;
	if ( !Q_stricmp( pszSuitClass, "item_combine_soldier_suit" ) )
		return RPG_SUIT_OVERWATCH;
	const RPGItemDef_t *pDef = RPGData().FindItem( pszSuitClass );
	if ( pDef && pDef->armor.Length() )
	{
		int t = RPG_SuitTypeFromKey( pDef->armor.Get() );
		if ( t >= 0 )
			return t;
	}
	return RPG_SUIT_HEV;
}

void RPG_UpdatePlayerSuitType( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;
	const bool bWearing = pPlayer->IsSuitEquipped();
	const char *pszClass = InventorySystem::GetEquippedSuitClass( pPlayer );
	int iType = RPG_SuitTypeForClass( pszClass, bWearing );
	if ( pPlayer->m_Local.m_iSuitType != iType )
		pPlayer->m_Local.m_iSuitType = iType;
	g_RPGCharacter.SetArmorClass( bWearing ? ( pszClass && pszClass[0] ? pszClass : "item_suit" ) : "" );
}

float RPG_GetCarryWeight( CBasePlayer *pPlayer )
{
	return g_RPGCharacter.Data().GetCarryWeight();
}

float RPG_GetItemWeight( const char *pszClassname )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClassname );
	return pDef ? pDef->kg : -1.0f;
}

// ---------------------------------------------------------------------------
// Часы мира
// ---------------------------------------------------------------------------
static float RPG_GetHoursPerSecond()
{
	CEnvTimecycle *pTC = GetTimecycle();
	if ( pTC )
		return pTC->GetHoursPerSecond();
	return WorldClock_GetHoursPerSecond();
}

void RPG_AdvanceClock( float flHours )
{
	float h = 12.0f;
	WorldClock_GetHours( h );
	float flNew = fmodf( h + flHours, 24.0f );
	if ( flNew < 0.0f )
		flNew += 24.0f;
	// игровые дни: полночь за время сна
	int nDays = (int)floorf( ( h + flHours ) / 24.0f );
	if ( nDays > 0 )
		g_RPGCharacter.AddFlag( RPG_FLAG_DAY, nDays );
	g_RPGCharacter.m_flLastClockHours = flNew;

	CEnvTimecycle *pTC = GetTimecycle();
	if ( pTC )
		pTC->SetTimeOfDayHours( flNew );
	else
		WorldClock_SetHours( flNew );
}

// ===========================================================================
// CRPGCharacter
// ===========================================================================
CRPGCharacter::CRPGCharacter() : m_KillCounts( k_eDictCompareTypeCaseInsensitive )
{
	m_bFullSyncPending = true;
	m_iLastSentSuspicion = 0;
	m_iKillDay = 0;
	m_flLastClockHours = 0.0f;
	m_bHaveClock = false;
}

void CRPGCharacter::Reset()
{
	m_Data.Reset();
	m_KillCounts.RemoveAll();
	m_iKillDay = 0;
	m_bHaveClock = false;
	m_Pending.RemoveAll();
	m_PendingNotify.RemoveAll();
	QueueFullSync();
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] character reset (new game)\n" );
}

static int RecordGroup( int type )
{
	if ( type == RPG_REC_FLAG_INT || type == RPG_REC_FLAG_STR || type == RPG_REC_FLAG_CLEAR )
		return RPG_REC_FLAG_INT;
	return type;
}

void CRPGCharacter::QueueRecord( const RPGRecord_t &rec )
{
	if ( m_bFullSyncPending )
		return;		// полная синхронизация и так всё отправит
	const int group = RecordGroup( rec.type );
	for ( int i = 0; i < m_Pending.Count(); ++i )
	{
		if ( RecordGroup( m_Pending[i].type ) == group && !Q_stricmp( m_Pending[i].key.Get(), rec.key.Get() ) )
		{
			m_Pending[i] = rec;
			return;
		}
	}
	m_Pending.AddToTail( rec );
}

void CRPGCharacter::QueueStat( const char *pszStat )
{
	int v = 0;
	if ( m_Data.GetStat( pszStat, v ) )
		QueueRecord( RPGRecord_t( RPG_REC_STAT, pszStat, v ) );
}

void CRPGCharacter::QueueFullSync()
{
	m_bFullSyncPending = true;
	m_Pending.RemoveAll();
}

void CRPGCharacter::Notify( int kind, const char *pszToken, int iValue, const char *pszExtra )
{
	PendingNotify_t &n = m_PendingNotify[m_PendingNotify.AddToTail()];
	n.kind = kind;
	n.token = pszToken ? pszToken : "";
	n.value = iValue;
	n.extra = pszExtra ? pszExtra : "";
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] notify %d \"%s\" %d \"%s\"\n", kind, n.token.Get(), iValue, n.extra.Get() );
}

// ---- сеть ----
static int RecordSize( const RPGRecord_t &rec )
{
	int n = 1 + Min( rec.key.Length(), 63 ) + 1;
	if ( RPGRecordHasString( rec.type ) )
		n += Min( rec.sValue.Length(), RPG_STRING_VALUE_MAX ) + 1;
	else
		n += 4;
	return n;
}

static void WriteTruncatedString( const char *psz, int maxLen )
{
	char buf[RPG_STRING_VALUE_MAX + 4];
	maxLen = Min( maxLen, (int)sizeof( buf ) - 1 );
	Q_strncpy( buf, psz ? psz : "", maxLen + 1 );
	// не резать посередине символа UTF-8
	int len = Q_strlen( buf );
	while ( len > 0 && ( (unsigned char)buf[len - 1] & 0xC0 ) == 0x80 && len == maxLen )
		buf[--len] = '\0';
	if ( len > 0 && (unsigned char)buf[len - 1] >= 0xC0 && len == maxLen )
		buf[--len] = '\0';
	WRITE_STRING( buf );
}

static void SendRecords( CBasePlayer *pPlayer, const CUtlVector<RPGRecord_t> &records )
{
	int i = 0;
	while ( i < records.Count() )
	{
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_State" );
		int bytes = 0;
		do
		{
			const RPGRecord_t &rec = records[i];
			WRITE_BYTE( rec.type );
			WriteTruncatedString( rec.key.Get(), 63 );
			if ( RPGRecordHasString( rec.type ) )
				WriteTruncatedString( rec.sValue.Get(), RPG_STRING_VALUE_MAX );
			else
				WRITE_LONG( rec.iValue );
			bytes += RecordSize( rec );
			if ( rpg_debug.GetInt() >= 2 )
				Msg( "[rpg]   rec %d %s %d \"%s\"\n", rec.type, rec.key.Get(), rec.iValue, rec.sValue.Get() );
			++i;
		}
		while ( i < records.Count() && bytes + RecordSize( records[i] ) <= RPG_STATE_MSG_MAX_BYTES );
		MessageEnd();
	}
}

void CRPGCharacter::Flush( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return;

	if ( m_bFullSyncPending )
	{
		CUtlVector<RPGRecord_t> records;
		records.AddToTail( RPGRecord_t( RPG_REC_FULL_BEGIN, "", 0 ) );
		m_Data.BuildRecords( records );
		records.AddToTail( RPGRecord_t( RPG_REC_FULL_END, "", 0 ) );
		m_bFullSyncPending = false;
		m_Pending.RemoveAll();
		m_iLastSentSuspicion = (int)m_Data.m_flSuspicion;
		SendRecords( pPlayer, records );
		if ( rpg_debug.GetBool() )
			Msg( "[rpg] full state sent (%d records)\n", records.Count() );
	}
	else if ( m_Pending.Count() )
	{
		SendRecords( pPlayer, m_Pending );
		m_Pending.RemoveAll();
	}

	for ( int i = 0; i < m_PendingNotify.Count(); ++i )
	{
		const PendingNotify_t &n = m_PendingNotify[i];
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_Notify" );
		WRITE_BYTE( n.kind );
		WriteTruncatedString( n.token.Get(), 100 );
		WRITE_LONG( n.value );
		WriteTruncatedString( n.extra.Get(), 120 );
		MessageEnd();
	}
	m_PendingNotify.RemoveAll();
}

// ---- изменения ----
void CRPGCharacter::SetName( const char *pszName )
{
	char buf[RPG_NAME_MAX + 1];
	Q_strncpy( buf, pszName ? pszName : "", sizeof( buf ) );
	m_Data.m_Name = buf;
	QueueRecord( RPGRecord_t( RPG_REC_NAME, "", 0, buf ) );
}

void CRPGCharacter::SetAttr( int attr, int value )
{
	if ( attr < 0 || attr >= RPG_ATTR_COUNT )
		return;
	m_Data.m_Attr[attr] = clamp( value, RPG_ATTR_MIN, RPG_ATTR_MAX_EFFECTIVE );
	QueueRecord( RPGRecord_t( RPG_REC_ATTR, g_pszRPGAttrIds[attr], m_Data.m_Attr[attr] ) );
}

void CRPGCharacter::AddXP( int amount, const char *pszSource, bool bNotify )
{
	if ( amount <= 0 )
		return;
	m_Data.m_iXP += amount;
	QueueStat( "xp" );
	if ( bNotify )
		Notify( RPG_NOTIFY_XP, "", amount, pszSource );
	CheckLevelUp();
}

void CRPGCharacter::CheckLevelUp()
{
	int target = RPGFormulas::LevelForXP( m_Data.m_iXP );
	while ( m_Data.m_iLevel < target )
	{
		m_Data.m_iLevel++;
		const int sp = RPGFormulas::SkillPointsPerLevel( m_Data.GetAttr( RPG_ATTR_ANALYTICS ) );
		m_Data.m_iSkillPoints += sp;
		const bool bPerk = RPGFormulas::LevelGivesPerk( m_Data.m_iLevel );
		if ( bPerk )
			m_Data.m_iPerkPicks++;
		QueueStat( "level" );
		QueueStat( "skillpoints" );
		QueueStat( "perkpicks" );
		Notify( RPG_NOTIFY_LEVELUP, "", m_Data.m_iLevel, CFmtStr( "%d %d", sp, bPerk ? 1 : 0 ) );
		FOR_EACH_RPG_LISTENER( OnRPGLevelUp( RPG_GetPlayer(), m_Data.m_iLevel ) );
	}
}

void CRPGCharacter::SetLevel( int level )
{
	level = clamp( level, 1, RPG_MAX_LEVEL );
	if ( level > m_Data.m_iLevel )
	{
		int need = RPGFormulas::XPForLevel( level ) - m_Data.m_iXP;
		if ( need > 0 )
			AddXP( need, "cheat" );
		return;
	}
	m_Data.m_iLevel = level;
	m_Data.m_iXP = RPGFormulas::XPForLevel( level );
	QueueStat( "level" );
	QueueStat( "xp" );
}

void CRPGCharacter::SetSkillPoints( int value )
{
	m_Data.m_iSkillPoints = Max( 0, value );
	QueueStat( "skillpoints" );
}

void CRPGCharacter::SetPerkPicks( int value )
{
	m_Data.m_iPerkPicks = Max( 0, value );
	QueueStat( "perkpicks" );
}

bool CRPGCharacter::InvestSkill( int skill, int points, CUtlString *pError )
{
	const CRPGData &data = RPGData();
	if ( skill < 0 || skill >= data.GetSkillCount() || skill >= RPG_MAX_SKILLS )
	{
		if ( pError ) *pError = "unknown skill";
		return false;
	}
	if ( points <= 0 || points > m_Data.m_iSkillPoints )
	{
		if ( pError ) pError->Format( "not enough skill points (%d)", m_Data.m_iSkillPoints );
		return false;
	}
	if ( m_Data.GetSkill( skill ) + points > RPG_SKILL_CAP )
	{
		if ( pError ) pError->Format( "skill cap %d", RPG_SKILL_CAP );
		return false;
	}
	m_Data.m_SkillInvested[skill] += points;
	m_Data.m_iSkillPoints -= points;
	QueueRecord( RPGRecord_t( RPG_REC_SKILL, data.GetSkill( skill ).id.Get(), m_Data.m_SkillInvested[skill] ) );
	QueueStat( "skillpoints" );
	return true;
}

void CRPGCharacter::SetSkillInvested( int skill, int value )
{
	const CRPGData &data = RPGData();
	if ( skill < 0 || skill >= data.GetSkillCount() || skill >= RPG_MAX_SKILLS )
		return;
	m_Data.m_SkillInvested[skill] = clamp( value, 0, RPG_SKILL_CAP );
	QueueRecord( RPGRecord_t( RPG_REC_SKILL, data.GetSkill( skill ).id.Get(), m_Data.m_SkillInvested[skill] ) );
}

void CRPGCharacter::SetSkillTagged( int skill, bool bTagged )
{
	const CRPGData &data = RPGData();
	if ( skill < 0 || skill >= data.GetSkillCount() || skill >= RPG_MAX_SKILLS )
		return;
	m_Data.m_SkillTagged[skill] = bTagged;
	QueueRecord( RPGRecord_t( RPG_REC_TAG, data.GetSkill( skill ).id.Get(), bTagged ? 1 : 0 ) );
}

bool CRPGCharacter::CanPickPerk( const char *pszPerk, CUtlString *pReason ) const
{
	const RPGPerkDef_t *pDef = RPGData().FindPerk( pszPerk );
	if ( !pDef )
	{
		if ( pReason ) *pReason = "unknown perk";
		return false;
	}
	if ( m_Data.m_iLevel < pDef->level )
	{
		if ( pReason ) pReason->Format( "level %d required", pDef->level );
		return false;
	}
	if ( m_Data.GetPerkRank( pDef->id.Get() ) >= pDef->ranks )
	{
		if ( pReason ) *pReason = "max rank";
		return false;
	}
	if ( pDef->require.Length() )
	{
		CUtlString reason;
		if ( !RPG_TestCondition( pDef->require.Get(), RPG_MakeEvalContext( NULL ), &reason ) )
		{
			if ( pReason ) *pReason = reason;
			return false;
		}
	}
	return true;
}

bool CRPGCharacter::PickPerk( const char *pszPerk, CUtlString *pError )
{
	if ( m_Data.m_iPerkPicks <= 0 )
	{
		if ( pError ) *pError = "no perk picks";
		return false;
	}
	if ( !CanPickPerk( pszPerk, pError ) )
		return false;
	const RPGPerkDef_t *pDef = RPGData().FindPerk( pszPerk );
	SetPerkRank( pDef->id.Get(), m_Data.GetPerkRank( pDef->id.Get() ) + 1 );
	m_Data.m_iPerkPicks--;
	QueueStat( "perkpicks" );
	Notify( RPG_NOTIFY_PERK, pDef->id.Get(), m_Data.GetPerkRank( pDef->id.Get() ) );
	return true;
}

void CRPGCharacter::SetPerkRank( const char *pszPerk, int rank )
{
	if ( !pszPerk || !pszPerk[0] )
		return;
	RPGRecord_t rec( RPG_REC_PERK, pszPerk, Max( 0, rank ) );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
}

void CRPGCharacter::SetTrait( const char *pszTrait, bool bOn )
{
	const RPGTraitDef_t *pDef = RPGData().FindTrait( pszTrait );
	RPGRecord_t rec( RPG_REC_TRAIT, pDef ? pDef->id.Get() : pszTrait, bOn ? 1 : 0 );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
}

void CRPGCharacter::AddCredits( int delta, bool bNotify )
{
	int old = m_Data.m_iCredits;
	m_Data.m_iCredits = Max( 0, old + delta );
	if ( m_Data.m_iCredits == old )
		return;
	QueueStat( "credits" );
	if ( bNotify )
		Notify( RPG_NOTIFY_CREDITS, "", m_Data.m_iCredits - old );
}

void CRPGCharacter::SetCredits( int value )
{
	m_Data.m_iCredits = Max( 0, value );
	QueueStat( "credits" );
}

void CRPGCharacter::AddCivil( int delta, bool bNotify )
{
	int old = m_Data.m_iCivil;
	m_Data.m_iCivil = clamp( old + delta, -100, 100 );
	if ( m_Data.m_iCivil == old )
		return;
	QueueStat( "civil" );
	if ( bNotify )
		Notify( RPG_NOTIFY_CIVIL, RPGFormulas::CivilTierKey( m_Data.GetCivilTier() ), m_Data.m_iCivil - old,
			RPGFormulas::CivilTier( old ) != m_Data.GetCivilTier() ? "tier" : "" );
}

void CRPGCharacter::SetCivil( int value )
{
	m_Data.m_iCivil = clamp( value, -100, 100 );
	QueueStat( "civil" );
}

void CRPGCharacter::AddSuspicion( float delta, bool bScaled )
{
	if ( bScaled && delta > 0.0f )
		delta *= m_Data.GetModMult( "suspicion_gain" );
	SetSuspicion( m_Data.m_flSuspicion + delta );
}

void CRPGCharacter::SetSuspicion( float value )
{
	const int oldTier = m_Data.GetSuspicionTier();
	m_Data.m_flSuspicion = clamp( value, 0.0f, 100.0f );
	const int iWhole = (int)m_Data.m_flSuspicion;
	if ( iWhole != m_iLastSentSuspicion )
	{
		m_iLastSentSuspicion = iWhole;
		QueueStat( "suspicion" );
	}
	if ( m_Data.GetSuspicionTier() != oldTier )
		ApplySuspicionTier( oldTier );

	// тревога: на 100 - да, снимается ниже rpg_suspicion_alarm_clear
	if ( !m_Data.m_bAlarm && m_Data.m_flSuspicion >= 100.0f )
		SetAlarm( true );
	else if ( m_Data.m_bAlarm && m_Data.m_flSuspicion < rpg_suspicion_alarm_clear.GetFloat() )
		SetAlarm( false );
}

void CRPGCharacter::ApplySuspicionTier( int oldTier )
{
	const int newTier = m_Data.GetSuspicionTier();
	QueueStat( "suspicion" );
	Notify( RPG_NOTIFY_SUSPICION, RPGFormulas::SuspicionTierKey( newTier ), (int)m_Data.m_flSuspicion,
		RPGFormulas::SuspicionTierKey( oldTier ) );
	RPG_FireWatchOutputs( oldTier, newTier );
	FOR_EACH_RPG_LISTENER( OnRPGSuspicionTier( RPG_GetPlayer(), oldTier, newTier ) );
}

void CRPGCharacter::AddHumanity( int delta, bool bNotify )
{
	int old = m_Data.m_iHumanity;
	m_Data.m_iHumanity = clamp( old + delta, 0, 100 );
	if ( m_Data.m_iHumanity == old )
		return;
	QueueStat( "humanity" );
	if ( bNotify )
		Notify( RPG_NOTIFY_HUMANITY, RPGFormulas::HumanityTierKey( m_Data.GetHumanityTier() ), m_Data.m_iHumanity - old );
}

void CRPGCharacter::AddResonance( int delta )
{
	if ( delta > 0 )
		delta = (int)ceilf( (float)delta * m_Data.GetModMult( "resonance_gain" ) - 0.001f );
	SetResonance( m_Data.m_iResonance + delta );
}

void CRPGCharacter::AddInfluence( int delta )
{
	SetInfluence( m_Data.m_iInfluence + delta );
}

void CRPGCharacter::SetHumanity( int value )
{
	m_Data.m_iHumanity = clamp( value, 0, 100 );
	QueueStat( "humanity" );
}

void CRPGCharacter::SetResonance( int value )
{
	m_Data.m_iResonance = clamp( value, 0, 100 );
	QueueStat( "resonance" );
}

void CRPGCharacter::SetInfluence( int value )
{
	m_Data.m_iInfluence = clamp( value, 0, 100 );
	QueueStat( "influence" );
}

void CRPGCharacter::AddRep( const char *pszFaction, int delta, bool bNotify )
{
	if ( !pszFaction || !pszFaction[0] || delta == 0 )
		return;
	const RPGFactionDef_t *pDef = RPGData().FindFaction( pszFaction );
	if ( pDef && pDef->service )
	{
		DevWarning( "[rpg] rep: '%s' is a service faction without a scale\n", pszFaction );
		return;
	}
	const char *pszId = pDef ? pDef->id.Get() : pszFaction;
	int old = m_Data.GetFaction( pszId );
	SetRep( pszId, old + delta );
	int now = m_Data.GetFaction( pszId );
	if ( bNotify && now != old )
		Notify( RPG_NOTIFY_REP, pszId, now - old, RPGFormulas::FactionTierKey( RPGFormulas::FactionTier( now ) ) );
}

void CRPGCharacter::SetRep( const char *pszFaction, int value )
{
	RPGRecord_t rec( RPG_REC_FACTION, pszFaction, clamp( value, -100, 100 ) );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
}

void CRPGCharacter::FlagChanged( const char *pszFlag )
{
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] flag %s = \"%s\"%s\n", pszFlag, m_Data.GetFlagString( pszFlag ), m_Data.FindFlag( pszFlag ) ? "" : " (cleared)" );
	FOR_EACH_RPG_LISTENER( OnRPGFlagChanged( RPG_GetPlayer(), pszFlag ) );
}

void CRPGCharacter::SetFlag( const char *pszFlag, int value )
{
	if ( !pszFlag || !pszFlag[0] )
		return;
	RPGRecord_t rec( RPG_REC_FLAG_INT, pszFlag, value );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	FlagChanged( pszFlag );
}

void CRPGCharacter::SetFlagString( const char *pszFlag, const char *pszValue )
{
	if ( !pszFlag || !pszFlag[0] )
		return;
	if ( !pszValue || !pszValue[0] )
	{
		SetFlag( pszFlag, 0 );
		return;
	}
	char buf[RPG_STRING_VALUE_MAX + 1];
	Q_strncpy( buf, pszValue, sizeof( buf ) );
	RPGRecord_t rec( RPG_REC_FLAG_STR, pszFlag, 0, buf );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	FlagChanged( pszFlag );
}

void CRPGCharacter::ClearFlag( const char *pszFlag )
{
	if ( !pszFlag || !pszFlag[0] || !m_Data.FindFlag( pszFlag ) )
		return;
	RPGRecord_t rec( RPG_REC_FLAG_CLEAR, pszFlag, 0 );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	FlagChanged( pszFlag );
}

void CRPGCharacter::AddFlag( const char *pszFlag, int delta )
{
	SetFlag( pszFlag, m_Data.GetFlagInt( pszFlag, 0 ) + delta );
}

void CRPGCharacter::SetDisposition( const char *pszNPC, int value )
{
	if ( !pszNPC || !pszNPC[0] )
		return;
	RPGRecord_t rec( RPG_REC_DISPOSITION, pszNPC, clamp( value, -100, 100 ) );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] disposition %s = %d\n", pszNPC, m_Data.GetDisposition( pszNPC ) );
}

void CRPGCharacter::AddDisposition( const char *pszNPC, int delta, int iStart )
{
	SetDisposition( pszNPC, m_Data.GetDisposition( pszNPC, iStart ) + delta );
}

bool CRPGCharacter::AddNote( const char *pszNote, bool bNotify )
{
	if ( !pszNote || !pszNote[0] || m_Data.HasNote( pszNote ) )
		return false;
	RPGRecord_t rec( RPG_REC_NOTE, pszNote, 1 );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	if ( bNotify )
		Notify( RPG_NOTIFY_NOTE, pszNote, 0 );
	AddXP( 5, "note", bNotify );	// уникальная записка - 5 XP (Part I/05)
	return true;
}

bool CRPGCharacter::Discover( const char *pszLocation, bool bNotify )
{
	if ( !pszLocation || !pszLocation[0] || m_Data.HasLocation( pszLocation ) )
		return false;
	const RPGLocationDef_t *pDef = RPGData().FindLocation( pszLocation );
	if ( !pDef )
		DevWarning( "[rpg] discover: unknown location '%s'\n", pszLocation );
	const char *pszId = pDef ? pDef->id.Get() : pszLocation;
	RPGRecord_t rec( RPG_REC_LOCATION, pszId, 1 );
	m_Data.ApplyRecord( rec );
	QueueRecord( rec );
	const int xp = pDef ? pDef->xp : 0;
	if ( bNotify )
		Notify( RPG_NOTIFY_LOCATION, pszId, xp );
	AddXP( xp, "location", bNotify );
	FOR_EACH_RPG_LISTENER( OnRPGLocationDiscovered( RPG_GetPlayer(), pszId ) );
	return true;
}

void CRPGCharacter::SetAct( int act )
{
	SetFlag( RPG_FLAG_ACT, clamp( act, 1, 4 ) );
}

void CRPGCharacter::SetChargenDone( bool bDone )
{
	m_Data.m_bChargenDone = bDone;
	QueueStat( "chargen" );
}

bool CRPGCharacter::CommitChargen( const char *pszName, const int *pAttrs, const char *const *ppTags, int nTags,
	const char *const *ppTraits, int nTraits, bool bForce, CUtlString *pError )
{
	const CRPGData &data = RPGData();
	if ( m_Data.m_bChargenDone && !bForce )
	{
		if ( pError ) *pError = "character already created";
		return false;
	}
	int sum = 0;
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
	{
		if ( pAttrs[i] < RPG_ATTR_MIN || pAttrs[i] > RPG_ATTR_MAX )
		{
			if ( pError ) pError->Format( "%s must be %d..%d", g_pszRPGAttrAbbr[i], RPG_ATTR_MIN, RPG_ATTR_MAX );
			return false;
		}
		sum += pAttrs[i];
	}
	if ( sum != RPG_CHARGEN_POINTS )
	{
		if ( pError ) pError->Format( "attributes sum %d, must be %d", sum, RPG_CHARGEN_POINTS );
		return false;
	}
	if ( nTags != RPG_TAG_COUNT )
	{
		if ( pError ) pError->Format( "need %d tag skills", RPG_TAG_COUNT );
		return false;
	}
	int tagIdx[RPG_TAG_COUNT];
	for ( int i = 0; i < nTags; ++i )
	{
		tagIdx[i] = data.FindSkill( ppTags[i] );
		if ( tagIdx[i] < 0 || tagIdx[i] >= RPG_MAX_SKILLS )
		{
			if ( pError ) pError->Format( "unknown skill '%s'", ppTags[i] );
			return false;
		}
		for ( int j = 0; j < i; ++j )
		{
			if ( tagIdx[j] == tagIdx[i] )
			{
				if ( pError ) *pError = "tag skills must differ";
				return false;
			}
		}
	}
	if ( nTraits < 0 || nTraits > RPG_MAX_TRAITS )
	{
		if ( pError ) pError->Format( "0..%d traits", RPG_MAX_TRAITS );
		return false;
	}
	const RPGTraitDef_t *pTraits[RPG_MAX_TRAITS] = { NULL, NULL };
	for ( int i = 0; i < nTraits; ++i )
	{
		pTraits[i] = data.FindTrait( ppTraits[i] );
		if ( !pTraits[i] || ( i > 0 && pTraits[i] == pTraits[0] ) )
		{
			if ( pError ) pError->Format( "bad trait '%s'", ppTraits[i] );
			return false;
		}
	}

	const bool bFirst = !m_Data.m_bChargenDone;
	if ( pszName && pszName[0] )
		SetName( pszName );
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
		SetAttr( i, pAttrs[i] );
	for ( int i = 0; i < data.GetSkillCount() && i < RPG_MAX_SKILLS; ++i )
		SetSkillTagged( i, false );
	for ( int i = 0; i < nTags; ++i )
		SetSkillTagged( tagIdx[i], true );
	while ( m_Data.m_Traits.Count() )
		SetTrait( m_Data.m_Traits[0].Get(), false );
	for ( int i = 0; i < nTraits; ++i )
		SetTrait( pTraits[i]->id.Get(), true );

	// стартовые модификаторы черт - только при первом создании
	if ( bFirst )
	{
		for ( int i = 0; i < nTraits; ++i )
		{
			const RPGModList_t &mods = pTraits[i]->mods;
			for ( int m = 0; m < mods.Count(); ++m )
			{
				const char *pszKey = mods[m].key.Get();
				if ( !Q_stricmp( pszKey, "start_credits" ) )
					AddCredits( (int)mods[m].value, false );
				else if ( !Q_stricmp( pszKey, "start_civil" ) )
					AddCivil( (int)mods[m].value, false );
				else if ( !Q_strnicmp( pszKey, "start_rep:", 10 ) )
					AddRep( pszKey + 10, (int)mods[m].value, false );
			}
		}
	}
	SetChargenDone( true );

	// новый персонаж: здоровье по новой формуле, полное
	CBasePlayer *pPlayer = RPG_GetPlayer();
	if ( pPlayer )
	{
		pPlayer->SetMaxHealth( m_Data.GetMaxHealth() );
		pPlayer->SetHealth( m_Data.GetMaxHealth() );
	}
	FOR_EACH_RPG_LISTENER( OnRPGChargenDone( pPlayer ) );
	return true;
}

void CRPGCharacter::SetZone( int zone )
{
	if ( m_Data.m_iZone == zone )
		return;
	m_Data.m_iZone = zone;
	QueueStat( "zone" );
	Notify( RPG_NOTIFY_ZONE, "", zone );
}

void CRPGCharacter::SetAlarm( bool bAlarm )
{
	if ( m_Data.m_bAlarm == bAlarm )
		return;
	m_Data.m_bAlarm = bAlarm;
	QueueStat( "alarm" );
	RPG_ApplyAlarm( bAlarm );
	RPG_Law_OnAlarm( bAlarm );
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] alarm %s\n", bAlarm ? "ON" : "off" );
}

void CRPGCharacter::SetArmorClass( const char *pszClass )
{
	if ( !Q_stricmp( m_Data.m_ArmorClass.Get(), pszClass ? pszClass : "" ) )
		return;
	m_Data.m_ArmorClass = pszClass ? pszClass : "";
	QueueRecord( RPGRecord_t( RPG_REC_ARMOR, "", 0, m_Data.m_ArmorClass.Get() ) );
}

int CRPGCharacter::CountKill( const char *pszClass, int iDay )
{
	if ( iDay != m_iKillDay )
	{
		m_KillCounts.RemoveAll();
		m_iKillDay = iDay;
	}
	int i = m_KillCounts.Find( pszClass );
	if ( i == m_KillCounts.InvalidIndex() )
		i = m_KillCounts.Insert( pszClass, 0 );
	return ++m_KillCounts[i];
}

// ---- сохранение ----
static const int RPG_SAVE_VERSION = 1;

void CRPGCharacter::Save( ISave *pSave )
{
	CUtlVector<RPGRecord_t> records;
	m_Data.BuildRecords( records );
	for ( int i = m_KillCounts.First(); i != m_KillCounts.InvalidIndex(); i = m_KillCounts.Next( i ) )
		records.AddToTail( RPGRecord_t( RPG_REC_KILLCOUNT, m_KillCounts.GetElementName( i ), m_KillCounts[i] ) );
	records.AddToTail( RPGRecord_t( RPG_REC_SERVER_STAT, "kill_day", m_iKillDay ) );
	records.AddToTail( RPGRecord_t( RPG_REC_SERVER_STAT, "clock_prev", m_bHaveClock ? (int)( m_flLastClockHours * 100.0f ) : -1 ) );

	int nVersion = RPG_SAVE_VERSION;
	pSave->WriteInt( &nVersion );
	int nCount = records.Count();
	pSave->WriteInt( &nCount );
	for ( int i = 0; i < nCount; ++i )
	{
		RPGRecord_t &rec = records[i];
		pSave->WriteInt( &rec.type );
		pSave->WriteString( rec.key.Get() );
		pSave->WriteInt( &rec.iValue );
		pSave->WriteString( rec.sValue.Get() );
	}
}

void CRPGCharacter::Restore( IRestore *pRestore )
{
	Reset();
	const int nVersion = pRestore->ReadInt();
	if ( nVersion < 1 || nVersion > RPG_SAVE_VERSION )
	{
		Warning( "[rpg] save: unknown character block version %d\n", nVersion );
		return;
	}
	const int nCount = pRestore->ReadInt();
	for ( int i = 0; i < nCount; ++i )
	{
		RPGRecord_t rec;
		char szKey[256], szValue[512];
		rec.type = pRestore->ReadInt();
		pRestore->ReadString( szKey, sizeof( szKey ), 0 );
		rec.iValue = pRestore->ReadInt();
		pRestore->ReadString( szValue, sizeof( szValue ), 0 );
		rec.key = szKey;
		rec.sValue = szValue;
		if ( rec.type < RPG_REC_SERVER_ONLY )
		{
			m_Data.ApplyRecord( rec );
		}
		else if ( rec.type == RPG_REC_KILLCOUNT )
		{
			m_KillCounts.Insert( szKey, rec.iValue );
		}
		else if ( rec.type == RPG_REC_SERVER_STAT )
		{
			if ( !Q_stricmp( szKey, "kill_day" ) )
				m_iKillDay = rec.iValue;
			else if ( !Q_stricmp( szKey, "clock_prev" ) )
			{
				m_bHaveClock = rec.iValue >= 0;
				m_flLastClockHours = (float)rec.iValue / 100.0f;
			}
		}
	}
	m_iLastSentSuspicion = (int)m_Data.m_flSuspicion;
	QueueFullSync();
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] character restored from save (%d records)\n", nCount );
}

// ===========================================================================
// Система: загрузка данных, сброс, кадр (подозрение, зоны, HP, костюм), XP за убийства
// ===========================================================================
class CRPGSystem : public CAutoGameSystemPerFrame, public CGameEventListener, public IRPGWorldQuery
{
public:
	CRPGSystem() : CAutoGameSystemPerFrame( "CRPGSystem" )
	{
		m_flReadyTime = 0.0f;
		m_flNextSim = 0.0f;
		m_flNextWatch = 0.0f;
		m_flNextAlarmCheck = 0.0f;
		m_flNextDebug = 0.0f;
		m_bSeen = false;
	}

	virtual bool Init()
	{
		RPG_SetWorldQuery( this );
		return true;
	}

	virtual void LevelInitPreEntity()
	{
		RPG_SetWorldQuery( this );
		RPGData().Load();
		RPG_InstallItemFactories();
		RPG_PrecacheItems();
		if ( gpGlobals->eLoadType == MapLoad_NewGame || gpGlobals->eLoadType == MapLoad_Background )
			g_RPGCharacter.Reset();
		m_flReadyTime = 0.0f;
	}

	virtual void LevelInitPostEntity()
	{
		ListenForGameEvent( "entity_killed" );
		g_RPGCharacter.QueueFullSync();
		// город Альянса - «мирный» режим HL2 (gordon_precriminal): горожане - мирные (CLASS_CITIZEN_PASSIVE),
		// ГО не нападает ни на них, ни на героя, пока подозрение не дошло до тревоги. Карты за стеной
		// выключают это своим env_global (тогда состояние уже есть в таблице и не трогается).
		if ( !GlobalEntity_IsInTable( "gordon_precriminal" ) )
			GlobalEntity_Add( "gordon_precriminal", STRING( gpGlobals->mapname ), GLOBAL_ON );
		m_flReadyTime = 0.0f;
		m_flNextSim = m_flNextWatch = m_flNextAlarmCheck = 0.0f;
		// слои карты по флагам: logic_rpg с test_on_spawn (загруженная игра уже в нужном состоянии)
		if ( gpGlobals->eLoadType != MapLoad_LoadGame )
			RPG_RunSpawnTests();
	}

	virtual void LevelShutdownPostEntity()
	{
		StopListeningForAllEvents();
	}

	virtual void FrameUpdatePostEntityThink()
	{
		CBasePlayer *pPlayer = RPG_GetPlayer();
		if ( !pPlayer || !pPlayer->IsConnected() )
		{
			m_flReadyTime = 0.0f;
			return;
		}
		if ( m_flReadyTime <= 0.0f )
			m_flReadyTime = gpGlobals->curtime + 0.2f;

		if ( gpGlobals->curtime >= m_flNextSim )
		{
			const float dt = ( m_flNextSim > 0.0f ) ? Min( 0.5f, gpGlobals->curtime - ( m_flNextSim - 0.1f ) ) : 0.1f;
			m_flNextSim = gpGlobals->curtime + 0.1f;
			UpdateClock();
			UpdateZone( pPlayer );
			RPG_UpdatePlayerSuitType( pPlayer );
			UpdateMaxHealth( pPlayer );
			if ( pPlayer->IsAlive() )
				UpdateSuspicion( pPlayer, dt );
			if ( g_RPGCharacter.Data().m_bAlarm && gpGlobals->curtime >= m_flNextAlarmCheck )
			{
				m_flNextAlarmCheck = gpGlobals->curtime + 1.0f;
				RPG_ApplyAlarm( true );	// новые ГО на карте тоже враждебны
			}
		}

		if ( gpGlobals->curtime >= m_flReadyTime )
			g_RPGCharacter.Flush( pPlayer );
	}

	virtual void FireGameEvent( IGameEvent *event )
	{
		if ( Q_stricmp( event->GetName(), "entity_killed" ) )
			return;
		CBasePlayer *pAttacker = NULL;
		int iAttacker = event->GetInt( "entindex_attacker", -1 );
		if ( iAttacker > 0 )
			pAttacker = ToBasePlayer( UTIL_EntityByIndex( iAttacker ) );
		if ( !pAttacker )
		{
			int iUser = event->GetInt( "attacker", 0 );
			if ( iUser > 0 )
				pAttacker = UTIL_PlayerByUserId( iUser );
		}
		if ( !pAttacker )
			return;
		CBaseEntity *pVictim = UTIL_EntityByIndex( event->GetInt( "entindex_killed", -1 ) );
		if ( !pVictim || pVictim == pAttacker )
			return;

		const RPGKillDef_t *pDef = RPGData().FindKill( pVictim->GetClassname() );
		if ( pDef )
		{
			const int iDay = g_RPGCharacter.Data().GetFlagInt( RPG_FLAG_DAY, 1 );
			const int n = g_RPGCharacter.CountKill( pVictim->GetClassname(), iDay );
			const int limit = RPGData().GetKillDailyLimit();
			if ( pDef->xp > 0 && ( limit <= 0 || n <= limit ) )
				g_RPGCharacter.AddXP( pDef->xp, "kill" );
			if ( pDef->humanity )
				g_RPGCharacter.AddHumanity( pDef->humanity );
		}
		FOR_EACH_RPG_LISTENER( OnRPGKill( pAttacker, pVictim ) );
	}

	// ---- IRPGWorldQuery ----
	virtual int CountItem( CBasePlayer *pPlayer, const char *pszClassname )
	{
		if ( !pPlayer )
			pPlayer = RPG_GetPlayer();
		return pPlayer ? RPG_CountItem( pPlayer, pszClassname ) : -1;
	}

	virtual int GetQuestState( CBasePlayer *pPlayer, const char *pszQuest, int &iStage )
	{
		if ( !pPlayer )
			pPlayer = RPG_GetPlayer();
		if ( !pPlayer )
			return -1;
		if ( RPGQuests::IsV2( pszQuest ) )
		{
			iStage = RPGQuests::GetStage( pszQuest );
			return RPGQuests::GetState( pszQuest );	// те же значения 0..3, что у QuestState
		}
		const PlayerQuestState *pState = QuestSystem().FindPlayerQuest( pPlayer, pszQuest );
		iStage = pState ? pState->stage : 0;
		return pState ? (int)pState->state : kQuestState_Inactive;
	}

	virtual const char *GetNPCName( CBaseEntity *pNPC )
	{
		return pNPC ? STRING( pNPC->GetEntityName() ) : "";
	}

	virtual int GetNPCStartDisposition( CBaseEntity *pNPC )
	{
		CAI_BaseNPC *pAI = pNPC ? pNPC->MyNPCPointer() : NULL;
		return pAI ? pAI->GetRPGDisposition() : 0;
	}

	virtual bool GetTimeOfDay( float &flHours, float &flSunrise, float &flSunset )
	{
		CEnvTimecycle *pTC = GetTimecycle();
		flSunrise = pTC ? pTC->GetSunriseHour() : 6.0f;
		flSunset = pTC ? pTC->GetSunsetHour() : 18.0f;
		if ( WorldClock_GetHours( flHours ) )
			return true;
		flHours = 12.0f;
		return true;	// часов нет вообще - считаем полдень
	}

	virtual int GetSuitType( CBasePlayer *pPlayer )
	{
		if ( !pPlayer )
			pPlayer = RPG_GetPlayer();
		return pPlayer ? pPlayer->m_Local.m_iSuitType : -1;
	}

	virtual int LawQuery( CBasePlayer *pPlayer, CBaseEntity *pNPC, const char *pszWhat )
	{
		return RPG_Law_Query( pPlayer, pNPC, pszWhat );
	}

private:
	void UpdateClock()
	{
		float h;
		if ( !WorldClock_GetHours( h ) )
			return;
		// часы перешли через полночь - новый игровой день
		if ( g_RPGCharacter.m_bHaveClock && h < g_RPGCharacter.m_flLastClockHours - 12.0f )
			g_RPGCharacter.AddFlag( RPG_FLAG_DAY, 1 );
		g_RPGCharacter.m_flLastClockHours = h;
		g_RPGCharacter.m_bHaveClock = true;
	}

	void UpdateZone( CBasePlayer *pPlayer )
	{
		g_RPGCharacter.SetZone( RPG_GetPlayerZone( pPlayer ) );
	}

	void UpdateMaxHealth( CBasePlayer *pPlayer )
	{
		const int iMax = g_RPGCharacter.Data().GetMaxHealth();
		if ( pPlayer->GetMaxHealth() != iMax )
		{
			pPlayer->SetMaxHealth( iMax );
			if ( pPlayer->GetHealth() > iMax )
				pPlayer->SetHealth( iMax );
		}
	}

	bool IsSeenByWatcher( CBasePlayer *pPlayer )
	{
		const float flDist = rpg_suspicion_watch_dist.GetFloat();
		const float flDistSqr = flDist * flDist;
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); ++i )
		{
			CAI_BaseNPC *pNPC = ppAIs[i];
			if ( !pNPC || !RPG_IsWatcher( pNPC ) )
				continue;
			if ( pNPC->GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() ) > flDistSqr )
				continue;
			if ( !pNPC->FInViewCone( pPlayer ) || !pNPC->FVisible( pPlayer ) )
				continue;
			return true;
		}
		return false;
	}

	bool IsHoldingContraband( CBasePlayer *pPlayer )
	{
		CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
		if ( !pWeapon || !pWeapon->IsWeaponVisible() )
			return false;
		const RPGItemDef_t *pDef = RPGData().FindItem( pWeapon->GetClassname() );
		return pDef ? pDef->contraband > 0 : true;	// неизвестное оружие - запрещено
	}

	void UpdateSuspicion( CBasePlayer *pPlayer, float dt )
	{
		if ( !rpg_suspicion_enable.GetBool() )
			return;
		const CRPGCharacterData &d = g_RPGCharacter.Data();

		if ( gpGlobals->curtime >= m_flNextWatch )
		{
			m_flNextWatch = gpGlobals->curtime + 0.25f;
			m_bSeen = IsSeenByWatcher( pPlayer );
		}

		const int zone = d.m_iZone;
		float flGain = 0.0f;
		bool bRunning = false, bWeapon = false;
		if ( zone >= RPG_ZONE_CONTROL )
		{
			if ( m_bSeen )
			{
				Vector vel = pPlayer->GetAbsVelocity();
				bRunning = vel.Length2D() > rpg_suspicion_run_speed.GetFloat() && ( pPlayer->GetFlags() & FL_ONGROUND );
				bWeapon = IsHoldingContraband( pPlayer );
				if ( bRunning )
					flGain += 2.0f * dt;
				if ( bWeapon )
					flGain += 20.0f * dt;
				// одежда повстанца в зоне контроля - сразу тревога
				if ( pPlayer->m_Local.m_iSuitType == RPG_SUIT_REBEL )
					flGain += 100.0f;
			}
			if ( zone >= RPG_ZONE_RESTRICTED )
				flGain += 5.0f * dt;
		}

		if ( flGain > 0.0f )
		{
			g_RPGCharacter.AddSuspicion( flGain, true );
		}
		else if ( !m_bSeen && zone < RPG_ZONE_RESTRICTED && d.m_flSuspicion > 0.0f )
		{
			// спад: -5 за игровую минуту вне взгляда ГО
			float hps = RPG_GetHoursPerSecond();
			if ( hps <= 0.0f )
				hps = 10.0f / 3600.0f;
			const float flGameMinutes = dt * hps * 60.0f;
			const float flRate = 5.0f * RPGFormulas::SuspicionDecayCivilMult( d.GetCivilTier() ) * d.GetModMult( "suspicion_decay" );
			g_RPGCharacter.AddSuspicion( -flRate * flGameMinutes, false );
		}

		if ( rpg_suspicion_debug.GetBool() && gpGlobals->curtime >= m_flNextDebug )
		{
			m_flNextDebug = gpGlobals->curtime + 1.0f;
			Msg( "[rpg] suspicion %.1f zone %d seen %d running %d weapon %d alarm %d\n", d.m_flSuspicion, zone,
				m_bSeen ? 1 : 0, bRunning ? 1 : 0, bWeapon ? 1 : 0, d.m_bAlarm ? 1 : 0 );
		}
	}

	float m_flReadyTime;
	float m_flNextSim;
	float m_flNextWatch;
	float m_flNextAlarmCheck;
	float m_flNextDebug;
	bool m_bSeen;
};

static CRPGSystem g_RPGSystem;

// ---------------------------------------------------------------------------
// Блок сохранения HL2RPMCharacter (как HL2RPMQuests: память новее при changelevel)
// ---------------------------------------------------------------------------
class CRPGSaveRestoreBlockHandler : public CDefSaveRestoreBlockHandler
{
public:
	const char *GetBlockName() { return "HL2RPMCharacter"; }

	void Save( ISave *pSave )
	{
		pSave->StartBlock( "Character" );
		g_RPGCharacter.Save( pSave );
		pSave->EndBlock();
	}

	void PreRestore()
	{
		m_bDoLoad = false;
		// сохранение без блока (старая сборка) не должно унаследовать персонажа прошлой сессии
		if ( gpGlobals->eLoadType == MapLoad_LoadGame )
		{
			RPGData().EnsureLoaded();
			g_RPGCharacter.Reset();
		}
	}

	void WriteSaveHeaders( ISave *pSave )
	{
		short nVersion = 1;
		pSave->WriteShort( &nVersion );
	}

	void ReadRestoreHeaders( IRestore *pRestore )
	{
		short nVersion;
		pRestore->ReadShort( &nVersion );
		// переход между картами восстанавливает старые данные уровня: персонаж в памяти новее
		m_bDoLoad = ( nVersion == 1 ) && ( gpGlobals->eLoadType == MapLoad_LoadGame );
	}

	void Restore( IRestore *pRestore, bool fCreatePlayers )
	{
		if ( !m_bDoLoad )
			return;
		pRestore->StartBlock();
		g_RPGCharacter.Restore( pRestore );
		pRestore->EndBlock();
	}

private:
	bool m_bDoLoad;
};

static CRPGSaveRestoreBlockHandler g_RPGSaveRestoreBlockHandler;

ISaveRestoreBlockHandler *GetRPGSaveRestoreBlockHandler()
{
	return &g_RPGSaveRestoreBlockHandler;
}

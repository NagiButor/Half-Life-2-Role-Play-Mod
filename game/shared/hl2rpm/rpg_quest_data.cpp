//========= HL2RPM ============//
//
// RPG-система: квесты v2 - загрузка scripts/quests/*.txt и чтение состояния из флагов.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_quest_data.h"
#include "hl2rpm/rpg_character_data.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CRPGQuestData g_RPGQuestData;

CRPGQuestData &RPGQuestData()
{
	return g_RPGQuestData;
}

static const char *s_pszQuestCategories[RPG_QCAT_COUNT] = { "main", "faction", "side", "companion" };
static const char *s_pszObjectiveTypes[RPG_OBJ_COUNT] = { "talk", "reach", "event", "kill", "item", "flag" };

const char *CRPGQuestData::CategoryKey( int cat )
{
	return ( cat >= 0 && cat < RPG_QCAT_COUNT ) ? s_pszQuestCategories[cat] : "side";
}

int CRPGQuestData::CategoryFromKey( const char *pszKey )
{
	for ( int i = 0; i < RPG_QCAT_COUNT; ++i )
	{
		if ( pszKey && !Q_stricmp( pszKey, s_pszQuestCategories[i] ) )
			return i;
	}
	return -1;
}

int CRPGQuestData::ObjectiveTypeFromKey( const char *pszKey )
{
	for ( int i = 0; i < RPG_OBJ_COUNT; ++i )
	{
		if ( pszKey && !Q_stricmp( pszKey, s_pszObjectiveTypes[i] ) )
			return i;
	}
	return -1;
}

static void SplitTargets( const char *pszList, CUtlVector<CUtlString> &out )
{
	out.RemoveAll();
	if ( !pszList )
		return;
	CUtlVector<char *> parts;
	V_SplitString( pszList, ";", parts );
	for ( int i = 0; i < parts.Count(); ++i )
	{
		char *p = parts[i];
		while ( *p == ' ' )
			++p;
		int len = Q_strlen( p );
		while ( len > 0 && p[len - 1] == ' ' )
			p[--len] = '\0';
		if ( p[0] )
			out.AddToTail( CUtlString( p ) );
	}
	parts.PurgeAndDeleteElements();
}

bool CRPGQuestData::ParseQuest( KeyValues *pKV, const char *pszFile )
{
	const char *pszId = pKV->GetString( "id", "" );
	if ( !pszId[0] )
	{
		Warning( "[rpg] %s: quest without \"id\"\n", pszFile );
		return false;
	}
	if ( Find( pszId ) )
	{
		Warning( "[rpg] %s: quest '%s' defined twice\n", pszFile, pszId );
		return false;
	}

	RPGQuestDef_t &d = m_Quests[m_Quests.AddToTail()];
	d.id = pszId;
	d.title = pKV->GetString( "title", CFmtStr( "#Q_%s_TITLE", pszId ) );
	d.desc = pKV->GetString( "desc", "" );
	d.faction = pKV->GetString( "faction", "" );
	d.category = CategoryFromKey( pKV->GetString( "category", "" ) );
	if ( d.category < 0 )
	{
		// по соглашению имён Part I/16 §3: mqNN_ - главный, sqNN_ - побочный, cqNN_ - спутник, иначе фракционный
		if ( !Q_strnicmp( pszId, "mq", 2 ) )
			d.category = RPG_QCAT_MAIN;
		else if ( !Q_strnicmp( pszId, "sq", 2 ) )
			d.category = RPG_QCAT_SIDE;
		else if ( !Q_strnicmp( pszId, "cq", 2 ) )
			d.category = RPG_QCAT_COMPANION;
		else
			d.category = RPG_QCAT_FACTION;
	}
	d.xp = Max( 0, pKV->GetInt( "xp", 0 ) );
	d.act = pKV->GetInt( "act", 0 );
	d.autoTrack = pKV->GetBool( "auto_track", d.category == RPG_QCAT_MAIN );
	d.completeOnLastStage = pKV->GetBool( "complete_on_last_stage", true );

	if ( KeyValues *pRewards = pKV->FindKey( "rewards" ) )
	{
		for ( KeyValues *pR = pRewards->GetFirstValue(); pR; pR = pR->GetNextValue() )
		{
			RPGQuestDef_t::Reward_t &r = d.rewards[d.rewards.AddToTail()];
			r.classname = pR->GetName();
			r.count = Max( 1, pR->GetInt() );
		}
	}
	if ( KeyValues *p = pKV->FindKey( "on_grant" ) )
		d.onGrant.ParseKV( p );
	if ( KeyValues *p = pKV->FindKey( "on_complete" ) )
		d.onComplete.ParseKV( p );
	if ( KeyValues *p = pKV->FindKey( "on_fail" ) )
		d.onFail.ParseKV( p );

	// стадии по номерам 0..N-1
	if ( KeyValues *pStages = pKV->FindKey( "stages" ) )
	{
		for ( int iStage = 0; ; ++iStage )
		{
			KeyValues *pSt = pStages->FindKey( CFmtStr( "%d", iStage ) );
			if ( !pSt )
				break;
			RPGQuestStage_t &st = d.stages[d.stages.AddToTail()];
			st.title = pSt->GetString( "title", "" );
			st.desc = pSt->GetString( "desc", "" );
			st.marker = pSt->GetString( "marker", "" );
			st.markerMap = pSt->GetString( "marker_map", "" );
			st.xp = Max( 0, pSt->GetInt( "xp", 0 ) );
			if ( KeyValues *p = pSt->FindKey( "on_complete" ) )
				st.onComplete.ParseKV( p );

			if ( KeyValues *pObjs = pSt->FindKey( "objectives" ) )
			{
				for ( KeyValues *pO = pObjs->GetFirstTrueSubKey(); pO; pO = pO->GetNextTrueSubKey() )
				{
					const int type = ObjectiveTypeFromKey( pO->GetString( "type", "" ) );
					if ( type < 0 )
					{
						Warning( "[rpg] %s: quest '%s' stage %d: unknown objective type '%s'\n", pszFile, pszId, iStage, pO->GetString( "type", "" ) );
						continue;
					}
					RPGQuestObjective_t &o = st.objectives[st.objectives.AddToTail()];
					o.type = type;
					o.target = pO->GetString( "target", "" );
					SplitTargets( o.target.Get(), o.targets );
					o.count = Max( 1, pO->GetInt( "count", type == RPG_OBJ_KILL ? Max( 1, o.targets.Count() ) : 1 ) );
					o.optional = pO->GetBool( "optional", false );
					o.title = pO->GetString( "title", "" );
					if ( type == RPG_OBJ_FLAG )
						o.cond.ParseExpression( o.target.Get() );
				}
			}
			// старые ключи kill_* у стадии - цель «убить»
			const char *pszKill = pSt->GetString( "kill_targets", pSt->GetString( "kill_target", "" ) );
			if ( pszKill[0] )
			{
				RPGQuestObjective_t &o = st.objectives[st.objectives.AddToTail()];
				o.type = RPG_OBJ_KILL;
				o.target = pszKill;
				SplitTargets( pszKill, o.targets );
				o.count = Max( 1, pSt->GetInt( "kill_count", o.targets.Count() ) );
				o.optional = false;
			}
		}
	}
	if ( d.stages.Count() == 0 )
	{
		// квест без стадий - одна пустая (выполняется эффектом complete_quest)
		RPGQuestStage_t &st = d.stages[d.stages.AddToTail()];
		st.title = d.title;
		st.desc = d.desc;
		st.xp = 0;
	}
	return true;
}

void CRPGQuestData::Load()
{
	m_Quests.RemoveAll();
	m_bLoaded = true;

	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *pszFile = filesystem->FindFirstEx( "scripts/quests/*.txt", "GAME", &hFind ); pszFile; pszFile = filesystem->FindNext( hFind ) )
	{
		if ( filesystem->FindIsDirectory( hFind ) )
			continue;
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "scripts/quests/%s", pszFile );
		KeyValues *pKV = new KeyValues( "Quest" );
		if ( !pKV->LoadFromFile( filesystem, szPath, "GAME" ) )
		{
			Warning( "[rpg] can't read %s\n", szPath );
			pKV->deleteThis();
			continue;
		}
		if ( !Q_stricmp( pKV->GetName(), "Quests" ) )
		{
			for ( KeyValues *pQ = pKV->GetFirstTrueSubKey(); pQ; pQ = pQ->GetNextTrueSubKey() )
				ParseQuest( pQ, szPath );
		}
		else
			ParseQuest( pKV, szPath );
		pKV->deleteThis();
	}
	filesystem->FindClose( hFind );
	DevMsg( "[rpg] %d quest(s) loaded from scripts/quests\n", m_Quests.Count() );
}

const RPGQuestDef_t *CRPGQuestData::Find( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Quests.Count(); ++i )
	{
		if ( !Q_stricmp( m_Quests[i].id.Get(), pszId ) )
			return &m_Quests[i];
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// Состояние из флагов
// ---------------------------------------------------------------------------
void RPGQuest_FlagName( char *pszOut, int nOut, const char *pszId, const char *pszSuffix )
{
	if ( pszSuffix && pszSuffix[0] )
		Q_snprintf( pszOut, nOut, "q_%s_%s", pszId, pszSuffix );
	else
		Q_snprintf( pszOut, nOut, "q_%s", pszId );
}

int RPGQuest_GetState( const CRPGCharacterData &d, const char *pszId )
{
	char sz[128];
	RPGQuest_FlagName( sz, sizeof( sz ), pszId );
	return d.GetFlagInt( sz, RPG_QUEST_INACTIVE );
}

int RPGQuest_GetStage( const CRPGCharacterData &d, const char *pszId )
{
	char sz[128];
	RPGQuest_FlagName( sz, sizeof( sz ), pszId, "s" );
	return d.GetFlagInt( sz, 0 );
}

int RPGQuest_GetProgress( const CRPGCharacterData &d, const char *pszId, int iObjective )
{
	char sz[128];
	RPGQuest_FlagName( sz, sizeof( sz ), pszId, CFmtStr( "o%d", iObjective ) );
	return d.GetFlagInt( sz, 0 );
}

const char *RPGQuest_GetTracked( const CRPGCharacterData &d )
{
	return d.GetFlagString( RPG_QFLAG_TRACK );
}

//========= HL2RPM ============//
//
// RPG-система (сервер): диалоги v2 - загрузка, разговор, проверки, эффекты, озвучка.
// См. rpg_dialog.h, RPG_SPEC §5, клиент - client/hl2rpm/rpg_dialog_panel.cpp.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_dialog.h"
#include "hl2rpm/rpg_character.h"
#include "hl2rpm/rpg_dialog_shared.h"
#include "hl2rpm/rpg_quests.h"
#include "dialog_definitions.h"
#include "ai_basenpc.h"
#include "ai_baseactor.h"
#include "ai_motor.h"
#include "player.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "recipientfilter.h"
#include "engine/IEngineSound.h"
#include "sceneentity.h"
#include "tier1/fmtstr.h"
#include "utldict.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpg_debug;
static ConVar rpg_dialog_debug( "rpg_dialog_debug", "0", FCVAR_GAMEDLL, "RPG: print dialog v2 flow" );
static ConVar rpg_dialog_vo( "rpg_dialog_vo", "1", FCVAR_GAMEDLL, "RPG: play recorded voice lines (sound/vo/hl2rpm/<language>/<voice>/<id>.wav) when they exist" );
static ConVar rpg_dialog_max_distance( "rpg_dialog_max_distance", "256", FCVAR_GAMEDLL, "RPG: conversation ends when the player walks further than this from the NPC" );

#define DLG_MSG( ... ) do { if ( rpg_dialog_debug.GetBool() ) Msg( "[dialog] " __VA_ARGS__ ); } while ( 0 )

// ===========================================================================
// Данные
// ===========================================================================
struct RPGDlgOption_t
{
	int idx;
	CUtlString text, textLow;
	int next, failNext;
	RPGConditionList_t cond;
	RPGCheck_t check;
	bool bHasCheck;
	CUtlString checkLabel;
	bool checkHidden, checkOnce, inQuest, bExit, bClose, bSpoils;
	CUtlString tag;
	RPGEffectList_t effects, onSuccess, onFail;
	CUtlString grantQuest;	// опция с grant_quest прячется, когда квест уже взят
	bool bOpensUI;			// эффекты открывают экран (торговля, создание персонажа, уровень)
};

struct RPGDlgVariant_t
{
	RPGConditionList_t cond;
	CUtlString line, lineLow, speaker, actor, choreo, sound, voice;
	int autoNext;			// INT_MIN - не задан
	int end;				// -1 - не задан
	RPGEffectList_t effects;
};

struct RPGDlgNode_t
{
	int id;
	CUtlString line, lineLow, speaker, actor, choreo, sound, voice;
	RPGEffectList_t effects;
	CUtlVector<RPGDlgVariant_t> variants;
	CUtlVector<RPGDlgOption_t> options;
	int autoNext;
	bool bEnd, bSpoils;
};

struct RPGDlgGreeting_t
{
	RPGConditionList_t cond;
	int node;
};

struct RPGDialog_t
{
	CUtlString entity, voice, speaker, file;
	bool bWildcard;
	bool bAutoFace;
	float flMaxDistance;	// < 0 - rpg_dialog_max_distance; 0 - без ограничения (сон, голос по радио)
	int startNode;
	CUtlVector<RPGDlgGreeting_t> greetings;
	CUtlVector<RPGDlgNode_t *> nodes;

	~RPGDialog_t() { nodes.PurgeAndDeleteElements(); }

	const RPGDlgNode_t *FindNode( int id ) const
	{
		for ( int i = 0; i < nodes.Count(); ++i )
		{
			if ( nodes[i]->id == id )
				return nodes[i];
		}
		return NULL;
	}
};

static CUtlVector<RPGDialog_t *> s_Dialogs;
static CUtlDict<int, int> s_DialogIndex( k_eDictCompareTypeCaseInsensitive );
static bool s_bLoaded = false;

// ===========================================================================
// Разбор
// ===========================================================================
static int ParseNodeRef( KeyValues *pKV, const char *pszKey, int iDefault )
{
	KeyValues *p = pKV->FindKey( pszKey );
	if ( !p )
		return iDefault;
	const char *psz = p->GetString();
	if ( !psz[0] )
		return iDefault;
	return atoi( psz );
}

static bool HasEffectType( const RPGEffectList_t &list, int type )
{
	for ( int i = 0; i < list.m_Effects.Count(); ++i )
	{
		if ( list.m_Effects[i].type == type )
			return true;
	}
	return false;
}

static void ParseOption( RPGDlgOption_t &o, KeyValues *pKV, int idx, const char *pszWhere )
{
	o.idx = idx;
	o.text = pKV->GetString( "text", "" );
	o.textLow = pKV->GetString( "text_low_ana", "" );
	o.next = ParseNodeRef( pKV, "next", -1 );
	o.failNext = ParseNodeRef( pKV, "fail_next", -1 );
	o.cond.ParseKV( pKV );
	const char *pszCheck = pKV->GetString( "check", "" );
	o.bHasCheck = false;
	if ( pszCheck[0] )
	{
		CUtlString err;
		o.bHasCheck = o.check.Parse( pszCheck, &err );
		if ( !o.bHasCheck )
			Warning( "[dialog] %s option %d: bad check '%s' (%s)\n", pszWhere, idx, pszCheck, err.Get() );
	}
	o.checkLabel = pKV->GetString( "check_label", "" );
	o.checkHidden = pKV->GetBool( "check_hidden" );
	o.checkOnce = pKV->GetBool( "check_once" );
	o.inQuest = pKV->GetBool( "in_quest" );
	o.bExit = pKV->GetBool( "exit" );
	o.bClose = pKV->GetBool( "close" ) || o.bExit;
	o.bSpoils = pKV->GetBool( "spoils" );
	o.tag = pKV->GetString( "tag", "" );
	o.effects.ParseKV( pKV );
	o.onSuccess.ParseKV( pKV, "on_success_" );
	o.onFail.ParseKV( pKV, "on_fail_" );
	o.grantQuest = pKV->GetString( "grant_quest", "" );
	o.bOpensUI = HasEffectType( o.effects, RPG_EFF_OPEN_BARTER ) || HasEffectType( o.effects, RPG_EFF_OPEN_CHARGEN ) || HasEffectType( o.effects, RPG_EFF_OPEN_LEVELUP )
		|| HasEffectType( o.onSuccess, RPG_EFF_OPEN_BARTER ) || HasEffectType( o.onSuccess, RPG_EFF_OPEN_CHARGEN );
}

static void ParseVariant( RPGDlgVariant_t &v, KeyValues *pKV )
{
	v.cond.ParseKV( pKV );
	v.line = pKV->GetString( "line", "" );
	v.lineLow = pKV->GetString( "line_low_ana", "" );
	v.speaker = pKV->GetString( "speaker", "" );
	v.actor = pKV->GetString( "actor", "" );
	v.choreo = pKV->GetString( "choreo", "" );
	v.sound = pKV->GetString( "sound", "" );
	v.voice = pKV->GetString( "voice", "" );
	v.autoNext = pKV->FindKey( "auto_next" ) ? ParseNodeRef( pKV, "auto_next", -1 ) : INT_MIN;
	v.end = ( pKV->FindKey( "end" ) || pKV->FindKey( "close" ) ) ? ( pKV->GetBool( "end" ) || pKV->GetBool( "close" ) ) : -1;
	if ( KeyValues *pEff = pKV->FindKey( "effects" ) )
		v.effects.ParseKV( pEff );
}

static void ParseNode( RPGDlgNode_t &n, KeyValues *pKV, const char *pszWhere )
{
	n.id = atoi( pKV->GetName() );
	n.line = pKV->GetString( "line", "" );
	n.lineLow = pKV->GetString( "line_low_ana", "" );
	n.speaker = pKV->GetString( "speaker", "" );
	n.actor = pKV->GetString( "actor", "" );
	n.choreo = pKV->GetString( "choreo", "" );
	n.sound = pKV->GetString( "sound", "" );
	n.voice = pKV->GetString( "voice", "" );
	n.autoNext = ParseNodeRef( pKV, "auto_next", -1 );
	n.bEnd = pKV->GetBool( "end" ) || pKV->GetBool( "close" );
	n.bSpoils = pKV->GetBool( "spoils" );
	if ( KeyValues *pEff = pKV->FindKey( "effects" ) )
		n.effects.ParseKV( pEff );
	if ( KeyValues *pVars = pKV->FindKey( "variants" ) )
	{
		for ( KeyValues *pV = pVars->GetFirstTrueSubKey(); pV; pV = pV->GetNextTrueSubKey() )
			ParseVariant( n.variants[n.variants.AddToTail()], pV );
	}
	if ( KeyValues *pOpts = pKV->FindKey( "options" ) )
	{
		// порядок показа: 1, 2, 3 ... затем 0 (выход) - как в Part V (элементы не копируются: вставка на место)
		for ( KeyValues *pO = pOpts->GetFirstTrueSubKey(); pO; pO = pO->GetNextTrueSubKey() )
		{
			const int idx = atoi( pO->GetName() );
			const int key = idx == 0 ? INT_MAX : idx;
			int pos = n.options.Count();
			for ( int k = 0; k < n.options.Count(); ++k )
			{
				const int kk = n.options[k].idx == 0 ? INT_MAX : n.options[k].idx;
				if ( key < kk )
				{
					pos = k;
					break;
				}
			}
			ParseOption( n.options[n.options.InsertBefore( pos )], pO, idx, pszWhere );
		}
	}
}

bool RPGDialog::IsV2File( KeyValues *pKV )
{
	return pKV && ( pKV->FindKey( "voice" ) || pKV->FindKey( "greetings" ) || pKV->FindKey( "speaker" ) );
}

static void ClearDialogs()
{
	s_Dialogs.PurgeAndDeleteElements();
	s_DialogIndex.RemoveAll();
}

void RPGDialog::Load()
{
	ClearDialogs();
	s_bLoaded = true;
	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *pszFile = filesystem->FindFirstEx( "scripts/dialogs/*.txt", "MOD", &hFind ); pszFile; pszFile = filesystem->FindNext( hFind ) )
	{
		if ( filesystem->FindIsDirectory( hFind ) )
			continue;
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "scripts/dialogs/%s", pszFile );
		KeyValues *pKV = new KeyValues( "Dialog" );
		if ( !pKV->LoadFromFile( filesystem, szPath, "MOD" ) )
		{
			pKV->deleteThis();
			continue;
		}
		if ( !IsV2File( pKV ) )
		{
			pKV->deleteThis();
			continue;
		}
		RPGDialog_t *pDlg = new RPGDialog_t;
		pDlg->file = szPath;
		char szStem[MAX_PATH];
		Q_FileBase( pszFile, szStem, sizeof( szStem ) );
		pDlg->entity = pKV->GetString( "entity", szStem );
		pDlg->voice = pKV->GetString( "voice", "" );
		pDlg->speaker = pKV->GetString( "speaker", "" );
		pDlg->bAutoFace = pKV->GetBool( "auto_face_npc", true );
		pDlg->flMaxDistance = pKV->GetFloat( "max_distance", -1.0f );
		pDlg->startNode = pKV->GetInt( "start_node", 0 );
		const int len = pDlg->entity.Length();
		pDlg->bWildcard = len > 0 && pDlg->entity.Get()[len - 1] == '*';
		if ( KeyValues *pGreet = pKV->FindKey( "greetings" ) )
		{
			for ( KeyValues *pG = pGreet->GetFirstTrueSubKey(); pG; pG = pG->GetNextTrueSubKey() )
			{
				RPGDlgGreeting_t &g = pDlg->greetings[pDlg->greetings.AddToTail()];
				g.cond.ParseKV( pG );
				g.node = pG->GetInt( "node", pDlg->startNode );
			}
		}
		if ( KeyValues *pNodes = pKV->FindKey( "nodes" ) )
		{
			for ( KeyValues *pN = pNodes->GetFirstTrueSubKey(); pN; pN = pN->GetNextTrueSubKey() )
			{
				RPGDlgNode_t *pNode = new RPGDlgNode_t;
				ParseNode( *pNode, pN, szPath );
				pDlg->nodes.AddToTail( pNode );
			}
		}
		pKV->deleteThis();

		const int iExisting = s_DialogIndex.Find( pDlg->entity.Get() );
		if ( iExisting != s_DialogIndex.InvalidIndex() )
		{
			Warning( "[dialog] %s: entity '%s' already has a dialog (%s) - replaced\n", szPath, pDlg->entity.Get(), s_Dialogs[s_DialogIndex[iExisting]]->file.Get() );
			delete s_Dialogs[s_DialogIndex[iExisting]];
			s_Dialogs[s_DialogIndex[iExisting]] = pDlg;
		}
		else
			s_DialogIndex.Insert( pDlg->entity.Get(), s_Dialogs.AddToTail( pDlg ) );
	}
	filesystem->FindClose( hFind );
	DevMsg( "[dialog] %d dialog v2 file(s) loaded\n", s_Dialogs.Count() );
}

static const RPGDialog_t *FindDialog( const char *pszEntity )
{
	if ( !s_bLoaded )
		RPGDialog::Load();
	if ( !pszEntity || !pszEntity[0] )
		return NULL;
	const int i = s_DialogIndex.Find( pszEntity );
	if ( i != s_DialogIndex.InvalidIndex() )
		return s_Dialogs[s_DialogIndex[i]];
	// шаблоны: самый длинный подходящий префикс
	const RPGDialog_t *pBest = NULL;
	int bestLen = -1;
	for ( int d = 0; d < s_Dialogs.Count(); ++d )
	{
		const RPGDialog_t *p = s_Dialogs[d];
		if ( !p->bWildcard )
			continue;
		const int len = p->entity.Length() - 1;
		if ( len > bestLen && !Q_strnicmp( p->entity.Get(), pszEntity, len ) )
		{
			pBest = p;
			bestLen = len;
		}
	}
	return pBest;
}

bool RPGDialog::Has( const char *pszEntityName )
{
	return FindDialog( pszEntityName ) != NULL;
}

// диалог для NPC: заданный явно, свой по targetname, общий по rpg_dialog, реплика фракции
static const char *DefaultBarkFor( CAI_BaseNPC *pAI )
{
	const bool bFemale = Q_stristr( STRING( pAI->GetModelName() ), "female" ) != NULL;
	const char *pszFaction = pAI->GetRPGFaction();
	if ( pszFaction && pszFaction[0] )
	{
		if ( !Q_stricmp( pszFaction, "cp" ) ) return "_bark_cp";
		if ( !Q_stricmp( pszFaction, "vort" ) || !Q_stricmp( pszFaction, "vortigaunts" ) ) return "_bark_vort";
		if ( !Q_stricmp( pszFaction, "overwatch" ) ) return NULL;		// Надзор не разговаривает
		return bFemale ? "_bark_citizen_f" : "_bark_citizen";
	}
	if ( pAI->ClassMatches( "npc_citizen" ) ) return bFemale ? "_bark_citizen_f" : "_bark_citizen";
	if ( pAI->ClassMatches( "npc_metropolice" ) ) return "_bark_cp";
	if ( pAI->ClassMatches( "npc_vortigaunt" ) ) return "_bark_vort";
	return NULL;
}

static const RPGDialog_t *ResolveDialog( CBaseEntity *pNPC, const char *pszOverride = NULL )
{
	if ( !pNPC )
		return NULL;
	if ( pszOverride && pszOverride[0] )
		return FindDialog( pszOverride );
	const RPGDialog_t *pDlg = FindDialog( STRING( pNPC->GetEntityName() ) );
	if ( pDlg )
		return pDlg;
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	if ( !pAI )
		return NULL;
	if ( pAI->GetRPGDialog()[0] && ( pDlg = FindDialog( pAI->GetRPGDialog() ) ) != NULL )
		return pDlg;
	const char *pszBark = DefaultBarkFor( pAI );
	return pszBark ? FindDialog( pszBark ) : NULL;
}

bool RPGDialog::HasFor( CBaseEntity *pNPC )
{
	return ResolveDialog( pNPC ) != NULL;
}

// ===========================================================================
// Разговор
// ===========================================================================
struct RPGConversation_t
{
	bool bActive;
	CHandle<CBaseEntity> hNPC;
	CUtlString entity;			// targetname собеседника
	const RPGDialog_t *pDlg;
	int node;
	// реплика сейчас звучит из этого актёра
	CHandle<CBaseEntity> hSpeaking;
	CUtlString speakingSound;
	// возврат после экрана интерфейса
	bool bResumePending;
	CHandle<CBaseEntity> hResumeNPC;
	int resumeNode;

	void Reset()
	{
		bActive = false;
		hNPC = NULL;
		entity.Clear();
		pDlg = NULL;
		node = -1;
		hSpeaking = NULL;
		speakingSound.Clear();
	}
};
static RPGConversation_t s_Conv;

static bool IsLowAnalytics()
{
	return RPG_GetCharacter()->Data().GetAttr( RPG_ATTR_ANALYTICS ) <= 3;
}

static void SendBegin( CBasePlayer *pPlayer, CBaseEntity *pNPC, const RPGDialog_t *pDlg, const char *pszSpeaker )
{
	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	UserMessageBegin( filter, "RPG_Dialog" );
		WRITE_BYTE( RPG_DLGMSG_BEGIN );
		WRITE_SHORT( pNPC ? pNPC->entindex() : -1 );
		WRITE_STRING( s_Conv.entity.Get() );
		WRITE_STRING( pszSpeaker ? pszSpeaker : "" );
		WRITE_BYTE( pDlg->bAutoFace ? RPG_DLGF_AUTOFACE : 0 );
	MessageEnd();
}

static void SendEnd( CBasePlayer *pPlayer )
{
	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	UserMessageBegin( filter, "RPG_Dialog" );
		WRITE_BYTE( RPG_DLGMSG_END );
	MessageEnd();
}

static void SendLongString( CBasePlayer *pPlayer, const char *pszText )
{
	// режем по границе UTF-8
	const int len = Q_strlen( pszText );
	int pos = 0;
	while ( pos < len )
	{
		int n = Min( RPG_DLG_MAX_LINE_CHUNK, len - pos );
		while ( n > 0 && pos + n < len && ( (unsigned char)pszText[pos + n] & 0xC0 ) == 0x80 )
			--n;
		char chunk[RPG_DLG_MAX_LINE_CHUNK + 1];
		Q_strncpy( chunk, pszText + pos, n + 1 );
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_Dialog" );
			WRITE_BYTE( RPG_DLGMSG_LINE );
			WRITE_STRING( chunk );
		MessageEnd();
		pos += n;
	}
}

static const RPGDialog_t *FindDialog( const char *pszEntity );

static const char *SpeakerFor( CBaseEntity *pNPC, const RPGDialog_t *pDlg )
{
	CAI_BaseNPC *pAI = pNPC ? pNPC->MyNPCPointer() : NULL;
	// общий диалог (_bark_*, _law_stop): имя самого NPC - loot_displayname карты или подпись его своего диалога
	if ( pDlg->entity.Get()[0] == '_' && pNPC )
	{
		if ( pAI && pAI->GetLootDisplayName()[0] )
			return pAI->GetLootDisplayName();
		const RPGDialog_t *pOwn = FindDialog( STRING( pNPC->GetEntityName() ) );
		if ( pOwn && pOwn != pDlg && pOwn->speaker.Length() )
			return pOwn->speaker.Get();
	}
	if ( pDlg->speaker.Length() )
		return pDlg->speaker.Get();
	if ( pAI && pAI->GetLootDisplayName()[0] )
		return pAI->GetLootDisplayName();
	return "";
}

const char *RPGDialog::GetSpeaker( CBaseEntity *pNPC )
{
	if ( !pNPC )
		return NULL;
	const char *pszName = STRING( pNPC->GetEntityName() );
	const RPGDialog_t *pDlg = ResolveDialog( pNPC );
	if ( !pDlg )
		return NULL;
	if ( pszName && pszName[0] && RPG_GetCharacter()->Data().IsFlagSet( CFmtStr( "dlg_spoiled_%s", pszName ) ) )
		return NULL;
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	if ( pAI && ( !pAI->IsAlive() || ( UTIL_GetLocalPlayer() && pAI->IRelationType( UTIL_GetLocalPlayer() ) == D_HT ) ) )
		return NULL;
	return SpeakerFor( pNPC, pDlg );
}

static void StopSpeaking()
{
	if ( s_Conv.hSpeaking.Get() && s_Conv.speakingSound.Length() )
		s_Conv.hSpeaking->StopSound( s_Conv.hSpeaking->entindex(), CHAN_VOICE, s_Conv.speakingSound.Get() );
	s_Conv.hSpeaking = NULL;
	s_Conv.speakingSound.Clear();
}

// записанная озвучка реплики: sound/vo/hl2rpm/<язык>/<голос>/<ID>.wav
static bool FindVO( CBasePlayer *pPlayer, const char *pszLineToken, const char *pszVoice, char *pszOut, int nOut )
{
	if ( !rpg_dialog_vo.GetBool() || !pszLineToken || pszLineToken[0] != '#' || !pszVoice || !pszVoice[0] )
		return false;
	const char *pszLang = engine->GetClientConVarValue( pPlayer->entindex(), "cl_language" );
	if ( !pszLang || !pszLang[0] )
		pszLang = "english";
	Q_snprintf( pszOut, nOut, "vo/hl2rpm/%s/%s/%s.wav", pszLang, pszVoice, pszLineToken + 1 );
	if ( filesystem->FileExists( CFmtStr( "sound/%s", pszOut ), "GAME" ) )
		return true;
	return false;
}

static float PlayLine( CBasePlayer *pPlayer, CBaseEntity *pActor, const char *pszSound, const char *pszChoreo )
{
	StopSpeaking();
	if ( !pActor )
		return -1.0f;
	if ( pszChoreo && pszChoreo[0] )
	{
		CBaseFlex *pFlex = dynamic_cast<CBaseFlex *>( pActor );
		if ( pFlex )
		{
			char szScene[MAX_PATH];
			Q_strncpy( szScene, pszChoreo, sizeof( szScene ) );
			if ( !Q_stristr( szScene, ".vcd" ) )
				Q_strncat( szScene, ".vcd", sizeof( szScene ) );
			const float flDur = InstancedScriptedScene( pFlex, szScene );
			return flDur > 0.0f ? flDur : -1.0f;
		}
	}
	if ( !pszSound || !pszSound[0] )
		return -1.0f;
	CPASAttenuationFilter filter( pActor, SNDLVL_TALKING );
	EmitSound_t ep;
	ep.m_nChannel = CHAN_VOICE;
	ep.m_pSoundName = pszSound;
	ep.m_flVolume = 1.0f;
	ep.m_SoundLevel = SNDLVL_TALKING;
	CBaseEntity::EmitSound( filter, pActor->entindex(), ep );
	s_Conv.hSpeaking = pActor;
	s_Conv.speakingSound = pszSound;
	float flDur = -1.0f;
	if ( Q_stristr( pszSound, ".wav" ) || Q_stristr( pszSound, ".mp3" ) )
		flDur = enginesound->GetSoundDuration( pszSound );
	else
		flDur = CBaseEntity::GetSoundDuration( pszSound, STRING( pActor->GetModelName() ) );
	return flDur > 0.0f ? flDur : -1.0f;
}

static void FaceEachOther( CBasePlayer *pPlayer, CBaseEntity *pActor, float flDuration )
{
	CAI_BaseNPC *pAI = pActor ? pActor->MyNPCPointer() : NULL;
	if ( !pAI || !pAI->IsAlive() )
		return;
	CAI_BaseActor *pBaseActor = dynamic_cast<CAI_BaseActor *>( pAI );
	if ( pBaseActor )
		pBaseActor->AddLookTarget( pPlayer, 1.0f, flDuration, 0.3f );
	if ( pAI->GetState() != NPC_STATE_SCRIPT && !pAI->IsMoving() && pAI->GetMotor() )
		pAI->GetMotor()->SetIdealYawToTarget( pPlayer->WorldSpaceCenter() );
}

static void LabelForCheck( const RPGDlgOption_t &o, const RPGCheckResult_t &res, char *pszOut, int nOut )
{
	if ( o.checkLabel.Length() )
	{
		Q_snprintf( pszOut, nOut, "custom:%s:%d", o.checkLabel.Get(), res.threshold );
		return;
	}
	const RPGCheck_t &c = o.check;
	const RPGCheckPart_t &pt = c.parts[clamp( res.part, 0, Max( 0, c.numParts - 1 ) )];
	switch ( c.kind )
	{
	case RPG_CHECK_SKILL:
	case RPG_CHECK_ATTR:
	{
		const char *pszId = pt.bAttr ? ( pt.index >= 0 && pt.index < RPG_ATTR_COUNT ? g_pszRPGAttrIds[pt.index] : "?" )
			: ( pt.index >= 0 && pt.index < RPGData().GetSkillCount() ? RPGData().GetSkill( pt.index ).id.Get() : "?" );
		Q_snprintf( pszOut, nOut, "%s:%s:%d", pt.bAttr ? "attr" : "skill", pszId, pt.threshold );
		break;
	}
	case RPG_CHECK_BRIBE:
		Q_snprintf( pszOut, nOut, "bribe::%d", res.cost );
		break;
	default:
		Q_snprintf( pszOut, nOut, "%s::%d", RPGCheck_t::KindName( c.kind ), pt.threshold );
		break;
	}
}

// метка перед текстом опции: из «tag» или по первому условию
static void TagFor( const RPGDlgOption_t &o, char *pszOut, int nOut )
{
	pszOut[0] = '\0';
	if ( o.tag.Length() )
	{
		Q_strncpy( pszOut, o.tag.Get(), nOut );
		return;
	}
	for ( int t = 0; t < o.cond.m_Terms.Count(); ++t )
	{
		const RPGConditionTerm_t &term = o.cond.m_Terms[t];
		if ( term.alternatives.Count() == 0 )
			continue;
		const RPGCondition_t &c = term.alternatives[0];
		switch ( c.type )
		{
		case RPG_COND_FACTION:	Q_snprintf( pszOut, nOut, "faction:%s", c.id.Get() ); return;
		case RPG_COND_CIVIL:	Q_strncpy( pszOut, "civil", nOut ); return;
		case RPG_COND_PERK:		Q_snprintf( pszOut, nOut, "perk:%s", c.id.Get() ); return;
		case RPG_COND_TRAIT:	Q_snprintf( pszOut, nOut, "trait:%s", c.id.Get() ); return;
		case RPG_COND_ITEM:		Q_snprintf( pszOut, nOut, "item:%s", c.id.Get() ); return;
		case RPG_COND_RESONANCE: Q_strncpy( pszOut, "resonance", nOut ); return;
		case RPG_COND_HUMANITY:	Q_strncpy( pszOut, ( c.op == RPG_OP_LE || c.op == RPG_OP_LT ) ? "cruelty" : "compassion", nOut ); return;
		default: break;
		}
	}
}

static void CheckOnceFlag( char *pszOut, int nOut, int node, int idx )
{
	Q_snprintf( pszOut, nOut, "chk_%s_%d_%d", s_Conv.entity.Get(), node, idx );
}

// опция видна? met - можно выбрать
static bool EvaluateOption( CBasePlayer *pPlayer, CBaseEntity *pNPC, const RPGDlgOption_t &o, int node, int &state, RPGCheckResult_t *pCheckRes )
{
	const RPGEvalContext_t ctx = RPG_MakeEvalContext( pPlayer, pNPC );
	RPGEvalResult_t ev = o.cond.Evaluate( ctx );
	if ( !ev.visible )
		return false;
	if ( o.grantQuest.Length() )
	{
		int iStage = 0;
		const int qs = RPGQuests::IsV2( o.grantQuest.Get() ) ? RPGQuests::GetState( o.grantQuest.Get() ) : RPG_GetWorldQuery()->GetQuestState( pPlayer, o.grantQuest.Get(), iStage );
		if ( qs == RPG_QUEST_ACTIVE || qs == RPG_QUEST_COMPLETED )
			return false;
	}
	state = o.bExit ? RPG_DLGOPT_EXIT : RPG_DLGOPT_NORMAL;
	if ( !ev.met )
	{
		state = RPG_DLGOPT_UNMET;
		return true;
	}
	if ( o.bHasCheck )
	{
		char szFlag[160];
		CheckOnceFlag( szFlag, sizeof( szFlag ), node, o.idx );
		if ( o.checkOnce && RPG_GetCharacter()->Data().IsFlagSet( szFlag ) )
		{
			state = RPG_DLGOPT_DONE;
			return true;
		}
		RPGCheckResult_t res = RPG_EvaluateCheck( o.check, ctx, o.inQuest );
		if ( pCheckRes )
			*pCheckRes = res;
		if ( !o.checkHidden )
			state = res.pass ? RPG_DLGOPT_CHECK_PASS : RPG_DLGOPT_CHECK_FAIL;
	}
	return true;
}

static void EndConversation( CBasePlayer *pPlayer, bool bSendEnd = true );

static void ShowNode( CBasePlayer *pPlayer, int nodeId )
{
	const RPGDialog_t *pDlg = s_Conv.pDlg;
	CBaseEntity *pNPC = s_Conv.hNPC.Get();
	const RPGDlgNode_t *pNode = pDlg ? pDlg->FindNode( nodeId ) : NULL;
	if ( !pNode )
	{
		Warning( "[dialog] %s: node %d not found\n", pDlg ? pDlg->file.Get() : "?", nodeId );
		EndConversation( pPlayer );
		return;
	}
	s_Conv.node = nodeId;
	DLG_MSG( "%s: node %d\n", s_Conv.entity.Get(), nodeId );

	// эффекты узла и первого подходящего варианта
	pNode->effects.Apply( pPlayer, pNPC, pNPC );
	const RPGEvalContext_t ctx = RPG_MakeEvalContext( pPlayer, pNPC );
	const RPGDlgVariant_t *pVar = NULL;
	for ( int i = 0; i < pNode->variants.Count(); ++i )
	{
		if ( pNode->variants[i].cond.Evaluate( ctx ).met )
		{
			pVar = &pNode->variants[i];
			break;
		}
	}
	if ( pVar )
		pVar->effects.Apply( pPlayer, pNPC, pNPC );
	if ( !s_Conv.bActive )
		return;		// эффекты закончили разговор (бой, экран)

	const bool bLow = IsLowAnalytics();
	const char *pszLine = pNode->line.Get();
	const char *pszLineLow = pNode->lineLow.Get();
	if ( pVar && pVar->line.Length() )
	{
		pszLine = pVar->line.Get();
		pszLineLow = pVar->lineLow.Get();
	}
	if ( bLow && pszLineLow && pszLineLow[0] )
		pszLine = pszLineLow;
	const char *pszSpeaker = ( pVar && pVar->speaker.Length() ) ? pVar->speaker.Get() : pNode->speaker.Get();
	const char *pszActorName = ( pVar && pVar->actor.Length() ) ? pVar->actor.Get() : pNode->actor.Get();
	const char *pszVoice = ( pVar && pVar->voice.Length() ) ? pVar->voice.Get() : ( pNode->voice.Length() ? pNode->voice.Get() : pDlg->voice.Get() );
	const char *pszSound = ( pVar && pVar->sound.Length() ) ? pVar->sound.Get() : pNode->sound.Get();
	const char *pszChoreo = ( pVar && pVar->choreo.Length() ) ? pVar->choreo.Get() : pNode->choreo.Get();
	const int autoNext = ( pVar && pVar->autoNext != INT_MIN ) ? pVar->autoNext : pNode->autoNext;
	const bool bEnd = ( pVar && pVar->end >= 0 ) ? ( pVar->end != 0 ) : pNode->bEnd;

	CBaseEntity *pActor = pNPC;
	if ( pszActorName && pszActorName[0] )
	{
		CBaseEntity *p = gEntList.FindEntityByName( NULL, pszActorName );
		if ( p )
			pActor = p;
	}

	// записанная озвучка этой реплики сильнее «sound» (ванильная реплика остаётся запасной)
	char szVO[MAX_PATH];
	if ( FindVO( pPlayer, pszLine, pszVoice, szVO, sizeof( szVO ) ) )
		pszSound = szVO;
	const float flDur = PlayLine( pPlayer, pActor, pszSound, pszSound && pszSound[0] ? NULL : pszChoreo );
	FaceEachOther( pPlayer, pActor, Max( 6.0f, flDur + 2.0f ) );

	// реплика: токен/короткий текст - в NODE, длинный текст - частями
	const bool bLong = Q_strlen( pszLine ) > RPG_DLG_MAX_LINE_CHUNK;
	if ( bLong )
		SendLongString( pPlayer, pszLine );

	{
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_Dialog" );
			WRITE_BYTE( RPG_DLGMSG_NODE );
			WRITE_SHORT( nodeId );
			WRITE_STRING( bLong ? "" : pszLine );
			WRITE_STRING( pszSpeaker ? pszSpeaker : "" );
			WRITE_SHORT( autoNext );
			WRITE_BYTE( bEnd ? 1 : 0 );
			WRITE_FLOAT( flDur );
			WRITE_SHORT( pActor ? pActor->entindex() : -1 );
		MessageEnd();
	}

	// варианты ответа
	for ( int i = 0; i < pNode->options.Count(); ++i )
	{
		const RPGDlgOption_t &o = pNode->options[i];
		int state = RPG_DLGOPT_NORMAL;
		RPGCheckResult_t res;
		if ( !EvaluateOption( pPlayer, pNPC, o, nodeId, state, &res ) )
			continue;
		char szLabel[96] = { 0 };
		if ( o.bHasCheck && !o.checkHidden )
			LabelForCheck( o, res, szLabel, sizeof( szLabel ) );
		char szTag[96];
		TagFor( o, szTag, sizeof( szTag ) );
		const char *pszText = ( bLow && o.textLow.Length() ) ? o.textLow.Get() : o.text.Get();
		char szText[RPG_DLG_MAX_LINE_CHUNK + 1];
		Q_strncpy( szText, pszText, sizeof( szText ) );	// опции длиннее 200 байт обрезаются (build_content предупреждает)

		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPG_Dialog" );
			WRITE_BYTE( RPG_DLGMSG_OPTION );
			WRITE_BYTE( o.idx );
			WRITE_BYTE( state );
			WRITE_STRING( szText );
			WRITE_STRING( szLabel );
			WRITE_STRING( szTag );
		MessageEnd();
	}

	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	UserMessageBegin( filter, "RPG_Dialog" );
		WRITE_BYTE( RPG_DLGMSG_SHOW );
	MessageEnd();

	if ( pNode->bSpoils )
		RPG_GetCharacter()->SetFlag( CFmtStr( "dlg_spoiled_%s", s_Conv.entity.Get() ), 1 );
}

static void EndConversation( CBasePlayer *pPlayer, bool bSendEnd )
{
	if ( !s_Conv.bActive )
		return;
	const CUtlString entity = s_Conv.entity;
	DLG_MSG( "%s: end\n", entity.Get() );
	s_Conv.bActive = false;
	if ( bSendEnd && pPlayer )
		SendEnd( pPlayer );
	s_Conv.Reset();
	if ( entity.IsEmpty() )
		return;		// безымянный генерик (общая реплика): ни «уже говорили», ни событий квеста
	CRPGCharacter *pChar = RPG_GetCharacter();
	pChar->SetFlag( CFmtStr( "dlg_spoken_%s", entity.Get() ), 1 );
	// квесты: «поговорить с ...»
	if ( pPlayer )
		RPG_QuestEvent( pPlayer, "talk", entity.Get() );
}

bool RPGDialog::IsActive()
{
	return s_Conv.bActive;
}

CBaseEntity *RPGDialog::GetActiveNPC()
{
	return s_Conv.bActive ? s_Conv.hNPC.Get() : NULL;
}

void RPGDialog::Stop()
{
	EndConversation( RPG_GetPlayer() );
}

bool RPGDialog::Start( CBasePlayer *pPlayer, CBaseEntity *pNPC, int iNode, const char *pszDialog )
{
	if ( !pPlayer || !pNPC )
		return false;
	const char *pszName = STRING( pNPC->GetEntityName() );
	const RPGDialog_t *pDlg = ResolveDialog( pNPC, pszDialog );
	if ( !pDlg )
		return false;
	// второе нажатие E / второй обработчик +use в том же разговоре - не начинать заново
	if ( s_Conv.bActive && s_Conv.hNPC.Get() == pNPC && s_Conv.pDlg == pDlg && iNode < 0 )
		return true;

	CRPGCharacter *pChar = RPG_GetCharacter();
	// испорчен: по флагу (наш) или по старому списку (урон по NPC, вход SpoilDialog)
	if ( pszName && pszName[0] )
	{
		if ( DialogDefinitions::IsEntitySpoiled( pszName ) )
			pChar->SetFlag( CFmtStr( "dlg_spoiled_%s", pszName ), 1 );
		if ( pChar->Data().IsFlagSet( CFmtStr( "dlg_spoiled_%s", pszName ) ) )
			return false;
	}
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	if ( pAI && ( !pAI->IsAlive() || pAI->IRelationType( pPlayer ) == D_HT ) )
		return false;

	if ( s_Conv.bActive )
		EndConversation( pPlayer );

	s_Conv.Reset();
	s_Conv.bActive = true;
	s_Conv.hNPC = pNPC;
	s_Conv.entity = pszName;
	s_Conv.pDlg = pDlg;
	s_Conv.bResumePending = false;

	int node = iNode;
	if ( node < 0 )
	{
		node = pDlg->startNode;
		const RPGEvalContext_t ctx = RPG_MakeEvalContext( pPlayer, pNPC );
		for ( int i = 0; i < pDlg->greetings.Count(); ++i )
		{
			if ( pDlg->greetings[i].cond.Evaluate( ctx ).met )
			{
				node = pDlg->greetings[i].node;
				break;
			}
		}
	}
	DLG_MSG( "start %s (%s) at node %d\n", pszName, pDlg->file.Get(), node );
	SendBegin( pPlayer, pNPC, pDlg, SpeakerFor( pNPC, pDlg ) );
	ShowNode( pPlayer, node );
	return true;
}

// ===========================================================================
// Ответы клиента
// ===========================================================================
static void Choose( CBasePlayer *pPlayer, int nodeId, int optIdx )
{
	if ( !s_Conv.bActive || !s_Conv.pDlg || nodeId != s_Conv.node )
		return;
	const RPGDlgNode_t *pNode = s_Conv.pDlg->FindNode( nodeId );
	if ( !pNode )
		return;
	const RPGDlgOption_t *pOpt = NULL;
	for ( int i = 0; i < pNode->options.Count(); ++i )
	{
		if ( pNode->options[i].idx == optIdx )
		{
			pOpt = &pNode->options[i];
			break;
		}
	}
	if ( !pOpt )
		return;
	CBaseEntity *pNPC = s_Conv.hNPC.Get();
	int state = 0;
	RPGCheckResult_t res;
	if ( !EvaluateOption( pPlayer, pNPC, *pOpt, nodeId, state, &res ) || state == RPG_DLGOPT_UNMET || state == RPG_DLGOPT_DONE )
		return;

	StopSpeaking();
	const RPGDlgOption_t &o = *pOpt;
	int next = o.next;
	// общие эффекты, затем исход проверки
	o.effects.Apply( pPlayer, pNPC, pNPC );
	if ( o.bHasCheck )
	{
		RPG_ApplyCheckOutcome( pPlayer, pNPC, o.check, res );
		if ( res.pass )
			o.onSuccess.Apply( pPlayer, pNPC, pNPC );
		else
		{
			o.onFail.Apply( pPlayer, pNPC, pNPC );
			if ( o.failNext != -1 )
				next = o.failNext;
			if ( o.checkOnce )
			{
				char szFlag[160];
				CheckOnceFlag( szFlag, sizeof( szFlag ), nodeId, o.idx );
				RPG_GetCharacter()->SetFlag( szFlag, 1 );
			}
		}
		DLG_MSG( "%s: check %s -> %s\n", s_Conv.entity.Get(), o.check.KindName( o.check.kind ), res.pass ? "pass" : "fail" );
	}
	if ( o.bSpoils )
		RPG_GetCharacter()->SetFlag( CFmtStr( "dlg_spoiled_%s", s_Conv.entity.Get() ), 1 );
	if ( !s_Conv.bActive )
		return;		// эффекты (бой) закончили разговор

	// экран интерфейса поверх диалога: разговор продолжится, когда экран закроют (rpg_dialog_resume)
	if ( o.bOpensUI )
	{
		s_Conv.bResumePending = next != -1;
		s_Conv.hResumeNPC = pNPC;
		s_Conv.resumeNode = next;
		const bool bResume = s_Conv.bResumePending;
		const CHandle<CBaseEntity> hResume = s_Conv.hResumeNPC;
		const int resumeNode = s_Conv.resumeNode;
		EndConversation( pPlayer );
		s_Conv.bResumePending = bResume;
		s_Conv.hResumeNPC = hResume;
		s_Conv.resumeNode = resumeNode;
		return;
	}

	if ( next != -1 && !o.bClose )
		ShowNode( pPlayer, next );
	else if ( next != -1 && o.bClose )
		ShowNode( pPlayer, next );		// «close» с переходом: показать последнюю реплику (у неё end)
	else
		EndConversation( pPlayer );
}

static void Continue( CBasePlayer *pPlayer, int nodeId )
{
	if ( !s_Conv.bActive || !s_Conv.pDlg || nodeId != s_Conv.node )
		return;
	const RPGDlgNode_t *pNode = s_Conv.pDlg->FindNode( nodeId );
	if ( !pNode )
	{
		EndConversation( pPlayer );
		return;
	}
	CBaseEntity *pNPC = s_Conv.hNPC.Get();
	const RPGEvalContext_t ctx = RPG_MakeEvalContext( pPlayer, pNPC );
	int autoNext = pNode->autoNext;
	for ( int i = 0; i < pNode->variants.Count(); ++i )
	{
		if ( pNode->variants[i].cond.Evaluate( ctx ).met )
		{
			if ( pNode->variants[i].autoNext != INT_MIN )
				autoNext = pNode->variants[i].autoNext;
			break;
		}
	}
	StopSpeaking();
	if ( autoNext != -1 )
		ShowNode( pPlayer, autoNext );
	else
		EndConversation( pPlayer );
}

// ===========================================================================
// Система: загрузка на уровне, разрыв разговора по дистанции/смерти, возврат после экранов
// ===========================================================================
class CRPGDialogSystem : public CAutoGameSystemPerFrame, public IRPGListener
{
public:
	CRPGDialogSystem() : CAutoGameSystemPerFrame( "CRPGDialogSystem" ) {}

	virtual bool Init() { RPG_AddListener( this ); return true; }
	virtual void Shutdown() { RPG_RemoveListener( this ); ClearDialogs(); }

	virtual void LevelInitPreEntity()
	{
		s_Conv.Reset();
		s_Conv.bResumePending = false;
		RPGDialog::Load();
	}

	virtual void LevelShutdownPreEntity()
	{
		s_Conv.Reset();
		s_Conv.bResumePending = false;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( !s_Conv.bActive )
			return;
		CBasePlayer *pPlayer = RPG_GetPlayer();
		CBaseEntity *pNPC = s_Conv.hNPC.Get();
		bool bBreak = !pPlayer || !pPlayer->IsAlive() || !pNPC;
		if ( !bBreak )
		{
			CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
			if ( pAI && ( !pAI->IsAlive() || pAI->IRelationType( pPlayer ) == D_HT ) )
				bBreak = true;
			else
			{
				const float flMax = ( s_Conv.pDlg && s_Conv.pDlg->flMaxDistance >= 0.0f ) ? s_Conv.pDlg->flMaxDistance : rpg_dialog_max_distance.GetFloat();
				if ( flMax > 0.0f && ( pNPC->WorldSpaceCenter() - pPlayer->WorldSpaceCenter() ).Length() > flMax )
					bBreak = true;
			}
		}
		if ( bBreak )
			EndConversation( pPlayer );
	}

	virtual void OnRPGChargenDone( CBasePlayer *pPlayer ) { Resume( pPlayer ); }

	static void Resume( CBasePlayer *pPlayer )
	{
		if ( !s_Conv.bResumePending )
			return;
		s_Conv.bResumePending = false;
		CBaseEntity *pNPC = s_Conv.hResumeNPC.Get();
		if ( pPlayer && pNPC && s_Conv.resumeNode >= 0 )
			RPGDialog::Start( pPlayer, pNPC, s_Conv.resumeNode );
	}
};
static CRPGDialogSystem g_RPGDialogSystem;

// ===========================================================================
// Команды
// ===========================================================================
static CBasePlayer *CmdPlayer()
{
	CBasePlayer *p = UTIL_GetCommandClient();
	return p ? p : UTIL_PlayerByIndex( 1 );
}

CON_COMMAND_F( rpg_dialog_choose, "RPG dialog: choose an option (client UI)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() >= 3 )
		Choose( CmdPlayer(), atoi( args[1] ), atoi( args[2] ) );
}

CON_COMMAND_F( rpg_dialog_continue, "RPG dialog: continue after a line without options (client UI)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( args.ArgC() >= 2 )
		Continue( CmdPlayer(), atoi( args[1] ) );
}

CON_COMMAND_F( rpg_dialog_close, "RPG dialog: close the conversation (client UI)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	if ( s_Conv.bActive )
		EndConversation( CmdPlayer() );
}

CON_COMMAND_F( rpg_dialog_resume, "RPG dialog: continue the conversation after a UI screen closed (client UI)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE )
{
	CRPGDialogSystem::Resume( CmdPlayer() );
}

CON_COMMAND_F( rpg_dialog_reload, "RPG dialog: reload dialog v2 files", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	RPGDialog::Load();
}

CON_COMMAND_F( rpg_dialog_force, "RPG dialog: start a conversation: rpg_dialog_force <targetname> [node]", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
		return;
	CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, args[1] );
	if ( !pEnt )
	{
		Msg( "[dialog] entity '%s' not found\n", args[1] );
		return;
	}
	if ( !RPGDialog::Start( CmdPlayer(), pEnt, args.ArgC() >= 3 ? atoi( args[2] ) : -1 ) )
		Msg( "[dialog] no v2 dialog for '%s' (or spoiled/hostile)\n", args[1] );
}

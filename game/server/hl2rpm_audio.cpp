//========= HL2RPM ============================================================//
//
// Sound of the Creation Kit (server), see hl2rpm_audio.h.
//
//   scripts/hl2rpm_speech_block.txt   HL2 lines about Gordon Freeman (make_speech_block.py)
//   scripts/npcs/<npc>.txt            "stock_speech" all|combat|none, "barks" { "<id>" { event text sound choreo
//                                     weight cooldown once chance value conditions } }, "bark_sets" "a;b" (empty - by class, "-" none)
//   scripts/rpg/barks.txt             "Barks" { "<set>" { "classes" "npc_citizen;.." "<id>" { ... } } }
//   scripts/rpg/music.txt             "Music" { "tracks" { "<id>" { file volume kind time maps event } } }
//   layer zone refs                   display_name(_<lang>) discover_xp music (track ids) soundscape
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_world.h"
#include "dialog_definitions.h"
#include "player.h"
#include "ai_basenpc.h"
#include "baseflex.h"
#include "sceneentity.h"
#include "ai_speech.h"
#include "soundscape_system.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"
#include "utldict.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern void RPMTalkGesture_OnLine( CBaseEntity *pSpeaker, const char *pszText, float flDuration, const char *pszScene );

ConVar rpm_stock_speech( "rpm_stock_speech", "0", FCVAR_GAMEDLL, "HL2RPM: HL2 speech of NPCs without a Creation Kit definition: 0 - all lines, 1 - combat only, 2 - none (lines about Freeman are never said)" );
ConVar rpm_audio_debug( "rpm_audio_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print barks, blocked HL2 lines and music changes" );
ConVar rpm_barks( "rpm_barks", "1", FCVAR_GAMEDLL, "HL2RPM: NPC barks of the Creation Kit" );
ConVar rpm_music( "rpm_music", "1", FCVAR_GAMEDLL, "HL2RPM: music of the Creation Kit (scripts/rpg/music.txt)" );

static bool IsRussian() { return RPMLogic::IsRussian(); }

// key_<language> if the block has it (even empty: "no voice in this language"), else key
static const char *LocKey( KeyValues *pKV, const char *pszKey )
{
	CFmtStr k( "%s_%s", pszKey, DialogDefinitions::GetDialogLanguage() );
	if ( pKV->FindKey( k ) )
		return pKV->GetString( k, "" );
	return pKV->GetString( pszKey, "" );
}

// characters of a UTF-8 text (reading time of subtitles)
static int Utf8Chars( const char *s )
{
	int n = 0;
	for ( ; *s; s++ )
		if ( ( *s & 0xC0 ) != 0x80 )
			n++;
	return n;
}

static bool ListHas( const char *pszList, const char *pszItem )
{
	if ( !pszList || !pszItem || !pszItem[0] )
		return false;
	CUtlStringList parts;
	V_SplitString( pszList, ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
	{
		char *p = parts[i];
		while ( *p == ' ' )
			p++;
		if ( !Q_stricmp( p, pszItem ) )
			return true;
	}
	return false;
}

//=============================================================================
// HL2 speech filter
//=============================================================================
static CUtlDict<int, int> s_Blocked( k_eDictCompareTypeCaseInsensitive );		// sound script names
static CUtlDict<int, int> s_BlockedScenes( k_eDictCompareTypeCaseInsensitive );	// scene base names of generic NPCs
static bool s_bBlockLoaded = false;

static void LoadSpeechBlock()
{
	s_bBlockLoaded = true;
	s_Blocked.RemoveAll();
	s_BlockedScenes.RemoveAll();
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !filesystem->ReadFile( "scripts/hl2rpm_speech_block.txt", "GAME", buf ) )
		return;
	char line[512];
	while ( buf.IsValid() )
	{
		buf.GetLine( line, sizeof( line ) );
		if ( !line[0] )
			break;
		char *c = Q_strstr( line, "//" );
		if ( c )
			*c = 0;
		char *s = line;
		while ( *s == ' ' || *s == '\t' )
			s++;
		int n = Q_strlen( s );
		while ( n > 0 && ( s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n' ) )
			s[--n] = 0;
		if ( !s[0] )
			continue;
		if ( s_Blocked.Find( s ) == s_Blocked.InvalidIndex() )
			s_Blocked.Insert( s, 1 );
		// npc_citizen.abouttime01 -> scenes/npc/<gender>01/abouttime01.vcd
		const char *dot = Q_strstr( s, "." );
		if ( dot && !Q_strnicmp( s, "npc_", 4 ) && s_BlockedScenes.Find( dot + 1 ) == s_BlockedScenes.InvalidIndex() )
			s_BlockedScenes.Insert( dot + 1, 1 );
	}
	DevMsg( "RPMSpeech: %d blocked lines\n", s_Blocked.Count() );
}

static bool IsSocialConcept( const char *pszConcept )
{
	static const char *s_pszSocial[] = { "TLK_HELLO", "TLK_IDLE", "TLK_QUESTION", "TLK_ANSWER", "TLK_STARE", "TLK_USE",
		"TLK_STARTFOLLOW", "TLK_STOPFOLLOW", "TLK_JOINPLAYER", "TLK_SELECTED", "TLK_COMMAND", "TLK_LOOK", "TLK_REMIND",
		"TLK_LEAD", "TLK_GREET", "TLK_FOUNDPLAYER", "TLK_DARKNESS", "TLK_PLPUSH", "TLK_NOSHOOT", "TLK_SPOTTED_" };
	for ( int i = 0; i < ARRAYSIZE( s_pszSocial ); i++ )
		if ( !Q_strnicmp( pszConcept, s_pszSocial[i], Q_strlen( s_pszSocial[i] ) ) )
			return true;
	return false;
}

bool RPMSpeech::Allow( CBaseEntity *pSpeaker, const char *pszConcept, const char *pszResponse )
{
	if ( !pSpeaker || pSpeaker->IsPlayer() )
		return true;
	if ( !s_bBlockLoaded )
		LoadSpeechBlock();
	const char *why = NULL;
	// the NPC's own setting (Creation Kit NPCs: combat lines only by default), else the convar
	const RPMNPCDef_t *pDef = RPMWorld::FindNPCDef( pSpeaker->GetEntityName().ToCStr() );
	int policy = pDef ? ( pDef->stockSpeech >= 0 ? pDef->stockSpeech : 1 ) : rpm_stock_speech.GetInt();
	if ( policy >= 2 )
		why = "stock speech off";
	else if ( policy == 1 && pszConcept && IsSocialConcept( pszConcept ) )
		why = "combat lines only";
	else if ( pszResponse && pszResponse[0] )
	{
		if ( s_Blocked.Find( pszResponse ) != s_Blocked.InvalidIndex() )
			why = "a line about Freeman";
		else
		{
			const char *ext = Q_GetFileExtension( pszResponse );
			if ( ext && !Q_stricmp( ext, "vcd" ) )
			{
				char base[128];
				Q_FileBase( pszResponse, base, sizeof( base ) );
				if ( s_BlockedScenes.Find( base ) != s_BlockedScenes.InvalidIndex() )
					why = "a line about Freeman";
			}
		}
	}
	if ( why && rpm_audio_debug.GetBool() )
		Msg( "[audio] %s: '%s' (%s) not said - %s\n", pSpeaker->GetDebugName(), pszResponse ? pszResponse : "", pszConcept ? pszConcept : "", why );
	return why == NULL;
}

CON_COMMAND( rpm_speech_check, "HL2RPM: rpm_speech_check <npc targetname> <concept> <response> - would the NPC say this HL2 line" )
{
	if ( args.ArgC() < 4 )
		return;
	CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, args[1] );
	if ( !pEnt )
		pEnt = gEntList.FindEntityByClassname( NULL, args[1] );
	if ( !pEnt )
	{
		Msg( "no entity '%s'\n", args[1] );
		return;
	}
	Msg( "rpm_speech_check %s %s %s: %s\n", pEnt->GetDebugName(), args[2], args[3], RPMSpeech::Allow( pEnt, args[2], args[3] ) ? "says it" : "silent" );
}

CON_COMMAND( rpm_speech_block_reload, "HL2RPM: reload scripts/hl2rpm_speech_block.txt" )
{
	LoadSpeechBlock();
	Msg( "%d blocked HL2 lines\n", s_Blocked.Count() );
}

//=============================================================================
// Barks
//=============================================================================
static KeyValues *s_pBarkSets = NULL;
static int s_iBarkGeneration = -1;

struct BarkNPC_t
{
	EHANDLE hNPC;
	int lastHealth;
	bool bCombat;
	bool bGreetArmed;
	float flNextIdle;
	float flNextGreet;
	float flLastBark;
	float flBusyUntil;
};
static CUtlVector<BarkNPC_t> s_BarkNPCs;
static CUtlDict<float, int> s_Cooldowns;		// "<entindex>.<line id>" -> game time it may be said again
static CUtlDict<float, int> s_SaidTexts;		// text of a line -> game time anyone said it (same line by two NPCs)
static float s_flNextAmbientIdle = 0.0f;		// the street talks one idle line at a time, with pauses
static float s_flNextGreetAny = 0.0f;			// after a greeting the others nearby keep quiet for a while

// A crowd must not speak in chorus: everybody shares the engine's speech semaphore (citizens' and soldiers' own
// HL2 lines wait for it too), the same line is not repeated by anyone for a while, idle lines come one at a time
// for the whole street.
ConVar rpm_bark_gap( "rpm_bark_gap", "2.0", FCVAR_GAMEDLL, "HL2RPM: seconds of silence after any NPC line before the next non-urgent one" );
ConVar rpm_bark_repeat( "rpm_bark_repeat", "150", FCVAR_GAMEDLL, "HL2RPM: seconds before any NPC repeats a line somebody said (urgent lines: 20)" );
ConVar rpm_bark_idle_gap_min( "rpm_bark_idle_gap_min", "18", FCVAR_GAMEDLL, "HL2RPM: min seconds between idle lines of the whole street" );
ConVar rpm_bark_idle_gap_max( "rpm_bark_idle_gap_max", "45", FCVAR_GAMEDLL, "HL2RPM: max seconds between idle lines of the whole street" );
ConVar rpm_bark_greet_chance( "rpm_bark_greet_chance", "55", FCVAR_GAMEDLL, "HL2RPM: percent of NPCs that greet the player passing by" );

static void LoadBarkSets()
{
	if ( s_pBarkSets )
		s_pBarkSets->deleteThis();
	s_pBarkSets = new KeyValues( "Barks" );
	if ( !s_pBarkSets->LoadFromFile( filesystem, "scripts/rpg/barks.txt", "MOD" ) )
		s_pBarkSets->Clear();
	s_iBarkGeneration = RPMWorld::Generation();
}

static void CollectLines( CBaseEntity *pNPC, CUtlVector<KeyValues *> &out )
{
	if ( s_iBarkGeneration != RPMWorld::Generation() || !s_pBarkSets )
		LoadBarkSets();
	const RPMNPCDef_t *pDef = RPMWorld::FindNPCDef( pNPC->GetEntityName().ToCStr() );
	if ( pDef && pDef->pBarks )
		for ( KeyValues *k = pDef->pBarks->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey() )
			out.AddToTail( k );
	for ( KeyValues *set = s_pBarkSets->GetFirstTrueSubKey(); set; set = set->GetNextTrueSubKey() )
	{
		// the sets the Creation Kit NPC names ("-" - none), else the sets of its class (NPCs of Hammer too)
		bool bUse = pDef && !pDef->barkSets.IsEmpty() ? ListHas( pDef->barkSets, set->GetName() )
			: ListHas( set->GetString( "classes" ), pNPC->GetClassname() );
		if ( !bUse )
			continue;
		for ( KeyValues *k = set->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey() )
			out.AddToTail( k );
	}
}

static BarkNPC_t *BarkState( CBaseEntity *pNPC )
{
	for ( int i = 0; i < s_BarkNPCs.Count(); i++ )
		if ( s_BarkNPCs[i].hNPC.Get() == pNPC )
			return &s_BarkNPCs[i];
	BarkNPC_t b;
	b.hNPC = pNPC;
	b.lastHealth = pNPC->GetHealth();
	b.bCombat = false;
	b.bGreetArmed = true;
	b.flNextIdle = gpGlobals->curtime + RandomFloat( 20.0f, 70.0f );
	b.flNextGreet = 0.0f;
	b.flLastBark = -100.0f;
	b.flBusyUntil = 0.0f;
	return &s_BarkNPCs[s_BarkNPCs.AddToTail( b )];
}

static CUtlString CleanText( const char *s )
{
	CUtlString out;
	for ( ; s && *s; ++s )
	{
		char t[2] = { ( *s == '"' ) ? '\'' : ( ( *s == '\n' || *s == '\r' ) ? ' ' : *s ), 0 };
		out.Append( t );
	}
	return out;
}

static void SendSubtitle( CBasePlayer *pPlayer, const char *pszName, const char *pszText, float flSeconds )
{
	if ( !pPlayer || !pszText || !pszText[0] )
		return;
	engine->ClientCommand( pPlayer->edict(), "rpm_subtitle \"%s\" \"%s\" %.1f\n", CleanText( pszName ).Get(), CleanText( pszText ).Get(), flSeconds );
}

static const char *SpeakerName( CBaseEntity *pNPC )
{
	const char *n = DialogDefinitions::GetDisplayNameForEntity( pNPC->GetEntityName().ToCStr() );
	if ( n && n[0] )
		return n;
	n = RPMWorld::GetNPCDisplayNameFor( pNPC );
	if ( n && n[0] )
		return n;
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	return pAI ? pAI->GetLootDisplayName() : "";
}

bool RPMAudio::Bark( CBaseEntity *pNPC, const char *pszEvent, const char *pszValue )
{
	if ( !rpm_barks.GetBool() || !pNPC || !pNPC->IsAlive() )
		return false;
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	BarkNPC_t *st = BarkState( pNPC );
	const bool bUrgent = !Q_stricmp( pszEvent, "hurt" ) || !Q_stricmp( pszEvent, "combat" ) || !Q_stricmp( pszEvent, "ally_death" ) || !Q_stricmp( pszEvent, "theft" ) || !Q_stricmp( pszEvent, "weapon_law" );
	if ( gpGlobals->curtime < st->flBusyUntil || gpGlobals->curtime - st->flLastBark < ( bUrgent ? 2.0f : 6.0f ) )
		return false;
	// one speaker at a time (the engine's semaphore: HL2's own citizen / soldier speech waits for it as well);
	// urgent lines (pain, combat, theft) cut in
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	CAI_TimedSemaphore *pSem = pAI ? GetSpeechSemaphore( pAI ) : NULL;
	if ( !bUrgent && pSem && !pSem->IsAvailable( pNPC ) )
		return false;
	CUtlVector<KeyValues *> lines;
	CollectLines( pNPC, lines );
	if ( !lines.Count() )
		return false;
	KeyValues *pOnce = RPMState::Section( "barks" );
	RPMLogic::Ctx_t ctx;
	ctx.pPlayer = pPlayer;
	ctx.pSelf = pNPC;
	CUtlVector<KeyValues *> ok;
	float total = 0;
	for ( int i = 0; i < lines.Count(); i++ )
	{
		KeyValues *k = lines[i];
		if ( Q_stricmp( k->GetString( "event" ), pszEvent ) )
			continue;
		const char *v = k->GetString( "value" );
		if ( v[0] && !ListHas( v, pszValue ) )
			continue;
		CFmtStr key( "%d.%s", pNPC->entindex(), k->GetName() );
		int c = s_Cooldowns.Find( key );
		if ( c != s_Cooldowns.InvalidIndex() && gpGlobals->curtime < s_Cooldowns[c] )
			continue;
		CFmtStr onceKey( "%s.%s", pNPC->GetEntityName().ToCStr(), k->GetName() );
		if ( k->GetBool( "once" ) && pOnce->GetInt( onceKey, 0 ) )
			continue;
		// somebody (this NPC or another one with a copy of the same set) said this very line a moment ago
		int said = s_SaidTexts.Find( k->GetString( "text" ) );
		if ( said != s_SaidTexts.InvalidIndex() && gpGlobals->curtime - s_SaidTexts[said] < ( bUrgent ? 20.0f : rpm_bark_repeat.GetFloat() ) )
			continue;
		if ( RandomFloat( 0, 100 ) >= k->GetFloat( "chance", 100 ) )
			continue;
		if ( !RPMLogic::Check( k->FindKey( "conditions" ), ctx ) )
			continue;
		ok.AddToTail( k );
		total += Max( 0.01f, k->GetFloat( "weight", 1.0f ) );
	}
	if ( !ok.Count() )
		return false;
	float r = RandomFloat( 0, total );
	KeyValues *pick = ok[0];
	for ( int i = 0; i < ok.Count(); i++ )
	{
		r -= Max( 0.01f, ok[i]->GetFloat( "weight", 1.0f ) );
		if ( r <= 0 )
		{
			pick = ok[i];
			break;
		}
	}
	const char *text = LocKey( pick, "text" );
	if ( !text[0] )
		text = pick->GetString( "text" );
	const char *snd = LocKey( pick, "sound" );
	const char *choreo = LocKey( pick, "choreo" );
	CBaseFlex *pFlexActor = dynamic_cast<CBaseFlex *>( pNPC );
	float dur = 0.0f;
	if ( choreo[0] && pFlexActor )
	{
		// the scene of the Creation Kit (lip sync, gestures) like a dialog line
		CFmtStr scene( "%s%s", choreo, Q_stristr( choreo, ".vcd" ) ? "" : ".vcd" );
		dur = InstancedScriptedScene( pFlexActor, scene, NULL, 0.0f, false, NULL, true );
		if ( dur > 0.0f )
			snd = "";
	}
	if ( snd[0] )
	{
		CBaseEntity::PrecacheSound( snd );
		CBaseFlex *pFlex = dynamic_cast<CBaseFlex *>( pNPC );
		if ( pFlex )
			dur = InstancedAutoGeneratedSoundScene( pFlex, snd );
		else
		{
			pNPC->EmitSound( snd );
			dur = CBaseEntity::GetSoundDuration( snd, STRING( pNPC->GetModelName() ) );
		}
	}
	if ( dur <= 0.0f )
		dur = clamp( Utf8Chars( text ) / 16.0f, 2.0f, 7.0f );
	RPMTalkGesture_OnLine( pNPC, text, dur, choreo );
	st->flLastBark = gpGlobals->curtime;
	st->flBusyUntil = gpGlobals->curtime + dur;
	if ( pSem )
		pSem->Acquire( dur + rpm_bark_gap.GetFloat(), pNPC );
	{
		const char *said = pick->GetString( "text" );
		int i = s_SaidTexts.Find( said );
		if ( i == s_SaidTexts.InvalidIndex() )
			s_SaidTexts.Insert( said, gpGlobals->curtime );
		else
			s_SaidTexts[i] = gpGlobals->curtime;
	}
	{
		CFmtStr cdKey( "%d.%s", pNPC->entindex(), pick->GetName() );
		int c = s_Cooldowns.Find( cdKey );
		if ( c == s_Cooldowns.InvalidIndex() )
			s_Cooldowns.Insert( cdKey, gpGlobals->curtime + pick->GetFloat( "cooldown", 30.0f ) );
		else
			s_Cooldowns[c] = gpGlobals->curtime + pick->GetFloat( "cooldown", 30.0f );
	}
	if ( pick->GetBool( "once" ) )
		pOnce->SetInt( CFmtStr( "%s.%s", pNPC->GetEntityName().ToCStr(), pick->GetName() ), 1 );
	// subtitles when the player hears it
	if ( pPlayer && text[0] && ( pPlayer->GetAbsOrigin() - pNPC->GetAbsOrigin() ).Length() < 1200.0f )
		SendSubtitle( pPlayer, SpeakerName( pNPC ), text, Max( dur, clamp( Utf8Chars( text ) / 16.0f, 2.0f, 7.0f ) ) + 0.6f );	// at least the reading time
	if ( rpm_audio_debug.GetBool() )
		Msg( "[audio] bark %s %s/%s: '%s'%s\n", pNPC->GetDebugName(), pszEvent, pick->GetName(), text,
			snd[0] || choreo[0] ? CFmtStr( " (%s, %.1f s)", snd[0] ? snd : choreo, dur ).Access() : "" );
	return true;
}

bool RPMAudio::Say( CBaseEntity *pNPC, const char *pszText, const char *pszSound, bool bForce )
{
	if ( !pNPC || !pNPC->IsAlive() )
		return false;
	BarkNPC_t *st = BarkState( pNPC );
	if ( bForce ? gpGlobals->curtime - st->flLastBark < 0.4f : ( gpGlobals->curtime < st->flBusyUntil || gpGlobals->curtime - st->flLastBark < 1.5f ) )
		return false;
	float dur = 0.0f;
	if ( pszSound && pszSound[0] )
	{
		CBaseEntity::PrecacheSound( pszSound );
		CBaseFlex *pFlex = dynamic_cast<CBaseFlex *>( pNPC );
		if ( pFlex )
			dur = InstancedAutoGeneratedSoundScene( pFlex, pszSound );
		else
		{
			pNPC->EmitSound( pszSound );
			dur = CBaseEntity::GetSoundDuration( pszSound, STRING( pNPC->GetModelName() ) );
		}
	}
	const char *text = pszText ? pszText : "";
	if ( dur <= 0.0f )
		dur = clamp( Utf8Chars( text ) / 16.0f, 1.5f, 6.0f );
	RPMTalkGesture_OnLine( pNPC, text, dur, NULL );
	st->flLastBark = gpGlobals->curtime;
	st->flBusyUntil = gpGlobals->curtime + dur;
	if ( pNPC->MyNPCPointer() )
		GetSpeechSemaphore( pNPC->MyNPCPointer() )->Acquire( dur + rpm_bark_gap.GetFloat(), pNPC );
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( pPlayer && text[0] && ( pPlayer->GetAbsOrigin() - pNPC->GetAbsOrigin() ).Length() < 1200.0f )
		SendSubtitle( pPlayer, SpeakerName( pNPC ), text, Max( dur, clamp( Utf8Chars( text ) / 16.0f, 2.0f, 7.0f ) ) + 0.6f );
	if ( rpm_audio_debug.GetBool() )
		Msg( "[audio] say %s: '%s' (%s, %.1f s)\n", pNPC->GetDebugName(), text, pszSound ? pszSound : "", dur );
	return true;
}

void RPMAudio::NoteDeath( CBaseEntity *pVictim, CBaseEntity *pAttacker )
{
	if ( !pVictim || !pVictim->MyNPCPointer() )
		return;
	CAI_BaseNPC *pV = pVictim->MyNPCPointer();
	const char *pszFaction = pV->GetFactionTag();
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *p = ppAIs[i];
		if ( !p || p == pV || !p->IsAlive() || ( p->GetAbsOrigin() - pV->GetAbsOrigin() ).Length() > 1200.0f )
			continue;
		bool bAlly = ( pszFaction[0] && !Q_stricmp( p->GetFactionTag(), pszFaction ) ) ||
			( pV->GetSquad() && p->GetSquad() == pV->GetSquad() ) || !Q_stricmp( p->GetEntityName().ToCStr(), pV->GetEntityName().ToCStr() );
		if ( bAlly && p->FVisible( pV ) )
			RPMAudio::Bark( p, "ally_death", pV->GetEntityName().ToCStr() );
	}
	if ( pAttacker && pAttacker->MyNPCPointer() && pAttacker != pVictim )
		RPMAudio::Bark( pAttacker, "kill", pVictim->GetEntityName().ToCStr() );
}

void RPMAudio::NoteDialogEnd( CBaseEntity *pNPC )
{
	if ( pNPC )
		RPMAudio::Bark( pNPC, "farewell", "" );
}

void RPMAudio::NotePackage( CBaseEntity *pNPC, const char *pszPackage )
{
	if ( pNPC )
		RPMAudio::Bark( pNPC, "package", pszPackage );
}

static bool IsWeaponOut( CBasePlayer *pPlayer )
{
	CBaseCombatWeapon *pW = pPlayer->GetActiveWeapon();
	if ( !pW || !pW->IsWeaponVisible() )
		return false;
	return Q_stricmp( pW->GetClassname(), "weapon_physcannon" ) != 0;
}

static void BarkTick( CBasePlayer *pPlayer )
{
	// forget NPCs that are gone
	for ( int i = s_BarkNPCs.Count() - 1; i >= 0; i-- )
		if ( !s_BarkNPCs[i].hNPC.Get() || !s_BarkNPCs[i].hNPC->IsAlive() )
			s_BarkNPCs.FastRemove( i );
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	const Vector eye = pPlayer->EyePosition();
	// while the player talks to somebody the street does not talk over the dialog
	bool bDialogOpen = false;
	for ( int i = 0; i < g_AI_Manager.NumAIs() && !bDialogOpen; i++ )
		if ( ppAIs[i] && ppAIs[i]->IsAlive() && RPMWorld::IsInDialog( ppAIs[i] ) )
			bDialogOpen = true;
	CUtlVector<CAI_BaseNPC *> idlers;		// who could say an idle line now: one of them, picked at random
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *p = ppAIs[i];
		if ( !p || !p->IsAlive() )
			continue;
		float dist = ( p->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).Length();
		if ( dist > 2500.0f )
			continue;
		CUtlVector<KeyValues *> lines;
		CollectLines( p, lines );
		if ( !lines.Count() )
			continue;
		BarkNPC_t *st = BarkState( p );
		const bool bHostile = p->IRelationType( pPlayer ) == D_HT;
		const bool bCombat = p->GetState() == NPC_STATE_COMBAT;
		const bool bTalking = RPMWorld::IsInDialog( p );
		if ( p->GetHealth() < st->lastHealth )
			RPMAudio::Bark( p, "hurt", "" );
		st->lastHealth = p->GetHealth();
		if ( bCombat && !st->bCombat )
			RPMAudio::Bark( p, "combat", "" );
		st->bCombat = bCombat;
		if ( bTalking || bCombat || bDialogOpen )
			continue;
		if ( dist > 520.0f )
			st->bGreetArmed = true;
		const bool bSees = dist < 900.0f && p->FVisible( pPlayer );
		if ( st->bGreetArmed && !bHostile && dist < 220.0f && bSees )
		{
			// one greeting per passing: not everybody greets, not two at once, not the same NPC every time
			st->bGreetArmed = false;
			if ( gpGlobals->curtime >= st->flNextGreet && gpGlobals->curtime >= s_flNextGreetAny &&
				RandomFloat( 0, 100 ) < rpm_bark_greet_chance.GetFloat() && RPMAudio::Bark( p, "greet", "" ) )
			{
				st->flNextGreet = gpGlobals->curtime + RandomFloat( 90.0f, 180.0f );
				s_flNextGreetAny = gpGlobals->curtime + ( st->flBusyUntil - gpGlobals->curtime ) + RandomFloat( 5.0f, 10.0f );
				continue;
			}
		}
		if ( !bHostile && dist < 180.0f && bSees && IsWeaponOut( pPlayer ) && RPMAudio::Bark( p, "weapon", "" ) )
			continue;
		if ( dist < 700.0f && gpGlobals->curtime >= st->flNextIdle && ( bSees || dist < 300.0f ) )
			idlers.AddToTail( p );
	}
	if ( idlers.Count() && gpGlobals->curtime >= s_flNextAmbientIdle )
	{
		CAI_BaseNPC *p = idlers[RandomInt( 0, idlers.Count() - 1 )];
		BarkNPC_t *st = BarkState( p );
		if ( RPMAudio::Bark( p, "idle", "" ) )
		{
			st->flNextIdle = gpGlobals->curtime + RandomFloat( 60.0f, 120.0f );
			s_flNextAmbientIdle = st->flBusyUntil + RandomFloat( rpm_bark_idle_gap_min.GetFloat(), rpm_bark_idle_gap_max.GetFloat() );
		}
		else
			st->flNextIdle = gpGlobals->curtime + RandomFloat( 10.0f, 25.0f );	// nothing to say now (or the semaphore is busy)
	}
}

CON_COMMAND_F( rpm_bark, "HL2RPM: rpm_bark <npc targetname> <event> [value] - make an NPC say a bark of that event (test)", FCVAR_CHEAT )
{
	if ( args.ArgC() < 3 )
		return;
	CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, args[1] );
	if ( !pEnt )
	{
		Msg( "no entity '%s'\n", args[1] );
		return;
	}
	BarkState( pEnt )->flLastBark = -100.0f;
	BarkState( pEnt )->flBusyUntil = 0.0f;
	Msg( "rpm_bark %s %s: %s\n", args[1], args[2], RPMAudio::Bark( pEnt, args[2], args.ArgC() > 3 ? args[3] : "" ) ? "said" : "nothing to say" );
}

//=============================================================================
// Music
//=============================================================================
static KeyValues *s_pMusic = NULL;
static long s_MusicTime = -1;
static CUtlString s_LastMusic;
static float s_flCombatUntil = 0.0f;

bool RPMAudio::InCombat()
{
	return gpGlobals->curtime < s_flCombatUntil;
}

static KeyValues *Music()
{
	long t = filesystem->GetFileTime( "scripts/rpg/music.txt", "MOD" );
	if ( !s_pMusic || t != s_MusicTime )
	{
		if ( s_pMusic )
			s_pMusic->deleteThis();
		s_pMusic = new KeyValues( "Music" );
		if ( !s_pMusic->LoadFromFile( filesystem, "scripts/rpg/music.txt", "MOD" ) )
			s_pMusic->Clear();
		s_MusicTime = t;
	}
	return s_pMusic->FindKey( "tracks", true );
}

static void AppendTrack( CUtlString &out, KeyValues *t )
{
	const char *f = t->GetString( "file" );
	if ( !f[0] )
		return;
	if ( !out.IsEmpty() )
		out.Append( ";" );
	out.Append( CFmtStr( "%s:%.2f", f, t->GetFloat( "volume", 1.0f ) ) );
}

static void TracksByIds( const char *pszIds, CUtlString &out )
{
	KeyValues *tracks = Music();
	CUtlStringList ids;
	V_SplitString( pszIds, ";", ids );
	for ( int i = 0; i < ids.Count(); i++ )
	{
		KeyValues *t = tracks->FindKey( ids[i] );
		if ( t )
			AppendTrack( out, t );
	}
}

static void TracksByKind( const char *pszKind, CUtlString &out )
{
	float h = RPMWorld::GetHours();
	const bool bDay = h >= 6.0f && h < 21.0f;
	const char *map = STRING( gpGlobals->mapname );
	for ( KeyValues *t = Music()->GetFirstTrueSubKey(); t; t = t->GetNextTrueSubKey() )
	{
		if ( Q_stricmp( t->GetString( "kind", "explore" ), pszKind ) )
			continue;
		const char *maps = t->GetString( "maps" );
		if ( maps[0] && !ListHas( maps, map ) )
			continue;
		const char *time = t->GetString( "time", "any" );
		if ( ( !Q_stricmp( time, "day" ) && !bDay ) || ( !Q_stricmp( time, "night" ) && bDay ) )
			continue;
		AppendTrack( out, t );
	}
}

static void MusicTick( CBasePlayer *pPlayer )
{
	if ( !rpm_music.GetBool() )
		return;
	// combat: an enemy of the player fights him nearby (and a few seconds after)
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs() && pPlayer->IsAlive(); i++ )
	{
		CAI_BaseNPC *p = ppAIs[i];
		if ( p && p->IsAlive() && p->GetEnemy() == pPlayer && p->GetState() == NPC_STATE_COMBAT &&
			p->IRelationType( pPlayer ) == D_HT && ( p->GetAbsOrigin() - pPlayer->GetAbsOrigin() ).Length() < 2500.0f )
		{
			s_flCombatUntil = gpGlobals->curtime + 8.0f;
			break;
		}
	}
	CUtlString mood, list;
	const char *pszOverride = RPMState::Section( "music" )->GetString( "override", "" );
	if ( pszOverride[0] )
	{
		mood = "override";
		TracksByIds( pszOverride, list );
	}
	if ( list.IsEmpty() && gpGlobals->curtime < s_flCombatUntil && pPlayer->IsAlive() )
	{
		mood = "combat";
		TracksByKind( "combat", list );
	}
	if ( list.IsEmpty() )
	{
		// a location with its own music
		for ( int z = 0; z < RPMWorld::ZoneCount(); z++ )
		{
			KeyValues *pZ = RPMWorld::ZoneKeys( z );
			if ( pZ && pZ->GetString( "music" )[0] && RPMWorld::IsInZone( z, pPlayer->GetAbsOrigin() ) )
			{
				mood.Format( "zone:%s", RPMWorld::ZoneName( z ) );
				TracksByIds( pZ->GetString( "music" ), list );
				break;
			}
		}
	}
	if ( list.IsEmpty() )
	{
		mood = "explore";
		TracksByKind( "explore", list );
	}
	CUtlString msg;
	msg.Format( "%s~%s", mood.Get(), list.Get() );
	if ( msg == s_LastMusic )
		return;
	s_LastMusic = msg;
	engine->ClientCommand( pPlayer->edict(), "rpm_music_set \"%s\"\n", msg.Get() );
	if ( rpm_audio_debug.GetBool() )
		Msg( "[audio] music %s\n", msg.Get() );
}

void RPMAudio::SetMusicOverride( const char *pszTracks )
{
	RPMState::Section( "music" )->SetString( "override", pszTracks ? pszTracks : "" );
}

void RPMAudio::Stinger( const char *pszTrack )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	KeyValues *t = Music()->FindKey( pszTrack );
	if ( !pPlayer || !t || !t->GetString( "file" )[0] )
		return;
	engine->ClientCommand( pPlayer->edict(), "rpm_music_stinger \"%s:%.2f\"\n", t->GetString( "file" ), t->GetFloat( "volume", 1.0f ) );
	if ( rpm_audio_debug.GetBool() )
		Msg( "[audio] stinger %s\n", pszTrack );
}

void RPMAudio::StingerEvent( const char *pszEvent )
{
	if ( !rpm_music.GetBool() )
		return;
	CUtlVector<KeyValues *> ok;
	for ( KeyValues *t = Music()->GetFirstTrueSubKey(); t; t = t->GetNextTrueSubKey() )
		if ( !Q_stricmp( t->GetString( "kind" ), "stinger" ) && !Q_stricmp( t->GetString( "event" ), pszEvent ) )
			ok.AddToTail( t );
	if ( ok.Count() )
		RPMAudio::Stinger( ok[RandomInt( 0, ok.Count() - 1 )]->GetName() );
}

//=============================================================================
// Locations
//=============================================================================
void RPMAudio::ZoneEnter( int iZone )
{
	KeyValues *pZ = RPMWorld::ZoneKeys( iZone );
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( !pZ || !pPlayer )
		return;
	const char *name = LocKey( pZ, "display_name" );
	if ( !name[0] )
		name = pZ->GetString( "display_name" );
	if ( !name[0] )
		return;
	KeyValues *pFound = RPMState::Section( "locations" );
	CFmtStr key( "%s.%s", STRING( gpGlobals->mapname ), RPMWorld::ZoneName( iZone ) );
	if ( pFound->GetInt( key, 0 ) )
		return;
	pFound->SetInt( key, 1 );
	RPMLogic::Toast( pPlayer, "location", CFmtStr( IsRussian() ? "Новая локация: %s" : "Location discovered: %s", name ), "friends/friend_online.wav" );
	RPMLogic::AddXP( pPlayer, pZ->GetInt( "discover_xp", 0 ) );
	RPMAudio::StingerEvent( "location" );
}

//=============================================================================
// The tick
//=============================================================================
class CRPMAudioSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMAudioSystem() : CAutoGameSystemPerFrame( "CRPMAudioSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		s_LastMusic.Clear();
		s_flCombatUntil = 0.0f;
		s_BarkNPCs.RemoveAll();
		s_Cooldowns.RemoveAll();
		s_SaidTexts.RemoveAll();
		s_flNextAmbientIdle = 0.0f;
		s_flNextGreetAny = 0.0f;
		m_flNext = 0.0f;
		LoadSpeechBlock();
		LoadBarkSets();
	}

	virtual void LevelShutdownPostEntity()
	{
		s_BarkNPCs.RemoveAll();
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.25f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer )
			return;
		if ( pPlayer->IsAlive() )
			BarkTick( pPlayer );
		if ( gpGlobals->curtime >= m_flNextMusic )
		{
			m_flNextMusic = gpGlobals->curtime + 0.5f;
			MusicTick( pPlayer );
		}
	}

	float m_flNext = 0.0f;
	float m_flNextMusic = 0.0f;
};
static CRPMAudioSystem g_RPMAudioSystem;

CON_COMMAND( rpm_music_dump, "HL2RPM: the music tracks of scripts/rpg/music.txt, what plays now, the player's soundscape" )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( pPlayer )
	{
		Msg( "player soundscape index %d\n", pPlayer->m_Local.m_audio.soundscapeIndex.Get() );
		for ( int z = 0; z < RPMWorld::ZoneCount(); z++ )
		{
			KeyValues *pZ = RPMWorld::ZoneKeys( z );
			const char *ss = pZ ? pZ->GetString( "soundscape" ) : "";
			if ( ss[0] )
				Msg( "  zone %s: soundscape '%s' = index %d%s\n", RPMWorld::ZoneName( z ), ss, g_SoundscapeSystem.GetSoundscapeIndex( ss ),
					RPMWorld::IsInZone( z, pPlayer->GetAbsOrigin() ) ? " (player inside)" : "" );
		}
	}
	for ( KeyValues *t = Music()->GetFirstTrueSubKey(); t; t = t->GetNextTrueSubKey() )
		Msg( "  %-20s %-8s %-6s %s (maps '%s', event '%s')\n", t->GetName(), t->GetString( "kind", "explore" ), t->GetString( "time", "any" ),
			t->GetString( "file" ), t->GetString( "maps" ), t->GetString( "event" ) );
	Msg( "now: %s\n", s_LastMusic.Get() );
}

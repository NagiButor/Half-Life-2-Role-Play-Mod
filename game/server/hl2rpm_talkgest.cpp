//========= HL2RPM ============================================================//
//
// Talk gestures: an NPC that says a line of a dialog (or a bark) moves its hands
// and shoulders through it, like the people of New Vegas - one to three gestures
// spread over the time of the line, picked by what the line is: a question
// (shrug, open palms), an exclamation (punctuating fist, pointing), plain speech
// (hand motions). Only gestures the model has are used (male citizens have most,
// female citizens a few, Civil Protection its own); a scene that already has its
// own gestures (made by hand in Faceposer) is left alone. No gestures lying down
// or in combat.
//
// Rule npc_talk_gestures (scripts/rpg/rpg.txt), convars rpm_talk_gestures,
// rpm_talk_gestures_debug.
//
//=============================================================================//
#include "cbase.h"
#include "ai_basenpc.h"
#include "choreoscene.h"
#include "choreoevent.h"
#include "hl2rpm_rpgdefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern CChoreoScene *BlockingLoadScene( const char *filename );

static ConVar rpm_talk_gestures( "rpm_talk_gestures", "1", 0, "HL2RPM: NPCs gesture while they say dialog lines and barks" );
static ConVar rpm_talk_gestures_debug( "rpm_talk_gestures_debug", "0", 0, "HL2RPM: print the talk gestures" );

namespace
{
	// questions: open hands, shrugs
	const char *s_Question[] = { "g_shrug", "g_palm_up_l", "g_palm_up_high_l", "g_openarms", "g_armsout", "g_openarms_right",
		"g_left_openhand", "g_right_openhand", "g_lhandease" };
	// exclamations: accents, pointing, fists
	const char *s_Exclaim[] = { "g_puncuate", "g_fist_l", "g_fistshake", "g_point", "g_palm_out_high_l", "g_palm_out_high_r",
		"g_medurgent_mid", "g_frustrated_point", "g_armsout_high", "g_righthandheavy", "harassfront1_gesture", "harassfront2_gesture" };
	// plain speech: hand motions and small accents
	const char *s_Plain[] = { "g_palm_out_l", "g_palm_out_r", "g_righthandmotion", "g_lefthandmotion", "g_medpuct_mid",
		"g_mid_relaxed_fist_accent", "g_righthand_flick", "g_righthandroll", "g_left_openhand", "g_right_openhand", "g_lhandease",
		"g_palm_up_l", "g_puncuate" };

	struct Pending_t
	{
		EHANDLE hNPC;
		float flTime;
		int iKind;		// 0 plain, 1 question, 2 exclamation
	};
	CUtlVector<Pending_t> s_Pending;
	CUtlDict<bool, int> s_SceneHasGestures;

	int Utf8Chars( const char *s )
	{
		int n = 0;
		for ( ; s && *s; s++ )
			if ( ( *s & 0xC0 ) != 0x80 )
				n++;
		return n;
	}

	bool SceneHasGestures( const char *pszScene )
	{
		if ( !pszScene || !pszScene[0] )
			return false;
		char file[MAX_PATH];
		Q_strncpy( file, pszScene, sizeof( file ) );
		Q_SetExtension( file, ".vcd", sizeof( file ) );
		Q_FixSlashes( file );
		int i = s_SceneHasGestures.Find( file );
		if ( i != s_SceneHasGestures.InvalidIndex() )
			return s_SceneHasGestures[i];
		bool bHas = false;
		CChoreoScene *pScene = BlockingLoadScene( file );
		if ( pScene )
		{
			for ( int e = 0; e < pScene->GetNumEvents(); e++ )
			{
				CChoreoEvent *pEvent = pScene->GetEvent( e );
				if ( pEvent && ( pEvent->GetType() == CChoreoEvent::GESTURE || pEvent->GetType() == CChoreoEvent::SEQUENCE ) )
				{
					bHas = true;
					break;
				}
			}
			delete pScene;
		}
		s_SceneHasGestures.Insert( file, bHas );
		return bHas;
	}

	bool CanGesture( CAI_BaseNPC *pNPC )
	{
		if ( !pNPC || !pNPC->IsAlive() || pNPC->GetState() == NPC_STATE_COMBAT || pNPC->GetState() == NPC_STATE_DEAD )
			return false;
		// lying on a bed / the ground: the hands are under the head or on the belly
		const char *seq = pNPC->GetSequenceName( pNPC->GetSequence() );
		if ( seq && ( !Q_strnicmp( seq, "rpm_lie", 7 ) || Q_stristr( seq, "lying" ) || Q_stristr( seq, "laycouch" ) || Q_stristr( seq, "sleep" ) ) )
			return false;
		return true;
	}

	void PlayOne( CAI_BaseNPC *pNPC, int iKind )
	{
		const char **list = s_Plain;
		int n = ARRAYSIZE( s_Plain );
		if ( iKind == 1 )
		{
			list = s_Question;
			n = ARRAYSIZE( s_Question );
		}
		else if ( iKind == 2 )
		{
			list = s_Exclaim;
			n = ARRAYSIZE( s_Exclaim );
		}
		int have[32];
		int count = 0;
		for ( int i = 0; i < n && count < 32; i++ )
		{
			int seq = pNPC->LookupSequence( list[i] );
			if ( seq != ACT_INVALID )
				have[count++] = seq;
		}
		if ( !count && iKind != 0 )
		{
			PlayOne( pNPC, 0 );		// a model without question / exclamation gestures: plain ones
			return;
		}
		if ( !count )
			return;
		int seq = have[RandomInt( 0, count - 1 )];
		// one at a time: a new gesture ends the previous talk gesture
		for ( int i = 0; i < pNPC->GetNumAnimOverlays(); i++ )
		{
			CAnimationLayer *pLayer = pNPC->GetAnimOverlay( i );
			if ( pLayer && pLayer->IsActive() && pLayer->m_nSequence >= 0 )
			{
				const char *name = pNPC->GetSequenceName( pLayer->m_nSequence );
				if ( name && ( !Q_strnicmp( name, "g_", 2 ) || Q_stristr( name, "_gesture" ) ) )
					pNPC->FastRemoveLayer( i );
			}
		}
		int layer = pNPC->AddGestureSequence( seq, true );
		if ( layer >= 0 )
			pNPC->SetLayerPriority( layer, 60 );
		if ( rpm_talk_gestures_debug.GetBool() )
			Msg( "[talkgest] %s: %s (%s)\n", pNPC->GetDebugName(), pNPC->GetSequenceName( seq ), iKind == 1 ? "question" : ( iKind == 2 ? "exclamation" : "plain" ) );
	}
}

// a line is said: gestures through its duration
void RPMTalkGesture_OnLine( CBaseEntity *pSpeaker, const char *pszText, float flDuration, const char *pszScene )
{
	if ( !rpm_talk_gestures.GetBool() || RPMRPG::Rule( "npc_talk_gestures" ) == 0.0f )
		return;
	CAI_BaseNPC *pNPC = pSpeaker ? pSpeaker->MyNPCPointer() : NULL;
	if ( !pNPC || !pszText || !pszText[0] || !CanGesture( pNPC ) )
		return;
	if ( SceneHasGestures( pszScene ) )
		return;
	// drop what was left of an earlier line of this NPC
	for ( int i = s_Pending.Count() - 1; i >= 0; i-- )
		if ( s_Pending[i].hNPC == pNPC )
			s_Pending.Remove( i );
	const int chars = Utf8Chars( pszText );
	if ( flDuration <= 0.0f )
		flDuration = clamp( chars / 16.0f, 1.5f, 7.0f );
	// how many: one for a short line, more for long ones (a gesture lasts ~1.5-2.5 s)
	int n = chars < 25 ? 1 : ( chars < 90 ? 2 : 3 );
	n = Min( n, Max( 1, (int)( flDuration / 1.6f ) ) );
	// the kind by the punctuation: the last sentence decides the last gesture, the first one the first
	const bool bQuestion = Q_strstr( pszText, "?" ) != NULL;
	const bool bExclaim = Q_strstr( pszText, "!" ) != NULL;
	for ( int k = 0; k < n; k++ )
	{
		Pending_t p;
		p.hNPC = pNPC;
		// the first gesture comes with the start of speech, the rest spread over it
		float slot = flDuration / n;
		p.flTime = gpGlobals->curtime + 0.15f + slot * k + RandomFloat( 0.0f, slot * 0.35f );
		p.iKind = 0;
		if ( k == n - 1 && bQuestion )
			p.iKind = 1;
		else if ( bExclaim && ( k == 0 || RandomInt( 0, 1 ) ) )
			p.iKind = 2;
		// not every gesture: a pause in between looks more natural (never the first one)
		if ( k > 0 && RandomFloat( 0, 1 ) < 0.25f )
			continue;
		s_Pending.AddToTail( p );
	}
}

class CRPMTalkGestures : public CAutoGameSystemPerFrame
{
public:
	CRPMTalkGestures() : CAutoGameSystemPerFrame( "CRPMTalkGestures" ) {}
	virtual void LevelShutdownPreEntity()
	{
		s_Pending.Purge();
	}
	virtual void FrameUpdatePostEntityThink()
	{
		for ( int i = s_Pending.Count() - 1; i >= 0; i-- )
		{
			Pending_t &p = s_Pending[i];
			CAI_BaseNPC *pNPC = p.hNPC.Get() ? p.hNPC->MyNPCPointer() : NULL;
			if ( !pNPC )
			{
				s_Pending.Remove( i );
				continue;
			}
			if ( gpGlobals->curtime < p.flTime )
				continue;
			int kind = p.iKind;
			s_Pending.Remove( i );
			if ( CanGesture( pNPC ) )
				PlayOne( pNPC, kind );
		}
	}
};
static CRPMTalkGestures s_RPMTalkGestures;

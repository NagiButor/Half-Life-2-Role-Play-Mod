//========= HL2RPM ============================================================//
//
// Furniture for the player like in New Vegas: the markers of the Creation Kit that
// NPCs sit, sleep or lean on (kinds sit / eat / ground / sleep / lean) are usable by
// the player too. The use prompt says "[E] Сесть / Стул"; E puts the player's body on
// the marker playing the marker's animation (actbusy: entry, then the busy loop) and
// the client's camera goes third person around it (hl2rpm_seat_cam.cpp). E, jump or a
// movement key - the exit animation and back to first person.
//
// The body is a separate entity (rpm_seat_actor with the player's model); the player
// himself is hidden and held still meanwhile: the player's own animation state would
// fight a forced sequence every frame.
//
//=============================================================================//
#include "cbase.h"
#include "player.h"
#include "ai_hint.h"
#include "basecombatweapon.h"
#include "BasePropDoor.h"
#include "ai_behavior_actbusy.h"
#include "in_buttons.h"
#include "hl2rpm_world.h"
#include "hl2rpm_items.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern bool ActBusy_GetAnimParts( const char *pszName, Activity *pActs, string_t *pSeqs );

static ConVar rpm_seat( "rpm_seat", "1", FCVAR_GAMEDLL, "HL2RPM: the player sits / lies / leans on the markers of the Creation Kit (E)" );
static ConVar rpm_seat_debug( "rpm_seat_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print the animations the player's seat uses" );

enum { SEAT_VERB_SIT = 8, SEAT_VERB_LIE = 9, SEAT_VERB_LEAN = 10, SEAT_VERB_STAND = 11 };

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }

static int SeatVerb( const char *kind )
{
	if ( !Q_stricmp( kind, "sit" ) || !Q_stricmp( kind, "eat" ) || !Q_stricmp( kind, "ground" ) )
		return SEAT_VERB_SIT;
	if ( !Q_stricmp( kind, "sleep" ) )
		return SEAT_VERB_LIE;
	if ( !Q_stricmp( kind, "lean" ) )
		return SEAT_VERB_LEAN;
	return 0;
}

static const char *SeatName( const char *kind )
{
	bool ru = IsRussian();
	if ( !Q_stricmp( kind, "sit" ) ) return ru ? "Стул" : "Chair";
	if ( !Q_stricmp( kind, "eat" ) ) return ru ? "Стол" : "Table";
	if ( !Q_stricmp( kind, "ground" ) ) return ru ? "Место" : "Spot";
	if ( !Q_stricmp( kind, "sleep" ) ) return ru ? "Кровать" : "Bed";
	if ( !Q_stricmp( kind, "lean" ) ) return ru ? "Стена" : "Wall";
	return "";
}

//-----------------------------------------------------------------------------
// the body on the seat
//-----------------------------------------------------------------------------
class CRPMSeatActor : public CBaseAnimating
{
public:
	DECLARE_CLASS( CRPMSeatActor, CBaseAnimating );
	DECLARE_DATADESC();

	enum { ST_ENTRY, ST_BUSY, ST_EXIT, ST_DONE };

	void Spawn()
	{
		Precache();
		SetModel( STRING( GetModelName() ) );
		SetSolid( SOLID_NONE );
		SetMoveType( MOVETYPE_NONE );
		m_iState = ST_BUSY;
		SetThink( &CRPMSeatActor::AnimThink );
		SetNextThink( gpGlobals->curtime );
	}

	void Precache()
	{
		PrecacheModel( STRING( GetModelName() ) );
	}

	void Play( int seq, int state )
	{
		if ( seq < 0 )
			return;
		ResetSequence( seq );
		SetCycle( 0.0f );
		SetPlaybackRate( 1.0f );
		m_iState = state;
	}

	void Start( int entry, int busy, int exitSeq )
	{
		m_iBusy = busy;
		m_iExit = exitSeq;
		if ( entry >= 0 )
			Play( entry, ST_ENTRY );
		else
			Play( busy, ST_BUSY );
	}

	// true if there is an exit animation to wait for
	bool Leave()
	{
		if ( m_iExit < 0 )
		{
			m_iState = ST_DONE;
			return false;
		}
		Play( m_iExit, ST_EXIT );
		return true;
	}

	void AnimThink()
	{
		StudioFrameAdvance();
		DispatchAnimEvents( this );
		if ( IsSequenceFinished() )
		{
			if ( m_iState == ST_ENTRY )
				Play( m_iBusy >= 0 ? m_iBusy : GetSequence(), ST_BUSY );
			else if ( m_iState == ST_BUSY )
				SetCycle( 0.0f );			// busy loops (some are not marked looping)
			else if ( m_iState == ST_EXIT )
				m_iState = ST_DONE;
		}
		SetNextThink( gpGlobals->curtime + 0.05f );
	}

	int m_iState, m_iBusy, m_iExit;
};

LINK_ENTITY_TO_CLASS( rpm_seat_actor, CRPMSeatActor );

BEGIN_DATADESC( CRPMSeatActor )
	DEFINE_FIELD( m_iState, FIELD_INTEGER ),
	DEFINE_FIELD( m_iBusy, FIELD_INTEGER ),
	DEFINE_FIELD( m_iExit, FIELD_INTEGER ),
	DEFINE_THINKFUNC( AnimThink ),
END_DATADESC()

//-----------------------------------------------------------------------------
// finding the seat under the crosshair
//-----------------------------------------------------------------------------
static CAI_Hint *FindSeat( CBasePlayer *pPlayer )
{
	if ( !rpm_seat.GetBool() || !pPlayer || !pPlayer->IsAlive() || pPlayer->IsInAVehicle() )
		return NULL;
	// the thing USE would use: NPCs, doors, containers, terminals, items keep E for themselves
	CBaseEntity *pUse = pPlayer->FindUseEntity();
	if ( pUse && ( pUse->IsNPC() || dynamic_cast<CBasePropDoor *>( pUse ) || FClassnameIs( pUse, "prop_loot_container" )
		|| FClassnameIs( pUse, "rpm_terminal" ) || FClassnameIs( pUse, "item_rpm" ) || !Q_strnicmp( pUse->GetClassname(), "item_", 5 )
		|| !Q_strnicmp( pUse->GetClassname(), "weapon_", 7 ) || !Q_strnicmp( pUse->GetClassname(), "func_", 5 ) ) )
		return NULL;
	Vector eye = pPlayer->EyePosition(), fwd;
	pPlayer->EyeVectors( &fwd );
	trace_t tr;
	UTIL_TraceLine( eye, eye + fwd * 150.0f, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
	Vector aim = tr.endpos;
	CAI_Hint *pBest = NULL;
	float flBest = 46.0f;
	for ( CBaseEntity *p = gEntList.FindEntityByClassname( NULL, "ai_hint" ); p; p = gEntList.FindEntityByClassname( p, "ai_hint" ) )
	{
		CAI_Hint *h = dynamic_cast<CAI_Hint *>( p );
		if ( !h || h->GetGroup() == NULL_STRING || Q_stricmp( STRING( h->GetGroup() ), "rpm_marker" ) )
			continue;
		const RPMMarker_t *m = RPMWorld::FindMarker( h->GetEntityName().ToCStr() );
		if ( !m || !SeatVerb( m->kind ) )
			continue;
		Vector hp = h->GetAbsOrigin();
		if ( ( hp - pPlayer->GetAbsOrigin() ).Length2D() > 140.0f || fabsf( hp.z - pPlayer->GetAbsOrigin().z ) > 64.0f )
			continue;
		// the seat: a column 0..56 units above the marker
		float dz = aim.z < hp.z ? hp.z - aim.z : ( aim.z > hp.z + 56.0f ? aim.z - hp.z - 56.0f : 0.0f );
		float d = ( aim - hp ).Length2D() + dz;
		if ( d < flBest )
		{
			flBest = d;
			pBest = h;
		}
	}
	return pBest;
}

//-----------------------------------------------------------------------------
// the session (one player)
//-----------------------------------------------------------------------------
struct SeatSession_t
{
	EHANDLE hPlayer;
	CHandle<CRPMSeatActor> hActor;
	CHandle<CAI_Hint> hHint;
	float flStart;
	bool bLeaving;
	float flLeaveStart;
	int iVerb;
};
static SeatSession_t s_Seat;

bool RPMSeat_IsSeated( CBasePlayer *pPlayer )
{
	return s_Seat.hActor.Get() != NULL && s_Seat.hPlayer.Get() == pPlayer;
}

static int SequenceOf( CBaseAnimating *pAnim, Activity act, string_t seq )
{
	if ( seq != NULL_STRING )
	{
		int s = pAnim->LookupSequence( STRING( seq ) );
		if ( s >= 0 )
			return s;
	}
	if ( act != ACT_INVALID )
		return pAnim->SelectWeightedSequence( act );
	return -1;
}

static void HoldWeapon( CBasePlayer *pPlayer, bool bHold )
{
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( !pWeapon )
		return;
	float t = bHold ? gpGlobals->curtime + 100000.0f : gpGlobals->curtime + 0.2f;
	pWeapon->m_flNextPrimaryAttack = t;
	pWeapon->m_flNextSecondaryAttack = t;
}

static void FinishSeat()
{
	CBasePlayer *pPlayer = ToBasePlayer( s_Seat.hPlayer.Get() );
	if ( s_Seat.hActor.Get() )
		UTIL_Remove( s_Seat.hActor.Get() );
	if ( s_Seat.hHint.Get() )
		s_Seat.hHint->Unlock( 0.0f );
	if ( pPlayer )
	{
		pPlayer->RemoveEffects( EF_NODRAW );
		pPlayer->SetMoveType( MOVETYPE_WALK );
		pPlayer->RemoveSolidFlags( FSOLID_NOT_SOLID );
		HoldWeapon( pPlayer, false );
		engine->ClientCommand( pPlayer->edict(), "rpm_seat_cam 0\n" );
	}
	s_Seat.hActor = NULL;
	s_Seat.hHint = NULL;
	s_Seat.hPlayer = NULL;
	s_Seat.bLeaving = false;
}

static void StandUp()
{
	if ( s_Seat.bLeaving )
		return;
	s_Seat.bLeaving = true;
	s_Seat.flLeaveStart = gpGlobals->curtime;
	if ( !s_Seat.hActor.Get() || !s_Seat.hActor->Leave() )
		FinishSeat();
}

static bool SitDown( CBasePlayer *pPlayer, CAI_Hint *pHint )
{
	const RPMMarker_t *m = RPMWorld::FindMarker( pHint->GetEntityName().ToCStr() );
	if ( !m )
		return false;
	if ( pHint->User() && pHint->User() != pPlayer )
	{
		engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"buttons/button10.wav\"\n", IsRussian() ? "Занято" : "Taken" );
		return true;
	}
	const char *pszModel = STRING( pPlayer->GetModelName() );
	if ( !pszModel[0] || !Q_stricmp( pszModel, "models/player.mdl" ) )
		pszModel = "models/humans/group01/male_07.mdl";
	CRPMSeatActor *pActor = dynamic_cast<CRPMSeatActor *>( CreateEntityByName( "rpm_seat_actor" ) );
	if ( !pActor )
		return false;
	pActor->SetModelName( AllocPooledString( pszModel ) );
	pActor->SetAbsOrigin( pHint->GetAbsOrigin() );
	pActor->SetAbsAngles( QAngle( 0, pHint->GetAbsAngles().y, 0 ) );
	pActor->m_nSkin = pPlayer->m_nSkin;
	DispatchSpawn( pActor );
	pActor->Activate();
	RPMWorld::ApplyMarkerPose( pActor, pHint->GetEntityName().ToCStr() );

	// the marker's animation: an actbusy name or an activity (entry / busy / exit)
	const char *pszAnim = STRING( pHint->HintActivityName() );
	Activity acts[BA_MAX_ANIMS];
	string_t seqs[BA_MAX_ANIMS];
	int entry = -1, busy = -1, exitSeq = -1;
	if ( pszAnim[0] && ActBusy_GetAnimParts( pszAnim, acts, seqs ) )
	{
		entry = SequenceOf( pActor, acts[BA_ENTRY], seqs[BA_ENTRY] );
		busy = SequenceOf( pActor, acts[BA_BUSY], seqs[BA_BUSY] );
		exitSeq = SequenceOf( pActor, acts[BA_EXIT], seqs[BA_EXIT] );
	}
	else if ( pszAnim[0] )
	{
		Activity act = (Activity)ActivityList_IndexForName( pszAnim );
		busy = act != ACT_INVALID ? pActor->SelectWeightedSequence( act ) : pActor->LookupSequence( pszAnim );
	}
	if ( busy < 0 )
		busy = pActor->SelectWeightedSequence( ACT_IDLE );
	if ( rpm_seat_debug.GetBool() )
		Msg( "[seat] %s anim '%s': entry %s, busy %s, exit %s\n", m->name.Get(), pszAnim,
			entry >= 0 ? pActor->GetSequenceName( entry ) : "-", busy >= 0 ? pActor->GetSequenceName( busy ) : "-",
			exitSeq >= 0 ? pActor->GetSequenceName( exitSeq ) : "-" );
	pActor->Start( entry, busy, exitSeq );

	pHint->Lock( pPlayer );
	pPlayer->AddEffects( EF_NODRAW );
	pPlayer->SetMoveType( MOVETYPE_NONE );
	pPlayer->SetAbsVelocity( vec3_origin );
	pPlayer->AddSolidFlags( FSOLID_NOT_SOLID );
	HoldWeapon( pPlayer, true );
	s_Seat.hPlayer = pPlayer;
	s_Seat.hActor = pActor;
	s_Seat.hHint = pHint;
	s_Seat.flStart = gpGlobals->curtime;
	s_Seat.bLeaving = false;
	s_Seat.iVerb = SeatVerb( m->kind );
	engine->ClientCommand( pPlayer->edict(), "rpm_seat_cam %d %d\n", pActor->entindex(), s_Seat.iVerb );
	return true;
}

// CHL2_Player::PlayerUse: E on a seat (or while seated) - true if it was handled here
bool RPMSeat_OnUse( CBasePlayer *pPlayer )
{
	if ( RPMSeat_IsSeated( pPlayer ) )
	{
		if ( gpGlobals->curtime - s_Seat.flStart > 0.5f )
			StandUp();
		return true;
	}
	CAI_Hint *pHint = FindSeat( pPlayer );
	return pHint && SitDown( pPlayer, pHint );
}

// the name of the furniture by the nearest model with a known word in its name (within 64 units of the seat)
class CSeatFurnitureFinder : public CTraceFilter
{
public:
	CSeatFurnitureFinder( const Vector &seat ) : m_pszName( NULL ), m_vecSeat( seat ), m_flBest( 64.0f ) {}

	void Consider( const char *pszModel, const Vector &center )
	{
		static const struct { const char *key, *en, *ru; } s_Furniture[] = {
			{ "couch", "Couch", "Диван" }, { "sofa", "Couch", "Диван" }, { "bench", "Bench", "Скамья" }, { "mattress", "Mattress", "Матрас" },
			{ "bed", "Bed", "Кровать" }, { "cot", "Cot", "Койка" }, { "stool", "Stool", "Табурет" }, { "armchair", "Armchair", "Кресло" },
			{ "chair", "Chair", "Стул" }, { "table", "Table", "Стол" }, { "desk", "Desk", "Стол" } };
		float d = ( center - m_vecSeat ).Length2D();
		if ( !pszModel || d >= m_flBest )
			return;
		for ( int i = 0; i < ARRAYSIZE( s_Furniture ); i++ )
		{
			if ( Q_stristr( pszModel, s_Furniture[i].key ) )
			{
				m_flBest = d;
				m_pszName = IsRussian() ? s_Furniture[i].ru : s_Furniture[i].en;
				return;
			}
		}
	}

	// a static prop reaches the filter only with this trace type; nothing is hit - the trace only lists them
	virtual TraceType_t GetTraceType() const { return TRACE_EVERYTHING_FILTER_PROPS; }
	virtual bool ShouldHitEntity( IHandleEntity *pHandle, int contentsMask )
	{
		if ( staticpropmgr->IsStaticProp( pHandle ) )
		{
			ICollideable *pCollide = enginetrace->GetCollideable( pHandle );
			const model_t *pModel = pCollide ? pCollide->GetCollisionModel() : NULL;
			if ( pModel )
			{
				Vector lo, hi;
				pCollide->WorldSpaceSurroundingBounds( &lo, &hi );
				Consider( modelinfo->GetModelName( pModel ), ( lo + hi ) * 0.5f );
			}
		}
		return false;
	}

	const char *m_pszName;

private:
	Vector m_vecSeat;
	float m_flBest;
};

// the use prompt (hl2rpm_prompt.cpp): verb and name of the seat under the crosshair, or of standing up
bool RPMSeat_Describe( CBasePlayer *pPlayer, int &verb, CUtlString &name )
{
	if ( RPMSeat_IsSeated( pPlayer ) )
	{
		verb = SEAT_VERB_STAND;
		name.Clear();
		return true;
	}
	CAI_Hint *pHint = FindSeat( pPlayer );
	const RPMMarker_t *m = pHint ? RPMWorld::FindMarker( pHint->GetEntityName().ToCStr() ) : NULL;
	if ( !m )
		return false;
	verb = SeatVerb( m->kind );
	name = SeatName( m->kind );
	// the furniture it stands on, by its model: a couch is not a chair. Entities (prop_dynamic / physics) and
	// static props - most furniture of a Hammer map; they are not entities, a trace filter that asks about props sees them
	CSeatFurnitureFinder finder( pHint->GetAbsOrigin() );
	for ( CBaseEntity *p = gEntList.FindEntityInSphere( NULL, pHint->GetAbsOrigin(), 64.0f ); p; p = gEntList.FindEntityInSphere( p, pHint->GetAbsOrigin(), 64.0f ) )
	{
		if ( !Q_strnicmp( p->GetClassname(), "prop_", 5 ) && p->GetModelName() != NULL_STRING )
			finder.Consider( STRING( p->GetModelName() ), p->WorldSpaceCenter() );
	}
	trace_t tr;
	UTIL_TraceHull( pHint->GetAbsOrigin() + Vector( 0, 0, 72 ), pHint->GetAbsOrigin() - Vector( 0, 0, 8 ), Vector( -40, -40, 0 ), Vector( 40, 40, 8 ),
		MASK_SOLID, &finder, &tr );
	if ( finder.m_pszName )
		name = finder.m_pszName;
	return true;
}

class CRPMSeatSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMSeatSystem() : CAutoGameSystemPerFrame( "CRPMSeatSystem" ) {}

	virtual void LevelInitPostEntity()
	{
		// a game saved while sitting: no session now - take the body away, give the player back
		s_Seat.hActor = NULL;
		s_Seat.hPlayer = NULL;
		for ( CBaseEntity *p = gEntList.FindEntityByClassname( NULL, "rpm_seat_actor" ); p; p = gEntList.FindEntityByClassname( p, "rpm_seat_actor" ) )
		{
			UTIL_Remove( p );
			m_bFixPlayer = true;
		}
	}

	virtual void LevelShutdownPreEntity()
	{
		s_Seat.hActor = NULL;
		s_Seat.hPlayer = NULL;
		s_Seat.hHint = NULL;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( m_bFixPlayer && pPlayer )
		{
			// a game saved while sitting: the player comes back hidden and held - give him back
			m_bFixPlayer = false;
			pPlayer->RemoveEffects( EF_NODRAW );
			pPlayer->SetMoveType( MOVETYPE_WALK );
			pPlayer->RemoveSolidFlags( FSOLID_NOT_SOLID );
			HoldWeapon( pPlayer, false );
			engine->ClientCommand( pPlayer->edict(), "rpm_seat_cam 0\n" );
		}
		if ( !s_Seat.hPlayer.Get() )
			return;
		CBasePlayer *p = ToBasePlayer( s_Seat.hPlayer.Get() );
		if ( !p || !p->IsAlive() || !s_Seat.hActor.Get() )
		{
			FinishSeat();
			return;
		}
		if ( s_Seat.bLeaving )
		{
			if ( s_Seat.hActor->m_iState == CRPMSeatActor::ST_DONE || gpGlobals->curtime - s_Seat.flLeaveStart > 3.0f )
				FinishSeat();
			return;
		}
		if ( gpGlobals->curtime - s_Seat.flStart > 0.6f &&
			( p->m_afButtonPressed & ( IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_DUCK ) ) )
			StandUp();
	}

	bool m_bFixPlayer = false;
};

static CRPMSeatSystem g_RPMSeatSystem;

CON_COMMAND( rpm_seat_use, "HL2RPM: sit on the marker under the crosshair / stand up (as E does; tests)" )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( pPlayer && !RPMSeat_OnUse( pPlayer ) )
		Msg( "rpm_seat_use: no seat under the crosshair\n" );
}

CON_COMMAND_F( rpm_seat_at, "HL2RPM: rpm_seat_at <marker name> - sit on a marker (tests)", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer || args.ArgC() < 2 )
		return;
	if ( RPMSeat_IsSeated( pPlayer ) )
	{
		StandUp();
		return;
	}
	CAI_Hint *h = dynamic_cast<CAI_Hint *>( gEntList.FindEntityByName( NULL, args[1] ) );
	if ( !h || !SitDown( pPlayer, h ) )
		Msg( "rpm_seat_at: no marker %s\n", args[1] );
}

//-----------------------------------------------------------------------------
// rpm_marker_use <npc> <marker>: send an NPC to a marker right now (tests of the animation library,
// "preview in the game" of the Creation Kit) - its routine takes over again when the busy ends
//-----------------------------------------------------------------------------
CON_COMMAND_F( rpm_marker_use, "HL2RPM: rpm_marker_use <npc targetname> <marker> - the NPC goes to the marker and plays its animation", FCVAR_CHEAT )
{
	if ( args.ArgC() < 3 )
	{
		Msg( "rpm_marker_use <npc targetname> <marker>\n" );
		return;
	}
	CBaseEntity *pEnt = gEntList.FindEntityByName( NULL, args[1] );
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	CAI_Hint *pHint = dynamic_cast<CAI_Hint *>( gEntList.FindEntityByName( NULL, args[2] ) );
	if ( !pNPC || !pHint )
	{
		Msg( "no NPC '%s' or marker '%s'\n", args[1], args[2] );
		return;
	}
	CAI_ActBusyBehavior *pActBusy = NULL;
	pNPC->GetBehavior( &pActBusy );
	if ( !pActBusy )
	{
		Msg( "%s has no actbusy behavior\n", pNPC->GetDebugName() );
		return;
	}
	pActBusy->ForceActBusy( NULL, pHint, 0.0f );
	Msg( "%s -> %s (%s)\n", pNPC->GetDebugName(), args[2], STRING( pHint->HintActivityName() ) );
}

CON_COMMAND_F( rpm_marker_leave, "HL2RPM: rpm_marker_leave <npc targetname> - the NPC ends its busy animation (plays the exit)", FCVAR_CHEAT )
{
	CBaseEntity *pEnt = args.ArgC() > 1 ? gEntList.FindEntityByName( NULL, args[1] ) : NULL;
	CAI_BaseNPC *pNPC = pEnt ? pEnt->MyNPCPointer() : NULL;
	CAI_ActBusyBehavior *pActBusy = NULL;
	if ( pNPC )
		pNPC->GetBehavior( &pActBusy );
	if ( pActBusy )
		pActBusy->StopBusying();
}

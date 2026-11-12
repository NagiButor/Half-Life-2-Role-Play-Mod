//========= HL2RPM ============================================================//
//
// The weapon ban of the occupied City: Civil Protection and soldiers (enforcers)
// don't let civilians - the player and citizens - walk around with a weapon in
// their hands. An enforcer that sees one warns ("Hold it! Put the weapon away!"),
// gives a final warning half way through rule weapon_law_grace, then opens fire
// together with the enforcers around that see the offender. Holstering (H) in
// time ends it ("All right, you can go"); a second offence within a minute is
// answered with fire at once.
//
// Who is who: RPMWorld::NPCWeaponLaw (Creation Kit "weapon_law" of the NPC, else
// by class: npc_metropolice / npc_combine_s enforce - rule weapon_law 0 off,
// 1 warn first, 2 fire at once - npc_citizen is a civilian). The player is a
// civilian unless he wears a Civil Protection / soldier / elite uniform (rule
// weapon_law_disguise). Melee weapons count only with rule weapon_law_melee.
// What the enforcer says: its barks of event "weapon_law" (value warn / final /
// attack / comply), else built-in HL2 lines with subtitles.
//
//=============================================================================//
#include "cbase.h"
#include "ai_basenpc.h"
#include "player.h"
#include "basebludgeonweapon.h"
#include "ai_baseactor.h"
#include "hl2rpm_world.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_items.h"
#include "hl2rpm_rpgdefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar rpm_weapon_law_debug( "rpm_weapon_law_debug", "0", 0, "HL2RPM: print what the weapon ban (enforcers vs armed civilians) does" );

extern int RPMChar_SuitTheme( CBasePlayer *pPlayer );

namespace
{
	enum { STAGE_NONE = 0, STAGE_WARNED, STAGE_FINAL, STAGE_ATTACKED };

	struct Offender_t
	{
		EHANDLE hEnt;
		int stage;
		float flWarned;			// time of the first warning
		float flLastSeen;		// last time an enforcer saw it armed
		float flLastOffence;	// time it last complied (a repeat within a minute is not warned)
		EHANDLE hWarner;
		float flComply;			// "you can go" is said as soon as the enforcer is free (until then + 4 s)
	};

	CUtlVector<Offender_t> s_Offenders;

	bool Russian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }

	// the weapon in the hands counts (not the gravity gun; melee only with the rule)
	bool IsArmed( CBaseCombatCharacter *pBCC )
	{
		CBaseCombatWeapon *pW = pBCC ? pBCC->GetActiveWeapon() : NULL;
		if ( !pW || !pW->IsWeaponVisible() )
			return false;
		if ( FClassnameIs( pW, "weapon_physcannon" ) )
			return false;
		if ( dynamic_cast<CBaseHLBludgeonWeapon *>( pW ) && RPMRPG::Rule( "weapon_law_melee" ) == 0.0f )
			return false;
		return true;
	}

	bool IsCivilian( CBaseEntity *pEnt )
	{
		if ( pEnt->IsPlayer() )
		{
			// in a Civil Protection / soldier / elite uniform the player passes for one of them
			if ( RPMRPG::Rule( "weapon_law_disguise" ) != 0.0f )
			{
				int theme = RPMChar_SuitTheme( ToBasePlayer( pEnt ) );
				if ( theme == 3 || theme == 4 )
					return false;
			}
			return true;
		}
		CAI_BaseNPC *pNPC = pEnt->MyNPCPointer();
		return pNPC && RPMWorld::NPCWeaponLaw( pNPC ) == 1;
	}

	Offender_t *State( CBaseEntity *pEnt, bool bCreate )
	{
		for ( int i = 0; i < s_Offenders.Count(); i++ )
			if ( s_Offenders[i].hEnt.Get() == pEnt )
				return &s_Offenders[i];
		if ( !bCreate )
			return NULL;
		Offender_t o;
		o.hEnt = pEnt;
		o.stage = STAGE_NONE;
		o.flWarned = 0;
		o.flLastSeen = 0;
		o.flLastOffence = -1000.0f;
		o.hWarner = NULL;
		o.flComply = 0;
		return &s_Offenders[s_Offenders.AddToTail( o )];
	}

	// an enforcer that would act on this offender now
	bool CanAct( CAI_BaseNPC *pCop, CBaseEntity *pOff, float flDist )
	{
		if ( !pCop->IsAlive() || pCop->IsInAScript() || pCop->GetState() == NPC_STATE_SCRIPT || pCop->GetState() == NPC_STATE_DEAD )
			return false;
		Disposition_t d = pCop->IRelationType( pOff );
		if ( d == D_HT || d == D_LI )
			return false;
		// one of their own (same faction) may carry
		const char *a = pCop->GetFactionTag();
		CAI_BaseNPC *pOffNPC = pOff->MyNPCPointer();
		if ( pOffNPC && a && a[0] && !Q_stricmp( a, pOffNPC->GetFactionTag() ) )
			return false;
		float d2 = ( pCop->EyePosition() - pOff->EyePosition() ).Length();
		if ( d2 > flDist )
			return false;
		if ( d2 > 200.0f && !pCop->FInViewCone( pOff ) )
			return false;
		return pCop->FVisible( pOff );
	}

	struct Line_t { const char *cls; const char *what; const char *snd; const char *en; const char *ru; };
	const Line_t s_Lines[] =
	{
		{ "npc_metropolice", "warn", "npc/metropolice/vo/holdit.wav", "Hold it! Put that weapon away!", "Стоять! Убрать оружие!" },
		{ "npc_metropolice", "final", "npc/metropolice/vo/finalwarning.wav", "Final warning!", "Последнее предупреждение!" },
		{ "npc_metropolice", "attack", "npc/metropolice/vo/prepareforjudgement.wav", "Prepare for judgement!", "Готовься к приговору!" },
		{ "npc_metropolice", "comply", "npc/metropolice/vo/allrightyoucango.wav", "All right. You can go.", "Ладно. Проходи." },
		{ "npc_combine_s", "warn", "npc/combine_soldier/vo/alert1.wav", "Citizen! Weapon down!", "Гражданин! Оружие убрать!" },
		{ "npc_combine_s", "final", "npc/combine_soldier/vo/weaponsoffsafeprepforcontact.wav", "Last warning!", "Последнее предупреждение!" },
		{ "npc_combine_s", "attack", "npc/combine_soldier/vo/contactconfirmprosecuting.wav", "Contact! Prosecuting!", "Нарушитель! Открыть огонь!" },
		{ "npc_combine_s", "comply", "npc/combine_soldier/vo/affirmative.wav", "Affirmative. Move along.", "Принято. Проходи." },
		{ "", "warn", "", "Put that weapon away!", "Убери оружие!" },
		{ "", "final", "", "Last warning!", "Последнее предупреждение!" },
		{ "", "attack", "", "You asked for it!", "Сам напросился!" },
		{ "", "comply", "", "That's better.", "Так-то лучше." },
	};

	// warnings and the order to fire are said over anything else; "comply" waits its turn
	bool Speak( CAI_BaseNPC *pCop, const char *pszWhat )
	{
		if ( RPMAudio::Bark( pCop, "weapon_law", pszWhat ) )
			return true;
		const bool bForce = Q_stricmp( pszWhat, "comply" ) != 0;
		for ( int i = 0; i < ARRAYSIZE( s_Lines ); i++ )
		{
			const Line_t &l = s_Lines[i];
			if ( Q_stricmp( l.what, pszWhat ) || ( l.cls[0] && !FClassnameIs( pCop, l.cls ) ) )
				continue;
			return RPMAudio::Say( pCop, Russian() ? l.ru : l.en, l.snd, bForce );
		}
		return false;
	}

	// the enforcer turns to the offender and looks at it
	void Face( CAI_BaseNPC *pCop, CBaseEntity *pOff, float flTime )
	{
		if ( pCop->GetState() == NPC_STATE_COMBAT )
			return;
		pCop->AddFacingTarget( pOff, 1.0f, flTime, 0.2f );
		CAI_BaseActor *pActor = dynamic_cast<CAI_BaseActor *>( pCop );
		if ( pActor )
			pActor->AddLookTarget( pOff, 1.0f, flTime, 0.2f );
	}

	void Attack( CAI_BaseNPC *pWarner, CBaseEntity *pOff, float flDist )
	{
		CAI_BaseNPC *pOffNPC = pOff->MyNPCPointer();
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		int n = 0;
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *pCop = ppAIs[i];
			if ( !pCop || RPMWorld::NPCWeaponLaw( pCop ) < 2 )
				continue;
			bool bSquad = pWarner && pCop->GetSquad() && pCop->GetSquad() == pWarner->GetSquad();
			if ( pCop != pWarner && !bSquad && !CanAct( pCop, pOff, flDist ) )
				continue;
			if ( !pCop->IsAlive() || pCop->IRelationType( pOff ) == D_LI )
				continue;
			pCop->AddEntityRelationship( pOff, D_HT, 60 );
			pCop->UpdateEnemyMemory( pOff, pOff->GetAbsOrigin() );
			if ( pOffNPC )
				pOffNPC->AddEntityRelationship( pCop, D_HT, 60 );
			n++;
		}
		if ( pWarner )
			Speak( pWarner, "attack" );
		if ( rpm_weapon_law_debug.GetBool() )
			Msg( "[weaponlaw] %s: %d enforcers attack %s\n", pWarner ? pWarner->GetDebugName() : "-", n, pOff->GetDebugName() );
	}
}

class CRPMWeaponLaw : public CAutoGameSystemPerFrame
{
public:
	CRPMWeaponLaw() : CAutoGameSystemPerFrame( "CRPMWeaponLaw" ), m_flNext( 0 ) {}

	virtual void LevelInitPostEntity()
	{
		s_Offenders.Purge();
		m_flNext = 0;
		for ( int i = 0; i < ARRAYSIZE( s_Lines ); i++ )
			if ( s_Lines[i].snd[0] )
				CBaseEntity::PrecacheSound( s_Lines[i].snd );
	}
	virtual void LevelShutdownPostEntity() { s_Offenders.Purge(); }

	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.3f;
		const float flDist = Max( 100.0f, RPMRPG::Rule( "weapon_law_dist" ) );
		const float flGrace = Max( 1.0f, RPMRPG::Rule( "weapon_law_grace" ) );

		for ( int i = s_Offenders.Count() - 1; i >= 0; i-- )
			if ( !s_Offenders[i].hEnt.Get() || !s_Offenders[i].hEnt->IsAlive() )
				s_Offenders.FastRemove( i );

		// enforcers of this map (an NPC of the Creation Kit can be one even with the rule at 0)
		CUtlVector<CAI_BaseNPC *> cops;
		CUtlVector<CBaseCombatCharacter *> offenders;
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *p = ppAIs[i];
			if ( !p || !p->IsAlive() )
				continue;
			int law = RPMWorld::NPCWeaponLaw( p );
			if ( law >= 2 )
				cops.AddToTail( p );
			else if ( law == 1 )
				offenders.AddToTail( p );
		}
		if ( !cops.Count() )
			return;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( pPlayer && pPlayer->IsAlive() && !( pPlayer->GetFlags() & FL_NOTARGET ) && IsCivilian( pPlayer ) )
			offenders.AddToTail( pPlayer );

		for ( int o = 0; o < offenders.Count(); o++ )
		{
			CBaseCombatCharacter *pOff = offenders[o];
			Offender_t *st = State( pOff, false );
			const bool bArmed = IsArmed( pOff );
			// the nearest enforcer that sees it
			CAI_BaseNPC *pCop = NULL;
			float best = FLT_MAX;
			for ( int c = 0; c < cops.Count(); c++ )
			{
				if ( !CanAct( cops[c], pOff, flDist ) )
					continue;
				float d = ( cops[c]->GetAbsOrigin() - pOff->GetAbsOrigin() ).LengthSqr();
				if ( d < best )
				{
					best = d;
					pCop = cops[c];
				}
			}
			if ( !bArmed )
			{
				// complied in time
				if ( st && ( st->stage == STAGE_WARNED || st->stage == STAGE_FINAL ) )
				{
					st->flComply = gpGlobals->curtime + 4.0f;
					st->stage = STAGE_NONE;
					st->flLastOffence = gpGlobals->curtime;
					if ( rpm_weapon_law_debug.GetBool() )
						Msg( "[weaponlaw] %s put the weapon away\n", pOff->GetDebugName() );
				}
				if ( st && st->flComply > gpGlobals->curtime )
				{
					CAI_BaseNPC *pW = st->hWarner.Get() ? st->hWarner->MyNPCPointer() : pCop;
					if ( !pW || !pW->IsAlive() || Speak( pW, "comply" ) )
						st->flComply = 0;
				}
				continue;
			}
			if ( !pCop )
			{
				// nobody saw it for a while: start over (a repeat is still remembered); an offender
				// that was fired at is forgotten by the enforcers that didn't fight it after 5 minutes
				if ( st && ( st->stage == STAGE_WARNED || st->stage == STAGE_FINAL ) && gpGlobals->curtime - st->flLastSeen > 10.0f )
					st->stage = STAGE_NONE;
				else if ( st && st->stage == STAGE_ATTACKED && gpGlobals->curtime - st->flLastSeen > 300.0f )
				{
					st->stage = STAGE_NONE;
					st->flLastOffence = -1000.0f;
				}
				continue;
			}
			st = State( pOff, true );
			st->flLastSeen = gpGlobals->curtime;
			const int mode = RPMWorld::NPCWeaponLaw( pCop );
			const bool bRepeat = gpGlobals->curtime - st->flLastOffence < 60.0f;
			switch ( st->stage )
			{
			case STAGE_ATTACKED:
				// a known offender: the enforcers that attacked hate it, these are others (CanAct)
				Attack( pCop, pOff, flDist );
				break;
			case STAGE_NONE:
				if ( mode == 3 || bRepeat )
				{
					st->stage = STAGE_ATTACKED;
					Attack( pCop, pOff, flDist );
					break;
				}
				st->stage = STAGE_WARNED;
				st->flWarned = gpGlobals->curtime;
				st->hWarner = pCop;
				Face( pCop, pOff, flGrace + 2.0f );
				Speak( pCop, "warn" );
				if ( pOff->IsPlayer() )
					RPMLogic::Toast( ToBasePlayer( pOff ), "hint", Russian() ? "Оружие запрещено — уберите его (H)" : "Weapons are banned - holster it (H)", "" );
				if ( rpm_weapon_law_debug.GetBool() )
					Msg( "[weaponlaw] %s warns %s (%.0f s)\n", pCop->GetDebugName(), pOff->GetDebugName(), flGrace );
				break;
			case STAGE_WARNED:
				Face( pCop, pOff, 2.0f );
				if ( gpGlobals->curtime - st->flWarned > flGrace * 0.55f )
				{
					st->stage = STAGE_FINAL;
					Speak( pCop, "final" );
				}
				break;
			case STAGE_FINAL:
				if ( gpGlobals->curtime - st->flWarned > flGrace )
				{
					st->stage = STAGE_ATTACKED;
					Attack( pCop, pOff, flDist );
				}
				break;
			}
		}
	}

private:
	float m_flNext;
};

static CRPMWeaponLaw g_RPMWeaponLaw;

CON_COMMAND( rpm_weapon_law_dump, "HL2RPM: enforcers, civilians and the state of the weapon ban" )
{
	CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *p = ppAIs[i];
		if ( !p || !p->IsAlive() )
			continue;
		int law = RPMWorld::NPCWeaponLaw( p );
		if ( law )
			Msg( "%-28s %s%s\n", p->GetDebugName(), law == 1 ? "civilian" : law == 2 ? "enforcer (warns)" : "enforcer (fires)",
				law == 1 && IsArmed( p ) ? ", ARMED" : "" );
	}
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( pPlayer )
		Msg( "player: %s%s\n", IsCivilian( pPlayer ) ? "civilian" : "in uniform", IsArmed( pPlayer ) ? ", ARMED" : "" );
	for ( int i = 0; i < s_Offenders.Count(); i++ )
		if ( s_Offenders[i].hEnt.Get() )
			Msg( "offender %s: stage %d, warned %.1f s ago\n", s_Offenders[i].hEnt->GetDebugName(), s_Offenders[i].stage, gpGlobals->curtime - s_Offenders[i].flWarned );
}

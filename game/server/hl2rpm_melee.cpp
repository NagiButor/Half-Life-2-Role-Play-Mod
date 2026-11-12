//========= HL2RPM ============================================================//
//
// The melee weapons of the user's "HL2 Melee Pack" (Garry's Mod): axe, bottle, broken bottle,
// hook, frying pan, pickaxe, pipe, pot, shovel. One class, a table: damage (the pack's numbers),
// rate, reach, the delay of the blow inside the swing animation, sharp or blunt.
//
//   attack     - a swing; the blow lands GetHitDelay() later (Mapbase's delayed bludgeon hit)
//   attack2    - a shove: pushes an NPC / a physics object away, a little damage
//   NPCs       - hold them like the crowbar (melee activities, EVENT_WEAPON_MELEE_HIT)
//
// Damage goes through the RPG: DMG_SLASH / DMG_CLUB = the skill "melee" (hl2rpm_char.cpp).
// Models: models/weapons/hl2rpm/{v,w}_<name>.mdl (hl2rpm_extras/tools/make_melee_weapons.py);
// the view models have no hands - the client bone-merges the hands of the suit (hl2rpm_hands.cpp).
// Scripts: scripts/weapon_rpm_<name>.txt.
//
//=============================================================================//
#include "cbase.h"
#include "basebludgeonweapon.h"
#include "npcevent.h"
#include "ai_basenpc.h"
#include "player.h"
#include "in_buttons.h"
#include "soundent.h"
#include "vphysics/constraints.h"
#include "activitylist.h"
#include "hl2rpm_rpgdefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar rpm_melee_debug( "rpm_melee_debug", "0", FCVAR_NONE, "HL2RPM: print the swings of NPCs with melee weapons" );
ConVar sk_rpm_melee_npc_scale( "sk_rpm_melee_npc_scale", "0.5", FCVAR_NONE, "HL2RPM: the damage of a melee weapon in an NPC's hands (x the player's)" );

struct RPMMeleeDef_t
{
	const char *cls;
	float dmgMin, dmgMax;	// one blow (HL2 Melee Pack)
	float rate;				// seconds between swings
	float range;			// reach (units)
	float delay;			// the blow lands this long after the swing starts
	float push;				// how hard the shove / blow throws an NPC (- pulls: the hook)
	bool sharp;				// DMG_SLASH (blood) or DMG_CLUB
	float kick;				// view punch
};

static const RPMMeleeDef_t s_Melee[] =
{
	{ "weapon_rpm_axe",				34, 40, 1.00f, 58, 0.40f, 300, true,  2.0f },
	{ "weapon_rpm_bottle",			 4,  8, 0.50f, 50, 0.20f, 100, false, 0.8f },
	{ "weapon_rpm_brokenbottle",	 8, 15, 0.50f, 46, 0.20f,   0, true,  0.8f },
	{ "weapon_rpm_hook",			34, 50, 1.25f, 64, 0.40f, -350, true, 2.0f },
	{ "weapon_rpm_pan",				12, 18, 1.00f, 62, 0.20f, 300, false, 1.4f },
	{ "weapon_rpm_pickaxe",			34, 50, 1.35f, 64, 0.40f, 400, true,  2.4f },
	{ "weapon_rpm_pipe",			20, 27, 1.10f, 64, 0.20f, 400, false, 1.6f },
	{ "weapon_rpm_pot",				10, 15, 0.90f, 56, 0.20f, 200, false, 1.2f },
	{ "weapon_rpm_shovel",			24, 32, 1.25f, 76, 0.40f, 600, false, 2.0f },
	{ "weapon_rpm_fists",			10, 16, 0.60f, 44, 0.00f, 150, false, 1.0f },
};

static const RPMMeleeDef_t *FindMelee( const char *cls )
{
	for ( int i = 0; i < ARRAYSIZE( s_Melee ); i++ )
		if ( !Q_stricmp( cls, s_Melee[i].cls ) )
			return &s_Melee[i];
	return &s_Melee[6];		// (the pipe)
}

class CWeaponRPMMelee : public CBaseHLBludgeonWeapon
{
	DECLARE_CLASS( CWeaponRPMMelee, CBaseHLBludgeonWeapon );
public:
	DECLARE_ACTTABLE();

	const RPMMeleeDef_t &Def() { return *FindMelee( GetClassname() ); }

	virtual float GetRange( void )		{ return Def().range; }
	virtual float GetFireRate( void )	{ return Def().rate; }
	virtual float GetHitDelay()			{ return GetOwner() && GetOwner()->IsPlayer() ? Def().delay : 0.0f; }
	virtual int GetDamageType()			{ return Def().sharp ? DMG_SLASH : DMG_CLUB; }

	virtual float GetDamageForActivity( Activity hitActivity )
	{
		float dmg = RandomFloat( Def().dmgMin, Def().dmgMax );
		if ( GetOwner() && !GetOwner()->IsPlayer() )
			dmg *= sk_rpm_melee_npc_scale.GetFloat();
		return dmg;
	}

	virtual void AddViewKick( void )
	{
		CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
		if ( !pPlayer )
			return;
		const float k = Def().kick;
		pPlayer->ViewPunch( QAngle( RandomFloat( 0.5f, 1.0f ) * k, RandomFloat( -1.0f, -0.5f ) * k, 0 ) );
	}

	// the shove (the pack's "pushback")
	virtual void SecondaryAttack( void )
	{
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
		if ( !pOwner )
			return;
		SendWeaponAnim( ACT_VM_MISSCENTER2 );
		pOwner->SetAnimation( PLAYER_ATTACK1 );
		WeaponSound( SINGLE );
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.35f;
		m_flNextSecondaryAttack = gpGlobals->curtime + 1.0f;

		Vector start = pOwner->Weapon_ShootPosition(), fwd;
		pOwner->EyeVectors( &fwd );
		trace_t tr;
		UTIL_TraceHull( start, start + fwd * 60.0f, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ), MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &tr );
		CBaseEntity *pHit = tr.m_pEnt;
		if ( !pHit || pHit->IsWorld() )
			return;
		WeaponSound( MELEE_HIT );
		Vector push = fwd;
		push.z = 0.15f;
		VectorNormalize( push );
		CTakeDamageInfo info( pOwner, pOwner, 2.0f, DMG_CLUB );
		info.SetDamagePosition( tr.endpos );
		info.SetDamageForce( push * 8000.0f );
		pHit->DispatchTraceAttack( info, fwd, &tr );
		ApplyMultiDamage();
		if ( pHit->MyNPCPointer() || pHit->IsPlayer() )
			pHit->ApplyAbsVelocityImpulse( push * 350.0f );
		else if ( pHit->VPhysicsGetObject() )
			pHit->VPhysicsGetObject()->ApplyForceCenter( push * 350.0f * pHit->VPhysicsGetObject()->GetMass() );
		CSoundEnt::InsertSound( SOUND_COMBAT, tr.endpos, 300, 0.2f, pOwner );
	}

	// NPCs: as the crowbar (lead the target, reach ~ the weapon's)
	virtual int WeaponMeleeAttack1Condition( float flDot, float flDist )
	{
		CAI_BaseNPC *pNPC = GetOwner() ? GetOwner()->MyNPCPointer() : NULL;
		CBaseEntity *pEnemy = pNPC ? pNPC->GetEnemy() : NULL;
		if ( !pEnemy )
			return COND_NONE;
		Vector vecExtrapolated;
		VectorMA( pEnemy->WorldSpaceCenter(), 0.8f + RandomFloat( -0.3f, 0.2f ), pEnemy->GetSmoothedVelocity(), vecExtrapolated );
		Vector vecDelta = vecExtrapolated - pNPC->WorldSpaceCenter();
		if ( fabs( vecDelta.z ) > 70 )
			return COND_TOO_FAR_TO_ATTACK;
		Vector vecForward = pNPC->BodyDirection2D();
		vecDelta.z = 0.0f;
		const float reach = MAX( 64.0f, Def().range );
		float flExtrapolatedDist = Vector2DNormalize( vecDelta.AsVector2D() );
		if ( flDist > reach && flExtrapolatedDist > reach )
			return COND_TOO_FAR_TO_ATTACK;
		if ( flDot < 0.7f && DotProduct2D( vecDelta.AsVector2D(), vecForward.AsVector2D() ) < 0.7f )
			return COND_NOT_FACING_ATTACK;
		return COND_CAN_MELEE_ATTACK1;
	}

	virtual void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
	{
		if ( pEvent->event != EVENT_WEAPON_MELEE_HIT )
		{
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			return;
		}
		Vector vecDirection;
		AngleVectors( GetAbsAngles(), &vecDirection );
		CBaseEntity *pEnemy = pOperator->MyNPCPointer() ? pOperator->MyNPCPointer()->GetEnemy() : NULL;
		if ( pEnemy )
		{
			Vector vecDelta = pEnemy->WorldSpaceCenter() - pOperator->Weapon_ShootPosition();
			VectorNormalize( vecDelta );
			Vector2D d2 = vecDelta.AsVector2D();
			Vector2DNormalize( d2 );
			if ( DotProduct2D( d2, vecDirection.AsVector2D() ) > 0.8f )
				vecDirection = vecDelta;
		}
		Vector vecEnd;
		VectorMA( pOperator->Weapon_ShootPosition(), MAX( 50.0f, Def().range * 0.8f ), vecDirection, vecEnd );
		CBaseEntity *pHurt = pOperator->CheckTraceHullAttack( pOperator->Weapon_ShootPosition(), vecEnd,
			Vector( -16, -16, -16 ), Vector( 36, 36, 36 ), GetDamageForActivity( ACT_VM_HITCENTER ), GetDamageType(), 0.75f );
		if ( rpm_melee_debug.GetBool() )
			Msg( "[melee] %s (%s) swings %s: %s\n", pOperator->GetDebugName(), pOperator->GetClassname(), GetClassname(),
				pHurt ? pHurt->GetDebugName() : "miss" );
		if ( pHurt )
		{
			WeaponSound( MELEE_HIT );
			trace_t traceHit;
			UTIL_TraceLine( pOperator->Weapon_ShootPosition(), pHurt->GetAbsOrigin(), MASK_SHOT_HULL, pOperator, COLLISION_GROUP_NONE, &traceHit );
			ImpactEffect( traceHit );
		}
		else
		{
			WeaponSound( MELEE_MISS );
		}
	}
};

// one networked class per weapon: the client learns the class name (its script, view model FOV)
// from the class (c_weapon__stubs_hl2.cpp: a stub each); the table of the weapons is shared
#define RPM_MELEE( name, cls ) 	class C##cls : public CWeaponRPMMelee { public: DECLARE_CLASS( C##cls, CWeaponRPMMelee ); DECLARE_SERVERCLASS(); }; 	IMPLEMENT_SERVERCLASS_ST( C##cls, DT_##cls ) END_SEND_TABLE() 	LINK_ENTITY_TO_CLASS( name, C##cls ); 	PRECACHE_WEAPON_REGISTER( name );
RPM_MELEE( weapon_rpm_axe, WeaponRPMAxe )
RPM_MELEE( weapon_rpm_bottle, WeaponRPMBottle )
RPM_MELEE( weapon_rpm_brokenbottle, WeaponRPMBrokenBottle )
RPM_MELEE( weapon_rpm_hook, WeaponRPMHook )
RPM_MELEE( weapon_rpm_pan, WeaponRPMPan )
RPM_MELEE( weapon_rpm_pickaxe, WeaponRPMPickaxe )
RPM_MELEE( weapon_rpm_pipe, WeaponRPMPipe )
RPM_MELEE( weapon_rpm_pot, WeaponRPMPot )
RPM_MELEE( weapon_rpm_shovel, WeaponRPMShovel )

//-----------------------------------------------------------------------------
// Fists (NPCs): an invisible "weapon" of the animation library - a boxing guard
// (ACT_RPM_FIST_STANCE) while there is an enemy, punches (ACT_RPM_FIST_PUNCH: jab,
// cross, hook, uppercut) with the blow event 3001 on the frame the fist lands. Unarmed
// NPCs that fight raise their fists themselves (rule npc_fists, CRPMFistsSystem) and lower
// them when the fight is over. Bare fists don't break the weapon ban; they never drop.
//-----------------------------------------------------------------------------
static Activity RPMActivity( const char *pszName )
{
	int i = ActivityList_IndexForName( pszName );
	return i > 0 ? (Activity)i : ACT_INVALID;
}

class CWeaponRPMFists : public CWeaponRPMMelee
{
	DECLARE_CLASS( CWeaponRPMFists, CWeaponRPMMelee );
public:
	DECLARE_SERVERCLASS();

	virtual void Spawn()
	{
		BaseClass::Spawn();
		AddEffects( EF_NODRAW );
	}
	virtual void Equip( CBaseCombatCharacter *pOwner )
	{
		BaseClass::Equip( pOwner );
		AddEffects( EF_NODRAW );
	}
	virtual bool IsWeaponVisible() { return false; }
	virtual bool CanBePickedUpByNPCs() { return false; }
	virtual void Drop( const Vector &vecVelocity ) { UTIL_Remove( this ); }

	virtual Activity ActivityOverride( Activity baseAct, bool *pRequired )
	{
		CBaseCombatCharacter *pOwner = GetOwner();
		CAI_BaseNPC *pNPC = pOwner ? pOwner->MyNPCPointer() : NULL;
		if ( pNPC )
		{
			Activity punch = RPMActivity( "ACT_RPM_FIST_PUNCH" );
			Activity stance = RPMActivity( "ACT_RPM_FIST_STANCE" );
			if ( baseAct == ACT_MELEE_ATTACK1 && punch != ACT_INVALID && pNPC->HaveSequenceForActivity( punch ) )
			{
				if ( pRequired )
					*pRequired = true;
				return punch;
			}
			if ( ( baseAct == ACT_IDLE_ANGRY || baseAct == ACT_IDLE || baseAct == ACT_IDLE_ANGRY_MELEE ) && stance != ACT_INVALID &&
				 pNPC->GetEnemy() && pNPC->HaveSequenceForActivity( stance ) )
				return stance;
			// otherwise as an unarmed character (no melee walk / idle of a club)
			if ( baseAct == ACT_IDLE || baseAct == ACT_RUN || baseAct == ACT_WALK || baseAct == ACT_IDLE_ANGRY )
				return baseAct;
		}
		return BaseClass::ActivityOverride( baseAct, pRequired );
	}
};

IMPLEMENT_SERVERCLASS_ST( CWeaponRPMFists, DT_WeaponRPMFists )
END_SEND_TABLE()
LINK_ENTITY_TO_CLASS( weapon_rpm_fists, CWeaponRPMFists );
PRECACHE_WEAPON_REGISTER( weapon_rpm_fists );

class CRPMFistsSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMFistsSystem() : CAutoGameSystemPerFrame( "CRPMFistsSystem" ), m_flNext( 0 ) {}
	virtual void LevelInitPostEntity() { m_flNext = 0; }
	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.5f;
		if ( RPMRPG::Rule( "npc_fists" ) == 0.0f )
			return;
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *p = ppAIs[i];
			if ( !p || !p->IsAlive() || p->IsInAScript() || !( p->CapabilitiesGet() & bits_CAP_USE_WEAPONS ) )
				continue;
			if ( !p->HaveSequenceForActivity( ACT_IDLE ) || RPMActivity( "ACT_RPM_FIST_PUNCH" ) == ACT_INVALID ||
				 !p->HaveSequenceForActivity( RPMActivity( "ACT_RPM_FIST_PUNCH" ) ) )
				continue;
			CBaseCombatWeapon *pW = p->GetActiveWeapon();
			const bool bFists = pW && FClassnameIs( pW, "weapon_rpm_fists" );
			CBaseEntity *pEnemy = p->GetEnemy();
			const bool bFight = pEnemy && pEnemy->IsAlive() && p->IRelationType( pEnemy ) == D_HT &&
				( p->GetAbsOrigin() - pEnemy->GetAbsOrigin() ).Length() < 900.0f;
			if ( !pW && bFight )
			{
				CBaseCombatWeapon *pFists = (CBaseCombatWeapon *)CBaseEntity::Create( "weapon_rpm_fists", p->GetAbsOrigin(), p->GetAbsAngles(), p );
				if ( pFists )
				{
					p->Weapon_Equip( pFists );
					if ( rpm_melee_debug.GetBool() )
						Msg( "[melee] %s raises its fists against %s\n", p->GetDebugName(), pEnemy->GetDebugName() );
				}
			}
			else if ( bFists && !pEnemy && gpGlobals->curtime - p->GetLastEnemyTime() > 8.0f )
			{
				p->Weapon_Drop( pW );		// removes itself
			}
		}
	}
private:
	float m_flNext;
};
static CRPMFistsSystem g_RPMFists;

acttable_t CWeaponRPMMelee::m_acttable[] =
{
	{ ACT_MELEE_ATTACK1,	ACT_MELEE_ATTACK_SWING, true },
	{ ACT_IDLE,				ACT_IDLE_ANGRY_MELEE,	false },
	{ ACT_IDLE_ANGRY,		ACT_IDLE_ANGRY_MELEE,	false },
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_RUN,				ACT_RUN_MELEE,			false },
	{ ACT_WALK,				ACT_WALK_MELEE,			false },
	{ ACT_ARM,				ACT_ARM_MELEE,			false },
	{ ACT_DISARM,			ACT_DISARM_MELEE,		false },
#endif
	{ ACT_RANGE_ATTACK1,				ACT_RANGE_ATTACK_SLAM,					true },
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_MELEE,					false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_MELEE,					false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_MELEE,			false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_MELEE,			false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_MELEE,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_MELEE,			false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_MELEE,					false },
};
IMPLEMENT_ACTTABLE( CWeaponRPMMelee );

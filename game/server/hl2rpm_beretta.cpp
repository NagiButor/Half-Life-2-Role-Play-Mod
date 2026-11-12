//========= HL2RPM ============================================================//
//
// The Beretta 92 (weapon_rpm_beretta): the user's "Dual Berettas" addon rethought as one pistol.
// The models are made by hl2rpm_extras/tools/make_beretta.py: the right Beretta of CS:S's Elites
// in the hands of CS:S's single-pistol animation, on the ValveBiped arm bones - the hands of the
// suit are bone-merged onto them ("uses_hands" in scripts/weapon_rpm_beretta.txt).
//
// One pistol: a 15-round magazine, semi-automatic (a shot per press, as fast as the trigger is
// pulled; held down - GetFireRate()), the accuracy of the HL2 pistol (it spreads with quick shots
// and settles). The slide stays back after the last round (shoot_last -> idle_empty, draw_empty);
// a reload from empty drops it (reload_empty), a reload with a round in the chamber leaves it
// forward (reload). Damage: sk_plr_dmg_rpm_beretta / sk_npc_dmg_rpm_beretta (the RPG skill
// "guns" scales it on top, RPMChar::ModifyDamage). NPCs hold it as a pistol (the pistol's
// activity table).
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "in_buttons.h"
#include "soundent.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sk_plr_dmg_rpm_beretta( "sk_plr_dmg_rpm_beretta", "9", FCVAR_NONE, "Beretta 92: damage of a player's shot" );
ConVar sk_npc_dmg_rpm_beretta( "sk_npc_dmg_rpm_beretta", "5", FCVAR_NONE, "Beretta 92: damage of an NPC's shot" );

extern acttable_t *GetPistolActtable();
extern int GetPistolActtableCount();

#define BERETTA_FASTEST_REFIRE		0.12f	// as fast as the trigger can be pulled
#define BERETTA_FASTEST_DRY_REFIRE	0.25f
#define BERETTA_PENALTY_PER_SHOT	0.22f	// seconds of "unsteadiness" a shot adds
#define BERETTA_PENALTY_MAX			1.4f

class CWeaponRPMBeretta : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponRPMBeretta, CBaseHLCombatWeapon );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	CWeaponRPMBeretta();

	void		ItemPreFrame() { UpdatePenaltyTime(); BaseClass::ItemPreFrame(); }
	void		ItemBusyFrame() { UpdatePenaltyTime(); BaseClass::ItemBusyFrame(); }
	void		ItemPostFrame();
	void		PrimaryAttack();
	void		DryFire();
	bool		Reload();
	void		WeaponIdle();
	Activity	GetDrawActivity() { return m_iClip1 > 0 ? ACT_VM_DRAW : ACT_VM_DRAW_EMPTY; }
	void		AddViewKick();

	const Vector &GetBulletSpread()
	{
		static Vector npcCone = VECTOR_CONE_4DEGREES;
		if ( GetOwner() && GetOwner()->IsNPC() )
			return npcCone;
		// from very accurate to loose as the shots come quickly
		static Vector cone;
		const float ramp = RemapValClamped( m_flAccuracyPenalty, 0.0f, BERETTA_PENALTY_MAX, 0.0f, 1.0f );
		VectorLerp( VECTOR_CONE_1DEGREES, VECTOR_CONE_5DEGREES, ramp, cone );
		return cone;
	}

	int			CapabilitiesGet() { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int			GetMinBurst() { return 1; }
	int			GetMaxBurst() { return 3; }
	float		GetFireRate() { return 0.4f; }		// held down; and the NPCs' pace

	void		Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );
	void		FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, const Vector &vecShootOrigin, const Vector &vecShootDir );
	void		Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary );

	// a pistol for the NPCs' animations
	acttable_t	*ActivityList() { return GetPistolActtable(); }
	int			ActivityListCount() { return GetPistolActtableCount(); }
	acttable_t	*GetBackupActivityList() { return NULL; }
	int			GetBackupActivityListCount() { return 0; }

private:
	void		UpdatePenaltyTime();

	float		m_flSoonestPrimaryAttack;
	float		m_flLastAttackTime;
	float		m_flAccuracyPenalty;
};

IMPLEMENT_SERVERCLASS_ST( CWeaponRPMBeretta, DT_WeaponRPMBeretta )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_rpm_beretta, CWeaponRPMBeretta );
PRECACHE_WEAPON_REGISTER( weapon_rpm_beretta );

BEGIN_DATADESC( CWeaponRPMBeretta )
	DEFINE_FIELD( m_flSoonestPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( m_flLastAttackTime, FIELD_TIME ),
	DEFINE_FIELD( m_flAccuracyPenalty, FIELD_FLOAT ),
END_DATADESC()

CWeaponRPMBeretta::CWeaponRPMBeretta()
{
	m_flSoonestPrimaryAttack = gpGlobals->curtime;
	m_flLastAttackTime = 0.0f;
	m_flAccuracyPenalty = 0.0f;
	m_fMinRange1 = 24;
	m_fMaxRange1 = 1500;
	m_fMinRange2 = 24;
	m_fMaxRange2 = 200;
	m_bFiresUnderwater = true;
}

void CWeaponRPMBeretta::UpdatePenaltyTime()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;
	if ( !( pOwner->m_nButtons & IN_ATTACK ) && m_flSoonestPrimaryAttack < gpGlobals->curtime )
		m_flAccuracyPenalty = clamp( m_flAccuracyPenalty - gpGlobals->frametime, 0.0f, BERETTA_PENALTY_MAX );
}

//-----------------------------------------------------------------------------
// One round per pull of the trigger
//-----------------------------------------------------------------------------
void CWeaponRPMBeretta::PrimaryAttack()
{
	if ( m_iClip1 <= 0 )
	{
		DryFire();
		return;
	}
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	m_flLastAttackTime = gpGlobals->curtime;
	m_flSoonestPrimaryAttack = gpGlobals->curtime + BERETTA_FASTEST_REFIRE;
	m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();
	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pPlayer );
	// (as the HL2 pistol: the punch is reset every shot, quick shots do not walk the aim off)
	pPlayer->ViewPunchReset();

	pPlayer->DoMuzzleFlash();
	// the last round: the slide stays back
	SendWeaponAnim( m_iClip1 == 1 ? ACT_VM_PRIMARYATTACK_EMPTY : ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );
	WeaponSound( SINGLE );
	m_iClip1--;

	FireBulletsInfo_t info;
	info.m_vecSrc = pPlayer->Weapon_ShootPosition();
	info.m_vecDirShooting = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	info.m_iShots = 1;
	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 2;
	info.m_flDamage = sk_plr_dmg_rpm_beretta.GetFloat();
	info.m_vecSpread = pPlayer->GetAttackSpread( this );
	pPlayer->FireBullets( info );

	if ( !m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );

	AddViewKick();
	m_flAccuracyPenalty += BERETTA_PENALTY_PER_SHOT;
	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );
}

void CWeaponRPMBeretta::DryFire()
{
	WeaponSound( EMPTY );
	SendWeaponAnim( ACT_VM_DRYFIRE );
	m_flSoonestPrimaryAttack = gpGlobals->curtime + BERETTA_FASTEST_DRY_REFIRE;
	m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
}

void CWeaponRPMBeretta::ItemPostFrame()
{
	BaseClass::ItemPostFrame();
	if ( m_bInReload )
		return;
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;
	// as fast as the trigger is pulled
	if ( !( pOwner->m_nButtons & IN_ATTACK ) && m_flSoonestPrimaryAttack < gpGlobals->curtime )
		m_flNextPrimaryAttack = gpGlobals->curtime - 0.1f;
	else if ( ( pOwner->m_nButtons & IN_ATTACK ) && m_flNextPrimaryAttack < gpGlobals->curtime && m_iClip1 <= 0 )
		DryFire();
}

//-----------------------------------------------------------------------------
// Reload: from empty the slide is released at the end; with a round in the chamber it stays
//-----------------------------------------------------------------------------
bool CWeaponRPMBeretta::Reload()
{
	const bool bEmpty = ( m_iClip1 <= 0 );
	if ( !DefaultReload( GetMaxClip1(), GetMaxClip2(), bEmpty ? ACT_VM_RELOAD_EMPTY : ACT_VM_RELOAD ) )
		return false;
	m_flAccuracyPenalty = 0.0f;
	return true;
}

void CWeaponRPMBeretta::WeaponIdle()
{
	if ( HasWeaponIdleTimeElapsed() )
		SendWeaponAnim( m_iClip1 > 0 ? ACT_VM_IDLE : ACT_VM_IDLE_EMPTY );
}

void CWeaponRPMBeretta::AddViewKick()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;
	// a 9 mm in one hand kicks a little more than the HL2 pistol
	QAngle punch( random->RandomFloat( -0.9f, -0.5f ), random->RandomFloat( -0.5f, 0.5f ), 0.0f );
	pPlayer->ViewPunch( punch );
}

//-----------------------------------------------------------------------------
// NPCs
//-----------------------------------------------------------------------------
void CWeaponRPMBeretta::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, const Vector &vecShootOrigin, const Vector &vecShootDir )
{
	CSoundEnt::InsertSound( SOUND_COMBAT | SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );
	WeaponSound( SINGLE_NPC );

	FireBulletsInfo_t info;
	info.m_vecSrc = vecShootOrigin;
	info.m_vecDirShooting = vecShootDir;
	info.m_iShots = 1;
	info.m_vecSpread = VECTOR_CONE_PRECALCULATED;
	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 2;
	info.m_flDamage = sk_npc_dmg_rpm_beretta.GetFloat();
	pOperator->FireBullets( info );
	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

void CWeaponRPMBeretta::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	if ( pEvent->event == EVENT_WEAPON_PISTOL_FIRE )
	{
		CAI_BaseNPC *pNPC = pOperator->MyNPCPointer();
		if ( pNPC )
		{
			const Vector vecShootOrigin = pOperator->Weapon_ShootPosition();
			const Vector vecShootDir = pNPC->GetActualShootTrajectory( vecShootOrigin );
			FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
		}
		return;
	}
	BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
}

void CWeaponRPMBeretta::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary )
{
	m_iClip1++;
	Vector vecShootOrigin, vecShootDir;
	QAngle angShootDir;
	GetAttachment( LookupAttachment( "muzzle" ), vecShootOrigin, angShootDir );
	AngleVectors( angShootDir, &vecShootDir );
	FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
}

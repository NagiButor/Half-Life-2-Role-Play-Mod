//========= Copyright Valve Corporation, All rights reserved. ============//
//
// XM1014 – CSS-style semi-auto shotgun
// Ported for HL2 / Mapbase Episodic
// No pump, shell-by-shell reload
//
//=============================================================================

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon_shared.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "vstdlib/random.h"
#include "gamestats.h"
#include "inventory_system.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar sk_auto_reload_time;

#ifdef MAPBASE
// *** ДОБАВЛЯЕМ ОПРЕДЕЛЕНИЕ ConVar (не extern!) ***
ConVar sk_npc_num_xm1014_pellets("sk_npc_num_xm1014_pellets", "8", FCVAR_NONE, "Number of pellets XM1014 fires from NPC");
#endif

//-----------------------------------------------------------------------------
// Weapon class
//-----------------------------------------------------------------------------
class CWeaponXM1014 : public CBaseHLCombatWeapon
{
public:
    DECLARE_CLASS(CWeaponXM1014, CBaseHLCombatWeapon);
    DECLARE_SERVERCLASS();
    DECLARE_ACTTABLE();

    CWeaponXM1014(void);

    int		CapabilitiesGet(void) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

    virtual const Vector& GetBulletSpread(void)
    {
        static Vector cone = VECTOR_CONE_6DEGREES;
        return cone;
    }

    virtual int		GetMinBurst() { return 1; }
    virtual int		GetMaxBurst() { return 3; }
    virtual float	GetMinRestTime();
    virtual float	GetMaxRestTime();
    virtual float	GetFireRate(void);

    bool	StartReload(void);
    bool	Reload(void);
    void	FillClip(void);
    void	FinishReload(void);
    void	ItemHolsterFrame(void);
    void	ItemPostFrame(void);
    void	PrimaryAttack(void);
    void	DryFire(void);

    void	FireNPCPrimaryAttack(CBaseCombatCharacter *pOperator, bool bUseWeaponAngles);
    void	Operator_ForceNPCFire(CBaseCombatCharacter *pOperator, bool bSecondary);
    void	Operator_HandleAnimEvent(animevent_t *pEvent, CBaseCombatCharacter *pOperator);

    void	Precache(void);

private:
    bool	m_bNeedPump;		// When emptied completely
    bool	m_bDelayedFire1;	// Fire when finished reloading

    CWeaponXM1014(const CWeaponXM1014&);
};

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST(CWeaponXM1014, DT_WeaponXM1014)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS(weapon_xm1014, CWeaponXM1014);
PRECACHE_WEAPON_REGISTER(weapon_xm1014);

//-----------------------------------------------------------------------------
// Activity Table
//-----------------------------------------------------------------------------
acttable_t CWeaponXM1014::m_acttable[] =
{
    { ACT_IDLE,						ACT_IDLE_SMG1,					true },
    { ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_SHOTGUN,		true },
    { ACT_RELOAD,					ACT_RELOAD_SHOTGUN,				false },
    { ACT_WALK,						ACT_WALK_RIFLE,					true },
    { ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_SHOTGUN,			true },
    { ACT_WALK_AIM,					ACT_WALK_AIM_SHOTGUN,			true },
    { ACT_WALK_CROUCH,				ACT_WALK_CROUCH_RIFLE,			true },
    { ACT_WALK_CROUCH_AIM,			ACT_WALK_CROUCH_AIM_RIFLE,		true },
    { ACT_RUN,						ACT_RUN_RIFLE,					true },
    { ACT_RUN_AIM,					ACT_RUN_AIM_SHOTGUN,			true },
    { ACT_RUN_CROUCH,				ACT_RUN_CROUCH_RIFLE,			true },
    { ACT_RUN_CROUCH_AIM,			ACT_RUN_CROUCH_AIM_RIFLE,		true },
    { ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_SHOTGUN,	true },
    { ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_SHOTGUN_LOW,	true },
    { ACT_RELOAD_LOW,				ACT_RELOAD_SHOTGUN_LOW,			false },
    { ACT_GESTURE_RELOAD,			ACT_GESTURE_RELOAD_SHOTGUN,		false },
};

IMPLEMENT_ACTTABLE(CWeaponXM1014);

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponXM1014::CWeaponXM1014(void)
{
    m_bReloadsSingly = true;
    m_bNeedPump = false;
    m_bDelayedFire1 = false;

    m_fMinRange1 = 0.0f;
    m_fMaxRange1 = 2048.0f;

    m_iClip1 = 8;
}

//-----------------------------------------------------------------------------
// Precache
//-----------------------------------------------------------------------------
void CWeaponXM1014::Precache(void)
{
    CBaseCombatWeapon::Precache();
}

//-----------------------------------------------------------------------------
// GetMinRestTime
//-----------------------------------------------------------------------------
float CWeaponXM1014::GetMinRestTime()
{
    return BaseClass::GetMinRestTime();
}

//-----------------------------------------------------------------------------
// GetMaxRestTime
//-----------------------------------------------------------------------------
float CWeaponXM1014::GetMaxRestTime()
{
    return BaseClass::GetMaxRestTime();
}

//-----------------------------------------------------------------------------
// GetFireRate - Semi-auto fire rate
//-----------------------------------------------------------------------------
float CWeaponXM1014::GetFireRate(void)
{
    return 0.01f;  // Было 0.25f, теперь 0.15f - быстрее!
}

//-----------------------------------------------------------------------------
// StartReload - Override so only reload one shell at a time
//-----------------------------------------------------------------------------
bool CWeaponXM1014::StartReload(void)
{
    CBaseCombatCharacter *pOwner = GetOwner();

    if (pOwner == NULL)
        return false;

    if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
        return false;

    if (m_iClip1 >= GetMaxClip1())
        return false;

    // If shotgun totally emptied then a pump animation is needed
    if (m_iClip1 <= 0)
    {
        m_bNeedPump = true;
    }

    int j = MIN(1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));

    if (j <= 0)
        return false;

    SendWeaponAnim(ACT_SHOTGUN_RELOAD_START);

    // Make shotgun shell visible
    SetBodygroup(1, 0);

    pOwner->m_flNextAttack = gpGlobals->curtime;
    m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();

#ifdef MAPBASE
    if (pOwner->IsPlayer())
    {
        static_cast<CBasePlayer*>(pOwner)->SetAnimation(PLAYER_RELOAD);
    }
#endif

    m_bInReload = true;
    return true;
}

//-----------------------------------------------------------------------------
// Reload - Override so only reload one shell at a time
//-----------------------------------------------------------------------------
bool CWeaponXM1014::Reload(void)
{
    // Check that StartReload was called first
    if (!m_bInReload)
    {
        Warning("ERROR: XM1014 Reload called incorrectly!\n");
    }

    CBaseCombatCharacter *pOwner = GetOwner();

    if (pOwner == NULL)
        return false;

    if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
        return false;

    if (m_iClip1 >= GetMaxClip1())
        return false;

    int j = MIN(1, pOwner->GetAmmoCount(m_iPrimaryAmmoType));

    if (j <= 0)
        return false;

    FillClip();
    // Play reload on different channel as otherwise steals channel away from fire sound
    WeaponSound(RELOAD);
    SendWeaponAnim(ACT_VM_RELOAD);

    pOwner->m_flNextAttack = gpGlobals->curtime;
    m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();

    return true;
}

//-----------------------------------------------------------------------------
// FinishReload
//-----------------------------------------------------------------------------
void CWeaponXM1014::FinishReload(void)
{
    // Make shotgun shell invisible
    SetBodygroup(1, 1);

    CBaseCombatCharacter *pOwner = GetOwner();

    if (pOwner == NULL)
        return;

    m_bInReload = false;

    // Finish reload animation
    SendWeaponAnim(ACT_SHOTGUN_RELOAD_FINISH);

    pOwner->m_flNextAttack = gpGlobals->curtime;
    m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
}

//-----------------------------------------------------------------------------
// FillClip
//-----------------------------------------------------------------------------
void CWeaponXM1014::FillClip(void)
{
    CBaseCombatCharacter *pOwner = GetOwner();

    if (pOwner == NULL)
        return;

    // Add them to the clip
    if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) > 0)
    {
        if (Clip1() < GetMaxClip1())
        {
            m_iClip1++;
            pOwner->RemoveAmmo(1, m_iPrimaryAmmoType);
        }
    }
}

//-----------------------------------------------------------------------------
// DryFire
//-----------------------------------------------------------------------------
void CWeaponXM1014::DryFire(void)
{
    WeaponSound(EMPTY);
    SendWeaponAnim(ACT_VM_DRYFIRE);

    m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
}

//-----------------------------------------------------------------------------
// PrimaryAttack - Player fire
//-----------------------------------------------------------------------------
void CWeaponXM1014::PrimaryAttack(void)
{
    // Only the player fires this way so we can cast
    CBasePlayer *pPlayer = ToBasePlayer(GetOwner());

    if (!pPlayer)
    {
        return;
    }

    // MUST call sound before removing a round from the clip
    WeaponSound(SINGLE);

    pPlayer->DoMuzzleFlash();

    SendWeaponAnim(ACT_VM_PRIMARYATTACK);

    // player "shoot" animation
    pPlayer->SetAnimation(PLAYER_ATTACK1);

    // Don't fire again until fire animation has completed
    // *** ИСПРАВЛЕНИЕ: Используем жесткое значение вместо анимации ***
    m_flNextPrimaryAttack = gpGlobals->curtime + 0.25f;  // Очень быстро (как CSS)
    
    m_iClip1 -= 1;

    Vector	vecSrc = pPlayer->Weapon_ShootPosition();
    Vector	vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

    pPlayer->SetMuzzleFlashTime(gpGlobals->curtime + 1.0);

    // Fire the bullets
    pPlayer->FireBullets(8, vecSrc, vecAiming, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0, -1, -1, 0, NULL, true, true);

    pPlayer->ViewPunch(QAngle(random->RandomFloat(-2, -1), random->RandomFloat(-2, 2), 0));

    CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), SOUNDENT_VOLUME_SHOTGUN, 0.2, GetOwner());

    if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
    {
        // HEV suit - indicate out of ammo condition
        pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
    }

    if (m_iClip1)
    {
        // No pump needed for semi-auto, but we can set a delay if needed
        m_bNeedPump = false;
    }

    m_iPrimaryAttacks++;
    gamestats->Event_WeaponFired(pPlayer, true, GetClassname());
}

//-----------------------------------------------------------------------------
// FireNPCPrimaryAttack - NPC fire (КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ)
//-----------------------------------------------------------------------------
void CWeaponXM1014::FireNPCPrimaryAttack(CBaseCombatCharacter *pOperator, bool bUseWeaponAngles)
{
    Vector vecShootOrigin, vecShootDir;
    CAI_BaseNPC *npc = pOperator->MyNPCPointer();
    ASSERT(npc != NULL);

    // Звук выстрела NPC - ДО выстрела (как в weapon_shotgun)
    WeaponSound(SINGLE_NPC);

    pOperator->DoMuzzleFlash();
    m_iClip1 = m_iClip1 - 1;

    if (bUseWeaponAngles)
    {
        QAngle	angShootDir;
        GetAttachment(LookupAttachment("muzzle"), vecShootOrigin, angShootDir);
        AngleVectors(angShootDir, &vecShootDir);
    }
    else
    {
        vecShootOrigin = pOperator->Weapon_ShootPosition();
        vecShootDir = npc->GetActualShootTrajectory(vecShootOrigin);
    }

#ifdef MAPBASE
    pOperator->FireBullets(sk_npc_num_xm1014_pellets.GetInt(), vecShootOrigin, vecShootDir, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);
#else
    pOperator->FireBullets(8, vecShootOrigin, vecShootDir, GetBulletSpread(), MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);
#endif
}

//-----------------------------------------------------------------------------
// Operator_ForceNPCFire
//-----------------------------------------------------------------------------
void CWeaponXM1014::Operator_ForceNPCFire(CBaseCombatCharacter *pOperator, bool bSecondary)
{
    // Ensure we have enough rounds in the clip
    m_iClip1++;

    FireNPCPrimaryAttack(pOperator, true);
}

//-----------------------------------------------------------------------------
// Operator_HandleAnimEvent
//-----------------------------------------------------------------------------
void CWeaponXM1014::Operator_HandleAnimEvent(animevent_t *pEvent, CBaseCombatCharacter *pOperator)
{
    switch (pEvent->event)
    {
    case EVENT_WEAPON_SHOTGUN_FIRE:
    {
        FireNPCPrimaryAttack(pOperator, false);
    }
    break;

    default:
        CBaseCombatWeapon::Operator_HandleAnimEvent(pEvent, pOperator);
        break;
    }
}

//-----------------------------------------------------------------------------
// ItemHolsterFrame
//-----------------------------------------------------------------------------
void CWeaponXM1014::ItemHolsterFrame(void)
{
    // Must be player held
    if (GetOwner() && GetOwner()->IsPlayer() == false)
        return;

    // We can't be active
    if (GetOwner()->GetActiveWeapon() == this)
        return;

    // If it's been longer than three seconds, reload
    if ((gpGlobals->curtime - m_flHolsterTime) > sk_auto_reload_time.GetFloat())
    {
        // Reset the timer
        m_flHolsterTime = gpGlobals->curtime;

        if (GetOwner() == NULL)
            return;

        if (m_iClip1 == GetMaxClip1())
            return;

        // Just load the clip with no animations
        int ammoFill = MIN((GetMaxClip1() - m_iClip1), GetOwner()->GetAmmoCount(GetPrimaryAmmoType()));

        GetOwner()->RemoveAmmo(ammoFill, GetPrimaryAmmoType());
        m_iClip1 += ammoFill;
    }
}

//-----------------------------------------------------------------------------
// ItemPostFrame - Override so XM1014 can do multiple reloads in a row
//-----------------------------------------------------------------------------
void CWeaponXM1014::ItemPostFrame(void)
{
    CBasePlayer *pOwner = ToBasePlayer(GetOwner());
    if (!pOwner)
    {
        return;
    }

    if (m_bInReload)
    {
        // If I'm firing and have one round stop reloading and fire
        if ((pOwner->m_nButtons & IN_ATTACK) && (m_iClip1 >= 1))
        {
            m_bInReload = false;
            m_bNeedPump = false;
            m_bDelayedFire1 = true;
        }
        else if (m_flNextPrimaryAttack <= gpGlobals->curtime)
        {
            // If out of ammo end reload
            if (pOwner->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
            {
                FinishReload();
                return;
            }
            // If clip not full reload again
            if (m_iClip1 < GetMaxClip1())
            {
                Reload();
                return;
            }
            // Clip full, stop reloading
            else
            {
                FinishReload();
                return;
            }
        }
    }
    else
    {
        // Make shotgun shell invisible
        SetBodygroup(1, 1);
    }

    if ((m_bNeedPump) && (m_flNextPrimaryAttack <= gpGlobals->curtime))
    {
        // For semi-auto, no need to pump
        m_bNeedPump = false;
    }

    // Fire
    if ((m_bDelayedFire1 || pOwner->m_nButtons & IN_ATTACK) && m_flNextPrimaryAttack <= gpGlobals->curtime)
    {
        m_bDelayedFire1 = false;

        if ((m_iClip1 <= 0 && UsesClipsForAmmo1()) || (!UsesClipsForAmmo1() && !pOwner->GetAmmoCount(m_iPrimaryAmmoType)))
        {
            if (!pOwner->GetAmmoCount(m_iPrimaryAmmoType))
            {
                DryFire();
            }
            else
            {
                StartReload();
            }
        }
        // Fire underwater?
        else if (pOwner->GetWaterLevel() == 3 && m_bFiresUnderwater == false)
        {
            WeaponSound(EMPTY);
            m_flNextPrimaryAttack = gpGlobals->curtime + 0.2;
            return;
        }
        else
        {
            // If the firing button was just pressed, reset the firing time
            if (pOwner->m_afButtonPressed & IN_ATTACK)
            {
                m_flNextPrimaryAttack = gpGlobals->curtime;
            }
            PrimaryAttack();
        }
    }

    if (pOwner->m_nButtons & IN_RELOAD && UsesClipsForAmmo1() && !m_bInReload)
    {
        // reload when reload is pressed, or if no buttons are down and weapon is empty.
        StartReload();
    }
    else
    {
        // no fire buttons down
        m_bFireOnEmpty = false;

        if (!HasAnyAmmo() && m_flNextPrimaryAttack < gpGlobals->curtime)
        {
            // weapon isn't useable, switch.
            if (!(GetWeaponFlags() & ITEM_FLAG_NOAUTOSWITCHEMPTY) && pOwner->SwitchToNextBestWeapon(this))
            {
                m_flNextPrimaryAttack = gpGlobals->curtime + 0.3;
                return;
            }
        }
        else
        {
            // weapon is useable. Reload if empty and weapon has waited as long as it has to after firing
            if (m_iClip1 <= 0 && !(GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD) && m_flNextPrimaryAttack < gpGlobals->curtime)
            {
                if (StartReload())
                {
                    // if we've successfully started to reload, we're done
                    return;
                }
            }
        }

        WeaponIdle();
        return;
    }
}

//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef VEHICLE_APC_H
#define VEHICLE_APC_H

#ifdef _WIN32
#pragma once
#endif

#include "vehicle_base.h"
#include "smoke_trail.h"

#define APC_WHEEL_COUNT	4


struct ApcWaterData_t
{
	bool		m_bWheelInWater[APC_WHEEL_COUNT];
	bool		m_bWheelWasInWater[APC_WHEEL_COUNT];
	Vector		m_vecWheelContactPoints[APC_WHEEL_COUNT];
	float		m_flNextRippleTime[APC_WHEEL_COUNT];
	bool		m_bBodyInWater;
	bool		m_bBodyWasInWater;

	DECLARE_SIMPLE_DATADESC();
};

//-----------------------------------------------------------------------------
// Purpose: Four wheel physics vehicle server vehicle with weaponry
//-----------------------------------------------------------------------------
class CAPCFourWheelServerVehicle : public CFourWheelServerVehicle
{
	typedef CFourWheelServerVehicle BaseClass;
// IServerVehicle
public:
	bool		NPC_HasPrimaryWeapon( void ) { return true; }
	void		NPC_AimPrimaryWeapon( Vector vecTarget );
	bool		NPC_HasSecondaryWeapon( void ) { return true; }
	void		NPC_AimSecondaryWeapon( Vector vecTarget );
	int			GetExitAnimToUse(Vector& vecEyeExitEndpoint, bool& bAllPointsBlocked);

	// Weaponry
	void		Weapon_PrimaryRanges( float *flMinRange, float *flMaxRange );
	void		Weapon_SecondaryRanges( float *flMinRange, float *flMaxRange );
	float		Weapon_PrimaryCanFireAt( void );		// Return the time at which this vehicle's primary weapon can fire again
	float		Weapon_SecondaryCanFireAt( void );		// Return the time at which this vehicle's secondary weapon can fire again
};


//-----------------------------------------------------------------------------
// A driveable vehicle with a gun that shoots wherever the driver looks.
//-----------------------------------------------------------------------------
class CPropAPC : public CPropVehicleDriveable
{
	DECLARE_CLASS( CPropAPC, CPropVehicleDriveable );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
public:
	// CBaseEntity
	virtual void Precache( void );
	void	Think( void );
	virtual void Spawn(void);
	virtual void Activate();
	virtual void UpdateOnRemove( void );
	virtual void OnRestore( void );


	// CPropVehicle

	virtual void	CreateServerVehicle( void );
	virtual void	DriveVehicle( float flFrameTime, CUserCmd *ucmd, int iButtonsDown, int iButtonsReleased );
	virtual void	ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMoveData );
	virtual Class_T	ClassifyPassenger( CBaseCombatCharacter *pPassenger, Class_T defaultClassification );
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );
	virtual float	PassengerDamageModifier( const CTakeDamageInfo &info );
	virtual bool	CanExitVehicle(CBaseEntity* pEntity);
	virtual void	SetupMove(CBasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move);

	// Weaponry
	const Vector	&GetPrimaryGunOrigin( void );
	void			AimPrimaryWeapon( const Vector &vecForward );
	void			AimSecondaryWeaponAt(CBaseEntity* pTarget);
	void			AimSecondaryWeaponAtNPC(CBaseEntity* pTarget);
	float			PrimaryWeaponFireTime( void ) { return m_flMachineGunTime; }
	float			SecondaryWeaponFireTime( void ) { return m_flRocketTime; }
	float			MaxAttackRange() const;
	bool			IsInPrimaryFiringCone() const { return m_bInFiringCone; }
	virtual bool	IsVehicleBodyInWater() { return m_WaterData.m_bBodyInWater; }

	void			AimGunAt(Vector* endPos, float flInterval);
	void		GetCannonAim(Vector* resultDir);

	bool			TauCannonHasBeenCutOff(void) { return m_bGunHasBeenCutOff; }

	virtual void	EnterVehicle(CBaseCombatCharacter* pPassenger);
	virtual void	ExitVehicle(int nRole);


	void		InitWaterData(void);
	void		CheckWaterLevel(void);
	void		CreateSplash(const Vector& vecPosition);
	void		CreateRipple(const Vector& vecPosition);


	// Muzzle flashes
	const char		*GetTracerType( void ) ;
	void			DoImpactEffect( trace_t &tr, int nDamageType );
	void			DoMuzzleFlash( void );

	virtual Vector	EyePosition( );				// position of eyes
	Vector			BodyTarget( const Vector &posSrc, bool bNoisy );

	
	virtual void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

private:
	enum
	{
		MAX_SMOKE_TRAILS = 4,
		MAX_EXPLOSIONS = 4,
	};

	// Should we trigger a damage effect?
	bool ShouldTriggerDamageEffect( int nPrevHealth, int nEffectCount ) const;

	// Add a smoke trail since we've taken more damage
	void AddSmokeTrail( const Vector &vecPos );

	// Creates the breakable husk of an attack chopper
	void CreateChopperHusk();

	// Pow!
	void ExplodeAndThrowChunk( const Vector &vecExplosionPos );

	void Event_Killed( const CTakeDamageInfo &info );

	// Purpose: 
	void GetRocketShootPosition( Vector *pPosition );

	void FireMachineGun( void );
	void FireRocket( void );
	void FireRocketNPC(void);
	virtual void HandleWater(void);
	bool		 CheckWater(void);

	// Death volley 
	void FireDying( );

	// Create a corpse 
	void CreateCorpse( );

	// Blows da shizzle up
	void InputDestroy( inputdata_t &inputdata );
	void InputFireMissileAt( inputdata_t &inputdata );

	void CreateAPCLaserDot( void );

	virtual bool ShouldAttractAutoAim( CBaseEntity *pAimingEnt );


private:
	// Danger sounds made by the APC
	float	m_flDangerSoundTime;

	// handbrake after the fact to keep vehicles from rolling
	float	m_flHandbrakeTime;
	bool	m_bInitialHandbrake;

	// Damage effects
	int		m_nSmokeTrailCount;

	bool			m_bGunHasBeenCutOff;

	// Machine gun attacks
	int		m_nMachineGunMuzzleAttachment;
	int		m_nMachineGunBaseAttachment;
	float	m_flMachineGunTime;
	int		m_iMachineGunBurstLeft;
	Vector	m_vecBarrelPos;
	bool	m_bInFiringCone;

	float			m_throttleDisableTime;

	int				m_iNumberOfEntries;

	float			m_flOverturnedTime;

	// Rocket attacks
	EHANDLE	m_hLaserDot;
	EHANDLE m_hRocketTarget;
	EHANDLE			m_hLastPlayerInVehicle;
	int		m_iRocketSalvoLeft;
	float	m_flRocketTime;
	int		m_nRocketAttachment;
	int		m_nRocketSide;
	EHANDLE m_hSpecificRocketTarget;
	string_t m_strMissileHint;

	int				m_nSpinPos;
	float			m_aimYaw;
	float			m_aimPitch;

	float			m_flPlayerExitedTime;
	float			m_flLastSawPlayerAt;

	ApcWaterData_t	m_WaterData;

	CNetworkVar( bool, m_bHeadlightIsOn );

	COutputEvent m_OnDeath;
	COutputEvent m_OnFiredMissile;
	COutputEvent m_OnDamaged;
	COutputEvent m_OnDamagedByPlayer;

};

#endif // VEHICLE_APC_H


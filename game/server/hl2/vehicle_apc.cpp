//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "vehicle_apc.h"
#include "ammodef.h"
#include "IEffects.h"
#include "engine/IEngineSound.h"
#include "weapon_rpg.h"
#include "in_buttons.h"
#include "globalstate.h"
#include "soundent.h"
#include "ai_basenpc.h"
#include "ndebugoverlay.h"
#include "gib.h"
#include "EntityFlame.h"
#include "smoke_trail.h"
#include "explode.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "beam_shared.h"
#include "weapon_gauss.h"
#include "soundenvelope.h"
#include "decals.h"
#include "grenade_ar2.h"
#include "hl2_player.h"
#include "movevars_shared.h"
#include "bone_setup.h"
#include "ai_hint.h"
#include "eventqueue.h"
#include "rumble_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define LOCK_SPEED					10

#define ROCKET_ATTACK_RANGE_MAX 5500.0f
#define ROCKET_ATTACK_RANGE_MIN 1250.0f

#define MACHINE_GUN_ATTACK_RANGE_MAX 1250.0f
#define MACHINE_GUN_ATTACK_RANGE_MIN 0.0f

#define MACHINE_GUN_MAX_UP_PITCH	180
#define MACHINE_GUN_MAX_DOWN_PITCH	180
#define MACHINE_GUN_MAX_LEFT_YAW	180
#define MACHINE_GUN_MAX_RIGHT_YAW	180

#define APC_GUN_YAW				"vehicle_weapon_yaw"
#define APC_GUN_PITCH				"vehicle_weapon_pitch"
//#define APC_GUN_SPIN				"gun_spin"
//#define	APC_GUN_SPIN_RATE			20


#define MACHINE_GUN_BURST_SIZE		10
#define MACHINE_GUN_BURST_TIME		0.075f
#define MACHINE_GUN_BURST_PAUSE_TIME	2.0f


#define ROCKET_SALVO_SIZE				5
#define ROCKET_DELAY_TIME				1.5
#define ROCKET_MIN_BURST_PAUSE_TIME		3
#define ROCKET_MAX_BURST_PAUSE_TIME		4
#define ROCKET_SPEED					800
#define DEATH_VOLLEY_ROCKET_COUNT		4
#define DEATH_VOLLEY_MIN_FIRE_TIME		0.333
#define DEATH_VOLLEY_MAX_FIRE_TIME		0.166

#define OVERTURNED_EXIT_WAITTIME	2.0f

const char* g_pAPCThinkContext = "JeepSeagullThink";

extern short g_sModelIndexFireball; // Echh...

ConVar	g_apcexitspeed("g_apcexitspeed", "100", FCVAR_CHEAT);

extern ConVar	hud_jeephint_numentries;

ConVar sk_apc_health( "sk_apc_health", "750" );

ConVar sk_drive_apc("sk_drive_apc", "0");
ConVar sk_npc_apc("sk_npc_apc", "1");

extern ConVar autoaim_max_dist;
extern ConVar sv_vehicle_autoaim_scale;

ConVar r_ApcViewZHeight( "r_ApcViewZHeight", "10.0", FCVAR_CHEAT );


#define APC_MAX_CHUNKS	3
static const char *s_pChunkModelName[APC_MAX_CHUNKS] = 
{
	"models/gibs/helicopter_brokenpiece_01.mdl",
	"models/gibs/helicopter_brokenpiece_02.mdl",
	"models/gibs/helicopter_brokenpiece_03.mdl",
};

#define APC_MAX_GIBS	6
static const char *s_pGibModelName[APC_MAX_GIBS] = 
{
	"models/combine_apc_destroyed_gib01.mdl",
	"models/combine_apc_destroyed_gib02.mdl",
	"models/combine_apc_destroyed_gib03.mdl",
	"models/combine_apc_destroyed_gib04.mdl",
	"models/combine_apc_destroyed_gib05.mdl",
	"models/combine_apc_destroyed_gib06.mdl",
};


LINK_ENTITY_TO_CLASS( prop_vehicle_apc, CPropAPC );


BEGIN_DATADESC( CPropAPC )

	DEFINE_FIELD( m_flDangerSoundTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flHandbrakeTime,	FIELD_TIME ),
	DEFINE_FIELD( m_bInitialHandbrake,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nSmokeTrailCount,	FIELD_INTEGER ),
	DEFINE_FIELD( m_flMachineGunTime,		FIELD_TIME ),
	DEFINE_FIELD( m_iMachineGunBurstLeft,	FIELD_INTEGER ),
	DEFINE_FIELD( m_nMachineGunMuzzleAttachment,	FIELD_INTEGER ),
	DEFINE_FIELD( m_nMachineGunBaseAttachment,	FIELD_INTEGER ),
	DEFINE_FIELD( m_vecBarrelPos,		FIELD_VECTOR ),
	DEFINE_FIELD( m_bInFiringCone,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hLaserDot,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_hRocketTarget,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_iRocketSalvoLeft,	FIELD_INTEGER ),
	DEFINE_FIELD( m_flRocketTime,		FIELD_TIME ),
	DEFINE_FIELD( m_nRocketAttachment,	FIELD_INTEGER ),
	DEFINE_FIELD( m_nRocketSide,		FIELD_INTEGER ),
	DEFINE_FIELD( m_hSpecificRocketTarget, FIELD_EHANDLE ),

	DEFINE_FIELD(m_hLastPlayerInVehicle, FIELD_EHANDLE),
	DEFINE_FIELD(m_flPlayerExitedTime, FIELD_TIME),
	DEFINE_FIELD(m_flLastSawPlayerAt, FIELD_TIME),

	DEFINE_KEYFIELD( m_strMissileHint,	FIELD_STRING, "missilehint" ),

	DEFINE_INPUTFUNC( FIELD_VOID, "Destroy", InputDestroy ),
	DEFINE_INPUTFUNC( FIELD_STRING, "FireMissileAt", InputFireMissileAt ),

	DEFINE_OUTPUT( m_OnDeath,				"OnDeath" ),
	DEFINE_OUTPUT( m_OnFiredMissile,		"OnFiredMissile" ),
	DEFINE_OUTPUT( m_OnDamaged,				"OnDamaged" ),
	DEFINE_OUTPUT( m_OnDamagedByPlayer,		"OnDamagedByPlayer" ),

	DEFINE_FIELD( m_bHeadlightIsOn, FIELD_BOOLEAN ),

END_DATADESC()





IMPLEMENT_SERVERCLASS_ST(CPropAPC, DT_PropAPC)
	SendPropBool(SENDINFO(m_bHeadlightIsOn)),
END_SEND_TABLE();


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::Precache( void )
{
	BaseClass::Precache();

	int i;
	for ( i = 0; i < APC_MAX_CHUNKS; ++i )
	{
		PrecacheModel( s_pChunkModelName[i] );
	}

	for ( i = 0; i < APC_MAX_GIBS; ++i )
	{
		PrecacheModel( s_pGibModelName[i] );
	}

	PrecacheScriptSound( "Weapon_AR2.Single" );
	PrecacheScriptSound( "PropAPC.FireRocket" );
	PrecacheScriptSound( "combine.door_lock" );
	PrecacheSound( "buttons/lightswitch2.wav" );
}


//------------------------------------------------
// Spawn
//------------------------------------------------
void CPropAPC::Spawn( void )
{
	// Setup vehicle as a real-wheels car.
	SetVehicleType(VEHICLE_TYPE_CAR_WHEELS);



	m_flMinimumSpeedToEnterExit = 0;
	

	BaseClass::Spawn();
	SetBlocksLOS( true );
	m_iHealth = m_iMaxHealth = sk_apc_health.GetFloat();
	SetCycle( 0 );
	m_iMachineGunBurstLeft = MACHINE_GUN_BURST_SIZE;
	m_iRocketSalvoLeft = ROCKET_SALVO_SIZE;
	m_nRocketSide = 0;
	m_lifeState = LIFE_ALIVE;
	m_bInFiringCone = false;
	m_bHasGun = true;


	m_flHandbrakeTime = gpGlobals->curtime + 0.1;
	m_bInitialHandbrake = false;

	// Reset the gun to a default pose.
	SetPoseParameter( "vehicle_weapon_pitch", 0 );
	SetPoseParameter( "vehicle_weapon_yaw", 90 );

	if (m_bHasGun)
	{
		SetBodygroup(1, true);

		// Initialize pose parameters
		SetPoseParameter(APC_GUN_YAW, 0);
		SetPoseParameter(APC_GUN_PITCH, 0);
		m_nSpinPos = 0;
//		SetPoseParameter(APC_GUN_SPIN, m_nSpinPos);
		m_aimYaw = 0;
		m_aimPitch = 0;
	}
	else
	{
		SetBodygroup(1, false);
	}


	m_bHeadlightIsOn = false;

	CreateAPCLaserDot();

	if( g_pGameRules->GetAutoAimMode() == AUTOAIM_ON_CONSOLE )
	{
		AddFlag( FL_AIMTARGET );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::CreateServerVehicle(void)
{
	// Create our armed server vehicle
	m_pServerVehicle = new CAPCFourWheelServerVehicle();
	m_pServerVehicle->SetVehicle(this);
	
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Vector CPropAPC::BodyTarget(const Vector& posSrc, bool bNoisy)
{
	Vector	shotPos;
	matrix3x4_t	matrix;

	int eyeAttachmentIndex = LookupAttachment("vehicle_driver_eyes");
	GetAttachment(eyeAttachmentIndex, matrix);
	MatrixGetColumn(matrix, 3, shotPos);

	if (bNoisy)
	{
		shotPos[0] += random->RandomFloat(-8.0f, 8.0f);
		shotPos[1] += random->RandomFloat(-8.0f, 8.0f);
		shotPos[2] += random->RandomFloat(-8.0f, 8.0f);
	}

	return shotPos;
}



//-----------------------------------------------------------------------------
// Purpose: Aim Gun at a target
//-----------------------------------------------------------------------------
void CPropAPC::AimGunAt(Vector* endPos, float flInterval)
{
	Vector	aimPos = *endPos;

	// See if the gun should be allowed to aim
	if (IsOverturned() || m_bEngineLocked || m_bHasGun == false)
	{
		SetPoseParameter(APC_GUN_YAW, 0);
		SetPoseParameter(APC_GUN_PITCH, 0);
		//SetPoseParameter(JEEP_GUN_SPIN, 0);
		return;

		// Make the gun go limp and look "down"
		Vector	v_forward, v_up;
		AngleVectors(GetLocalAngles(), NULL, &v_forward, &v_up);
		aimPos = WorldSpaceCenter() + (v_forward * -32.0f) - Vector(0, 0, 128.0f);
	}

	matrix3x4_t gunMatrix;
	GetAttachment(LookupAttachment("gun_ref"), gunMatrix);

	// transform the enemy into gun space
	Vector localEnemyPosition;
	VectorITransform(aimPos, gunMatrix, localEnemyPosition);

	// do a look at in gun space (essentially a delta-lookat)
	QAngle localEnemyAngles;
	VectorAngles(localEnemyPosition, localEnemyAngles);

	// convert to +/- 180 degrees
	localEnemyAngles.x = UTIL_AngleDiff(localEnemyAngles.x, 0);
	localEnemyAngles.y = UTIL_AngleDiff(localEnemyAngles.y, 0);

	float targetYaw = m_aimYaw + localEnemyAngles.y;
	float targetPitch = m_aimPitch + localEnemyAngles.x;

	// Constrain our angles
	float newTargetYaw = clamp(targetYaw, -MACHINE_GUN_MAX_LEFT_YAW, MACHINE_GUN_MAX_RIGHT_YAW);
	float newTargetPitch = clamp(targetPitch, -MACHINE_GUN_MAX_DOWN_PITCH, MACHINE_GUN_MAX_UP_PITCH);

	// If the angles have been clamped, we're looking outside of our valid range
	if (fabs(newTargetYaw - targetYaw) > 1e-4 || fabs(newTargetPitch - targetPitch) > 1e-4)
	{
		m_bUnableToFire = true;
	}

	targetYaw = newTargetYaw;
	targetPitch = newTargetPitch;

	// Exponentially approach the target
	float yawSpeed = 60;
	float pitchSpeed = 60;

	m_aimYaw = UTIL_Approach(targetYaw, m_aimYaw, yawSpeed);
	m_aimPitch = UTIL_Approach(targetPitch, m_aimPitch, pitchSpeed);

	SetPoseParameter(APC_GUN_YAW, -m_aimYaw);
	SetPoseParameter(APC_GUN_PITCH, -m_aimPitch);

	InvalidateBoneCache();

	// read back to avoid drift when hitting limits
	// as long as the velocity is less than the delta between the limit and 180, this is fine.
	m_aimPitch = -GetPoseParameter(APC_GUN_PITCH);
	m_aimYaw = -GetPoseParameter(APC_GUN_YAW);

	// Now draw crosshair for actual aiming point
	Vector	vecMuzzle, vecMuzzleDir;
	QAngle	vecMuzzleAng;

	GetAttachment("Muzzle", vecMuzzle, vecMuzzleAng);
	AngleVectors(vecMuzzleAng, &vecMuzzleDir);

	trace_t	tr;
	UTIL_TraceLine(vecMuzzle, vecMuzzle + (vecMuzzleDir * MAX_TRACE_LENGTH), MASK_SHOT, this, COLLISION_GROUP_NONE, &tr);

	// see if we hit something, if so, adjust endPos to hit location
	if (tr.fraction < 1.0)
	{
		m_vecGunCrosshair = vecMuzzle + (vecMuzzleDir * MAX_TRACE_LENGTH * tr.fraction);
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CPropAPC::CheckWater(void)
{
	bool bInWater = false;

	// Check all four wheels.
	for (int iWheel = 0; iWheel < APC_WHEEL_COUNT; ++iWheel)
	{
		// Get the current wheel and get its contact point.
		IPhysicsObject* pWheel = m_VehiclePhysics.GetWheel(iWheel);
		if (!pWheel)
			continue;

		// Check to see if we hit water.
		if (pWheel->GetContactPoint(&m_WaterData.m_vecWheelContactPoints[iWheel], NULL))
		{
			m_WaterData.m_bWheelInWater[iWheel] = (UTIL_PointContents(m_WaterData.m_vecWheelContactPoints[iWheel]) & MASK_WATER) ? true : false;
			if (m_WaterData.m_bWheelInWater[iWheel])
			{
				bInWater = true;
			}
		}
	}

	// Check the body and the BONNET.
	int iEngine = LookupAttachment("vehicle_engine");
	Vector vecEnginePoint;
	QAngle vecEngineAngles;
	GetAttachment(iEngine, vecEnginePoint, vecEngineAngles);

	m_WaterData.m_bBodyInWater = (UTIL_PointContents(vecEnginePoint) & MASK_WATER) ? true : false;
	if (m_WaterData.m_bBodyInWater)
	{

		if (!m_VehiclePhysics.IsEngineDisabled())
		{
			m_VehiclePhysics.SetDisableEngine(true);
		}
	}
	else
	{
		if (m_VehiclePhysics.IsEngineDisabled())
		{
			m_VehiclePhysics.SetDisableEngine(false);
		}
	}

	if (bInWater)
	{
		// Check the player's water level.
		CheckWaterLevel();
	}

	return bInWater;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::CreateSplash(const Vector& vecPosition)
{
	// Splash data.
	CEffectData	data;
	data.m_fFlags = 0;
	data.m_vOrigin = vecPosition;
	data.m_vNormal.Init(0.0f, 0.0f, 1.0f);
	VectorAngles(data.m_vNormal, data.m_vAngles);
	data.m_flScale = 10.0f + random->RandomFloat(0, 2);

	// Create the splash..
	DispatchEffect("watersplash", data);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::CreateRipple(const Vector& vecPosition)
{
	// Ripple data.
	CEffectData	data;
	data.m_fFlags = 0;
	data.m_vOrigin = vecPosition;
	data.m_vNormal.Init(0.0f, 0.0f, 1.0f);
	VectorAngles(data.m_vNormal, data.m_vAngles);
	data.m_flScale = 10.0f + random->RandomFloat(0, 2);
	if (GetWaterType() & CONTENTS_SLIME)
	{
		data.m_fFlags |= FX_WATER_IN_SLIME;
	}

	// Create the ripple.
	DispatchEffect("waterripple", data);
}

//-----------------------------------------------------------------------------
// Purpose: Create a laser
//-----------------------------------------------------------------------------
void CPropAPC::CreateAPCLaserDot( void )
{
	// Create a laser if we don't have one
	if ( m_hLaserDot == NULL )
	{
		m_hLaserDot = CreateLaserDot( GetAbsOrigin(), this, false );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPropAPC::ShouldAttractAutoAim( CBaseEntity *pAimingEnt )
{
	if( g_pGameRules->GetAutoAimMode() == AUTOAIM_ON_CONSOLE && pAimingEnt->IsPlayer() && GetDriver() )
	{
		return true;
	}

	return BaseClass::ShouldAttractAutoAim( pAimingEnt );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::Activate()
{
	BaseClass::Activate();

	m_nRocketAttachment = LookupAttachment( "cannon_muzzle" );
	m_nMachineGunMuzzleAttachment = LookupAttachment( "muzzle" );
	m_nMachineGunBaseAttachment = LookupAttachment( "gun_base" );

	// NOTE: gun_ref must have the same position as gun_base, but rotates with the gun
	int nMachineGunRefAttachment = LookupAttachment( "gun_def" );

	Vector vecWorldBarrelPos;
	matrix3x4_t matRefToWorld;
	GetAttachment( m_nMachineGunMuzzleAttachment, vecWorldBarrelPos );
	GetAttachment( nMachineGunRefAttachment, matRefToWorld );
	GetAttachment(m_nRocketAttachment, vecWorldBarrelPos);
	VectorITransform( vecWorldBarrelPos, matRefToWorld, m_vecBarrelPos );

	CBaseServerVehicle* pServerVehicle = dynamic_cast<CBaseServerVehicle*>(GetServerVehicle());
	if (pServerVehicle)
	{
		if (pServerVehicle->GetPassenger())
		{
			// If a jeep comes back from a save game with a driver, make sure the engine rumble starts up.
			pServerVehicle->StartEngineRumble();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::CheckWaterLevel(void)
{
	CBaseEntity* pEntity = GetDriver();
	if (pEntity && pEntity->IsPlayer())
	{
		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(pEntity);

		Vector vecAttachPoint;
		QAngle vecAttachAngles;

		// Check eyes. (vehicle_driver_eyes point)
		int iAttachment = LookupAttachment("vehicle_driver_eyes");
		GetAttachment(iAttachment, vecAttachPoint, vecAttachAngles);

		// Add the jeep's Z view offset
		Vector vecUp;
		AngleVectors(vecAttachAngles, NULL, NULL, &vecUp);
		vecUp.z = clamp(vecUp.z, 0.0f, vecUp.z);
		vecAttachPoint.z += r_ApcViewZHeight.GetFloat() * vecUp.z;

		bool bEyes = (UTIL_PointContents(vecAttachPoint) & MASK_WATER) ? true : false;
		if (bEyes)
		{
			pPlayer->SetWaterLevel(WL_Eyes);
			return;
		}

		// Check waist.  (vehicle_engine point -- see parent function).
		if (m_WaterData.m_bBodyInWater)
		{
			pPlayer->SetWaterLevel(WL_Waist);
			return;
		}

		// Check feet. (vehicle_feet_passenger0 point)
		iAttachment = LookupAttachment("vehicle_feet_passenger0");
		GetAttachment(iAttachment, vecAttachPoint, vecAttachAngles);
		bool bFeet = (UTIL_PointContents(vecAttachPoint) & MASK_WATER) ? true : false;
		if (bFeet)
		{
			pPlayer->SetWaterLevel(WL_Feet);
			return;
		}

		// Not in water.
		pPlayer->SetWaterLevel(WL_NotInWater);
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::UpdateOnRemove( void )
{
	if ( m_hLaserDot )
	{
		UTIL_Remove( m_hLaserDot );
		m_hLaserDot = NULL;
	}
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::ExitVehicle(int nRole)
{
	// Addition, turn the jeep's headlights off.
	//if (HeadlightIsOn())
	//	HeadlightTurnOff();

	BaseClass::ExitVehicle(nRole);

	//If the player has exited, stop charging
	//StopChargeSound();
	//m_bCannonCharging = false;

	// Remember when we last saw the player
	m_flPlayerExitedTime = gpGlobals->curtime;
	m_flLastSawPlayerAt = gpGlobals->curtime;

	if (GlobalEntity_GetState("no_seagulls_on_jeep") == GLOBAL_OFF)
	{
		// Look for fly nodes
		CHintCriteria hintCriteria;
		hintCriteria.SetHintType(HINT_CROW_FLYTO_POINT);
		hintCriteria.AddIncludePosition(GetAbsOrigin(), 4500);

	}
}





//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pMoveData - 
//-----------------------------------------------------------------------------
Class_T	CPropAPC::ClassifyPassenger( CBaseCombatCharacter *pPassenger, Class_T defaultClassification )
{ 
	return CLASS_COMBINE;	
}


//-----------------------------------------------------------------------------
// Purpose: Damage events as modified for the passenger of the APC, not the APC itself
//-----------------------------------------------------------------------------
float CPropAPC::PassengerDamageModifier( const CTakeDamageInfo &info ) 
{ 
	CTakeDamageInfo DmgInfo = info;

	// bullets, slashing and headbutts don't hurt us in the apc, neither do rockets
	if( (DmgInfo.GetDamageType() & DMG_BULLET) || (DmgInfo.GetDamageType() & DMG_SLASH) ||
		(DmgInfo.GetDamageType() & DMG_CLUB) || (DmgInfo.GetDamageType() & DMG_BLAST) )
		return (0);

	// Accept everything else by default
	return 1.0; 
}


//-----------------------------------------------------------------------------
// position of eyes
//-----------------------------------------------------------------------------
Vector CPropAPC::EyePosition( )
{
	Vector vecEyePosition;
	CollisionProp()->NormalizedToWorldSpace( Vector( 0.5, 0.5, 1.0 ), &vecEyePosition );
	return vecEyePosition;
}


	
//-----------------------------------------------------------------------------
// Add a smoke trail since we've taken more damage
//-----------------------------------------------------------------------------
void CPropAPC::AddSmokeTrail( const Vector &vecPos )
{
	// Start this trail out with a bang!
	ExplosionCreate( vecPos, vec3_angle, this, 1000, 500.0f, SF_ENVEXPLOSION_NODAMAGE | 
		SF_ENVEXPLOSION_NOSPARKS | SF_ENVEXPLOSION_NODLIGHTS | SF_ENVEXPLOSION_NOSMOKE | 
		SF_ENVEXPLOSION_NOFIREBALLSMOKE, 0 );
	UTIL_ScreenShake( vecPos, 25.0, 150.0, 1.0, 750.0f, SHAKE_START );

	if ( m_nSmokeTrailCount == MAX_SMOKE_TRAILS )
		return;

	SmokeTrail *pSmokeTrail =  SmokeTrail::CreateSmokeTrail();
	if( !pSmokeTrail )
		return;

	// See if there's an attachment for this smoke trail
	char buf[32];
	Q_snprintf( buf, 32, "damage%d", m_nSmokeTrailCount );
	int nAttachment = LookupAttachment( buf );

	++m_nSmokeTrailCount;

	pSmokeTrail->m_SpawnRate = 4;
	pSmokeTrail->m_ParticleLifetime = 5.0f;
	pSmokeTrail->m_StartColor.Init( 0.7f, 0.7f, 0.7f );
	pSmokeTrail->m_EndColor.Init( 0.6, 0.6, 0.6 );
	pSmokeTrail->m_StartSize = 32;
	pSmokeTrail->m_EndSize = 64;
	pSmokeTrail->m_SpawnRadius = 4;
	pSmokeTrail->m_Opacity = 0.5f;
	pSmokeTrail->m_MinSpeed = 16;
	pSmokeTrail->m_MaxSpeed = 16;
	pSmokeTrail->m_MinDirectedSpeed	= 16.0f;
	pSmokeTrail->m_MaxDirectedSpeed	= 16.0f;
	pSmokeTrail->SetLifetime( 5 );
	pSmokeTrail->SetParent( this, nAttachment );

	Vector vecForward( 0, 0, 1 );
	QAngle angles;
	VectorAngles( vecForward, angles );

	if ( nAttachment == 0 )
	{
		pSmokeTrail->SetAbsOrigin( vecPos );
		pSmokeTrail->SetAbsAngles( angles );
	}
	else
	{
		pSmokeTrail->SetLocalOrigin( vec3_origin );
		pSmokeTrail->SetLocalAngles( angles );
	}

	pSmokeTrail->SetMoveType( MOVETYPE_NONE );
}


//------------------------------------------------------------------------------
// Pow!
//------------------------------------------------------------------------------
void CPropAPC::ExplodeAndThrowChunk( const Vector &vecExplosionPos )
{
	ExplosionCreate( vecExplosionPos, vec3_angle, this, 1000, 500.0f, 
		SF_ENVEXPLOSION_NODAMAGE | SF_ENVEXPLOSION_NOSPARKS | SF_ENVEXPLOSION_NODLIGHTS	|
		SF_ENVEXPLOSION_NOSMOKE  | SF_ENVEXPLOSION_NOFIREBALLSMOKE, 0 );
	UTIL_ScreenShake( vecExplosionPos, 25.0, 150.0, 1.0, 750.0f, SHAKE_START );

	// Drop a flaming, smoking chunk.
	CGib *pChunk = CREATE_ENTITY( CGib, "gib" );
	pChunk->Spawn( "models/gibs/hgibs.mdl" );
	pChunk->SetBloodColor( DONT_BLEED );

	QAngle vecSpawnAngles;
	vecSpawnAngles.Random( -90, 90 );
	pChunk->SetAbsOrigin( vecExplosionPos );
	pChunk->SetAbsAngles( vecSpawnAngles );

	int nGib = random->RandomInt( 0, APC_MAX_CHUNKS - 1 );
	pChunk->Spawn( s_pChunkModelName[nGib] );
	pChunk->SetOwnerEntity( this );
	pChunk->m_lifeTime = random->RandomFloat( 6.0f, 8.0f );
	pChunk->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
	IPhysicsObject *pPhysicsObject = pChunk->VPhysicsInitNormal( SOLID_VPHYSICS, pChunk->GetSolidFlags(), false );
	
	// Set the velocity
	if ( pPhysicsObject )
	{
		pPhysicsObject->EnableMotion( true );
		Vector vecVelocity;

		QAngle angles;
		angles.x = random->RandomFloat( -40, 0 );
		angles.y = random->RandomFloat( 0, 360 );
		angles.z = 0.0f;
		AngleVectors( angles, &vecVelocity );
		
		vecVelocity *= random->RandomFloat( 300, 900 );
		vecVelocity += GetAbsVelocity();

		AngularImpulse angImpulse;
		angImpulse = RandomAngularImpulse( -180, 180 );

		pChunk->SetAbsVelocity( vecVelocity );
		pPhysicsObject->SetVelocity(&vecVelocity, &angImpulse );
	}

	CEntityFlame *pFlame = CEntityFlame::Create( pChunk, false );
	if ( pFlame != NULL )
	{
		pFlame->SetLifetime( pChunk->m_lifeTime );
	}
}


//-----------------------------------------------------------------------------
// Should we trigger a damage effect?
//-----------------------------------------------------------------------------
inline bool CPropAPC::ShouldTriggerDamageEffect( int nPrevHealth, int nEffectCount ) const
{
	int nPrevRange = (int)( ((float)nPrevHealth / (float)GetMaxHealth()) * nEffectCount );
	int nRange = (int)( ((float)GetHealth() / (float)GetMaxHealth()) * nEffectCount );
	return ( nRange != nPrevRange );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::Event_Killed( const CTakeDamageInfo &info )
{
	m_OnDeath.FireOutput( info.GetAttacker(), this );

	Vector vecAbsMins, vecAbsMaxs;
	CollisionProp()->WorldSpaceAABB( &vecAbsMins, &vecAbsMaxs );

	Vector vecNormalizedMins, vecNormalizedMaxs;
	CollisionProp()->WorldToNormalizedSpace( vecAbsMins, &vecNormalizedMins );
	CollisionProp()->WorldToNormalizedSpace( vecAbsMaxs, &vecNormalizedMaxs );

	Vector vecAbsPoint;
	CPASFilter filter( GetAbsOrigin() );
	for (int i = 0; i < 5; i++)
	{
		CollisionProp()->RandomPointInBounds( vecNormalizedMins, vecNormalizedMaxs, &vecAbsPoint );
		te->Explosion( filter, random->RandomFloat( 0.0, 1.0 ),	&vecAbsPoint, 
			g_sModelIndexFireball, random->RandomInt( 4, 10 ), 
			random->RandomInt( 8, 15 ), 
			( i < 2 ) ? TE_EXPLFLAG_NODLIGHTS : TE_EXPLFLAG_NOPARTICLES | TE_EXPLFLAG_NOFIREBALLSMOKE | TE_EXPLFLAG_NODLIGHTS,
			100, 0 );
	}

	// TODO: make the gibs spawn in sync with the delayed explosions
	int nGibs = random->RandomInt( 1, 4 );
	for ( int i = 0; i < nGibs; i++)
	{
		// Throw a flaming, smoking chunk.
		CGib *pChunk = CREATE_ENTITY( CGib, "gib" );
		pChunk->Spawn( "models/gibs/hgibs.mdl" );
		pChunk->SetBloodColor( DONT_BLEED );

		QAngle vecSpawnAngles;
		vecSpawnAngles.Random( -90, 90 );
		pChunk->SetAbsOrigin( vecAbsPoint );
		pChunk->SetAbsAngles( vecSpawnAngles );

		int nGib = random->RandomInt( 0, APC_MAX_CHUNKS - 1 );
		pChunk->Spawn( s_pChunkModelName[nGib] );
		pChunk->SetOwnerEntity( this );
		pChunk->m_lifeTime = random->RandomFloat( 6.0f, 8.0f );
		pChunk->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
		IPhysicsObject *pPhysicsObject = pChunk->VPhysicsInitNormal( SOLID_VPHYSICS, pChunk->GetSolidFlags(), false );
		
		// Set the velocity
		if ( pPhysicsObject )
		{
			pPhysicsObject->EnableMotion( true );
			Vector vecVelocity;

			QAngle angles;
			angles.x = random->RandomFloat( -20, 20 );
			angles.y = random->RandomFloat( 0, 360 );
			angles.z = 0.0f;
			AngleVectors( angles, &vecVelocity );
			
			vecVelocity *= random->RandomFloat( 300, 900 );
			vecVelocity += GetAbsVelocity();

			AngularImpulse angImpulse;
			angImpulse = RandomAngularImpulse( -180, 180 );

			pChunk->SetAbsVelocity( vecVelocity );
			pPhysicsObject->SetVelocity(&vecVelocity, &angImpulse );
		}

		CEntityFlame *pFlame = CEntityFlame::Create( pChunk, false );
		if ( pFlame != NULL )
		{
			pFlame->SetLifetime( pChunk->m_lifeTime );
		}
	}

	UTIL_ScreenShake( vecAbsPoint, 25.0, 150.0, 1.0, 750.0f, SHAKE_START );

	if( hl2_episodic.GetBool() )
	{
		// EP1 perf hit
		Ignite( 6, false );
	}
	else
	{
		Ignite( 60, false );
	}

	m_lifeState = LIFE_DYING;

	// Spawn a lesser amount if the player is close
	m_iRocketSalvoLeft = DEATH_VOLLEY_ROCKET_COUNT;
	m_flRocketTime = gpGlobals->curtime;

}



//-----------------------------------------------------------------------------
// Purpose: Blows it up!
//-----------------------------------------------------------------------------
void CPropAPC::InputDestroy( inputdata_t &inputdata )
{
	CTakeDamageInfo info( this, this, m_iHealth, DMG_BLAST );
	info.SetDamagePosition( WorldSpaceCenter() );
	info.SetDamageForce( Vector( 0, 0, 1 ) );
	TakeDamage( info );
}


//-----------------------------------------------------------------------------
// Aim the next rocket at a specific target
//-----------------------------------------------------------------------------
void CPropAPC::InputFireMissileAt( inputdata_t &inputdata )
{
	string_t strMissileTarget = MAKE_STRING( inputdata.value.String() );
	CBaseEntity *pTarget = gEntList.FindEntityByName( NULL, strMissileTarget, NULL, inputdata.pActivator, inputdata.pCaller );
	if ( pTarget == NULL )
	{
		DevWarning( "%s: Could not find target '%s'!\n", GetClassname(), STRING( strMissileTarget ) );
		return;
	}

	m_hSpecificRocketTarget = pTarget;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CPropAPC::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( m_iHealth == 0 )
		return 0;

	m_OnDamaged.FireOutput( info.GetAttacker(), this );

	if ( info.GetAttacker() && info.GetAttacker()->IsPlayer() )
	{
		m_OnDamagedByPlayer.FireOutput( info.GetAttacker(), this );
	}

	CTakeDamageInfo dmgInfo = info;
	if ( dmgInfo.GetDamageType() & (DMG_BLAST | DMG_AIRBOAT) )
	{
		int nPrevHealth = GetHealth();

		m_iHealth -= dmgInfo.GetDamage();
		if ( m_iHealth <= 0 )
		{
			m_iHealth = 0;
			Event_Killed( dmgInfo );
			return 0;
		}

		// Chain
//		BaseClass::OnTakeDamage( dmgInfo );

		// Spawn damage effects
		if ( nPrevHealth != GetHealth() )
		{
			if ( ShouldTriggerDamageEffect( nPrevHealth, MAX_SMOKE_TRAILS ) )
			{
				AddSmokeTrail( dmgInfo.GetDamagePosition() );
			}

			if ( ShouldTriggerDamageEffect( nPrevHealth, MAX_EXPLOSIONS ) )
			{
				ExplodeAndThrowChunk( dmgInfo.GetDamagePosition() );
			}
		}
	}
	return 1;
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::EnterVehicle(CBaseCombatCharacter* pPassenger)
{
	CBasePlayer* pPlayer = ToBasePlayer(pPassenger);
	if (!pPlayer)
		return;

	CheckWater();
	BaseClass::EnterVehicle(pPassenger);

	// Start looking for seagulls to land
	m_hLastPlayerInVehicle = m_hPlayer;
	pPlayer->EnableControl(true);
	SetContextThink(NULL, 0, g_pAPCThinkContext);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::SetupMove(CBasePlayer* player, CUserCmd* ucmd, IMoveHelper* pHelper, CMoveData* move)
{
	// If we are overturned and hit any key - leave the vehicle (IN_USE is already handled!).
	if (m_flOverturnedTime > OVERTURNED_EXIT_WAITTIME)
	{
		if ((ucmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_SPEED | IN_JUMP | IN_ATTACK | IN_ATTACK2)) && !m_bExitAnimOn)
		{
			// Can't exit yet? We're probably still moving. Swallow the keys.
			if (!CanExitVehicle(player))
				return;

			if (!GetServerVehicle()->HandlePassengerExit(m_hPlayer) && (m_hPlayer != NULL))
			{
				m_hPlayer->PlayUseDenySound();
			}
			return;
		}
	}

	// If the throttle is disabled or we're upside-down, don't allow throttling (including turbo)
	CUserCmd tmp;
	if ((m_throttleDisableTime > gpGlobals->curtime) || (IsOverturned()))
	{
		m_bUnableToFire = true;

		tmp = (*ucmd);
		tmp.buttons &= ~(IN_FORWARD | IN_BACK);
		ucmd = &tmp;
	}

	BaseClass::SetupMove(player, ucmd, pHelper, move);
}



//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//			*pMoveData - 
//-----------------------------------------------------------------------------
void CPropAPC::ProcessMovement(CBasePlayer* pPlayer, CMoveData* pMoveData)
{
	BaseClass::ProcessMovement(pPlayer, pMoveData);

	// Create dangers sounds in front of the vehicle.
	//CreateDangerSounds();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::HandleWater(void)
{
	// Only check the wheels and engine in water if we have a driver (player).
	if (!GetDriver())
		return;

	// Check to see if we are in water.
	if (CheckWater())
	{
		for (int iWheel = 0; iWheel < APC_WHEEL_COUNT; ++iWheel)
		{
			// Create an entry/exit splash!
			if (m_WaterData.m_bWheelInWater[iWheel] != m_WaterData.m_bWheelWasInWater[iWheel])
			{
				CreateSplash(m_WaterData.m_vecWheelContactPoints[iWheel]);
				CreateRipple(m_WaterData.m_vecWheelContactPoints[iWheel]);
			}

			// Create ripples.
			if (m_WaterData.m_bWheelInWater[iWheel] && m_WaterData.m_bWheelWasInWater[iWheel])
			{
				if (m_WaterData.m_flNextRippleTime[iWheel] < gpGlobals->curtime)
				{
					// Stagger ripple times
					m_WaterData.m_flNextRippleTime[iWheel] = gpGlobals->curtime + RandomFloat(0.1, 0.3);
					CreateRipple(m_WaterData.m_vecWheelContactPoints[iWheel]);
				}
			}
		}
	}

	// Save of data from last think.
	for (int iWheel = 0; iWheel < APC_WHEEL_COUNT; ++iWheel)
	{
		m_WaterData.m_bWheelWasInWater[iWheel] = m_WaterData.m_bWheelInWater[iWheel];
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::Think(void)
{
	BaseClass::Think();

	CBasePlayer* pPlayer = UTIL_GetLocalPlayer();

	if (m_bEngineLocked)
	{
		m_bUnableToFire = true;

		if (pPlayer != NULL)
		{
			pPlayer->m_Local.m_iHideHUD |= HIDEHUD_VEHICLE_CROSSHAIR;
		}
	}
	else if (m_bHasGun)
	{
		// Start this as false and update it again each frame
		m_bUnableToFire = false;

		if (pPlayer != NULL)
		{
			pPlayer->m_Local.m_iHideHUD &= ~HIDEHUD_VEHICLE_CROSSHAIR;
		}
	}

	// Water!?
	HandleWater();

	SetSimulationTime(gpGlobals->curtime);

	SetNextThink(gpGlobals->curtime);
	SetAnimatedEveryTick(true);

	if (!m_bInitialHandbrake)	// after initial timer expires, set the handbrake
	{
		m_bInitialHandbrake = true;
		m_VehiclePhysics.SetHandbrake(true);
		m_VehiclePhysics.Think();
	}

	// Check overturned status.
	if (!IsOverturned())
	{
		m_flOverturnedTime = 0.0f;
	}
	else
	{
		m_flOverturnedTime += gpGlobals->frametime;
	}

	// spin gun if charging cannon
	//FIXME: Don't bother for E3


	// Aim gun based on the player view direction.
	if (m_bHasGun && m_hPlayer && !m_bExitAnimOn && !m_bEnterAnimOn)
	{
		Vector vecEyeDir, vecEyePos;
		m_hPlayer->EyePositionAndVectors(&vecEyePos, &vecEyeDir, NULL, NULL);

		if (g_pGameRules->GetAutoAimMode() == AUTOAIM_ON_CONSOLE)
		{
			autoaim_params_t params;

			params.m_fScale = AUTOAIM_SCALE_DEFAULT * sv_vehicle_autoaim_scale.GetFloat();
			params.m_fMaxDist = autoaim_max_dist.GetFloat();
			m_hPlayer->GetAutoaimVector(params);

			// Use autoaim as the eye dir if there is an autoaim ent.
			vecEyeDir = params.m_vecAutoAimDir;
		}

		// Trace out from the player's eye point.
		Vector	vecEndPos = vecEyePos + (vecEyeDir * MAX_TRACE_LENGTH);
		trace_t	trace;
		UTIL_TraceLine(vecEyePos, vecEndPos, MASK_SHOT, this, COLLISION_GROUP_NONE, &trace);

		// See if we hit something, if so, adjust end position to hit location.
		if (trace.fraction < 1.0)
		{
			vecEndPos = vecEyePos + (vecEyeDir * MAX_TRACE_LENGTH * trace.fraction);
		}

		//m_vecLookCrosshair = vecEndPos;
		AimGunAt(&vecEndPos, 0.1f);

		// Update laser dot position for rocket guidance
		if (m_hLaserDot)
		{
			m_hLaserDot->SetAbsOrigin(vecEndPos);
		}
	}

	StudioFrameAdvance();

	// If the enter or exit animation has finished, tell the server vehicle
	if (IsSequenceFinished() && (m_bExitAnimOn || m_bEnterAnimOn))
	{
		if (m_bEnterAnimOn)
		{
			m_VehiclePhysics.ReleaseHandbrake();
			StartEngine();

			// HACKHACK: This forces the jeep to play a sound when it gets entered underwater
			if (m_VehiclePhysics.IsEngineDisabled())
			{
				CBaseServerVehicle* pServerVehicle = dynamic_cast<CBaseServerVehicle*>(GetServerVehicle());
				if (pServerVehicle)
				{
					pServerVehicle->SoundStartDisabled();
				}
			}

			// The first few time we get into the jeep, print the jeep help
			if (m_iNumberOfEntries < hud_jeephint_numentries.GetInt())
			{
				g_EventQueue.AddEvent(this, "ShowHudHint", 1.5f, this, this);
			}
		}

		if (hl2_episodic.GetBool())
		{
			// Set its running animation idle
			if (m_bEnterAnimOn)
			{
				// Idle running
				int nSequence = SelectWeightedSequence(ACT_IDLE_STIMULATED);
				if (nSequence > ACTIVITY_NOT_AVAILABLE)
				{
					SetCycle(0);
					m_flAnimTime = gpGlobals->curtime;
					ResetSequence(nSequence);
					ResetClientsideFrame();
				}
			}
		}

		// If we're exiting and have had the tau cannon removed, we don't want to reset the animation
		if (hl2_episodic.GetBool())
		{
			// Reset on exit anim
			GetServerVehicle()->HandleEntryExitFinish(m_bExitAnimOn, m_bExitAnimOn);
		}
		//else
		//{
		//	GetServerVehicle()->HandleEntryExitFinish(m_bExitAnimOn, m_bExitAnimOn);
		//}
	}
	/*
	// See if the ammo crate needs to close
	if ((m_flAmmoCrateCloseTime < gpGlobals->curtime) && (GetSequence() == LookupSequence("ammo_open")))
	{
		m_flAnimTime = gpGlobals->curtime;
		m_flPlaybackRate = 0.0;
		SetCycle(0);
		ResetSequence(LookupSequence("ammo_close"));
	}
	else if ((GetSequence() == LookupSequence("ammo_close")) && IsSequenceFinished())
	{
		m_flAnimTime = gpGlobals->curtime;
		m_flPlaybackRate = 0.0;
		SetCycle(0);

		int nSequence = SelectWeightedSequence(ACT_IDLE);
		ResetSequence(nSequence);

		CPASAttenuationFilter sndFilter(this, "PropJeep.AmmoClose");
		EmitSound(sndFilter, entindex(), "PropJeep.AmmoClose");
	}
	*/
}



/*
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::Think( void )
{
	BaseClass::Think();

	SetNextThink( gpGlobals->curtime );


	// Water!?
	HandleWater();

	SetSimulationTime(gpGlobals->curtime);

	SetNextThink(gpGlobals->curtime);
	SetAnimatedEveryTick(true);

	if (!m_bInitialHandbrake)	// after initial timer expires, set the handbrake
	{
		m_bInitialHandbrake = true;
		m_VehiclePhysics.SetHandbrake(true);
		m_VehiclePhysics.Think();
	}

	// Check overturned status.
	if (!IsOverturned())
	{
		m_flOverturnedTime = 0.0f;
	}
	else
	{
		m_flOverturnedTime += gpGlobals->frametime;
	}


	if ( !m_bInitialHandbrake )	// after initial timer expires, set the handbrake
	{
		m_bInitialHandbrake = true;
		m_VehiclePhysics.SetHandbrake( true );
		m_VehiclePhysics.Think();
	}

	StudioFrameAdvance();

	if ( IsSequenceFinished() )
	{
		int iSequence = SelectWeightedSequence( ACT_IDLE );
		if ( iSequence > ACTIVITY_NOT_AVAILABLE )
		{
			SetCycle( 0 );
			m_flAnimTime = gpGlobals->curtime;
			ResetSequence( iSequence );
			ResetClientsideFrame();
		}
	}

	if (m_debugOverlays & OVERLAY_NPC_KILL_BIT)
	{
		CTakeDamageInfo info( this, this, m_iHealth, DMG_BLAST );
		info.SetDamagePosition( WorldSpaceCenter() );
		info.SetDamageForce( Vector( 0, 0, 1 ) );
		TakeDamage( info );
	}

	if (IsSequenceFinished() && (m_bExitAnimOn || m_bEnterAnimOn))
	{
		if (m_bEnterAnimOn)
		{
			m_VehiclePhysics.ReleaseHandbrake();
			StartEngine();

			// HACKHACK: This forces the jeep to play a sound when it gets entered underwater
			if (m_VehiclePhysics.IsEngineDisabled())
			{
				CBaseServerVehicle* pServerVehicle = dynamic_cast<CBaseServerVehicle*>(GetServerVehicle());
				if (pServerVehicle)
				{
					pServerVehicle->SoundStartDisabled();
				}
			}

			// The first few time we get into the jeep, print the jeep help
			if (m_iNumberOfEntries < hud_jeephint_numentries.GetInt())
			{
				g_EventQueue.AddEvent(this, "ShowHudHint", 1.5f, this, this);
			}
		}

		if (hl2_episodic.GetBool())
		{
			// Set its running animation idle
			if (m_bEnterAnimOn)
			{
				// Idle running
				int nSequence = SelectWeightedSequence(ACT_IDLE_STIMULATED);
				if (nSequence > ACTIVITY_NOT_AVAILABLE)
				{
					SetCycle(0);
					m_flAnimTime = gpGlobals->curtime;
					ResetSequence(nSequence);
					ResetClientsideFrame();
				}
			}
		}

		// If we're exiting and have had the tau cannon removed, we don't want to reset the animation
		if (hl2_episodic.GetBool())
		{
			// Reset on exit anim
			GetServerVehicle()->HandleEntryExitFinish(m_bExitAnimOn, m_bExitAnimOn);
		}
	}



}
*/
//-----------------------------------------------------------------------------
// Purpose: Finds the true aiming position of the gun (looks at what player 
//			is looking at and adjusts)
// Input  : &resultDir - direction to be calculated
//-----------------------------------------------------------------------------
void CPropAPC::GetCannonAim(Vector* resultDir)
{
	Vector	muzzleOrigin;
	QAngle	muzzleAngles;

	GetAttachment(LookupAttachment("gun_ref"), muzzleOrigin, muzzleAngles);

	AngleVectors(muzzleAngles, resultDir);
}







//-----------------------------------------------------------------------------
// Aims the secondary weapon at a target 
//-----------------------------------------------------------------------------
void CPropAPC::AimSecondaryWeaponAt(CBaseEntity* pTarget)
{

	 
	m_hRocketTarget = pTarget;

	// Update the rocket target
	CreateAPCLaserDot();

	if (m_hRocketTarget)
	{
		m_hLaserDot->SetAbsOrigin(m_hRocketTarget->BodyTarget(WorldSpaceCenter(), false));

	}
	SetLaserDotTarget(m_hLaserDot, m_hRocketTarget);
	EnableLaserDot(m_hLaserDot, m_hRocketTarget != NULL);

}


//-----------------------------------------------------------------------------
// Aims the secondary weapon at a target 
//-----------------------------------------------------------------------------
void CPropAPC::AimSecondaryWeaponAtNPC(CBaseEntity* pTarget)
{


	m_hRocketTarget = pTarget;

	// Update the rocket target
	CreateAPCLaserDot();

	if (m_hRocketTarget)
	{
		m_hLaserDot->SetAbsOrigin(m_hRocketTarget->BodyTarget(WorldSpaceCenter(), false));

	}
	SetLaserDotTarget(m_hLaserDot, m_hRocketTarget);
	EnableLaserDot(m_hLaserDot, m_hRocketTarget != NULL);

}


	
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::DriveVehicle( float flFrameTime, CUserCmd *ucmd, int iButtonsDown, int iButtonsReleased )
{
	switch( m_lifeState )
	{
	case LIFE_ALIVE:
		{
			int iButtons = ucmd->buttons;

			// Toggle headlights on flashlight impulse (F key)
			if ( ucmd->impulse == 100 )
			{
				m_bHeadlightIsOn = !m_bHeadlightIsOn;
				EmitSound( "buttons/lightswitch2.wav" );
				ucmd->impulse = 0; // consume
			}

			if ( iButtons & IN_ATTACK )
			{
				FireMachineGun();
			}
			else if (iButtons & IN_ATTACK2)
			{
				FireRocket();
			}
		}
		break;

	case LIFE_DYING:
		FireDying( );
		break;

	case LIFE_DEAD:
		return;
	}

	BaseClass::DriveVehicle( flFrameTime, ucmd, iButtonsDown, iButtonsReleased );
}

void CPropAPC::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	BaseClass::Use( pActivator, pCaller, useType, value );



	CBasePlayer* pPlayer = ToBasePlayer(pActivator);

	if (pPlayer == NULL)
		return;

	// Find out if the player's looking at our ammocrate hitbox 
	Vector vecForward;
	pPlayer->EyeVectors(&vecForward, NULL, NULL);

	trace_t tr;
	Vector vecStart = pPlayer->EyePosition();
	UTIL_TraceLine(vecStart, vecStart + vecForward * 1024, MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_HITBOX, pPlayer, COLLISION_GROUP_NONE, &tr);


	if ( pActivator->IsPlayer() )
	{
		 EmitSound ( "combine.door_lock" );
	}
}


//-----------------------------------------------------------------------------
// Primary gun 
//-----------------------------------------------------------------------------
void CPropAPC::AimPrimaryWeapon( const Vector &vecWorldTarget ) 
{
	EntityMatrix parentMatrix;
	parentMatrix.InitFromEntity( this, m_nMachineGunBaseAttachment );
	Vector target = parentMatrix.WorldToLocal( vecWorldTarget ); 

	float quadTarget = target.LengthSqr();
	float quadTargetXY = target.x*target.x + target.y*target.y;

	// Target is too close!  Can't aim at it
	if ( quadTarget > m_vecBarrelPos.LengthSqr() )
	{
		// We're trying to aim the offset barrel at an arbitrary point.
		// To calculate this, I think of the target as being on a sphere with 
		// it's center at the origin of the gun.
		// The rotation we need is the opposite of the rotation that moves the target 
		// along the surface of that sphere to intersect with the gun's shooting direction
		// To calculate that rotation, we simply calculate the intersection of the ray 
		// coming out of the barrel with the target sphere (that's the new target position)
		// and use atan2() to get angles

		// angles from target pos to center
		float targetToCenterYaw = atan2( target.y, target.x );
		float centerToGunYaw = atan2( m_vecBarrelPos.y, sqrt( quadTarget - (m_vecBarrelPos.y*m_vecBarrelPos.y) ) );

		float targetToCenterPitch = atan2( target.z, sqrt( quadTargetXY ) );
		float centerToGunPitch = atan2( -m_vecBarrelPos.z, sqrt( quadTarget - (m_vecBarrelPos.z*m_vecBarrelPos.z) ) );

		QAngle angles;
		angles.Init( -RAD2DEG(targetToCenterPitch+centerToGunPitch), RAD2DEG( targetToCenterYaw + centerToGunYaw ), 0 );

		SetPoseParameter( "vehicle_weapon_yaw", angles.y );
		SetPoseParameter( "vehicle_weapon_pitch", angles.x );
		StudioFrameAdvance();

		float curPitch = GetPoseParameter( "vehicle_weapon_pitch" );
		float curYaw = GetPoseParameter( "vehicle_weapon_yaw" );
		m_bInFiringCone = (fabs(curPitch - angles.x) < 1e-3) && (fabs(curYaw - angles.y) < 1e-3);
	}
	else
	{
		m_bInFiringCone = false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CPropAPC::GetTracerType( void ) 
{
	return "HelicopterTracer"; 
}


//-----------------------------------------------------------------------------
// Allows the shooter to change the impact effect of his bullets
//-----------------------------------------------------------------------------
void CPropAPC::DoImpactEffect( trace_t &tr, int nDamageType )
{
	UTIL_ImpactTrace( &tr, nDamageType, "HelicopterImpact" );
} 


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::DoMuzzleFlash( void )
{
	CEffectData data;
	data.m_nEntIndex = entindex();
	data.m_nAttachmentIndex = m_nMachineGunMuzzleAttachment;
	data.m_flScale = 1.0f;
	DispatchEffect( "ChopperMuzzleFlash", data );

	BaseClass::DoMuzzleFlash();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CPropAPC::CanExitVehicle(CBaseEntity* pEntity)
{
	return (!m_bEnterAnimOn && !m_bExitAnimOn && !m_bLocked && (m_nSpeed <= g_apcexitspeed.GetFloat()));
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::FireMachineGun( void )
{
	if ( m_flMachineGunTime > gpGlobals->curtime )
		return;

	bool bPlayerFiring = (GetDriver() && GetDriver()->IsPlayer());

	if (bPlayerFiring)
	{
		// Player fires automatic (no burst pauses)
		m_flMachineGunTime = gpGlobals->curtime + MACHINE_GUN_BURST_TIME;
	}
	else
	{
		// NPC burst logic
		m_iMachineGunBurstLeft--;
		if ( m_iMachineGunBurstLeft > 0 )
		{
			m_flMachineGunTime = gpGlobals->curtime + MACHINE_GUN_BURST_TIME;
		}
		else
		{
			m_iMachineGunBurstLeft = MACHINE_GUN_BURST_SIZE;
			m_flMachineGunTime = gpGlobals->curtime + MACHINE_GUN_BURST_PAUSE_TIME;
		}
	}

	Vector vecMachineGunShootPos;
	Vector vecMachineGunDir;
	GetAttachment( m_nMachineGunMuzzleAttachment, vecMachineGunShootPos, &vecMachineGunDir );
	
	int	bulletType = GetAmmoDef()->Index("AR2");

	if (bPlayerFiring)
	{
		// Player gets higher accuracy and fires 2 bullets per shot
		FireBullets( 2, vecMachineGunShootPos, vecMachineGunDir, VECTOR_CONE_2DEGREES, MAX_TRACE_LENGTH, bulletType, 1 );
	}
	else
	{
		FireBullets( 1, vecMachineGunShootPos, vecMachineGunDir, VECTOR_CONE_8DEGREES, MAX_TRACE_LENGTH, bulletType, 1 );
	}

	DoMuzzleFlash();
	EmitSound( "Weapon_AR2.Single" );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::GetRocketShootPosition( Vector *pPosition )
{
	GetAttachment( m_nRocketAttachment, *pPosition );
}


//-----------------------------------------------------------------------------
// Create a corpse 
//-----------------------------------------------------------------------------
void CPropAPC::CreateCorpse( )
{
	m_lifeState = LIFE_DEAD;

	for ( int i = 0; i < APC_MAX_GIBS; ++i )
	{
		CPhysicsProp *pGib = assert_cast<CPhysicsProp*>(CreateEntityByName( "prop_physics" ));
		pGib->SetAbsOrigin( GetAbsOrigin() );
		pGib->SetAbsAngles( GetAbsAngles() );
		pGib->SetAbsVelocity( GetAbsVelocity() );
		pGib->SetModel( s_pGibModelName[i] );
		pGib->Spawn();
		pGib->SetMoveType( MOVETYPE_VPHYSICS );

		float flMass = pGib->GetMass();
		if ( flMass < 200 )
		{
			Vector vecVelocity;
			pGib->GetMassCenter( &vecVelocity );
			vecVelocity -= WorldSpaceCenter();
			vecVelocity.z = fabs(vecVelocity.z);
			VectorNormalize( vecVelocity );

			// Apply a force that would make a 100kg mass travel 150 - 300 m/s
			float flRandomVel = random->RandomFloat( 150, 300 );
			vecVelocity *= (100 * flRandomVel) / flMass;
			vecVelocity.z += 100.0f;
			AngularImpulse angImpulse = RandomAngularImpulse( -500, 500 );
			
			IPhysicsObject *pObj = pGib->VPhysicsGetObject();
			if ( pObj != NULL )
			{
				pObj->AddVelocity( &vecVelocity, &angImpulse );
			}
			pGib->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
		}	
		if( hl2_episodic.GetBool() )
		{
			// EP1 perf hit
			pGib->Ignite( 6, false );
		}
		else
		{
			pGib->Ignite( 60, false );
		}
	}

	AddSolidFlags( FSOLID_NOT_SOLID );
	AddEffects( EF_NODRAW );
	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
// Death volley 
//-----------------------------------------------------------------------------
void CPropAPC::FireDying( )
{
	if ( m_flRocketTime > gpGlobals->curtime )
		return;

	Vector vecRocketOrigin;
	GetRocketShootPosition(	&vecRocketOrigin );

	Vector vecDir;
	vecDir.Random( -1.0f, 1.0f );
	if ( vecDir.z < 0.0f )
	{
		vecDir.z *= -1.0f;
	}

	VectorNormalize( vecDir );

	Vector vecVelocity;
	VectorMultiply( vecDir, ROCKET_SPEED * random->RandomFloat( 0.75f, 1.25f ), vecVelocity );

	QAngle angles;
	VectorAngles( vecDir, angles );

	CAPCMissile *pRocket = (CAPCMissile *) CAPCMissile::Create( vecRocketOrigin, angles, vecVelocity, this );
	float flDeathTime = random->RandomFloat( 0.3f, 0.5f );
	if ( random->RandomFloat( 0.0f, 1.0f ) < 0.3f )
	{
		pRocket->ExplodeDelay( flDeathTime );
	}
	else
	{
		pRocket->AugerDelay( flDeathTime );
	}

	// Make erratic firing
	m_flRocketTime = gpGlobals->curtime + random->RandomFloat( DEATH_VOLLEY_MIN_FIRE_TIME, DEATH_VOLLEY_MAX_FIRE_TIME );
	if ( --m_iRocketSalvoLeft <= 0 )
	{
		CreateCorpse();
	}
}



void CPropAPC::FireRocket(void)
{
	if (m_flRocketTime > gpGlobals->curtime)
		return;

	Vector vecRocketOrigin;
	GetRocketShootPosition(&vecRocketOrigin);

	CBaseEntity *pDriver = GetDriver();
	if (pDriver && pDriver->IsPlayer())
	{
		// Player rocket: launch upward first, then laser-guide to crosshair
		Vector vecUpDir(0, 0, 1);
		Vector vecVelocity;
		VectorMultiply(vecUpDir, ROCKET_SPEED, vecVelocity);

		QAngle angles;
		VectorAngles(vecUpDir, angles);

		CAPCMissile* pRocket = (CAPCMissile*)CAPCMissile::Create(vecRocketOrigin, angles, vecVelocity, this);
		pRocket->IgniteDelay();

		// Set up laser dot guidance so rocket tracks player's crosshair
		CreateAPCLaserDot();
		if (m_hLaserDot)
		{
			EnableLaserDot(m_hLaserDot, true);
			SetLaserDotTarget(m_hLaserDot, NULL);
		}

		EmitSound("PropAPC.FireRocket");
		m_OnFiredMissile.FireOutput(this, this);
	}
	else
	{
		// NPC/scripted firing
		Vector vecFireDirection;
		GetCannonAim(&vecFireDirection);

		Vector vecVelocity;
		VectorMultiply(vecFireDirection, ROCKET_SPEED, vecVelocity);

		QAngle angles;
		VectorAngles(vecFireDirection, angles);

		CAPCMissile* pRocket = (CAPCMissile*)CAPCMissile::Create(vecRocketOrigin, angles, vecVelocity, this);
		pRocket->IgniteDelay();

		if (m_hSpecificRocketTarget)
		{
			pRocket->AimAtSpecificTarget(m_hSpecificRocketTarget);
			m_hSpecificRocketTarget = NULL;
		}
		else if (m_strMissileHint != NULL_STRING)
		{
			pRocket->SetGuidanceHint(STRING(m_strMissileHint));
		}

		EmitSound("PropAPC.FireRocket");
		m_OnFiredMissile.FireOutput(this, this);
	}

	m_flRocketTime = gpGlobals->curtime + ROCKET_DELAY_TIME;
}
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::FireRocketNPC( void )
{
	if ( m_flRocketTime > gpGlobals->curtime )
		return;

	// If we're still firing the salvo, fire quickly
	m_iRocketSalvoLeft--;
	if ( m_iRocketSalvoLeft > 0 )
	{
		m_flRocketTime = gpGlobals->curtime + ROCKET_DELAY_TIME;
	}
	else
	{
		// Reload the salvo
		m_iRocketSalvoLeft = ROCKET_SALVO_SIZE;
		m_flRocketTime = gpGlobals->curtime + random->RandomFloat( ROCKET_MIN_BURST_PAUSE_TIME, ROCKET_MAX_BURST_PAUSE_TIME );
	}

	Vector vecRocketOrigin;
	GetRocketShootPosition(	&vecRocketOrigin );

	static float s_pSide[] = { 0.966, 0.866, 0.5, -0.5, -0.866, -0.966 };

	Vector forward;
	GetVectors( &forward, NULL, NULL );

	Vector vecDir;
	CrossProduct( Vector( 0, 0, 1 ), forward, vecDir );
	vecDir.z = 1.0f;
	vecDir.x *= s_pSide[m_nRocketSide];
	vecDir.y *= s_pSide[m_nRocketSide];
	if ( ++m_nRocketSide >= 6 )
	{
		m_nRocketSide = 0;
	}

	VectorNormalize( vecDir );

	Vector vecVelocity;
	VectorMultiply( vecDir, ROCKET_SPEED, vecVelocity );

	QAngle angles;
	VectorAngles( vecDir, angles );



	CAPCMissile *pRocket = (CAPCMissile *)CAPCMissile::Create( vecRocketOrigin, angles, vecVelocity, this );
	pRocket->IgniteDelay();

	if ( m_hSpecificRocketTarget )
	{
		pRocket->AimAtSpecificTarget( m_hSpecificRocketTarget );
		m_hSpecificRocketTarget = NULL;
	}
	else if ( m_strMissileHint != NULL_STRING )
	{
		pRocket->SetGuidanceHint( STRING( m_strMissileHint ) );
	}

	EmitSound( "PropAPC.FireRocket" );
	m_OnFiredMissile.FireOutput( this, this );
}
									 

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CPropAPC::MaxAttackRange() const
{
	return ROCKET_ATTACK_RANGE_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPropAPC::OnRestore( void )
{
	IServerVehicle *pServerVehicle = GetServerVehicle();
	if ( pServerVehicle != NULL )
	{
		// Restore the passenger information we're holding on to
		pServerVehicle->RestorePassengerInfo();
	}
}

//========================================================================================================================================
// APC FOUR WHEEL PHYSICS VEHICLE SERVER VEHICLE
//========================================================================================================================================
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAPCFourWheelServerVehicle::NPC_AimPrimaryWeapon( Vector vecTarget )
{
	CPropAPC *pAPC = ((CPropAPC*)m_pVehicle);
	pAPC->AimPrimaryWeapon( vecTarget );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAPCFourWheelServerVehicle::NPC_AimSecondaryWeapon( Vector vecTarget )
{
	// Add some random noise
	//Vector vecOffset = vecTarget + RandomVector( -128, 128 );
//	((CPropAPC*)m_pVehicle)->AimSecondaryWeaponAt(&vecTarget, 0.1f);
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAPCFourWheelServerVehicle::Weapon_PrimaryRanges( float *flMinRange, float *flMaxRange )
{
	*flMinRange = MACHINE_GUN_ATTACK_RANGE_MIN;
	*flMaxRange = MACHINE_GUN_ATTACK_RANGE_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAPCFourWheelServerVehicle::Weapon_SecondaryRanges( float *flMinRange, float *flMaxRange )
{
	*flMinRange = ROCKET_ATTACK_RANGE_MIN;
	*flMaxRange = ROCKET_ATTACK_RANGE_MAX;
}

//-----------------------------------------------------------------------------
// Purpose: Return the time at which this vehicle's primary weapon can fire again
//-----------------------------------------------------------------------------
float CAPCFourWheelServerVehicle::Weapon_PrimaryCanFireAt( void )
{
	return ((CPropAPC*)m_pVehicle)->PrimaryWeaponFireTime();
}

//-----------------------------------------------------------------------------
// Purpose: Return the time at which this vehicle's secondary weapon can fire again
//-----------------------------------------------------------------------------
float CAPCFourWheelServerVehicle::Weapon_SecondaryCanFireAt( void )
{
	return ((CPropAPC*)m_pVehicle)->SecondaryWeaponFireTime();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vecEyeExitEndpoint - 
// Output : int
//-----------------------------------------------------------------------------
int CAPCFourWheelServerVehicle::GetExitAnimToUse(Vector& vecEyeExitEndpoint, bool& bAllPointsBlocked)
{
	bAllPointsBlocked = false;

	if (!m_bParsedAnimations)
	{
		// Load the entry/exit animations from the vehicle
		ParseEntryExitAnims();
		m_bParsedAnimations = true;
	}

	CBaseAnimating* pAnimating = dynamic_cast<CBaseAnimating*>(m_pVehicle);
	// If we don't have the gun anymore, we want to get out using the "gun-less" animation
	if (pAnimating && ((CPropAPC*)m_pVehicle))
	{
		// HACK: We know the tau-cannon removed exit anim uses the first upright anim's exit details
		trace_t tr;

		// Convert our offset points to worldspace ones
		Vector vehicleExitOrigin = m_ExitAnimations[0].vecExitPointLocal;
		QAngle vehicleExitAngles = m_ExitAnimations[0].vecExitAnglesLocal;
		UTIL_ParentToWorldSpace(pAnimating, vehicleExitOrigin, vehicleExitAngles);

		// Ensure the endpoint is clear by dropping a point down from above
		vehicleExitOrigin -= VEC_VIEW;
		Vector vecMove = Vector(0, 0, 64);
		Vector vecStart = vehicleExitOrigin + vecMove;
		Vector vecEnd = vehicleExitOrigin - vecMove;
		UTIL_TraceHull(vecStart, vecEnd, VEC_HULL_MIN, VEC_HULL_MAX, MASK_SOLID, NULL, COLLISION_GROUP_NONE, &tr);

		Assert(!tr.startsolid && tr.fraction < 1.0);
		m_vecCurrentExitEndPoint = vecStart + ((vecEnd - vecStart) * tr.fraction);
		vecEyeExitEndpoint = m_vecCurrentExitEndPoint + VEC_VIEW;
		m_iCurrentExitAnim = 0;
		//return pAnimating->LookupSequence("exit_tauremoved");
	}

	return BaseClass::GetExitAnimToUse(vecEyeExitEndpoint, bAllPointsBlocked);
}




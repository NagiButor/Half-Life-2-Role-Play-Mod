
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_verbose.h"

#ifdef GAME_DLL
#include "timecycle/env_timecycle.h"
#endif

#include "tier0/memdbgon.h"

static CDeferredLightGlobal *__g_pGlobalLight = NULL;
CDeferredLightGlobal *GetGlobalLight()
{
	return __g_pGlobalLight;
}

#ifdef GAME_DLL
BEGIN_DATADESC( CDeferredLightGlobal )

	DEFINE_KEYFIELD( m_str_Diff, FIELD_STRING, "diffuse" ),
	DEFINE_KEYFIELD( m_str_Ambient_High, FIELD_STRING, "ambient_high" ),
	DEFINE_KEYFIELD( m_str_Ambient_Low, FIELD_STRING, "ambient_low" ),

	DEFINE_KEYFIELD( m_flFadeTime, FIELD_FLOAT, "fadetime" ),

	DEFINE_FIELD( m_vecColor_Diff, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecColor_Ambient_High, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecColor_Ambient_Low, FIELD_VECTOR ),
	DEFINE_FIELD( m_iDefFlags, FIELD_INTEGER ),

END_DATADESC()
#endif

IMPLEMENT_NETWORKCLASS_DT( CDeferredLightGlobal, CDeferredLightGlobal_DT )
#ifdef GAME_DLL
	SendPropVector( SENDINFO( m_vecColor_Diff ), 32 ),
	SendPropVector( SENDINFO( m_vecColor_Ambient_High ), 32 ),
	SendPropVector( SENDINFO( m_vecColor_Ambient_Low ), 32 ),

	SendPropInt( SENDINFO( m_iDefFlags ), DEFLIGHTGLOBAL_FLAGS_MAX_SHARED_BITS, SPROP_UNSIGNED ),
#else
	RecvPropVector( RECVINFO( m_vecColor_Diff ) ),
	RecvPropVector( RECVINFO( m_vecColor_Ambient_High ) ),
	RecvPropVector( RECVINFO( m_vecColor_Ambient_Low ) ),

	RecvPropInt( RECVINFO( m_iDefFlags ) ),
#endif
END_NETWORK_TABLE();

LINK_ENTITY_TO_CLASS( light_deferred_global, CDeferredLightGlobal );

CDeferredLightGlobal::CDeferredLightGlobal()
{
	Assert( __g_pGlobalLight == NULL );
	__g_pGlobalLight = this;

	m_iDefFlags = DEFLIGHTGLOBAL_ENABLED | DEFLIGHTGLOBAL_SHADOW_ENABLED;
#ifdef GAME_DLL
	bGenerated = false;
#endif
}

CDeferredLightGlobal::~CDeferredLightGlobal()
{
	Assert( __g_pGlobalLight == this );
	__g_pGlobalLight = NULL;
}

#ifdef GAME_DLL

void CDeferredLightGlobal::Activate()
{
	BaseClass::Activate();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );

	m_iDefFlags = GetSpawnFlags();

	m_vecColor_Diff.GetForModify() = stringColToVec( STRING( m_str_Diff ) );
	m_vecColor_Ambient_High.GetForModify() = stringColToVec( STRING( m_str_Ambient_High ) );
	m_vecColor_Ambient_Low.GetForModify() = stringColToVec( STRING( m_str_Ambient_Low ) );

	if ( !GetTimecycle() )
	{
		CBaseEntity *ent = CreateEntityByName( "env_timecycle" );
		if ( ent )
		{
			DispatchSpawn( ent );
			ent->Activate();
		}
	}

	if ( DeferredVerboseLevel() >= 1 )
	{
		DevMsg( "light_deferred_global[%d] Activate: flags=0x%x ang=(%.1f %.1f %.1f) diff=(%.3f %.3f %.3f) ambh=(%.3f %.3f %.3f) ambl=(%.3f %.3f %.3f) fadetime=%.3f\n",
			entindex(),
			m_iDefFlags,
			XYZ( GetAbsAngles() ),
			XYZ( m_vecColor_Diff.Get() ),
			XYZ( m_vecColor_Ambient_High.Get() ),
			XYZ( m_vecColor_Ambient_Low.Get() ),
			m_flFadeTime );
	}
}

int CDeferredLightGlobal::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}

CON_COMMAND( r_deferred_light_global_override_angles, "" )
{
	if ( !GetGlobalLight() )
	{
		Warning( "No global light existing!\n" );
		return;
	}

	if ( args.ArgC() < 4 )
	{
		Warning( "Not enough parameters!\n" );
		return;
	}

	QAngle ang( atof( args[1] ), atof( args[2] ), atof( args[3] ) );
	GetGlobalLight()->SetAbsAngles( ang );
}

#else

static inline float DeferredSaturate( float x )
{
	return clamp( x, 0.0f, 1.0f );
}

static Vector DeferredLerp( float t, const Vector &a, const Vector &b )
{
	return a + ( b - a ) * t;
}

lightData_Global_t CDeferredLightGlobal::GetState()
{
	lightData_Global_t data;

	data.diff.Init( GetColor_Diffuse(), 1.0f );
	data.ambh.Init( GetColor_Ambient_High(), 1.0f );
	data.ambl.Init( GetColor_Ambient_Low(), 1.0f );

	Vector dir;
	AngleVectors( GetAbsAngles(), &dir );
	data.vecLight.Init( -dir, 0.0f );

	{
		const float sunZ = data.vecLight.z;
		const float day = DeferredSaturate( ( sunZ + 0.02f ) / 0.12f );
		const float twilight = DeferredSaturate( 1.0f - fabsf( sunZ ) / 0.10f ) * ( 1.0f - day );
		const float sunAltDeg = RAD2DEG( asinf( clamp( sunZ, -1.0f, 1.0f ) ) );

		Vector baseDiff( data.diff.x, data.diff.y, data.diff.z );
		Vector baseAmbH( data.ambh.x, data.ambh.y, data.ambh.z );
		Vector baseAmbL( data.ambl.x, data.ambl.y, data.ambl.z );

		const float warm = DeferredSaturate( ( 0.25f - sunZ ) / 0.25f ) * day;
		Vector warmTint( 1.0f, 0.55f, 0.25f );
		Vector neutralTint( 1.0f, 1.0f, 1.0f );
		Vector diffTint = DeferredLerp( warm, neutralTint, warmTint );

		float nightFade = DeferredSaturate( ( -sunAltDeg - 6.0f ) / 12.0f );
		nightFade = nightFade * nightFade * ( 3.0f - 2.0f * nightFade );

		const float nightAmbScale = Lerp( nightFade, 0.008f, 0.003f );

		Vector nightAmbH = baseAmbH * nightAmbScale;
		nightAmbH.x *= 0.40f;
		nightAmbH.y *= 0.55f;
		nightAmbH.z *= 1.20f;

		Vector nightAmbL = baseAmbL * nightAmbScale;
		nightAmbL.x *= 0.35f;
		nightAmbL.y *= 0.50f;
		nightAmbL.z *= 1.30f;

		Vector diff = baseDiff * ( day * day );
		diff.x *= diffTint.x;
		diff.y *= diffTint.y;
		diff.z *= diffTint.z;

		Vector ambH = DeferredLerp( day, nightAmbH, baseAmbH );
		Vector ambL = DeferredLerp( day, nightAmbL, baseAmbL );

		Vector twilightWarm( 0.22f, 0.10f, 0.00f );
		ambH += baseAmbH * twilight * twilightWarm;
		ambL += baseAmbL * twilight * twilightWarm * 0.6f;

		data.diff.Init( diff, 1.0f );
		data.ambh.Init( ambH, 1.0f );
		data.ambl.Init( ambL, 1.0f );
	}

	if ( IsEnabled() &&
		( data.diff.LengthSqr() > 0.01f ||
		data.ambh.LengthSqr() > 0.01f ||
		data.ambl.LengthSqr() > 0.01f ) )
	{
		data.bEnabled = true;
		const float sunAltDeg = RAD2DEG( asinf( clamp( data.vecLight.z, -1.0f, 1.0f ) ) );
		data.bShadow = HasShadow() && sunAltDeg > -2.0f;
	}

	if ( DeferredVerboseLevel() >= 2 )
	{
		DevMsg( "light_deferred_global[%d] GetState: enabled=%d shadow=%d ang=(%.1f %.1f %.1f) dir=(%.3f %.3f %.3f) diff=(%.3f %.3f %.3f) ambh=(%.3f %.3f %.3f) ambl=(%.3f %.3f %.3f)\n",
			entindex(),
			data.bEnabled ? 1 : 0,
			data.bShadow ? 1 : 0,
			XYZ( GetAbsAngles() ),
			data.vecLight.x, data.vecLight.y, data.vecLight.z,
			XYZ( data.diff ),
			XYZ( data.ambh ),
			XYZ( data.ambl ) );
	}

	return data;
}


#endif

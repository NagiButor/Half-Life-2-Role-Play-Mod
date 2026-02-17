
#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_verbose.h"

#include "tier0/memdbgon.h"


#ifdef GAME_DLL

#include "timecycle/env_timecycle.h"

static ConVar r_deferred_force_transmit( "r_deferred_force_transmit", "1", FCVAR_CHEAT, "Force light_deferred transmit to clients" );
BEGIN_DATADESC( CDeferredLight )

	DEFINE_KEYFIELD( m_str_Diff, FIELD_STRING, GetLightParamName( LPARAM_DIFFUSE ) ),
	DEFINE_KEYFIELD( m_str_Ambient, FIELD_STRING, GetLightParamName( LPARAM_AMBIENT ) ),

	DEFINE_KEYFIELD( m_flRadius, FIELD_FLOAT, GetLightParamName( LPARAM_RADIUS ) ),
	DEFINE_KEYFIELD( m_flFalloffPower, FIELD_FLOAT, GetLightParamName( LPARAM_POWER ) ),
	DEFINE_KEYFIELD( m_flSpotConeInner, FIELD_FLOAT, GetLightParamName( LPARAM_SPOTCONE_INNER ) ),
	DEFINE_KEYFIELD( m_flSpotConeOuter, FIELD_FLOAT, GetLightParamName( LPARAM_SPOTCONE_OUTER ) ),

	DEFINE_KEYFIELD( m_iVisible_Dist, FIELD_INTEGER, GetLightParamName( LPARAM_VIS_DIST ) ),
	DEFINE_KEYFIELD( m_iVisible_FadeRange, FIELD_INTEGER, GetLightParamName( LPARAM_VIS_RANGE ) ),
	DEFINE_KEYFIELD( m_iShadow_Dist, FIELD_INTEGER, GetLightParamName( LPARAM_SHADOW_DIST ) ),
	DEFINE_KEYFIELD( m_iShadow_FadeRange, FIELD_INTEGER, GetLightParamName( LPARAM_SHADOW_RANGE ) ),

	DEFINE_KEYFIELD( m_iLightType, FIELD_INTEGER, GetLightParamName( LPARAM_LIGHTTYPE ) ),
	DEFINE_KEYFIELD( m_str_CookieString, FIELD_STRING, GetLightParamName( LPARAM_COOKIETEX ) ),

	DEFINE_KEYFIELD( m_flStyle_Amount, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_AMT ) ),
	DEFINE_KEYFIELD( m_flStyle_Speed, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_SPEED ) ),
	DEFINE_KEYFIELD( m_flStyle_Smooth, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_SMOOTH ) ),
	DEFINE_KEYFIELD( m_flStyle_Random, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_RANDOM ) ),
	DEFINE_KEYFIELD( m_iStyle_Seed, FIELD_INTEGER, GetLightParamName( LPARAM_STYLE_SEED ) ),

	DEFINE_KEYFIELD( m_flEnableFromHour, FIELD_FLOAT, "enable_from_hour" ),
	DEFINE_KEYFIELD( m_flEnableToHour, FIELD_FLOAT, "enable_to_hour" ),
	DEFINE_KEYFIELD( m_bTimeToggleShadows, FIELD_BOOLEAN, "time_toggle_shadows" ),
	DEFINE_KEYFIELD( m_bTimeToggleVolumetrics, FIELD_BOOLEAN, "time_toggle_volumetrics" ),

#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	DEFINE_KEYFIELD( m_flVolumeLOD0Dist, FIELD_FLOAT, GetLightParamName( LPARAM_VOLUME_LOD0_DIST ) ),
	DEFINE_KEYFIELD( m_flVolumeLOD1Dist, FIELD_FLOAT, GetLightParamName( LPARAM_VOLUME_LOD1_DIST ) ),
	DEFINE_KEYFIELD( m_flVolumeLOD2Dist, FIELD_FLOAT, GetLightParamName( LPARAM_VOLUME_LOD2_DIST ) ),
	DEFINE_KEYFIELD( m_flVolumeLOD3Dist, FIELD_FLOAT, GetLightParamName( LPARAM_VOLUME_LOD3_DIST ) ),
#endif

#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	DEFINE_KEYFIELD( m_iVolumeSamples, FIELD_INTEGER, GetLightParamName( LPARAM_VOLUME_SAMPLES ) ),
#endif

END_DATADESC()
#endif

IMPLEMENT_NETWORKCLASS_DT( CDeferredLight, CDeferredLight_DT )

#ifdef GAME_DLL
	SendPropVector( SENDINFO( m_vecColor_Diff ), 32 ),
	SendPropVector( SENDINFO( m_vecColor_Ambient ), 32 ),

	SendPropFloat( SENDINFO( m_flRadius ) ),
	SendPropFloat( SENDINFO( m_flFalloffPower ) ),
	SendPropFloat( SENDINFO( m_flSpotConeInner ) ),
	SendPropFloat( SENDINFO( m_flSpotConeOuter ) ),

	SendPropInt( SENDINFO( m_iVisible_Dist ), 15, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iVisible_FadeRange ), 15, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iShadow_Dist ), 15, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iShadow_FadeRange ), 15, SPROP_UNSIGNED ),

	SendPropInt( SENDINFO( m_iLightType ), MAX_DEFLIGHTTYPE_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iDefFlags ), DEFLIGHT_FLAGS_MAX_SHARED_BITS, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iCookieIndex ), MAX_COOKIE_TEXTURES_BITS, SPROP_UNSIGNED ),

	SendPropFloat( SENDINFO( m_flStyle_Amount ) ),
	SendPropFloat( SENDINFO( m_flStyle_Speed ) ),
	SendPropFloat( SENDINFO( m_flStyle_Smooth ) ),
	SendPropFloat( SENDINFO( m_flStyle_Random ) ),
	SendPropInt( SENDINFO( m_iStyle_Seed ), DEFLIGHT_SEED_MAX_BITS, SPROP_UNSIGNED ),

#	if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	SendPropFloat( SENDINFO( m_flVolumeLOD0Dist ) ),
	SendPropFloat( SENDINFO( m_flVolumeLOD1Dist ) ),
	SendPropFloat( SENDINFO( m_flVolumeLOD2Dist ) ),
	SendPropFloat( SENDINFO( m_flVolumeLOD3Dist ) ),
#	endif

#	if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	SendPropInt( SENDINFO( m_iVolumeSamples ) ),
#	endif
#else
	RecvPropVector( RECVINFO( m_vecColor_Diff ) ),
	RecvPropVector( RECVINFO( m_vecColor_Ambient ) ),

	RecvPropFloat( RECVINFO( m_flRadius ) ),
	RecvPropFloat( RECVINFO( m_flFalloffPower ) ),
	RecvPropFloat( RECVINFO( m_flSpotConeInner ) ),
	RecvPropFloat( RECVINFO( m_flSpotConeOuter ) ),

	RecvPropInt( RECVINFO( m_iVisible_Dist ) ),
	RecvPropInt( RECVINFO( m_iVisible_FadeRange ) ),
	RecvPropInt( RECVINFO( m_iShadow_Dist ) ),
	RecvPropInt( RECVINFO( m_iShadow_FadeRange ) ),

	RecvPropInt( RECVINFO( m_iLightType ) ),
	RecvPropInt( RECVINFO( m_iDefFlags ) ),
	RecvPropInt( RECVINFO( m_iCookieIndex ) ),

	RecvPropFloat( RECVINFO( m_flStyle_Amount ) ),
	RecvPropFloat( RECVINFO( m_flStyle_Speed ) ),
	RecvPropFloat( RECVINFO( m_flStyle_Smooth ) ),
	RecvPropFloat( RECVINFO( m_flStyle_Random ) ),
	RecvPropInt( RECVINFO( m_iStyle_Seed ) ),

#	if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	RecvPropFloat( RECVINFO( m_flVolumeLOD0Dist ) ),
	RecvPropFloat( RECVINFO( m_flVolumeLOD1Dist ) ),
	RecvPropFloat( RECVINFO( m_flVolumeLOD2Dist ) ),
	RecvPropFloat( RECVINFO( m_flVolumeLOD3Dist ) ),
#	endif

#	if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	RecvPropInt( RECVINFO( m_iVolumeSamples ) ),
#	endif

#endif

END_NETWORK_TABLE();

LINK_ENTITY_TO_CLASS( light_deferred, CDeferredLight );

#ifdef CLIENT_DLL
class Clight_deferred_spotFoo
{
public:
	Clight_deferred_spotFoo( void )
	{
		GetClassMap().Add( "light_deferred_spot", "CDeferredLight", sizeof( CDeferredLight ), &CCDeferredLightFactory );
	}
};
static Clight_deferred_spotFoo g_Clight_deferred_spotFoo;
#else
LINK_ENTITY_TO_CLASS( light_deferred_spot, CDeferredLight );
#endif

CDeferredLight::CDeferredLight()
{
#ifdef GAME_DLL
	m_bShouldTransmit = false;
	m_flEnableFromHour = -1.0f;
	m_flEnableToHour = -1.0f;
	m_bTimeToggleShadows = true;
	m_bTimeToggleVolumetrics = true;
	m_bTimeGated = false;
	m_bLastTimeEnabled = false;
	m_iDefFlagsBase = 0;
#else
	m_pLight = NULL;
#endif
}

CDeferredLight::~CDeferredLight()
{
#ifdef CLIENT_DLL
	Assert( m_pLight == NULL );
#endif
}

#ifdef GAME_DLL

void CDeferredLight::Spawn()
{
	BaseClass::Spawn();

	m_iDefFlags = GetSpawnFlags();
	m_iDefFlagsBase = m_iDefFlags;
	m_bTimeGated = ( m_flEnableFromHour >= 0.0f || m_flEnableToHour >= 0.0f );

	if ( m_bTimeGated )
	{
		UpdateEnabledFromTime();
		SetThink( &CDeferredLight::TimegateThink );
		SetNextThink( gpGlobals->curtime );
	}
}

bool CDeferredLight::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( !szKeyName || !szValue )
		return BaseClass::KeyValue( szKeyName, szValue );

	if ( !Q_stricmp( szKeyName, "enable_from_hour" ) || !Q_stricmp( szKeyName, "enable_from" ) )
	{
		m_flEnableFromHour = atof( szValue );
		return true;
	}

	if ( !Q_stricmp( szKeyName, "enable_to_hour" ) || !Q_stricmp( szKeyName, "enable_to" ) )
	{
		m_flEnableToHour = atof( szValue );
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}

void CDeferredLight::Activate()
{
	BaseClass::Activate();

	SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );

	m_iDefFlags = GetSpawnFlags();
	m_iDefFlagsBase = m_iDefFlags;

	m_bTimeGated = ( m_flEnableFromHour >= 0.0f || m_flEnableToHour >= 0.0f );

	m_bShouldTransmit = GetParent() != NULL ||
		Q_strlen( GetEntityName().ToCStr() ) > 0 ||
		r_deferred_force_transmit.GetBool();

	if ( m_bTimeGated )
		m_bShouldTransmit = true;

	SetMoveType( (GetParent() != NULL) ? MOVETYPE_PUSH : MOVETYPE_NONE );

	DispatchUpdateTransmitState();

	const char *pszCookie = STRING( m_str_CookieString );

	if ( m_iDefFlags & DEFLIGHT_COOKIE_ENABLED &&
		pszCookie != NULL && Q_strlen( pszCookie ) > 0 )
		m_iCookieIndex = GetDeferredManager()->AddCookieTexture( pszCookie );
	else
		m_iCookieIndex = 0;

	Assert( m_iCookieIndex >= 0 && m_iCookieIndex < MAX_COOKIE_TEXTURES );

	m_vecColor_Diff.GetForModify() = stringColToVec( STRING( m_str_Diff ) );
	m_vecColor_Ambient.GetForModify() = stringColToVec( STRING( m_str_Ambient ) );

	if ( m_bTimeGated )
	{
		if ( !GetTimecycle() )
		{
			CBaseEntity *ent = CreateEntityByName( "env_timecycle" );
			if ( ent )
			{
				DispatchSpawn( ent );
				ent->Activate();
			}
		}

		UpdateEnabledFromTime();
		SetThink( &CDeferredLight::TimegateThink );
		SetNextThink( gpGlobals->curtime );
	}

	if ( DeferredVerboseLevel() >= 1 )
	{
		DevMsg( "light_deferred[%d] Activate: name=\"%s\" parent=\"%s\" transmit=%d type=%d flags=0x%x radius=%.1f falloff=%.3f cone=(%.2f/%.2f) vis=(%d/%d) shadow=(%d/%d) cookie=\"%s\" idx=%d diff=(%.3f %.3f %.3f) amb=(%.3f %.3f %.3f)\n",
			entindex(),
			GetEntityName().ToCStr(),
			( GetParent() ? GetParent()->GetClassname() : "" ),
			m_bShouldTransmit ? 1 : 0,
			m_iLightType,
			m_iDefFlags,
			m_flRadius,
			m_flFalloffPower,
			m_flSpotConeInner,
			m_flSpotConeOuter,
			m_iVisible_Dist,
			m_iVisible_FadeRange,
			m_iShadow_Dist,
			m_iShadow_FadeRange,
			( pszCookie ? pszCookie : "" ),
			m_iCookieIndex,
			XYZ( m_vecColor_Diff.Get() ),
			XYZ( m_vecColor_Ambient.Get() ) );
	}

	if ( !m_bShouldTransmit )
	{
		if ( m_iDefFlags & DEFLIGHT_ENABLED &&
			( m_vecColor_Diff.Get().LengthSqr() > 0 || m_vecColor_Ambient.Get().LengthSqr() > 0 ) &&
			m_flSpotConeOuter > 0.01f && m_flRadius > 0 )
		{
			GetDeferredManager()->AddWorldLight( this );
		}
		else
			AssertMsg( 0, "I'm turned off and nobody can turn me on :(" );

		UTIL_Remove( this );
	}
	else
	{
		UpdateSize();
	}
}

int CDeferredLight::UpdateTransmitState()
{
	if ( DeferredVerboseLevel() >= 3 )
	{
		DevMsg( "light_deferred[%d] UpdateTransmitState: transmit=%d\n",
			entindex(), m_bShouldTransmit ? 1 : 0 );
	}
	return SetTransmitState( m_bShouldTransmit ? FL_EDICT_PVSCHECK : FL_EDICT_DONTSEND );
}

void CDeferredLight::UpdateSize()
{
	SetSize( Vector( -m_flRadius, -m_flRadius, -m_flRadius ),
			Vector( m_flRadius, m_flRadius, m_flRadius ) );
}

void CDeferredLight::SetRadius( float r )
{
	m_flRadius = r;
	UpdateSize();
}

bool CDeferredLight::IsInTimeWindow( float hour ) const
{
	if ( !m_bTimeGated )
		return true;

	while ( hour < 0.0f ) hour += 24.0f;
	while ( hour >= 24.0f ) hour -= 24.0f;

	float start = m_flEnableFromHour;
	float end = m_flEnableToHour;

	if ( start < 0.0f )
		start = 0.0f;
	if ( end < 0.0f )
		end = 24.0f;

	while ( start < 0.0f ) start += 24.0f;
	while ( start >= 24.0f ) start -= 24.0f;
	while ( end < 0.0f ) end += 24.0f;
	while ( end >= 24.0f ) end -= 24.0f;

	if ( start == end )
		return true;

	if ( start < end )
		return ( hour >= start && hour < end );

	return ( hour >= start || hour < end );
}

void CDeferredLight::UpdateEnabledFromTime()
{
	CEnvTimecycle *tc = GetTimecycle();
	if ( !tc )
	{
		const bool shouldEnable = false;
		const bool currentlyEnabled = ( m_iDefFlags & ( DEFLIGHT_ENABLED | DEFLIGHT_VOLUMETRICS_ENABLED | DEFLIGHT_SHADOW_ENABLED ) ) != 0;
		if ( shouldEnable != m_bLastTimeEnabled || currentlyEnabled != shouldEnable )
		{
			m_bLastTimeEnabled = shouldEnable;
			m_iDefFlags = m_iDefFlagsBase & ~DEFLIGHT_ENABLED;
			if ( m_bTimeToggleShadows )
				m_iDefFlags &= ~DEFLIGHT_SHADOW_ENABLED;
			if ( m_bTimeToggleVolumetrics )
				m_iDefFlags &= ~DEFLIGHT_VOLUMETRICS_ENABLED;
			NetworkStateChanged();
		}
		return;
	}

	const float hour = tc->GetTimeOfDayHours();
	const bool shouldEnable = IsInTimeWindow( hour );
	const bool currentlyEnabled = ( m_iDefFlags & ( DEFLIGHT_ENABLED | DEFLIGHT_VOLUMETRICS_ENABLED | DEFLIGHT_SHADOW_ENABLED ) ) != 0;

	if ( shouldEnable == m_bLastTimeEnabled && ( shouldEnable == currentlyEnabled ) )
		return;

	m_bLastTimeEnabled = shouldEnable;

	if ( shouldEnable )
	{
		m_iDefFlags = m_iDefFlagsBase | DEFLIGHT_ENABLED;
		if ( m_bTimeToggleShadows )
			m_iDefFlags |= DEFLIGHT_SHADOW_ENABLED;
		if ( m_bTimeToggleVolumetrics )
			m_iDefFlags |= DEFLIGHT_VOLUMETRICS_ENABLED;
	}
	else
	{
		m_iDefFlags = m_iDefFlagsBase & ~DEFLIGHT_ENABLED;
		if ( m_bTimeToggleShadows )
			m_iDefFlags &= ~DEFLIGHT_SHADOW_ENABLED;
		if ( m_bTimeToggleVolumetrics )
			m_iDefFlags &= ~DEFLIGHT_VOLUMETRICS_ENABLED;
	}
	NetworkStateChanged();
}

void CDeferredLight::TimegateThink()
{
	UpdateEnabledFromTime();
	SetNextThink( gpGlobals->curtime + 0.25f );
}

#else

void CDeferredLight::ApplyDataToLight()
{
	Assert( m_pLight != NULL );

	m_pLight->ang = GetRenderAngles();
	m_pLight->pos = GetRenderOrigin();

	m_pLight->col_diffuse = GetColor_Diffuse();
	m_pLight->col_ambient = GetColor_Ambient();

	m_pLight->flRadius = GetRadius();
	m_pLight->flFalloffPower = GetFalloffPower();

	m_pLight->flSpotCone_Inner = SPOT_DEGREE_TO_RAD( GetSpotCone_Inner() );
	m_pLight->flSpotCone_Outer = SPOT_DEGREE_TO_RAD( GetSpotCone_Outer() );

	m_pLight->iVisible_Dist = GetVisible_Distance();
	m_pLight->iVisible_Range = GetVisible_FadeRange();
	m_pLight->iShadow_Dist = GetShadow_Distance();
	m_pLight->iShadow_Range = GetShadow_FadeRange();

	m_pLight->iStyleSeed = GetStyle_Seed();
	m_pLight->flStyle_Amount = GetStyle_Amount();
	m_pLight->flStyle_Random = GetStyle_Random();
	m_pLight->flStyle_Smooth = GetStyle_Smooth();
	m_pLight->flStyle_Speed = GetStyle_Speed();

	m_pLight->iLighttype = GetLight_Type();
	m_pLight->iFlags >>= DEFLIGHTGLOBAL_FLAGS_MAX_SHARED_BITS;
	m_pLight->iFlags <<= DEFLIGHTGLOBAL_FLAGS_MAX_SHARED_BITS;
	m_pLight->iFlags |= GetLight_Flags();
	m_pLight->iCookieIndex = GetCookieIndex();

#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	GetVolumeLODDistances( m_pLight->flVolumeLOD0Dist,
		 m_pLight->flVolumeLOD1Dist, m_pLight->flVolumeLOD2Dist,
		 m_pLight->flVolumeLOD3Dist );
#endif

#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	m_pLight->iVolumeSamples = GetVolumeSamples();
#endif

	if ( DeferredVerboseLevel() >= 3 )
	{
		DevMsg( "light_deferred[%d] ApplyDataToLight: type=%d flags=0x%x radius=%.1f falloff=%.3f pos=(%.1f %.1f %.1f) ang=(%.1f %.1f %.1f) cone=(%.3f/%.3f) vis=(%d/%d) shadow=(%d/%d) cookie=%d\n",
			entindex(),
			m_pLight->iLighttype,
			m_pLight->iFlags,
			m_pLight->flRadius,
			m_pLight->flFalloffPower,
			XYZ( m_pLight->pos ),
			XYZ( m_pLight->ang ),
			m_pLight->flSpotCone_Inner,
			m_pLight->flSpotCone_Outer,
			m_pLight->iVisible_Dist,
			m_pLight->iVisible_Range,
			m_pLight->iShadow_Dist,
			m_pLight->iShadow_Range,
			m_pLight->iCookieIndex );
	}
}

void CDeferredLight::PostDataUpdate( DataUpdateType_t t )
{
	BaseClass::PostDataUpdate( t );

	if ( t == DATA_UPDATE_CREATED )
	{
		Assert( m_pLight == NULL );

		m_pLight = new def_light_t();

		ApplyDataToLight();
		m_pLight->MakeDirtyAll();

		GetLightingManager()->AddLight( m_pLight );

		SetNextClientThink( CLIENT_THINK_ALWAYS );

		if ( DeferredVerboseLevel() >= 1 )
		{
			DevMsg( "light_deferred[%d] PostDataUpdate(CREATED): type=%d flags=0x%x radius=%.1f falloff=%.3f pos=(%.1f %.1f %.1f) ang=(%.1f %.1f %.1f)\n",
				entindex(),
				m_pLight->iLighttype,
				m_pLight->iFlags,
				m_pLight->flRadius,
				m_pLight->flFalloffPower,
				XYZ( m_pLight->pos ),
				XYZ( m_pLight->ang ) );
		}
	}
	else
	{
		ApplyDataToLight();

		m_pLight->MakeDirtyAll();

		if ( DeferredVerboseLevel() >= 2 )
		{
			DevMsg( "light_deferred[%d] PostDataUpdate(UPDATED): type=%d flags=0x%x radius=%.1f falloff=%.3f pos=(%.1f %.1f %.1f) ang=(%.1f %.1f %.1f)\n",
				entindex(),
				m_pLight->iLighttype,
				m_pLight->iFlags,
				m_pLight->flRadius,
				m_pLight->flFalloffPower,
				XYZ( m_pLight->pos ),
				XYZ( m_pLight->ang ) );
		}
	}
}

void CDeferredLight::UpdateOnRemove()
{
	BaseClass::UpdateOnRemove();

	if ( m_pLight != NULL )
	{
		if ( DeferredVerboseLevel() >= 1 )
			DevMsg( "light_deferred[%d] UpdateOnRemove\n", entindex() );

		GetLightingManager()->RemoveLight( m_pLight );
		delete m_pLight;
		m_pLight = NULL;
	}
}

void CDeferredLight::ClientThink()
{
	if ( m_pLight == NULL )
		return;

	Vector curOrig = GetRenderOrigin();
	QAngle curAng = GetRenderAngles();

	if ( VectorCompare( curOrig.Base(), m_pLight->pos.Base() ) == 0 ||
		VectorCompare( curAng.Base(), m_pLight->ang.Base() ) == 0 )
	{
		ApplyDataToLight();

		if ( m_pLight->flSpotCone_Outer != GetSpotCone_Outer() )
			m_pLight->MakeDirtyAll();
		else
			m_pLight->MakeDirtyXForms();
	}
}

#endif

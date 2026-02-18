#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/flashlighteffect_deferred.h"

#include "iinput.h"

#include "tier0/memdbgon.h"

ConVar r_flashlightvolumetrics("r_flashlightvolumetrics", "0", FCVAR_ARCHIVE);

CFlashlightEffectDeferred::CFlashlightEffectDeferred( int nEntIndex ) : CFlashlightEffect( nEntIndex )
{
	m_pDefLight = NULL;
}

CFlashlightEffectDeferred::~CFlashlightEffectDeferred()
{
	LightOff();
}

void CFlashlightEffectDeferred::UpdateLight( const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance )
{
	if ( !m_bIsOn )
		return;

	if ( m_pDefLight == NULL )
	{
		m_pDefLight = new def_light_t();
		GetLightingManager()->AddLight( m_pDefLight );

		ITexture *pTex = materials->FindTexture( "effects/flashlight001", TEXTURE_GROUP_OTHER );
		if ( !IsErrorTexture( pTex ) )
		{
			m_pDefLight->SetCookie( new CDefCookieTexture( pTex ) );
			m_pDefLight->iFlags |= DEFLIGHT_COOKIE_ENABLED;
		}
	}

	m_pDefLight->pos = vecPos;

	QAngle ang;
	VectorAngles( vecDir, vecUp, ang );
	m_pDefLight->ang = ang;

	m_pDefLight->col_diffuse.Init( 1.0f * m_flIntensityScale, 1.0f * m_flIntensityScale, 1.0f * m_flIntensityScale );
	m_pDefLight->flFalloffPower = 2.0f;
	m_pDefLight->flRadius = Max( 32.0f, (float)nDistance * m_flDistanceScale );
    const uint16 visDist = (uint16)MIN( (int)( m_pDefLight->flRadius + 256.0f ), 0xFFFF );
    m_pDefLight->iVisible_Dist = visDist;
    m_pDefLight->iVisible_Range = visDist;
    m_pDefLight->iShadow_Dist = visDist;
    m_pDefLight->iShadow_Range = visDist;
	m_pDefLight->iLighttype = DEFLIGHTTYPE_SPOT;

	ConVarRef flashlightFov( "r_flashlightfov" );
	const float flFov = flashlightFov.IsValid() ? flashlightFov.GetFloat() : 45.0f;
	const float flTunedFov = flFov * m_flFovScale;
	m_pDefLight->flSpotCone_Outer = SPOT_DEGREE_TO_RAD( flTunedFov );
	m_pDefLight->flSpotCone_Inner = SPOT_DEGREE_TO_RAD( flTunedFov * 0.8f );

	ConVarRef flashlightShadows( "r_flashlightdepthtexture" );
	if ( flashlightShadows.IsValid() && flashlightShadows.GetBool() )
		m_pDefLight->iFlags |= DEFLIGHT_SHADOW_ENABLED;
	else
		m_pDefLight->iFlags &= ~DEFLIGHT_SHADOW_ENABLED;

	const bool bDoVolumetrics = m_bForceVolumetrics || ( r_flashlightvolumetrics.GetBool() && input->CAM_IsThirdPerson() );
	if ( bDoVolumetrics )
		m_pDefLight->iFlags |= DEFLIGHT_VOLUMETRICS_ENABLED;
	else
		m_pDefLight->iFlags &= ~DEFLIGHT_VOLUMETRICS_ENABLED;

	m_pDefLight->MakeDirtyAll();
}

void CFlashlightEffectDeferred::LightOff()
{
	if ( m_pDefLight != NULL )
	{
		GetLightingManager()->RemoveLight( m_pDefLight );
		delete m_pDefLight;
		m_pDefLight = NULL;
	}
}

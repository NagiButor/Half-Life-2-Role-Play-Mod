//========= HL2RPM ============================================================//
//
// Purpose: Engine dynamic lights -> deferred lights.
//
// Muzzle flashes, explosions, burning entities, flares, light_dynamic, sparks...
// allocate engine dlights (world) and elights (models only). The forward
// renderer adds them to the lightmaps and model lighting, but the deferred
// composite only uses the light accumulation buffer, so they lit nothing.
// Every frame the active ones are mirrored as deferred lights (no shadows).
//
// Elights can only be looked up by key, so the client's `effects` interface is
// wrapped to learn the keys the game code allocates.
//
//=============================================================================//

#include "cbase.h"
#include "iefx.h"
#include "dlight.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_dlight_bridge.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_dlights( "r_deferred_dlights", "1", FCVAR_ARCHIVE, "Engine dynamic lights (muzzle flashes, explosions, fire, flares) light the deferred scene" );
static ConVar r_deferred_dlights_scale( "r_deferred_dlights_scale", "1.0", FCVAR_ARCHIVE, "Brightness of engine dynamic lights in the deferred renderer" );
static ConVar r_deferred_dlights_max( "r_deferred_dlights_max", "16", FCVAR_ARCHIVE, "Most engine dynamic lights mirrored per frame" );
static ConVar r_deferred_dlights_debug( "r_deferred_dlights_debug", "0", 0, "Print the mirrored engine dynamic lights" );

// ---------------------------------------------------------------------------
// effects proxy: forwards everything to the engine, remembers elight keys
// ---------------------------------------------------------------------------
class CEffectsProxy : public IVEfx
{
public:
	CEffectsProxy() : m_pEngine( NULL ) {}

	virtual int Draw_DecalIndexFromName( char *name )
	{
		return m_pEngine->Draw_DecalIndexFromName( name );
	}

	virtual void DecalShoot( int textureIndex, int entity, const model_t *model, const Vector &model_origin,
		const QAngle &model_angles, const Vector &position, const Vector *saxis, int flags )
	{
		m_pEngine->DecalShoot( textureIndex, entity, model, model_origin, model_angles, position, saxis, flags );
	}

	virtual void DecalColorShoot( int textureIndex, int entity, const model_t *model, const Vector &model_origin,
		const QAngle &model_angles, const Vector &position, const Vector *saxis, int flags, const color32 &rgbaColor )
	{
		m_pEngine->DecalColorShoot( textureIndex, entity, model, model_origin, model_angles, position, saxis, flags, rgbaColor );
	}

	virtual void PlayerDecalShoot( IMaterial *material, void *userdata, int entity, const model_t *model,
		const Vector &model_origin, const QAngle &model_angles, const Vector &position, const Vector *saxis,
		int flags, const color32 &rgbaColor )
	{
		m_pEngine->PlayerDecalShoot( material, userdata, entity, model, model_origin, model_angles, position, saxis, flags, rgbaColor );
	}

	virtual dlight_t *CL_AllocDlight( int key )
	{
		return m_pEngine->CL_AllocDlight( key );
	}

	virtual dlight_t *CL_AllocElight( int key )
	{
		if ( m_ElightKeys.Find( key ) == m_ElightKeys.InvalidIndex() && m_ElightKeys.Count() < 256 )
			m_ElightKeys.AddToTail( key );
		return m_pEngine->CL_AllocElight( key );
	}

	virtual int CL_GetActiveDLights( dlight_t *pList[MAX_DLIGHTS] )
	{
		return m_pEngine->CL_GetActiveDLights( pList );
	}

	virtual const char *Draw_DecalNameFromIndex( int nIndex )
	{
		return m_pEngine->Draw_DecalNameFromIndex( nIndex );
	}

	virtual dlight_t *GetElightByKey( int key )
	{
		return m_pEngine->GetElightByKey( key );
	}

	IVEfx *m_pEngine;
	CUtlVector<int> m_ElightKeys;
};

static CEffectsProxy s_EffectsProxy;

IVEfx *DeferredDLights_WrapEffects( IVEfx *pEngineEffects )
{
	if ( !pEngineEffects )
		return NULL;
	s_EffectsProxy.m_pEngine = pEngineEffects;
	return &s_EffectsProxy;
}

// ---------------------------------------------------------------------------
// Mirroring
// ---------------------------------------------------------------------------
struct BridgedLight_t
{
	def_light_t *pLight;
	const dlight_t *pSource;
	int iFrame;
};
static CUtlVector<BridgedLight_t> s_Bridged;

struct DLightCandidate_t
{
	const dlight_t *pSource;
	bool bElight;
	float flScore;
};

static int CandidateSort( const DLightCandidate_t *a, const DLightCandidate_t *b )
{
	return ( a->flScore > b->flScore ) ? -1 : ( ( a->flScore < b->flScore ) ? 1 : 0 );
}

static void RemoveBridged( int i )
{
	if ( s_Bridged[i].pLight )
	{
		GetLightingManager()->RemoveLight( s_Bridged[i].pLight );
		delete s_Bridged[i].pLight;
	}
	s_Bridged.Remove( i );
}

void DeferredDLights_Clear()
{
	for ( int i = s_Bridged.Count() - 1; i >= 0; i-- )
		RemoveBridged( i );
	s_EffectsProxy.m_ElightKeys.RemoveAll();
}

static void UpdateBridgedLight( def_light_t *l, const dlight_t *src, bool bElight )
{
	// color: r,g,b * 2^exponent in lightmap units (255 = full light)
	Vector col( TexLightToLinear( src->color.r, src->color.exponent ),
		TexLightToLinear( src->color.g, src->color.exponent ),
		TexLightToLinear( src->color.b, src->color.exponent ) );
	col *= 1.0f / 255.0f;
	// the exponents vary wildly (hot muzzle flashes, light_dynamic "brightness"):
	// keep the hue, bring the strength into a range a deferred light can use
	const float flMax = Max( col.x, Max( col.y, col.z ) );
	if ( flMax > 1e-4f )
		col *= clamp( flMax, 0.6f, 3.0f ) / flMax;
	col *= r_deferred_dlights_scale.GetFloat() * ( bElight ? 0.6f : 1.0f );

	// Engine dlights light the lightmaps by distance only; a deferred light also uses
	// the angle, so one lying on the floor (flares, explosions at the impact point)
	// would not light the floor at all: lift it a little.
	l->pos = src->origin + Vector( 0, 0, clamp( src->GetRadius() * 0.08f, 4.0f, 24.0f ) );
	l->col_diffuse = col;
	l->col_ambient.Init();
	l->flRadius = Max( src->GetRadius(), 16.0f );
	l->flFalloffPower = 1.6f;

	const bool bSpot = src->m_OuterAngle > 0.0f && src->m_Direction.LengthSqr() > 0.01f;
	if ( bSpot )
	{
		QAngle ang;
		VectorAngles( src->m_Direction, ang );
		l->ang = ang;
		l->iLighttype = DEFLIGHTTYPE_SPOT;
		const float flOuter = clamp( src->m_OuterAngle * 2.0f, 2.0f, 175.0f );
		const float flInner = clamp( Min( src->m_InnerAngle * 2.0f, flOuter - 1.0f ), 1.0f, flOuter );
		l->flSpotCone_Outer = SPOT_DEGREE_TO_RAD( flOuter );
		l->flSpotCone_Inner = SPOT_DEGREE_TO_RAD( flInner );
	}
	else
	{
		l->ang.Init();
		l->iLighttype = DEFLIGHTTYPE_POINT;
	}

	const uint16 iVis = (uint16)Min( (int)( l->flRadius * 4.0f + 512.0f ), 0xFFFF );
	l->iVisible_Dist = iVis;
	l->iVisible_Range = (uint16)Min( (int)( l->flRadius * 2.0f + 256.0f ), 0xFFFF );
	l->iShadow_Dist = 0;
	l->iShadow_Range = 0;
	l->iFlags &= ~( DEFLIGHT_SHADOW_ENABLED | DEFLIGHT_VOLUMETRICS_ENABLED | DEFLIGHT_COOKIE_ENABLED | DEFLIGHT_LIGHTSTYLE_ENABLED );
	l->iFlags |= DEFLIGHT_ENABLED;
	l->MakeDirtyAll();
}

void DeferredDLights_Update( const Vector &vecViewOrigin )
{
	static int s_iLastFrame = -1;
	if ( gpGlobals->framecount == s_iLastFrame )
		return;
	s_iLastFrame = gpGlobals->framecount;

	if ( !r_deferred_dlights.GetBool() || !s_EffectsProxy.m_pEngine )
	{
		if ( s_Bridged.Count() )
			DeferredDLights_Clear();
		return;
	}

	CUtlVector<DLightCandidate_t> candidates;

	// world dlights
	dlight_t *pList[MAX_DLIGHTS];
	const int nDLights = s_EffectsProxy.m_pEngine->CL_GetActiveDLights( pList );
	for ( int i = 0; i < nDLights; i++ )
	{
		const dlight_t *dl = pList[i];
		if ( !dl || dl->GetRadius() < 8.0f || ( dl->die != 0.0f && dl->die < gpGlobals->curtime ) )
			continue;
		DLightCandidate_t &c = candidates[ candidates.AddToTail() ];
		c.pSource = dl;
		c.bElight = false;
	}

	// entity lights (NPC muzzle flashes...): skip the ones that double a world dlight
	for ( int k = s_EffectsProxy.m_ElightKeys.Count() - 1; k >= 0; k-- )
	{
		const dlight_t *el = s_EffectsProxy.m_pEngine->GetElightByKey( s_EffectsProxy.m_ElightKeys[k] );
		if ( !el || ( el->die != 0.0f && el->die < gpGlobals->curtime - 2.0f ) )
		{
			s_EffectsProxy.m_ElightKeys.Remove( k );
			continue;
		}
		if ( el->GetRadius() < 8.0f || ( el->die != 0.0f && el->die < gpGlobals->curtime ) )
			continue;

		bool bDuplicate = false;
		for ( int i = 0; i < nDLights; i++ )
		{
			if ( pList[i] && pList[i]->origin.DistToSqr( el->origin ) < 48.0f * 48.0f )
			{
				bDuplicate = true;
				break;
			}
		}
		if ( bDuplicate )
			continue;

		DLightCandidate_t &c = candidates[ candidates.AddToTail() ];
		c.pSource = el;
		c.bElight = true;
	}

	// keep the ones that matter most for this view
	for ( int i = 0; i < candidates.Count(); i++ )
	{
		const dlight_t *src = candidates[i].pSource;
		const float flLum = ( src->color.r * 0.3f + src->color.g * 0.59f + src->color.b * 0.11f ) * TexLightToLinear( 1, src->color.exponent );
		const float flDist = src->origin.DistTo( vecViewOrigin );
		candidates[i].flScore = flLum * src->GetRadius() / ( 1.0f + flDist / Max( src->GetRadius(), 1.0f ) );
	}
	candidates.Sort( CandidateSort );
	const int nMax = clamp( r_deferred_dlights_max.GetInt(), 0, 64 );
	if ( candidates.Count() > nMax )
		candidates.SetCountNonDestructively( nMax );

	// sync
	const int iFrame = gpGlobals->framecount;
	for ( int i = 0; i < candidates.Count(); i++ )
	{
		const DLightCandidate_t &c = candidates[i];
		int iSlot = -1;
		for ( int j = 0; j < s_Bridged.Count(); j++ )
		{
			if ( s_Bridged[j].pSource == c.pSource )
			{
				iSlot = j;
				break;
			}
		}
		if ( iSlot < 0 )
		{
			iSlot = s_Bridged.AddToTail();
			s_Bridged[iSlot].pSource = c.pSource;
			s_Bridged[iSlot].pLight = new def_light_t();
			GetLightingManager()->AddLight( s_Bridged[iSlot].pLight );
		}
		s_Bridged[iSlot].iFrame = iFrame;
		UpdateBridgedLight( s_Bridged[iSlot].pLight, c.pSource, c.bElight );

		if ( r_deferred_dlights_debug.GetBool() )
		{
			const def_light_t *l = s_Bridged[iSlot].pLight;
			Msg( "[dlight] %s key %d pos %.0f %.0f %.0f r %.0f col %.2f %.2f %.2f %s\n", c.bElight ? "elight" : "dlight",
				c.pSource->key, XYZ( l->pos ), l->flRadius, XYZ( l->col_diffuse ), l->iLighttype == DEFLIGHTTYPE_SPOT ? "spot" : "point" );
		}
	}

	for ( int j = s_Bridged.Count() - 1; j >= 0; j-- )
	{
		if ( s_Bridged[j].iFrame != iFrame )
			RemoveBridged( j );
	}
}

//========= HL2RPM ============================================================//
//
// Purpose: Fire and explosions light the deferred scene.
//
// The fires of the game are particle systems - env_fire (env_fire_tiny ..
// env_fire_large), the fires of the maps (fire_medium_01, bonfire ...), burning
// things (burning_character of a C_EntityFlame) - and none of them gives light:
// in the stock game a map put a light next to its fire. Explosions lit their
// surroundings with an engine dlight - for a tenth of a second, and the call
// was commented out in C_BaseExplosionEffect ("FIXME").
//
// Here every particle system of fire is a point light while it burns: orange,
// as large as its flames, flickering with its own noise (the position too: its
// shadows move). These are ordinary lights - the shadow budget of the lighting
// manager gives the nearest ones shadows. An explosion - a particle system of
// one, or the engine's te_explosion (env_explosion, grenades, barrels, rockets)
// - is a flash: very bright for an instant, fading over half a second into the
// glow of its fire. A flash takes a shadow at once (its light would come
// through the walls otherwise: there is no time for the budget's fade in).
//
//=============================================================================//

#include "cbase.h"
#include "particlemgr.h"
#include "particles_new.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_firelight.h"
#include "tier1/fmtstr.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_firelights( "r_deferred_firelights", "1", FCVAR_ARCHIVE, "Fire and explosions light the scene (deferred lights of the particle systems of fire and of the explosions)" );
static ConVar r_deferred_firelights_scale( "r_deferred_firelights_scale", "1.0", FCVAR_ARCHIVE, "Fire lights: brightness", true, 0.0f, true, 8.0f );
static ConVar r_deferred_firelights_max( "r_deferred_firelights_max", "12", 0, "Fire lights: the most at once (the strongest for the view)", true, 0.0f, true, 48.0f );
static ConVar r_deferred_firelights_radius( "r_deferred_firelights_radius", "3.2", 0, "Fire lights: the radius of the light in sizes of the flames", true, 0.5f, true, 16.0f );
static ConVar r_deferred_firelights_flicker( "r_deferred_firelights_flicker", "1.0", 0, "Fire lights: how much they flicker", true, 0.0f, true, 4.0f );
static ConVar r_deferred_firelights_shadow_dist( "r_deferred_firelights_shadow_dist", "1100", 0, "Fire lights: the distance to the camera within which a fire may get a shadow from the shadow budget (0 = never)", true, 0.0f, true, 8000.0f );
static ConVar r_deferred_firelights_flash_shadows( "r_deferred_firelights_flash_shadows", "2", 0, "Explosion flashes: the most of them that get a shadow at once (their light comes through walls without)", true, 0.0f, true, 8.0f );
static ConVar r_deferred_firelights_debug( "r_deferred_firelights_debug", "0", 0, "Print the fire lights" );

enum FireKind_t
{
	FIREKIND_NONE = 0,
	FIREKIND_FIRE,
	FIREKIND_EMBERS,
	FIREKIND_EXPLOSION,
};

// what a particle system is by its name
static FireKind_t ClassifyEffect( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return FIREKIND_NONE;
	static const char *const s_pszNot[] = { "muzzle", "ion_cannon", "steam", "water", "blood", "rain", "snow", "dust", "spark" };
	for ( const char *psz : s_pszNot )
	{
		if ( V_stristr( pszName, psz ) )
			return FIREKIND_NONE;
	}
	if ( V_stristr( pszName, "explosion" ) || V_stristr( pszName, "explode" ) || V_stristr( pszName, "turret_break" ) )
		return FIREKIND_EXPLOSION;
	if ( V_stristr( pszName, "fire" ) || V_stristr( pszName, "flame" ) || V_stristr( pszName, "burn" ) || V_stristr( pszName, "bonfire" ) )
		return FIREKIND_FIRE;
	if ( V_stristr( pszName, "ember" ) )
		return FIREKIND_EMBERS;
	return FIREKIND_NONE;
}

struct FireLight_t
{
	CNewParticleEffect *pEffect;	// NULL: a flash of te_explosion
	def_light_t *pLight;
	FireKind_t kind;
	float flSeed;
	float flBorn;
	int iFrame;
	float flPeakParticles;
	float flSize;					// smoothed size of the flames
	Vector vecPos;					// smoothed center of the flames
	float flFlashScale;				// flashes: how big
	int iEntity;					// what burns (left out of its own shadow), -1 = nothing
	float flStrength;				// this frame (for the choice of the strongest)
};

static CUtlVector< FireLight_t > s_Fires;

static void RemoveFire( int i )
{
	if ( s_Fires[i].pLight )
	{
		GetLightingManager()->RemoveLight( s_Fires[i].pLight );
		delete s_Fires[i].pLight;
	}
	s_Fires.Remove( i );
}

void DeferredFireLights_Clear()
{
	for ( int i = s_Fires.Count() - 1; i >= 0; i-- )
		RemoveFire( i );
}

bool DeferredFireLights_Enabled()
{
	return r_deferred_firelights.GetBool() && GetDeferredManager() && GetDeferredManager()->IsDeferredRenderingEnabled();
}

bool DeferredFireLights_IsFireLight( const def_light_t *l )
{
	for ( int i = 0; i < s_Fires.Count(); i++ )
	{
		if ( s_Fires[i].pLight == l )
			return true;
	}
	return false;
}

void DeferredFireLights_AddFlash( const Vector &vecOrigin, float flScale )
{
	if ( !DeferredFireLights_Enabled() )
		return;
	FireLight_t &f = s_Fires[ s_Fires.AddToTail() ];
	f.pEffect = NULL;
	f.pLight = NULL;
	f.kind = FIREKIND_EXPLOSION;
	f.flSeed = RandomFloat( 0.0f, 100.0f );
	f.flBorn = gpGlobals->curtime;
	f.iFrame = gpGlobals->framecount;
	f.flPeakParticles = 1.0f;
	f.flFlashScale = clamp( flScale, 0.25f, 4.0f );
	f.flSize = 96.0f * f.flFlashScale;
	f.vecPos = vecOrigin;
	f.iEntity = -1;
	f.flStrength = 0.0f;
}

// a flicker of a few incommensurate waves around 1
static float Flicker( float t, float flSeed )
{
	const float a = sinf( t * 9.7f + flSeed ) * 0.5f + sinf( t * 17.3f + flSeed * 1.7f ) * 0.3f + sinf( t * 31.1f + flSeed * 2.3f ) * 0.2f;
	const float b = sinf( t * 2.3f + flSeed * 0.7f );
	return 1.0f + ( a * 0.13f + b * 0.06f ) * r_deferred_firelights_flicker.GetFloat();
}

// how bright a fire is now (before the flicker): a flash of an explosion fades fast into the glow of
// its fire; a fire burns as strong as it has flames, faded in when it is lit
static float FireLevel( const FireLight_t &f, float flAge )
{
	if ( !f.pEffect )
		return ( 6.0f * expf( -flAge / 0.12f ) + 1.0f * expf( -flAge / 0.6f ) ) * Min( 1.0f, f.flFlashScale );
	const float flAlive = ( f.flPeakParticles > 0.0f )
		? clamp( f.pEffect->m_nActiveParticles / Max( f.flPeakParticles * 0.5f, 1.0f ), 0.0f, 1.0f ) : 0.0f;
	if ( f.kind == FIREKIND_EXPLOSION )
		return 6.0f * expf( -flAge / 0.12f ) + 1.1f * expf( -flAge / 0.7f ) * flAlive;
	return flAlive * clamp( flAge / 0.3f, 0.0f, 1.0f ) * ( f.kind == FIREKIND_EMBERS ? 0.35f : 1.0f );
}

static int StrengthSort( const int *a, const int *b )
{
	const float fa = s_Fires[ *a ].flStrength, fb = s_Fires[ *b ].flStrength;
	return ( fa > fb ) ? -1 : ( ( fa < fb ) ? 1 : 0 );
}

void DeferredFireLights_Update( const Vector &vecViewOrigin )
{
	static int s_iLastFrame = -1;
	if ( gpGlobals->framecount == s_iLastFrame )
		return;
	s_iLastFrame = gpGlobals->framecount;

	if ( !DeferredFireLights_Enabled() )
	{
		if ( s_Fires.Count() )
			DeferredFireLights_Clear();
		return;
	}

	const int iFrame = gpGlobals->framecount;
	const float flNow = gpGlobals->curtime;

	// the particle systems of fire alive now
	for ( CNewParticleEffect *pEffect = ParticleMgr()->FirstNewEffect(); pEffect; pEffect = pEffect->m_pNext )
	{
		if ( !pEffect->IsValid() || pEffect->m_bDormant || pEffect->GetRemoveFlag() )
			continue;
		const FireKind_t kind = ClassifyEffect( pEffect->GetEffectName() );
		if ( kind == FIREKIND_NONE )
			continue;
		int iSlot = -1;
		for ( int j = 0; j < s_Fires.Count(); j++ )
		{
			if ( s_Fires[j].pEffect == pEffect )
			{
				iSlot = j;
				break;
			}
		}
		if ( iSlot < 0 )
		{
			iSlot = s_Fires.AddToTail();
			FireLight_t &f = s_Fires[iSlot];
			f.pEffect = pEffect;
			f.pLight = NULL;
			f.kind = kind;
			f.flSeed = RandomFloat( 0.0f, 100.0f );
			f.flBorn = flNow;
			f.flPeakParticles = 0.0f;
			f.flSize = 0.0f;
			f.vecPos = pEffect->GetControlPointAtCurrentTime( 0 );
			f.flFlashScale = 1.0f;
			f.iEntity = -1;
		}
		FireLight_t &f = s_Fires[iSlot];
		f.iFrame = iFrame;

		// where the flames are and how big: the bounds of the particles (smoothed - they jump as
		// particles are born and die)
		Vector vecMin, vecMax;
		pEffect->GetBounds( &vecMin, &vecMax );
		const Vector vecCP = pEffect->GetControlPointAtCurrentTime( 0 );
		Vector vecCenter = vecCP;
		float flSize = 32.0f;
		if ( pEffect->m_nActiveParticles > 0 && vecMax.x >= vecMin.x && ( vecMax - vecMin ).LengthSqr() < 4096.0f * 4096.0f )
		{
			vecCenter = ( vecMin + vecMax ) * 0.5f;
			const Vector vecExtent = vecMax - vecMin;
			flSize = Max( Max( vecExtent.x, vecExtent.y ), vecExtent.z * 0.7f );
		}
		// (the light stands low in the flames: the hot base lights the ground, not the smoke above)
		vecCenter.z = Lerp( 0.6f, vecCenter.z, vecCP.z + 8.0f );
		const float flBlend = ( f.flSize <= 0.0f ) ? 1.0f : clamp( gpGlobals->frametime * 4.0f, 0.0f, 1.0f );
		f.vecPos = Lerp( flBlend, f.vecPos, vecCenter );
		f.flSize = Lerp( flBlend, f.flSize, clamp( flSize, 16.0f, 400.0f ) );
		f.flPeakParticles = Max( f.flPeakParticles, (float)pEffect->m_nActiveParticles );

		// what burns: the entity the effect follows
		C_BaseEntity *pOwner = pEffect->GetControlPointEntity( 0 ).Get();
		f.iEntity = pOwner ? pOwner->entindex() : -1;
	}

	// strength of each now; the gone ones go
	for ( int i = s_Fires.Count() - 1; i >= 0; i-- )
	{
		FireLight_t &f = s_Fires[i];
		const float flAge = flNow - f.flBorn;
		if ( f.pEffect ? f.iFrame != iFrame : flAge > 2.0f )
		{
			RemoveFire( i );
			continue;
		}
		f.flStrength = FireLevel( f, flAge );
	}

	// the strongest for this view keep a light
	CUtlVector< int > order;
	for ( int i = 0; i < s_Fires.Count(); i++ )
	{
		FireLight_t &f = s_Fires[i];
		const float flRadius = f.flSize * r_deferred_firelights_radius.GetFloat() * ( f.kind == FIREKIND_EXPLOSION ? 1.6f : 1.0f );
		const float flDist = f.vecPos.DistTo( vecViewOrigin );
		f.flStrength *= flRadius / ( 1.0f + flDist / Max( flRadius, 1.0f ) );
		if ( f.flStrength > 1e-3f )
			order.AddToTail( i );
	}
	order.Sort( StrengthSort );
	const int nMax = r_deferred_firelights_max.GetInt();
	int nFlashShadows = 0;
	const float flScale = r_deferred_firelights_scale.GetFloat();

	for ( int k = 0; k < order.Count(); k++ )
	{
		FireLight_t &f = s_Fires[ order[k] ];
		if ( k >= nMax )
		{
			if ( f.pLight )
			{
				GetLightingManager()->RemoveLight( f.pLight );
				delete f.pLight;
				f.pLight = NULL;
			}
			continue;
		}
		if ( !f.pLight )
		{
			f.pLight = new def_light_t();
			GetLightingManager()->AddLight( f.pLight );
		}
		def_light_t *l = f.pLight;
		const float flAge = flNow - f.flBorn;
		const bool bExplosion = f.kind == FIREKIND_EXPLOSION;

		const float flLevel = FireLevel( f, flAge ) * Flicker( flNow, f.flSeed ) * flScale;

		// the color of a flame: deep orange in the glow, yellow white in the flash
		const float flHot = bExplosion ? clamp( 6.0f * expf( -flAge / 0.12f ) / 6.0f, 0.0f, 1.0f ) : 0.0f;
		const Vector vecColor = Lerp( flHot, Vector( 1.0f, 0.47f, 0.16f ), Vector( 1.0f, 0.82f, 0.55f ) );
		l->col_diffuse = vecColor * ( 2.1f * flLevel );
		l->col_ambient.Init();
		const float flRadius = f.flSize * r_deferred_firelights_radius.GetFloat() * ( bExplosion ? ( 1.6f + 0.6f * flHot ) : 1.0f );
		l->flRadius = clamp( flRadius, 96.0f, 1400.0f );
		l->flFalloffPower = 1.8f;
		// (a few units of wander: the shadows of what stands by a fire move with its flames)
		const float t = flNow + f.flSeed;
		const float flWander = bExplosion ? 0.0f : Min( f.flSize * 0.06f, 4.0f ) * r_deferred_firelights_flicker.GetFloat();
		l->pos = f.vecPos + Vector( sinf( t * 7.1f ), cosf( t * 5.3f ), sinf( t * 9.7f ) * 0.5f ) * flWander;
		l->ang.Init();
		l->iLighttype = DEFLIGHTTYPE_POINT;
		l->iVisible_Dist = (uint16)Min( (int)( l->flRadius * 5.0f + 1024.0f ), 0xFFFF );
		l->iVisible_Range = (uint16)Min( (int)( l->flRadius * 2.0f + 512.0f ), 0xFFFF );
		const float flShadowDist = r_deferred_firelights_shadow_dist.GetFloat();
		l->iShadow_Dist = (uint16)clamp( (int)flShadowDist, 0, 0xFFFF );
		l->iShadow_Range = (uint16)( flShadowDist > 0.0f ? 300 : 0 );
		l->iShadowExcludeEntIndex = f.iEntity;
		l->iFlags &= ~( DEFLIGHT_SHADOW_ENABLED | DEFLIGHT_VOLUMETRICS_ENABLED | DEFLIGHT_COOKIE_ENABLED | DEFLIGHT_LIGHTSTYLE_ENABLED );
		// (a flash has no time for the budget's fade in: it takes a shadow of its own, a few at once)
		if ( bExplosion && flAge < 0.8f && nFlashShadows < r_deferred_firelights_flash_shadows.GetInt()
			&& f.vecPos.DistTo( vecViewOrigin ) < Max( flShadowDist, 1.0f ) + l->flRadius )
		{
			l->iFlags |= DEFLIGHT_SHADOW_ENABLED;
			nFlashShadows++;
		}
		l->iFlags |= DEFLIGHT_ENABLED;
		l->MakeDirtyAll();

		if ( r_deferred_firelights_debug.GetBool() )
		{
			Msg( "[firelight] %s %s pos %.0f %.0f %.0f size %.0f r %.0f level %.2f%s%s\n",
				f.pEffect ? f.pEffect->GetEffectName() : "te_explosion", bExplosion ? "(explosion)" : "", XYZ( l->pos ),
				f.flSize, l->flRadius, flLevel, ( l->iFlags & DEFLIGHT_SHADOW_ENABLED ) ? " shadow" : "",
				f.iEntity >= 0 ? CFmtStr( " ent %d", f.iEntity ).Access() : "" );
		}
	}
	// the ones out of the choice now have no light
	for ( int i = 0; i < s_Fires.Count(); i++ )
	{
		if ( s_Fires[i].pLight && s_Fires[i].flStrength <= 1e-3f )
		{
			GetLightingManager()->RemoveLight( s_Fires[i].pLight );
			delete s_Fires[i].pLight;
			s_Fires[i].pLight = NULL;
		}
	}
}

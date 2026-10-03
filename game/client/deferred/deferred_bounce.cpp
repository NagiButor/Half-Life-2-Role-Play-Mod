//========= HL2RPM ============================================================//
//
// Purpose: One-bounce light of the lamps ("virtual point lights").
//
// A room lit by a lamp is lit twice: by the lamp, and by the light its floor and
// walls throw back. Baked lighting (vrad) has that bounce; the deferred lights did
// not, so a lamp made a bright spot in a black room - "worse than the old baked
// lighting". For every lamp near the camera a shadowless point light is placed
// where most of its light lands (the target of a spot, the floor under a bulb),
// colored by that surface (its texture reflectivity: a red carpet bounces red)
// with roughly the energy the surface reflects. It lights the walls and the
// ceiling from below and fills the shadows around the lamp.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_bounce.h"
#include "deferred/deferred_dlight_bridge.h"
#include "materialsystem/imaterial.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_bounce( "r_deferred_bounce", "1", FCVAR_ARCHIVE, "Lamps light the room a second time with the light bounced off the surface they shine on" );
static ConVar r_deferred_bounce_scale( "r_deferred_bounce_scale", "1.0", FCVAR_ARCHIVE, "Strength of the bounced light of the lamps" );
static ConVar r_deferred_bounce_max( "r_deferred_bounce_max", "24", FCVAR_ARCHIVE, "Most lamps with a bounce light at a time (the most important ones around the camera)" );
static ConVar r_deferred_bounce_debug( "r_deferred_bounce_debug", "0", 0, "Print the bounce lights" );

struct BounceLight_t
{
	def_light_t *pSource;
	def_light_t *pVPL;

	// where the source was when its surface was found (no retrace while it stays)
	Vector vecSrcPos;
	QAngle angSrc;
	float flSrcRadius;
	uint8 iSrcType;

	bool bHit;
	Vector vecHit;
	Vector vecHitNormal;
	float flHitDist;
	Vector vecAlbedo;

	int iFrame;
};
static CUtlVector< BounceLight_t > s_Bounce;

bool DeferredBounce_IsBounceLight( const def_light_t *l )
{
	for ( int i = 0; i < s_Bounce.Count(); i++ )
	{
		if ( s_Bounce[i].pVPL == l )
			return true;
	}
	return false;
}

static void RemoveBounce( int i )
{
	if ( s_Bounce[i].pVPL )
	{
		GetLightingManager()->RemoveLight( s_Bounce[i].pVPL );
		delete s_Bounce[i].pVPL;
	}
	s_Bounce.Remove( i );
}

void DeferredBounce_Clear()
{
	for ( int i = s_Bounce.Count() - 1; i >= 0; i-- )
		RemoveBounce( i );
}

// average color of the surface a trace hit (vtex stores it in every VTF)
static Vector SurfaceAlbedo( const trace_t &tr )
{
	Vector vecAlbedo( 0.3f, 0.3f, 0.3f );
	if ( tr.surface.name && *tr.surface.name && ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D | SURF_NODRAW ) ) == 0 )
	{
		IMaterial *pMat = materials->FindMaterial( tr.surface.name, TEXTURE_GROUP_WORLD, false );
		if ( pMat && !pMat->IsErrorMaterial() )
		{
			pMat->GetReflectivity( vecAlbedo );
			// reflectivity is linear; keep it plausible (textures are rarely calibrated)
			vecAlbedo.x = clamp( vecAlbedo.x, 0.04f, 0.8f );
			vecAlbedo.y = clamp( vecAlbedo.y, 0.04f, 0.8f );
			vecAlbedo.z = clamp( vecAlbedo.z, 0.04f, 0.8f );
		}
	}
	return vecAlbedo;
}

static void FindBounceSurface( BounceLight_t &b, const def_light_t *src )
{
	b.vecSrcPos = src->pos;
	b.angSrc = src->ang;
	b.flSrcRadius = src->flRadius;
	b.iSrcType = src->iLighttype;
	b.bHit = false;

	Vector vecDir( 0, 0, -1 );		// a bulb: the floor under it
	if ( src->iLighttype == DEFLIGHTTYPE_SPOT )
		AngleVectors( src->ang, &vecDir );

	trace_t tr;
	CTraceFilterWorldOnly filter;
	UTIL_TraceLine( src->pos + vecDir * 2.0f, src->pos + vecDir * ( src->flRadius * 0.9f ), MASK_SOLID_BRUSHONLY, &filter, &tr );
	if ( tr.startsolid || tr.fraction >= 1.0f || ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) )
		return;

	b.bHit = true;
	b.vecHit = tr.endpos;
	b.vecHitNormal = tr.plane.normal;
	b.flHitDist = ( tr.endpos - src->pos ).Length();
	b.vecAlbedo = SurfaceAlbedo( tr );
}

static void UpdateBounceLight( BounceLight_t &b, const def_light_t *src )
{
	def_light_t *l = b.pVPL;

	// energy the surface throws back. A spot concentrates its light on a small area
	// (reflected flux ~ intensity * tan^2 of the half cone), a bulb lights the whole
	// floor under it.
	float k;
	if ( src->iLighttype == DEFLIGHTTYPE_SPOT )
	{
		const float c = clamp( src->flSpotCone_Outer, 0.05f, 0.9999f );	// cos( half outer cone )
		const float flTan2 = ( 1.0f - c * c ) / ( c * c );
		k = clamp( 1.2f * flTan2, 0.25f, 1.0f );
	}
	else
	{
		k = 0.8f;
	}
	k *= r_deferred_bounce_scale.GetFloat();

	const Vector &a = b.vecAlbedo;
	Vector col( src->col_diffuse.x * a.x, src->col_diffuse.y * a.y, src->col_diffuse.z * a.z );
	col *= k;

	// lifted off the surface: lights what is around it, not the lit spot itself
	const float flLift = clamp( b.flHitDist * 0.15f, 8.0f, 40.0f );
	l->pos = b.vecHit + b.vecHitNormal * flLift;
	l->ang.Init();
	l->iLighttype = DEFLIGHTTYPE_POINT;
	l->col_diffuse = col;
	// surfaces facing away from it (the lit floor itself, backs) still get a soft fill
	l->col_ambient = col * 0.4f;
	// bounced light is a room-sized effect; it has no shadows, a long reach would leak
	// through the walls into the next room
	l->flRadius = Min( src->flRadius * 0.6f, 640.0f ) + flLift;
	l->flFalloffPower = 1.3f;

	l->iVisible_Dist = src->iVisible_Dist;
	l->iVisible_Range = src->iVisible_Range;
	l->iShadow_Dist = 0;
	l->iShadow_Range = 0;

	// flickers with its lamp
	l->iStyleSeed = src->iStyleSeed;
	l->flStyle_Amount = src->flStyle_Amount;
	l->flStyle_Smooth = src->flStyle_Smooth;
	l->flStyle_Random = src->flStyle_Random;
	l->flStyle_Speed = src->flStyle_Speed;

	l->iFlags &= ~( DEFLIGHT_SHADOW_ENABLED | DEFLIGHT_VOLUMETRICS_ENABLED | DEFLIGHT_COOKIE_ENABLED | DEFLIGHT_LIGHTSTYLE_ENABLED );
	l->iFlags |= DEFLIGHT_ENABLED | ( src->iFlags & DEFLIGHT_LIGHTSTYLE_ENABLED );
	l->MakeDirtyAll();
}

struct BounceCandidate_t
{
	def_light_t *pSource;
	float flScore;
};

static int BounceCandidateSort( const BounceCandidate_t *a, const BounceCandidate_t *b )
{
	return ( a->flScore > b->flScore ) ? -1 : ( ( a->flScore < b->flScore ) ? 1 : 0 );
}

void DeferredBounce_Update( const Vector &vecViewOrigin )
{
	static int s_iLastFrame = -1;
	if ( gpGlobals->framecount == s_iLastFrame )
		return;
	s_iLastFrame = gpGlobals->framecount;

	if ( !r_deferred_bounce.GetBool() || r_deferred_bounce_scale.GetFloat() <= 0.0f )
	{
		if ( s_Bounce.Count() )
			DeferredBounce_Clear();
		return;
	}

	CUtlVector< def_light_t* > lights;
	GetLightingManager()->GetAllDeferredLights( lights );

	CUtlVector< BounceCandidate_t > candidates;
	for ( int i = 0; i < lights.Count(); i++ )
	{
		def_light_t *l = lights[i];
		if ( !l || !( l->iFlags & DEFLIGHT_ENABLED ) || l->flRadius < 64.0f )
			continue;
		if ( DeferredBounce_IsBounceLight( l ) || DeferredDLights_IsMirrored( l ) || GetLightingManager()->IsTempLight( l ) )
			continue;

		const float flLum = l->col_diffuse.x * 0.3f + l->col_diffuse.y * 0.59f + l->col_diffuse.z * 0.11f;
		if ( flLum < 0.02f )
			continue;

		const float flDist = l->pos.DistTo( vecViewOrigin );
		if ( flDist > l->flRadius + 3000.0f )
			continue;

		BounceCandidate_t &c = candidates[ candidates.AddToTail() ];
		c.pSource = l;
		c.flScore = flLum * l->flRadius / ( 1.0f + flDist / l->flRadius );
	}

	candidates.Sort( BounceCandidateSort );
	const int nMax = clamp( r_deferred_bounce_max.GetInt(), 0, 64 );
	if ( candidates.Count() > nMax )
		candidates.SetCountNonDestructively( nMax );

	const int iFrame = gpGlobals->framecount;
	for ( int i = 0; i < candidates.Count(); i++ )
	{
		def_light_t *src = candidates[i].pSource;

		int iSlot = -1;
		for ( int j = 0; j < s_Bounce.Count(); j++ )
		{
			if ( s_Bounce[j].pSource == src )
			{
				iSlot = j;
				break;
			}
		}

		const bool bNew = ( iSlot < 0 );
		if ( bNew )
		{
			iSlot = s_Bounce.AddToTail();
			BounceLight_t &b = s_Bounce[iSlot];
			b.pSource = src;
			b.pVPL = NULL;
			FindBounceSurface( b, src );
		}

		BounceLight_t &b = s_Bounce[iSlot];
		b.iFrame = iFrame;

		// the lamp moved or turned (a flashlight, a swinging lamp): find its surface again
		if ( !bNew && ( b.vecSrcPos.DistToSqr( src->pos ) > 1.0f || b.iSrcType != src->iLighttype
			|| fabsf( b.flSrcRadius - src->flRadius ) > 1.0f
			|| ( src->iLighttype == DEFLIGHTTYPE_SPOT && ( fabsf( AngleDiff( b.angSrc.x, src->ang.x ) ) > 0.25f
				|| fabsf( AngleDiff( b.angSrc.y, src->ang.y ) ) > 0.25f ) ) ) )
		{
			FindBounceSurface( b, src );
		}

		if ( !b.bHit )
		{
			if ( b.pVPL )
			{
				GetLightingManager()->RemoveLight( b.pVPL );
				delete b.pVPL;
				b.pVPL = NULL;
			}
			continue;
		}

		if ( !b.pVPL )
		{
			b.pVPL = new def_light_t();
			GetLightingManager()->AddLight( b.pVPL );
		}
		UpdateBounceLight( b, src );

		if ( r_deferred_bounce_debug.GetBool() )
		{
			const def_light_t *l = b.pVPL;
			Msg( "[bounce] %s at %.0f %.0f %.0f -> vpl %.0f %.0f %.0f r %.0f col %.3f %.3f %.3f albedo %.2f %.2f %.2f\n",
				src->iLighttype == DEFLIGHTTYPE_SPOT ? "spot" : "point", XYZ( src->pos ), XYZ( l->pos ), l->flRadius,
				XYZ( l->col_diffuse ), XYZ( b.vecAlbedo ) );
		}
	}

	// lamps that are gone, off or no longer among the important ones
	for ( int j = s_Bounce.Count() - 1; j >= 0; j-- )
	{
		if ( s_Bounce[j].iFrame != iFrame )
			RemoveBounce( j );
	}
}

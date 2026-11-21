//========= HL2RPM ============================================================//
//
// Purpose: Camera sky visibility (for indoor sunshafts and dust motes) and
//          compatibility stubs for removed GI ConVars.
//          (All GI passes, render targets, and shaders have been removed.)
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_gi.h"

#include "tier1/callqueue.h"
#include "engine/IEngineTrace.h"

#include "tier0/memdbgon.h"

// Hidden compatibility stubs so existing config.cfg files never print "Unknown command"
static ConVar r_deferred_gi_stub( "r_deferred_gi", "0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_quality_stub( "r_deferred_gi_quality", "1", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_sky_open_stub( "r_deferred_gi_sky_open", "1.5", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_sky_desat_stub( "r_deferred_gi_sky_desat", "0.25", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_intensity_stub( "r_deferred_gi_intensity", "1.45", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_ao_stub( "r_deferred_gi_ao", "0.75", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_ray_length_stub( "r_deferred_gi_ray_length", "1.0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_fadeout_stub( "r_deferred_gi_fadeout", "100.0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_multibounce_stub( "r_deferred_gi_multibounce", "0.0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_budget_ms_stub( "r_deferred_gi_budget_ms", "1.0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_bounce_stub( "r_deferred_gi_bounce", "1.0", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_min_stub( "r_deferred_gi_min", "0.07", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_sky_gamma_stub( "r_deferred_gi_sky_gamma", "0.5", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_rays_stub( "r_deferred_gi_rays", "192", FCVAR_HIDDEN, "Removed (GI deleted)" );
static ConVar r_deferred_gi_sky_boost_stub( "r_deferred_gi_sky_boost", "4", FCVAR_HIDDEN, "Removed (GI deleted)" );

static float s_flCameraSky = 1.0f;
static float s_flSkySamples[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int s_iSkyRayIdx = 0;

void InitGIRTs()
{
}

void ShutdownGI()
{
}

void DeferredGI_Reset()
{
	s_flCameraSky = 1.0f;
	for ( int i = 0; i < 9; i++ )
		s_flSkySamples[i] = 1.0f;
	s_iSkyRayIdx = 0;
}

void DeferredGI_OnRTContentLost()
{
}

void DeferredGI_UpdateCameraSky( const Vector &origin )
{
	static const Vector s_Dirs[9] = {
		Vector(  0.000f,  0.000f, 1.000f ),
		Vector(  0.500f,  0.000f, 0.866f ),
		Vector( -0.500f,  0.000f, 0.866f ),
		Vector(  0.000f,  0.500f, 0.866f ),
		Vector(  0.000f, -0.500f, 0.866f ),
		Vector(  0.577f,  0.577f, 0.577f ),
		Vector( -0.577f,  0.577f, 0.577f ),
		Vector(  0.577f, -0.577f, 0.577f ),
		Vector( -0.577f, -0.577f, 0.577f ),
	};

	if ( enginetrace )
	{
		Ray_t ray;
		trace_t tr;
		CTraceFilterWorldOnly filter;
		ray.Init( origin, origin + s_Dirs[ s_iSkyRayIdx ] * 2048.0f );
		enginetrace->TraceRay( ray, MASK_SOLID_BRUSHONLY, &filter, &tr );
		const bool bHitSky = ( !tr.DidHit() ) || ( ( tr.surface.flags & SURF_SKY ) != 0 );
		s_flSkySamples[ s_iSkyRayIdx ] = bHitSky ? 1.0f : 0.0f;
		s_iSkyRayIdx = ( s_iSkyRayIdx + 1 ) % 9;
	}

	float flSum = 0.0f;
	for ( int i = 0; i < 9; i++ )
		flSum += s_flSkySamples[i];
	const float flTarget = flSum * ( 1.0f / 9.0f );
	const float flBlend = 1.0f - expf( -clamp( gpGlobals->absoluteframetime, 0.0f, 0.2f ) / 0.4f );
	s_flCameraSky += ( flTarget - s_flCameraSky ) * flBlend;

	giData_t gi;
	gi.bEnabled = false;
	gi.vecParams2.Init( s_flCameraSky, 1.0f, 0.0f, 0.0f );
	QUEUE_FIRE( CommitGIData, gi );
}

float DeferredGI_GetCameraSky()
{
	return s_flCameraSky;
}

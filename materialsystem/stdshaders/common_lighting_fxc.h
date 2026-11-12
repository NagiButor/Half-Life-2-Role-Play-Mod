
#ifndef COMMON_LIGHTING_H
#define COMMON_LIGHTING_H

static const float3 flWorldGridSize = float3(
	RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE * RADIOSITY_BUFFER_SAMPLES_XY,
	RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE * RADIOSITY_BUFFER_SAMPLES_XY,
	RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE * RADIOSITY_BUFFER_SAMPLES_Z
	);
static const float3 flWorldGridSize_Far = float3(
	RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR * RADIOSITY_BUFFER_SAMPLES_XY,
	RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR * RADIOSITY_BUFFER_SAMPLES_XY,
	RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR * RADIOSITY_BUFFER_SAMPLES_Z
	);

static const float2 flRadiosityTexelSizeHalf = float2( 0.5f / RADIOSITY_BUFFER_RES_X,
	0.5f / RADIOSITY_BUFFER_RES_Y );
static const float2 flRadiosityUVRatio = float2( RADIOSITY_UVRATIO_X, RADIOSITY_UVRATIO_Y );

// HL2RPM: how a point or spot light falls off with distance (the light passes, their volumetrics).
// It used to be ( 1 - d / r )^power alone: with a power of 1 (the lights converted from a map's light
// entities) or a bright light the light was still strong at its radius and stopped there - a circle
// on the floor and the walls where it was cut off. Now:
//  power > 0: the deferred light's own curve, ( 1 - d / r )^power, x ( 1 - x^8 ): the same up to
//             three quarters of the radius, then it reaches 0 with no edge, whatever the power;
//  power < 0: a light converted from the map's light / light_spot (cdeferred_manager_server.cpp) -
//             the engine's attenuation 1 / ( c + l d + q d^2 ) as k = c / q = -power, scaled to 1 at
//             100 units like vrad's brightness, in the window ( 1 - x^4 )^2 to its radius. k is
//             kept to 24^2 or more: the hot spot right at a lamp at most ~18 x its 100-unit light.
float DeferredLightFalloff( float flDist, float flRadius, float flPower )
{
	const float x = saturate( flDist / flRadius );
	const float x2 = x * x;
	const float x4 = x2 * x2;
	const float flOwn = pow( 1.0f - x, max( flPower, 0.01f ) ) * ( 1.0f - x4 * x4 );
	const float k = max( -flPower, 576.0f );
	const float w = 1.0f - x4;
	const float flEngine = ( k + 10000.0f ) / ( k + flDist * flDist ) * w * w;
	return ( flPower < 0.0f ) ? flEngine : flOwn;
}

// HL2RPM: a light far brighter than a lit surface can show (a floodlight of brightness 3000 is
// 11 x "white" at 100 units) was clipped flat by the composite's soft limit (r_deferred_light_limit)
// only after its cone and its edge had been applied: the whole penumbra of the cone was still over
// the limit and the light fell from "full" to nothing in the last degree of it - a hard ellipse on
// the floor, a circle on the wall. The same soft limit, on the light's own strength before its
// cone and edge, keeps the penumbra the author gave it. Returns the factor for the falloff.
#define DEFERRED_LIGHT_PEAK 2.5f
float DeferredLightPeak( float3 vecColor, float flFalloff )
{
	const float x = dot( vecColor, float3( 0.3f, 0.59f, 0.11f ) ) * flFalloff * ( 1.0f / DEFERRED_LIGHT_PEAK );
	const float x2 = x * x;
	return rsqrt( sqrt( 1.0f + x2 * x2 ) );
}

float3 DoStandardCookie( sampler sCookie, float2 uvs )
{
	return tex2D( sCookie, uvs ).rgb;
}

float3 DoCubemapCookie( sampler sCubemap, float3 delta )
{
	return texCUBE( sCubemap, delta ).rgb;
}

float4 DoLightFinal( float3 diffuse, float3 ambient, float4 litdot_lamount_spec_fade )
{
#if DEFCFG_CHEAP_LIGHTS
	return float4( diffuse * litdot_lamount_spec_fade.y, 0 );
#else
	// HL2RPM (PBR): the specular term is a luminance - the light's own, times its GGX reflection
	// (SpecularGGX) and the light amount (N.L and the shadow)
	return float4( lerp( ambient,
		diffuse * ( litdot_lamount_spec_fade.x ),
		litdot_lamount_spec_fade.y ),
		litdot_lamount_spec_fade.z * litdot_lamount_spec_fade.y * dot( diffuse, float3( 0.3f, 0.59f, 0.11f ) ) ) * litdot_lamount_spec_fade.w;
#endif
}

float4 DoLightFinalCookied( float3 diffuse, float3 ambient, float4 litdot_lamount_spec_fade, float3 vecCookieRGB )
{
#if DEFCFG_CHEAP_LIGHTS
	return float4( diffuse * vecCookieRGB * litdot_lamount_spec_fade.y, 0 );
#else
	return float4( lerp( ambient,
		diffuse * vecCookieRGB * ( litdot_lamount_spec_fade.x ),
		litdot_lamount_spec_fade.y ),
		litdot_lamount_spec_fade.z * litdot_lamount_spec_fade.y * dot( diffuse * vecCookieRGB, float3( 0.3f, 0.59f, 0.11f ) ) )
		* litdot_lamount_spec_fade.w;
#endif
}

float3 GetBilinearRadiositySample( sampler RadiositySampler, float3 vecPositionDelta, float2 vecUVOffset )
{
	float2 flGridUVLocal = vecPositionDelta.xy / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridIndexSplit;
	flGridIndexSplit.x = modf( vecPositionDelta.z * RADIOSITY_BUFFER_GRIDS_PER_AXIS, flGridIndexSplit.y );

	flGridIndexSplit.x *= RADIOSITY_BUFFER_GRIDS_PER_AXIS;
	float flSampleFrac = modf( flGridIndexSplit.x, flGridIndexSplit.x );

	flGridIndexSplit /= RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridUVLow = flRadiosityUVRatio * (flGridUVLocal + flGridIndexSplit) + flRadiosityTexelSizeHalf;

	flGridIndexSplit.x = modf(
		( floor( vecPositionDelta.z * RADIOSITY_BUFFER_SAMPLES_Z ) + 1 ) / RADIOSITY_BUFFER_GRIDS_PER_AXIS,
		flGridIndexSplit.y );
	flGridIndexSplit.y /= RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridUVHigh = flRadiosityUVRatio * (flGridUVLocal + flGridIndexSplit) + flRadiosityTexelSizeHalf;

	return lerp( tex2D( RadiositySampler, flGridUVLow + vecUVOffset ).rgb,
		tex2D( RadiositySampler, flGridUVHigh + vecUVOffset ).rgb, flSampleFrac );
}


float3 DoRadiosity( float3 worldPos,
	sampler RadiositySampler, float3 vecRadiosityOrigin, float3 vecRadiosityOrigin_Far,
	float flRadiositySettings )
{
#if RADIOSITY_SMOOTH_TRANSITION == 1
	float3 vecDeltaFar = ( worldPos - vecRadiosityOrigin_Far ) / flWorldGridSize_Far;

#if VENDOR == VENDOR_FXC_AMD
	AMD_PRE_5K_NON_COMPLIANT
#elif ( DEFCFG_DEFERRED_SHADING == 0 )
	clip( 0.5f - any( floor( vecDeltaFar ) ) );
#else
	flRadiositySettings *= 1 - any( floor( vecDeltaFar ) );
#endif

	float3 vecDeltaClose = ( worldPos - vecRadiosityOrigin ) / flWorldGridSize;

	float3 flTransition = abs( saturate( vecDeltaClose ) * 2 - 1 );
	float flBlendAmt = max( flTransition.x, max( flTransition.y, flTransition.z ) );
	flBlendAmt = smoothstep( 0.7f, 1.0f, flBlendAmt );

	return lerp( GetBilinearRadiositySample( RadiositySampler, vecDeltaClose, float2(0,0) ),
		GetBilinearRadiositySample( RadiositySampler, vecDeltaFar, float2(0,0.5f) ),
		flBlendAmt ) * flRadiositySettings.x;
#else
	float3 vecDelta = ( worldPos - vecRadiosityOrigin ) / flWorldGridSize;

#if VENDOR == VENDOR_FXC_AMD
	AMD_PRE_5K_NON_COMPLIANT
#else
	float flLerpTo1 = any( floor( (vecDelta.xyz - 0.025f) * 1.05f) );
#endif

	vecDelta = lerp( vecDelta,
		( worldPos - vecRadiosityOrigin_Far ) / flWorldGridSize_Far,
		flLerpTo1 );

	float2 flUV_Y_Offset = float2( 0, flLerpTo1 ) * 0.5f;

#if VENDOR == VENDOR_FXC_AMD
	AMD_PRE_5K_NON_COMPLIANT
#elif ( DEFCFG_DEFERRED_SHADING == 0 )
	clip( 0.5f - any( floor( vecDelta ) ) );
#else
	flRadiositySettings *= 1 - any( floor( vecDeltaFar ) );
#endif

	return GetBilinearRadiositySample( RadiositySampler, vecDelta, flUV_Y_Offset ) * flRadiositySettings.x;
#endif
}

#endif
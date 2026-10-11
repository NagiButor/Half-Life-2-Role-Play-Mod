
#ifndef COMMON_DEFERRED_FXC_H
#define COMMON_DEFERRED_FXC_H

// SKIP:		!$MODEL && $MORPHING_VTEX

// SKIP:		!$MODEL && $COMPRESSED_VERTS
// SKIP:		!$MODEL && $SKINNING
// SKIP:		!$MODEL && $MORPHING_VTEX

#include "common_fxc.h"
#include "deferred_global_common.h"
#include "common_lighting_fxc.h"
#include "common_shadowmapping_fxc.h"


float WriteDepth( float3 eyeworldvec, float3 eyeforward, float scale )
{
	return dot( eyeforward, eyeworldvec ) * scale;
}
float WriteDepth( float3 worldpos, float3 eyepos, float3 eyeforward, float scale )
{
	float3 d = worldpos - eyepos;
	return WriteDepth( d, eyeforward, scale );
}

float4 WriteLighting( float4 normalizedLight )
{
#if DEFCFG_LIGHTACCUM_COMPRESSED
	return normalizedLight / DEFCFG_LIGHTSCALE_COMPRESS_RATIO;
#else
	return normalizedLight;
#endif
}

float4 ReadLighting( float4 compressedLight )
{
#if DEFCFG_LIGHTACCUM_COMPRESSED
	return compressedLight * DEFCFG_LIGHTSCALE_COMPRESS_RATIO;
#else
	return compressedLight;
#endif
}

half2 EncodeDirectionVectorXYZToSphericalST( float3 vDirectionVector )
{
	half p = sqrt(vDirectionVector.z*8+8);
	return half2( vDirectionVector.xy/p + 0.5 );

    /*float2 vEncoded = normalize( vDirectionVector.xy ) * sqrt( -vDirectionVector.z * 0.5 + 0.5 );
    vEncoded = vEncoded * 0.5 + 0.5;
    return vEncoded;*/
}

float3 DecodeDirectionVectorXYZFromSphericalST( half2 vSphericalST )
{
	half2 fenc = vSphericalST*4-2;
    half f = dot(fenc,fenc);
    half g = sqrt(1-f/4);
    float3 n;
    n.xy = fenc*g;
    n.z = 1-f/2;
    return n;
    /*float3 vDirectionVector = float3(vSphericalST.x, vSphericalST.y, 0) * float3(2,2,0) + float3(-1,-1,1);
	float flZLength = dot( vDirectionVector.xyz, float3( -vDirectionVector.xy, -1 ) );
	vDirectionVector.z = flZLength;
	vDirectionVector.xy *= sqrt(flZLength);
    return vDirectionVector.xyz * 2 + float3(0,0,-1);*/
}

// gloss 6							-half lambert 1	- litface 1
// 128 + 64 + 32 + 16 + 8 + 4		- 2				- 1
// 6 bits, 63						- 1				- 1
//
// HL2RPM (PBR): the 6 bits that held a phong exponent hold the gloss of the surface, 0 (rough)
// .. 62 (a mirror); 63 stays reserved for the marks of the G-buffer pass (gbuffer_ps30). The
// lighting passes and the composite pass make the roughness of it: 1 - gloss / 62.

float PackLightingControls( int phong_exp, int half_lambert, int litface )
{
	return ( litface +
		half_lambert * 2 +
		phong_exp * 4 ) * 0.0039215686f;
}

// (the first output is the roughness now - the name is kept where the passes call it)
void UnpackLightingControls( float mixed,
	out float phong_exp, out bool half_lambert, out bool litface )
{
	mixed *= 255.0f;

	litface = fmod( mixed, 2.0f );
	float tmp = fmod( mixed -= litface, 4.0f );
	phong_exp = fmod( mixed -= tmp, 256.0f );

	half_lambert = tmp * 0.5f;
	// (the marks - the 3D skybox, the first person body - are matte)
	phong_exp = ( phong_exp > 250.0f ) ? 1.0f : clamp( 1.0f - phong_exp * ( 1.0f / 248.0f ), 0.08f, 1.0f );
}

// HL2RPM (PBR): the roughness alone (the composite pass)
float UnpackRoughness( float mixed )
{
	mixed *= 255.0f;
	const float gloss = mixed - fmod( mixed, 4.0f );
	return ( gloss > 250.0f ) ? 1.0f : clamp( 1.0f - gloss * ( 1.0f / 248.0f ), 0.08f, 1.0f );
}

// HL2RPM (PBR): the mirror reflection of a light off a rough surface - the GGX distribution of
// the microfacets x their Smith-Schlick visibility. Without N.L (the passes multiply their
// light amount in, which holds it and the shadow). In the units of this renderer, where a
// light's color is what multiplies the albedo (irradiance / pi): the pi of the distribution
// cancels.
//
// The Fresnel factor - how much of the light a surface mirrors: a few % seen from the front,
// nearly all of it at a glancing angle (the glare of a road toward a low sun) - is split: the
// composite pass, which knows the material, multiplies by its reflectance from the front (F0);
// its growth toward glancing angles is here, as a multiple of the F0 of a common non-metal
// (PBR_F0_REF: 1 from the front .. PBR_FRESNEL_MAX). A metal, whose F0 is high, comes out too
// bright at glancing angles for it - capped.
#define PBR_F0_REF 0.04f
#define PBR_FRESNEL_MAX 9.0f

float SpecularGGX( float3 N, float3 V, float3 L, float roughness )
{
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float3 H = normalize( V + L );
	const float NH = saturate( dot( N, H ) );
	const float NL = saturate( dot( N, L ) );
	const float NV = abs( dot( N, V ) ) + 1e-3f;
	const float d = NH * NH * ( a2 - 1.0f ) + 1.0f;
	const float k = a * 0.5f;
	const float flFresnel = min( 1.0f + ( 1.0f / PBR_F0_REF - 1.0f ) * pow( 1.0f - saturate( dot( V, H ) ), 5.0f ), PBR_FRESNEL_MAX );
	return min( a2 / ( d * d ) * 0.25f / ( ( NL * ( 1.0f - k ) + k ) * ( NV * ( 1.0f - k ) + k ) ), 96.0f ) * flFresnel;
}

// ...and of the light around a surface (pre-integrated: the rougher it is, the less of the
// glancing angles' reflection is left)
float EnvFresnel( float3 N, float3 V, float roughness )
{
	return 1.0f + ( min( max( 1.0f - roughness, PBR_F0_REF ) * ( 1.0f / PBR_F0_REF ), PBR_FRESNEL_MAX ) - 1.0f )
		* pow( 1.0f - saturate( dot( N, V ) ), 5.0f );
}

#define ONEOVERTHREE 0.33333333333333333333333333333333
#define TWOOVERTHREE 0.66666666666666666666666666666666

float PackLightingControls( bool half_lambert, bool litface )
{
	if( half_lambert && litface )
		return 1;
	else if( half_lambert )
		return 1/3;
	else if( litface )
		return 2/3;
	else return 0;
}

void UnpackLightingControls( float mixed, 
	out bool half_lambert, out bool litface )
{
	if( mixed == 1 )
	{
		half_lambert = true;
		litface = true;
	}
	else if( mixed == 1/3 )
	{
		half_lambert = true;
		litface = false;
	}
	else if( mixed == 2/3 )
	{
		half_lambert = false;
		litface = true;
	}
	else
	{
		half_lambert = false;
		litface = false;
	}
}

// compensates for point sampling
float2 GetLightAccumUVs( float3 projXYW, float2 halfScreenTexelSize )
{
	return projXYW.xy / projXYW.z *
		float2( 0.5f, -0.5f ) +
		0.5f +
		halfScreenTexelSize;
}

float2 GetTransformedUVs( in float2 uv, in float4 transform[2] )
{
	float2 uvOut;

	uvOut.x = dot( uv, transform[0] ) + transform[0].w;
	uvOut.y = dot( uv, transform[1] ) + transform[1].w;

	return uvOut;
}

float GetModulatedBlend( in float flBlendAmount, in float2 modt )
{
	float minb = max( 0, modt.g - modt.r );
	float maxb = min( 1, modt.g + modt.r );

	return smoothstep( minb, maxb, flBlendAmount );
}

float GetMultiBlend( const float BlendAmount, inout float Remaining )
{
	float Result = smoothstep( 0.0, 1.0f, BlendAmount );
	Result = min( Result, Remaining );
	Remaining -= Result;

	return Result;
}

float GetMultiBlendModulated( in float2 modt, const float flBlendAmount, const float AlphaBlend, inout float Remaining )
{
	float Result = any( flBlendAmount )
		* GetModulatedBlend( flBlendAmount, modt );

	float alpha = 2.0 - AlphaBlend - modt.g;
	Result *= lerp( saturate( alpha ), 1, step( AlphaBlend, modt.g ) );

	Result = min( Result, Remaining );
	Remaining -= Result;

	return Result;
}

#endif

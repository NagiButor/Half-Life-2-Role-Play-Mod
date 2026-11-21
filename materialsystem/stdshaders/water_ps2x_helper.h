//========= Copyright Valve Corporation, All rights reserved. ============//
#include "common_ps_fxc.h"

struct DrawWater_params_t
{
	float2 vBumpTexCoord;
#if MULTITEXTURE
	float4 vExtraBumpTexCoord;
#endif
	float4 vReflectXY_vRefractYX;
	float w;
	float4 vReflectRefractScale;
	float fReflectOverbright;
	float4 vReflectTint;
	float4 vRefractTint;
	half3 vTangentEyeVect;
	float4 waterFogColor;
#if BASETEXTURE
	HALF4 lightmapTexCoord1And2;
	HALF4 lightmapTexCoord3;
#endif
	float4 vProjPos;
	float4 pixelFogParams;
	float fWaterFogStart;
	float fWaterFogEndMinusStart;
	float3 vEyePos;
	float3 vWorldPos;
	float fRefractDepthEncoded;	// HL2RPM: 1 = the refraction alpha is the depth under the surface (deferred)
	float4 vFoamColor;			// HL2RPM: rgb foam in the current light (linear), a its strength (0 = none)
	float4 vBodyLight;			// HL2RPM: x 1 / the light of the open sky and sun of the moment, y the most the water's own color is lit by (x that light), z density of the water's fog (1 = the material's), w 1 = by the path of the view ray, 0 = by the depth alone
	float4 vBodyAmbient;		// HL2RPM: rgb the least light of the water's own color where the sky is in sight (x the same: the sky light and what the water passes on from where the sun reaches it), w uv scale of the sky visibility target
	float4 vWaterKind;			// HL2RPM: xyz how much faster than the average each color fades on the way through the water, w how much its own color glows (radioactive)
#if FLOWMAP
	float4 vFlowParams1;		// HL2RPM: x flow map uv per unit, y normal uv per unit, z scroll distance, w bump strength
	float4 vFlowParams2;		// x time x timescale, y 1 / ( 2 x interval ), z noise uv per unit, w color layer on
	float4 vFlowColorParams1;	// x color uv per unit, y 1 / ( 2 x color interval ), z color scroll distance, w displacement by the normal
	float4 vFlowColorParams2;	// x color weight exponent, y color brightness, z color opacity
	float3 vFlowColorTint;		// rgb linear tint for $flow_colortexture
#endif
};

#if FLOWMAP
// HL2RPM: the flow of Portal 2 / Left 4 Dead 2 water (A. Vlachos, "Water Flow in Portal 2", 2010):
// a texture is carried along the flow vector of the map for one interval, then starts over; two
// layers half an interval apart are blended so that neither reset is seen, every interval is shifted
// by its own offset (no visible repetition), and a noise texture shifts the phase from place to place
// (the whole surface would pulse together otherwise). Returns the blend of the two layers' uv:
// xy layer 1, zw layer 2; flWeight2 - the share of layer 2.
float4 FlowUV( float2 vUv, float2 vFlow, float flTime, float flOoTwoIntervals, float flNoisePhase, float flScroll, out float flWeight2 )
{
	const float flT = flTime * flOoTwoIntervals + flNoisePhase;
	const float flPhase1 = frac( flT );
	const float flPhase2 = frac( flT + 0.5f );
	const float flOffset1 = floor( flT ) * 0.311f;
	const float flOffset2 = floor( flT + 0.5f ) * 0.311f + 0.5f;
	flWeight2 = abs( 2.0f * flPhase1 - 1.0f );
	return float4( vUv + flOffset1 + vFlow * ( flPhase1 * flScroll ),
				   vUv + flOffset2 + vFlow * ( flPhase2 * flScroll ) );
}
#endif

// HL2RPM: with the deferred renderer the alpha of the refraction view is the depth under the
// water surface, sqrt( depth / 1024 units ), the same for every water in sight
// (composite_ps30): turned into this water's fog here - 0 at the surface, 1 at the depth of its
// $fogend. (The stock alpha was the fog of the one water the engine picked - other waters
// showed its fog.) flPath: the length of the view ray in the water per unit of depth.
float DecodeRefractFog( float a, DrawWater_params_t i, float flPath )
{
	return ( i.fRefractDepthEncoded > 0.5f ) ? saturate( a * a * ( 1024.0f * flPath ) / max( i.fWaterFogEndMinusStart, 1.0f ) ) : a;
}

void DrawWater( in DrawWater_params_t i, 
#if BASETEXTURE
				in sampler BaseTextureSampler,
			    in sampler LightmapSampler,
#endif
#if FLOWMAP
				in sampler FlowSampler,
				in sampler FlowNoiseSampler,
				in sampler FlowColorSampler,
#endif
				in sampler NormalSampler,
			    in sampler RefractSampler,
			    in sampler ReflectSampler,
			    in sampler LightSampler,
			    in sampler SkySampler,
			    out float4 result, out float fogFactor )
{
	bool bReflect = REFLECT ? true : false;
	bool bRefract = REFRACT ? true : false;

#if FLOWMAP
	// HL2RPM: the normal carried by the flow map. World x / -y is the uv of a floor face in Hammer
	// (the flow maps of Portal 2 are drawn in it); the tangent space of a water face is the same.
	const float2 vWorldUv = float2( i.vWorldPos.x, -i.vWorldPos.y );
	const float4 vFlowTexel = tex2D( FlowSampler, vWorldUv * i.vFlowParams1.x );
	const float2 vFlow = vFlowTexel.rg * 2.0f - 1.0f;
	const float flNoise = tex2D( FlowNoiseSampler, vWorldUv * i.vFlowParams2.z ).g;
	float flFlowWeight2;
	const float4 vFlowUv = FlowUV( vWorldUv * i.vFlowParams1.y, vFlow, i.vFlowParams2.x, i.vFlowParams2.y, flNoise, i.vFlowParams1.z, flFlowWeight2 );
	const float4 vNormalTexel1 = tex2D( NormalSampler, vFlowUv.xy );
	const float4 vNormalTexel2 = tex2D( NormalSampler, vFlowUv.zw );
	float4 vNormal;
	vNormal.xy = lerp( vNormalTexel1.xy, vNormalTexel2.xy, flFlowWeight2 ) * 2.0f - 1.0f;
	vNormal.xy *= i.vFlowParams1.w;
	vNormal.z = sqrt( saturate( 1.0f - dot( vNormal.xy, vNormal.xy ) ) );
	vNormal.a = lerp( vNormalTexel1.a, vNormalTexel2.a, flFlowWeight2 );
#elif MULTITEXTURE
	float4 vNormal  = tex2D( NormalSampler, i.vBumpTexCoord );
	float4 vNormal1 = tex2D( NormalSampler, i.vExtraBumpTexCoord.xy );
	float4 vNormal2 = tex2D( NormalSampler, i.vExtraBumpTexCoord.zw );
	vNormal = 0.33 * ( vNormal + vNormal1 + vNormal2 );

#if ( NORMAL_DECODE_MODE == NORM_DECODE_ATI2N )
	vNormal.xy = vNormal.xy * 2.0f - 1.0f;
	vNormal.z = sqrt( 1.0f - dot(vNormal.xy, vNormal.xy) );
	vNormal.a = 1.0f;
#else
	vNormal.xyz = 2.0 * vNormal.xyz - 1.0;
#endif

#else
	float4 vNormal = DecompressNormal( NormalSampler, i.vBumpTexCoord, NORMAL_DECODE_MODE );
#endif

	// Perform division by W only once
	float ooW = 1.0f / i.w;

	float2 unwarpedRefractTexCoord = i.vReflectXY_vRefractYX.wz * ooW;

	// HL2RPM: what lies straight under this pixel of the surface (no wave offset)
#if REFRACT
	const float4 vUnwarpedRefract = tex2D( RefractSampler, unwarpedRefractTexCoord );
#else
	const float4 vUnwarpedRefract = float4( 0.0f, 0.0f, 0.0f, 0.0f );
#endif

	// HL2RPM: the view ray, bent at the surface (n = 1.33), runs 1 (looking straight down) to
	// 1.5 (at a glancing angle) units through the water per unit of depth: a murky water shows
	// its bottom from above and hides it from the bank. (The stock fog goes by the depth alone;
	// the unbent ray Valve tried drew a hard line along the shore.)
	const half3 vEyeVectN = normalize( i.vTangentEyeVect );
	// (x 0.8: as dense as the material says at the angle one looks at water from a bank)
	const float flPath = i.vBodyLight.z
		* lerp( 1.0f, 0.8f * rsqrt( 1.0f - ( 1.0f - vEyeVectN.z * vEyeVectN.z ) * 0.565f ), saturate( i.vBodyLight.w ) );

#if ABOVEWATER
	const float flRefractAlpha = vUnwarpedRefract.a;
#if FLOWMAP && !REFRACT
	// HL2RPM: a flowing goo without refraction (its body is opaque): the full depth of its fog
	float waterFogDepthValue = 1.0f;
#else
	float waterFogDepthValue = DecodeRefractFog( flRefractAlpha, i, flPath );
#endif
#else
	// We don't actually have valid depth values in alpha when we are underwater looking out, so 
	// just set to farthest value.
	float waterFogDepthValue = 1.0f;
#endif
	float4 reflectRefractScale = i.vReflectRefractScale;
#if !BASETEXTURE
#if ( BLURRY_REFRACT == 0 )
	reflectRefractScale *= waterFogDepthValue;
#endif
#endif

	// Compute coordinates for sampling Reflection
	float2 vReflectTexCoord;
	float2 vRefractTexCoord;

	// vectorize the dependent UV calculations (reflect = .xy, refract = .wz)
	float4 vN;
	vN.xy = vNormal.xy;
	vN.w = vNormal.x;
	vN.z = vNormal.y;
	float4 vDependentTexCoords = vN * vNormal.a * reflectRefractScale;

	vDependentTexCoords += ( i.vReflectXY_vRefractYX * ooW );
	vReflectTexCoord = vDependentTexCoords.xy;
	vRefractTexCoord = vDependentTexCoords.wz;

	HALF4 vReflectColor = tex2D( ReflectSampler, vReflectTexCoord );
#if BLURRY_REFRACT
	// Sample reflection and refraction
	float2 ddx1=float2(0.005,0);
	float2 ddy1=float2(0,0.005);
	float4 vRefractColor=float4(0,0,0,0);

#if 0
	float sumweights=0;
	for(int ix=-2;ix<=2;ix++)
	{
		for(int iy=-2;iy<=2;iy++)
		{
			float weight=1; ///(1+abs(ix)+abs(iy));
			vRefractColor += weight*tex2D( RefractSampler, vRefractTexCoord+ix*ddx1+iy*ddy1);
			sumweights+=weight;
		}
	}
#else
	// NOTE: Generated by genwaterloop.pl in the stdshaders directory.
	// Need to unroll for 360 to avoid shader compilation problems.
	// Modified genwaterloop.pl and regenerate if you need different params
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -2 * ddx1 + -2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -2 * ddx1 + -1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -2 * ddx1 + 0 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -2 * ddx1 + 1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -2 * ddx1 + 2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -1 * ddx1 + -2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -1 * ddx1 + -1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -1 * ddx1 + 0 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -1 * ddx1 + 1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + -1 * ddx1 + 2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 0 * ddx1 + -2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 0 * ddx1 + -1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 0 * ddx1 + 0 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 0 * ddx1 + 1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 0 * ddx1 + 2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 1 * ddx1 + -2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 1 * ddx1 + -1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 1 * ddx1 + 0 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 1 * ddx1 + 1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 1 * ddx1 + 2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 2 * ddx1 + -2 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 2 * ddx1 + -1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 2 * ddx1 + 0 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 2 * ddx1 + 1 * ddy1 );
	vRefractColor += tex2D( RefractSampler, vRefractTexCoord + 2 * ddx1 + 2 * ddy1 );
	float sumweights = 25;
	// NOTE: end of generated code.
#endif

	vRefractColor *= (1.0/sumweights);
	vReflectColor *= i.fReflectOverbright;
	vReflectColor *= i.vReflectTint;
	vRefractColor *= i.vRefractTint;
#	if ABOVEWATER
		// Don't mess with this in the underwater case since we don't really have
		// depth values there.
		// get the blurred depth value to be used for fog.
	waterFogDepthValue = DecodeRefractFog( vRefractColor.a, i, flPath );
#	endif
#else
	vReflectColor *= i.vReflectTint;
#if REFRACT
	HALF4 vRefractColor = tex2D( RefractSampler, vRefractTexCoord );
	// HL2RPM: the wave offset can land where nothing under the water was drawn - past the far
	// bank, above the waterline of a wall: the cleared target (black, alpha 0; every drawn pixel
	// deeper than a couple of units has a depth in its alpha). Black blotches along the banks;
	// the pixel straight below instead. (The stock renderer cleared the target to the fog color
	// of the one water it picked.)
	// (the light further down is read at the offset place all the same: making its coordinates
	// depend on this sample is one dependent texture read more than ps_2_b takes, X4018)
	if ( i.fRefractDepthEncoded > 0.5f && vRefractColor.a < ( 0.5f / 255.0f ) )
		vRefractColor = vUnwarpedRefract;
#else
	HALF4 vRefractColor = float4( 0.0f, 0.0f, 0.0f, 1.0f );
#endif
	// get the depth value from the refracted sample to be used for fog.
#	if ABOVEWATER && !( FLOWMAP && !REFRACT )
	// Don't mess with this in the underwater case since we don't really have
	// depth values there.
	waterFogDepthValue = DecodeRefractFog( vRefractColor.a, i, flPath );
#	endif
#endif

	// HL2RPM: the light the water's own color (its fog - what the water scatters back) and the
	// foam are seen in. The deferred light buffer under the pixel (where the refracted ray ends:
	// the shadow of a wall lies on the water as it lies on the bottom, moving with the waves; four
	// samples around the place - a shadow in a volume has no sharp edge) - the sun, a shadow, a
	// lamp at night - but no less than the light of the open sky plus a part of the sun's (the
	// bottom of a pool is a pit to the sky light, its surface is not; and a murky water passes
	// the sun's light on into its shaded parts), where the sky is in sight from the bottom (the
	// sky visibility of the SSAO target: under a bridge and in a sewer the water is dark). All
	// of it compared to the open daylight of the moment: $fogcolor follows the time of day by
	// itself (weather_render.cpp). The stock color is unlit - as bright in a sewer and at night
	// as in the sun. (It used to be dimmed by the brightness of what lies under the water
	// instead: over a dark or a deep bottom every water was black glass.)
	float3 vBodyLight = float3( 1.0f, 1.0f, 1.0f );
	if ( i.fRefractDepthEncoded > 0.5f )
	{
#if BLURRY_REFRACT
		// (one sample: with the 25 of the blurred refraction and the sRGB table lookups fxc gives
		// up on the four - X4018)
		float3 vLightUnder = tex2D( LightSampler, vRefractTexCoord ).rgb * i.vBodyLight.x;
#else
#if FLOWMAP && !REFRACT
		const float2 vSpread = float2( 0.012f, 0.021f );
#else
		const float2 vSpread = float2( 0.0045f, 0.008f );
#endif
		float3 vLightUnder = tex2D( LightSampler, vRefractTexCoord + vSpread ).rgb;
		vLightUnder += tex2D( LightSampler, vRefractTexCoord - vSpread ).rgb;
		vLightUnder += tex2D( LightSampler, vRefractTexCoord + float2( vSpread.x, -vSpread.y ) ).rgb;
		vLightUnder += tex2D( LightSampler, vRefractTexCoord + float2( -vSpread.x, vSpread.y ) ).rgb;
		vLightUnder *= 0.25f * i.vBodyLight.x;
#endif
		const float flSky = tex2D( SkySampler, vRefractTexCoord * i.vBodyAmbient.w ).g;
		vBodyLight = min( max( vLightUnder, i.vBodyAmbient.rgb * flSky ), i.vBodyLight.y );
	}

	half3 vEyeVect;
	vEyeVect = vEyeVectN;

	// Fresnel term
	HALF fNdotV = saturate( dot( vEyeVect, vNormal ) );
	HALF fFresnel = pow( 1.0 - fNdotV, 5 );

#if !BASETEXTURE
	// fFresnel == 1.0f means full reflection
	fFresnel *= saturate( ( waterFogDepthValue - 0.05f ) * 20.0f );
#endif


	// blend between refraction and fog color.
#if ABOVEWATER
	// HL2RPM: the stock renderer fogs what lies under the water twice - in the refraction view
	// (by the depth, into the color it writes) and here once more (less 0.05: the clear rim along
	// the shore). The deferred refraction view writes the color unfogged: both here. With this
	// blend alone a water was half as dense as the same material in the original game.
	const float flFogBlend = ( i.fRefractDepthEncoded > 0.5f )
		? 1.0f - ( 1.0f - waterFogDepthValue ) * ( 1.0f - saturate( waterFogDepthValue - 0.05f ) )
		: saturate( waterFogDepthValue - 0.05f );
	// HL2RPM: each color fades at its own rate (the kind of water, water.cpp): clear water loses the
	// red first and turns blue-green with depth, silt water loses the blue and browns; a radioactive
	// water's own color glows in the dark as well
#if FLOWMAP && !REFRACT
	// (an opaque goo: all fog - pow( 0, k ) folded by the compiler is an infinity literal, X4579)
	const float3 vFogBlend = float3( 1.0f, 1.0f, 1.0f );
#else
	const float3 vFogBlend = 1.0f - pow( saturate( 1.0f - flFogBlend ).xxx, i.vWaterKind.xyz );
#endif
	vRefractColor.rgb = lerp( vRefractColor.rgb, i.waterFogColor.rgb * LINEAR_LIGHT_SCALE * max( vBodyLight, i.vWaterKind.www ), vFogBlend );
#if FLOWMAP
	// HL2RPM: the goo's own color, carried by the flow like the normals (its own interval and scroll,
	// a little displaced by the waves), lit by the light at the place like the water's fog color
	if ( i.vFlowParams2.w > 0.5f )
	{
		const float2 vColorUv = vWorldUv * i.vFlowColorParams1.x + vNormal.xy * i.vFlowColorParams1.w;
		float flColorWeight2;
		const float4 vColorUvs = FlowUV( vColorUv, vFlow, i.vFlowParams2.x, i.vFlowColorParams1.y, flNoise, i.vFlowColorParams1.z, flColorWeight2 );
		const float4 vColor1 = tex2D( FlowColorSampler, vColorUvs.xy );
		const float4 vColor2 = tex2D( FlowColorSampler, vColorUvs.zw );
		// (the exponent sharpens the change from one layer to the other, as in Portal 2)
		const float flW2 = pow( flColorWeight2, i.vFlowColorParams2.x );
		const float flW1 = pow( 1.0f - flColorWeight2, i.vFlowColorParams2.x );
		const float4 vGoo = ( vColor1 * flW1 + vColor2 * flW2 ) / max( flW1 + flW2, 0.001f );
		vRefractColor.rgb = lerp( vRefractColor.rgb,
			vGoo.rgb * i.vFlowColorTint * LINEAR_LIGHT_SCALE * i.vFlowColorParams2.y * max( vBodyLight, i.vWaterKind.www ), i.vFlowColorParams2.z );
		// Matte surface scum/foam in alpha dampens mirror reflection; glossy liquid between reflects crisply
		fFresnel *= lerp( 1.0f, 0.35f, saturate( vGoo.a * i.vFlowColorParams2.z ) );
	}
#endif
#else
	float waterFogFactor = saturate( ( i.vProjPos.z - i.fWaterFogStart ) / i.fWaterFogEndMinusStart );
	vRefractColor = lerp( vRefractColor, i.waterFogColor * LINEAR_LIGHT_SCALE, waterFogFactor );
#endif

#if BASETEXTURE
	float4 baseSample = tex2D( BaseTextureSampler, i.vBumpTexCoord.xy );
	HALF2 bumpCoord1;
	HALF2 bumpCoord2;
	HALF2 bumpCoord3;
	ComputeBumpedLightmapCoordinates( i.lightmapTexCoord1And2, i.lightmapTexCoord3.xy,
		bumpCoord1, bumpCoord2, bumpCoord3 );

	HALF4 lightmapSample1 = tex2D( LightmapSampler, bumpCoord1 );
	HALF3 lightmapColor1 = lightmapSample1.rgb;
	HALF3 lightmapColor2 = tex2D( LightmapSampler, bumpCoord2 );
	HALF3 lightmapColor3 = tex2D( LightmapSampler, bumpCoord3 );

	float3 dp;
	dp.x = saturate( dot( vNormal, bumpBasis[0] ) );
	dp.y = saturate( dot( vNormal, bumpBasis[1] ) );
	dp.z = saturate( dot( vNormal, bumpBasis[2] ) );
	dp *= dp;

	float3 diffuseLighting = dp.x * lightmapColor1 +
		dp.y * lightmapColor2 +
		dp.z * lightmapColor3;
	float sum = dot( dp, float3( 1.0f, 1.0f, 1.0f ) );
	diffuseLighting *= LIGHT_MAP_SCALE / sum;
	HALF3 diffuseComponent = baseSample.rgb * diffuseLighting;
#endif

#if FLOWMAP
	// HL2RPM: a flowing water/goo without refraction still has its body (fog color, goo color)
	// under the reflection
	result = bReflect ? lerp( vRefractColor, vReflectColor, fFresnel ) : vRefractColor;
#else
	if( bReflect && bRefract )
	{
		result = lerp( vRefractColor, vReflectColor, fFresnel );
	}
	else if( bReflect )
	{
#if BASETEXTURE
		result = float4( diffuseComponent, 1.0f ) + vReflectColor * fFresnel * baseSample.a;
#else
		result = vReflectColor;
#endif
	}
	else if( bRefract )
	{
		result = vRefractColor;
	}
	else
	{
		result = float4( 0.0f, 0.0f, 0.0f, 0.0f );
	}
#endif

#if ABOVEWATER
	// HL2RPM: foam where the water is shallow - along the shore and around whatever stands in it
	// (with the deferred renderer the refraction alpha is the depth under the surface), broken up
	// by the waves (the slopes of the normal map, which scrolls with the water)
	// (its color is the sky and sun light of the moment, in the light under the surface here -
	// vBodyLight; it used to take the reflection tint - a register nobody sets on water without
	// a reflection: red rims around every puddle of demo_map)
	if ( i.fRefractDepthEncoded > 0.5f )
	{
		const float flDepthUnits = flRefractAlpha * flRefractAlpha * 1024.0f;
		const float flShore = saturate( 1.0f - flDepthUnits / 8.0f );
		const float flTurb = saturate( length( vNormal.xy ) * 4.0f );
		const float flFoam = flShore * saturate( flTurb + flShore * 0.5f - 0.3f );
		result.rgb = lerp( result.rgb, i.vFoamColor.rgb * saturate( dot( vBodyLight, float3( 0.3f, 0.59f, 0.11f ) ) ), flFoam * i.vFoamColor.a );
	}
#endif

	// HL2RPM: r_deferred_water_debugview (2 = the reflection's share, 3 = N.V / normal z / eye z,
	// 4 = the light of the water's own color, 5 = its fog)
	if ( i.vBodyLight.w > 1.5f )
		result.rgb = ( i.vBodyLight.w > 4.5f ) ? waterFogDepthValue : ( ( i.vBodyLight.w > 3.5f ) ? vBodyLight * 0.5f
			: ( ( i.vBodyLight.w > 2.5f ) ? float3( fNdotV, vNormal.z, vEyeVect.z ) : fFresnel ) );

#if (PIXELFOGTYPE == PIXEL_FOG_TYPE_RANGE)
	fogFactor = CalcRangeFogFactorNonFixedFunction( i.vWorldPos, i.vEyePos, i.pixelFogParams.z, i.pixelFogParams.x, i.pixelFogParams.w );
#else
	fogFactor = 0;
#endif
}

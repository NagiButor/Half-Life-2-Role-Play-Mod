//========= HL2RPM ============================================================//
//
// The sky as it is seen - for what mirrors it (hl2rpm_mirror_ps30, deferred_glass_ps30).
// The light of the sky the lighting uses (the ambient) is many times dimmer than the sky
// on the screen: procsky_atmo draws it at its own exposure and tonemaps it. A mirror
// shows the sky the eye sees: the same sky-view LUT, at that exposure, through that
// tonemap; the clouds, by their coverage, as a grey of the horizon.
//
//=============================================================================//

#ifndef HL2RPM_SKY_FXC_H
#define HL2RPM_SKY_FXC_H

// must match SkyViewUV of procsky_atmo_ps20b (DirFromSkyViewUV of sky_atmo_skyview_lutgen_ps30)
float2 HL2RPM_SkyViewUV( float3 rayDir, float3 sunDir )
{
	const float lenS = length( sunDir.xy );
	const float2 sunH = ( lenS > 1e-4 ) ? sunDir.xy / lenS : float2( 1.0, 0.0 );
	const float lenR = length( rayDir.xy );
	const float2 rayH = ( lenR > 1e-4 ) ? rayDir.xy / lenR : sunH;
	const float u = sqrt( saturate( 0.5 - 0.5 * dot( rayH, sunH ) ) );
	const float elev = asin( clamp( rayDir.z, -1.0, 1.0 ) );
	const float v = 0.5 + 0.5 * sign( elev ) * sqrt( abs( elev ) * ( 2.0 / 3.14159265 ) );
	return float2( u, v );
}

// the tonemap of procsky_atmo (extended Reinhard, white at 5)
float3 HL2RPM_SkyTonemap( float3 c )
{
	const float invW2 = 1.0 / ( 5.0 * 5.0 );
	const float l = dot( c, float3( 0.2126, 0.7152, 0.0722 ) );
	const float3 tc = c * ( 1.0 + c * invW2 ) / ( c + 1.0 );
	const float3 lc = c * ( 1.0 + l * invW2 ) / ( l + 1.0 );
	return saturate( lerp( lc, tc, saturate( tc ) ) );
}

// the sky seen in direction R (above the horizon). vecParams: x exposure of the sky, y cloud
// coverage (0..1), z how much the clouds hide (scale), w 1 = the LUT is valid
float3 HL2RPM_SkySeen( sampler sSkyView, float3 R, float3 vecLightDir, float4 vecParams )
{
	const float3 dir = normalize( float3( R.xy, max( R.z, 0.02 ) ) );
	const float3 sky = HL2RPM_SkyTonemap( tex2Dlod( sSkyView, float4( HL2RPM_SkyViewUV( dir, vecLightDir ), 0, 0 ) ).rgb * vecParams.x );
	// the clouds: a grey of the sky near the horizon, as they cover it
	const float3 horizon = HL2RPM_SkyTonemap( tex2Dlod( sSkyView, float4( HL2RPM_SkyViewUV( normalize( float3( dir.xy, 0.15 ) ), vecLightDir ), 0, 0 ) ).rgb * vecParams.x );
	const float grey = dot( horizon, float3( 0.2126, 0.7152, 0.0722 ) ) * 0.9;
	return lerp( sky, grey.xxx, saturate( vecParams.y * vecParams.z ) );
}

#endif // HL2RPM_SKY_FXC_H

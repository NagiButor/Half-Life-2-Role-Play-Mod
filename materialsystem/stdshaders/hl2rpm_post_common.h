//========= HL2RPM ============================================================//
//
// Shared helpers of the HL2RPM post-processing (hl2rpm_lum_ps30, hl2rpm_bloom_ps30,
// hl2rpm_postfx_ps30): the frame is the gamma space LDR image of the engine.
//
//=============================================================================//

#ifndef HL2RPM_POST_COMMON_H
#define HL2RPM_POST_COMMON_H

static const float3 g_vecLumWeights = float3( 0.2126, 0.7152, 0.0722 );

// (gamma 2.0 instead of 2.2: the frame goes there and back, the difference doesn't show, and
// pow() on every tap of the bloom prefilter and the combine was the largest part of their cost)
float3 PostToLinear( float3 c )
{
	return c * c;
}

float3 PostToGamma( float3 c )
{
	return sqrt( max( c, 0.0 ) );
}

// eye adaptation: the exposure for the adapted average (log) luminance of the frame.
// p: x luminance that keeps exposure 1, y how much the eye adapts (0 = not at all,
// 1 = every scene to the same average), z min, w max exposure
float PostExposure( float flAvgLogLum, float4 p )
{
	const float flAvg = exp( flAvgLogLum );
	return clamp( pow( p.x / max( flAvg, 1e-4 ), p.y ), p.z, p.w );
}

#endif // HL2RPM_POST_COMMON_H

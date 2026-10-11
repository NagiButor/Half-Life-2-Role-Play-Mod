// HL2RPM: parallax occlusion mapping - the relief of a brush material.
//
// A bump map only bends the light: seen at an angle a cobbled street stays a flat photo. Here
// the texture coordinate itself is moved to where the view ray meets the relief: the stones
// overlap the gaps behind them, the mortar sinks between the bricks. The relief is a height map
// the client makes of the material's bump map when it is loaded (deferred_pom.cpp).
//
// The G-buffer pass (normals) and the composite pass (albedo) both run this with the same
// inputs - the texture coordinate and the view vector in tangent space out of their vertex
// shaders - and so get the same shifted coordinate.
//
// Everything sampled with the result has to give its gradients (tex2Dgrad): they come out of a
// loop that ends where the ray hits.
#ifndef HL2RPM_POM_FXC_H
#define HL2RPM_POM_FXC_H

#define POM_STEPS 14

// vecViewTS: from the surface to the eye, in tangent space - x along +u, y along +v, z out of
// the surface. flScale: the depth of the relief, in texture repeats.
float2 ParallaxOcclusionUV( sampler sHeight, float2 uv, float3 vecViewTS, float flScale )
{
	const float3 v = normalize( vecViewTS );
	// the shift over the whole depth of the relief, top (height 1) to floor (0); at a glancing
	// angle it is held back (the ray would run across many repeats of the texture)
	const float2 vecShift = -v.xy / max( v.z, 0.28f ) * flScale;
	const float flStep = 1.0f / POM_STEPS;

	float flRay = 1.0f;
	float flHeight = tex2Dlod( sHeight, float4( uv, 0, 0 ) ).r;
	float flPrevRay = 1.0f;
	float flPrevHeight = flHeight;
	[loop]
	for ( int i = 0; i < POM_STEPS; i++ )
	{
		if ( flHeight >= flRay )
			break;
		flPrevRay = flRay;
		flPrevHeight = flHeight;
		flRay -= flStep;
		flHeight = tex2Dlod( sHeight, float4( uv + vecShift * ( 1.0f - flRay ), 0, 0 ) ).r;
	}

	// where the ray went into the relief between the last two samples
	const float flAbove = flPrevRay - flPrevHeight;
	const float flBelow = flHeight - flRay;
	const float flCross = flRay + ( flPrevRay - flRay ) * saturate( flBelow / max( flAbove + flBelow, 1e-5f ) );
	return uv + vecShift * ( 1.0f - flCross );
}

// vecParams: x the depth of the relief (texture repeats; 0 = none), y, z its fade with the
// distance: 1 at z - distance * y >= 1, gone at 0
float2 ParallaxOcclusionUV( sampler sHeight, float2 uv, float3 vecViewTS, float4 vecParams )
{
	const float flScale = vecParams.x * saturate( vecParams.z - length( vecViewTS ) * vecParams.y );
	[branch]
	if ( flScale > 0.0f )
		uv = ParallaxOcclusionUV( sHeight, uv, vecViewTS, flScale );
	return uv;
}

#endif // HL2RPM_POM_FXC_H

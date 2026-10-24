// HL2RPM: parallax occlusion mapping - the relief of a brush material.
//
// A bump map only bends the light: seen at an angle a cobbled street stays a flat photo. Here
// the texture coordinate itself is moved to where the view ray meets the relief: the stones
// overlap the gaps behind them, the mortar sinks between the bricks. The relief is a height map
// the client makes when the material is loaded (deferred_pom.cpp) - of its bump map, or of the
// texture itself when it has none.
//
// The G-buffer pass (normals) and the composite pass (albedo) both run this with the same
// inputs - the texture coordinate and the view vector in tangent space out of their vertex
// shaders - and so get the same shifted coordinate.
//
// Everything sampled with the result has to give its gradients (tex2Dgrad): they come out of a
// loop that ends where the ray hits.
#ifndef HL2RPM_POM_FXC_H
#define HL2RPM_POM_FXC_H

// the most steps of the march (at a glancing angle); head on a few do
#define POM_MAX_STEPS 28
#define POM_MIN_STEPS 8

// vecViewTS: from the surface to the eye, in tangent space - x along +u, y along +v, z out of
// the surface. flScale: the depth of the relief, in texture repeats. flHit: the height where the
// ray meets the relief (1 = the top: nothing in front of it).
float2 ParallaxOcclusionUV( sampler sHeight, float2 uv, float3 vecViewTS, float flScale, out float flHit )
{
	const float3 v = normalize( vecViewTS );
	// the shift over the whole depth of the relief, top (height 1) to floor (0); at a glancing
	// angle it is held back (the ray would run across many repeats of the texture), and almost
	// along the surface the relief is pressed flat: a ray nearly parallel to it drags the texture
	// over every step of the relief into streaks - and the eye reads no depth there anyway
	// (full up to 70 degrees off the normal, none from 84)
	const float flGlance = saturate( ( v.z - 0.1f ) * ( 1.0f / 0.24f ) );
	const float2 vecShift = -v.xy / max( v.z, 0.28f ) * ( flScale * flGlance );
	// (a glancing ray crosses many texels of the height map per step: more steps for it - the
	// walls of the bricks were stairs of stretched texels, "fur")
	const float flSteps = floor( lerp( (float)POM_MAX_STEPS, (float)POM_MIN_STEPS, saturate( v.z ) ) );
	const float flStep = 1.0f / flSteps;

	float flRay = 1.0f;
	float flHeight = tex2Dlod( sHeight, float4( uv, 0, 0 ) ).r;
	float flPrevRay = 1.0f;
	float flPrevHeight = flHeight;
	[loop]
	for ( int i = 0; i < POM_MAX_STEPS; i++ )
	{
		if ( flHeight >= flRay || i >= (int)flSteps )
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
	flHit = saturate( flCross );
	return uv + vecShift * ( 1.0f - flCross );
}

// vecParams: x the depth of the relief (texture repeats; 0 = none), y, z its fade with the
// distance: 1 at z - distance * y >= 1, gone at 0. flHit: as above (1 without a relief; it fades
// with the relief).
float2 ParallaxOcclusionUV( sampler sHeight, float2 uv, float3 vecViewTS, float4 vecParams, out float flHit )
{
	flHit = 1.0f;
	const float flFade = saturate( vecParams.z - length( vecViewTS ) * vecParams.y );
	const float flScale = vecParams.x * flFade;
	[branch]
	if ( flScale > 0.0f )
	{
		uv = ParallaxOcclusionUV( sHeight, uv, vecViewTS, flScale, flHit );
		flHit = lerp( 1.0f, flHit, flFade );
	}
	return uv;
}

// A blend of two materials (WorldVertexTransition - the ground of the maps): the relief is the
// blend of their two heights by the vertex alpha, the second on its own coordinates (its
// texture's $basetexturetransform2: vecTransform2 - the rows of an affine map of the first's).
// The same march; returns the first layer's coordinate.
float2 ParallaxOcclusionUVBlend( sampler sHeight, sampler sHeight2, float flBlend, const float4 vecTransform2[2],
	float2 uv, float3 vecViewTS, float4 vecParams, out float flHit )
{
	flHit = 1.0f;
	const float flFade = saturate( vecParams.z - length( vecViewTS ) * vecParams.y );
	const float flScale = vecParams.x * flFade;
	[branch]
	if ( flScale > 0.0f )
	{
		const float3 v = normalize( vecViewTS );
		const float flGlance = saturate( ( v.z - 0.1f ) * ( 1.0f / 0.24f ) );
		const float2 vecShift = -v.xy / max( v.z, 0.28f ) * ( flScale * flGlance );
		// (the second layer: the same shift through the linear part of its transform)
		const float2 uv2 = float2( dot( uv, vecTransform2[0].xy ) + vecTransform2[0].w, dot( uv, vecTransform2[1].xy ) + vecTransform2[1].w );
		const float2 vecShift2 = float2( dot( vecShift, vecTransform2[0].xy ), dot( vecShift, vecTransform2[1].xy ) );
		const float flSteps = floor( lerp( (float)POM_MAX_STEPS, (float)POM_MIN_STEPS, saturate( v.z ) ) );
		const float flStep = 1.0f / flSteps;

		float flRay = 1.0f;
		float flHeight = lerp( tex2Dlod( sHeight, float4( uv, 0, 0 ) ).r, tex2Dlod( sHeight2, float4( uv2, 0, 0 ) ).r, flBlend );
		float flPrevRay = 1.0f;
		float flPrevHeight = flHeight;
		[loop]
		for ( int i = 0; i < POM_MAX_STEPS; i++ )
		{
			if ( flHeight >= flRay || i >= (int)flSteps )
				break;
			flPrevRay = flRay;
			flPrevHeight = flHeight;
			flRay -= flStep;
			const float t = 1.0f - flRay;
			flHeight = lerp( tex2Dlod( sHeight, float4( uv + vecShift * t, 0, 0 ) ).r,
				tex2Dlod( sHeight2, float4( uv2 + vecShift2 * t, 0, 0 ) ).r, flBlend );
		}

		const float flAbove = flPrevRay - flPrevHeight;
		const float flBelow = flHeight - flRay;
		const float flCross = flRay + ( flPrevRay - flRay ) * saturate( flBelow / max( flAbove + flBelow, 1e-5f ) );
		flHit = lerp( 1.0f, saturate( flCross ), flFade );
		uv += vecShift * ( 1.0f - flCross );
	}
	return uv;
}

// the coordinate of the second layer of a blend for the first layer's
float2 BlendLayer2UV( float2 uv, const float4 vecTransform2[2] )
{
	return float2( dot( uv, vecTransform2[0].xy ) + vecTransform2[0].w, dot( uv, vecTransform2[1].xy ) + vecTransform2[1].w );
}

// ...and its gradient (the linear part only)
float2 BlendLayer2Grad( float2 vecGrad, const float4 vecTransform2[2] )
{
	return float2( dot( vecGrad, vecTransform2[0].xy ), dot( vecGrad, vecTransform2[1].xy ) );
}

#endif // HL2RPM_POM_FXC_H

// HL2RPM: trees and bushes in the wind (the vertex shaders of the deferred model passes).
//
// A static prop is a rigid mesh: every tree stood frozen in a storm that bent the rain. The
// vertices are moved in the model's own space, by their height over its origin (the foot of
// the trunk): the trunk leans with the gusts - not at all at the ground, most at the top - and
// what carries leaves ($treesway 2) shivers on top of that. The G-buffer, shadow and composite
// passes all include this with the same constants: the three must agree to the last bit (the
// composite tests its depth against the G-buffer pass's).
#ifndef HL2RPM_SWAY_FXC_H
#define HL2RPM_SWAY_FXC_H

// vecWind:   xy the wind (world space; its length the strength, 0 .. ~1), z time (s), w leaf flutter (units)
// vecParams: x the lean of the trunk (units at 100 units of height in a wind of 1), y the calm part
//            (the lean with no wind at all), z how fast it swings, w unused
float3 TreeSway( float3 vecPos, float4x3 matModel, float4 vecWind, float4 vecParams )
{
	// the wind in the model's space (the rotation of the model's matrix is orthonormal)
	const float3 vecWindWorld = float3( vecWind.xy, 0.0f );
	const float3 vecWindModel = float3( dot( matModel[0], vecWindWorld ), dot( matModel[1], vecWindWorld ), dot( matModel[2], vecWindWorld ) );
	const float flStrength = length( vecWind.xy );
	const float3 vecDir = vecWindModel / max( flStrength, 1e-4f );

	// every tree out of step with its neighbours
	const float flPhase = dot( matModel[3], float3( 0.0113f, 0.0171f, 0.0057f ) ) * 6.2831853f;
	const float flTime = vecWind.z * vecParams.z;

	// the trunk: bent like a beam held at the foot
	const float flHeight = max( vecPos.z, 0.0f ) * 0.01f;
	const float flBend = flHeight * sqrt( flHeight );
	// (leaning with the wind, swinging about that lean; a gust every few seconds)
	const float flSwing = 0.55f + 0.45f * sin( flTime * 0.83f + flPhase ) + 0.3f * sin( flTime * 2.17f + flPhase * 1.7f );
	const float flAmount = ( vecParams.y + flStrength ) * vecParams.x * flBend;
	float3 vecOffset = vecDir * ( flAmount * flSwing );
	// (and a little across the wind)
	vecOffset += float3( -vecDir.y, vecDir.x, 0.0f ) * ( flAmount * 0.25f * sin( flTime * 1.31f + flPhase * 2.3f ) );

	// the leaves: a quick shiver, each vertex its own
	const float flLeaf = vecWind.w * ( 0.35f + flStrength ) * saturate( flHeight * 2.0f );
	const float flSeed = dot( vecPos, float3( 0.37f, 0.23f, 0.31f ) ) + flPhase;
	vecOffset += float3( sin( flTime * 4.1f + flSeed ), sin( flTime * 3.3f + flSeed * 1.3f ), sin( flTime * 5.3f + flSeed * 0.7f ) * 0.6f ) * flLeaf;

	return vecPos + vecOffset;
}

#endif // HL2RPM_SWAY_FXC_H

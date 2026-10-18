// HL2RPM: trees and bushes in the wind (the vertex shaders of the deferred model passes).
//
// A static prop is a rigid mesh: every tree stood frozen in a storm that bent the rain. The
// vertices are moved in the model's own space. The trunk hardly moves: only in a strong wind its
// top leans a little (a beam held at the foot: by the height over the model's origin). What moves
// are the branches - the more the farther they reach out from the trunk (its axis is the model's
// z axis): at the trunk not at all, the twigs at their ends most, each branch in a rhythm of its
// own (by its direction around the trunk and its height: the cards of one branch move together).
// The leaves at the ends shiver on top of that. The G-buffer, shadow and composite passes all
// include this with the same constants: the three must agree to the last bit (the composite tests
// its depth against the G-buffer pass's).
#ifndef HL2RPM_SWAY_FXC_H
#define HL2RPM_SWAY_FXC_H

// vecWind:   xy the wind (world space; its length the strength, 0 .. ~1), z time (s), w leaf
//            flutter (units; 0 = a trunk material: only its lean)
// vecParams: x the lean of the trunk (units at 100 units of height in a wind of 1), y the calm part
//            (the wind on a day without any), z how fast it swings, w how far the branches swing
//            (units at 100 units out from the trunk in a wind of 1; 0 = a trunk material)
float3 TreeSway( float3 vecPos, float4x3 matModel, float4 vecWind, float4 vecParams )
{
	// the wind in the model's space (the rotation of the model's matrix is orthonormal)
	const float3 vecWindWorld = float3( vecWind.xy, 0.0f );
	const float3 vecWindModel = float3( dot( matModel[0], vecWindWorld ), dot( matModel[1], vecWindWorld ), dot( matModel[2], vecWindWorld ) );
	const float flStrength = length( vecWind.xy );
	const float3 vecDir = vecWindModel / max( flStrength, 1e-4f );
	const float flWind = vecParams.y + flStrength;

	// every tree out of step with its neighbours
	const float flPhase = dot( matModel[3], float3( 0.0113f, 0.0171f, 0.0057f ) ) * 6.2831853f;
	const float flTime = vecWind.z * vecParams.z;

	// the trunk: leaning with the wind, swinging slowly about that lean
	const float flHeight = max( vecPos.z, 0.0f ) * 0.01f;
	const float flBend = flHeight * sqrt( flHeight );
	const float flSwing = 0.55f + 0.45f * sin( flTime * 0.83f + flPhase ) + 0.3f * sin( flTime * 2.17f + flPhase * 1.7f );
	float3 vecOffset = vecDir * ( flWind * vecParams.x * flBend * flSwing );

	// the branches
	const float flReach = length( vecPos.xy );
	const float flOut = saturate( ( flReach - 16.0f ) * ( 1.0f / 160.0f ) );
	// (by the direction out from the trunk - a smooth function of it all the way round: an angle
	// jumps where it wraps, and the cards of a branch across that line tore apart)
	const float2 vecOutDir = vecPos.xy / max( flReach, 1.0f );
	const float flBranchPhase = dot( vecOutDir, float2( 2.7f, 4.1f ) ) + vecPos.z * 0.013f + flPhase;
	const float flGust = 0.6f * sin( flTime * 1.37f + flBranchPhase ) + 0.4f * sin( flTime * 2.71f + flBranchPhase * 1.3f );
	const float flBranch = vecParams.w * flWind * flOut * flReach * 0.01f;
	// (pushed along with the wind and back, and bobbing up and down)
	vecOffset += vecDir * ( flBranch * ( 0.5f + 0.5f * flGust ) );
	vecOffset.z += flBranch * 0.35f * flGust;

	// the leaves: a quick shiver at the ends of the branches, each vertex its own
	const float flLeaf = vecWind.w * ( 0.35f + flStrength ) * flOut;
	const float flSeed = dot( vecPos, float3( 0.37f, 0.23f, 0.31f ) ) + flPhase;
	vecOffset += float3( sin( flTime * 4.1f + flSeed ), sin( flTime * 3.3f + flSeed * 1.3f ), sin( flTime * 5.3f + flSeed * 0.7f ) * 0.6f ) * flLeaf;

	return vecPos + vecOffset;
}

#endif // HL2RPM_SWAY_FXC_H

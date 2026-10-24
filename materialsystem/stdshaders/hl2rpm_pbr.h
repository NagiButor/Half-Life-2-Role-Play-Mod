// HL2RPM: physically based materials for the deferred renderer.
//
// A stock material says next to nothing about how its surface reflects light: a diffuse
// texture, at best a bump map and a baked cubemap. The deferred shaders give every opaque
// material a roughness (how wide the mirror reflection of a light is: the gloss of the
// G-buffer), a metalness (how much of the light it mirrors, and whether in the color of
// the surface) and a specular level (the mirror reflection of a non-metal, 1 = the 4 % of
// most of them). A material says them itself
//
//		$pbr_roughness	0 (a mirror) .. 1 (matte)
//		$pbr_roughness2	the same for $basetexture2 of a blend
//		$pbr_metalness	0 .. 1
//		$pbr_specular	0 .. 4
//		$pbr_glossmask	1 = the alpha of the bump map scales the gloss (0 = it doesn't)
//
// or, where it doesn't, they are made of what it is: its $surfaceprop, the folder and the
// name of the material (concrete/, metal/, wood/ ...), what its author said about its
// reflections ($envmap and its tint, $normalmapalphaenvmapmask, $phong, $phongexponent).
#ifndef HL2RPM_PBR_H
#define HL2RPM_PBR_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialVar;

struct pbrParms_t
{
	pbrParms_t()
	{
		iRoughness = iRoughness2 = iMetalness = iSpecular = iGlossMask = -1;
		iGloss = iGloss2 = -1;
		iSurfaceProp = iBaseTexture2 = iBumpmap = -1;
		iEnvmap = iEnvmapTint = iNormalAlphaEnvmapMask = iBaseAlphaEnvmapMask = -1;
		iPhong = iPhongExponent = iPhongBoost = iBaseAlphaPhongMask = -1;
		bModel = false;
	}

	// what the material may say
	int iRoughness, iRoughness2, iMetalness, iSpecular, iGlossMask;
	// what the passes read: the gloss of the G-buffer (1 - roughness; $phong_exp of before)
	int iGloss, iGloss2;
	// what the stock material says
	int iSurfaceProp, iBaseTexture2, iBumpmap;
	int iEnvmap, iEnvmapTint, iNormalAlphaEnvmapMask, iBaseAlphaEnvmapMask;
	int iPhong, iPhongExponent, iPhongBoost, iBaseAlphaPhongMask;

	bool bModel;
};

// SHADER_INIT_PARAMS: fills in whatever of roughness / metalness / specular / gloss mask the
// material leaves open, and the gloss the G-buffer pass writes
void InitParmsPBR( const pbrParms_t &info, IMaterialVar **params, const char *pszMaterialName );

// dev: r_deferred_pbr_list <substring> (prints what materials loaded after it was set get)
bool PBR_ShouldReport( const char *pszMaterialName );

//-----------------------------------------------------------------------------
// The relief of a brush material (parallax occlusion mapping, hl2rpm_pom_fxc.h):
//		$pom_heightmap	its height map (the client makes one of the bump map of a stock material)
//		$pom_scale		its depth, in repeats of the texture (not set = by what the material is)
//-----------------------------------------------------------------------------
float PBR_GetReliefDepth( const char *pszMaterialName );
// the constant of the passes: x the depth (0 = none - r_deferred_pom, the 3D skybox, a reflection
// view), y, z the fade with the distance
void PBR_GetReliefConstants( float flMaterialDepth, bool bReflectionView, float *pOut4 );

//-----------------------------------------------------------------------------
// Trees and bushes in the wind (hl2rpm_sway_fxc.h): $treesway 1 = the trunk leans, 2 = and the
// leaves shiver. Sets the two vertex shader constants of the model passes.
//-----------------------------------------------------------------------------
class IShaderDynamicAPI;
bool PBR_TreeSwayEnabled();
void PBR_SetTreeSwayConstants( IShaderDynamicAPI *pShaderAPI, int iMode );

//-----------------------------------------------------------------------------
// Thin things the light shines through (leaves, grass, cloth): $pbr_transmission 0 .. 1.
// The two constants of the composite pass.
//-----------------------------------------------------------------------------
void PBR_GetTransmissionConstants( float flMaterial, float *pTransmit4, float *pSunDir4 );

// HL2RPM: the sun light that comes through the leaves of a crown into its shade (the sun pass,
// lightingpass_global_ps30 c80): x 1 / the depth in the crown over which it halves, y the most of it
void PBR_GetFoliageLightConstants( float *pOut4 );

// HL2RPM: the shading of the bottom of a relief (composite c32.w)
float PBR_GetReliefOcclusion();

// HL2RPM: the two rows (x, y, -, translation) of a texture transform parameter for two shader
// constants - $basetexturetransform2 of a blend; the identity for -1 or a parameter not set
void PBR_GetTextureTransformRows( IMaterialVar **params, int iVar, float *pOut8 );

//-----------------------------------------------------------------------------
// The sky as it is seen, for what mirrors it (hl2rpm_sky_fxc.h): xyz the light the sky is lit by;
// x the exposure of the procedural sky, y cloud coverage, z how much the clouds hide. False = no
// global light (no sky-view LUT is built).
//-----------------------------------------------------------------------------
bool HL2RPM_GetSkySeenConstants( float *pLightDir4, float *pParams4 );

#endif // HL2RPM_PBR_H

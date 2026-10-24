//========= HL2RPM ============================================================//
//
// Purpose: Height maps for parallax occlusion mapping, made of the bump maps - or of the
//          texture itself (see deferred_pom.cpp).
//
//=============================================================================//

#ifndef DEFERRED_POM_H
#define DEFERRED_POM_H
#ifdef _WIN32
#pragma once
#endif

// The name of the height map texture made of a bump map ("brick/brickwall001a_normal"), for
// $pom_heightmap. NULL = it has no relief to speak of, or can't be read.
const char *DeferredPOM_GetHeightTexture( const char *pszBumpName, bool bSSBump );

// what scripts/hl2rpm_relief.txt says about a material
enum
{
	RELIEF_RULE_AUTO = 0,	// (not named) the relief is guessed
	RELIEF_RULE_INVERT,		// "invert": light joints - what is lighter sinks
	RELIEF_RULE_NONE,		// "none": no relief made of the texture (no bump map, stays flat)
	RELIEF_RULE_KEEP,		// "keep": what is lighter stands out, no guessing
};
int DeferredPOM_GetReliefRule( const char *pszMaterial );

// The relief of a material without a bump map, made of its base texture: a height map
// ($pom_heightmap) and a normal map of it ($bumpmap). False = none (flat, unreadable, rule "none").
bool DeferredPOM_GetTextureRelief( const char *pszBaseTexture, int iRule, const char **ppszHeight, const char **ppszNormal );

// A flat relief - the top, facing straight out: the layer of a blend that has no relief of its own
// next to one that has ($pom_heightmap / $bumpmap of that layer).
bool DeferredPOM_GetFlatRelief( const char **ppszHeight, const char **ppszNormal );

// (the textures and their regenerators; before the material system goes)
void DeferredPOM_Shutdown();

#endif // DEFERRED_POM_H

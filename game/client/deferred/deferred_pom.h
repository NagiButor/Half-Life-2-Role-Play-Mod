//========= HL2RPM ============================================================//
//
// Purpose: Height maps for parallax occlusion mapping, made of the bump maps
//          (see deferred_pom.cpp).
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

// (the textures and their regenerators; before the material system goes)
void DeferredPOM_Shutdown();

#endif // DEFERRED_POM_H

//========= HL2RPM ============================================================//
//
// Purpose: Occlusion culling against the walls of the map (see deferred_occlusion.cpp).
//
//=============================================================================//

#ifndef DEFERRED_OCCLUSION_H
#define DEFERRED_OCCLUSION_H
#ifdef _WIN32
#pragma once
#endif

class CViewSetup;

// Draws the occlusion buffer of this frame for the view (once a frame: the first call wins)
void DeferredOcclusion_Setup( const CViewSetup &view );

// The buffer of this frame was drawn for exactly this camera
bool DeferredOcclusion_IsForView( const Vector &vecOrigin, const QAngle &angles );

// True = nothing of the world space box can be seen: it is behind the walls everywhere
bool DeferredOcclusion_IsBoxHidden( const Vector &vecMins, const Vector &vecMaxs );

// The renderables being collected are those of the view the buffer was drawn for (the client
// leaf system asks: it collects for the shadow views, the skybox and the reflections as well)
void DeferredOcclusion_SetCollecting( bool bMainView );
bool DeferredOcclusion_IsCollecting();

#endif // DEFERRED_OCCLUSION_H

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

// Around the engine building the world lists of the view the buffer was drawn for: the leaves
// of the map behind the walls lose their visibility mark (Hide), and get it back (Restore -
// always called after Hide, right after the lists are built)
void DeferredOcclusion_HideWorldLeaves( const Vector &vecOrigin, const QAngle &angles );
void DeferredOcclusion_RestoreWorldLeaves();

// True = the part of the box within each of the leaves (the whole list of leaves the box
// touches) is behind the walls or out of the visibility data's view: a lamp lights nothing seen
bool DeferredOcclusion_AreLeafPartsHidden( const int *pLeaves, int nLeaves, const Vector &vecMins, const Vector &vecMaxs );

#endif // DEFERRED_OCCLUSION_H

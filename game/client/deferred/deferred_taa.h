//========= HL2RPM ============================================================//
//
// Purpose: Temporal anti-aliasing for the deferred renderer.
//
//=============================================================================//

#ifndef DEFERRED_TAA_H
#define DEFERRED_TAA_H
#pragma once

class CViewSetup;

void InitTAARTs();			// from InitDeferredRTs
void DeferredTAA_Shutdown();
void DeferredTAA_Reset();	// new map, teleport: no history

// anti-aliasing of the final frame: 0 none, 1 FXAA, 2 TAA
int DeferredAA_GetMode();

// once, at startup: the engine's MSAA does nothing for the deferred renderer but cost time
void DeferredAA_DisableMSAA();

// start of the main view: shifts its projection by this frame's sub-pixel jitter
// (only while TAA runs). Returns whether the view was jittered.
bool DeferredTAA_JitterView( CViewSetup &view, bool bMainView );

// whether this frame is drawn for TAA (jittered)
bool DeferredTAA_IsFrameActive();

// around drawing what moves with the camera (the first person weapon and body):
// those pixels are marked in the stencil and not reprojected
void DeferredTAA_MarkCameraAttached( bool bBegin );

// in place of FXAA: blends the frame into the history
void DeferredTAA_Draw( const CViewSetup &view );

#endif // DEFERRED_TAA_H

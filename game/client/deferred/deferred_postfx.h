//========= HL2RPM ============================================================//
//
// Purpose: Post-processing of the final frame for the deferred renderer: eye
//          adaptation, bloom, grading, lens effects.
//
//=============================================================================//

#ifndef DEFERRED_POSTFX_H
#define DEFERRED_POSTFX_H
#pragma once

class CViewSetup;

void InitPostFXRTs();		// from InitDeferredRTs
void DeferredPostFX_Shutdown();	// the client goes away: the procedural texture must not keep its regenerator

// new map: the eye starts adapted to the first frame
void DeferredPostFX_Reset();

// depth of field, before the anti-aliasing (TAA evens out its noise)
void DeferredPostFX_DrawDoF( const CViewSetup &view );

// after the anti-aliasing, before the HUD
void DeferredPostFX_Draw( const CViewSetup &view );

// the engine's own HDR exposure / bloom (HDR maps) stay out of the way
bool DeferredPostFX_OwnsExposure();
bool DeferredPostFX_OwnsBloom();

#endif // DEFERRED_POSTFX_H

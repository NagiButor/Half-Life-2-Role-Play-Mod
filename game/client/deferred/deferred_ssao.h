//========= HL2RPM ============================================================//
//
// Purpose: Ambient occlusion (SSAO + sky visibility) for the deferred global light.
//
//=============================================================================//

#ifndef DEFERRED_SSAO_H
#define DEFERRED_SSAO_H
#pragma once

class CViewSetup;

void InitSSAORTs();			// from InitDeferredRTs
void ShutdownSSAO();

// before the lighting passes, after the G-buffer
void DeferredSSAO_Render( const CViewSetup &view );

// the sky visibility needs the weather's top-down occlusion map
bool DeferredSSAO_WantsSkyVisibility();

#endif // DEFERRED_SSAO_H

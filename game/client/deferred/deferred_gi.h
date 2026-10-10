//========= HL2RPM ============================================================//
//
// Purpose: World-space indirect light of the global light (GI probes).
//
//=============================================================================//

#ifndef DEFERRED_GI_H
#define DEFERRED_GI_H
#pragma once

class CViewSetup;
struct lightData_Global_t;

void InitGIRTs();			// from InitDeferredRTs
void ShutdownGI();

// new map: every probe is traced again
void DeferredGI_Reset();
// the render targets lost their content: upload the probes again
void DeferredGI_OnRTContentLost();

// before the lighting passes, after the G-buffer: traces probes (CPU budget), uploads
// the changed ones, renders the half resolution indirect light
void DeferredGI_Render( const CViewSetup &view, const lightData_Global_t &globalLight );

#endif // DEFERRED_GI_H

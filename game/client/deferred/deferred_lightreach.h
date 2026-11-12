//========= HL2RPM ============================================================//
//
// Purpose: What of the world each point light reaches ("light reach maps"), see the .cpp.
//
//=============================================================================//

#ifndef DEFERRED_LIGHTREACH_H
#define DEFERRED_LIGHTREACH_H
#pragma once

struct def_light_t;

// The atlas the maps are drawn into (deferred_rt.cpp, with the other render targets).
void InitLightReachRTs();

// Once a frame, after the lights are culled: traces / uploads the maps of the point lights around
// the camera, gives each light its shader parameter (def_light_t::flReachParam) and the box of what
// it reaches (the leaves it is culled by).
void DeferredLightReach_Update( const Vector &vecViewOrigin, CUtlVector< def_light_t* > &lights );

// Is the reach map of this light in use already (a new bounce light waits for it before it shows)?
// True as well when it will never get one (reach maps off, the light sits in a wall, no room).
bool DeferredLightReach_IsReady( const def_light_t *l );

// The render targets came back empty (device reset): every map is uploaded again.
void DeferredLightReach_OnRTContentLost();

#endif // DEFERRED_LIGHTREACH_H

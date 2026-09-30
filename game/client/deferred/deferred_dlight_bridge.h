//========= HL2RPM ============================================================//
//
// Purpose: Engine dynamic lights (dlights / elights) -> deferred lights.
//
//=============================================================================//

#ifndef DEFERRED_DLIGHT_BRIDGE_H
#define DEFERRED_DLIGHT_BRIDGE_H
#pragma once

class IVEfx;

// Wraps the engine's effects interface (CHLClient::Init) to learn elight keys.
IVEfx *DeferredDLights_WrapEffects( IVEfx *pEngineEffects );

// Mirrors the active engine dynamic lights as deferred lights (once per frame,
// before the deferred lights are prepared).
void DeferredDLights_Update( const Vector &vecViewOrigin );

// Removes every mirrored light (level shutdown).
void DeferredDLights_Clear();

#endif // DEFERRED_DLIGHT_BRIDGE_H

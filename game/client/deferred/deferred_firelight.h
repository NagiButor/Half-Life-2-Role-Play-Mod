//========= HL2RPM ============================================================//
//
// Purpose: Fire and explosions light the deferred scene (deferred_firelight.cpp).
//
//=============================================================================//

#ifndef DEFERRED_FIRELIGHT_H
#define DEFERRED_FIRELIGHT_H
#pragma once

// The fires and explosions of this frame become deferred lights (once per frame, before the
// deferred lights are prepared).
void DeferredFireLights_Update( const Vector &vecViewOrigin );

// A flash of an explosion that is no particle system (te_explosion: env_explosion, grenades,
// barrels, rockets): flScale ~ 1 for a grenade.
void DeferredFireLights_AddFlash( const Vector &vecOrigin, float flScale );

// Removes every fire light (level shutdown).
void DeferredFireLights_Clear();

// Is the deferred renderer lighting fire and explosions itself (the engine dlights of the
// same thing would double them)?
bool DeferredFireLights_Enabled();

struct def_light_t;
bool DeferredFireLights_IsFireLight( const def_light_t *l );

#endif // DEFERRED_FIRELIGHT_H

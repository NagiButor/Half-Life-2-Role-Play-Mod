//========= HL2RPM ============================================================//
//
// Purpose: One-bounce light of the lamps (virtual point lights), see the .cpp.
//
//=============================================================================//

#ifndef DEFERRED_BOUNCE_H
#define DEFERRED_BOUNCE_H
#pragma once

struct def_light_t;

// Places / updates the bounce lights of the lamps around the camera (once per
// frame, before the deferred lights are prepared).
void DeferredBounce_Update( const Vector &vecViewOrigin );

// Removes every bounce light (level shutdown).
void DeferredBounce_Clear();

// Is this one of the bounce lights (not a real lamp)?
bool DeferredBounce_IsBounceLight( const def_light_t *l );

#endif // DEFERRED_BOUNCE_H

//========= HL2RPM ============================================================//
//
// Shadow maps of spot lamps kept between frames (deferred_shadowcache.cpp).
//
// A lamp's shadow map was drawn every frame - the casters of its cone collected, the map cleared
// and drawn again - though for a lamp of the map nothing in it changes for minutes: 10 shadowed
// spots of rpm_trainstation_testoptimization cost ~8 ms of the main thread and ~3.7 ms of the GPU
// a frame. Now a shadowed spot gets a map of its own from a pool (r_deferred_shadow_cache_spots,
// render targets made with the others in deferred_rt.cpp). Its view still collects the casters
// in its cone every frame (cheap: the leaves of a still view are kept); the map is drawn again
// only if the lamp changed or what the casters are (the set of renderables, their position,
// angles, model, animation: sequence, cycle, pose parameters, skin, body, alpha) differs from
// what was drawn. NPCs, players, ragdolls and swaying trees ($treesway) in the cone are drawn
// every frame. The light pass reads the lamp's own map (CommitTexture_ProjectedDepth on its slot).
//
//=============================================================================//

#ifndef DEFERRED_SHADOWCACHE_H
#define DEFERRED_SHADOWCACHE_H
#pragma once

struct def_light_t;
class CClientRenderablesList;
class ITexture;

// The cache entry of this lamp for the frame (-1: no cache - the shared slot map, as before)
int DeferredShadowCache_Acquire( def_light_t *pLight );
ITexture *DeferredShadowCache_GetTexture( int iEntry );

// After the view collected its casters: true = the entry's map still shows them (no drawing);
// false = draw it now (the entry then remembers what is drawn).
bool DeferredShadowCache_IsCurrent( int iEntry, def_light_t *pLight, int iResolution, const CClientRenderablesList *pList );

// Everything drawn so far is stale (map change, render targets re-made or their content lost,
// settings of the shadow passes changed)
void DeferredShadowCache_InvalidateAll();

#endif // DEFERRED_SHADOWCACHE_H

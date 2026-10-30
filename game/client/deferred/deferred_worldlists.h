//========= HL2RPM ============================================================//
//
// Purpose: The engine's world lists put in the order of its static meshes
// (see deferred_worldlists.cpp).
//
//=============================================================================//

#ifndef DEFERRED_WORLDLISTS_H
#define DEFERRED_WORLDLISTS_H
#ifdef _WIN32
#pragma once
#endif

class IWorldRenderList;

// Right after render->BuildWorldLists: the opaque material groups of the list are put in the
// order of the engine's static meshes - the engine draws the same, without going over its limit
// of 127 mesh changes a batch ("Too many vertex format changes in frame")
void DeferredWorldLists_SortByMesh( IWorldRenderList *pList );

#endif // DEFERRED_WORLDLISTS_H

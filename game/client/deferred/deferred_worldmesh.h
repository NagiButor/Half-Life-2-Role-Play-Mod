//========= HL2RPM ============================================================//
//
// Purpose: The map's faces as static meshes of our own, for the shadow views
//          (see deferred_worldmesh.cpp).
//
//=============================================================================//

#ifndef DEFERRED_WORLDMESH_H
#define DEFERRED_WORLDMESH_H
#ifdef _WIN32
#pragma once
#endif

// r_deferred_shadow_worldmesh: 0 = the engine draws the world into the shadow maps,
// 1 = our meshes do in the views that see all of the map (the sun's cascades, the rain map),
// 2 = in every shadow view (the lamps too)
int DeferredWorldMesh_Mode();

// Dev: the engine's world on top of ours (r_deferred_shadow_worldmesh_add)
bool DeferredWorldMesh_AlsoEngine();

// Draws what casts a shadow in the current view (the view pushed - its frustum is the engine's
// current one - and the render stage the shadow pass). False = no meshes for this map: the
// caller draws the world the engine's way.
bool DeferredWorldMesh_DrawShadowCasters();

// Dev: the meshes as a wireframe in the view being drawn (r_deferred_shadow_worldmesh_show)
void DeferredWorldMesh_DebugDraw();

// The large opaque flat faces of the map: what hides things behind it (deferred_occlusion.cpp)
#define WORLD_OCCLUDER_MAX_VERTS 12
struct WorldOccluder_t
{
	Vector	vecVerts[ WORLD_OCCLUDER_MAX_VERTS ];
	Vector	vecCenter;
	Vector	vecNormal;
	float	flArea;
	int		nVerts;
	// bit i: the edge from corner i to the next is shared with another face of the same wall
	// (nothing shows through it: the two are drawn without a gap)
	unsigned int nInnerEdges;
};
// (NULL while there is no map; the list lives until the map is left)
const WorldOccluder_t *DeferredWorldMesh_GetOccluders( int &nCount );

#endif // DEFERRED_WORLDMESH_H

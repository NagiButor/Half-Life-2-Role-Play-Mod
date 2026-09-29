#ifndef CASCADE_T_H
#define CASCADE_T_H

#include "deferred/deferred_shared_common.h"

struct cascade_t
{
	int iResolution;

	float flProjectionSize;
	float flOriginOffset;
	float flFarZ;

	float flSlopeScaleMin;
	float flSlopeScaleMax;
	float flNormalScaleMax;

	float flUpdateDelay;
	bool bOutputRadiosityData;
	int iRadiosityCascadeTarget;

#if CSM_USE_COMPOSITED_TARGET
	int iViewport_x;
	int iViewport_y;
#endif

	// HL2RPM: view-frustum fitted cascades
	float flSplitNear;		// view-space distance where this cascade starts
	float flSplitFar;		// view-space distance where this cascade ends
	int iUpdateInterval;	// render every N frames (1 = every frame)
};

const cascade_t &GetCascadeInfo( int index );

// HL2RPM: number of cascades actually rendered/used for the current quality (<= SHADOW_NUM_CASCADES)
int GetActiveCascadeCount();
// Max distance covered by sun shadows for the current quality
float GetCascadeShadowDistance();
// Recompute split distances for the given camera projection (cheap, call once per frame)
void UpdateCascadeSplits( float flZNear, float flFov, float flAspect );

#endif

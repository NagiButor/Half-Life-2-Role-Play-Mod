#pragma once

#include "iclientrenderable.h"
#include "mathlib/vector.h"
#include "mathlib/vmatrix.h"

class ITexture;

struct DeferredSpotLightParams_t
{
	Vector origin;
	Vector direction;
	Vector color;
	float radius;
	float innerAngle;
	float outerAngle;
	ClientShadowHandle_t shadowHandle;
	bool enableShadows;
};

int GetDeferredProjectedTextureLights( DeferredSpotLightParams_t *pOut, int maxCount );
bool GetDeferredShadowDataFromHandle( ClientShadowHandle_t handle, VMatrix &worldToShadow, ITexture *&pShadowDepthTexture );
bool GetDeferredGlobalLightShadow( VMatrix &worldToShadow, ITexture *&pShadowDepthTexture );

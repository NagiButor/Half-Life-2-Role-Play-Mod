#ifndef DEFERRED_RT_H
#define DEFERRED_RT_H

class ITexture;

float GetDepthMapDepthResolution( float zDelta );
void DefRTsOnModeChanged();
void InitDeferredRTs( bool bInitial = false );
void RequestDeferredRTRefresh();
void ServiceDeferredRTRefresh();

// HL2RPM: render scale - the 3D view drawn into a smaller frame (_rt_hl2rpm_scene) and stretched
// to the screen. Latched with the screen-sized render targets: they all take the size of the
// 3D frame then.
bool DeferredRenderScale_IsActive();
void DeferredRenderScale_GetFrameSize( int &w, int &h );	// the 3D frame (the back buffer when not scaled)
float DeferredRenderScale_GetScale();						// frame width / back buffer width (1 when not scaled)
ITexture *GetDefRT_Scene();									// the 3D frame when scaled, else NULL
ITexture *GetDefRT_SceneCopy();								// a copy of it for the passes that read the frame
// a render target the size of the 3D frame / iDivisor (1 or 4) - what the deferred passes draw
ITexture *CreateFrameSizedRT( const char *pszName, int iDivisor, int iFormat, int iDepth, unsigned int nFlags );

ITexture *GetDefRT_Normals();
ITexture *GetDefRT_Depth();
ITexture *GetDefRT_Albedo();
ITexture *GetDefRT_Specular();
ITexture *GetDefRT_LightCtrl();
ITexture *GetDefRT_Lightaccum();

ITexture *GetDefRT_VolumePrepass();
ITexture *GetDefRT_VolumetricsBuffer( int index );

ITexture *GetDefRT_SkyAtmoLUT();
ITexture *GetDefRT_SkyAtmoTransmittanceLUT();
ITexture *GetDefRT_SkyAtmoMultiScatteringLUT();
ITexture *GetDefRT_SkyAtmoSkyViewLUT();

ITexture *GetDefRT_RadiosityBuffer( int index );
ITexture *GetDefRT_RadiosityNormal( int index );

int GetShadowResolution_Spot();
int GetShadowResolution_Point();

#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
int GetShadowResolution_Spot_LOD1();
int GetShadowResolution_Spot_LOD2();


int GetShadowResolution_Point_LOD1();
int GetShadowResolution_Point_LOD2();
#endif

ITexture *GetShadowColorRT_Ortho( int index );
ITexture *GetShadowDepthRT_Ortho( int index );

ITexture *GetShadowColorRT_Proj( int index );
ITexture *GetShadowDepthRT_Proj( int index );
// HL2RPM: what the light pass reads from a spot shadow slot unless a lamp's own map is put there
ITexture *GetShadowSampleRT_Proj( int index );

// HL2RPM: the pool of spot shadow maps kept between frames (deferred_shadowcache.cpp)
#define MAX_SHADOW_CACHE_PROJ	24
int GetNumShadowCacheRT_Proj();
ITexture *GetShadowCacheRT_Proj( int index );

ITexture *GetShadowColorRT_DP( int index );
ITexture *GetShadowDepthRT_DP( int index );

ITexture *GetProjectableVguiRT( int index );

ITexture *GetRadiosityAlbedoRT_Ortho( int index );
ITexture *GetRadiosityNormalRT_Ortho( int index );

// HL2RPM
bool AreRadiosityRTsAvailable();

#endif

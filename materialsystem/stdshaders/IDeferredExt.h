#ifndef I_DEFERRED_EXT_H
#define I_DEFERRED_EXT_H

#ifdef CLIENT_DLL
#include "interface.h"
#include "deferred/deferred_shared_common.h"
#else
#include "tier1/interface.h"
#include "deferred_global_common.h"
#endif


struct lightData_Global_t
{
	lightData_Global_t()
	{
		bEnabled = false;
		bShadow = false;
		vecLight.Init( 0, 0, 1 );

		diff.Init();
		ambh.Init();
		ambl.Init();
	};

	Vector4D diff, ambh, ambl;
	bool bEnabled;
	bool bShadow;
	Vector4D vecLight;

	// client logic
	float flFadeTime;
	float flShadowBlend;
};

struct shadowData_ortho_t
{
	VMatrix matWorldToTexture;
#if CSM_USE_COMPOSITED_TARGET
	Vector4D vecUVTransform;
#endif

	Vector4D vecSlopeSettings;
	Vector4D vecOrigin;
	int iRes_x;
	int iRes_y;
};

struct shadowData_proj_t
{
	Vector4D vecForward;
	Vector4D vecSlopeSettings;
	Vector4D vecOrigin;
};

struct shadowData_general_t
{
	shadowData_general_t()
	{
		iDPSM_Res_x = 256;
		iDPSM_Res_y = 256;
		iPROJ_Res = 256;
		iPointShadowMode = 0;

#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
		iDPSM_Res_x_LOD1 = 128;
		iDPSM_Res_y_LOD1 = 128;
		iDPSM_Res_x_LOD2 = 64;
		iDPSM_Res_y_LOD2 = 64;
		iPROJ_Res_LOD1 = 128;
		iPROJ_Res_LOD2 = 64;
#endif
	};
	int iDPSM_Res_x;
	int iDPSM_Res_y;

	int iPROJ_Res;
	int iPointShadowMode;

#if DEFCFG_ADAPTIVE_SHADOWMAP_LOD
	int iDPSM_Res_x_LOD1;
	int iDPSM_Res_y_LOD1;
	int iDPSM_Res_x_LOD2;
	int iDPSM_Res_y_LOD2;

	int iPROJ_Res_LOD1;
	int iPROJ_Res_LOD2;
#endif
};

struct volumeData_t
{
	int iDataOffset;
	int iSamplerOffset;
	int iNumRows;

	bool bHasShadow;
	bool bHasCookie;

#if DEFCFG_ADAPTIVE_VOLUMETRIC_LOD
	int iLOD;
#endif
#if DEFCFG_CONFIGURABLE_VOLUMETRIC_LOD
	int iSamples;
#endif
};

struct radiosityData_t
{
	Vector vecOrigin[2];
};

// HL2RPM: dynamic weather / sky state, committed by the client once per frame.
struct weatherData_t
{
	weatherData_t()
	{
		bEnabled = false;
		bCloudTextureValid = false;
		bRainMapValid = false;
		vecCloudParams0.Init();
		vecCloudParams1.Init();
		vecCloudParams2.Init();
		vecWind.Init();
		vecAtmoParams.Init( 1, 0, 1, 0 );
		vecFogParams.Init();
		vecFogColor.Init();
		vecRainParams.Init();
		vecSunColor.Init();
		vecSkyZenith.Init();
		vecSkyHorizon.Init();
		vecMoonDir.Init( 0, 0, -1, 0 );
		vecSunDir.Init( 0, 0, 1, 90 );
		vecLightning.Init();
		vecCameraParams.Init();
		vecScreenParams.Init();
		matPrevViewProj.Identity();
		matRainMap.Identity();
		vecRainMapParams.Init();
		vecSkyLight.Init( 1, 1, 1, 0 );
		vecDebug.Init();
		vecCloudParams3.Init();
		vecSkyLightDir.Init( 0, 0, 1, 0 );
		vecSkyLightDiff.Init();
		vecGodRays0.Init();
		vecGodRays1.Init();
	}

	bool bEnabled;
	bool bCloudTextureValid;	// screen-space cloud RT matches the view being drawn
	bool bRainMapValid;

	Vector4D vecCloudParams0;	// x coverage, y density, z type, w darkness
	Vector4D vecCloudParams1;	// x base (m), y thickness (m), z cirrus, w cloud time (s)
	Vector4D vecCloudParams2;	// x brightness, y shadow strength, z quality steps, w temporal blend
	Vector4D vecWind;			// xy cloud offset (m), zw rain slant (unit/unit)
	Vector4D vecAtmoParams;		// x haze, y sky desaturation, z sky brightness, w lightning flash
	Vector4D vecFogParams;		// x density /1000u, y falloff /1000u, z base height (units), w volumetric density
	Vector4D vecFogColor;		// rgb fog color (linear), w max opacity
	Vector4D vecRainParams;		// x rain, y wetness, z puddles, w time (s)
	Vector4D vecSunColor;		// rgb direct light color reaching the clouds, w night factor
	Vector4D vecSkyZenith;		// rgb sky ambient from above, w cubemap reflection scale (world brightness now / daylight)
	Vector4D vecSkyHorizon;		// rgb sky ambient at the horizon
	Vector4D vecMoonDir;		// xyz moon direction, w moon brightness
	Vector4D vecSunDir;			// xyz real sun direction (the global light may be the moon), w sun altitude (deg)
	Vector4D vecLightning;		// xyz flash direction, w flash intensity
	Vector4D vecCameraParams;	// xyz camera world pos, w camera altitude (m)
	Vector4D vecScreenParams;	// x 1/w, y 1/h, z cloud RT uv scale, w frame index
	VMatrix matPrevViewProj;	// rotation-only view projection of the previous frame (cloud reprojection)
	VMatrix matRainMap;			// world -> rain occlusion map texture space
	Vector4D vecRainMapParams;	// x depth bias, y map size (units), z texel size, w unused
	Vector4D vecSkyLight;		// rgb illuminance of the light the sky LUT is built for (untinted sun, or the moon)
	Vector4D vecDebug;			// x: r_weather_debug_view (1 = rain exposure map)
	Vector4D vecCloudParams3;	// x altocumulus amount, y altocumulus altitude (m), zw unused
	Vector4D vecSkyLightDir;	// xyz the sun or the moon lighting sky and clouds (the global light without a lightning flash)
	Vector4D vecSkyLightDiff;	// rgb its diffuse color (global light without the flash)
	Vector4D vecGodRays0;		// xy sun position (screen uv), z strength (0 = off), w aspect
	Vector4D vecGodRays1;		// rgb light color, w decay per step
};

// HL2RPM: screen-space ambient occlusion + sky visibility of the ambient light
struct ssaoData_t
{
	ssaoData_t()
	{
		bEnabled = false;
		vecParams0.Init();
		vecParams1.Init();
		vecBlurH.Init();
		vecBlurV.Init();
		vecApply.Init( 0, 0.35f, 256.0f, 0.5f );
	}

	bool bEnabled;
	Vector4D vecParams0;	// x radius (units), y intensity, z cos bias, w projection scale (px per unit at depth 1)
	Vector4D vecParams1;	// x 1/viewport w, y 1/viewport h, z max radius (px), w frame
	Vector4D vecBlurH;		// xy step (screen uv), z depth tolerance, w AO uv scale
	Vector4D vecBlurV;
	Vector4D vecApply;		// x sky visibility on, y indoor ambient, z sky visibility radius (units), w AO uv scale (0 = no AO)
};

// HL2RPM: world-space indirect light of the global light (GI probes, deferred_gi.cpp in the client)
struct giData_t
{
	giData_t()
	{
		bEnabled = false;
		vecGrid.Init();
		vecDims.Init();
		vecAtlas.Init();
		vecParams0.Init();
		vecParams1.Init();
		vecBlurH.Init();
		vecBlurV.Init();
		vecApply.Init();
	}

	bool bEnabled;
	Vector4D vecGrid;		// xyz world cell index of the grid's first probe, w 1 / probe spacing
	Vector4D vecDims;		// xyz probes per axis, w probe spacing (units)
	Vector4D vecAtlas;		// x 1 / atlas width, y 1 / atlas height, z lookup offset along the normal (units), w debug
	Vector4D vecParams0;	// x sun bounce scale, y sky bounce scale, z multiple bounce fill, w sky light left without sky in sight
	Vector4D vecParams1;	// x leak tolerance (units), y probe offset in its cell, z sky light boost (daylight), w unused
	Vector4D vecBlurH;		// xy step (screen uv), z depth tolerance, w GI uv scale
	Vector4D vecBlurV;
	Vector4D vecApply;		// x on, y debug, z unused, w GI uv scale (lighting pass)
};

// HL2RPM: with the GI probes the sky light is occluded where it should be (a room gets it only
// through its openings), so it can be as strong as a real sky: the maps' ambient was tuned for
// the flat sky light of before (a few % of the sun - the shade outdoors was nearly black).
// Daylight only: the night keeps its sky.
inline float GetGISkyBoost( const giData_t &gi, const weatherData_t &w )
{
	if ( !gi.bEnabled || gi.vecParams1.z <= 1.0f )
		return 1.0f;
	// world brightness compared to the day (1 by day, a few % at night)
	const float flWorld = w.bEnabled ? w.vecSkyZenith.w : 1.0f;
	float flDay = ( flWorld - 0.05f ) / 0.45f;
	flDay = ( flDay < 0.0f ) ? 0.0f : ( ( flDay > 1.0f ) ? 1.0f : flDay );
	return 1.0f + ( gi.vecParams1.z - 1.0f ) * flDay;
}

// HL2RPM: post-processing of the final frame (deferred_postfx.cpp in the client): eye
// adaptation, bloom on mip levels packed in two targets (level 0 = the frame), grading, lens
#define POSTFX_LEVELS 6

struct postfxData_t
{
	postfxData_t()
	{
		vecExposure.Init( 0.18f, 0.0f, 1.0f, 1.0f );
		vecAdapt.Init( 1.0f, 1.0f, 1.0f, 0.0f );
		vecBloom.Init();
		vecGrade.Init( 0.0f, 1.0f, 0.8f, 0.0f );
		vecLens.Init();
		vecTint.Init( 1.0f, 1.0f, 1.0f, 1.0f );
		vecFlare.Init();
		vecDoF.Init();
		vecLumBlocks.Init();
		vecFrameToBloom.Init();
		vecFrameTexel.Init();
		for ( int i = 0; i < POSTFX_LEVELS; i++ )
		{
			vecLevelTexel[i].Init();
			vecLevelRect[i].Init();
		}
	}

	Vector4D vecExposure;		// x average luminance that keeps exposure 1, y adaptation (0..1), z min, w max exposure
	Vector4D vecAdapt;			// x rate toward a brighter scene, y toward a darker one, z reset
	Vector4D vecBloom;			// x strength, y lens dirt, z threshold, w knee
	Vector4D vecGrade;			// x contrast, y saturation, z shoulder start, w sharpening
	Vector4D vecLens;			// x vignette, y grain, z chromatic aberration (pixels), w frame
	Vector4D vecTint;			// rgb color balance, w aspect ratio
	Vector4D vecFlare;			// lens flare of the sun: xy its place on screen (uv), z strength (0 = off), w bloom brightness there it starts at
	Vector4D vecDoF;			// depth of field: x circle of confusion scale (pixels x units), y 1 / focus distance, z largest radius (pixels), w debug
	Vector4D vecLumBlocks;		// xy one block of the frame (uv) / texel size of the block target, zw blocks in x / y
	Vector4D vecFrameToBloom;	// xy frame uv -> bloom uv scale, zw offset
	Vector4D vecFrameTexel;		// xy 1 / frame texture size, zw frame texture size
	Vector4D vecLevelTexel[ POSTFX_LEVELS ];	// xy texel size of the level's target, z upsample radius, w weight when added
	Vector4D vecLevelRect[ POSTFX_LEVELS ];		// uv rectangle of the level in its target (xy min, zw max)
};

// HL2RPM: temporal anti-aliasing (deferred_taa.cpp in the client): the rows x, y, w of the
// unjittered view-projection of the last and of this frame, both for positions relative to
// this frame's camera (the camera's move is applied separately: no big world coordinates)
struct taaData_t
{
	taaData_t()
	{
		for ( int i = 0; i < 3; i++ )
		{
			vecPrev[i].Init();
			vecCur[i].Init();
		}
		vecCamDelta.Init();
		vecParams.Init();
		vecParams2.Init();
		vecParams3.Init();
		vecTexel.Init();
	}

	Vector4D vecPrev[3];		// last frame: clip x, y, w
	Vector4D vecCur[3];			// this frame: clip x, y, w
	Vector4D vecCamDelta;		// xyz last camera position - this one
	Vector4D vecParams;			// x history weight (history like the frame), y (history unlike it), z clip box size (deviations), w reset
	Vector4D vecParams2;		// x debug mode, y motion (pixels) at which less history is kept, zw view size / texture size
	Vector4D vecParams3;		// x frame number (dither of the 8 bit history), y mip bias of the albedo in the composite (this frame jittered)
	Vector4D vecTexel;			// xy 1 / texture size, zw texture size
};

#include "tier0/memdbgon.h"

struct lightDataCommon_t
{
	lightDataCommon_t( size_t dataCount )
	{
		pFlData = new float[dataCount];
	}
	void DeleteThis()
	{
		delete[] pFlData;
		delete this;
	}

	float* pFlData;
	int numRows;
	int numShadowedCookied;
	int numShadowed;
	int numCookied;
	int numSimple;
};

#include "tier0/memdbgoff.h"

class IDeferredExtension : public IBaseInterface
{
public:
	virtual void EnableDeferredLighting() = 0;

	virtual void CommitCommonData( const Vector &origin,
								   const Vector &fwd,
								   const float &zNear, const float &zFar,
								   const VMatrix &matTFrustum
#if DEFCFG_BILATERAL_DEPTH_TEST
								,  const VMatrix &matWorldCameraDepthTex
#endif
																) = 0;

	virtual void CommitZScale( const float &zScale ) = 0;

	virtual void CommitShadowData_Ortho( const int &index, const shadowData_ortho_t &data ) = 0;
	virtual void CommitShadowData_Rain( const shadowData_ortho_t &data ) = 0;
	virtual void CommitShadowData_Proj( const int &index, const shadowData_proj_t &data ) = 0;
	virtual void CommitShadowData_General( const shadowData_general_t &data ) = 0;

	virtual void CommitVolumeData( const volumeData_t &data ) = 0;

	virtual void CommitRadiosityData( const radiosityData_t &data ) = 0;

	virtual void CommitLightData_Global( const lightData_Global_t &data ) = 0;
	virtual void CommitLightData_Common( lightDataCommon_t* pData ) = 0;

	virtual void CommitTexture_General( ITexture *pTexNormals, ITexture *pTexDepth,
#if ( DEFCFG_LIGHTCTRL_PACKING == 0 )
		ITexture *pTexLightingCtrl,
#elif DEFCFG_DEFERRED_SHADING == 1
		ITexture *pTexAlbedo,
		ITexture *pTexSpecular,
#endif
		ITexture *pTexLightAccum ) = 0;
	virtual void CommitTexture_CascadedDepth( const int &index, ITexture *pTexShadowDepth ) = 0;
	virtual void CommitTexture_DualParaboloidDepth( const int &index, ITexture *pTexShadowDepth ) = 0;
	virtual void CommitTexture_ProjectedDepth( const int &index, ITexture *pTexShadowDepth ) = 0;
	virtual void CommitTexture_Cookie( const int &index, ITexture *pTexCookie ) = 0;
	virtual void CommitTexture_VolumePrePass( ITexture *pTexVolumePrePass ) = 0;
	virtual void CommitTexture_ShadowRadOutput_Ortho( ITexture *pAlbedo, ITexture *pNormal ) = 0;
	virtual void CommitTexture_Radiosity( ITexture *pTexRadBuffer0, ITexture *pTexRadBuffer1,
		ITexture *pTexRadNormal0, ITexture *pTexRadNormal1 ) = 0;

	// HL2RPM weather
	virtual void CommitWeatherData( const weatherData_t &data ) = 0;
	virtual void CommitWeatherCloudTextureValid( const bool &bValid ) = 0;
	virtual void CommitTexture_Weather( ITexture *pCloudTexture, ITexture *pCloudHistory, ITexture *pCloudRaw,
		ITexture *pCloudNoise, ITexture *pWeatherMap, ITexture *pRainMap ) = 0;

	// HL2RPM ambient occlusion
	virtual void CommitSSAOData( const ssaoData_t &data ) = 0;
	virtual void CommitTexture_SSAO( ITexture *pAO, ITexture *pBlur ) = 0;

	// HL2RPM: world -> screen texture coords of the main view (its G-buffer and light buffer),
	// and whether a planar reflection view (water, glass) is being drawn right now: the
	// composite of such a view reprojects into the main view's light buffer
	virtual void CommitMainViewToScreenTex( const VMatrix &matWorldToScreenTex ) = 0;
	virtual void CommitReflectionView( const bool &bReflection ) = 0;

	// HL2RPM: the sun shadow atlas' caster depth as a readable R32F texture (PCSS blocker search)
	virtual void CommitTexture_CascadedDepthRaw( const int &index, ITexture *pTexDepthRaw ) = 0;

	// HL2RPM: GI probes (atlas), the half resolution indirect light and its blur target
	virtual void CommitGIData( const giData_t &data ) = 0;
	virtual void CommitTexture_GI( ITexture *pProbes, ITexture *pGI, ITexture *pBlur ) = 0;

	// HL2RPM: post-processing of the final frame
	virtual void CommitPostFXData( const postfxData_t &data ) = 0;

	// HL2RPM: temporal anti-aliasing
	virtual void CommitTAAData( const taaData_t &data ) = 0;
};

#define DEFERRED_EXTENSION_VERSION "DeferredExtensionVersion008"

#ifdef STDSHADER_DX9_DLL_EXPORT

class CDeferredExtension : public IDeferredExtension
{
public:
	CDeferredExtension();
	~CDeferredExtension();

	virtual void EnableDeferredLighting();
	bool IsDeferredLightingEnabled() const;
	bool IsRadiosityEnabled() const;

	void CommitCommonData( const Vector &origin,
						   const Vector &fwd,
						   const float &zNear, const float &zFar,
						   const VMatrix &matTFrustum
#if DEFCFG_BILATERAL_DEPTH_TEST
						   , const VMatrix &matWorldCameraDepthTex
#endif
														);

	virtual void CommitZScale( const float &zScale );

	virtual void CommitShadowData_Ortho( const int &index, const shadowData_ortho_t &data );
	virtual void CommitShadowData_Rain( const shadowData_ortho_t &data );
	virtual void CommitShadowData_Proj( const int &index, const shadowData_proj_t &data );
	virtual void CommitShadowData_General( const shadowData_general_t &data );

	virtual void CommitVolumeData( const volumeData_t &data );
	virtual void CommitRadiosityData( const radiosityData_t &data );

	virtual void CommitLightData_Global( const lightData_Global_t &data );
	virtual void CommitLightData_Common( lightDataCommon_t* pData );

	virtual void CommitTexture_General( ITexture *pTexNormals, ITexture *pTexDepth,
#if ( DEFCFG_LIGHTCTRL_PACKING == 0 )
		ITexture *pTexLightingCtrl,
#elif DEFCFG_DEFERRED_SHADING == 1
		ITexture *pTexAlbedo,
		ITexture *pTexSpecular,
#endif
		ITexture *pTexLightAccum );
	virtual void CommitTexture_CascadedDepth( const int &index, ITexture *pTexShadowDepth );
	virtual void CommitTexture_DualParaboloidDepth( const int &index, ITexture *pTexShadowDepth );
	virtual void CommitTexture_ProjectedDepth( const int &index, ITexture *pTexShadowDepth );
	virtual void CommitTexture_Cookie( const int &index, ITexture *pTexCookie );
	virtual void CommitTexture_VolumePrePass( ITexture *pTexVolumePrePass );
	virtual void CommitTexture_ShadowRadOutput_Ortho( ITexture *pAlbedo, ITexture *pNormal );
	virtual void CommitTexture_Radiosity( ITexture *pTexRadBuffer0, ITexture *pTexRadBuffer1,
		ITexture *pTexRadNormal0, ITexture *pTexRadNormal1 );

	virtual void CommitWeatherData( const weatherData_t &data );
	virtual void CommitWeatherCloudTextureValid( const bool &bValid );
	virtual void CommitTexture_Weather( ITexture *pCloudTexture, ITexture *pCloudHistory, ITexture *pCloudRaw,
		ITexture *pCloudNoise, ITexture *pWeatherMap, ITexture *pRainMap );

	virtual void CommitSSAOData( const ssaoData_t &data );
	virtual void CommitTexture_SSAO( ITexture *pAO, ITexture *pBlur );
	inline const ssaoData_t &GetSSAOData() { return m_dataSSAO; }

	virtual void CommitMainViewToScreenTex( const VMatrix &matWorldToScreenTex );
	virtual void CommitReflectionView( const bool &bReflection );
	inline float *GetMainViewToScreenTexBase() { return m_matMainViewToScreenTex.Base(); }
	inline bool IsReflectionView() const { return m_bReflectionView; }

	virtual void CommitTexture_CascadedDepthRaw( const int &index, ITexture *pTexDepthRaw );

	virtual void CommitGIData( const giData_t &data );
	virtual void CommitTexture_GI( ITexture *pProbes, ITexture *pGI, ITexture *pBlur );
	inline const giData_t &GetGIData() { return m_dataGI; }
	inline ITexture *GetTexture_GIProbes() { return m_pTexGIProbes; }
	inline ITexture *GetTexture_GI() { return m_pTexGI; }
	inline ITexture *GetTexture_GIBlur() { return m_pTexGIBlur; }

	virtual void CommitPostFXData( const postfxData_t &data );
	inline const postfxData_t &GetPostFXData() { return m_dataPostFX; }

	virtual void CommitTAAData( const taaData_t &data );
	inline const taaData_t &GetTAAData() { return m_dataTAA; }

	inline ITexture *GetTexture_ShadowDepthRaw_Ortho( const int &index ) { return m_pTexShadowDepthRaw_Ortho[ index ]; }
	inline ITexture *GetTexture_SSAO() { return m_pTexSSAO; }
	inline ITexture *GetTexture_SSAOBlur() { return m_pTexSSAOBlur; }

	inline const weatherData_t &GetWeatherData() { return m_dataWeather; }
	inline ITexture *GetTexture_Clouds() { return m_pTexClouds; }
	inline ITexture *GetTexture_CloudHistory() { return m_pTexCloudHistory; }
	inline ITexture *GetTexture_CloudRaw() { return m_pTexCloudRaw; }
	inline ITexture *GetTexture_CloudNoise() { return m_pTexCloudNoise; }
	inline ITexture *GetTexture_WeatherMap() { return m_pTexWeatherMap; }
	inline ITexture *GetTexture_RainMap() { return m_pTexRainMap; }

	inline float *GetOriginBase();
	inline float *GetForwardBase();
	inline const float &GetZDistNear();
	inline const float &GetZDistFar();
	inline float GetZScale();
	inline float *GetFrustumDeltaBase();
#if DEFCFG_BILATERAL_DEPTH_TEST
	inline float *GetWorldToCameraDepthTexBase();
#endif

	inline int GetNumActiveLights_ShadowedCookied();
	inline int GetNumActiveLights_Shadowed();
	inline int GetNumActiveLights_Cookied();
	inline int GetNumActiveLights_Simple();
	inline float *GetActiveLightData();
	inline int GetActiveLights_NumRows();

	inline const shadowData_ortho_t &GetShadowData_Ortho( const int &index );
	inline const shadowData_ortho_t &GetShadowData_Rain() { return m_dataRainShadow; }
	inline const shadowData_proj_t &GetShadowData_Proj( const int &index );
	inline const shadowData_general_t &GetShadowData_General();

	inline const volumeData_t &GetVolumeData();
	inline const radiosityData_t &GetRadiosityData();

	inline const lightData_Global_t &GetLightData_Global();

	inline ITexture *GetTexture_Normals();
	inline ITexture *GetTexture_Depth();
	inline ITexture *GetTexture_LightAccum();
#if ( DEFCFG_LIGHTCTRL_PACKING == 0 )
	inline ITexture *GetTexture_LightCtrl();
#elif DEFCFG_DEFERRED_SHADING == 1
	inline ITexture *GetTexture_Albedo();
	inline ITexture *GetTexture_Specular();
#endif
	inline ITexture *GetTexture_ShadowDepth_Ortho( const int &index );
	inline ITexture *GetTexture_ShadowDepth_DP( const int &index );
	inline ITexture *GetTexture_ShadowDepth_Proj( const int &index );
	inline ITexture *GetTexture_Cookie( const int &index );
	inline ITexture *GetTexture_VolumePrePass();
	inline ITexture *GetTexture_ShadowRad_Ortho_Albedo();
	inline ITexture *GetTexture_ShadowRad_Ortho_Normal();
	inline ITexture *GetTexture_RadBuffer( const int &index );
	inline ITexture *GetTexture_RadNormal( const int &index );

private:
	bool m_bDefLightingEnabled;

	Vector4D m_vecOrigin;
	Vector4D m_vecForward;
	float m_flZDists[3];
	VMatrix m_matTFrustumD;
	VMatrix m_matMainViewToScreenTex;
	bool m_bReflectionView;
#if DEFCFG_BILATERAL_DEPTH_TEST
	VMatrix m_matWorldCameraDepthTex;
#endif

	shadowData_ortho_t m_dataOrtho[ SHADOW_NUM_CASCADES ];
	shadowData_ortho_t m_dataRainShadow;
	shadowData_proj_t m_dataProj[ MAX_SHADOW_PROJ ];
	shadowData_general_t m_dataGeneral;

	volumeData_t m_dataVolume;
	radiosityData_t m_dataRadiosity;

	lightData_Global_t m_globalLight;
	lightDataCommon_t *m_pflCommonLightData;
	int m_iCommon_NumRows;
	int m_iNumCommon_ShadowedCookied;
	int m_iNumCommon_Shadowed;
	int m_iNumCommon_Cookied;
	int m_iNumCommon_Simple;

	ITexture *m_pTexNormals;
	ITexture *m_pTexDepth;
	ITexture *m_pTexLightAccum;
#if ( DEFCFG_LIGHTCTRL_PACKING == 0 )
	ITexture *m_pTexLightCtrl;
#elif DEFCFG_DEFERRED_SHADING == 1
	ITexture *m_pTexAlbedo;
	ITexture *m_pTexSpecular;
#endif
	ITexture *m_pTexShadowDepth_Ortho[ MAX_SHADOW_ORTHO ];
	ITexture *m_pTexShadowDepthRaw_Ortho[ MAX_SHADOW_ORTHO ];
	ITexture *m_pTexShadowDepth_DP[ MAX_SHADOW_DP ];
	ITexture *m_pTexShadowDepth_Proj[ MAX_SHADOW_PROJ ];
	ITexture *m_pTexCookie[ NUM_COOKIE_SLOTS ];
	ITexture *m_pTexVolumePrePass;
	ITexture *m_pTexShadowRad_Ortho[ 2 ];
	ITexture *m_pTexRadBuffer[ 2 ];
	ITexture *m_pTexRadNormal[ 2 ];

	weatherData_t m_dataWeather;
	ITexture *m_pTexClouds;
	ITexture *m_pTexCloudHistory;
	ITexture *m_pTexCloudRaw;
	ITexture *m_pTexCloudNoise;
	ITexture *m_pTexWeatherMap;
	ITexture *m_pTexRainMap;

	ssaoData_t m_dataSSAO;
	ITexture *m_pTexSSAO;
	ITexture *m_pTexSSAOBlur;

	giData_t m_dataGI;
	ITexture *m_pTexGIProbes;
	ITexture *m_pTexGI;
	ITexture *m_pTexGIBlur;

	postfxData_t m_dataPostFX;

	taaData_t m_dataTAA;
};

float *CDeferredExtension::GetOriginBase()
{
	return m_vecOrigin.Base();
}
float *CDeferredExtension::GetForwardBase()
{
	return m_vecForward.Base();
}
const float &CDeferredExtension::GetZDistNear()
{
	return m_flZDists[0];
}
const float &CDeferredExtension::GetZDistFar()
{
	return m_flZDists[1];
}
float CDeferredExtension::GetZScale()
{
	return m_flZDists[2];
}
float *CDeferredExtension::GetFrustumDeltaBase()
{
	return m_matTFrustumD.Base();
}

#if DEFCFG_BILATERAL_DEPTH_TEST
float *CDeferredExtension::GetWorldToCameraDepthTexBase()
{
	return m_matWorldCameraDepthTex.Base();
}
#endif
const shadowData_ortho_t &CDeferredExtension::GetShadowData_Ortho( const int &index )
{
	Assert( index >= 0 && index < SHADOW_NUM_CASCADES );
	return m_dataOrtho[ index ];
}
const shadowData_proj_t &CDeferredExtension::GetShadowData_Proj( const int &index )
{
	Assert( index >= 0 && index < MAX_SHADOW_PROJ );
	return m_dataProj[ index ];
}
const shadowData_general_t &CDeferredExtension::GetShadowData_General()
{
	return m_dataGeneral;
}

const lightData_Global_t &CDeferredExtension::GetLightData_Global()
{
	return m_globalLight;
}

const volumeData_t &CDeferredExtension::GetVolumeData()
{
	return m_dataVolume;
}

const radiosityData_t &CDeferredExtension::GetRadiosityData()
{
	return m_dataRadiosity;
}

int CDeferredExtension::GetNumActiveLights_ShadowedCookied()
{
	return m_iNumCommon_ShadowedCookied;
}
int CDeferredExtension::GetNumActiveLights_Shadowed()
{
	return m_iNumCommon_Shadowed;
}
int CDeferredExtension::GetNumActiveLights_Cookied()
{
	return m_iNumCommon_Cookied;
}
int CDeferredExtension::GetNumActiveLights_Simple()
{
	return m_iNumCommon_Simple;
}
float *CDeferredExtension::GetActiveLightData()
{
	if ( m_pflCommonLightData )
		return m_pflCommonLightData->pFlData;
	return NULL;
}
int CDeferredExtension::GetActiveLights_NumRows()
{
	return m_iCommon_NumRows;
}
ITexture *CDeferredExtension::GetTexture_Normals()
{
	return m_pTexNormals;
}
ITexture *CDeferredExtension::GetTexture_Depth()
{
	return m_pTexDepth;
}
ITexture *CDeferredExtension::GetTexture_LightAccum()
{
	return m_pTexLightAccum;
}
#if ( DEFCFG_LIGHTCTRL_PACKING == 0 )
ITexture *CDeferredExtension::GetTexture_LightCtrl()
{
	return m_pTexLightCtrl;
}
#elif DEFCFG_DEFERRED_SHADING == 1
ITexture *CDeferredExtension::GetTexture_Albedo()
{
	return m_pTexAlbedo;
}
ITexture *CDeferredExtension::GetTexture_Specular()
{
	return m_pTexSpecular;
}
#endif
ITexture *CDeferredExtension::GetTexture_ShadowDepth_Ortho( const int &index )
{
	Assert( index >= 0 && index < MAX_SHADOW_ORTHO );
	return m_pTexShadowDepth_Ortho[index];
}
ITexture *CDeferredExtension::GetTexture_ShadowDepth_DP( const int &index )
{
	Assert( index >= 0 && index < MAX_SHADOW_DP );
	return m_pTexShadowDepth_DP[index];
}
ITexture *CDeferredExtension::GetTexture_ShadowDepth_Proj( const int &index )
{
	Assert( index >= 0 && index < MAX_SHADOW_PROJ );
	return m_pTexShadowDepth_Proj[index];
}
ITexture *CDeferredExtension::GetTexture_Cookie( const int &index )
{
	Assert( index >= 0 && index < NUM_COOKIE_SLOTS );
	return m_pTexCookie[index];
}
ITexture *CDeferredExtension::GetTexture_VolumePrePass()
{
	return m_pTexVolumePrePass;
}
ITexture *CDeferredExtension::GetTexture_ShadowRad_Ortho_Albedo()
{
	return m_pTexShadowRad_Ortho[0];
}
ITexture *CDeferredExtension::GetTexture_ShadowRad_Ortho_Normal()
{
	return m_pTexShadowRad_Ortho[1];
}
ITexture *CDeferredExtension::GetTexture_RadBuffer( const int &index )
{
	Assert( index >= 0 && index < 2 );
	return m_pTexRadBuffer[index];
}
ITexture *CDeferredExtension::GetTexture_RadNormal( const int &index )
{
	Assert( index >= 0 && index < 2 );
	return m_pTexRadNormal[index];
}
#endif

#ifdef CLIENT_DLL
bool ConnectDeferredExt();
void ShutdownDeferredExt();

extern IDeferredExtension *GetDeferredExt();
#else
extern CDeferredExtension __g_defExt;
FORCEINLINE CDeferredExtension *GetDeferredExt()
{
	return &__g_defExt;
}
#endif

#endif

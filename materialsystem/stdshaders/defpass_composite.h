#ifndef DEFPASS_COMPOSITE_H
#define DEFPASS_COMPOSITE_H

class CDeferredPerMaterialContextData;

struct defParms_composite
{
	defParms_composite()
	{
		Q_memset( this, 0xFF, sizeof( defParms_composite ) );

		bModel = false;
	};

	// textures
	int iAlbedo;
	int iAlbedo2;
	// HL2RPM: a higher resolution version of the base texture(s) the client found in materials/hd -
	// sampled instead of them; $basetexture keeps the size the brushes are mapped with
	int iAlbedoHD;
	int iAlbedo2HD;
	int iAlbedo3;
	int iAlbedo4;
	int iEnvmap;
	int iEnvmapMask;
	int iEnvmapMask2;
	// (its alpha: the envmap mask of $normalmapalphaenvmapmask)
	int iBumpmap;
	int iBlendmodulate;
	int iBlendmodulate2;
	int iBlendmodulate3;

	// envmapping
	int iEnvmapTint;
	int iEnvmapSaturation;
	int iEnvmapContrast;
	int iEnvmapFresnel;

	// rimlight
	int iRimlightEnable;
	int iRimlightExponent;
	int iRimlightAlbedoScale;
	int iRimlightTint;
	int iRimlightModLight;
	int iRimlightBoost;

	// alpha
	int iAlphatestRef;

	// phong
	int iPhongScale;
	int iPhongFresnel;

	// HL2RPM (PBR, hl2rpm_pbr.h)
	int iMetalness;
	int iSpecular;
	// HL2RPM: the relief of a brush material, the wind in a tree, the light through a leaf
	int iPomHeightmap;
	int iPomScale;
	// HL2RPM: a blend: the relief of its second layer (made of $basetexture2: on its coordinates) and
	// the coordinates of $basetexture2 ($basetexturetransform2)
	int iPomHeightmap2;
	int iPomTextureRelief2;
	int iBaseTransform2;
	int iTreeSway;
	int iTransmission;
	// HL2RPM: glass in the texture ($pbr_glassmask, set by the client: the windows of a facade, a pane
	// painted into a texture) - smooth where its envmap mask is (the G-buffer's gloss map); what it
	// mirrors comes from the weather pass (the sky by the direction, the scene around), not from the
	// envmap here, and its dark texels (the windows) keep their glint
	int iGlassMask;
	// HL2RPM: the detail texture ($detail, $detailscale, $detailblendfactor, $detailblendmode)
	int iDetail;
	int iDetailScale;
	int iDetailBlendFactor;
	int iDetailBlendMode;

	// self illum
	int iSelfIllumTint;
	int iSelfIllumMaskInEnvmapAlpha;
	int iSelfIllumFresnelModulate;
	int iSelfIllumMask;

	// blendmod
	int iBlendmodulateTransform;
	int iBlendmodulateTransform2;
	int iBlendmodulateTransform3;
	int iMultiblend;

	int iFresnelRanges;

	int iEnvmapParallax;
	int iEnvmapOrigin;

	// config
	bool bModel;
};


void InitParmsComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params );
void InitPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params );
void DrawPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext );


#endif

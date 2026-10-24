#ifndef DEFPASS_GBUFFER_H
#define DEFPASS_GBUFFER_H

class CDeferredPerMaterialContextData;

struct defParms_gBuffer
{
	defParms_gBuffer()
	{
		Q_memset( this, 0xFF, sizeof( defParms_gBuffer ) );

		bModel = false;
		bDepthBias = false;
	};

	// textures
	int iAlbedo;
	int iAlbedo2;
	int iAlbedo3;
	int iAlbedo4;

	int iBumpmap;
	int iBumpmap2;
	int iBumpmap3;
	int iBumpmap4;
	int iPhongmap;
	int iBlendmodulate;
	int iBlendmodulate2;
	int iBlendmodulate3;

	// control
	int iAlphatestRef;
	int iLitface;
	int iPhongExp;		// HL2RPM (PBR): the gloss of the surface, 0 .. 1 (hl2rpm_pbr.h)
	int iPhongExp2;
	int iGlossMask;		// HL2RPM (PBR): the alpha of the bump map scales it
	int iSSBump;
	// HL2RPM: the relief of a brush material (hl2rpm_pom_fxc.h), the wind in a tree (hl2rpm_sway_fxc.h)
	int iPomHeightmap;
	int iPomScale;
	// HL2RPM: the relief of the second layer of a blend, and whether it is made of $basetexture2
	// (then on its $basetexturetransform2)
	int iPomHeightmap2;
	int iPomTextureRelief2;
	int iBaseTransform2;
	int iTreeSway;

	// blending
	int iBlendmodulateTransform;
	int iBlendmodulateTransform2;
	int iBlendmodulateTransform3;
	int iMultiblend;

	// config
	bool bModel;
	bool bDepthBias;
};


void InitParmsGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params );
void InitPassGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params );
void DrawPassGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext );


#endif

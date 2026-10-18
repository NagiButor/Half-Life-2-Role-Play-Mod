
#include "deferred_includes.h"
#include "hl2rpm_pbr.h"

#include "composite_vs30.inc"
#include "composite_ps30.inc"

#include "tier0/memdbgon.h"

static CCommandBufferBuilder< CFixedCommandStorageBuffer< 512 > > tmpBuf;

ConVar building_cubemaps( "building_cubemaps", "0" );

// HL2RPM dev: white albedo - the composite shows the lighting only (temporal stability tests:
// without the textures motion aliasing doesn't hide light flicker)
// HL2RPM: MSAA - the fragments of the triangles that don't own a pixel's center take the light
// of a neighbouring pixel of their own surface (composite_ps30). Off: MSAA doesn't work in the
// deferred view yet (mat_antialias changes nothing - one fragment per pixel, see r_deferred_fxaa),
// the lookup only cost a depth fetch per pixel.
static ConVar r_deferred_msaa_light( "r_deferred_msaa_light", "0", 0,
	"Deferred composite: with MSAA, edge fragments take the light of a neighbour pixel of their own surface (less crawling on thin geometry)" );

// HL2RPM (PBR): the mirror reflection of the opaque deferred materials (composite_ps30, hl2rpm_pbr.h)
ConVar r_deferred_pbr( "r_deferred_pbr", "1", FCVAR_ARCHIVE,
	"Physically based materials: surfaces mirror the sun, the lamps and the light around them by their roughness and metalness (0 = the matte materials of before)" );
static ConVar r_deferred_pbr_specular( "r_deferred_pbr_specular", "1.0", FCVAR_ARCHIVE,
	"PBR: scale of everything the surfaces mirror", true, 0.0f, true, 4.0f );
static ConVar r_deferred_pbr_cavity( "r_deferred_pbr_cavity", "0.7", 0,
	"PBR: how much the dark texels of a texture (cracks, dirt, seams) mirror less", true, 0.0f, true, 1.0f );
static ConVar r_deferred_pbr_cavity_white( "r_deferred_pbr_cavity_white", "0.3", 0,
	"PBR: the albedo brightness (linear) from which a texel mirrors fully", true, 0.02f, true, 1.0f );
static ConVar r_deferred_pbr_debug( "r_deferred_pbr_debug", "0", FCVAR_CHEAT, "PBR: 1 = only what the surfaces mirror, 2 = only what they scatter" );
static ConVar r_deferred_pbr_metal_diffuse( "r_deferred_pbr_metal_diffuse", "0.7", 0,
	"PBR: how much of its diffuse color a full metal loses", true, 0.0f, true, 1.0f );

// HL2RPM: $envmap without a cubemap to show (composite_ps30)
static ConVar r_deferred_envmap_fallback( "r_deferred_envmap_fallback", "0.7", FCVAR_ARCHIVE,
	"$envmap materials whose cubemap is missing (maps without built cubemaps, models) mirror the light at the pixel, this much of it (0 = the engine's default cubemap)", true, 0.0f, true, 4.0f );
// (set by the client when a map loads: 0 = the map has no cubemaps of its own)
ConVar mat_hl2rpm_map_cubemaps( "mat_hl2rpm_map_cubemaps", "1", FCVAR_HIDDEN, "Set by the client: the loaded map has built cubemaps" );

// HL2RPM: the light right at a lamp (composite_ps30)
static ConVar r_deferred_light_limit( "r_deferred_light_limit", "2.5", FCVAR_ARCHIVE,
	"The most light a surface takes, in units of the daylight (a soft limit: the ground under a strong lamp is bright, not a white patch)", true, 0.5f, true, 64.0f );
static ConVar r_deferred_light_limit_specular( "r_deferred_light_limit_specular", "48", 0,
	"The most a surface mirrors of the lamps and the sun (for a full mirror)", true, 1.0f, true, 1024.0f );

static ConVar r_deferred_reflect_debug( "r_deferred_reflect_debug", "0", FCVAR_CHEAT,
	"Dev: planar reflection views (water, glass) show where they find their light in the main view (red = found, green = depth mismatch, blue = on its screen)" );
static ConVar r_deferred_debug_lighting_only( "r_deferred_debug_lighting_only", "0", FCVAR_CHEAT, "Dev: draw the deferred composite with a white albedo (lighting only)" );
// HL2RPM: caustics on what lies under the water (the refraction views, composite_ps30)
static ConVar r_deferred_water_caustics( "r_deferred_water_caustics", "0.9", FCVAR_ARCHIVE, "Caustics under the water: strength (0 = off)", true, 0.0f, true, 4.0f );

void InitParmsComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_NO_DEFAULT( info.iAlphatestRef ) ||
		PARM_VALID( info.iAlphatestRef ) && PARM_FLOAT( info.iAlphatestRef ) == 0.0f )
		params[ info.iAlphatestRef ]->SetFloatValue( DEFAULT_ALPHATESTREF );

	PARM_INIT_FLOAT( info.iPhongScale, DEFAULT_PHONG_SCALE );
	PARM_INIT_INT( info.iPhongFresnel, 0 );

	PARM_INIT_FLOAT( info.iEnvmapContrast, 0.0f );
	PARM_INIT_FLOAT( info.iEnvmapSaturation, 1.0f );
	PARM_INIT_VEC3( info.iEnvmapTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iEnvmapFresnel, 0 );

	PARM_INIT_INT( info.iRimlightEnable, 0 );
	PARM_INIT_FLOAT( info.iRimlightExponent, 4.0f );
	PARM_INIT_FLOAT( info.iRimlightAlbedoScale, 0.0f );
	PARM_INIT_VEC3( info.iRimlightTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iRimlightModLight, 0 );

	PARM_INIT_VEC3( info.iSelfIllumTint, 1.0f, 1.0f, 1.0f );
	PARM_INIT_INT( info.iSelfIllumMaskInEnvmapAlpha, 0 );
	PARM_INIT_INT( info.iSelfIllumFresnelModulate, 0 );
}

void InitPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_DEFINED( info.iAlbedo ) )
		pShader->LoadTexture( info.iAlbedo );

	if ( PARM_DEFINED( info.iEnvmap ) )
		pShader->LoadCubeMap( info.iEnvmap );

	if ( PARM_DEFINED( info.iEnvmapMask ) )
		pShader->LoadTexture( info.iEnvmapMask );

	if ( PARM_DEFINED( info.iAlbedo2 ) )
		pShader->LoadTexture( info.iAlbedo2 );

	if ( PARM_DEFINED( info.iAlbedo3 ) )
		pShader->LoadTexture( info.iAlbedo3 );

	if ( PARM_DEFINED( info.iAlbedo4 ) )
		pShader->LoadTexture( info.iAlbedo4 );

	if ( PARM_DEFINED( info.iBlendmodulate ) )
		pShader->LoadTexture( info.iBlendmodulate );

	if ( PARM_DEFINED( info.iBlendmodulate2 ) )
		pShader->LoadTexture( info.iBlendmodulate2 );

	if ( PARM_DEFINED( info.iBlendmodulate3 ) )
		pShader->LoadTexture( info.iBlendmodulate3 );

	if ( PARM_DEFINED( info.iSelfIllumMask ) )
		pShader->LoadTexture( info.iSelfIllumMask );

	if ( PARM_DEFINED( info.iPomHeightmap ) )
		pShader->LoadTexture( info.iPomHeightmap );
}

void DrawPassComposite( const defParms_composite &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext )
{
	const bool bModel = info.bModel;
	const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();

	const bool bAlbedo = PARM_TEX( info.iAlbedo );
	const bool bAlbedo2 = !bModel && bAlbedo && PARM_TEX( info.iAlbedo2 );
	const bool bAlbedo3 = !bModel && bAlbedo && PARM_TEX( info.iAlbedo3 );
	const bool bAlbedo4 = !bModel && bAlbedo && PARM_TEX( info.iAlbedo4 );

	const bool bAlphatest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) && bAlbedo;
	const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) && bAlbedo && !bAlphatest;

	const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );

	const bool bUseSRGB = DEFCFG_USE_SRGB_CONVERSION != 0;
	const bool bPhongFresnel = PARM_SET( info.iPhongFresnel );

	const bool bEnvmap = PARM_TEX( info.iEnvmap );
	// HL2RPM: the envmap mask as the stock shaders take it - a mask texture, the bump map's alpha
	// ($normalmapalphaenvmapmask) or 1 - the base texture's alpha ($basealphaenvmapmask; see c35).
	// The base alpha as it was (not inverted, and for every material without a mask texture) made
	// the dull parts mirror: white Metrocops, a blue APC mirroring the light around them.
	const bool bNormalAlphaEnvmapMask = bEnvmap && !PARM_TEX( info.iEnvmapMask ) && IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK )
		&& PARM_VALID( info.iBumpmap ) && PARM_TEX( info.iBumpmap );
	const bool bEnvmapMask = bEnvmap && ( PARM_TEX( info.iEnvmapMask ) || bNormalAlphaEnvmapMask );
	const bool bEnvmapMask2 = bEnvmapMask && !bNormalAlphaEnvmapMask && PARM_TEX( info.iEnvmapMask2 );
	const bool bEnvmapFresnel = bEnvmap && PARM_SET( info.iEnvmapFresnel );
	const bool bEnvmapCorrection = !info.bModel && bEnvmap && info.iEnvmapParallax != -1 && !params[info.iEnvmapParallax]->MatrixIsIdentity();

	const bool bRimLight = PARM_SET( info.iRimlightEnable );
	const bool bRimLightModLight = bRimLight && PARM_SET( info.iRimlightModLight );
	const bool bBlendmodulate = bAlbedo2 && PARM_TEX( info.iBlendmodulate );
	const bool bBlendmodulate2 = bBlendmodulate && PARM_TEX( info.iBlendmodulate2 );
	const bool bBlendmodulate3 = bBlendmodulate && PARM_TEX( info.iBlendmodulate3 );

	const bool bSelfIllum = !bAlbedo2 && IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
	const bool bSelfIllumMaskInEnvmapMask = bSelfIllum && bEnvmapMask && PARM_SET( info.iSelfIllumMaskInEnvmapAlpha );
	const bool bSelfIllumMask = bSelfIllum && !bSelfIllumMaskInEnvmapMask && !bEnvmapMask && PARM_TEX( info.iSelfIllumMask );

	const bool bMultiBlend = PARM_SET( info.iMultiblend )
		&& bAlbedo && bAlbedo2 && bAlbedo3 && !bEnvmapMask && !bSelfIllumMask;

	const bool bNeedsFresnel = bPhongFresnel || bEnvmapFresnel;
	const bool bGBufferNormal = bEnvmap || bRimLight || bNeedsFresnel;
	const bool bWorldEyeVec = bGBufferNormal;
	// HL2RPM (PBR): the opaque materials that say what they are made of (hl2rpm_pbr.h)
	const bool bPBR = !bTranslucent && info.iMetalness != -1;
	// HL2RPM: the relief - the brush materials the G-buffer pass gives one (plain, bumped, with a
	// height map); the wind in a tree
	const bool bPOM = !bModel && !bIsDecal && !bTranslucent && !bAlbedo2 && !bMultiBlend
		&& PARM_VALID( info.iPomHeightmap ) && PARM_TEX( info.iPomHeightmap );
	const int iTreeSway = ( bModel && PARM_VALID( info.iTreeSway ) && PARM_DEFINED( info.iTreeSway ) ) ? PARM_INT( info.iTreeSway ) : 0;

	AssertMsgOnce( !(bTranslucent || bAlphatest) || !bAlbedo2,
		"blended albedo not supported by gbuffer pass!" );

	AssertMsgOnce( !PARM_TEX( info.iSelfIllumMask ) || !bEnvmapMask,
		"Can't use separate selfillum mask with envmap mask - use SELFILLUM_ENVMAPMASK_ALPHA instead." );

	AssertMsgOnce( PARM_SET( info.iMultiblend ) == bMultiBlend,
		"Multiblend forced off due to invalid usage! May cause vertexformat mis-matches between passes." );

	SHADOW_STATE
	{
		pShaderShadow->SetDefaultState();
		pShaderShadow->EnableSRGBWrite( bUseSRGB );

		if ( bIsDecal )
		{
			pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_DECAL );
		}

		if ( bNoCull )
		{
			pShaderShadow->EnableCulling( false );
		}

		int iVFmtFlags = VERTEX_POSITION;
		int iUserDataSize = 0;

		int *pTexCoordDim;
		int iTexCoordNum;
		GetTexcoordSettings( ( bModel && bIsDecal && bFastVTex ), bMultiBlend, iTexCoordNum, &pTexCoordDim );

		if ( bModel )
		{
			iVFmtFlags |= VERTEX_NORMAL;
			iVFmtFlags |= VERTEX_FORMAT_COMPRESSED;
		}
		else
		{
			if ( bAlbedo2 )
				iVFmtFlags |= VERTEX_COLOR;
		}

		pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, bUseSRGB );

		if ( bGBufferNormal )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
		}

		if ( bTranslucent )
		{
			pShader->EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}

		pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );

		// HL2RPM: main view depth, read when drawing a planar reflection (REFLECTVIEW)
		pShaderShadow->EnableTexture( SHADER_SAMPLER11, true );
		pShaderShadow->EnableSRGBRead( SHADER_SAMPLER11, false );

		if ( bEnvmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

			if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );

			if ( bEnvmapMask )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );

				if ( bAlbedo2 )
					pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
			}
		}
		else if ( bSelfIllumMask )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
		}

		if ( bAlbedo2 )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, bUseSRGB );

			if ( bBlendmodulate )
				pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
		}

		if ( bMultiBlend )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, bUseSRGB );

			if ( bAlbedo4 )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, bUseSRGB );
			}

			if ( bBlendmodulate )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER10, true );
			}
		}

		// HL2RPM: opaque surfaces write alpha like the stock shaders: 1, or in the refraction
		// view of a water the water fog amount the water shader reads back (composite_ps30)
		pShaderShadow->EnableAlphaWrites( !bTranslucent );
		pShaderShadow->EnableDepthWrites( !bTranslucent );

		pShader->DefaultFog();

		if ( bPOM )
		{
			// (the vertex shader turns the view vector into tangent space; the height map)
			iVFmtFlags |= VERTEX_NORMAL | VERTEX_TANGENT_SPACE;
			pShaderShadow->EnableTexture( SHADER_SAMPLER12, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER12, false );
		}

		pShaderShadow->VertexShaderVertexFormat( iVFmtFlags, iTexCoordNum, pTexCoordDim, iUserDataSize );

		DECLARE_STATIC_VERTEX_SHADER( composite_vs30 );
		SET_STATIC_VERTEX_SHADER_COMBO( MODEL, bModel );
		SET_STATIC_VERTEX_SHADER_COMBO( MORPHING_VTEX, bModel && bFastVTex );
		SET_STATIC_VERTEX_SHADER_COMBO( DECAL, bModel && bIsDecal );
		SET_STATIC_VERTEX_SHADER_COMBO( EYEVEC, bWorldEyeVec );
		SET_STATIC_VERTEX_SHADER_COMBO( BASETEXTURE2, bAlbedo2 && !bMultiBlend );
		SET_STATIC_VERTEX_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_VERTEX_SHADER_COMBO( MULTIBLEND, bMultiBlend );
		SET_STATIC_VERTEX_SHADER_COMBO( TREESWAY, iTreeSway > 0 );
		SET_STATIC_VERTEX_SHADER_COMBO( POM, bPOM );
		SET_STATIC_VERTEX_SHADER( composite_vs30 );

		DECLARE_STATIC_PIXEL_SHADER( composite_ps30 );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphatest );
		SET_STATIC_PIXEL_SHADER_COMBO( TRANSLUCENT, bTranslucent );
		SET_STATIC_PIXEL_SHADER_COMBO( READNORMAL, bGBufferNormal );
		SET_STATIC_PIXEL_SHADER_COMBO( NOCULL, bNoCull );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAP, bEnvmap );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK, bEnvmapMask );
		SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPFRESNEL, bEnvmapFresnel );
		SET_STATIC_PIXEL_SHADER_COMBO( PHONGFRESNEL, bPhongFresnel );
		SET_STATIC_PIXEL_SHADER_COMBO( RIMLIGHT, bRimLight );
		SET_STATIC_PIXEL_SHADER_COMBO( RIMLIGHTMODULATELIGHT, bRimLightModLight );
		SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2, bAlbedo2 && !bMultiBlend );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_PIXEL_SHADER_COMBO( MULTIBLEND, bMultiBlend );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM, bSelfIllum );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM_MASK, bSelfIllumMask );
		SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM_ENVMAP_ALPHA, bSelfIllumMaskInEnvmapMask );
		SET_STATIC_PIXEL_SHADER_COMBO( PARALLAXCORRECT, bEnvmapCorrection );
		SET_STATIC_PIXEL_SHADER( composite_ps30 );
	}
	DYNAMIC_STATE
	{
		Assert( pDeferredContext != NULL );

		if ( pDeferredContext->m_bMaterialVarsChanged || !pDeferredContext->HasCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE )
			|| building_cubemaps.GetBool() )
		{
			tmpBuf.Reset();

			if ( bAlphatest )
			{
				PARM_VALIDATE( info.iAlphatestRef );
				tmpBuf.SetPixelShaderConstant1( 0, PARM_FLOAT( info.iAlphatestRef ) );
			}

			if ( bAlbedo )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );
			else
				tmpBuf.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_GREY );

			if ( bEnvmap )
			{
				if ( building_cubemaps.GetBool() )
					tmpBuf.BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );
				else
				{
					if ( PARM_TEX( info.iEnvmap ) && !bModel )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER3, info.iEnvmap );
					else
						//tmpBuf.BindStandardTexture( SHADER_SAMPLER3, TEXTURE_LOCAL_ENV_CUBEMAP );
						tmpBuf.BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );
				}

				if ( bEnvmapMask )
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER4, bNormalAlphaEnvmapMask ? info.iBumpmap : info.iEnvmapMask );

				if ( bAlbedo2 )
				{
					if ( bEnvmapMask2 )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER7, info.iEnvmapMask2 );
					else
						tmpBuf.BindStandardTexture( SHADER_SAMPLER7, TEXTURE_WHITE );
				}

				tmpBuf.SetPixelShaderConstant( 5, info.iEnvmapTint );

				float fl6[4] = { 0 };
				fl6[0] = PARM_FLOAT( info.iEnvmapSaturation );
				fl6[1] = PARM_FLOAT( info.iEnvmapContrast );
				tmpBuf.SetPixelShaderConstant( 6, fl6 );
			}

			if ( bNeedsFresnel )
			{
				tmpBuf.SetPixelShaderConstant( 7, info.iFresnelRanges );
			}

			if ( bRimLight )
			{
				float fl9[4] = { 0 };
				fl9[0] = PARM_FLOAT( info.iRimlightExponent );
				fl9[1] = PARM_FLOAT( info.iRimlightAlbedoScale );
				tmpBuf.SetPixelShaderConstant( 9, fl9 );
			}

			if ( bAlbedo2 )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER5, info.iAlbedo2 );

				if ( bBlendmodulate )
				{
					tmpBuf.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, info.iBlendmodulateTransform );
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER6, info.iBlendmodulate );
				}
			}

			if ( bMultiBlend )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER7, info.iAlbedo3 );

				if ( bAlbedo4 )
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER8, info.iAlbedo4 );
				else
					tmpBuf.BindStandardTexture( SHADER_SAMPLER8, TEXTURE_WHITE );

				if ( bBlendmodulate )
				{
					tmpBuf.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, info.iBlendmodulateTransform2 );
					tmpBuf.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_5, info.iBlendmodulateTransform3 );

					if ( bBlendmodulate2 )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER9, info.iBlendmodulate2 );
					else
						tmpBuf.BindStandardTexture( SHADER_SAMPLER9, TEXTURE_BLACK );

					if ( bBlendmodulate3 )
						tmpBuf.BindTexture( pShader, SHADER_SAMPLER10, info.iBlendmodulate3 );
					else
						tmpBuf.BindStandardTexture( SHADER_SAMPLER10, TEXTURE_BLACK );
				}
			}

			if ( bSelfIllum && bSelfIllumMask )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER4, info.iSelfIllumMask );
			}

			if ( bPOM )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER12, info.iPomHeightmap );

			tmpBuf.SetPixelShaderConstant1( 4, PARM_FLOAT( info.iPhongScale ) );

			if ( bEnvmapCorrection )
			{
				tmpBuf.SetPixelShaderConstant( 11, params[info.iEnvmapOrigin]->GetVecValue() );
				tmpBuf.SetPixelShaderConstant( 12, params[info.iEnvmapParallax]->GetMatrixValue().Base(), 4 );
			}

			tmpBuf.End();

			pDeferredContext->SetCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE, tmpBuf.Copy() );
		}

		pShaderAPI->SetDefaultState();

		if ( bModel && bFastVTex )
			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

		DECLARE_DYNAMIC_VERTEX_SHADER( composite_vs30 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, (bModel && pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, (bModel && pShaderAPI->IsHWMorphingEnabled()) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER( composite_vs30 );

		// HL2RPM: planar reflection views (water, glass) reproject into the main view's light buffer
		const bool bReflectView = GetDeferredExt()->IsReflectionView();

		DECLARE_DYNAMIC_PIXEL_SHADER( composite_ps30 );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
		SET_DYNAMIC_PIXEL_SHADER_COMBO( REFLECTVIEW, bReflectView ? 1 : 0 );
		SET_DYNAMIC_PIXEL_SHADER( composite_ps30 );

		if ( bModel && bFastVTex )
		{
			bool bUnusedTexCoords[3] = { false, true, !pShaderAPI->IsHWMorphingEnabled() || !bIsDecal };
			pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
		}

		pShaderAPI->ExecuteCommandBuffer( pDeferredContext->GetCommands( CDeferredPerMaterialContextData::DEFSTAGE_COMPOSITE ) );

		if ( r_deferred_debug_lighting_only.GetBool() )
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );

		// HL2RPM: the light buffer texel size and the fog belong to the view, not to the
		// material: they used to live in the command buffer above, which is built once per
		// material. A material first drawn in a water reflection/refraction view (1024x512
		// target) kept that texel size, and the main view then read the light buffer at
		// x1.56 / x1.76 - walls showed "mirror images" of the lighting of other objects
		// (and the water views got the main view's fog, or the other way round).
		{
			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			float fl1[4] = { 1.0f / Max( viewport.m_nWidth, 1 ), 1.0f / Max( viewport.m_nHeight, 1 ), 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 1, fl1, 1, true );
			pShaderAPI->SetPixelShaderFogParams( 2 );
		}

		if ( bGBufferNormal )
			pShader->BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );

		pShader->BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_LightAccum() );

		// the G-buffer depth: forward and scale of WriteDepth (MSAA light lookup and the
		// reflection views); w 0 turns the MSAA lookup off
		{
			const float *pFwd = GetDeferredExt()->GetForwardBase();
			const bool bLookup = bReflectView || ( r_deferred_msaa_light.GetBool() && !bTranslucent );
			float flFwd[4] = { pFwd[0], pFwd[1], pFwd[2], bLookup ? GetDeferredExt()->GetZScale() : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 21, flFwd, 1, true );
			pShader->BindTexture( SHADER_SAMPLER11, GetDeferredExt()->GetTexture_Depth() );
		}

		// HL2RPM (PBR): per draw - r_deferred_pbr switches it without the materials being loaded anew
		{
			const bool bPBROn = bPBR && r_deferred_pbr.GetBool();
			const float flPBR[4] = { bPBROn ? clamp( PARM_FLOAT( info.iMetalness ), 0.0f, 1.0f ) : 0.0f,
				bPBROn ? 0.04f * clamp( PARM_FLOAT( info.iSpecular ), 0.0f, 4.0f ) : 0.0f,
				r_deferred_pbr_cavity.GetFloat(), bPBROn ? 1.0f + clamp( r_deferred_pbr_debug.GetInt(), 0, 2 ) : 0.0f };
			// (the cubemap of $envmap: a model's is never bound; a brush's may be the engine's
			// default one - the patch of a map without built cubemaps, or a missing texture)
			bool bEnvFallback = false;
			if ( bEnvmap && r_deferred_envmap_fallback.GetFloat() > 0.0f )
			{
				bEnvFallback = bModel || !mat_hl2rpm_map_cubemaps.GetBool();
				if ( !bEnvFallback )
				{
					ITexture *pEnvmap = params[ info.iEnvmap ]->IsTexture() ? params[ info.iEnvmap ]->GetTextureValue() : NULL;
					bEnvFallback = !pEnvmap || pEnvmap->IsError() || V_stristr( pEnvmap->GetName(), "defaultcubemap" ) != NULL;
				}
			}
			const float flPBR2[4] = { 1.0f / r_deferred_pbr_cavity_white.GetFloat(), r_deferred_pbr_metal_diffuse.GetFloat(),
				r_deferred_pbr_specular.GetFloat(), bEnvFallback ? r_deferred_envmap_fallback.GetFloat() : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 26, flPBR, 1, true );
			pShaderAPI->SetPixelShaderConstant( 27, flPBR2, 1, true );
			const float flLimit[4] = { r_deferred_light_limit.GetFloat(), r_deferred_light_limit_specular.GetFloat(), 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 28, flLimit, 1, true );
		}

		// HL2RPM: the relief of the material (the same constant as the G-buffer pass's) and the
		// light through a thin one - always: nothing else sets these registers back
		{
			float flPOM[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( bPOM )
				PBR_GetReliefConstants( PARM_VALID( info.iPomScale ) ? PARM_FLOAT( info.iPomScale ) : 0.0f, bReflectView, flPOM );
			pShaderAPI->SetPixelShaderConstant( 32, flPOM, 1, true );

			float flTransmit[4], flTransmitSun[4];
			PBR_GetTransmissionConstants( ( !bTranslucent && PARM_VALID( info.iTransmission ) && PARM_DEFINED( info.iTransmission ) )
				? PARM_FLOAT( info.iTransmission ) : 0.0f, flTransmit, flTransmitSun );
			pShaderAPI->SetPixelShaderConstant( 33, flTransmit, 1, true );
			pShaderAPI->SetPixelShaderConstant( 34, flTransmitSun, 1, true );

			// where the envmap mask comes from (composite_ps30 c35): x, y scale and bias of the base
			// alpha, z the mask texture's alpha instead of its red
			float flEnvMask[4] = { 0.0f, 1.0f, bNormalAlphaEnvmapMask ? 1.0f : 0.0f, 0.0f };
			if ( !bEnvmapMask && IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) )
				flEnvMask[0] = -1.0f;
			pShaderAPI->SetPixelShaderConstant( 35, flEnvMask, 1, true );
		}
		if ( iTreeSway > 0 )
			PBR_SetTreeSwayConstants( pShaderAPI, iTreeSway );

		if ( bReflectView )
		{
			pShaderAPI->SetPixelShaderConstant( 16, GetDeferredExt()->GetMainViewToScreenTexBase(), 4, true );
			pShaderAPI->SetPixelShaderConstant( 20, GetDeferredExt()->GetOriginBase(), 1, true );

			// what the main view doesn't see gets the sky's light (as strong as the global light
			// pass makes it, see GetGISkyBoost) plus the sun's by the facing of the surface - a part
			// of it: whether the point lies in a shadow isn't known
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
			const float flSunUp = light.bEnabled ? clamp( ( light.vecLight.z + 0.02f ) / 0.08f, 0.0f, 1.0f ) : 0.0f;
			const float flSkyBoost = GetGISkyBoost( GetDeferredExt()->GetGIData(), GetDeferredExt()->GetWeatherData() );
			float flFallback[4], flFallbackSun[4];
			for ( int i = 0; i < 3; i++ )
			{
				flFallback[i] = ( light.ambh[i] * 0.75f + light.ambl[i] * 0.25f ) * flSkyBoost;
				flFallbackSun[i] = light.diff[i] * 0.6f * flSunUp;
			}
			flFallback[3] = r_deferred_reflect_debug.GetBool() ? 1.0f : 0.0f;
			flFallbackSun[3] = 0.0f;
			const float flFallbackDir[4] = { light.vecLight.x, light.vecLight.y, light.vecLight.z, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 22, flFallback, 1, true );
			pShaderAPI->SetPixelShaderConstant( 24, flFallbackDir, 1, true );
			pShaderAPI->SetPixelShaderConstant( 25, flFallbackSun, 1, true );
		}

		// HL2RPM: the fog distance is measured from the camera of the view being drawn. The
		// deferred origin is the main view's: in the 3D skybox (its own coordinates, fog
		// distances / skybox scale) the skybox city was ~16000 units "away" from it and
		// drowned in the fog completely - flat fog-colored silhouettes.
		{
			float vCameraPos[4] = { 0, 0, 0, 1 };
			pShaderAPI->GetWorldSpaceCameraPosition( vCameraPos );
			vCameraPos[3] = 1.0f;
			pShaderAPI->SetPixelShaderConstant( 3, vCameraPos, 1, true );
		}

		// HL2RPM: caustics under the water (refraction view, see composite_ps30)
		{
			const float flStrength = r_deferred_water_caustics.GetFloat();
			// (w: the albedo's mip bias while TAA jitters the frame - textures stay crisp)
			float flCaustics[4] = { flStrength, pShaderAPI->CurrentTime() * 0.6f, 1.0f / 48.0f,
				GetDeferredExt()->GetTAAData().vecParams3.y };
			pShaderAPI->SetPixelShaderConstant( 23, flCaustics, 1, true );
		}

		if ( bWorldEyeVec )
		{
			float vEyepos[4] = {0,0,0,0};
			pShaderAPI->GetWorldSpaceCameraPosition( vEyepos );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vEyepos );
		}

		if ( bRimLight )
		{
			pShaderAPI->SetPixelShaderConstant( 8, params[ info.iRimlightTint ]->GetVecValue() );
		}

		if ( bSelfIllum )
		{
			pShaderAPI->SetPixelShaderConstant( 10, params[ info.iSelfIllumTint ]->GetVecValue() );
		}

		// HL2RPM: the cubemaps are baked once (by day); scale the reflection with how bright
		// the world is now, or glass and metal keep reflecting a sunny sky at night
		if ( bEnvmap )
		{
			// (not the light at the pixel that stands in for a missing cubemap: it is the light of now)
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const bool bLightEnv = r_deferred_envmap_fallback.GetFloat() > 0.0f && ( bModel || !mat_hl2rpm_map_cubemaps.GetBool() );
			const float flEnvScale = ( w.bEnabled && !bLightEnv ) ? w.vecSkyZenith.w : 1.0f;
			const float *pTint = params[ info.iEnvmapTint ]->GetVecValue();
			float flTint[4] = { pTint[0] * flEnvScale, pTint[1] * flEnvScale, pTint[2] * flEnvScale, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 5, flTint, 1, true );
		}

		// FIX: Engine's cLightScale lives in pixel shader register c30
		// (PSREG_LIGHT_SCALE / TONE_MAPPING_SCALE_PSH_CONSTANT).
		// Stale data from prior draw calls can leave c30 at zero, which
		// multiplies the entire composite to black (night-darkness bug).
		// Force a neutral light-scale so LINEAR_LIGHT_SCALE is always 1.
		{
			float flNeutralLightScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			pShaderAPI->SetPixelShaderConstant( 30, flNeutralLightScale );
		}
	}

	pShader->Draw();
}

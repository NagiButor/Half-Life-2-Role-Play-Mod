
#include "deferred_includes.h"
#include "hl2rpm_pbr.h"

#include "deferred_decal_vs30.inc"
#include "hl2rpm_itempreview_ps30.inc"

#include "tier0/memdbgon.h"


BEGIN_VS_SHADER( DEFERRED_MODEL, "" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )

		SHADER_PARAM( LITFACE, SHADER_PARAM_TYPE_BOOL, "0", "" )

		SHADER_PARAM( PHONG_SCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_MAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( PHONG_FRESNEL, SHADER_PARAM_TYPE_BOOL, "", "" )

		SHADER_PARAM( FRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "", "" )
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "" )

		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_env", "envmap" )
		SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "envmap tint" )
		SHADER_PARAM( ENVMAPCONTRAST, SHADER_PARAM_TYPE_FLOAT, "0.0", "contrast 0 == normal 1 == color*color" )
		SHADER_PARAM( ENVMAPSATURATION, SHADER_PARAM_TYPE_FLOAT, "1.0", "saturation 0 == greyscale 1 == normal" )
		SHADER_PARAM( ENVMAPFRESNEL, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( ENVMAPMASK, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_envmask", "envmap mask" )

		SHADER_PARAM( RIMLIGHT, SHADER_PARAM_TYPE_BOOL, "0", "enables rim lighting" )
		SHADER_PARAM( RIMLIGHTEXPONENT, SHADER_PARAM_TYPE_FLOAT, "4.0", "Exponent for rim lights" )
		SHADER_PARAM( RIMLIGHTALBEDOSCALE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Albedo influence on rim light" )
		SHADER_PARAM( RIMLIGHTTINT, SHADER_PARAM_TYPE_VEC3, "[1 1 1]", "Tint for rim lights" )
		SHADER_PARAM( RIMLIGHT_MODULATE_BY_LIGHT, SHADER_PARAM_TYPE_BOOL, "0", "Modulate rimlight by lighting." )
		SHADER_PARAM( RIMLIGHTBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "Boost for the rim light (of the light around, as the stock shaders take it)" )

		SHADER_PARAM( SELFILLUMTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Self-illumination tint" )
		SHADER_PARAM( SELFILLUM_ENVMAPMASK_ALPHA, SHADER_PARAM_TYPE_BOOL, "0", "defines that self illum value comes from env map mask alpha" )
		SHADER_PARAM( SELFILLUMFRESNEL, SHADER_PARAM_TYPE_BOOL, "0", "Self illum fresnel" )
		SHADER_PARAM( SELFILLUMMASK, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "If we bind a texture here, it overrides base alpha (if any) for self illum" )

		// HL2RPM: blended, but composited here (lit by the light buffer behind it) instead of
		// falling back to VertexLitGeneric (materialsystem_passthru.cpp sets it)
		SHADER_PARAM( DEFERREDTRANSLUCENT, SHADER_PARAM_TYPE_BOOL, "0", "" )

		// HL2RPM (PBR, hl2rpm_pbr.h): what the material may say, and what the stock material says
		SHADER_PARAM( PBR_ROUGHNESS, SHADER_PARAM_TYPE_FLOAT, "-1", "0 (a mirror) .. 1 (matte); not set = by what the material is made of" )
		SHADER_PARAM( PBR_METALNESS, SHADER_PARAM_TYPE_FLOAT, "-1", "0 .. 1" )
		SHADER_PARAM( PBR_SPECULAR, SHADER_PARAM_TYPE_FLOAT, "-1", "the mirror reflection of the non-metal, 1 = 4 %" )
		SHADER_PARAM( PBR_GLOSSMASK, SHADER_PARAM_TYPE_BOOL, "", "the alpha of the bump map scales the gloss" )
		SHADER_PARAM( SURFACEPROP, SHADER_PARAM_TYPE_STRING, "", "" )
		SHADER_PARAM( NORMALMAPALPHAENVMAPMASK, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( BASEALPHAENVMAPMASK, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( PHONG, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( BASEMAPALPHAPHONGMASK, SHADER_PARAM_TYPE_BOOL, "", "" )

		// HL2RPM: the wind in a tree (1 = the trunk leans, 2 = and the leaves shiver), the light
		// through leaves
		SHADER_PARAM( TREESWAY, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( PBR_TRANSMISSION, SHADER_PARAM_TYPE_FLOAT, "0", "0 .. 1: how much light shines through (leaves, cloth)" )

		// HL2RPM: the detail texture of the stock material (composite: blend mode 0, "mod2x")
		SHADER_PARAM( DETAIL, SHADER_PARAM_TYPE_TEXTURE, "", "detail texture" )
		SHADER_PARAM( DETAILSCALE, SHADER_PARAM_TYPE_FLOAT, "4", "repeats of the detail texture per repeat of the base texture" )
		SHADER_PARAM( DETAILBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1", "how much of the detail texture" )
		SHADER_PARAM( DETAILBLENDMODE, SHADER_PARAM_TYPE_INTEGER, "0", "0 = mod2x (the only one done)" )

	END_SHADER_PARAMS

	void SetupParmsGBuffer( defParms_gBuffer &p )
	{
		p.bModel = true;

		p.iAlbedo = BASETEXTURE;
		p.iBumpmap = BUMPMAP;
		p.iPhongmap = PHONG_MAP;

		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iLitface = LITFACE;
		p.iPhongExp = PHONG_EXP;
		p.iGlossMask = PBR_GLOSSMASK;
		p.iTreeSway = TREESWAY;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iTreeSway = TREESWAY;
		p.iLitface = LITFACE;
	}

	void SetupParmsComposite( defParms_composite &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;

		p.iEnvmap = ENVMAP;
		p.iEnvmapMask = ENVMAPMASK;
		p.iBumpmap = BUMPMAP;
		p.iEnvmapTint = ENVMAPTINT;
		p.iEnvmapContrast = ENVMAPCONTRAST;
		p.iEnvmapSaturation = ENVMAPSATURATION;
		p.iEnvmapFresnel = ENVMAPFRESNEL;

		p.iRimlightEnable = RIMLIGHT;
		p.iRimlightExponent = RIMLIGHTEXPONENT;
		p.iRimlightAlbedoScale = RIMLIGHTALBEDOSCALE;
		p.iRimlightTint = RIMLIGHTTINT;
		p.iRimlightModLight = RIMLIGHT_MODULATE_BY_LIGHT;
		p.iRimlightBoost = RIMLIGHTBOOST;

		p.iAlphatestRef = ALPHATESTREFERENCE;

		p.iPhongScale = PHONG_SCALE;
		p.iPhongFresnel = PHONG_FRESNEL;
		p.iMetalness = PBR_METALNESS;
		p.iSpecular = PBR_SPECULAR;
		p.iTreeSway = TREESWAY;
		p.iTransmission = PBR_TRANSMISSION;

		p.iSelfIllumTint = SELFILLUMTINT;
		p.iSelfIllumMaskInEnvmapAlpha = SELFILLUM_ENVMAPMASK_ALPHA;
		p.iSelfIllumFresnelModulate = SELFILLUMFRESNEL;
		p.iSelfIllumMask = SELFILLUMMASK;

		p.iFresnelRanges = FRESNELRANGES;

		p.iDetail = DETAIL;
		p.iDetailScale = DETAILSCALE;
		p.iDetailBlendFactor = DETAILBLENDFACTOR;
		p.iDetailBlendMode = DETAILBLENDMODE;
	}

	bool DrawToGBuffer( IMaterialVar **params )
	{
#if DEFCFG_DEFERRED_SHADING == 1
		return true;
#else
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );

		return !bTranslucent && !bIsDecal;
#endif
	}

	// HL2RPM: the model alone, on the card of an item (hl2rpm_itempreview_ps30.fxc): lit by itself,
	// there is no scene around it - no G-buffer, no light buffer
	void DrawPreview( IMaterialVar **params, IShaderShadow *pShaderShadow, IShaderDynamicAPI *pShaderAPI,
		VertexCompressionType_t vertexCompression )
	{
		const bool bBump = params[ BUMPMAP ]->IsTexture();
		const bool bBlend = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );
		const bool bBase = params[ BASETEXTURE ]->IsTexture();
		const bool bSelfIllum = IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
		const bool bEnvmapMask = params[ ENVMAPMASK ]->IsDefined() && params[ ENVMAPMASK ]->IsTexture();
		const bool bSelfIllumMask = bSelfIllum && params[ SELFILLUMMASK ]->IsDefined() && params[ SELFILLUMMASK ]->IsTexture();

		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableSRGBWrite( true );
			pShaderShadow->EnableAlphaWrites( true );
			if ( bBlend )
			{
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
				pShaderShadow->EnableDepthWrites( false );
			}
			if ( IS_FLAG_SET( MATERIAL_VAR_NOCULL ) )
				pShaderShadow->EnableCulling( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );

			int pTexCoordDim[3] = { 2, 0, 3 };
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED, 1, pTexCoordDim, 0 );

			DECLARE_STATIC_VERTEX_SHADER( deferred_decal_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, 0 );
			SET_STATIC_VERTEX_SHADER( deferred_decal_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_itempreview_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP, bBump );
			SET_STATIC_PIXEL_SHADER( hl2rpm_itempreview_ps30 );

			pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			if ( g_pHardwareConfig->HasFastVertexTextures() )
				SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

			DECLARE_DYNAMIC_VERTEX_SHADER( deferred_decal_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 ? 1 : 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() ? 1 : 0 );
			SET_DYNAMIC_VERTEX_SHADER( deferred_decal_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_itempreview_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_itempreview_ps30 );

			if ( g_pHardwareConfig->HasFastVertexTextures() )
			{
				bool bUnusedTexCoords[3] = { false, false, !pShaderAPI->IsHWMorphingEnabled() };
				pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
			}

			if ( bBase )
				BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			if ( bBump )
				BindTexture( SHADER_SAMPLER1, BUMPMAP );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_NORMALMAP_FLAT );
			if ( bSelfIllumMask )
				BindTexture( SHADER_SAMPLER2, SELFILLUMMASK );
			else if ( bEnvmapMask )
				BindTexture( SHADER_SAMPLER2, ENVMAPMASK );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );

			float vEye[4] = { 0, 0, 0, 0 };
			pShaderAPI->GetWorldSpaceCameraPosition( vEye );
			pShaderAPI->SetPixelShaderConstant( 0, vEye, 1, true );

			// (the vertex shader's G-buffer depth goes unused)
			const float vForward[4] = { 1, 0, 0, 0 }, vScale[4] = { 1, 0, 0, 0 };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vEye );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, vForward );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, vScale );

			const bool bGlossMask = bBump && params[ PBR_GLOSSMASK ]->GetIntValue() != 0;
			const float c1[4] = { 1.0f - clamp( params[ PHONG_EXP ]->GetFloatValue(), 0.0f, 1.0f ),
				clamp( params[ PBR_METALNESS ]->GetFloatValue(), 0.0f, 1.0f ),
				0.04f * clamp( params[ PBR_SPECULAR ]->GetFloatValue(), 0.0f, 4.0f ),
				bGlossMask ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 1, c1, 1, true );

			float c2[4] = { 0, 0, 0, 0 };
			if ( bSelfIllum )
			{
				params[ SELFILLUMTINT ]->GetVecValue( c2, 3 );
				const bool bInMaskAlpha = !bSelfIllumMask && bEnvmapMask && params[ SELFILLUM_ENVMAPMASK_ALPHA ]->GetIntValue() != 0;
				c2[3] = bInMaskAlpha ? 3.0f : bSelfIllumMask ? 2.0f : 1.0f;
			}
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1, true );

			// the mask of the reflections, as the stock shaders take it: 1 - the base alpha, or the
			// base alpha for phong, or the alpha of the bump map
			const bool bBaseAlphaEnvmap = IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK );
			const bool bBaseAlphaPhong = params[ BASEMAPALPHAPHONGMASK ]->GetIntValue() != 0;
			const bool bAlphaUsed = bSelfIllum && !bSelfIllumMask;
			const float flRef = params[ ALPHATESTREFERENCE ]->GetFloatValue();
			const float c3[4] = { IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) ? ( flRef > 0.0f ? flRef : 0.5f ) : 0.0f,
				( bBaseAlphaEnvmap && !bAlphaUsed ) ? 1.0f : ( bBaseAlphaPhong && !bAlphaUsed ) ? 0.0f : 1.0f,
				( bBaseAlphaEnvmap && !bAlphaUsed ) ? -1.0f : ( bBaseAlphaPhong && !bAlphaUsed ) ? 1.0f : 0.0f,
				( bBump && IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) ) ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 3, c3, 1, true );

			const Vector vecRim = pShaderAPI->GetVectorRenderingParameter( VECTOR_RENDERPARM_HL2RPM_PREVIEW_RIM );
			const float c4[4] = { vecRim.x, vecRim.y, vecRim.z, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 4, c4, 1, true );

			const float c5[4] = { ( bEnvmapMask && !bSelfIllumMask ) ? 1.0f : 0.0f, bBlend ? 1.0f : 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 5, c5, 1, true );
		}
		Draw();
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );

		if ( g_pHardwareConfig->HasFastVertexTextures() )
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );

		// HL2RPM (PBR): roughness, metalness and specular level of what the material is made of
		{
			pbrParms_t pbr;
			pbr.bModel = true;
			pbr.iRoughness = PBR_ROUGHNESS;
			pbr.iMetalness = PBR_METALNESS;
			pbr.iSpecular = PBR_SPECULAR;
			pbr.iGlossMask = PBR_GLOSSMASK;
			pbr.iGloss = PHONG_EXP;
			pbr.iSurfaceProp = SURFACEPROP;
			pbr.iBumpmap = BUMPMAP;
			pbr.iEnvmap = ENVMAP;
			pbr.iEnvmapTint = ENVMAPTINT;
			pbr.iNormalAlphaEnvmapMask = NORMALMAPALPHAENVMAPMASK;
			pbr.iBaseAlphaEnvmapMask = BASEALPHAENVMAPMASK;
			pbr.iPhong = PHONG;
			pbr.iPhongExponent = PHONGEXPONENT;
			pbr.iPhongBoost = PHONGBOOST;
			pbr.iBaseAlphaPhongMask = BASEMAPALPHAPHONGMASK;
			InitParmsPBR( pbr, params, pMaterialName );
		}

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitParmsGBuffer( parms_gbuffer, this, params );

			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitParmsShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitParmsComposite( parms_composite, this, params );
	}

	SHADER_INIT
	{
		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitPassGBuffer( parms_gbuffer, this, params );

			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitPassShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitPassComposite( parms_composite, this, params );
	}

	SHADER_FALLBACK
	{
		if ( !GetDeferredExt()->IsDeferredLightingEnabled() )
			return "VertexlitGeneric";

		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );

		if ( bTranslucent && !bIsDecal && !( params[DEFERREDTRANSLUCENT]->IsDefined() && params[DEFERREDTRANSLUCENT]->GetIntValue() != 0 ) )
			return "VertexlitGeneric";

		return 0;
	}

	SHADER_DRAW
	{
		if ( pShaderAPI != NULL && *pContextDataPtr == NULL )
			*pContextDataPtr = new CDeferredPerMaterialContextData();

		CDeferredPerMaterialContextData *pDefContext = reinterpret_cast< CDeferredPerMaterialContextData* >(*pContextDataPtr);

		const int iDeferredRenderStage = pShaderAPI ?
			pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		Assert( pShaderAPI == NULL ||
			iDeferredRenderStage != DEFERRED_RENDER_STAGE_INVALID );

		if ( bDrawToGBuffer )
		{
			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_GBUFFER )
			{
				defParms_gBuffer parms_gbuffer;
				SetupParmsGBuffer( parms_gbuffer );
				DrawPassGBuffer( parms_gbuffer, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				Draw(false);

			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_SHADOWPASS )
			{
				defParms_shadow parms_shadow;
				SetupParmsShadow( parms_shadow );
				DrawPassShadowPass( parms_shadow, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				Draw(false);
		}

#if ( DEFCFG_DEFERRED_SHADING == 0 )
		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_COMPOSITION )
		{
			defParms_composite parms_composite;
			SetupParmsComposite( parms_composite );
			DrawPassComposite( parms_composite, this, params, pShaderShadow, pShaderAPI,
				vertexCompression, pDefContext );
		}
		else
			Draw(false);
#endif

		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_PREVIEW )
			DrawPreview( params, pShaderShadow, pShaderAPI, vertexCompression );
		else
			Draw(false);

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

END_SHADER

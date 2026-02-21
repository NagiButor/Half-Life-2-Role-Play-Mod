// DEFERRED_TEETH shader
// Wraps the SDK_Teeth shader to participate in the deferred lighting pipeline.
// GBuffer pass: outputs normals and depth
// Shadow pass: outputs depth for shadow casting
// Composite pass: renders teeth with deferred lighting from _rt_LightAccum

#include "deferred_includes.h"

#include "gbuffer_vs30.inc"
#include "gbuffer_ps30.inc"

#include "shadowpass_vs30.inc"
#include "shadowpass_ps30.inc"

// Reuse the existing Teeth vertex shader for composite pass
#include "SDK_teeth_vs30.inc"
// New deferred composite pixel shader
#include "deferred_teeth_composite_ps30.inc"

#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DEFERRED_TEETH, "Deferred lighting version of SDK_Teeth" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( ILLUMFACTOR, SHADER_PARAM_TYPE_FLOAT, "1", "Amount to darken or brighten the teeth" )
		SHADER_PARAM( FORWARD, SHADER_PARAM_TYPE_VEC3, "[1 0 0]", "Forward direction vector for teeth lighting" )
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "bump map" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "100", "phong exponent" )
		SHADER_PARAM( INTRO, SHADER_PARAM_TYPE_BOOL, "0", "is teeth in the ep1 intro" )
		SHADER_PARAM( ENTITYORIGIN, SHADER_PARAM_TYPE_VEC3, "0.0", "center of the model in world space" )
		SHADER_PARAM( WARPPARAM, SHADER_PARAM_TYPE_FLOAT, "0.0", "animation param between 0 and 1" )

		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "" )
	END_SHADER_PARAMS

	void SetupParmsGBuffer( defParms_gBuffer &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iPhongExp = PHONG_EXP;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );

		if ( g_pHardwareConfig->HasFastVertexTextures() )
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );

		if ( !params[INTRO]->IsDefined() )
			params[INTRO]->SetIntValue( 0 );

		if ( !params[ILLUMFACTOR]->IsDefined() )
			params[ILLUMFACTOR]->SetFloatValue( 1.0f );

		if ( !params[FORWARD]->IsDefined() )
		{
			float vForward[3] = { 1.0f, 0.0f, 0.0f };
			params[FORWARD]->SetVecValue( vForward, 3 );
		}

		defParms_gBuffer parms_gbuffer;
		SetupParmsGBuffer( parms_gbuffer );
		InitParmsGBuffer( parms_gbuffer, this, params );

		defParms_shadow parms_shadow;
		SetupParmsShadow( parms_shadow );
		InitParmsShadowPass( parms_shadow, this, params );
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );

		defParms_gBuffer parms_gbuffer;
		SetupParmsGBuffer( parms_gbuffer );
		InitPassGBuffer( parms_gbuffer, this, params );

		defParms_shadow parms_shadow;
		SetupParmsShadow( parms_shadow );
		InitPassShadowPass( parms_shadow, this, params );
	}

	SHADER_FALLBACK
	{
		if ( !GetDeferredExt()->IsDeferredLightingEnabled() )
			return "SDK_Teeth";

		return 0;
	}

	SHADER_DRAW
	{
		if ( pShaderAPI != NULL && *pContextDataPtr == NULL )
			*pContextDataPtr = new CDeferredPerMaterialContextData();

		CDeferredPerMaterialContextData *pDefContext = reinterpret_cast< CDeferredPerMaterialContextData* >( *pContextDataPtr );

		const int iDeferredRenderStage = pShaderAPI ?
			pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;

		Assert( pShaderAPI == NULL ||
			iDeferredRenderStage != DEFERRED_RENDER_STAGE_INVALID );

		// --- Pass 1: GBuffer ---
		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_GBUFFER )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			DrawPassGBuffer( parms_gbuffer, this, params, pShaderShadow, pShaderAPI,
				vertexCompression, pDefContext );
		}
		else
			Draw( false );

		// --- Pass 2: Shadow ---
		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_SHADOWPASS )
		{
			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			DrawPassShadowPass( parms_shadow, this, params, pShaderShadow, pShaderAPI,
				vertexCompression, pDefContext );
		}
		else
			Draw( false );

		// --- Pass 3: Composite (deferred lighting) ---
#if ( DEFCFG_DEFERRED_SHADING == 0 )
		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_COMPOSITION )
		{
			DrawCompositeTeeth( params, pShaderAPI, pShaderShadow, vertexCompression, pDefContext );
		}
		else
			Draw( false );
#endif

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

	void DrawCompositeTeeth( IMaterialVar **params,
		IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow,
		VertexCompressionType_t vertexCompression,
		CDeferredPerMaterialContextData *pDefContext )
	{
		const bool bUseSRGB = DEFCFG_USE_SRGB_CONVERSION != 0;

		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableSRGBWrite( bUseSRGB );

			// Samplers: s0=Base, s3=LightAccum
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, false );

			// Vertex format: position + normal + compressed + 1 texcoord
			int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
			int nTexCoordCount = 1;
			int userDataSize = 0;
			pShaderShadow->VertexShaderVertexFormat( flags, nTexCoordCount, NULL, userDataSize );

			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableDepthWrites( true );

			DefaultFog();

			// Vertex shader: reuse Teeth VS
			DECLARE_STATIC_VERTEX_SHADER( sdk_teeth_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( INTRO, params[INTRO]->GetIntValue() ? 1 : 0 );
			SET_STATIC_VERTEX_SHADER( sdk_teeth_vs30 );

			// Pixel shader: deferred composite
			DECLARE_STATIC_PIXEL_SHADER( deferred_teeth_composite_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_teeth_composite_ps30 );
		}
		DYNAMIC_STATE
		{
			// Bind textures
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_LightAccum() );

			// Set teeth-specific vertex shader constants
			// VERTEX_SHADER_SHADER_SPECIFIC_CONST_0: forward direction (xyz) + illumfactor (w)
			Vector4D lighting;
			params[FORWARD]->GetVecValue( lighting.Base(), 3 );
			lighting[3] = params[ILLUMFACTOR]->GetFloatValue();
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, lighting.Base() );

			SetAmbientCubeDynamicStateVertexShader();

			// Vertex shader dynamic combos
			DECLARE_DYNAMIC_VERTEX_SHADER( sdk_teeth_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DYNAMIC_LIGHT, 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( STATIC_LIGHT, 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER( sdk_teeth_vs30 );

			if ( pShaderAPI->IsHWMorphingEnabled() )
			{
				SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_6,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_7, SHADER_VERTEXTEXTURE_SAMPLER0 );
			}

			// Pixel shader constants

			// c0: fullscreen texel size for sampling light accum
			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			float fl0[4] = { 1.0f / viewport.m_nWidth, 1.0f / viewport.m_nHeight, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 0, fl0, 1 );

			// Fog
			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );

			float vEyePos_SpecExponent[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos_SpecExponent );
			vEyePos_SpecExponent[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1 );

			// Pixel shader dynamic combos
			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_teeth_composite_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( deferred_teeth_composite_ps30 );

			// Handle intro warp
			if ( params[INTRO]->GetIntValue() )
			{
				float curTime = params[WARPPARAM]->GetFloatValue();
				float timeVec[4] = { 0.0f, 0.0f, 0.0f, curTime };
				if ( params[ENTITYORIGIN]->IsDefined() )
					params[ENTITYORIGIN]->GetVecValue( timeVec, 3 );
				pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_5, timeVec, 1 );
			}
		}

		Draw();
	}

END_SHADER

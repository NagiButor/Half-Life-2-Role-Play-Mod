// DEFERRED_EYES shader
// Wraps the SDK_Eyes shader to participate in the deferred lighting pipeline.
// GBuffer pass: outputs normals and depth
// Shadow pass: outputs depth for shadow casting
// Composite pass: renders eye effect with deferred lighting from _rt_LightAccum

#include "deferred_includes.h"

#include "gbuffer_vs30.inc"
#include "gbuffer_ps30.inc"

#include "shadowpass_vs30.inc"
#include "shadowpass_ps30.inc"

// Reuse the existing Eyes vertex shader for composite pass
#include "SDK_eyes_vs30.inc"
// New deferred composite pixel shader
#include "deferred_eyes_composite_ps30.inc"

#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DEFERRED_EYES, "Deferred lighting version of SDK_Eyes" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( IRIS, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "iris texture" )
		SHADER_PARAM( IRISFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame for the iris texture" )
		SHADER_PARAM( GLINT, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "glint texture" )
		SHADER_PARAM( EYEORIGIN, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "origin for the eyes" )
		SHADER_PARAM( EYEUP, SHADER_PARAM_TYPE_VEC3, "[0 0 1]", "up vector for the eyes" )
		SHADER_PARAM( IRISU, SHADER_PARAM_TYPE_VEC4, "[0 1 0 0]", "U projection vector for the iris" )
		SHADER_PARAM( IRISV, SHADER_PARAM_TYPE_VEC4, "[0 0 1 0]", "V projection vector for the iris" )
		SHADER_PARAM( GLINTU, SHADER_PARAM_TYPE_VEC4, "[0 1 0 0]", "U projection vector for the glint" )
		SHADER_PARAM( GLINTV, SHADER_PARAM_TYPE_VEC4, "[0 0 1 0]", "V projection vector for the glint" )
		SHADER_PARAM( DILATION, SHADER_PARAM_TYPE_FLOAT, "0", "Pupil dilation" )
		SHADER_PARAM( INTRO, SHADER_PARAM_TYPE_BOOL, "0", "is eyes in the ep1 intro" )
		SHADER_PARAM( ENTITYORIGIN, SHADER_PARAM_TYPE_VEC3, "0.0", "center of the model in world space" )
		SHADER_PARAM( WARPPARAM, SHADER_PARAM_TYPE_FLOAT, "0.0", "animation param between 0 and 1" )

		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.5", "" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "" )
	END_SHADER_PARAMS

	void SetupParmsGBuffer( defParms_gBuffer &p )
	{
		p.bModel = true;
		// HL2RPM: no depth bias (see DrawCompositeEyes)
		p.bDepthBias = false;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iPhongExp = PHONG_EXP;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = true;
		p.bDepthBias = true;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );

		if ( g_pHardwareConfig->HasFastVertexTextures() )
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );

		if ( !params[DILATION]->IsDefined() )
			params[DILATION]->SetFloatValue( 0.0f );

		if ( !params[INTRO]->IsDefined() )
			params[INTRO]->SetIntValue( 0 );

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
		LoadTexture( IRIS, TEXTUREFLAGS_SRGB );
		LoadTexture( GLINT );

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
			return "SDK_Eyes";

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
			DrawCompositeEyes( params, pShaderAPI, pShaderShadow, vertexCompression, pDefContext );
		}
		else
			Draw( false );
#endif

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

	void DrawCompositeEyes( IMaterialVar **params,
		IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow,
		VertexCompressionType_t vertexCompression,
		CDeferredPerMaterialContextData *pDefContext )
	{
		const bool bUseSRGB = DEFCFG_USE_SRGB_CONVERSION != 0;

		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableSRGBWrite( bUseSRGB );

			// Samplers: s0=Base, s1=Iris, s2=Glint, s3=LightAccum
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, false );

			// Vertex format: position + normal + compressed + 1 texcoord
			int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
			int nTexCoordCount = 1;
			int userDataSize = 0;
			pShaderShadow->VertexShaderVertexFormat( flags, nTexCoordCount, NULL, userDataSize );

			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableDepthWrites( true );

			// HL2RPM: no SHADER_POLYOFFSET_SHADOW_BIAS here (nor in the G-buffer pass): it is the
			// *slope-scaled* bias of the shadow maps (2..4 x the depth slope, whatever the last
			// shadow view set). Where the eyeball curves away from the camera the slope is huge
			// and its pixels went behind the whole scene - the wall behind the head showed
			// through a gap under the eyelids. The stock Eyes shader has no offset either.

			DefaultFog();

			// Vertex shader: reuse Eyes VS
			DECLARE_STATIC_VERTEX_SHADER( sdk_eyes_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( HALFLAMBERT, IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT ) );
			SET_STATIC_VERTEX_SHADER_COMBO( INTRO, params[INTRO]->GetIntValue() ? 1 : 0 );
			SET_STATIC_VERTEX_SHADER( sdk_eyes_vs30 );

			// Pixel shader: deferred composite
			DECLARE_STATIC_PIXEL_SHADER( deferred_eyes_composite_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_eyes_composite_ps30 );
		}
		DYNAMIC_STATE
		{
			// Bind textures
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			BindTexture( SHADER_SAMPLER1, IRIS, IRISFRAME );
			BindTexture( SHADER_SAMPLER2, GLINT );
			BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_LightAccum() );

			// Set eye-specific vertex shader constants
			SetAmbientCubeDynamicStateVertexShader();
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, EYEORIGIN );
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, EYEUP );
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, IRISU );
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, IRISV );
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, GLINTU );
			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_5, GLINTV );

			// Vertex shader dynamic combos
			DECLARE_DYNAMIC_VERTEX_SHADER( sdk_eyes_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DYNAMIC_LIGHT, 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( STATIC_LIGHT, 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER( sdk_eyes_vs30 );

			if ( pShaderAPI->IsHWMorphingEnabled() )
			{
				SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_7,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, SHADER_VERTEXTEXTURE_SAMPLER0 );
			}

			// Pixel shader constants

			// c0: { Dilation, glintDamping, 0, 0 }
			float fGlintDamping = max( 0.0f, min( pShaderAPI->GetAmbientLightCubeLuminance(), 1.0f ) );
			const float fDimGlint = 0.01f;
			if ( fGlintDamping > fDimGlint )
				fGlintDamping = 1.0f;
			else
				fGlintDamping *= SimpleSplineRemapVal( fGlintDamping, 0.0f, fDimGlint, 0.0f, 1.0f );

			float vPSConst0[4] = { params[DILATION]->GetFloatValue(), fGlintDamping, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, vPSConst0, 1 );

			// c1: fullscreen texel size for sampling light accum
			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			float fl1[4] = { 1.0f / viewport.m_nWidth, 1.0f / viewport.m_nHeight, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 1, fl1, 1 );

			// Fog
			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );

			float vEyePos_SpecExponent[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos_SpecExponent );
			vEyePos_SpecExponent[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1 );

			// Pixel shader dynamic combos
			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_eyes_composite_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( deferred_eyes_composite_ps30 );

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

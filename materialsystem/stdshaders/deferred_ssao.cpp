//========= HL2RPM ============================================================//
//
// Purpose: DEFERRED_SSAO (deferred_ssao_ps30): half resolution ambient occlusion
//          from the G-buffer, and DEFERRED_SSAOBLUR (deferred_ssaoblur_ps30):
//          its depth-aware separable blur ($direction 0 = horizontal: AO -> blur
//          target, 1 = vertical: blur target -> AO).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "deferred_ssao_ps30.inc"
#include "deferred_ssaoblur_ps30.inc"

BEGIN_VS_SHADER( DEFERRED_SSAO, "" )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_ssao_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_ssao_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const ssaoData_t &data = GetDeferredExt()->GetSSAOData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_ssao_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_ssao_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, data.vecParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecParams1.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_SSAOBLUR, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DIRECTION, SHADER_PARAM_TYPE_INTEGER, "0", "0 = horizontal (AO -> blur), 1 = vertical (blur -> AO)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const ssaoData_t &data = GetDeferredExt()->GetSSAOData();
			const bool bVertical = params[DIRECTION]->GetIntValue() != 0;
			ITexture *pSource = bVertical ? GetDeferredExt()->GetTexture_SSAOBlur() : GetDeferredExt()->GetTexture_SSAO();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, pSource );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, bVertical ? data.vecBlurV.Base() : data.vecBlurH.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

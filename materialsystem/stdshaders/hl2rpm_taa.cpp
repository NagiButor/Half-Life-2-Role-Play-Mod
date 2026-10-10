//========= HL2RPM ============================================================//
//
// Purpose: temporal anti-aliasing (deferred_taa.cpp in the client, hl2rpm_taa_ps30):
//          this frame ($frame = _rt_FullFrameFB) + $historytexture -> the frame;
//          $viewmodel 1 for the pixels of the first person weapon and body (stencil),
//          which are not reprojected; $debugpass 1 shows the debug mode instead.
//          Per frame values come from IDeferredExt::GetTAAData().
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_taa_ps30.inc"

BEGIN_VS_SHADER( HL2RPM_TAA, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( HISTORYTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "history: the last frame's result" )
		SHADER_PARAM( VIEWMODEL, SHADER_PARAM_TYPE_INTEGER, "0", "1 = pixels that move with the camera (no reprojection)" )
		SHADER_PARAM( DEBUGPASS, SHADER_PARAM_TYPE_INTEGER, "0", "1 = draw the debug mode (r_deferred_taa_debug)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[HISTORYTEXTURE]->IsDefined() )
			LoadTexture( HISTORYTEXTURE );
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
			pShaderShadow->EnableSRGBWrite( false );
			for ( int i = 0; i < 3; i++ )
			{
				pShaderShadow->EnableTexture( (Sampler_t)( SHADER_SAMPLER0 + i ), true );
				pShaderShadow->EnableSRGBRead( (Sampler_t)( SHADER_SAMPLER0 + i ), false );
			}
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_taa_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_taa_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const taaData_t &data = GetDeferredExt()->GetTAAData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_taa_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_taa_ps30 );

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			if ( params[HISTORYTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, HISTORYTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );
			BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_Depth() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, data.vecTexel.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecPrev[0].Base(), 3, true );
			pShaderAPI->SetPixelShaderConstant( 4, data.vecCur[0].Base(), 3, true );
			const float c7[4] = { data.vecCamDelta.x, data.vecCamDelta.y, data.vecCamDelta.z,
				params[VIEWMODEL]->GetIntValue() ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 7, c7, 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, data.vecParams.Base(), 1, true );
			const float c9[4] = { params[DEBUGPASS]->GetIntValue() ? data.vecParams2.x : 0.0f,
				data.vecParams2.y, data.vecParams2.z, data.vecParams2.w };
			pShaderAPI->SetPixelShaderConstant( 9, c9, 1, true );
			pShaderAPI->SetPixelShaderConstant( 10, data.vecParams3.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

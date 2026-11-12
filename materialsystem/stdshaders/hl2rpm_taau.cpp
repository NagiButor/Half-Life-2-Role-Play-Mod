//========= HL2RPM ============================================================//
//
// Purpose: TAA upscale (render scale; deferred_taa.cpp DeferredTAA_DrawUpscale, hl2rpm_taau_ps30):
//          $frametexture (the smaller 3D frame) + $historytexture (the last result, screen size)
//          + $masktexture (what moves with the camera) -> the screen. $lowres: 1 / the frame
//          target's size, the frame's size; $jitter: where this frame's samples lie (frame pixels),
//          frame / screen. Per frame values of the TAA come from IDeferredExt::GetTAAData().
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_taau_ps30.inc"

BEGIN_VS_SHADER( HL2RPM_TAAU, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FRAMETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "this frame at the render scale" )
		SHADER_PARAM( HISTORYTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "history: the last frame's result (screen size)" )
		SHADER_PARAM( MASKTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "1 = moves with the camera" )
		SHADER_PARAM( LOWRES, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "1 / frame target size, frame size" )
		SHADER_PARAM( JITTER, SHADER_PARAM_TYPE_VEC4, "[0 0 1 1]", "jitter (frame pixels), frame / screen" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[FRAMETEXTURE]->IsDefined() )
			LoadTexture( FRAMETEXTURE );
		if ( params[HISTORYTEXTURE]->IsDefined() )
			LoadTexture( HISTORYTEXTURE );
		if ( params[MASKTEXTURE]->IsDefined() )
			LoadTexture( MASKTEXTURE );
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
			for ( int i = 0; i < 4; i++ )
			{
				pShaderShadow->EnableTexture( (Sampler_t)( SHADER_SAMPLER0 + i ), true );
				pShaderShadow->EnableSRGBRead( (Sampler_t)( SHADER_SAMPLER0 + i ), false );
			}
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_taau_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_taau_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const taaData_t &data = GetDeferredExt()->GetTAAData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_taau_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_taau_ps30 );

			if ( params[FRAMETEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, FRAMETEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			if ( params[HISTORYTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, HISTORYTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );
			BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_Depth() );
			if ( params[MASKTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER3, MASKTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, data.vecTexel.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecPrev[0].Base(), 3, true );
			pShaderAPI->SetPixelShaderConstant( 4, data.vecCur[0].Base(), 3, true );
			pShaderAPI->SetPixelShaderConstant( 7, data.vecCamDelta.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, data.vecParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 9, data.vecParams2.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 10, data.vecParams3.Base(), 1, true );
			float c11[4], c12[4];
			params[LOWRES]->GetVecValue( c11, 4 );
			params[JITTER]->GetVecValue( c12, 4 );
			pShaderAPI->SetPixelShaderConstant( 11, c11, 1, true );
			pShaderAPI->SetPixelShaderConstant( 12, c12, 1, true );
		}

		Draw();
	}
END_SHADER

//========= HL2RPM ============================================================//
//
// Purpose: post-processing of the final frame (deferred_postfx.cpp in the client):
//          HL2RPM_LUM (hl2rpm_lum_ps30): eye adaptation metering,
//            $pass 0 frame -> blocks, 1 blocks -> average, 2 adapt ($prevtexture)
//          HL2RPM_BLOOM (hl2rpm_bloom_ps30): $pass 0 the frame's bright part ->
//            level 1, 1 downsample of level $level, 2 upsample of level $level
//            added onto the level above (additive blending)
//          HL2RPM_POSTFX (hl2rpm_postfx_ps30): the final combine
//          Per frame values come from IDeferredExt::GetPostFXData().
//          HL2RPM_LOWHEALTH (hl2rpm_lowhealth_ps30): the wounded view without the
//            suit (hl2rpm_vitals.cpp in the client), its values in material vars -
//            it runs without the deferred renderer too.
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_lum_ps30.inc"
#include "hl2rpm_bloom_ps30.inc"
#include "hl2rpm_postfx_ps30.inc"
#include "hl2rpm_dof_ps30.inc"
#include "hl2rpm_lowhealth_ps30.inc"

static void SetupPostShadowState( IShaderShadow *pShaderShadow, int nSamplers )
{
	pShaderShadow->SetDefaultState();
	pShaderShadow->EnableDepthTest( false );
	pShaderShadow->EnableDepthWrites( false );
	pShaderShadow->EnableAlphaWrites( false );
	pShaderShadow->EnableSRGBWrite( false );
	for ( int i = 0; i < nSamplers; i++ )
	{
		pShaderShadow->EnableTexture( (Sampler_t)( SHADER_SAMPLER0 + i ), true );
		pShaderShadow->EnableSRGBRead( (Sampler_t)( SHADER_SAMPLER0 + i ), false );
	}
	pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
}

BEGIN_VS_SHADER( HL2RPM_LUM, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "0 = frame -> blocks, 1 = blocks -> average, 2 = adapt" )
		SHADER_PARAM( PREVTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "adapted value of the last frame (pass 2)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
			LoadTexture( BASETEXTURE );
		if ( params[PREVTEXTURE]->IsDefined() )
			LoadTexture( PREVTEXTURE );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		const int iPass = clamp( params[PASS]->GetIntValue(), 0, 2 );

		SHADOW_STATE
		{
			// (pass 2 also looks for the sky around the sun in the G-buffer's depth)
			SetupPostShadowState( pShaderShadow, ( iPass == 2 ) ? 3 : 2 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_lum_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( PASS, iPass );
			SET_STATIC_PIXEL_SHADER( hl2rpm_lum_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const postfxData_t &data = GetDeferredExt()->GetPostFXData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_lum_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_lum_ps30 );

			if ( iPass == 0 )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			else if ( params[BASETEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, BASETEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );

			if ( params[PREVTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, PREVTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );

			float c0[4];
			if ( iPass == 0 )
			{
				// one block of the frame in uv
				c0[0] = data.vecLumBlocks.x;
				c0[1] = data.vecLumBlocks.y;
				c0[2] = 0.0f;
				c0[3] = 0.0f;
			}
			else if ( iPass == 1 )
			{
				c0[0] = 1.0f / Max( 1.0f, data.vecLumBlocks.z );
				c0[1] = 1.0f / Max( 1.0f, data.vecLumBlocks.w );
				c0[2] = data.vecLumBlocks.z;
				c0[3] = data.vecLumBlocks.w;
			}
			else
			{
				c0[0] = data.vecAdapt.x;
				c0[1] = data.vecAdapt.y;
				c0[2] = data.vecAdapt.z;
				c0[3] = data.vecAdapt.w;

				// the sun: its place (uv), uv per radian around it (y; x from the aspect) - 0 = none
				ITexture *pDepth = GetDeferredExt()->GetTexture_Depth();
				if ( pDepth )
					BindTexture( SHADER_SAMPLER2, pDepth );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );
				const float c1[4] = { data.vecFlare.x, data.vecFlare.y, pDepth ? data.vecGlare.w : 0.0f, Max( data.vecTint.w, 0.1f ) };
				pShaderAPI->SetPixelShaderConstant( 1, c1, 1, true );
			}
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( HL2RPM_BLOOM, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "0 = the frame's bright part, 1 = downsample, 2 = upsample (added)" )
		SHADER_PARAM( LEVEL, SHADER_PARAM_TYPE_INTEGER, "0", "level read (pass 1, 2)" )
		SHADER_PARAM( LUMTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "adapted log luminance (pass 0)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
			LoadTexture( BASETEXTURE );
		if ( params[LUMTEXTURE]->IsDefined() )
			LoadTexture( LUMTEXTURE );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		const int iPass = clamp( params[PASS]->GetIntValue(), 0, 2 );
		const int iLevel = clamp( ( iPass == 0 ) ? 0 : params[LEVEL]->GetIntValue(), 0, POSTFX_LEVELS - 1 );

		SHADOW_STATE
		{
			SetupPostShadowState( pShaderShadow, 2 );
			if ( iPass == 2 )
			{
				EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			}

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_bloom_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( PASS, iPass );
			SET_STATIC_PIXEL_SHADER( hl2rpm_bloom_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const postfxData_t &data = GetDeferredExt()->GetPostFXData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_bloom_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_bloom_ps30 );

			if ( iPass == 0 )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			else if ( params[BASETEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, BASETEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );

			if ( params[LUMTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, LUMTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );

			const Vector4D &texel = data.vecLevelTexel[ iLevel ];
			const float c0[4] = { texel.x, texel.y, texel.z, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecLevelRect[ iLevel ].Base(), 1, true );
			const float c2[4] = { data.vecBloom.z, data.vecBloom.w, texel.w, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, data.vecExposure.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( HL2RPM_POSTFX, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BLOOMTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "bloom (level 1)" )
		SHADER_PARAM( LUMTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "adapted log luminance" )
		SHADER_PARAM( DIRTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "lens dirt" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[BLOOMTEXTURE]->IsDefined() )
			LoadTexture( BLOOMTEXTURE );
		if ( params[LUMTEXTURE]->IsDefined() )
			LoadTexture( LUMTEXTURE );
		if ( params[DIRTTEXTURE]->IsDefined() )
			LoadTexture( DIRTTEXTURE );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			SetupPostShadowState( pShaderShadow, 4 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_postfx_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_postfx_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const postfxData_t &data = GetDeferredExt()->GetPostFXData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_postfx_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_postfx_ps30 );

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			if ( params[BLOOMTEXTURE]->IsTexture() && data.vecBloom.x + data.vecBloom.y > 0.0f )
				BindTexture( SHADER_SAMPLER1, BLOOMTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );
			if ( params[LUMTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER2, LUMTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_BLACK );
			if ( params[DIRTTEXTURE]->IsTexture() )
				BindTexture( SHADER_SAMPLER3, DIRTTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			const float c0[4] = { data.vecFrameTexel.x, data.vecFrameTexel.y, data.vecFrameToBloom.x, data.vecFrameToBloom.y };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecExposure.Base(), 1, true );
			const float c2[4] = { data.vecBloom.x, data.vecBloom.y, data.vecLevelRect[1].z, data.vecLevelRect[1].w };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, data.vecGrade.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 4, data.vecLens.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, data.vecTint.Base(), 1, true );
			const float c6[4] = { data.vecFrameToBloom.z, data.vecFrameToBloom.w, data.vecFrameTexel.z, data.vecFrameTexel.w };
			pShaderAPI->SetPixelShaderConstant( 6, c6, 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, data.vecFlare.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, data.vecGlare.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( HL2RPM_DOF, "" )
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
			SetupPostShadowState( pShaderShadow, 2 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_dof_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_dof_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const postfxData_t &data = GetDeferredExt()->GetPostFXData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_dof_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_dof_ps30 );

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );

			const float c0[4] = { data.vecFrameTexel.x, data.vecFrameTexel.y, data.vecFrameTexel.z, data.vecFrameTexel.w };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecDoF.Base(), 1, true );
			const float c2[4] = { data.vecLens.w, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( HL2RPM_LOWHEALTH, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "1", "0 = the frame -> quarter size, 1 = the effect over the frame" )
		SHADER_PARAM( VEINTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "r vessels, g the order they show in, b blotches, a halo" )
		SHADER_PARAM( BLURTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "quarter size copy of the frame (pass 1)" )
		SHADER_PARAM( EFFECTPARAMS, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "strength, heartbeat pulse, dimming, clouding" )
		SHADER_PARAM( EFFECTPARAMS2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "desaturation, swell of the frame on a beat" )
		SHADER_PARAM( VIEWRECT, SHADER_PARAM_TYPE_VEC4, "[0 0 1 1]", "the view in the frame texture: uv of its corner, its uv size" )
		SHADER_PARAM( TEXELSIZE, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "1 / frame texture size, 1 / quarter texture size" )
		SHADER_PARAM( BLURRECT, SHADER_PARAM_TYPE_VEC4, "[0 0 1 1]", "the view in the quarter texture: uv of its corner, its uv size" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[VEINTEXTURE]->IsDefined() )
			LoadTexture( VEINTEXTURE );
		if ( params[BLURTEXTURE]->IsDefined() )
			LoadTexture( BLURTEXTURE );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		const int iPass = clamp( params[PASS]->GetIntValue(), 0, 1 );

		SHADOW_STATE
		{
			SetupPostShadowState( pShaderShadow, ( iPass == 0 ) ? 1 : 3 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_lowhealth_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( PASS, iPass );
			SET_STATIC_PIXEL_SHADER( hl2rpm_lowhealth_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_lowhealth_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_lowhealth_ps30 );

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			if ( iPass == 1 )
			{
				if ( params[VEINTEXTURE]->IsTexture() )
					BindTexture( SHADER_SAMPLER1, VEINTEXTURE );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );
				if ( params[BLURTEXTURE]->IsTexture() )
					BindTexture( SHADER_SAMPLER2, BLURTEXTURE );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_BLACK );
			}

			pShaderAPI->SetPixelShaderConstant( 0, params[EFFECTPARAMS]->GetVecValue(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, params[EFFECTPARAMS2]->GetVecValue(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, params[VIEWRECT]->GetVecValue(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, params[TEXELSIZE]->GetVecValue(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 4, params[BLURRECT]->GetVecValue(), 1, true );
		}

		Draw();
	}
END_SHADER

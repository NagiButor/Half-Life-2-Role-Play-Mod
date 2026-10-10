//========= HL2RPM ============================================================//
//
// Purpose: World-space indirect light of the global light (GI probes, see
//          deferred_gi.cpp in the client):
//          DEFERRED_GI (deferred_gi_ps30): the indirect light of every pixel at
//          half resolution from the 8 probes around it.
//          DEFERRED_GIBLUR (deferred_giblur_ps30): its depth-aware separable blur
//          ($direction 0 = horizontal: GI -> blur target, 1 = vertical: back).
//          DEFERRED_GI_UPLOAD (deferred_gi_upload_vs30/ps30): writes probe data
//          into the probe atlas (tiny quads in clip space).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "deferred_gi_ps30.inc"
#include "deferred_giblur_ps30.inc"
#include "deferred_gi_upload_vs30.inc"
#include "deferred_gi_upload_ps30.inc"

BEGIN_VS_SHADER( DEFERRED_GI, "" )
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
			pShaderShadow->EnableAlphaWrites( true );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_gi_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_gi_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const giData_t &gi = GetDeferredExt()->GetGIData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_gi_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_gi_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );
			ITexture *pProbes = GetDeferredExt()->GetTexture_GIProbes();
			if ( pProbes )
				BindTexture( SHADER_SAMPLER2, pProbes );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_BLACK );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			pShaderAPI->SetPixelShaderConstant( 1, gi.vecGrid.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, gi.vecDims.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, gi.vecAtlas.Base(), 1, true );

			// the sun / moon fades out at the horizon like in the global light pass
			const float flSunUp = clamp( ( light.vecLight.z + 0.02f ) / 0.08f, 0.0f, 1.0f );
			const float flBoost = GetGISkyBoost( gi, GetDeferredExt()->GetWeatherData() );
			const float flAmbHigh[4] = { light.ambh.x * flBoost, light.ambh.y * flBoost, light.ambh.z * flBoost, gi.vecParams0.w };
			const float flAmbLow[4] = { light.ambl.x * flBoost, light.ambl.y * flBoost, light.ambl.z * flBoost, gi.vecParams0.y };
			const float flSun[4] = { light.diff.x * gi.vecParams0.x * flSunUp, light.diff.y * gi.vecParams0.x * flSunUp,
				light.diff.z * gi.vecParams0.x * flSunUp, gi.vecParams0.z };
			pShaderAPI->SetPixelShaderConstant( 4, flAmbHigh, 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, flAmbLow, 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, flSun, 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, gi.vecParams1.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_GIBLUR, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DIRECTION, SHADER_PARAM_TYPE_INTEGER, "0", "0 = horizontal (GI -> blur), 1 = vertical (blur -> GI)" )
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
			pShaderShadow->EnableAlphaWrites( true );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_giblur_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_giblur_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const giData_t &gi = GetDeferredExt()->GetGIData();
			const bool bVertical = params[DIRECTION]->GetIntValue() != 0;
			ITexture *pSource = bVertical ? GetDeferredExt()->GetTexture_GIBlur() : GetDeferredExt()->GetTexture_GI();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_giblur_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_giblur_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, pSource );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, bVertical ? gi.vecBlurV.Base() : gi.vecBlurH.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_GI_UPLOAD, "" )
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
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableCulling( false );

			int iTexCoordSize = 4;
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, &iTexCoordSize, 0 );

			DECLARE_STATIC_VERTEX_SHADER( deferred_gi_upload_vs30 );
			SET_STATIC_VERTEX_SHADER( deferred_gi_upload_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( deferred_gi_upload_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( deferred_gi_upload_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
		}

		Draw();
	}
END_SHADER

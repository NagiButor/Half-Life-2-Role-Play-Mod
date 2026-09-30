//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_GODRAYS - screen-space crepuscular rays (weather_godrays_ps30),
//          added into the volumetrics buffer.
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "weather_godrays_ps30.inc"

BEGIN_VS_SHADER( WEATHER_GODRAYS, "" )
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

			// added to the shadow-map sun shafts already in the buffer
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_godrays_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_godrays_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pClouds = GetDeferredExt()->GetTexture_Clouds();
			const bool bClouds = w.bEnabled && w.bCloudTextureValid && pClouds != NULL;

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_godrays_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_godrays_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			if ( bClouds )
				BindTexture( SHADER_SAMPLER1, pClouds );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_WHITE );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			// forced: with the sun behind the camera these are all zero every frame, the shader API
			// took them for unchanged and the pass read the sun shafts' c0/c1 (camera origin,
			// light direction) -> saturated blue sky and blue halos around the geometry
			pShaderAPI->SetPixelShaderConstant( 0, w.vecGodRays0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, w.vecGodRays1.Base(), 1, true );
			float flCloud[4] = { w.vecScreenParams.z, bClouds ? 1.0f : 0.0f, w.vecScreenParams.w, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 2, flCloud, 1, true );
		}

		Draw();
	}
END_SHADER

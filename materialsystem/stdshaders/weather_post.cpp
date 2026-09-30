//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_POST - wet surfaces, puddles and height fog (weather_post_ps30).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "weather_post_ps30.inc"

BEGIN_VS_SHADER( WEATHER_POST, "" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

			// result = src.rgb + dest * src.a
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_SRC_ALPHA );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_post_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_post_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_post_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_post_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Normals() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );
			if ( GetDeferredExt()->GetTexture_RainMap() )
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_RainMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );
			if ( GetDeferredExt()->GetTexture_WeatherMap() )
				BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_WeatherMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_GREY );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			pShaderAPI->SetPixelShaderConstant( 1, w.vecRainParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, w.vecFogParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, w.vecFogColor.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 4, w.vecSkyZenith.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, w.vecSkyHorizon.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, light.vecLight.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, light.diff.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, w.matRainMap.Base(), 3, true );

			float flRainMap[4] = { w.vecRainMapParams.x, w.vecRainMapParams.y, w.vecRainMapParams.z, w.bRainMapValid ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 11, flRainMap, 1, true );

			float flFlags[4] = { w.vecFogParams.x > 0.0f ? 1.0f : 0.0f, w.vecRainParams.y > 0.0f ? 1.0f : 0.0f, w.vecSunColor.w, w.vecDebug.x };
			pShaderAPI->SetPixelShaderConstant( 12, flFlags, 1, true );
		}

		Draw();
	}
END_SHADER

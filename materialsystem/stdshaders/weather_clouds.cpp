//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_CLOUDS - screen-space volumetric clouds (weather_clouds_ps30).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "weather_clouds_ps30.inc"

BEGIN_VS_SHADER( WEATHER_CLOUDS, "" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_clouds_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_clouds_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_clouds_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_clouds_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_CloudNoise() );
			BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_WeatherMap() );
			BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_CloudHistory() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, w.vecCameraParams.Base(), 1, true );
			// the sun / moon, not a lightning flash that took over the global light
			pShaderAPI->SetPixelShaderConstant( 1, w.bEnabled ? w.vecSkyLightDir.Base() : light.vecLight.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, w.vecSunColor.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, w.vecSkyZenith.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 4, w.vecSkyHorizon.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, w.vecCloudParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, w.vecCloudParams1.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, w.vecCloudParams2.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, w.vecWind.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 9, w.vecScreenParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 10, w.vecCloudParams3.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 14, w.vecLightning.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

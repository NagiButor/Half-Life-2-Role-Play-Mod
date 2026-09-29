//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_RAIN - GPU-animated rain streaks (weather_rain_vs30/ps30).
//
//=============================================================================//

#include "deferred_includes.h"

#include "weather_rain_vs30.inc"
#include "weather_rain_ps30.inc"

static ConVar r_weather_rain_box( "r_weather_rain_box", "1500", 0, "Width of the rain volume around the camera (units)" );
static ConVar r_weather_rain_alpha( "r_weather_rain_alpha", "0.32", 0, "Rain streak opacity" );

BEGIN_VS_SHADER( WEATHER_RAIN, "" )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_TRANSLUCENT );
		SET_FLAGS( MATERIAL_VAR_NOFOG );
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
			pShaderShadow->EnableDepthTest( true );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableCulling( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );

			int nTexCoordDims[2] = { 2, 4 };
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 2, nTexCoordDims, 0 );

			DECLARE_STATIC_VERTEX_SHADER( weather_rain_vs30 );
			SET_STATIC_VERTEX_SHADER( weather_rain_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_rain_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_rain_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( weather_rain_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( weather_rain_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_rain_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_rain_ps30 );

			if ( GetDeferredExt()->GetTexture_RainMap() )
				BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_RainMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );

			// vertex constants
			const float flBox = clamp( r_weather_rain_box.GetFloat(), 400.0f, 4000.0f );
			float vCamera[4] = { w.vecCameraParams.x, w.vecCameraParams.y, w.vecCameraParams.z, w.vecRainParams.w };
			// vecRainMapParams.w = fraction of the drops to draw (rain intensity x density setting)
			float vRain[4] = { clamp( w.vecRainMapParams.w, 0.0f, 1.0f ), flBox, flBox * 0.75f, 360.0f };
			float vWind[4] = { w.vecWind.z, w.vecWind.w, 0.032f, 0.55f };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vCamera );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, vRain );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, vWind );

			// pixel constants
			pShaderAPI->SetPixelShaderConstant( 0, w.matRainMap.Base(), 3 );
			float flRainMap[4] = { w.vecRainMapParams.x, w.vecRainMapParams.y, w.vecRainMapParams.z, w.bRainMapValid ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 3, flRainMap );
			pShaderAPI->SetPixelShaderConstant( 4, w.vecSkyHorizon.Base() );
			pShaderAPI->SetPixelShaderConstant( 5, w.vecSkyZenith.Base() );
			float flLight[4] = { light.diff.x, light.diff.y, light.diff.z, w.vecLightning.w };
			pShaderAPI->SetPixelShaderConstant( 6, flLight );
			float flAlpha[4] = { r_weather_rain_alpha.GetFloat(), 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 7, flAlpha );
		}

		Draw();
	}
END_SHADER

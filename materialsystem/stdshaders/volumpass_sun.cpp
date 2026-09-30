//========= HL2RPM ============================================================//
//
// Purpose: VOLUME_SUN - raymarched volumetric sun light shafts (volumpass_sun_ps30).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "volumpass_sun_ps30.inc"

static ConVar r_weather_sunshafts_distance( "r_weather_sunshafts_distance", "3500", FCVAR_ARCHIVE, "How far the sun shafts are raymarched (units)" );

BEGIN_VS_SHADER( VOLUME_SUN, "" )
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

			// adds to whatever point/spot volumetrics are already in the buffer
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( volumpass_sun_ps30 );
			SET_STATIC_PIXEL_SHADER( volumpass_sun_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( volumpass_sun_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( volumpass_sun_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_ShadowDepth_Ortho( 0 ) );
			if ( GetDeferredExt()->GetTexture_WeatherMap() )
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_WeatherMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_GREY );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );
			CommitGlobalLightForward( pShaderAPI, 1 );
			CommitShadowProjectionConstants_Ortho_Composite( pShaderAPI, SHADOW_NUM_CASCADES, 2 );

			pShaderAPI->SetPixelShaderConstant( 58, light.diff.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 59, w.vecFogParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 60, w.vecCloudParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 61, w.vecCloudParams1.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 62, w.vecWind.Base(), 1, true );

			static const float s_flSteps[4] = { 0.0f, 16.0f, 24.0f, 36.0f };
			static ConVarRef r_weather_sunshafts( "r_weather_sunshafts" );
			const int iQuality = r_weather_sunshafts.IsValid() ? clamp( r_weather_sunshafts.GetInt(), 0, 3 ) : 2;
			float flParams[4] = { s_flSteps[iQuality], clamp( r_weather_sunshafts_distance.GetFloat(), 500.0f, 12000.0f ),
				w.vecScreenParams.w, w.vecSunColor.w };
			pShaderAPI->SetPixelShaderConstant( 63, flParams, 1, true );
		}

		Draw();
	}
END_SHADER

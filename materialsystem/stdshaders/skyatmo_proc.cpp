#include "BaseVSShader.h"

#include "deferred_includes.h"
#include "lighting_helper.h"

#include "procsky_atmo_vs30.inc"
#include "procsky_atmo_ps30.inc"

BEGIN_VS_SHADER( SkyAtmoProc, "" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SKYFACE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( EXPOSURE, SHADER_PARAM_TYPE_FLOAT, "20.0", "" )
		SHADER_PARAM( STARSINTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNDISKINTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNRISEBIAS, SHADER_PARAM_TYPE_FLOAT, "-1.0", "" )
		SHADER_PARAM( AEROSOL, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( STARROTATIONSPEED, SHADER_PARAM_TYPE_FLOAT, "0.00001157", "" )
		SHADER_PARAM( USELUT, SHADER_PARAM_TYPE_BOOL, "1", "" )
		SHADER_PARAM( SKYLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_ATMO_LUT, "" )
		SHADER_PARAM( TRANSMITTANCELUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_TRANSMITTANCE_LUT, "" )
		SHADER_PARAM( SKYVIEWLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_SKYVIEW_LUT, "" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_NOFOG );
		SET_FLAGS( MATERIAL_VAR_IGNOREZ );
	}

	SHADER_INIT
	{
		// HL2RPM: default values don't count as "defined" -> the transmittance LUT was
		// never loaded (the sun disk sampled the error texture).
		if ( !params[SKYLUT]->IsDefined() )
			params[SKYLUT]->SetStringValue( DEFRTNAME_SKY_ATMO_LUT );
		if ( !params[TRANSMITTANCELUT]->IsDefined() )
			params[TRANSMITTANCELUT]->SetStringValue( DEFRTNAME_SKY_TRANSMITTANCE_LUT );
		if ( !params[SKYVIEWLUT]->IsDefined() )
			params[SKYVIEWLUT]->SetStringValue( DEFRTNAME_SKY_SKYVIEW_LUT );
		LoadTexture( SKYLUT );
		LoadTexture( TRANSMITTANCELUT );
		LoadTexture( SKYVIEWLUT );
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );	// HL2RPM clouds
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// HL2RPM weather map

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( procsky_atmo_vs30 );
			SET_STATIC_VERTEX_SHADER( procsky_atmo_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( procsky_atmo_ps30 );
			SET_STATIC_PIXEL_SHADER( procsky_atmo_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}

		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			BindTexture( SHADER_SAMPLER0, SKYLUT, -1 );
			BindTexture( SHADER_SAMPLER1, TRANSMITTANCELUT, -1 );
			BindTexture( SHADER_SAMPLER2, SKYVIEWLUT, -1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( procsky_atmo_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( procsky_atmo_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( procsky_atmo_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( procsky_atmo_ps30 );

			const lightData_Global_t& data = GetDeferredExt()->GetLightData_Global();

			CommitGlobalLightForward( pShaderAPI, 1 );

			float procParams0[4] = { 0, 0, 0, 0 };
			procParams0[0] = ( params[USELUT]->IsDefined() && params[USELUT]->GetIntValue() ) ? 1.0f : 0.0f;
			procParams0[1] = ( params[EXPOSURE]->IsDefined() ) ? params[EXPOSURE]->GetFloatValue() : 20.0f;
			procParams0[2] = ( params[STARSINTENSITY]->IsDefined() ) ? params[STARSINTENSITY]->GetFloatValue() : 1.0f;
			procParams0[3] = ( params[SUNDISKINTENSITY]->IsDefined() ) ? params[SUNDISKINTENSITY]->GetFloatValue() : 1.0f;
			pShaderAPI->SetPixelShaderConstant( 2, procParams0, 1, true );

			float procParams1[4] = { 0, 0, 0, 0 };
			procParams1[0] = ( params[SUNRISEBIAS]->IsDefined() ) ? params[SUNRISEBIAS]->GetFloatValue() : -1.0f;
			procParams1[1] = ( params[AEROSOL]->IsDefined() ) ? params[AEROSOL]->GetFloatValue() : 1.0f;
			const float starSpeed = ( params[STARROTATIONSPEED]->IsDefined() ) ? params[STARROTATIONSPEED]->GetFloatValue() : 0.00001157f;
			procParams1[2] = pShaderAPI->CurrentTime() * starSpeed;
			pShaderAPI->SetPixelShaderConstant( 3, procParams1, 1, true );

			pShaderAPI->SetPixelShaderConstant( 16, data.diff.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 17, data.ambh.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 18, MakeHalfAmbient( data.ambl, data.ambh ).Base(), 1, true );

			// HL2RPM: dynamic weather
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pClouds = GetDeferredExt()->GetTexture_Clouds();
			ITexture *pWeatherMap = GetDeferredExt()->GetTexture_WeatherMap();
			const bool bCloudsValid = w.bEnabled && w.bCloudTextureValid && pClouds != NULL;

			if ( bCloudsValid )
				BindTexture( SHADER_SAMPLER3, pClouds );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			if ( w.bEnabled && pWeatherMap )
				BindTexture( SHADER_SAMPLER4, pWeatherMap );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_GREY );

			pShaderAPI->SetPixelShaderConstant( 4, w.vecCloudParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, w.vecCloudParams1.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, w.vecAtmoParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, w.vecFogColor.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, w.vecFogParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 9, w.vecScreenParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 10, w.vecMoonDir.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 11, w.vecSunDir.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 12, w.vecLightning.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 13, w.vecWind.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 14, w.vecSkyHorizon.Base(), 1, true );
			float flFlags[4] = { w.bEnabled ? 1.0f : 0.0f, bCloudsValid ? 1.0f : 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 15, flFlags, 1, true );

			// a lightning flash briefly turns the global light into the bolt: the sky keeps the sun / moon
			if ( w.bEnabled )
			{
				pShaderAPI->SetPixelShaderConstant( 1, w.vecSkyLightDir.Base(), 1, true );
				pShaderAPI->SetPixelShaderConstant( 16, w.vecSkyLightDiff.Base(), 1, true );
			}
		}

		Draw();
	}

END_SHADER

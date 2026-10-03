#include "BaseVSShader.h"

#include "deferred_includes.h"
#include "lighting_helper.h"

#include "screenspace_vs30.inc"
#include "sky_atmo_skyview_lutgen_ps30.inc"

BEGIN_VS_SHADER( SKY_ATMO_SKYVIEW_LUTGEN, "" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( TRANSMITTANCELUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_TRANSMITTANCE_LUT, "" )
		SHADER_PARAM( MULTISCATTERINGLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_MULTISCATTERING_LUT, "" )
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
		// HL2RPM: default param values don't count as "defined", so these LUTs were
		// never loaded and the sky was computed from the error texture (purple checker).
		if ( !params[TRANSMITTANCELUT]->IsDefined() )
			params[TRANSMITTANCELUT]->SetStringValue( DEFRTNAME_SKY_TRANSMITTANCE_LUT );
		if ( !params[MULTISCATTERINGLUT]->IsDefined() )
			params[MULTISCATTERINGLUT]->SetStringValue( DEFRTNAME_SKY_MULTISCATTERING_LUT );
		LoadTexture( TRANSMITTANCELUT );
		LoadTexture( MULTISCATTERINGLUT );
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

			DECLARE_STATIC_VERTEX_SHADER( screenspace_vs30 );
			SET_STATIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( sky_atmo_skyview_lutgen_ps30 );
			SET_STATIC_PIXEL_SHADER( sky_atmo_skyview_lutgen_ps30 );
		}

		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			BindTexture( SHADER_SAMPLER0, TRANSMITTANCELUT, -1 );
			BindTexture( SHADER_SAMPLER1, MULTISCATTERINGLUT, -1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( sky_atmo_skyview_lutgen_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( sky_atmo_skyview_lutgen_ps30 );

			const lightData_Global_t& data = GetDeferredExt()->GetLightData_Global();

			CommitGlobalLightForward( pShaderAPI, 1 );

			// HL2RPM: weather haze thickens the aerosol layer. With the weather system the sky
			// is lit by the untinted sun (the atmosphere colors it and casts the earth's shadow
			// at dusk; the global light's diffuse is tinted and faded out at the horizon).
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			pShaderAPI->SetPixelShaderConstant( 16, w.bEnabled ? w.vecSkyLight.Base() : data.diff.Base(), 1, true );
			// a lightning flash briefly turns the global light into the bolt: the sky keeps the sun / moon
			if ( w.bEnabled )
				pShaderAPI->SetPixelShaderConstant( 1, w.vecSkyLightDir.Base(), 1, true );
			float flAtmo[4] = { w.bEnabled ? w.vecAtmoParams.x : 1.0f, 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 2, flAtmo, 1, true );
		}

		Draw();
	}

END_SHADER


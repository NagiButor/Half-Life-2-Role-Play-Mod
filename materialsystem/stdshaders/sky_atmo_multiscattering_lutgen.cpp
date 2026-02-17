#include "deferred_includes.h"

#include "screenspace_vs30.inc"
#include "sky_atmo_multiscattering_lutgen_ps30.inc"

BEGIN_VS_SHADER( SKY_ATMO_MULTISCATTERING_LUTGEN, "" )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_NOFOG );
		SET_FLAGS( MATERIAL_VAR_IGNOREZ );
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

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( screenspace_vs30 );
			SET_STATIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( sky_atmo_multiscattering_lutgen_ps30 );
			SET_STATIC_PIXEL_SHADER( sky_atmo_multiscattering_lutgen_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( sky_atmo_multiscattering_lutgen_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( sky_atmo_multiscattering_lutgen_ps30 );

			CommitGlobalLightForward( pShaderAPI, 1 );
		}

		Draw();
	}
END_SHADER


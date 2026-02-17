#include "BaseVSShader.h"
 
#include "deferred_includes.h"
#include "lighting_helper.h"
 
#include "screenspace_vs30.inc"
#include "sky_atmo_lutgen_ps30.inc"
 
BEGIN_VS_SHADER( SKY_ATMO_LUTGEN, "" )
 
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
 
			DECLARE_STATIC_PIXEL_SHADER( sky_atmo_lutgen_ps30 );
			SET_STATIC_PIXEL_SHADER( sky_atmo_lutgen_ps30 );
		}
 
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();
 
			BindTexture( SHADER_SAMPLER0, TRANSMITTANCELUT, -1 );
			BindTexture( SHADER_SAMPLER1, MULTISCATTERINGLUT, -1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
 
			DECLARE_DYNAMIC_PIXEL_SHADER( sky_atmo_lutgen_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( sky_atmo_lutgen_ps30 );
 
			const lightData_Global_t& data = GetDeferredExt()->GetLightData_Global();
 
			CommitGlobalLightForward( pShaderAPI, 1 );
			pShaderAPI->SetPixelShaderConstant( 16, data.diff.Base() );
		}
 
		Draw();
	}
 
END_SHADER

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
			pShaderAPI->SetPixelShaderConstant( 2, procParams0, 1 );

			float procParams1[4] = { 0, 0, 0, 0 };
			procParams1[0] = ( params[SUNRISEBIAS]->IsDefined() ) ? params[SUNRISEBIAS]->GetFloatValue() : -1.0f;
			procParams1[1] = ( params[AEROSOL]->IsDefined() ) ? params[AEROSOL]->GetFloatValue() : 1.0f;
			const float starSpeed = ( params[STARROTATIONSPEED]->IsDefined() ) ? params[STARROTATIONSPEED]->GetFloatValue() : 0.00001157f;
			procParams1[2] = pShaderAPI->CurrentTime() * starSpeed;
			pShaderAPI->SetPixelShaderConstant( 3, procParams1, 1 );

			pShaderAPI->SetPixelShaderConstant( 16, data.diff.Base() );
			pShaderAPI->SetPixelShaderConstant( 17, data.ambh.Base() );
			pShaderAPI->SetPixelShaderConstant( 18, MakeHalfAmbient( data.ambl, data.ambh ).Base() );
		}

		Draw();
	}

END_SHADER

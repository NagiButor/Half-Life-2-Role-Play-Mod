
#include "deferred_includes.h"

#include "screenspace_vs30.inc"
#include "sun_godrays_ps30.inc"

BEGIN_VS_SHADER( SUN_GODRAYS, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SUNPOS, SHADER_PARAM_TYPE_VEC2, "[0.5 0.5]", "" )
		SHADER_PARAM( SUNCOLOR, SHADER_PARAM_TYPE_VEC3, "[1 1 1]", "" )
		SHADER_PARAM( INTENSITY, SHADER_PARAM_TYPE_FLOAT, "0.25", "" )
		SHADER_PARAM( RADIUS, SHADER_PARAM_TYPE_FLOAT, "0.25", "" )
		SHADER_PARAM( DECAY, SHADER_PARAM_TYPE_FLOAT, "0.92", "" )
		SHADER_PARAM( FALLOFFEXP, SHADER_PARAM_TYPE_FLOAT, "2.0", "" )
		SHADER_PARAM( OCCLUSIONPOWER, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
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
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( screenspace_vs30 );
			SET_STATIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( sun_godrays_ps30 );
			SET_STATIC_PIXEL_SHADER( sun_godrays_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( sun_godrays_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( sun_godrays_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );

			const float *sunPos = params[SUNPOS]->GetVecValue();
			const float intensity = params[INTENSITY]->GetFloatValue();
			const float radius = params[RADIUS]->GetFloatValue();
			float c0[4] = { sunPos[0], sunPos[1], intensity, radius };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1 );

			const float decay = params[DECAY]->GetFloatValue();
			const float falloffExp = params[FALLOFFEXP]->GetFloatValue();
			const float occlusionPower = params[OCCLUSIONPOWER]->GetFloatValue();
			float c1[4] = { decay, falloffExp, occlusionPower, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 1, c1, 1 );

			const float *sunColor = params[SUNCOLOR]->GetVecValue();
			float c2[4] = { sunColor[0], sunColor[1], sunColor[2], 0.0f };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1 );
		}

		Draw();
	}
END_SHADER



#include "deferred_includes.h"

#include "screenspace_vs30.inc"
#include "volume_blend_ps30.inc"

BEGIN_VS_SHADER( VOLUME_BLEND, "" )
	BEGIN_SHADER_PARAMS

	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		Assert( params[ BASETEXTURE ]->IsDefined() );

		if ( params[ BASETEXTURE ]->IsDefined() )
			LoadTexture( BASETEXTURE );
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

			DECLARE_STATIC_PIXEL_SHADER( volume_blend_ps30 );
			SET_STATIC_PIXEL_SHADER( volume_blend_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( volume_blend_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( volume_blend_ps30 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE );

			// HL2RPM: GAMMA_LIGHT_SCALE is cLightScale (c30), which the engine does not restore
			// per draw. The passes before this one (sun shafts: cascade UV transforms c26-c33)
			// leave junk there, which zeroed all volumetric light. Same fix as the composite.
			float flNeutralLightScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			pShaderAPI->SetPixelShaderConstant( 30, flNeutralLightScale );
		}

		Draw();
	}
END_SHADER

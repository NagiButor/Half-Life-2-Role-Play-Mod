
#include "deferred_includes.h"

#include "screenspace_vs30.inc"
#include "screenspace_combine_ps30.inc"

static ConVar r_deferred_aa_mode( "r_deferred_aa_mode", "1", FCVAR_ARCHIVE, "0=off,1=FXAA,2=SMAA-lite" );
static ConVar r_deferred_aa_strength( "r_deferred_aa_strength", "1.0", FCVAR_ARCHIVE, "0..1 mix between original and AA" );

BEGIN_VS_SHADER( SCREENSPACE_COMBINE, "" )
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

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( screenspace_vs30 );
			SET_STATIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( screenspace_combine_ps30 );
			SET_STATIC_PIXEL_SHADER( screenspace_combine_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( screenspace_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( screenspace_combine_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( screenspace_combine_ps30 );

#if DEFCFG_DEFERRED_SHADING == 1
			int x, y, w, t;
			pShaderAPI->GetCurrentViewport( x, y, w, t );
			float params[4] = { 1.0f / w, 1.0f / t, r_deferred_aa_mode.GetFloat(), clamp( r_deferred_aa_strength.GetFloat(), 0.0f, 1.0f ) };
			pShaderAPI->SetPixelShaderConstant( 0, params );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Albedo() );
#else
			Assert( 0 );
#endif
		}

		Draw();
	}
END_SHADER

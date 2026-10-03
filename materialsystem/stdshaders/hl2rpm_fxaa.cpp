//========= HL2RPM ============================================================//
//
// Purpose: HL2RPM_FXAA - FXAA on the final frame (hl2rpm_fxaa_ps30). The client copies
//          the frame into _rt_FullFrameFB and draws this over the whole screen.
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_fxaa_ps30.inc"

static ConVar r_deferred_fxaa_subpix( "r_deferred_fxaa_subpix", "0.75", FCVAR_ARCHIVE,
	"FXAA: how much sub-pixel aliasing (one pixel thin lines, specks) is smoothed, 0..1", true, 0.0f, true, 1.0f );
static ConVar r_deferred_fxaa_edge( "r_deferred_fxaa_edge", "0.125", FCVAR_ARCHIVE,
	"FXAA: local contrast an edge needs, relative to the brightest neighbour (0.063 = more edges .. 0.333 = fewer)", true, 0.03f, true, 0.5f );
static ConVar r_deferred_fxaa_edge_min( "r_deferred_fxaa_edge_min", "0.0625", FCVAR_ARCHIVE,
	"FXAA: contrast below which dark areas are left alone", true, 0.0f, true, 0.25f );
static ConVar r_deferred_fxaa_debug( "r_deferred_fxaa_debug", "0", 0, "FXAA: show the edges it smooths in red" );

BEGIN_VS_SHADER( HL2RPM_FXAA, "" )
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

			// the gamma space frame as it is (no sRGB conversion in or out)
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_fxaa_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_fxaa_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_fxaa_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_fxaa_ps30 );

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );

			int w = 1, h = 1;
			pShaderAPI->GetStandardTextureDimensions( &w, &h, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			float flC0[4] = { 1.0f / Max( w, 1 ), 1.0f / Max( h, 1 ),
				r_deferred_fxaa_subpix.GetFloat(), r_deferred_fxaa_edge.GetFloat() };
			float flC1[4] = { r_deferred_fxaa_edge_min.GetFloat(), r_deferred_fxaa_debug.GetBool() ? 1.0f : 0.0f, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 0, flC0, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, flC1, 1, true );
		}

		Draw();
	}

END_SHADER

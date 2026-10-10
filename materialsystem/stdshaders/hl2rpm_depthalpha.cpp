//========= HL2RPM ============================================================//
//
// Purpose: soft particles for the deferred renderer (hl2rpm_depthalpha_ps30): writes
//          the G-buffer depth into the frame's alpha in the stock dest alpha format,
//          after the opaque composite of the main view (viewrender_deferred.cpp).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_depthalpha_ps30.inc"

static ConVar r_deferred_soft_particles_debug( "r_deferred_soft_particles_debug", "0", FCVAR_CHEAT,
	"Soft particles debug: 1 show the scene depth written for them, 2 the linear fog color constant (top) and its dest alpha depth scale (bottom)" );

BEGIN_VS_SHADER( HL2RPM_DEPTHALPHA, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DEBUGVIEW, SHADER_PARAM_TYPE_INTEGER, "0", "1 = the debug variant (color writes on)" )
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
			pShaderShadow->EnableColorWrites( params[DEBUGVIEW]->GetIntValue() != 0 );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			// (the fog state brings the linear fog color constant, whose w is the dest alpha
			// depth scale SoftParticleDepth uses - the same the particles get)
			DefaultFog();

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_depthalpha_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_depthalpha_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_depthalpha_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_depthalpha_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );

			const float flNear = GetDeferredExt()->GetZDistNear();
			const float flFar = Max( GetDeferredExt()->GetZDistFar(), flNear + 1.0f );
			const float c0[4] = { flFar / ( flFar - flNear ), flNear,
				params[DEBUGVIEW]->GetIntValue() ? (float)r_deferred_soft_particles_debug.GetInt() : 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );
		}

		Draw();
	}
END_SHADER

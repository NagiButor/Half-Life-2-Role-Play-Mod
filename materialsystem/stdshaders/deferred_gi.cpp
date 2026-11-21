//========= HL2RPM ============================================================//
//
// Purpose: DEFERRED_GI_UPLOAD (deferred_gi_upload_vs30/ps30)
//          Kept for deferred_lightreach.cpp point-light reach atlas texel uploads.
//          All GI shaders and passes have been removed.
//
//=============================================================================//

#include "deferred_includes.h"

#include "deferred_gi_upload_vs30.inc"
#include "deferred_gi_upload_ps30.inc"

BEGIN_VS_SHADER( DEFERRED_GI_UPLOAD, "" )
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
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableCulling( false );

			int iTexCoordSize = 4;
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, &iTexCoordSize, 0 );

			DECLARE_STATIC_VERTEX_SHADER( deferred_gi_upload_vs30 );
			SET_STATIC_VERTEX_SHADER( deferred_gi_upload_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( deferred_gi_upload_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( deferred_gi_upload_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_gi_upload_ps30 );
		}

		Draw();
	}
END_SHADER

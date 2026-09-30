//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_CLOUDRESOLVE - temporal resolve of the screen-space clouds
//          (weather_cloudresolve_ps30).
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "weather_cloudresolve_ps30.inc"

BEGIN_VS_SHADER( WEATHER_CLOUDRESOLVE, "" )
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

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_cloudresolve_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_cloudresolve_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pRaw = GetDeferredExt()->GetTexture_CloudRaw();
			ITexture *pHistory = GetDeferredExt()->GetTexture_CloudHistory();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_cloudresolve_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_cloudresolve_ps30 );

			if ( pRaw )
				BindTexture( SHADER_SAMPLER0, pRaw );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			if ( pHistory )
				BindTexture( SHADER_SAMPLER1, pHistory );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			const float flTexelX = pRaw ? 1.0f / Max( 1, pRaw->GetActualWidth() ) : 0.0f;
			const float flTexelY = pRaw ? 1.0f / Max( 1, pRaw->GetActualHeight() ) : 0.0f;
			float flResolve[4] = { flTexelX, flTexelY, w.vecScreenParams.z, w.vecCloudParams2.w };
			pShaderAPI->SetPixelShaderConstant( 0, flResolve, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, w.matPrevViewProj.Base(), 4, true );
		}

		Draw();
	}
END_SHADER

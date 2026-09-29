
#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "lightingpass_global_ps30.inc"

static ConVar r_csm_color( "r_csm_color", "0", FCVAR_ARCHIVE );

BEGIN_VS_SHADER( LIGHTING_GLOBAL, "" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
#if !DEFCFG_LIGHTCTRL_PACKING
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
#endif
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// HL2RPM weather map
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );	// HL2RPM cloud noise

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( lightingpass_global_ps30 );
			SET_STATIC_PIXEL_SHADER( lightingpass_global_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const lightData_Global_t& data = GetDeferredExt()->GetLightData_Global();

			AssertMsg( data.bEnabled, "I shouldn't be drawn at all." );

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( lightingpass_global_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( HAS_SHADOW, data.bShadow );
			SET_DYNAMIC_PIXEL_SHADER( lightingpass_global_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Normals() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );
#if !DEFCFG_LIGHTCTRL_PACKING
			BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_LightCtrl() );
#endif

			if ( data.bShadow )
			{
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_ShadowDepth_Ortho( 0 ) );

				COMPILE_TIME_ASSERT( CSM_USE_COMPOSITED_TARGET == 1 );

				CommitShadowProjectionConstants_Ortho_Composite( pShaderAPI, SHADOW_NUM_CASCADES, 2 );
			}

			CommitGlobalLightForward( pShaderAPI, 1 );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA, data.diff.Base() );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 1, data.ambh.Base() );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 2, MakeHalfAmbient( data.ambl, data.ambh ).Base() );

			float flCSMColorize[4] = { r_csm_color.GetBool() ? 1.0f : 0.0f, 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 3, flCSMColorize );

			// HL2RPM: moving cloud shadows
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pWeatherMap = GetDeferredExt()->GetTexture_WeatherMap();
			ITexture *pCloudNoise = GetDeferredExt()->GetTexture_CloudNoise();
			const bool bCloudShadows = w.bEnabled && pWeatherMap && pCloudNoise && w.vecCloudParams2.y > 0.001f;
			if ( bCloudShadows )
			{
				BindTexture( SHADER_SAMPLER4, pWeatherMap );
				BindTexture( SHADER_SAMPLER5, pCloudNoise );
			}
			else
			{
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_GREY );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_GREY );
			}
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 4, w.vecCloudParams0.Base() );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 5, w.vecCloudParams1.Base() );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 6, w.vecWind.Base() );
			float flWeatherFlags[4] = { bCloudShadows ? w.vecCloudParams2.y : 0.0f, 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 7, flWeatherFlags );
		}

		Draw();
	}

END_SHADER

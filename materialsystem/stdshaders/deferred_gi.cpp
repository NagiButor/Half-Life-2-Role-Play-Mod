//========= HL2RPM ============================================================//
//
// Purpose: World-space indirect light of the global light (GI probes, see
//          deferred_gi.cpp in the client):
//          DEFERRED_GI (deferred_gi_ps30): the indirect light of every pixel at
//          half resolution from the 8 probes around it.
//          DEFERRED_GIBLUR (deferred_giblur_ps30): its depth-aware separable blur
//          ($direction 0 = horizontal: GI -> blur target, 1 = vertical: back).
//          DEFERRED_GIUP (deferred_giup_ps30): the blurred half resolution light
//          for every full resolution pixel, from the half resolution pixels of
//          its own surface (GI -> the blur target, full size).
//          DEFERRED_GI_UPLOAD (deferred_gi_upload_vs30/ps30): writes probe data
//          into the probe atlas (points in clip space, one texel each).
//          DEFERRED_GI_BLEND (deferred_gi_blend_ps30): the atlas the lighting
//          reads, eased toward the one the probes are written to.
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "deferred_gi_ps30.inc"
#include "deferred_giblur_ps30.inc"
#include "deferred_giup_ps30.inc"
#include "deferred_gi_upload_vs30.inc"
#include "deferred_gi_upload_ps30.inc"
#include "deferred_gi_blend_ps30.inc"

BEGIN_VS_SHADER( DEFERRED_GI, "" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );	// probes of the far grid
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// SSAO / sky visibility (half resolution)

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_gi_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_gi_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const giData_t &gi = GetDeferredExt()->GetGIData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_gi_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_gi_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );
			ITexture *pProbes = GetDeferredExt()->GetTexture_GIProbes();
			if ( pProbes )
				BindTexture( SHADER_SAMPLER2, pProbes );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_BLACK );
			ITexture *pProbesFar = GetDeferredExt()->GetTexture_GIProbesFar();
			if ( pProbesFar )
				BindTexture( SHADER_SAMPLER3, pProbesFar );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			// the sky visibility of the rain map (the G of the SSAO target, drawn before this
			// pass): what the light beyond the probes is estimated from
			const ssaoData_t &ao = GetDeferredExt()->GetSSAOData();
			ITexture *pAO = GetDeferredExt()->GetTexture_SSAO();
			const bool bAO = ao.bEnabled && pAO != NULL && ao.vecApply.w > 0.0f;
			if ( bAO )
				BindTexture( SHADER_SAMPLER4, pAO );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_WHITE );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			pShaderAPI->SetPixelShaderConstant( 1, gi.vecGrid[0].Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, gi.vecDims[0].Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, gi.vecAtlas.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, gi.vecGrid[1].Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 9, gi.vecDims[1].Base(), 1, true );
			// x sky visibility uv scale (0 = none), y indoor ambient of the sky visibility, z height of the sun
			const float flEstimate[4] = { bAO ? ao.vecApply.w : 0.0f, ao.vecApply.y, clamp( light.vecLight.z, 0.0f, 1.0f ),
				( gi.vecParams2.y > 0.0f ) ? gi.vecParams2.y : 1.0f };
			pShaderAPI->SetPixelShaderConstant( 10, flEstimate, 1, true );

			// the sun / moon fades out at the horizon like in the global light pass
			const float flSunUp = clamp( ( light.vecLight.z + 0.02f ) / 0.08f, 0.0f, 1.0f );
			const Vector4D ambh = GetGISkyLight( light.ambh, gi, GetDeferredExt()->GetWeatherData() );
			const Vector4D ambl = GetGISkyLight( light.ambl, gi, GetDeferredExt()->GetWeatherData() );
			// (the interior terms - the least sky light without sky in sight, the sky light bounced in a
			// room - keep the boost the rooms were lit with; only the open sky is toned down)
			const float flIndoor = GetGISkyIndoorFactor( gi, GetDeferredExt()->GetWeatherData() );
			const float flAmbHigh[4] = { ambh.x, ambh.y, ambh.z, Min( gi.vecParams0.w * flIndoor, 0.9f ) };
			const float flAmbLow[4] = { ambl.x, ambl.y, ambl.z, gi.vecParams0.y * flIndoor };
			// (w: 1 / cells over which the near grid fades into the far one)
			const float flSun[4] = { light.diff.x * gi.vecParams0.x * flSunUp, light.diff.y * gi.vecParams0.x * flSunUp,
				light.diff.z * gi.vecParams0.x * flSunUp, gi.vecParams0.z };
			pShaderAPI->SetPixelShaderConstant( 4, flAmbHigh, 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, flAmbLow, 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, flSun, 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, gi.vecParams1.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_GIBLUR, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DIRECTION, SHADER_PARAM_TYPE_INTEGER, "0", "0 = horizontal (GI -> blur), 1 = vertical (blur -> GI)" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );	// G-buffer normals

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_giblur_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_giblur_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const giData_t &gi = GetDeferredExt()->GetGIData();
			const bool bVertical = params[DIRECTION]->GetIntValue() != 0;
			ITexture *pSource = bVertical ? GetDeferredExt()->GetTexture_GIBlur() : GetDeferredExt()->GetTexture_GI();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_giblur_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_giblur_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, pSource );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_Normals() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, bVertical ? gi.vecBlurV.Base() : gi.vecBlurH.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_GIUP, "" )
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

			DECLARE_STATIC_PIXEL_SHADER( deferred_giup_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_giup_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const giData_t &gi = GetDeferredExt()->GetGIData();
			ITexture *pSource = GetDeferredExt()->GetTexture_GI();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_giup_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_giup_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, pSource );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			// (the half resolution source's uv scale, as the blur gets it)
			const float flUp[4] = { gi.vecBlurH.w, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flUp, 1, true );
		}

		Draw();
	}
END_SHADER

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

BEGIN_VS_SHADER( DEFERRED_GI_BLEND, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( TARGETATLAS, SHADER_PARAM_TYPE_TEXTURE, "", "probe atlas as the probes are now" )
		SHADER_PARAM( PREVATLAS, SHADER_PARAM_TYPE_TEXTURE, "", "probe atlas the lighting read last frame" )
		SHADER_PARAM( BLENDPARAMS, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "part of the way to the target, weight step, probes per row block, 1 = take the target" )
		SHADER_PARAM( ATLASSIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "atlas size, 1 / size" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[TARGETATLAS]->IsDefined() )
			LoadTexture( TARGETATLAS );
		if ( params[PREVATLAS]->IsDefined() )
			LoadTexture( PREVATLAS );
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

			DECLARE_STATIC_PIXEL_SHADER( deferred_gi_blend_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_gi_blend_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_gi_blend_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_gi_blend_ps30 );

			if ( params[TARGETATLAS]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, TARGETATLAS );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
			if ( params[PREVATLAS]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, PREVATLAS );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_BLACK );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, params[BLENDPARAMS]->GetVecValue(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, params[ATLASSIZE]->GetVecValue(), 1, true );
		}

		Draw();
	}
END_SHADER

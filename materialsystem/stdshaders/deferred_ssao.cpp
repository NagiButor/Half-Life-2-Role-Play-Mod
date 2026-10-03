//========= HL2RPM ============================================================//
//
// Purpose: DEFERRED_SSAO (deferred_ssao_ps30): half resolution ambient occlusion
//          from the G-buffer, and DEFERRED_SSAOBLUR (deferred_ssaoblur_ps30):
//          its depth-aware separable blur ($direction 0 = horizontal: AO -> blur
//          target, 1 = vertical: blur target -> AO).
//          DEFERRED_SKYVIS (deferred_skyvis_ps30): the sky visibility map
//          DEFERRED_SSAO reads, made from the raw depth of the rain occlusion
//          map ($basetexture) whenever that is rendered.
//
//=============================================================================//

#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "deferred_ssao_ps30.inc"
#include "deferred_ssaoblur_ps30.inc"
#include "deferred_skyvis_ps30.inc"

// HL2RPM: sky visibility (see deferred_skyvis_ps30)
static ConVar r_deferred_skyvis_area( "r_deferred_skyvis_area", "128", 0,
	"Sky visibility: radius of the area around a point that decides how much sky it sees (units)", true, 32.0f, true, 512.0f );
static ConVar r_deferred_skyvis_cover( "r_deferred_skyvis_cover", "24", 0,
	"Sky visibility: what is less than this high above a point doesn't hide the sky (units; fully hidden 16 units higher)", true, 0.0f, true, 256.0f );
// (2.5: the foot of a wall - half of the area or a bit more covered by it and what stands
// behind it - keeps nearly all of the sky light; the shade there must look like the rest of
// the shade, only what is really under a roof darkens)
// (in the open: only nearly enclosed spots - a narrow slot between tall walls - lose sky light;
// the foot of every wall and curb darkened into a soft band that read as a second shadow)
static ConVar r_deferred_skyvis_debug( "r_deferred_skyvis_debug", "0", FCVAR_CHEAT,
	"Sky visibility: 1 = open part around points in the open, 2 = under a cover, 3 = how covered (shown by r_csm_pcss 6)", true, 0.0f, true, 3.0f );
static ConVar r_deferred_skyvis_open_outdoor( "r_deferred_skyvis_open_outdoor", "8", 0,
	"Sky visibility of points in the open: open part of the area -> sky light (scale, clamped to 1)", true, 1.0f, true, 16.0f );
static ConVar r_deferred_skyvis_open( "r_deferred_skyvis_open", "2.5", 0,
	"Sky visibility under a cover: open part of the area -> sky light (scale, clamped to 1)", true, 1.0f, true, 4.0f );

// the rain occlusion map's depth range (units): its depth bias is 2 texels in depth units
static float GetRainMapDepthRange( const weatherData_t &w )
{
	return ( w.vecRainMapParams.x > 1e-9f ) ? ( 2.0f * w.vecRainMapParams.z / w.vecRainMapParams.x ) : 14000.0f;
}

BEGIN_VS_SHADER( DEFERRED_SSAO, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SKYVISMAP, SHADER_PARAM_TYPE_TEXTURE, "", "sky visibility map (DEFERRED_SKYVIS)" )
		SHADER_PARAM( RAINDEPTH, SHADER_PARAM_TYPE_TEXTURE, "", "raw top-down depth of the rain occlusion map" )
		SHADER_PARAM( SKYVISMAPWIDE, SHADER_PARAM_TYPE_TEXTURE, "", "sky visibility map with the wide disk (DEFERRED_SKYVIS)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[SKYVISMAP]->IsDefined() )
			LoadTexture( SKYVISMAP );
		if ( params[RAINDEPTH]->IsDefined() )
			LoadTexture( RAINDEPTH );
		if ( params[SKYVISMAPWIDE]->IsDefined() )
			LoadTexture( SKYVISMAPWIDE );
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );	// HL2RPM: sky visibility map
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );	// HL2RPM: raw top-down depth of the rain map
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// HL2RPM: sky visibility map, wide disk

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_ssao_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_ssao_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const ssaoData_t &data = GetDeferredExt()->GetSSAOData();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_ssao_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_ssao_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Depth() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Normals() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 2 );

			pShaderAPI->SetPixelShaderConstant( 0, data.vecParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, data.vecParams1.Base(), 1, true );

			// HL2RPM: sky visibility (g) from the map DEFERRED_SKYVIS made out of the rain occlusion map
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const bool bMaps = params[SKYVISMAP]->IsTexture() && params[RAINDEPTH]->IsTexture() && params[SKYVISMAPWIDE]->IsTexture();
			const bool bSkyVis = w.bEnabled && w.bRainMapValid && bMaps && data.vecApply.x > 0.5f;
			if ( bSkyVis )
			{
				BindTexture( SHADER_SAMPLER2, SKYVISMAP );
				BindTexture( SHADER_SAMPLER3, RAINDEPTH );
				BindTexture( SHADER_SAMPLER4, SKYVISMAPWIDE );
			}
			else
			{
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_WHITE );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_WHITE );
			}
			pShaderAPI->SetPixelShaderConstant( 3, w.matRainMap.Base(), 3, true );
			float flRainMap[4] = { r_deferred_skyvis_open_outdoor.GetFloat(), w.vecRainMapParams.y, w.vecRainMapParams.z, bSkyVis ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 6, flRainMap, 1, true );
			float flSky[4] = { bSkyVis ? 1.0f + r_deferred_skyvis_debug.GetInt() : 0.0f, GetRainMapDepthRange( w ), 1.0f / r_deferred_skyvis_area.GetFloat(), r_deferred_skyvis_open.GetFloat() };
			pShaderAPI->SetPixelShaderConstant( 7, flSky, 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_SSAOBLUR, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DIRECTION, SHADER_PARAM_TYPE_INTEGER, "0", "0 = horizontal (AO -> blur), 1 = vertical (blur -> AO)" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const ssaoData_t &data = GetDeferredExt()->GetSSAOData();
			const bool bVertical = params[DIRECTION]->GetIntValue() != 0;
			ITexture *pSource = bVertical ? GetDeferredExt()->GetTexture_SSAOBlur() : GetDeferredExt()->GetTexture_SSAO();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_ssaoblur_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, pSource );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			pShaderAPI->SetPixelShaderConstant( 0, bVertical ? data.vecBlurV.Base() : data.vecBlurH.Base(), 1, true );
		}

		Draw();
	}
END_SHADER

BEGIN_VS_SHADER( DEFERRED_SKYVIS, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( RADIUS, SHADER_PARAM_TYPE_FLOAT, "0", "disk radius (units), 0 = r_deferred_skyvis_area" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
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

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_skyvis_ps30 );
			SET_STATIC_PIXEL_SHADER( deferred_skyvis_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pSource = params[BASETEXTURE]->IsTexture() ? params[BASETEXTURE]->GetTextureValue() : NULL;

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_skyvis_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( deferred_skyvis_ps30 );

			if ( pSource )
				BindTexture( SHADER_SAMPLER0, BASETEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );

			const float flRange = GetRainMapDepthRange( w );
			const float flMapSize = ( w.vecRainMapParams.y > 1.0f ) ? w.vecRainMapParams.y : 3072.0f;
			// x a quarter of the target's texel (4x4 samples per texel), y depth range, z disk
			// radius (uv), w how far below its own top the ground under a cover may be (depth units)
			ShaderViewport_t viewport;
			const int iTargetW = ( pShaderAPI->GetViewports( &viewport, 1 ) > 0 ) ? viewport.m_nWidth : 128;
			const float flRadius = ( params[RADIUS]->GetFloatValue() > 0.0f ) ? params[RADIUS]->GetFloatValue() : r_deferred_skyvis_area.GetFloat();
			float flParams[4] = { 0.25f / Max( 1, iTargetW ), flRange, flRadius / flMapSize, 512.0f / flRange };
			pShaderAPI->SetPixelShaderConstant( 0, flParams, 1, true );
			float flLevels[4] = { 16.0f, r_deferred_skyvis_cover.GetFloat(), 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 1, flLevels, 1, true );
		}

		Draw();
	}
END_SHADER

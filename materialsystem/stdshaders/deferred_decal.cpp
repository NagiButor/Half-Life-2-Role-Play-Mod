//========= HL2RPM ============================================================//
//
// Purpose: DEFERRED_DECAL - blood and bullet holes (the DecalModulate decals) on
//          the world and on models, as surfaces of their own:
//
//          - in the G-buffer their own normal (the relief the client makes of their
//            texture, $decalnormal: the crater of a hole, the film of blood) and gloss
//            ($decal_gloss) - the light passes then light them as what they are: the
//            sun glints on wet blood, a lamp shows the rim of a hole;
//          - in the composite the stock "surface times twice the texture", through the
//            parallax of that relief ($decal_relief: a hole looks deep), then their
//            reflection added - the light buffer's specular at their gloss times their
//            reflectance ($decal_f0), and the surroundings mirrored by the wet ones
//            ($decal_wet).
//
//          The client gives DecalModulate materials this shader with the values of
//          what they are (materialsystem_passthru.cpp, ApplyDecalPBR). Without the
//          deferred renderer it falls back to DecalModulate.
//
//=============================================================================//

#include "deferred_includes.h"

#include "deferred_decal_vs30.inc"
#include "deferred_decal_ps30.inc"

static ConVar r_deferred_decals( "r_deferred_decals", "1", 0, "Blood and bullet holes as surfaces of their own: normal, gloss and reflection in the deferred passes (0 = the stock decal: the surface darkened)" );
static ConVar r_deferred_decals_spec( "r_deferred_decals_spec", "1", 0, "Decals: strength of their reflection", true, 0.0f, true, 8.0f );
static ConVar r_deferred_decals_relief( "r_deferred_decals_relief", "1", 0, "Decals: scale of the depth of their relief (parallax)", true, 0.0f, true, 4.0f );
// (weather_post.cpp: the weather pass mirrors the surroundings for every smooth pixel of the G-buffer -
// the wet ones among the decals too; their own stand-in of it is left out then)
extern ConVar r_deferred_reflections;

static ConVar r_deferred_decals_bump( "r_deferred_decals_bump", "1", 0, "Decals: scale of the strength of their normal map", true, 0.0f, true, 4.0f );

BEGIN_VS_SHADER( DEFERRED_DECAL, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DECALNORMAL, SHADER_PARAM_TYPE_TEXTURE, "", "normal map (rgb) and height (a, 1 = the surface) of the decal - made by the client" )
		SHADER_PARAM( DECAL_GLOSS, SHADER_PARAM_TYPE_FLOAT, "0.3", "0 rough .. 1 a mirror" )
		SHADER_PARAM( DECAL_F0, SHADER_PARAM_TYPE_FLOAT, "0.03", "reflectance seen from the front" )
		SHADER_PARAM( DECAL_WET, SHADER_PARAM_TYPE_FLOAT, "0", "how much it mirrors its surroundings (wet)" )
		SHADER_PARAM( DECAL_RELIEF, SHADER_PARAM_TYPE_FLOAT, "0", "depth of the parallax of its relief, in texture units" )
		SHADER_PARAM( DECAL_BUMPSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "strength of the normal map" )
		SHADER_PARAM( DECAL_COVERAGE, SHADER_PARAM_TYPE_FLOAT, "1", "scale of how much of the surface it covers" )
		SHADER_PARAM( FOGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "0.4", "(DecalModulate)" )
		SHADER_PARAM( FOGSCALE, SHADER_PARAM_TYPE_FLOAT, "1.0", "(DecalModulate)" )
		// (set by the client: materials/hd, hl2rpm_extras/tools/hd_textures.py - the size of a decal is
		// its $basetexture's, so the higher resolution one is sampled in its place)
		SHADER_PARAM( HDTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "a higher resolution $basetexture (sampled with the same coordinates)" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
		if ( g_pHardwareConfig->HasFastVertexTextures() )
		{
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );
			SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		}
		if ( !params[ DECAL_GLOSS ]->IsDefined() )
			params[ DECAL_GLOSS ]->SetFloatValue( 0.3f );
		if ( !params[ DECAL_F0 ]->IsDefined() )
			params[ DECAL_F0 ]->SetFloatValue( 0.03f );
		if ( !params[ DECAL_WET ]->IsDefined() )
			params[ DECAL_WET ]->SetFloatValue( 0.0f );
		if ( !params[ DECAL_RELIEF ]->IsDefined() )
			params[ DECAL_RELIEF ]->SetFloatValue( 0.0f );
		if ( !params[ DECAL_BUMPSCALE ]->IsDefined() )
			params[ DECAL_BUMPSCALE ]->SetFloatValue( 1.0f );
		if ( !params[ DECAL_COVERAGE ]->IsDefined() )
			params[ DECAL_COVERAGE ]->SetFloatValue( 1.0f );
	}

	SHADER_FALLBACK
	{
		if ( !GetDeferredExt()->IsDeferredLightingEnabled() )
			return "DecalModulate";
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
		if ( params[ DECALNORMAL ]->IsDefined() )
			LoadTexture( DECALNORMAL );
		if ( params[ HDTEXTURE ]->IsDefined() )
			LoadTexture( HDTEXTURE );
	}

	// one pass: 0 the G-buffer, 1 the color, 2 the reflection
	void DrawDecalPass( int iPass, IMaterialVar **params, IShaderShadow *pShaderShadow, IShaderDynamicAPI *pShaderAPI,
		VertexCompressionType_t vertexCompression )
	{
		const bool bVertexAlpha = IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) && IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA );
		const bool bBump = params[ DECALNORMAL ]->IsTexture();

		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_DECAL );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			if ( bBump )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			}

			if ( iPass == 0 )
			{
				// (the normal, the packed gloss and the depth: written, not blended)
				pShaderShadow->EnableAlphaWrites( true );
				pShaderShadow->EnableSRGBWrite( false );
			}
			else if ( iPass == 1 )
			{
				// the stock decal: the surface times twice the texture
				pShaderShadow->EnableAlphaWrites( false );
				pShaderShadow->EnableSRGBWrite( false );
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_DST_COLOR, SHADER_BLEND_SRC_COLOR );
				pShaderShadow->DisableFogGammaCorrection( true );
				FogToGrey();
			}
			else
			{
				// the reflection, added (linear light, like the composite)
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );
				pShaderShadow->EnableAlphaWrites( false );
				pShaderShadow->EnableSRGBWrite( DEFCFG_USE_SRGB_CONVERSION != 0 );
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
				FogToBlack();
			}

			unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
			if ( bVertexAlpha )
				flags |= VERTEX_COLOR;
			int pTexCoordDim[3] = { 2, 0, 3 };
			pShaderShadow->VertexShaderVertexFormat( flags, 1, pTexCoordDim, 0 );

			DECLARE_STATIC_VERTEX_SHADER( deferred_decal_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, bVertexAlpha );
			SET_STATIC_VERTEX_SHADER( deferred_decal_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_decal_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( PASS, iPass );
			SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP, bBump );
			SET_STATIC_PIXEL_SHADER_COMBO( VERTEXALPHA, bVertexAlpha );
			SET_STATIC_PIXEL_SHADER( deferred_decal_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			if ( g_pHardwareConfig->HasFastVertexTextures() )
				SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

			DECLARE_DYNAMIC_VERTEX_SHADER( deferred_decal_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 ? 1 : 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, pShaderAPI->IsHWMorphingEnabled() ? 1 : 0 );
			SET_DYNAMIC_VERTEX_SHADER( deferred_decal_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_decal_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER( deferred_decal_ps30 );

			if ( g_pHardwareConfig->HasFastVertexTextures() )
			{
				bool bUnusedTexCoords[3] = { false, false, !pShaderAPI->IsHWMorphingEnabled() };
				pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
			}

			if ( params[ HDTEXTURE ]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, HDTEXTURE );
			else
				BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			if ( bBump )
				BindTexture( SHADER_SAMPLER1, DECALNORMAL );
			if ( iPass == 2 )
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_LightAccum() );

			// the G-buffer depth, as gbuffer_vs30 writes it
			float vPos[4] = { 0, 0, 0, 0 };
			pShaderAPI->GetWorldSpaceCameraPosition( vPos );
			const float zScale[4] = { GetDeferredExt()->GetZScale(), 0, 0, 0 };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vPos );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, GetDeferredExt()->GetForwardBase() );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, zScale );

			const float flGloss = clamp( params[ DECAL_GLOSS ]->GetFloatValue(), 0.0f, 1.0f ) * 62.0f;
			const float c0[4] = { flGloss, params[ DECAL_F0 ]->GetFloatValue() * r_deferred_decals_spec.GetFloat(),
				r_deferred_reflections.GetBool() ? 0.0f : params[ DECAL_WET ]->GetFloatValue(),
				GetDeferredExt()->IsReflectionView() ? 0.0f : params[ DECAL_RELIEF ]->GetFloatValue() * r_deferred_decals_relief.GetFloat() };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1, true );

			ShaderViewport_t viewport;
			pShaderAPI->GetViewports( &viewport, 1 );
			const float c1[4] = { 1.0f / Max( viewport.m_nWidth, 1 ), 1.0f / Max( viewport.m_nHeight, 1 ), 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 1, c1, 1, true );
			pShaderAPI->SetPixelShaderFogParams( 2 );
			pShaderAPI->SetPixelShaderConstant( 3, vPos, 1, true );

			// the light of the open sky: what the wet ones mirror at most (the composite's c36)
			{
				const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
				const float flSkyBoost = GetGISkyBoost( GetDeferredExt()->GetGIData(), GetDeferredExt()->GetWeatherData() );
				float c4[4] = { 0.5f, 0.55f, 0.6f, 0.0f };
				if ( light.bEnabled )
				{
					for ( int i = 0; i < 3; i++ )
						c4[i] = ( light.ambh[i] * 0.75f + light.ambl[i] * 0.25f ) * flSkyBoost;
				}
				pShaderAPI->SetPixelShaderConstant( 4, c4, 1, true );
			}

			static int s_iDitherFrame = 0;
			if ( iPass == 0 )
				s_iDitherFrame = ( s_iDitherFrame + 1 ) & 63;
			const float c5[4] = { params[ DECAL_BUMPSCALE ]->GetFloatValue() * r_deferred_decals_bump.GetFloat(),
				params[ DECAL_COVERAGE ]->GetFloatValue(), (float)( s_iDitherFrame * 7 ), 0.0f };
			pShaderAPI->SetPixelShaderConstant( 5, c5, 1, true );
		}
		Draw();
	}

	SHADER_DRAW
	{
		const int iStage = pShaderAPI ? pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;
		const bool bOn = r_deferred_decals.GetBool();

		// the G-buffer: its own surface
		if ( pShaderShadow != NULL || ( iStage == DEFERRED_RENDER_STAGE_GBUFFER && bOn ) )
			DrawDecalPass( 0, params, pShaderShadow, pShaderAPI, vertexCompression );
		else
			Draw( false );

		// the composite (and the views drawn without stages): the color, then the reflection
		const bool bComposite = iStage == DEFERRED_RENDER_STAGE_COMPOSITION || iStage == DEFERRED_RENDER_STAGE_INVALID;
		if ( pShaderShadow != NULL || bComposite )
			DrawDecalPass( 1, params, pShaderShadow, pShaderAPI, vertexCompression );
		else
			Draw( false );

		if ( pShaderShadow != NULL || ( bComposite && bOn && iStage != DEFERRED_RENDER_STAGE_INVALID ) )
			DrawDecalPass( 2, params, pShaderShadow, pShaderAPI, vertexCompression );
		else
			Draw( false );
	}
END_SHADER

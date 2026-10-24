//========= HL2RPM ============================================================//
//
// Purpose: DEFERRED_GLASS - panes of glass (windows, doors, the glass of vehicles and
//          lamps) as glass: what lies behind seen through it, its surroundings mirrored by
//          the angle of the view (the sky and the ground outdoors, a dim room under a roof),
//          the sun glinting on it in the shadow of the cascades, the dirt of its texture lit
//          like a surface. See deferred_glass_ps30.fxc.
//
//          The blended panes of the stock materials (LightmappedGeneric / VertexLitGeneric with
//          $translucent) used to fall back to the forward shaders: lit by the lightmap baked at
//          one time of day and mirroring the engine's default cubemap on maps without built
//          ones - half opaque grey, greenish, as bright at night as by day. The client gives
//          them this shader (materialsystem_passthru.cpp, IsGlassMaterial).
//
//=============================================================================//

#include "deferred_includes.h"
#include "hl2rpm_pbr.h"

#include "deferred_decal_vs30.inc"
#include "deferred_glass_ps30.inc"

static ConVar r_deferred_glass( "r_deferred_glass", "1", 0, "Panes of glass as glass: see-through, mirroring the sky by the angle of the view, the sun glinting on them (0 = drawn as the stock material)" );
static ConVar r_deferred_glass_reflect( "r_deferred_glass_reflect", "1", 0, "Glass: scale of what it mirrors", true, 0.0f, true, 4.0f );
static ConVar r_deferred_glass_dirt( "r_deferred_glass_dirt", "0.3", 0,
	"Glass: how much of the stock texture's opacity is dirt on the pane (the rest only made the pane visible)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_glass_roughness( "r_deferred_glass_roughness", "0.05", 0, "Glass: roughness of the panes", true, 0.0f, true, 1.0f );
static ConVar r_deferred_glass_glint( "r_deferred_glass_glint", "1", 0, "Glass: scale of the sun's glint", true, 0.0f, true, 8.0f );
static ConVar r_deferred_glass_room( "r_deferred_glass_room", "0.12", 0,
	"Glass under a roof: what it mirrors, of the light of the open sky", true, 0.0f, true, 2.0f );
static ConVar r_deferred_glass_frame( "r_deferred_glass_frame", "0.85", 0,
	"Glass: the opacity of the texture from which it is a frame painted into it (stays opaque)", true, 0.2f, true, 1.0f );
static ConVar r_deferred_glass_debug( "r_deferred_glass_debug", "0", FCVAR_CHEAT, "Glass: 1 = outdoors (green) / under a roof (red), 2 = the sun on it, 3 = how much it mirrors" );
static ConVar r_deferred_glass_ssr( "r_deferred_glass_ssr", "1", FCVAR_ARCHIVE, "Glass mirrors the scene around it (screen space reflections; the sky where the trace misses)" );
static ConVar r_deferred_glass_ssr_steps( "r_deferred_glass_ssr_steps", "16", 0, "Glass reflections: steps of the trace", true, 4.0f, true, 20.0f );
static ConVar r_deferred_glass_ssr_thickness( "r_deferred_glass_ssr_thickness", "24", 0, "Glass reflections: how thick a surface is taken to be (units)", true, 1.0f, true, 256.0f );

BEGIN_VS_SHADER( DEFERRED_GLASS, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( GLASS_ROUGHNESS, SHADER_PARAM_TYPE_FLOAT, "-1", "roughness of the pane (-1 = r_deferred_glass_roughness)" )
		SHADER_PARAM( GLASS_F0, SHADER_PARAM_TYPE_FLOAT, "0.08", "reflectance from the front (a pane: two surfaces of 4 %)" )
		SHADER_PARAM( GLASS_DIRT, SHADER_PARAM_TYPE_FLOAT, "-1", "how much of the texture's opacity is dirt on the pane (-1 = r_deferred_glass_dirt)" )
		SHADER_PARAM( GLASS_MODEL, SHADER_PARAM_TYPE_BOOL, "0", "the material of a model (its stock shader was VertexLitGeneric)" )
		SHADER_PARAM( SKYVIEWLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_SKYVIEW_LUT, "the sky-view LUT of the procedural sky" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_TRANSLUCENT );
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
		if ( params[ GLASS_MODEL ]->IsDefined() && params[ GLASS_MODEL ]->GetIntValue() != 0 )
			SET_FLAGS( MATERIAL_VAR_MODEL );
		if ( g_pHardwareConfig->HasFastVertexTextures() )
		{
			SET_FLAGS2( MATERIAL_VAR2_USES_VERTEXID );
			SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		}
		if ( !params[ GLASS_ROUGHNESS ]->IsDefined() )
			params[ GLASS_ROUGHNESS ]->SetFloatValue( -1.0f );
		if ( !params[ GLASS_F0 ]->IsDefined() )
			params[ GLASS_F0 ]->SetFloatValue( 0.08f );
		if ( !params[ GLASS_DIRT ]->IsDefined() )
			params[ GLASS_DIRT ]->SetFloatValue( -1.0f );
		// (a parameter left at its default is not "defined": nothing would load the texture)
		if ( !params[ SKYVIEWLUT ]->IsDefined() )
			params[ SKYVIEWLUT ]->SetStringValue( DEFRTNAME_SKY_SKYVIEW_LUT );
	}

	SHADER_FALLBACK
	{
		if ( !GetDeferredExt()->IsDeferredLightingEnabled() )
			return ( params[ GLASS_MODEL ]->IsDefined() && params[ GLASS_MODEL ]->GetIntValue() != 0 ) ? "VertexLitGeneric" : "LightmappedGeneric";
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
		if ( params[ SKYVIEWLUT ]->IsDefined() )
			LoadTexture( SKYVIEWLUT );
	}

	SHADER_DRAW
	{
		// only where the scene gets its colors: nothing of a pane in the G-buffer or the shadow maps
		const int iStage = pShaderAPI ? pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;
		if ( pShaderShadow == NULL && iStage != DEFERRED_RENDER_STAGE_COMPOSITION && iStage != DEFERRED_RENDER_STAGE_INVALID )
		{
			Draw( false );
			return;
		}

		const bool bVertexAlpha = IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) && IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA );

		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableDepthWrites( false );
			// premultiplied: the pane's color added, what is behind it kept by its alpha
			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableSRGBWrite( DEFCFG_USE_SRGB_CONVERSION != 0 );
			if ( IS_FLAG_SET( MATERIAL_VAR_NOCULL ) )
				pShaderShadow->EnableCulling( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, DEFCFG_USE_SRGB_CONVERSION != 0 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, false );
			// (the frame copy: gamma colors, read linear like the rest of this light)
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, DEFCFG_USE_SRGB_CONVERSION != 0 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, false );

			unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED;
			if ( bVertexAlpha )
				flags |= VERTEX_COLOR;
			int pTexCoordDim[3] = { 2, 0, 3 };
			pShaderShadow->VertexShaderVertexFormat( flags, 1, pTexCoordDim, 0 );

			DECLARE_STATIC_VERTEX_SHADER( deferred_decal_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, bVertexAlpha );
			SET_STATIC_VERTEX_SHADER( deferred_decal_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( deferred_glass_ps30 );
			SET_STATIC_PIXEL_SHADER_COMBO( VERTEXALPHA, bVertexAlpha );
			SET_STATIC_PIXEL_SHADER( deferred_glass_ps30 );

			DefaultFog();
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

			DECLARE_DYNAMIC_PIXEL_SHADER( deferred_glass_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER( deferred_glass_ps30 );

			if ( g_pHardwareConfig->HasFastVertexTextures() )
			{
				bool bUnusedTexCoords[3] = { false, false, !pShaderAPI->IsHWMorphingEnabled() };
				pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
			}

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
			// (the 3D skybox: its own coordinates - no shadow maps or rain map of it)
			const bool bSkybox = GetDeferredExt()->GetZScale() > 1.5f;

			BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_ShadowDepth_Ortho( 0 ) );
			ITexture *pRainDepth = GetDeferredExt()->GetTexture_RainMap();
			const bool bRainMap = !bSkybox && w.bEnabled && w.bRainMapValid && pRainDepth != NULL;
			if ( bRainMap )
				BindTexture( SHADER_SAMPLER2, pRainDepth );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );

			// the vertex shader of the decals: its G-buffer depth goes unused
			float vEye[4] = { 0, 0, 0, 0 };
			pShaderAPI->GetWorldSpaceCameraPosition( vEye );
			const float zScale[4] = { GetDeferredExt()->GetZScale(), 0, 0, 0 };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vEye );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, GetDeferredExt()->GetForwardBase() );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, zScale );

			// the eye of this view, the direction to the sun, its first three cascades
			pShaderAPI->SetPixelShaderConstant( 0, vEye, 1, true );
			pShaderAPI->SetPixelShaderConstant( 1, light.vecLight.Base(), 1, true );
			for ( int i = 0; i < 3; i++ )
			{
				const shadowData_ortho_t &data = GetDeferredExt()->GetShadowData_Ortho( i );
				pShaderAPI->SetPixelShaderConstant( 2 + i * 3, data.matWorldToTexture.Base(), 3, true );
				pShaderAPI->SetPixelShaderConstant( 26 + i, data.vecUVTransform.Base(), 1, true );
			}

			// the sun's light; the light of the sky above, at the horizon and of the ground below
			// (in the light buffer's units, as the composite's open sky, c36)
			const float flSkyBoost = GetGISkyBoost( GetDeferredExt()->GetGIData(), w );
			float c58[4] = { 0, 0, 0, bSkybox ? 0.0f : 1.0f };
			float c59[4] = { 0.5f, 0.55f, 0.6f, 1.0f }, c60[4] = { 0.6f, 0.62f, 0.65f, 0.0f }, c61[4] = { 0.2f, 0.2f, 0.2f, 0.0f };
			if ( light.bEnabled )
			{
				for ( int i = 0; i < 3; i++ )
				{
					c58[i] = light.diff[i];
					c59[i] = light.ambh[i] * flSkyBoost;
					// (the horizon: brighter and paler than the zenith; the ground: what the sun and
					// the sky light, about a fifth of it mirrored back)
					c60[i] = ( light.ambh[i] * 0.6f + light.ambl[i] * 0.4f ) * flSkyBoost * 1.15f;
					c61[i] = ( light.ambl[i] * flSkyBoost + light.diff[i] * 0.35f ) * 0.22f;
				}
			}
			pShaderAPI->SetPixelShaderConstant( 58, c58, 1, true );
			pShaderAPI->SetPixelShaderConstant( 59, c59, 1, true );
			pShaderAPI->SetPixelShaderConstant( 60, c60, 1, true );
			pShaderAPI->SetPixelShaderConstant( 61, c61, 1, true );

			const float flRoughness = ( params[ GLASS_ROUGHNESS ]->GetFloatValue() >= 0.0f ) ? params[ GLASS_ROUGHNESS ]->GetFloatValue() : r_deferred_glass_roughness.GetFloat();
			const float flDirt = ( params[ GLASS_DIRT ]->GetFloatValue() >= 0.0f ) ? params[ GLASS_DIRT ]->GetFloatValue() : r_deferred_glass_dirt.GetFloat();
			const float c62[4] = { clamp( flRoughness, 0.0f, 1.0f ), params[ GLASS_F0 ]->GetFloatValue(), r_deferred_glass_reflect.GetFloat(), flDirt };
			pShaderAPI->SetPixelShaderConstant( 62, c62, 1, true );
			pShaderAPI->SetPixelShaderFogParams( 63 );

			// the rain map: is there a roof above (24 units of margin, as the motes take)
			pShaderAPI->SetPixelShaderConstant( 64, w.matRainMap.Base(), 3, true );
			const float flRainRange = ( w.vecRainMapParams.x > 1e-9f ) ? ( 2.0f * w.vecRainMapParams.z / w.vecRainMapParams.x ) : 14000.0f;
			const float c67[4] = { 24.0f / flRainRange + w.vecRainMapParams.x, bRainMap ? 1.0f : 0.0f, r_deferred_glass_room.GetFloat(),
				(float)r_deferred_glass_debug.GetInt() };
			pShaderAPI->SetPixelShaderConstant( 67, c67, 1, true );
			const float c68[4] = { r_deferred_glass_glint.GetFloat(), r_deferred_glass_frame.GetFloat(), 1.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 68, c68, 1, true );

			// the scene around, mirrored: the main view's depth and its opaque scene, which the engine
			// copies into the full frame "depth" texture as the translucent pass starts (its rgb is the frame)
			const bool bSSR = r_deferred_glass_ssr.GetBool() && !bSkybox && !GetDeferredExt()->IsReflectionView();
			BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_Depth() );
			if ( bSSR )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_FRAME_BUFFER_FULL_DEPTH );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_BLACK );
			pShaderAPI->SetPixelShaderConstant( 69, GetDeferredExt()->GetMainViewToScreenTexBase(), 4, true );
			const float *pFwd = GetDeferredExt()->GetForwardBase();
			const float c73[4] = { pFwd[0], pFwd[1], pFwd[2], bSSR ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 73, c73, 1, true );
			const float c74[4] = { (float)r_deferred_glass_ssr_steps.GetInt(), 1.0f, r_deferred_glass_ssr_thickness.GetFloat(), 4.0f };
			pShaderAPI->SetPixelShaderConstant( 74, c74, 1, true );

			// the sky as it is seen (hl2rpm_sky_fxc.h)
			float c75[4], c76[4];
			const bool bSkyView = HL2RPM_GetSkySeenConstants( c75, c76 ) && params[ SKYVIEWLUT ]->IsTexture();
			if ( bSkyView )
				BindTexture( SHADER_SAMPLER5, SKYVIEWLUT );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_BLACK );
			c76[3] = bSkyView ? 1.0f : 0.0f;
			pShaderAPI->SetPixelShaderConstant( 75, c75, 1, true );
			pShaderAPI->SetPixelShaderConstant( 76, c76, 1, true );
		}
		Draw();
	}
END_SHADER

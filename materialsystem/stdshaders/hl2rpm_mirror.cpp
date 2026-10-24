//========= HL2RPM ============================================================//
//
// Purpose: HL2RPM_MIRROR - mirror-smooth surfaces of the lit opaque scene (the glass in
//          the windows of facade textures, panes that are textures, wet blood) mirror the
//          sky and the scene around them (hl2rpm_mirror_ps30). Drawn by the client
//          (weather_render.cpp, WeatherRender_PostOpaque) before the weather pass.
//
//=============================================================================//

#include "deferred_includes.h"
#include "hl2rpm_pbr.h"

#include "defconstruct_vs30.inc"
#include "hl2rpm_mirror_ps30.inc"

// (the client draws the pass and copies the frame for it while this is on)
ConVar r_deferred_reflections( "r_deferred_reflections", "1", FCVAR_ARCHIVE,
	"Mirror-smooth surfaces - the glass in the textures of facades, wet blood - mirror the sky and the scene around them by the angle of the view" );
static ConVar r_deferred_reflections_roughness( "r_deferred_reflections_roughness", "0.12", 0,
	"Reflections: the roughness below which a surface mirrors fully (fading out over 0.06 above it): glass, wet blood - not the glaze of a tile", true, 0.0f, true, 1.0f );
static ConVar r_deferred_reflections_room( "r_deferred_reflections_room", "0.12", 0,
	"Reflections: what a surface under a roof mirrors where the trace misses, of the light of the sky", true, 0.0f, true, 2.0f );
static ConVar r_deferred_reflections_strength( "r_deferred_reflections_strength", "1", 0, "Reflections: scale", true, 0.0f, true, 4.0f );
static ConVar r_deferred_reflections_glass_f0( "r_deferred_reflections_glass_f0", "0.14", 0,
	"Reflections: what the glass of the windows mirrors from the front (a window is several panes: ~15 %)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_reflections_debug( "r_deferred_reflections_debug", "0", FCVAR_CHEAT, "Reflections: r how much a pixel mirrors (x 4), g the scene found by the trace, b glass" );

// (the exposure of the procedural sky - $exposure of materials/skybox/sky_proc_atmo_01*.vmt)
static ConVar r_deferred_reflections_sky_exposure( "r_deferred_reflections_sky_exposure", "20", 0,
	"Reflections of the sky: the exposure the procedural sky is drawn with", true, 0.0f, true, 1000.0f );
static ConVar r_deferred_reflections_clouds( "r_deferred_reflections_clouds", "0.85", 0,
	"Reflections of the sky: how much of it the clouds hide, at full coverage", true, 0.0f, true, 1.0f );

bool HL2RPM_GetSkySeenConstants( float *pLightDir4, float *pParams4 )
{
	const weatherData_t &w = GetDeferredExt()->GetWeatherData();
	const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
	// (the light the sky-view LUT is built for: the weather's without a lightning flash, as the sky has it)
	const Vector4D &dir = w.bEnabled ? w.vecSkyLightDir : light.vecLight;
	pLightDir4[0] = dir.x;
	pLightDir4[1] = dir.y;
	pLightDir4[2] = dir.z;
	pLightDir4[3] = 0.0f;
	pParams4[0] = r_deferred_reflections_sky_exposure.GetFloat();
	pParams4[1] = w.bEnabled ? w.vecCloudParams0.x : 0.0f;
	pParams4[2] = r_deferred_reflections_clouds.GetFloat();
	pParams4[3] = light.bEnabled ? 1.0f : 0.0f;
	return light.bEnabled;
}

BEGIN_VS_SHADER( HL2RPM_MIRROR, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SKYVIEWLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_SKYVIEW_LUT, "the sky-view LUT of the procedural sky" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// (a parameter left at its default is not "defined": nothing would load the texture)
		if ( !params[ SKYVIEWLUT ]->IsDefined() )
			params[ SKYVIEWLUT ]->SetStringValue( DEFRTNAME_SKY_SKYVIEW_LUT );
	}

	SHADER_INIT
	{
		if ( params[ SKYVIEWLUT ]->IsDefined() )
			LoadTexture( SKYVIEWLUT );
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
			// (the frame copy holds gamma colors: read linear, as this pass writes)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, false );

			// result = src.rgb + dest * src.a
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_SRC_ALPHA );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( hl2rpm_mirror_ps30 );
			SET_STATIC_PIXEL_SHADER( hl2rpm_mirror_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( hl2rpm_mirror_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( hl2rpm_mirror_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Normals() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );
			const bool bRainMap = w.bEnabled && w.bRainMapValid && GetDeferredExt()->GetTexture_RainMap() != NULL;
			if ( bRainMap )
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_RainMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );
			// (the scene: what the weather pass traces too - the client copies it right before, the
			// render parameter says how many steps; 0 = no copy, the sky alone)
			const int iSSRSteps = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_HL2RPM_SSR );
			if ( iSSRSteps > 0 )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			// the sky above, at the horizon and the ground below - as deferred_glass.cpp has them
			const float flSkyBoost = GetGISkyBoost( GetDeferredExt()->GetGIData(), w );
			float c1[4] = { 0.5f, 0.55f, 0.6f, 0.0f }, c2[4] = { 0.6f, 0.62f, 0.65f, 0.0f }, c3[4] = { 0.2f, 0.2f, 0.2f, 0.0f };
			if ( light.bEnabled )
			{
				for ( int i = 0; i < 3; i++ )
				{
					c1[i] = light.ambh[i] * flSkyBoost;
					c2[i] = ( light.ambh[i] * 0.6f + light.ambl[i] * 0.4f ) * flSkyBoost * 1.15f;
					c3[i] = ( light.ambl[i] * flSkyBoost + light.diff[i] * 0.35f ) * 0.22f;
				}
			}
			pShaderAPI->SetPixelShaderConstant( 1, c1, 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, c3, 1, true );

			pShaderAPI->SetPixelShaderConstant( 4, w.matRainMap.Base(), 3, true );
			const float c7[4] = { w.vecRainMapParams.x, w.vecRainMapParams.y, w.vecRainMapParams.z, bRainMap ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 7, c7, 1, true );

			pShaderAPI->SetPixelShaderConstant( 8, GetDeferredExt()->GetMainViewToScreenTexBase(), 4, true );
			const float c12[4] = { (float)iSSRSteps, 0.0f, 0.0f, 1.0f };
			pShaderAPI->SetPixelShaderConstant( 12, c12, 1, true );
			const float c13[4] = { r_deferred_reflections_strength.GetFloat(), r_deferred_reflections_roughness.GetFloat(), 1.0f / 0.06f,
				r_deferred_reflections_room.GetFloat() };
			pShaderAPI->SetPixelShaderConstant( 13, c13, 1, true );
			const float c14[4] = { r_deferred_reflections_glass_f0.GetFloat(), (float)r_deferred_reflections_debug.GetInt(), 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 14, c14, 1, true );

			// the sky as it is seen (hl2rpm_sky_fxc.h)
			float c15[4], c16[4];
			const bool bSkyView = HL2RPM_GetSkySeenConstants( c15, c16 ) && params[ SKYVIEWLUT ]->IsTexture();
			if ( bSkyView )
				BindTexture( SHADER_SAMPLER4, SKYVIEWLUT );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_BLACK );
			c16[3] = bSkyView ? 1.0f : 0.0f;
			pShaderAPI->SetPixelShaderConstant( 15, c15, 1, true );
			pShaderAPI->SetPixelShaderConstant( 16, c16, 1, true );
		}

		Draw();
	}
END_SHADER

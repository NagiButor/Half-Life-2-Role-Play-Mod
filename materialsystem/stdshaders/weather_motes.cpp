//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_MOTES - dust motes in the air, lit by the sun through its
//          cascaded shadows (weather_motes_vs30/ps30, drawn by weather_render.cpp).
//
//=============================================================================//

#include "deferred_includes.h"

#include "weather_motes_vs30.inc"
#include "weather_motes_ps30.inc"

static ConVar r_weather_motes_box( "r_weather_motes_box", "360", 0, "Dust motes: size of the volume around the camera they float in (units)", true, 100.0f, true, 1500.0f );
static ConVar r_weather_motes_size( "r_weather_motes_size", "0.22", 0, "Dust motes: size of a mote (units)", true, 0.02f, true, 4.0f );
static ConVar r_weather_motes_minsize( "r_weather_motes_minsize", "0.0036", 0, "Dust motes: smallest size on screen, per unit of distance (0.0036 is about 2 pixels at 1600 wide)", true, 0.0005f, true, 0.05f );
static ConVar r_weather_motes_intensity( "r_weather_motes_intensity", "0.8", 0, "Dust motes: brightness in the sun", true, 0.0f, true, 10.0f );
static ConVar r_weather_motes_outdoor( "r_weather_motes_outdoor", "0.08", 0, "Dust motes: brightness outdoors relative to indoors (the bright air hides them)", true, 0.0f, true, 1.0f );
static ConVar r_weather_motes_speed( "r_weather_motes_speed", "1", 0, "Dust motes: drift speed", true, 0.0f, true, 10.0f );
static ConVar r_weather_motes_g( "r_weather_motes_g", "0.55", 0, "Dust motes: forward scattering (0 = the same from every side, toward 1 = only looking toward the sun)", true, 0.0f, true, 0.95f );
static ConVar r_weather_motes_density( "r_weather_motes_density", "1", 0, "Dust motes: share of the motes drawn (0..1)", true, 0.0f, true, 1.0f );
static ConVar r_weather_motes_debug( "r_weather_motes_debug", "0", FCVAR_CHEAT, "Dust motes debug: draw every mote grey (red = indoors)" );

static float GetRainMapDepthRangeMotes( const weatherData_t &w )
{
	return ( w.vecRainMapParams.x > 1e-9f ) ? ( 2.0f * w.vecRainMapParams.z / w.vecRainMapParams.x ) : 14000.0f;
}

BEGIN_VS_SHADER( WEATHER_MOTES, "" )
	BEGIN_SHADER_PARAMS
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_TRANSLUCENT );
		SET_FLAGS( MATERIAL_VAR_NOFOG );
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
			pShaderShadow->EnableDepthTest( true );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableCulling( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			int nTexCoordDims[2] = { 2, 4 };
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 2, nTexCoordDims, 0 );

			DECLARE_STATIC_VERTEX_SHADER( weather_motes_vs30 );
			SET_STATIC_VERTEX_SHADER( weather_motes_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_motes_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_motes_ps30 );

			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();

			DECLARE_DYNAMIC_VERTEX_SHADER( weather_motes_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( weather_motes_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_motes_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( weather_motes_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_ShadowDepth_Ortho( 0 ) );
			ITexture *pRainDepth = GetDeferredExt()->GetTexture_RainMap();
			const bool bRainMap = w.bEnabled && w.bRainMapValid && pRainDepth != NULL;
			if ( bRainMap )
				BindTexture( SHADER_SAMPLER1, pRainDepth );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_WHITE );

			// vertex constants
			const float *pOrigin = GetDeferredExt()->GetOriginBase();
			const float vCamera[4] = { pOrigin[0], pOrigin[1], pOrigin[2], pShaderAPI->CurrentTime() };
			const float vMotes[4] = { r_weather_motes_density.GetFloat(), r_weather_motes_box.GetFloat(),
				r_weather_motes_size.GetFloat(), r_weather_motes_speed.GetFloat() };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vCamera, 1, true );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, vMotes, 1, true );
			const float vMotes2[4] = { r_weather_motes_minsize.GetFloat(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, vMotes2, 1, true );

			// pixel constants: the sun and its cascades as for the sun shafts
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );
			CommitGlobalLightForward( pShaderAPI, 1 );
			CommitShadowProjectionConstants_Ortho_Composite( pShaderAPI, SHADOW_NUM_CASCADES, 2 );
			pShaderAPI->SetPixelShaderConstant( 58, light.diff.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 65, w.matRainMap.Base(), 3, true );
			const float flLight[4] = { 24.0f / GetRainMapDepthRangeMotes( w ) + w.vecRainMapParams.x, r_weather_motes_intensity.GetFloat(),
				bRainMap ? 1.0f : 0.0f, r_weather_motes_outdoor.GetFloat() };
			pShaderAPI->SetPixelShaderConstant( 68, flLight, 1, true );
			const float flParams[4] = { r_weather_motes_g.GetFloat(), (float)r_weather_motes_debug.GetInt(), 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 69, flParams, 1, true );
		}

		Draw();
	}
END_SHADER

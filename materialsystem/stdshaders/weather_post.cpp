//========= HL2RPM ============================================================//
//
// Purpose: WEATHER_POST - wet surfaces, puddles, height fog and atmospheric
//          aerial perspective (weather_post_ps30).
//
//=============================================================================//

#include "deferred_includes.h"
#include "lighting_helper.h"

#include "defconstruct_vs30.inc"
#include "weather_post_ps30.inc"

static FORCEINLINE float WPClamp01( float x )
{
	return ( x < 0.0f ) ? 0.0f : ( ( x > 1.0f ) ? 1.0f : x );
}

static FORCEINLINE float WPSmooth01( float x )
{
	x = WPClamp01( x );
	return x * x * ( 3.0f - 2.0f * x );
}

static FORCEINLINE float WPRemap01( float x, float a, float b )
{
	return WPClamp01( ( x - a ) / ( b - a ) );
}

BEGIN_VS_SHADER( WEATHER_POST, "" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SKYVIEWLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_SKYVIEW_LUT, "the sky-view LUT of the procedural sky" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[SKYVIEWLUT]->IsDefined() )
			params[SKYVIEWLUT]->SetStringValue( DEFRTNAME_SKY_SKYVIEW_LUT );
	}

	SHADER_INIT
	{
		if ( params[SKYVIEWLUT]->IsDefined() )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// HL2RPM: the lit opaque scene (reflections)
			// (the frame copy holds gamma colors: read linear, as this pass writes - read as they were, the
			// mirrored scene came out brighter than the scene itself)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );	// HL2RPM: procedural sky-view LUT (aerial perspective)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );	// HL2RPM: half-res SSAO + SkyVisibility (indoor fog mask)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER6, false );

			// result = src.rgb + dest * src.a
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_SRC_ALPHA );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_STATIC_VERTEX_SHADER_COMBO( USEWORLDTRANSFORM, 0 );
			SET_STATIC_VERTEX_SHADER_COMBO( SENDWORLDPOS, 0 );
			SET_STATIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( weather_post_ps30 );
			SET_STATIC_PIXEL_SHADER( weather_post_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
			const ssaoData_t &ao = GetDeferredExt()->GetSSAOData();
			const giData_t &gi = GetDeferredExt()->GetGIData();

			const bool bWet = w.vecRainParams.y > 0.001f;
			const bool bDebug = w.vecDebug.x > 0.5f;
			const float flAerialDensity = Max( w.vecSkyHorizon.w, 0.0f );
			const float flFogSkyColor = WPClamp01( w.vecSkyLight.w );

			DECLARE_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( defconstruct_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( weather_post_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WET, ( bWet || bDebug ) ? 1 : 0 );
			SET_DYNAMIC_PIXEL_SHADER( weather_post_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_Normals() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_Depth() );
			if ( ( bWet || bDebug ) && GetDeferredExt()->GetTexture_RainMap() )
				BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_RainMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );
			if ( bWet && GetDeferredExt()->GetTexture_WeatherMap() )
				BindTexture( SHADER_SAMPLER3, GetDeferredExt()->GetTexture_WeatherMap() );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_GREY );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			const float flOvercast = WPSmooth01( WPRemap01( WPClamp01( w.vecCloudParams0.x ), 0.65f, 0.96f ) );
			const float flOvercastDim = 1.0f - flOvercast * WPClamp01( w.vecCloudParams0.w ) * 0.42f;
			const float flOpenSky = WPClamp01( ( 1.0f - flOvercast * 0.82f ) * ( flFogSkyColor / 0.85f ) );

			float c4[4] = { w.vecSkyZenith.x, w.vecSkyZenith.y, w.vecSkyZenith.z, flFogSkyColor };
			float c5[4] = { w.vecSkyHorizon.x, w.vecSkyHorizon.y, w.vecSkyHorizon.z, flOvercastDim };
			pShaderAPI->SetPixelShaderConstant( 1, w.vecRainParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 2, w.vecFogParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 3, w.vecFogColor.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 4, c4, 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, c5, 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, light.vecLight.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, light.diff.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, w.matRainMap.Base(), 3, true );

			float flRainMap[4] = { w.vecRainMapParams.x, w.vecRainMapParams.y, w.vecRainMapParams.z, w.bRainMapValid ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 11, flRainMap, 1, true );

			const bool bFogOn = ( w.vecFogParams.x > 0.0f ) || ( flAerialDensity > 0.0f );
			float flFlags[4] = { bFogOn ? 1.0f : 0.0f, bWet ? 1.0f : 0.0f, w.vecSunColor.w, w.vecDebug.x };
			pShaderAPI->SetPixelShaderConstant( 12, flFlags, 1, true );

			// HL2RPM: screen space reflections in puddles / on wet surfaces - only in the main view,
			// which copies its scene to _rt_FullFrameFB right before this pass (render parm = steps)
			const int iSSRSteps = bWet ? pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_HL2RPM_SSR ) : 0;
			if ( iSSRSteps > 0 )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_FRAME_BUFFER_FULL_TEXTURE_0 );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_BLACK );
			pShaderAPI->SetPixelShaderConstant( 13, GetDeferredExt()->GetMainViewToScreenTexBase(), 4, true );
			static ConVarRef r_weather_ssr_puddles( "r_weather_ssr_puddles" );
			static ConVarRef r_weather_ssr_wet( "r_weather_ssr_wet" );
			float flSSR[4] = { (float)iSSRSteps, r_weather_ssr_puddles.IsValid() ? r_weather_ssr_puddles.GetFloat() : 1.0f,
				r_weather_ssr_wet.IsValid() ? r_weather_ssr_wet.GetFloat() : 0.6f, 1.0f };
			pShaderAPI->SetPixelShaderConstant( 17, flSSR, 1, true );

			// HL2RPM: atmospheric aerial perspective constants (c18..c24, strictly below reserved c28..c31)
			const bool bSkyView = light.bEnabled && params[SKYVIEWLUT]->IsTexture();
			if ( bSkyView )
				BindTexture( SHADER_SAMPLER5, SKYVIEWLUT );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_BLACK );

			ITexture *pSSAO = GetDeferredExt()->GetTexture_SSAO();
			const bool bAO = ao.bEnabled && pSSAO != NULL && ao.vecApply.w > 0.0f;
			if ( bAO )
				BindTexture( SHADER_SAMPLER6, pSSAO );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_WHITE );

			static ConVarRef r_weather_fog_aerial_start( "r_weather_fog_aerial_start" );
			static ConVarRef r_weather_fog_aerial_falloff( "r_weather_fog_aerial_falloff" );
			static ConVarRef r_weather_fog_aerial_indoor( "r_weather_fog_aerial_indoor" );
			static ConVarRef r_deferred_reflections_sky_exposure( "r_deferred_reflections_sky_exposure" );

			const float flAerialStart = r_weather_fog_aerial_start.IsValid() ? Max( r_weather_fog_aerial_start.GetFloat(), 0.0f ) : 150.0f;
			const float flAerialFalloff = r_weather_fog_aerial_falloff.IsValid() ? Max( r_weather_fog_aerial_falloff.GetFloat(), 0.0f ) : 0.12f;
			const float flIndoorFloor = r_weather_fog_aerial_indoor.IsValid() ? WPClamp01( r_weather_fog_aerial_indoor.GetFloat() ) : 0.12f;
			const float flSkyExposure = r_deferred_reflections_sky_exposure.IsValid() ? Max( r_deferred_reflections_sky_exposure.GetFloat(), 0.0f ) : 20.0f;

			float c18[4] = { flAerialDensity, flAerialFalloff, flAerialStart, 1.0f / 600.0f };
			pShaderAPI->SetPixelShaderConstant( 18, c18, 1, true );

			const float flCamSky = WPClamp01( gi.vecParams2.x );
			const float flSkyLutWeight = bSkyView ? ( 0.55f + flOpenSky * 0.45f ) : 0.0f;
			float c19[4] = { flCamSky, bAO ? ao.vecApply.w : 0.0f, flSkyLutWeight, flIndoorFloor };
			pShaderAPI->SetPixelShaderConstant( 19, c19, 1, true );

			// Precompute frame-invariant procedural sky horizon terms matching skyatmo_proc.cpp (c20..c27, strictly below c28..c31)
			Vector vecLightDir = w.bEnabled ? w.vecSkyLightDir.AsVector3D() : light.vecLight.AsVector3D();
			if ( vecLightDir.LengthSqr() > 1e-8f )
				vecLightDir.NormalizeInPlace();
			else
				vecLightDir.Init( 0.0f, 0.0f, 1.0f );

			Vector vecSunDir = w.bEnabled ? w.vecSunDir.AsVector3D() : vecLightDir;
			if ( vecSunDir.LengthSqr() > 1e-8f )
				vecSunDir.NormalizeInPlace();
			else
				vecSunDir = vecLightDir;

			float lightHX = vecLightDir.x;
			float lightHY = vecLightDir.y;
			const float flLightHLen = sqrtf( lightHX * lightHX + lightHY * lightHY );
			if ( flLightHLen > 1e-4f )
			{
				lightHX /= flLightHLen;
				lightHY /= flLightHLen;
			}
			else
			{
				lightHX = 1.0f;
				lightHY = 0.0f;
			}

			float sunHX = vecSunDir.x + 0.0001f;
			float sunHY = vecSunDir.y;
			const float flSunHLen = sqrtf( sunHX * sunHX + sunHY * sunHY );
			if ( flSunHLen > 1e-6f )
			{
				sunHX /= flSunHLen;
				sunHY /= flSunHLen;
			}
			else
			{
				sunHX = 1.0f;
				sunHY = 0.0f;
			}

			float c20[4] = { lightHX, lightHY, flSkyExposure, bSkyView ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 20, c20, 1, true );

			const float flSunAltDeg = RAD2DEG( asinf( clamp( vecSunDir.z, -1.0f, 1.0f ) ) );
			const float flNightVis = WPSmooth01( WPRemap01( -flSunAltDeg, 2.0f, 18.0f ) );
			const float flAmbientFactor = 0.012f + flNightVis * ( 0.00035f - 0.012f );

			const float flGolden = WPSmooth01( 1.0f - WPRemap01( flSunAltDeg, 1.0f, 10.0f ) )
				* WPSmooth01( WPRemap01( flSunAltDeg, -2.0f, 1.0f ) );
			const float flCivil = WPSmooth01( WPRemap01( flSunAltDeg, -7.0f, -2.0f ) )
				* ( 1.0f - WPSmooth01( WPRemap01( flSunAltDeg, 0.0f, 4.0f ) ) );
			const float flNautical = WPSmooth01( WPRemap01( flSunAltDeg, -8.0f, -3.0f ) )
				* ( 1.0f - WPSmooth01( WPRemap01( flSunAltDeg, -2.0f, 1.0f ) ) );
			const float flBlueHour = WPSmooth01( WPRemap01( -flSunAltDeg, 2.0f, 7.0f ) );
			const float flNight = WPSmooth01( WPRemap01( -flSunAltDeg, 8.0f, 12.0f ) );
			const float flSunriseBias = ( vecSunDir.y >= 0.0f ) ? 1.0f : 0.0f;

			const float flBlueT = WPClamp01( flBlueHour * 0.65f );
			const float flNightT = WPClamp01( flNight * 0.45f );
			const float bColR = 0.60f + flSunriseBias * ( 0.52f - 0.60f );
			const float bColG = 0.78f + flSunriseBias * ( 0.72f - 0.78f );
			const float bColB = 1.35f + flSunriseBias * ( 1.28f - 1.35f );
			float c21[4] = {
				( ( 1.0f - flBlueT ) + flBlueT * bColR ) * ( ( 1.0f - flNightT ) + flNightT * 0.25f ),
				( ( 1.0f - flBlueT ) + flBlueT * bColG ) * ( ( 1.0f - flNightT ) + flNightT * 0.34f ),
				( ( 1.0f - flBlueT ) + flBlueT * bColB ) * ( ( 1.0f - flNightT ) + flNightT * 0.42f ),
				WPClamp01( w.vecAtmoParams.y )
			};
			pShaderAPI->SetPixelShaderConstant( 21, c21, 1, true );

			const Vector4D vecHalfAmb = MakeHalfAmbient( light.ambl, light.ambh );
			float c22[4] = {
				Max( 0.0f, vecHalfAmb.x ) * flAmbientFactor,
				Max( 0.0f, vecHalfAmb.y ) * flAmbientFactor,
				Max( 0.0f, vecHalfAmb.z ) * flAmbientFactor,
				w.vecAtmoParams.z
			};
			pShaderAPI->SetPixelShaderConstant( 22, c22, 1, true );

			const bool bTwilightGlow = ( flGolden + flCivil + flNautical ) > 1e-4f;
			const float flAerosol = 1.15f; // matches $aerosol in sky_proc_atmo_01*.vmt
			const Vector4D &srcDiff = w.bEnabled ? w.vecSkyLightDiff : light.diff;
			const Vector vecLightCol( Max( srcDiff.x, 0.0f ), Max( srcDiff.y, 0.0f ), Max( srcDiff.z, 0.0f ) );

			const Vector vecGoldenTint(
				1.00f,
				0.76f + flSunriseBias * ( 0.88f - 0.76f ),
				0.40f + flSunriseBias * ( 0.58f - 0.40f ) );
			const Vector vecGoldenCol = vecGoldenTint * vecLightCol * ( flGolden * flAerosol );

			const Vector vecCivilTint(
				1.00f,
				0.42f + flSunriseBias * ( 0.65f - 0.42f ),
				0.18f + flSunriseBias * ( 0.40f - 0.18f ) );
			const Vector vecCivilWide = vecCivilTint * ( flCivil * flAerosol * 0.020f );
			const Vector vecCivilCol = vecCivilTint * vecLightCol * ( flCivil * flAerosol );
			const Vector vecVenusTint( 0.95f, 0.52f, 0.92f );

			// Exact same un-scaled twilight components as skyatmo_proc.cpp (evaluated per-pixel at elev)
			float c23[4] = {
				vecGoldenCol.x * 0.025f + vecCivilWide.x,
				vecGoldenCol.y * 0.025f + vecCivilWide.y,
				vecGoldenCol.z * 0.025f + vecCivilWide.z,
				bTwilightGlow ? 1.0f : 0.0f
			};
			pShaderAPI->SetPixelShaderConstant( 23, c23, 1, true );

			float c24[4] = {
				vecGoldenCol.x * 0.100f,
				vecGoldenCol.y * 0.100f,
				vecGoldenCol.z * 0.100f,
				sunHX
			};
			pShaderAPI->SetPixelShaderConstant( 24, c24, 1, true );

			float c25[4] = {
				vecCivilCol.x * 0.020f + vecVenusTint.x * ( flNautical * 0.002f ),
				vecCivilCol.y * 0.020f + vecVenusTint.y * ( flNautical * 0.002f ),
				vecCivilCol.z * 0.020f + vecVenusTint.z * ( flNautical * 0.002f ),
				sunHY
			};
			pShaderAPI->SetPixelShaderConstant( 25, c25, 1, true );

			const float flHazeK = WPClamp01( ( w.vecAtmoParams.x - 1.0f ) * 0.25f + flAerialDensity * 1.4f );
			float c26[4] = {
				vecCivilCol.x * 0.250f,
				vecCivilCol.y * 0.250f,
				vecCivilCol.z * 0.250f,
				flHazeK
			};
			pShaderAPI->SetPixelShaderConstant( 26, c26, 1, true );

			float c27[4] = {
				vecVenusTint.x * ( flNautical * 0.030f ),
				vecVenusTint.y * ( flNautical * 0.030f ),
				vecVenusTint.z * ( flNautical * 0.030f ),
				0.0f
			};
			pShaderAPI->SetPixelShaderConstant( 27, c27, 1, true );
		}

		Draw();
	}
END_SHADER

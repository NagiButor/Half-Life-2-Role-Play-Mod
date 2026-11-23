#include "BaseVSShader.h"

#include "deferred_includes.h"
#include "lighting_helper.h"

#include "procsky_atmo_vs30.inc"
#include "procsky_atmo_ps30.inc"

// HL2RPM: the sky is drawn first, without a depth test - the whole screen ran its shader, also where
// buildings are drawn over it a moment later. In the main view the G-buffer of the same screen
// tells which pixels geometry covers: they leave at once.
static FORCEINLINE float SkyClamp01( float x )
{
	return ( x < 0.0f ) ? 0.0f : ( ( x > 1.0f ) ? 1.0f : x );
}

static FORCEINLINE float SkySmooth01( float x )
{
	x = SkyClamp01( x );
	return x * x * ( 3.0f - 2.0f * x );
}

static FORCEINLINE float SkyRemap01( float x, float a, float b )
{
	return SkyClamp01( ( x - a ) / ( b - a ) );
}

static ConVar r_deferred_sky_skip_covered( "r_deferred_sky_skip_covered", "1", FCVAR_NONE,
	"HL2RPM: the procedural sky skips the pixels the G-buffer has geometry in (main view only; 0 = shades every pixel)" );

BEGIN_VS_SHADER( SkyAtmoProc, "" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SKYFACE, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( EXPOSURE, SHADER_PARAM_TYPE_FLOAT, "20.0", "" )
		SHADER_PARAM( STARSINTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNDISKINTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNRISEBIAS, SHADER_PARAM_TYPE_FLOAT, "-1.0", "" )
		SHADER_PARAM( AEROSOL, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( STARROTATIONSPEED, SHADER_PARAM_TYPE_FLOAT, "0.00001157", "" )
		SHADER_PARAM( USELUT, SHADER_PARAM_TYPE_BOOL, "1", "" )
		SHADER_PARAM( SKYLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_ATMO_LUT, "" )
		SHADER_PARAM( TRANSMITTANCELUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_TRANSMITTANCE_LUT, "" )
		SHADER_PARAM( SKYVIEWLUT, SHADER_PARAM_TYPE_TEXTURE, DEFRTNAME_SKY_SKYVIEW_LUT, "" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_NOFOG );
		SET_FLAGS( MATERIAL_VAR_IGNOREZ );
	}

	SHADER_INIT
	{
		// HL2RPM: default values don't count as "defined" -> the transmittance LUT was
		// never loaded (the sun disk sampled the error texture).
		if ( !params[TRANSMITTANCELUT]->IsDefined() )
			params[TRANSMITTANCELUT]->SetStringValue( DEFRTNAME_SKY_TRANSMITTANCE_LUT );
		if ( !params[SKYVIEWLUT]->IsDefined() )
			params[SKYVIEWLUT]->SetStringValue( DEFRTNAME_SKY_SKYVIEW_LUT );
		LoadTexture( TRANSMITTANCELUT );
		LoadTexture( SKYVIEWLUT );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );

			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );	// HL2RPM clouds
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );	// HL2RPM weather map
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );	// HL2RPM G-buffer depth

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( procsky_atmo_vs30 );
			SET_STATIC_VERTEX_SHADER( procsky_atmo_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( procsky_atmo_ps30 );
			SET_STATIC_PIXEL_SHADER( procsky_atmo_ps30 );

			pShaderShadow->EnableSRGBWrite( true );
		}

		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			BindTexture( SHADER_SAMPLER1, TRANSMITTANCELUT, -1 );
			BindTexture( SHADER_SAMPLER2, SKYVIEWLUT, -1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( procsky_atmo_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( procsky_atmo_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( procsky_atmo_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( procsky_atmo_ps30 );

			const lightData_Global_t& data = GetDeferredExt()->GetLightData_Global();

			CommitGlobalLightForward( pShaderAPI, 1 );

			float procParams0[4] = { 0, 0, 0, 0 };
			procParams0[0] = ( params[USELUT]->IsDefined() && params[USELUT]->GetIntValue() ) ? 1.0f : 0.0f;
			procParams0[1] = ( params[EXPOSURE]->IsDefined() ) ? params[EXPOSURE]->GetFloatValue() : 20.0f;
			procParams0[2] = ( params[STARSINTENSITY]->IsDefined() ) ? params[STARSINTENSITY]->GetFloatValue() : 1.0f;
			procParams0[3] = ( params[SUNDISKINTENSITY]->IsDefined() ) ? params[SUNDISKINTENSITY]->GetFloatValue() : 1.0f;
			pShaderAPI->SetPixelShaderConstant( 2, procParams0, 1, true );

			float procParams1[4] = { 0, 0, 0, 0 };
			procParams1[0] = ( params[SUNRISEBIAS]->IsDefined() ) ? params[SUNRISEBIAS]->GetFloatValue() : -1.0f;
			procParams1[1] = ( params[AEROSOL]->IsDefined() ) ? params[AEROSOL]->GetFloatValue() : 1.0f;
			const float starSpeed = ( params[STARROTATIONSPEED]->IsDefined() ) ? params[STARROTATIONSPEED]->GetFloatValue() : 0.00001157f;
			procParams1[2] = pShaderAPI->CurrentTime() * starSpeed;
			pShaderAPI->SetPixelShaderConstant( 3, procParams1, 1, true );

			const Vector4D vecHalfAmb = MakeHalfAmbient( data.ambl, data.ambh );
			pShaderAPI->SetPixelShaderConstant( 16, data.diff.Base(), 1, true );

			// HL2RPM: dynamic weather
			const weatherData_t &w = GetDeferredExt()->GetWeatherData();
			ITexture *pClouds = GetDeferredExt()->GetTexture_Clouds();
			ITexture *pWeatherMap = GetDeferredExt()->GetTexture_WeatherMap();
			const bool bCloudsValid = w.bEnabled && w.bCloudTextureValid && pClouds != NULL;

			if ( bCloudsValid )
				BindTexture( SHADER_SAMPLER3, pClouds );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_BLACK );

			if ( w.bEnabled && pWeatherMap )
				BindTexture( SHADER_SAMPLER4, pWeatherMap );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_GREY );

			pShaderAPI->SetPixelShaderConstant( 4, w.vecCloudParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 5, w.vecCloudParams1.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 6, w.vecAtmoParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 7, w.vecFogColor.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 8, w.vecFogParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 9, w.vecScreenParams.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 10, w.vecMoonDir.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 11, w.vecSunDir.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 12, w.vecLightning.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 13, w.vecWind.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( 14, w.vecSkyHorizon.Base(), 1, true );
			float flFlags[4] = { w.bEnabled ? 1.0f : 0.0f, bCloudsValid ? 1.0f : 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 15, flFlags, 1, true );

			// HL2RPM: precompute frame-invariant twilight, ambient, sun transmittance, wind & moon constants
			{
				Vector vecLightDir = w.bEnabled ? w.vecSkyLightDir.AsVector3D() : data.vecLight.AsVector3D();
				if ( vecLightDir.LengthSqr() > 1e-8f )
					vecLightDir.NormalizeInPlace();
				else
					vecLightDir.Init( 0.0f, 0.0f, 1.0f );

				Vector vecSunDir = w.bEnabled ? w.vecSunDir.AsVector3D() : vecLightDir;
				if ( vecSunDir.LengthSqr() > 1e-8f )
					vecSunDir.NormalizeInPlace();
				else
					vecSunDir = vecLightDir;

				const float flSunAltDeg = RAD2DEG( asinf( clamp( vecSunDir.z, -1.0f, 1.0f ) ) );
				const float flNightVis = SkySmooth01( SkyRemap01( -flSunAltDeg, 2.0f, 18.0f ) );
				const float flAmbientFactor = 0.012f + flNightVis * ( 0.00035f - 0.012f );

				const float flStarsVis = SkySmooth01( SkyRemap01( -flSunAltDeg, 6.0f, 18.0f ) );
				const float flStarsScale = Max( procParams0[2], 0.0f ) * flStarsVis * 3.0f;

				float c20[4] = {
					Max( 0.0f, 2.0f * vecHalfAmb.x - data.ambh.x ) * flAmbientFactor,
					Max( 0.0f, 2.0f * vecHalfAmb.y - data.ambh.y ) * flAmbientFactor,
					Max( 0.0f, 2.0f * vecHalfAmb.z - data.ambh.z ) * flAmbientFactor,
					flStarsScale
				};
				pShaderAPI->SetPixelShaderConstant( 20, c20, 1, true );

				const float flGolden = SkySmooth01( 1.0f - SkyRemap01( flSunAltDeg, 1.0f, 10.0f ) )
				                     * SkySmooth01( SkyRemap01( flSunAltDeg, -2.0f, 1.0f ) );
				const float flCivil = SkySmooth01( SkyRemap01( flSunAltDeg, -7.0f, -2.0f ) )
				                    * ( 1.0f - SkySmooth01( SkyRemap01( flSunAltDeg, 0.0f, 4.0f ) ) );
				const float flNautical = SkySmooth01( SkyRemap01( flSunAltDeg, -8.0f, -3.0f ) )
				                       * ( 1.0f - SkySmooth01( SkyRemap01( flSunAltDeg, -2.0f, 1.0f ) ) );
				const float flBlueHour = SkySmooth01( SkyRemap01( -flSunAltDeg, 2.0f, 7.0f ) );
				const float flNight = SkySmooth01( SkyRemap01( -flSunAltDeg, 8.0f, 12.0f ) );

				const bool bTwilightGlow = ( flGolden + flCivil + flNautical ) > 1e-4f;
				float c21[4] = {
					data.ambh.x * flAmbientFactor,
					data.ambh.y * flAmbientFactor,
					data.ambh.z * flAmbientFactor,
					bTwilightGlow ? 1.0f : 0.0f
				};
				pShaderAPI->SetPixelShaderConstant( 21, c21, 1, true );

				const float flSunriseBias = ( procParams1[0] < 0.0f ) ? ( vecSunDir.y >= 0.0f ? 1.0f : 0.0f ) : SkyClamp01( procParams1[0] );
				const float flAerosol = Max( procParams1[1], 0.0f );

				const Vector4D &srcDiff = w.bEnabled ? w.vecSkyLightDiff : data.diff;
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

				float c22[4] = {
					vecGoldenCol.x * 0.025f + vecCivilWide.x,
					vecGoldenCol.y * 0.025f + vecCivilWide.y,
					vecGoldenCol.z * 0.025f + vecCivilWide.z,
					sunHX
				};
				pShaderAPI->SetPixelShaderConstant( 22, c22, 1, true );

				float c23[4] = {
					vecGoldenCol.x * 0.100f,
					vecGoldenCol.y * 0.100f,
					vecGoldenCol.z * 0.100f,
					sunHY
				};
				pShaderAPI->SetPixelShaderConstant( 23, c23, 1, true );

				const float flGroundR = 6.360f;
				const float flViewZ = 6.3602f;
				const float flB = flViewZ * vecLightDir.z;
				const float flC = flViewZ * flViewZ - flGroundR * flGroundR;
				const bool bSunHitsGround = ( flB <= 0.0f ) && ( flB * flB - flC >= 0.0f );
				const float flSunTransU = clamp( vecLightDir.z, -1.0f, 1.0f ) * 0.5f + 0.5f;

				float c24[4] = {
					vecCivilCol.x * 0.020f + vecVenusTint.x * ( flNautical * 0.002f ),
					vecCivilCol.y * 0.020f + vecVenusTint.y * ( flNautical * 0.002f ),
					vecCivilCol.z * 0.020f + vecVenusTint.z * ( flNautical * 0.002f ),
					flSunTransU
				};
				pShaderAPI->SetPixelShaderConstant( 24, c24, 1, true );

				float c25[4] = {
					vecCivilCol.x * 0.250f,
					vecCivilCol.y * 0.250f,
					vecCivilCol.z * 0.250f,
					bSunHitsGround ? 0.0f : 1.0f
				};
				pShaderAPI->SetPixelShaderConstant( 25, c25, 1, true );

				const float windX = 0.8f;
				const float windY = 0.6f;

				float c26[4] = {
					vecVenusTint.x * ( flNautical * 0.030f ),
					vecVenusTint.y * ( flNautical * 0.030f ),
					vecVenusTint.z * ( flNautical * 0.030f ),
					windX
				};
				pShaderAPI->SetPixelShaderConstant( 26, c26, 1, true );

				const float flBlueT = SkyClamp01( flBlueHour * 0.65f );
				const float flNightT = SkyClamp01( flNight * 0.45f );
				const float bColR = 0.60f + flSunriseBias * ( 0.52f - 0.60f );
				const float bColG = 0.78f + flSunriseBias * ( 0.72f - 0.78f );
				const float bColB = 1.35f + flSunriseBias * ( 1.28f - 1.35f );
				float c27[4] = {
					( ( 1.0f - flBlueT ) + flBlueT * bColR ) * ( ( 1.0f - flNightT ) + flNightT * 0.25f ),
					( ( 1.0f - flBlueT ) + flBlueT * bColG ) * ( ( 1.0f - flNightT ) + flNightT * 0.34f ),
					( ( 1.0f - flBlueT ) + flBlueT * bColB ) * ( ( 1.0f - flNightT ) + flNightT * 0.42f ),
					windY
				};
				pShaderAPI->SetPixelShaderConstant( 27, c27, 1, true );

				Vector vecMoonFwd = w.vecMoonDir.AsVector3D();
				if ( vecMoonFwd.LengthSqr() > 1e-8f )
					vecMoonFwd.NormalizeInPlace();
				else
					vecMoonFwd.Init( 0.0f, 0.0f, 1.0f );
				Vector vecMoonRight = CrossProduct( Vector( 0.0f, 0.0f, 1.0f ), vecMoonFwd ) + Vector( 1e-5f, 0.0f, 0.0f );
				vecMoonRight.NormalizeInPlace();
				float c17[4] = { vecMoonRight.x, vecMoonRight.y, vecMoonRight.z, 0.35f * SkySmooth01( w.vecMoonDir.w * 1.5f ) };
				pShaderAPI->SetPixelShaderConstant( 17, c17, 1, true );

				pShaderAPI->SetPixelShaderConstant( 18, w.vecCloudUVRect.Base(), 1, true );
			}

			// HL2RPM: the main view - drawn to the back buffer or scaled scene RT over the whole
			// G-buffer (not reflections, refractions, glass, monitors or item previews).
			float flSkip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			ITexture *pGBufferDepth = GetDeferredExt()->GetTexture_Depth();
			if ( r_deferred_sky_skip_covered.GetBool() && pGBufferDepth && !GetDeferredExt()->IsReflectionView()
				&& GetDeferredExt()->IsMainViewSkySkip() )
			{
				ShaderViewport_t viewport;
				if ( pShaderAPI->GetViewports( &viewport, 1 ) >= 1 && viewport.m_nTopLeftX == 0 && viewport.m_nTopLeftY == 0
					&& viewport.m_nWidth <= pGBufferDepth->GetActualWidth() && viewport.m_nHeight <= pGBufferDepth->GetActualHeight() )
				{
					flSkip[0] = 1.0f;
					flSkip[2] = 1.0f / (float)pGBufferDepth->GetActualWidth();
					flSkip[3] = 1.0f / (float)pGBufferDepth->GetActualHeight();
				}
			}
			if ( flSkip[0] > 0.0f )
				BindTexture( SHADER_SAMPLER5, pGBufferDepth );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_BLACK );
			pShaderAPI->SetPixelShaderConstant( 19, flSkip, 1, true );

			// a lightning flash briefly turns the global light into the bolt: the sky keeps the sun / moon
			if ( w.bEnabled )
			{
				pShaderAPI->SetPixelShaderConstant( 1, w.vecSkyLightDir.Base(), 1, true );
				pShaderAPI->SetPixelShaderConstant( 16, w.vecSkyLightDiff.Base(), 1, true );
			}
		}

		Draw();
	}

END_SHADER

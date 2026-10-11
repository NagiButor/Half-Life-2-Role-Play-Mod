//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BaseVSShader.h"
#include "IDeferredExt.h"
#include "mathlib/vmatrix.h"
#include "common_hlsl_cpp_consts.h" // hack hack hack!
#include "convar.h"

#include "SDK_WaterCheap_vs20.inc"
#include "SDK_WaterCheap_ps20.inc"
#include "SDK_WaterCheap_ps20b.inc"
#include "SDK_Water_vs20.inc"
#include "SDK_Water_ps20.inc"
#include "SDK_Water_ps20b.inc"

#ifndef _X360
static ConVar r_waterforceexpensive( "r_waterforceexpensive", "0", FCVAR_ARCHIVE );
#endif

// HL2RPM: the water's own color with the deferred renderer (water_ps2x_helper.h)
static ConVar r_deferred_water_density( "r_deferred_water_density", "1.0", FCVAR_ARCHIVE,
	"Water: density of its fog compared to what the material says ($fogstart / $fogend)", true, 0.1f, true, 4.0f );
static ConVar r_deferred_water_viewangle( "r_deferred_water_viewangle", "1", 0,
	"Water: its fog goes by the path of the view ray through the water (clearer from above, murkier from the bank); 0 = by the depth alone, like the stock shader" );
static ConVar r_deferred_water_lit( "r_deferred_water_lit", "1", 0,
	"Water: its own color is lit by the light at the place (sun, shadow, lamps); 0 = by the time of day alone" );
// HL2RPM: the waters that only refract (the low dirty water of the canals, the sewers) take
// their reflection from a cubemap in a second, cheap pass: baked once - the map's noon sky at
// night, nothing of what stands at the water - or never built at all (the default cubemap: white
// streaks on black water). With the deferred renderer they get the real-time reflection the
// other waters have (decided when the material loads: the water materials are loaded anew when
// this changes).
static void WaterReflectAllChanged( IConVar *pVar, const char *pszOldValue, float flOldValue )
{
	ConVarRef var( pVar );
	if ( ( var.GetFloat() != 0.0f ) == ( flOldValue != 0.0f ) || !g_pCVar )
		return;
	ConCommand *pReload = g_pCVar->FindCommand( "mat_reloadmaterial" );
	if ( pReload )
	{
		const char *ppArgs[2] = { "mat_reloadmaterial", "water" };
		pReload->Dispatch( CCommand( 2, ppArgs ) );
	}
}
static ConVar r_deferred_water_reflect_all( "r_deferred_water_reflect_all", "1", FCVAR_ARCHIVE,
	"Water: the waters that only refract get the real-time reflection too (instead of their baked cubemap)", WaterReflectAllChanged );

static ConVar r_deferred_water_debugview( "r_deferred_water_debugview", "0", FCVAR_CHEAT,
	"Water: 1 = the reflection's share (fresnel), 2 = N.V / normal z / eye z, 3 = the light of the water's own color (x 0.5), 4 = its fog" );
static ConVar r_deferred_water_scatter( "r_deferred_water_scatter", "0.25", FCVAR_ARCHIVE,
	"Water: how much of the open daylight its own color keeps in a shadow under the open sky (the light a murky water passes on inside itself)", true, 0.0f, true, 1.0f );

DEFINE_FALLBACK_SHADER( SDK_Water, SDK_Water_DX9_HDR )

BEGIN_VS_SHADER( SDK_Water_DX90, 
			  "Help for SDK_Water" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( REFRACTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_WaterRefraction", "" )
		SHADER_PARAM( REFLECTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_WaterReflection", "" )
		SHADER_PARAM( REFRACTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0", "" )
		SHADER_PARAM( REFRACTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "refraction tint" )
		SHADER_PARAM( REFLECTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0.8", "" )
		SHADER_PARAM( REFLECTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "reflection tint" )
		SHADER_PARAM( NORMALMAP, SHADER_PARAM_TYPE_TEXTURE, "dev/water_normal", "normal map" )
		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $bumpmap" )
		SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "$bumpmap texcoord transform" )
		SHADER_PARAM( SCALE, SHADER_PARAM_TYPE_VEC2, "[1 1]", "" )
		SHADER_PARAM( TIME, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( WATERDEPTH, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( CHEAPWATERSTARTDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "This is the distance from the eye in inches that the shader should start transitioning to a cheaper water shader." )
		SHADER_PARAM( CHEAPWATERENDDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "This is the distance from the eye in inches that the shader should finish transitioning to a cheaper water shader." )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "env_cubemap", "envmap" )
		SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( FOGCOLOR, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( FORCECHEAP, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( FORCEEXPENSIVE, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTENTITIES, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( FOGSTART, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FOGEND, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( ABOVEWATER, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( NOFRESNEL, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( NOLOWENDLIGHTMAP, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( SCROLL1, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( SCROLL2, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( BLURREFRACT, SHADER_PARAM_TYPE_BOOL, "0", "Cause the refraction to be blurry on ps2b hardware" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if( !params[ABOVEWATER]->IsDefined() )
		{
			// (HL2RPM: a developer message, not a warning at every load of such a material)
			DevMsg( 2, "$abovewater not set for material %s: taken as seen from above\n", pMaterialName );
			params[ABOVEWATER]->SetIntValue( 1 );
		}
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		if( !params[CHEAPWATERSTARTDISTANCE]->IsDefined() )
		{
			params[CHEAPWATERSTARTDISTANCE]->SetFloatValue( 500.0f );
		}
		if( !params[CHEAPWATERENDDISTANCE]->IsDefined() )
		{
			params[CHEAPWATERENDDISTANCE]->SetFloatValue( 1000.0f );
		}
		if( !params[SCALE]->IsDefined() )
		{
			params[SCALE]->SetVecValue( 1.0f, 1.0f );
		}
		if( !params[SCROLL1]->IsDefined() )
		{
			params[SCROLL1]->SetVecValue( 0.0f, 0.0f, 0.0f );
		}
		if( !params[SCROLL2]->IsDefined() )
		{
			params[SCROLL2]->SetVecValue( 0.0f, 0.0f, 0.0f );
		}
		if( !params[FOGCOLOR]->IsDefined() )
		{
			params[FOGCOLOR]->SetVecValue( 1.0f, 0.0f, 0.0f );
			Warning( "material %s needs to have a $fogcolor.\n", pMaterialName );
		}
		if( !params[REFLECTENTITIES]->IsDefined() )
		{
			params[REFLECTENTITIES]->SetIntValue( 0 );
		}
		if( !params[REFLECTBLENDFACTOR]->IsDefined() )
		{
			params[REFLECTBLENDFACTOR]->SetFloatValue( 1.0f );
		}

		// By default, we're force expensive on dx9.  NO WE DON'T!!!!
		if( !params[FORCEEXPENSIVE]->IsDefined() )
		{
#ifdef _X360
			params[FORCEEXPENSIVE]->SetIntValue( 0 );
#else
			params[FORCEEXPENSIVE]->SetIntValue( 1 );
#endif
		}
		if( params[FORCEEXPENSIVE]->GetIntValue() && params[FORCECHEAP]->GetIntValue() )
		{
			params[FORCEEXPENSIVE]->SetIntValue( 0 );
		}

		// HL2RPM: see r_deferred_water_reflect_all (the surface seen from above only: the
		// materials of the underside have no reflection)
		if ( r_deferred_water_reflect_all.GetBool() && GetDeferredExt()->IsDeferredLightingEnabled()
			&& !params[REFLECTTEXTURE]->IsDefined() && params[REFRACTTEXTURE]->IsDefined()
			&& params[FORCEEXPENSIVE]->GetIntValue() != 0 && params[FORCECHEAP]->GetIntValue() == 0
			&& params[ABOVEWATER]->GetIntValue() != 0 )
		{
			params[REFLECTTEXTURE]->SetStringValue( "_rt_WaterReflection" );
			if ( !params[REFLECTAMOUNT]->IsDefined() )
				params[REFLECTAMOUNT]->SetFloatValue( 0.2f );
		}

		// Fallbacks for water need lightmaps usually
		if ( !params[NOLOWENDLIGHTMAP]->GetIntValue() )
		{
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		}

		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		if( g_pConfig->UseBumpmapping() && params[NORMALMAP]->IsDefined() )
		{
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
		}
	}

	SHADER_FALLBACK
	{
		if( g_pHardwareConfig->GetDXSupportLevel() < 90 )
		{
			return "Water_DX81";
		}
		return 0;
	}

	SHADER_INIT
	{
		Assert( params[WATERDEPTH]->IsDefined() );

		if( params[REFRACTTEXTURE]->IsDefined() )
		{
			LoadTexture( REFRACTTEXTURE, TEXTUREFLAGS_SRGB );
		}
		if( params[REFLECTTEXTURE]->IsDefined() )
		{
			LoadTexture( REFLECTTEXTURE, TEXTUREFLAGS_SRGB );
		}
		if ( params[ENVMAP]->IsDefined() )
		{
			LoadCubeMap( ENVMAP, TEXTUREFLAGS_SRGB );

#ifdef MAPBASE
			if (mat_specular_disable_on_missing.GetBool())
			{
				// Revert to defaultcubemap when the envmap texture is missing
				// (should be equivalent to toolsblack in Mapbase)
				if (params[ENVMAP]->GetTextureValue()->IsError())
				{
					params[ENVMAP]->SetStringValue( "engine/defaultcubemap" );
					LoadCubeMap( ENVMAP, TEXTUREFLAGS_SRGB );
				}
			}
#endif
		}
		if ( params[NORMALMAP]->IsDefined() )
		{
			LoadBumpMap( NORMALMAP );
		}
		if( params[BASETEXTURE]->IsDefined() )
		{
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
		}
	}

	inline void GetVecParam( int constantVar, float *val )
	{
		if( constantVar == -1 )
			return;

		IMaterialVar* pVar = s_ppParams[constantVar];
		Assert( pVar );

		if (pVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
			pVar->GetVecValue( val, 4 );
		else
			val[0] = val[1] = val[2] = val[3] = pVar->GetFloatValue();
	}

	inline void DrawReflectionRefraction( IMaterialVar **params, IShaderShadow* pShaderShadow,
		IShaderDynamicAPI* pShaderAPI, bool bReflection, bool bRefraction ) 
	{
		SHADOW_STATE
		{
			SetInitialShadowState( );
			if( bRefraction )
			{
				// refract sampler
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			}
			if( bReflection )
			{
				// reflect sampler
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, true );
				if( params[BASETEXTURE]->IsTexture() )
				{
					// BASETEXTURE
					pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
					// LIGHTMAP
					pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );
				}
			}
			// normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			// Normalizing cube map
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			// HL2RPM: the light buffer of the deferred renderer and its sky visibility (the light the
			// water's color is seen in)
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );

			int fmt = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T;

			// texcoord0 : base texcoord
			// texcoord1 : lightmap texcoord
			// texcoord2 : lightmap texcoord offset
			int numTexCoords = 1;
			if( params[BASETEXTURE]->IsTexture() )
			{
				numTexCoords = 3;
			}
			pShaderShadow->VertexShaderVertexFormat( fmt, numTexCoords, 0, 0 );
			
			Vector4D Scroll1;
			params[SCROLL1]->GetVecValue( Scroll1.Base(), 4 );

			NormalDecodeMode_t nNormalDecodeMode = NORMAL_DECODE_NONE;
			if ( params[NORMALMAP]->IsTexture() && g_pHardwareConfig->SupportsNormalMapCompression() )
			{
				ITexture *pNormalMap = params[NORMALMAP]->GetTextureValue();
				if ( pNormalMap )
				{
					// Clamp this to 0 or 1 since that's how we've authored the water shader (i.e. no separate alpha map/channel)
					nNormalDecodeMode = pNormalMap->GetNormalDecodeMode() == NORMAL_DECODE_NONE ? NORMAL_DECODE_NONE : NORMAL_DECODE_ATI2N;
				}
			}

			DECLARE_STATIC_VERTEX_SHADER( sdk_water_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( MULTITEXTURE,fabs(Scroll1.x) > 0.0);
			SET_STATIC_VERTEX_SHADER_COMBO( BASETEXTURE, params[BASETEXTURE]->IsTexture() );
			SET_STATIC_VERTEX_SHADER( sdk_water_vs20 );

			// "REFLECT" "0..1"
			// "REFRACT" "0..1"
			
			if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				DECLARE_STATIC_PIXEL_SHADER( sdk_water_ps20b );
				SET_STATIC_PIXEL_SHADER_COMBO( REFLECT,  bReflection );
				SET_STATIC_PIXEL_SHADER_COMBO( REFRACT,  bRefraction );
				SET_STATIC_PIXEL_SHADER_COMBO( ABOVEWATER,  params[ABOVEWATER]->GetIntValue() );
				SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE,fabs(Scroll1.x) > 0.0);
				SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE, params[BASETEXTURE]->IsTexture() );
				SET_STATIC_PIXEL_SHADER_COMBO( BLURRY_REFRACT, params[BLURREFRACT]->GetIntValue() );
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, (int) nNormalDecodeMode );
				SET_STATIC_PIXEL_SHADER( sdk_water_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( sdk_water_ps20 );
				SET_STATIC_PIXEL_SHADER_COMBO( REFLECT,  bReflection );
				SET_STATIC_PIXEL_SHADER_COMBO( REFRACT,  bRefraction );
				SET_STATIC_PIXEL_SHADER_COMBO( ABOVEWATER,  params[ABOVEWATER]->GetIntValue() );
				SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE,fabs(Scroll1.x) > 0.0);
				SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE, params[BASETEXTURE]->IsTexture() );
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, (int) nNormalDecodeMode );
				SET_STATIC_PIXEL_SHADER( sdk_water_ps20 );
			}

			FogToFogColor();

			// we are writing linear values from this shader.
			pShaderShadow->EnableSRGBWrite( true );

			pShaderShadow->EnableAlphaWrites( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();
			if( bRefraction )
			{
				// HDRFIXME: add comment about binding.. Specify the number of MRTs in the enable
				BindTexture( SHADER_SAMPLER0, REFRACTTEXTURE, -1 );
			}
			if( bReflection )
			{
				BindTexture( SHADER_SAMPLER2, REFLECTTEXTURE, -1 );
			}
			BindTexture( SHADER_SAMPLER4, NORMALMAP, BUMPFRAME );
			if( params[BASETEXTURE]->IsTexture() )
			{
				BindTexture( SHADER_SAMPLER1, BASETEXTURE, FRAME );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_LIGHTMAP );
			}

			pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );
			
			// Refraction tint
			if( bRefraction )
			{
				SetPixelShaderConstantGammaToLinear( 1, REFRACTTINT );
			}
			// Reflection tint
			if( bReflection )
			{
				if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
				{
					// Need to multiply by 4 in linear space since we premultiplied into
					// the render target by .25 to get overbright data in the reflection render target.
					float gammaReflectTint[3];
					params[REFLECTTINT]->GetVecValue( gammaReflectTint, 3 );
					float linearReflectTint[4];
					linearReflectTint[0] = GammaToLinear( gammaReflectTint[0] ) * 4.0f;
					linearReflectTint[1] = GammaToLinear( gammaReflectTint[1] ) * 4.0f;
					linearReflectTint[2] = GammaToLinear( gammaReflectTint[2] ) * 4.0f;
					linearReflectTint[3] = 1.0f;
					pShaderAPI->SetPixelShaderConstant( 4, linearReflectTint, 1 );
				}
				else
				{
					SetPixelShaderConstantGammaToLinear( 4, REFLECTTINT );
				}
			}

			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, BUMPTRANSFORM );
			
			float curtime=pShaderAPI->CurrentTime();
			float vc0[4];
			float v0[4];
			params[SCROLL1]->GetVecValue(v0,4);
			vc0[0]=curtime*v0[0];
			vc0[1]=curtime*v0[1];
			params[SCROLL2]->GetVecValue(v0,4);
			vc0[2]=curtime*v0[0];
			vc0[3]=curtime*v0[1];
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, vc0, 1 );

			float c0[4] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1 );
			
			float c2[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1 );
			
			// fresnel constants
			float c3[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 3, c3, 1 );

			float c5[4] = { params[REFLECTAMOUNT]->GetFloatValue(), params[REFLECTAMOUNT]->GetFloatValue(), 
				params[REFRACTAMOUNT]->GetFloatValue(), params[REFRACTAMOUNT]->GetFloatValue() };
			pShaderAPI->SetPixelShaderConstant( 5, c5, 1 );

			SetPixelShaderConstantGammaToLinear( 6, FOGCOLOR );

			float c7[4] = 
			{ 
				params[FOGSTART]->GetFloatValue(), 
				params[FOGEND]->GetFloatValue() - params[FOGSTART]->GetFloatValue(), 
				1.0f, 
				0.0f 
			};
			if (g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
			{
				// water overbright factor
				c7[2] = 4.0;
			}
			// HL2RPM: the deferred refraction view writes the depth under the surface to its
			// alpha, not the fog of one water (composite_ps30, water_ps2x_helper.h)
			c7[3] = GetDeferredExt()->IsDeferredLightingEnabled() ? 1.0f : 0.0f;
			pShaderAPI->SetPixelShaderConstant( 7, c7, 1 );

			// HL2RPM: the foam along the shore, in the light of the moment (sky + sun)
			float c10[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( GetDeferredExt()->IsDeferredLightingEnabled() )
			{
				const weatherData_t &weather = GetDeferredExt()->GetWeatherData();
				const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
				Vector vecFoam( 0.55f, 0.6f, 0.65f );
				if ( weather.bEnabled )
				{
					const float flSun = clamp( light.vecLight.z, 0.0f, 1.0f ) * 0.3f;
					vecFoam = weather.vecSkyHorizon.AsVector3D() * 0.85f + light.diff.AsVector3D() * flSun;
				}
				const float flMax = MAX( vecFoam.x, MAX( vecFoam.y, vecFoam.z ) );
				if ( flMax > 0.9f )
					vecFoam *= 0.9f / flMax;
				c10[0] = vecFoam.x;
				c10[1] = vecFoam.y;
				c10[2] = vecFoam.z;
				c10[3] = 0.7f;
			}
			pShaderAPI->SetPixelShaderConstant( 10, c10, 1 );

			// HL2RPM: the water's own color ($fogcolor - what the water scatters back) in the light
			// at the place: the deferred light buffer under the pixel, and no less than the light of
			// the sky where the sky is in sight (the sky visibility of the SSAO target), compared to
			// the light of the open sky and sun on level ground at the moment (without a lightning
			// flash: the water lights up with it). $fogcolor itself follows the time of day
			// (weather_render.cpp), so that light is "1"; a lamp at night can light the water up to
			// its daylight color.
			float c11[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
			float c12[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			ITexture *pLightAccum = NULL;
			ITexture *pSkyVis = NULL;
			if ( GetDeferredExt()->IsDeferredLightingEnabled() )
			{
				c11[2] = r_deferred_water_density.GetFloat();
				c11[3] = r_deferred_water_viewangle.GetBool() ? 1.0f : 0.0f;
				if ( r_deferred_water_debugview.GetInt() > 0 )
					c11[3] = 1.0f + r_deferred_water_debugview.GetInt();
				pLightAccum = r_deferred_water_lit.GetBool() ? GetDeferredExt()->GetTexture_LightAccum() : NULL;
				if ( pLightAccum )
				{
					const weatherData_t &weather = GetDeferredExt()->GetWeatherData();
					const lightData_Global_t &light = GetDeferredExt()->GetLightData_Global();
					Vector vecSun = light.diff.AsVector3D();
					float flUp = light.vecLight.z;
					float flWorld = 1.0f;
					if ( weather.bEnabled )
					{
						vecSun = weather.vecSkyLightDiff.AsVector3D();
						flUp = weather.vecSkyLightDir.z;
						flWorld = clamp( weather.vecSkyZenith.w, 0.03f, 1.2f );
					}
					const Vector vecSky = light.ambh.AsVector3D() * GetGISkyBoost( GetDeferredExt()->GetGIData(), weather );
					const Vector vecOpen = vecSun * clamp( flUp, 0.0f, 1.0f ) + vecSky;
					const float flOpen = MAX( vecOpen.x * 0.3f + vecOpen.y * 0.59f + vecOpen.z * 0.11f, 0.002f );
					c11[0] = 1.0f / flOpen;
					c11[1] = 1.5f / flWorld;
					// (the least where the sky is in sight: the sky's light and a part of all of it)
					const float flScatter = r_deferred_water_scatter.GetFloat();
					c12[0] = ( vecSky.x + vecOpen.x * flScatter ) / flOpen;
					c12[1] = ( vecSky.y + vecOpen.y * flScatter ) / flOpen;
					c12[2] = ( vecSky.z + vecOpen.z * flScatter ) / flOpen;

					const ssaoData_t &ao = GetDeferredExt()->GetSSAOData();
					if ( ao.bEnabled && ao.vecApply.x > 0.0f && ao.vecApply.w > 0.0f )
					{
						pSkyVis = GetDeferredExt()->GetTexture_SSAO();
						c12[3] = ao.vecApply.w;
					}
				}
			}
			if ( pLightAccum )
				BindTexture( SHADER_SAMPLER6, pLightAccum );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_WHITE );
			if ( pSkyVis )
				BindTexture( SHADER_SAMPLER7, pSkyVis );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER7, TEXTURE_WHITE );
			pShaderAPI->SetPixelShaderConstant( 11, c11, 1 );
			pShaderAPI->SetPixelShaderConstant( 12, c12, 1 );

			pShaderAPI->SetPixelShaderFogParams( 8 );

			float vEyePos[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
			vEyePos[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 9, vEyePos );

			DECLARE_DYNAMIC_VERTEX_SHADER( sdk_water_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( sdk_water_vs20 );
			
			if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( sdk_water_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
				SET_DYNAMIC_PIXEL_SHADER( sdk_water_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( sdk_water_ps20 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER( sdk_water_ps20 );
			}
		}
		Draw();
	}

	inline void DrawCheapWater( IMaterialVar **params, IShaderShadow* pShaderShadow, 
		                        IShaderDynamicAPI* pShaderAPI, bool bBlend, bool bRefraction )
	{
		SHADOW_STATE
		{
			SetInitialShadowState( );

			// In edit mode, use nocull
			if ( UsingEditor( params ) )
			{
				s_pShaderShadow->EnableCulling( false );
			}

			if( bBlend )
			{
				EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			}
			// envmap
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			// normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			if( bRefraction && bBlend )
			{
				// refraction map (used for alpha)
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			}
			// Normalizing cube map
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			int fmt = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			NormalDecodeMode_t nNormalDecodeMode = NORMAL_DECODE_NONE;
			if ( params[NORMALMAP]->IsTexture() && g_pHardwareConfig->SupportsNormalMapCompression() )
			{
				ITexture *pNormalMap = params[NORMALMAP]->GetTextureValue();
				if ( pNormalMap )
				{
					// Clamp this to 0 or 1 since that's how we've authored the water shader (i.e. no separate alpha map/channel)
					nNormalDecodeMode = pNormalMap->GetNormalDecodeMode() == NORMAL_DECODE_NONE ? NORMAL_DECODE_NONE : NORMAL_DECODE_ATI2N;
				}
			}

			DECLARE_STATIC_VERTEX_SHADER( sdk_watercheap_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( BLEND,  bBlend && bRefraction );
			SET_STATIC_VERTEX_SHADER( sdk_watercheap_vs20 );

			if( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				DECLARE_STATIC_PIXEL_SHADER( sdk_watercheap_ps20b );
				SET_STATIC_PIXEL_SHADER_COMBO( FRESNEL,  params[NOFRESNEL]->GetIntValue() == 0 );
				SET_STATIC_PIXEL_SHADER_COMBO( BLEND,  bBlend );
				SET_STATIC_PIXEL_SHADER_COMBO( REFRACTALPHA,  bRefraction );
				SET_STATIC_PIXEL_SHADER_COMBO( HDRTYPE,  g_pHardwareConfig->GetHDRType() );
				Vector4D Scroll1;
				params[SCROLL1]->GetVecValue( Scroll1.Base(), 4 );
				SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE,fabs(Scroll1.x) > 0.0);
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, (int) nNormalDecodeMode );
				SET_STATIC_PIXEL_SHADER( sdk_watercheap_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( sdk_watercheap_ps20 );
				SET_STATIC_PIXEL_SHADER_COMBO( FRESNEL,  params[NOFRESNEL]->GetIntValue() == 0 );
				SET_STATIC_PIXEL_SHADER_COMBO( BLEND,  bBlend );
				SET_STATIC_PIXEL_SHADER_COMBO( REFRACTALPHA,  bRefraction );
				SET_STATIC_PIXEL_SHADER_COMBO( HDRTYPE,  g_pHardwareConfig->GetHDRType() );
				Vector4D Scroll1;
				params[SCROLL1]->GetVecValue( Scroll1.Base(), 4 );
				SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE,fabs(Scroll1.x) > 0.0);
				SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, (int) nNormalDecodeMode );
				SET_STATIC_PIXEL_SHADER( sdk_watercheap_ps20 );
			}

			// HDRFIXME: test cheap water!
			if( g_pHardwareConfig->GetHDRType() != HDR_TYPE_NONE )
			{
				// we are writing linear values from this shader.
				pShaderShadow->EnableSRGBWrite( true );
			}

			FogToFogColor();
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			BindTexture( SHADER_SAMPLER0, ENVMAP, ENVMAPFRAME );
			BindTexture( SHADER_SAMPLER1, NORMALMAP, BUMPFRAME );
			if( bRefraction && bBlend )
			{
				BindTexture( SHADER_SAMPLER2, REFRACTTEXTURE, -1 );
			}
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );

			SetPixelShaderConstant( 0, FOGCOLOR );

			float cheapWaterStartDistance = params[CHEAPWATERSTARTDISTANCE]->GetFloatValue();
			float cheapWaterEndDistance = params[CHEAPWATERENDDISTANCE]->GetFloatValue();
			float cheapWaterParams[4] = 
			{
				cheapWaterStartDistance * VSHADER_VECT_SCALE,
				cheapWaterEndDistance * VSHADER_VECT_SCALE,
				PSHADER_VECT_SCALE / ( cheapWaterEndDistance - cheapWaterStartDistance ),
				cheapWaterStartDistance / ( cheapWaterEndDistance - cheapWaterStartDistance ),
			};
			pShaderAPI->SetPixelShaderConstant( 1, cheapWaterParams );

			float vEyePos[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
			vEyePos[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 4, vEyePos );

			if( g_pConfig->bShowSpecular )
			{
				SetPixelShaderConstant( 2, REFLECTTINT, REFLECTBLENDFACTOR );
			}
			else
			{
				float zero[4] = { 0.0f, 0.0f, 0.0f, params[REFLECTBLENDFACTOR]->GetFloatValue() };
				pShaderAPI->SetPixelShaderConstant( 2, zero );
			}
		
			pShaderAPI->SetPixelShaderFogParams( 3 );

			if( params[SCROLL1]->IsDefined())
			{
				float curtime=pShaderAPI->CurrentTime();
				float vc0[4];
				float v0[4];
				params[SCROLL1]->GetVecValue(v0,4);
				vc0[0]=curtime*v0[0];
				vc0[1]=curtime*v0[1];
				params[SCROLL2]->GetVecValue(v0,4);
				vc0[2]=curtime*v0[0];
				vc0[3]=curtime*v0[1];
				pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, vc0, 1 );
			}

			DECLARE_DYNAMIC_VERTEX_SHADER( sdk_watercheap_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( sdk_watercheap_vs20 );

			if( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( HDRENABLED,  IsHDREnabled() );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( HDRENABLED,  IsHDREnabled() );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20 );
			}
		}
		Draw();
	}

	SHADER_DRAW
	{
		// TODO: fit the cheap water stuff into the water shader so that we don't have to do
		// 2 passes.
#ifdef _X360
		bool bForceExpensive = false;
#else
		bool bForceExpensive = r_waterforceexpensive.GetBool();
#endif
		bool bForceCheap = (params[FORCECHEAP]->GetIntValue() != 0) || UsingEditor( params );
		if ( bForceCheap )
		{
			bForceExpensive = false;
		}
		else
		{
			bForceExpensive = bForceExpensive || (params[FORCEEXPENSIVE]->GetIntValue() != 0);
		}
		Assert( !( bForceCheap && bForceExpensive ) );

		bool bRefraction = params[REFRACTTEXTURE]->IsTexture();
#ifdef _X360
		bool bReflection = params[REFLECTTEXTURE]->IsTexture();
#else
		bool bReflection = bForceExpensive && params[REFLECTTEXTURE]->IsTexture();
#endif
		bool bDrewSomething = false;
		if ( !bForceCheap && ( bReflection || bRefraction ) )
		{
			bDrewSomething = true;
			DrawReflectionRefraction( params, pShaderShadow, pShaderAPI, bReflection, bRefraction );
		}

		// Use $decal to see if we are a decal or not. . if we are, then don't bother
		// drawing the cheap version for now since we don't have access to env_cubemap
#ifdef _X360
		if( params[ENVMAP]->IsTexture() && !IS_FLAG_SET( MATERIAL_VAR_DECAL ) && !bForceExpensive )
#else
		if( !bReflection && params[ENVMAP]->IsTexture() && !IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
#endif
		{
			bDrewSomething = true;
			DrawCheapWater( params, pShaderShadow, pShaderAPI, !bForceCheap, bRefraction );
		}

		if( !bDrewSomething )
		{
			// We are likely here because of the tools. . . draw something so that 
			// we won't go into wireframe-land.
			Draw();
		}
	}
END_SHADER

//-----------------------------------------------------------------------------
// This allows us to use a block labelled 'Water_DX9_HDR' in the water materials
//-----------------------------------------------------------------------------
BEGIN_INHERITED_SHADER( SDK_Water_DX9_HDR, SDK_Water_DX90,
			  "Help for SDK_Water_DX9_HDR" )

	SHADER_FALLBACK
	{
		if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
		{
			return "SDK_WATER_DX90";
		}
		return 0;
	}
END_INHERITED_SHADER


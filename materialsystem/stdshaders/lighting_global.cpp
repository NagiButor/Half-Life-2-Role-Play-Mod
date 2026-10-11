
#include "deferred_includes.h"

#include "defconstruct_vs30.inc"
#include "lightingpass_global_ps30.inc"

static ConVar r_csm_color( "r_csm_color", "0", FCVAR_ARCHIVE );
// HL2RPM: soft sun shadows (percentage-closer soft shadows)
static ConVar r_csm_pcss( "r_csm_pcss", "1", FCVAR_ARCHIVE, "Soft sun shadows: sharp at the caster, softer the farther they fall (PCSS)" );
// (replaces r_csm_pcss_softness, which used the tangent of the sun's whole angular size as
// the penumbra *radius* and defaulted to 1.4x the sun: penumbrae 2.7x too wide - renamed so
// the old value saved in config.cfg doesn't come back)
static ConVar r_csm_pcss_sunsize( "r_csm_pcss_sunsize", "1", FCVAR_ARCHIVE,
	"Angular size of the sun for the soft sun shadows, relative to the real sun (0.53 degrees)", true, 0.0f, true, 4.0f );
// (was r_csm_pcss_max, the radius of a per-pixel rotated disk - renamed so old configs
// with its value of 10 don't widen the fixed disk beyond what its 32 taps cover)
// HL2RPM: the shadow filter is a cubic B-spline whose scale goes from 1 (contact) to 2: the
// penumbra radius adds to it as a disk would (a radius of 2 texels and more = scale 2)
static ConVar r_csm_pcss_penumbra( "r_csm_pcss_penumbra", "2.5", FCVAR_ARCHIVE,
	"Widest penumbra of the soft sun shadows, in shadow map texels on top of the base filter", true, 0.0f, true, 3.0f );

// HL2RPM: sun shadow bias on the receiver side. The casters are drawn two-sided (nodraw-backed
// walls must cast), so lit surfaces compare against themselves: the receiver is moved along
// its geometric normal (from the depth buffer - the G-buffer normal carries the bump map) and
// gets a little depth bias of its own slope. (The caster side used a 3x slope bias from the
// rasterizer: steep walls were pushed behind the floor at their foot - a lit gap.)
static ConVar r_csm_bias_normal( "r_csm_bias_normal", "1.5", 0,
	"Sun shadows: receiver offset along its normal at grazing angles, in shadow map texels", true, 0.0f, true, 4.0f );
static ConVar r_csm_bias_slope( "r_csm_bias_slope", "0.25", 0,
	"Sun shadows: receiver depth bias per unit of tan( angle to the sun ), in shadow map texels", true, 0.0f, true, 8.0f );
// HL2RPM: the first person body is drawn with its head and arms scaled away, the shadow maps
// get the whole model: the hidden head and shoulders shadowed the visible torso and legs
// HL2RPM: the normal the sun shadow lookups use (offset, slope bias, receiver plane)
static ConVar r_csm_shadow_normal( "r_csm_shadow_normal", "2", 0,
	"Sun shadows: normal of the receiver - 0 G-buffer (bump mapped), 1 2x2 pixel derivatives of the depth, 2 the pixel's own surface from the depth (stable at creases)", true, 0.0f, true, 2.0f );
// HL2RPM: the shadow filter covers the pixel's footprint on the shadow map (far, grazing): the
// shadows of thin things smaller than a pixel don't flicker from texel to texel while walking
static ConVar r_csm_footprint( "r_csm_footprint", "0.5", 0,
	"Sun shadows: filter scale per shadow map texel a pixel covers (0 = off; the B-spline of scale s averages ~2s texels)", true, 0.0f, true, 2.0f );
// HL2RPM: contact shadows (see lightingpass_global_ps30)
// HL2RPM (PBR): what the surfaces mirror (lightingpass_global_ps30, composite_ps30)
static ConVar r_deferred_pbr_sun( "r_deferred_pbr_sun", "1.0", 0, "PBR: the sun's (moon's) mirror reflection", true, 0.0f, true, 4.0f );
static ConVar r_deferred_pbr_sun_roughness( "r_deferred_pbr_sun_roughness", "0.16", 0,
	"PBR: least roughness for the sun's reflection (a perfect mirror of the sun's disk is a few pixels of pure white)", true, 0.05f, true, 1.0f );
static ConVar r_deferred_pbr_env( "r_deferred_pbr_env", "1.0", 0, "PBR: what the surfaces mirror of the light around them", true, 0.0f, true, 4.0f );
static ConVar r_deferred_pbr_wet( "r_deferred_pbr_wet", "1.0", FCVAR_ARCHIVE, "PBR: rain makes what lies under the open sky smooth (0 = off)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_pbr_wet_roughness( "r_deferred_pbr_wet_roughness", "0.3", 0, "PBR: the roughness of a wet surface", true, 0.05f, true, 1.0f );

static ConVar r_deferred_contact_shadows( "r_deferred_contact_shadows", "1", FCVAR_ARCHIVE,
	"Contact shadows: the small shadows right at the foot of things the sun cascades are too coarse for (screen space)" );
static ConVar r_deferred_contact_shadows_length( "r_deferred_contact_shadows_length", "24", 0, "Contact shadows: how far toward the sun at most (units)", true, 2.0f, true, 256.0f );
// HL2RPM: ...and no farther than the sun cascade at the point is coarse (see lightingpass_global_ps30)
static ConVar r_deferred_contact_shadows_texels( "r_deferred_contact_shadows_texels", "8", 0,
	"Contact shadows: length of the march in texels of the sun cascade at the point (a fine cascade needs none; 1000 = always the full length)", true, 0.0f, true, 1000.0f );
static ConVar r_deferred_contact_shadows_thickness( "r_deferred_contact_shadows_thickness", "12", 0, "Contact shadows: how thick an occluder is assumed (units)", true, 1.0f, true, 128.0f );
static ConVar r_deferred_contact_shadows_distance( "r_deferred_contact_shadows_distance", "1500", 0, "Contact shadows: up to this distance from the eye (units)", true, 0.0f, true, 20000.0f );
static ConVar r_deferred_contact_shadows_strength( "r_deferred_contact_shadows_strength", "0.85", 0, "Contact shadows: strength (0..1)", true, 0.0f, true, 1.0f );
static ConVar r_deferred_contact_shadows_debug( "r_deferred_contact_shadows_debug", "0", FCVAR_CHEAT, "Contact shadows debug: red = occlusion, green = strength, blue = lit (march skipped)" );
static ConVar r_deferred_contact_shadows_dither( "r_deferred_contact_shadows_dither", "1", 0,
	"Contact shadows: per pixel offset of the march - 0 none, 1 moving with the frame while TAA accumulates (smooth), 2 the same every frame (the old stipple, for comparison)" );

static ConVar r_csm_body_bias( "r_csm_body_bias", "36", 0,
	"Sun shadows on the local player's first person body ignore casters closer than this (units) - the body's own", true, 0.0f, true, 128.0f );

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
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );	// HL2RPM ambient occlusion + sky visibility
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );	// HL2RPM GI probes' indirect light
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );	// HL2RPM sun caster depth (PCSS)

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

			// HL2RPM: soft sun shadows
			{
				ITexture *pRaw = data.bShadow ? GetDeferredExt()->GetTexture_ShadowDepthRaw_Ortho( 0 ) : NULL;
				const bool bPCSS = pRaw != NULL && r_csm_pcss.GetBool();
				if ( bPCSS )
					BindTexture( SHADER_SAMPLER8, pRaw );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER8, TEXTURE_WHITE );
				// (r_csm_pcss 0 keeps the same filter at its sharpest, without the blocker search)
				// penumbra radius per unit of caster distance: tan( half the sun's angular size )
				const float flSunRadius = tanf( DEG2RAD( 0.533f * 0.5f ) ) * Max( r_csm_pcss_sunsize.GetFloat(), 0.0f );
				const float flMax = clamp( r_csm_pcss_penumbra.GetFloat(), 0.0f, 3.0f );
				// the same widest penumbra in world units for every cascade: what the first one allows
				const float flMaxWorld = flMax * 2.0f * GetDeferredExt()->GetShadowData_Ortho( 0 ).vecSlopeSettings.z;
				float flPCSS[4] = { flSunRadius, flMax, flMaxWorld, bPCSS ? (float)r_csm_pcss.GetInt() : 0.0f };
				pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 13, flPCSS, 1, true );

				// (w: the per pixel dither of the contact shadow march moves every frame while TAA
				// accumulates the frames - and is off without it, see lightingpass_global_ps30)
				const taaData_t &taa = GetDeferredExt()->GetTAAData();
				const int iDither = r_deferred_contact_shadows_dither.GetInt();
				float flDitherPhase = -1.0f;
				if ( iDither == 2 )
					flDitherPhase = 0.0f;
				else if ( iDither == 1 && taa.vecParams3.z > 0.5f && !GetDeferredExt()->IsReflectionView() )
					flDitherPhase = taa.vecParams3.w;
				float flShadowExtra[4] = { r_csm_body_bias.GetFloat(), (float)r_csm_shadow_normal.GetInt(), r_csm_footprint.GetFloat(), flDitherPhase };
				pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 8, flShadowExtra, 1, true );
			}

			CommitGlobalLightForward( pShaderAPI, 1 );

			CommitBaseDeferredConstants_Frustum( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_0 );
			CommitBaseDeferredConstants_Origin( pShaderAPI, 0 );

			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA, data.diff.Base(), 1, true );
			// HL2RPM: the sky light as strong as a real sky's while the GI probes occlude it
			const float flSkyBoost = GetGISkyBoost( GetDeferredExt()->GetGIData(), GetDeferredExt()->GetWeatherData() );
			Vector4D ambh = data.ambh, ambl = data.ambl;
			for ( int i = 0; i < 3; i++ )
			{
				ambh[i] *= flSkyBoost;
				ambl[i] *= flSkyBoost;
			}
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 1, ambh.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 2, MakeHalfAmbient( ambl, ambh ).Base(), 1, true );

			float flCSMColorize[4] = { r_csm_color.GetBool() ? 1.0f : 0.0f, 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 3, flCSMColorize, 1, true );

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
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 4, w.vecCloudParams0.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 5, w.vecCloudParams1.Base(), 1, true );
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 6, w.vecWind.Base(), 1, true );
			// (z: the contact shadow march, in texels of the cascade at the point)
			float flWeatherFlags[4] = { bCloudShadows ? w.vecCloudParams2.y : 0.0f, w.vecCloudParams3.z, r_deferred_contact_shadows_texels.GetFloat(), 0 };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 7, flWeatherFlags, 1, true );

			// HL2RPM: ambient occlusion (SSAO) and sky visibility (top-down occlusion map), both
			// in the half resolution AO target (r, g) - see deferred_ssao_ps30
			const ssaoData_t &ao = GetDeferredExt()->GetSSAOData();
			ITexture *pSSAO = GetDeferredExt()->GetTexture_SSAO();
			const bool bAO = ao.bEnabled && pSSAO != NULL && ao.vecApply.w > 0.0f;
			if ( bAO )
				BindTexture( SHADER_SAMPLER6, pSSAO );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_WHITE );

			// (x, z: the receiver bias of the sun shadows - see r_csm_bias_normal)
			float flAmbientCtrl[4] = { r_csm_bias_normal.GetFloat(), ao.vecApply.y, r_csm_bias_slope.GetFloat(), bAO ? ao.vecApply.w : 0.0f };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 12, flAmbientCtrl, 1, true );

			// HL2RPM: indirect light of the GI probes (half resolution, blurred) - see deferred_gi_ps30
			const giData_t &gi = GetDeferredExt()->GetGIData();
			ITexture *pGI = GetDeferredExt()->GetTexture_GI();
			const bool bGI = gi.bEnabled && pGI != NULL && gi.vecApply.w > 0.0f;
			if ( bGI )
				BindTexture( SHADER_SAMPLER7, pGI );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER7, TEXTURE_BLACK );
			float flGIApply[4] = { bGI ? 1.0f : 0.0f, gi.vecApply.y, r_deferred_contact_shadows_debug.GetBool() ? 1.0f : 0.0f, bGI ? gi.vecApply.w : 0.0f };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 14, flGIApply, 1, true );

			// HL2RPM: contact shadows - this view's world -> screen texture coords (c73 - c76)
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 15, GetDeferredExt()->GetMainViewToScreenTexBase(), 4, true );
			const bool bContact = r_deferred_contact_shadows.GetBool() && !GetDeferredExt()->IsReflectionView();
			float flContact[4] = { r_deferred_contact_shadows_length.GetFloat(), r_deferred_contact_shadows_thickness.GetFloat(),
				r_deferred_contact_shadows_distance.GetFloat(), bContact ? r_deferred_contact_shadows_strength.GetFloat() : 0.0f };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 19, flContact, 1, true );

			// HL2RPM (PBR): the sun's reflection; the light around the point as a surface
			// mirrors it - less of it looking down at the ground, more looking up at the sky
			const float flEnv = r_deferred_pbr_env.GetFloat();
			float flPBR[4] = { r_deferred_pbr_sun.GetFloat(), 0.5f * flEnv, 1.3f * flEnv, r_deferred_pbr_sun_roughness.GetFloat() };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 20, flPBR, 1, true );
			// (the wetness of the weather: 0 dry .. 1 soaked)
			const weatherData_t &wetWeather = GetDeferredExt()->GetWeatherData();
			const float flWetness = wetWeather.bEnabled ? clamp( wetWeather.vecRainParams.y, 0.0f, 1.0f ) * r_deferred_pbr_wet.GetFloat() : 0.0f;
			float flPBRWet[4] = { flWetness, r_deferred_pbr_wet_roughness.GetFloat(), 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( CSM_PSREG_LIGHTDATA + 21, flPBRWet, 1, true );
		}

		Draw();
	}

END_SHADER

#include "cbase.h"
#include "viewrender_deferred.h"
#include "vprof.h"
#include "c_rope.h"
#include "rendertexture.h"
#include "renderparm.h"
#include "viewdebug.h"
#include "glow_overlay.h"
#include "c_effects.h"
#include "clientsideeffects.h"
#include "view_scene.h"
#include "toolframework_client.h"
#include "model_types.h"
#include "view.h"
#include "viewpostprocess.h"
#include "ivieweffects.h"
#include "smoke_fog_overlay.h"
#include "clientmode_shared.h"

#include "ienginevgui.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "client_virtualreality.h"
#include "sourcevr/isourcevirtualreality.h"

#ifdef SHADEREDITOR
#include "ShaderEditor/IVShaderEditor.h"
#endif

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/callqueue.h"

#include "deferred/deferred_rt.h"
#include "deferred/cdeferred_manager_client.h"
#include "weather/weather_render.h"
#include "weather/c_weather_system.h"
#include "deferred/deferred_ssao.h"
#include "detailobjectsystem.h"
#include "c_func_reflective_glass.h"

#include "vgui_int.h"
#include "vgui/IPanel.h"
#include "vgui_controls/Controls.h"
#include "debugoverlay_shared.h"

//#include "viewrender_deferred_helper.h" // doesn't seem to be working, let's not hook into the engine dll

#include "tier0/memdbgon.h"

extern ConVar r_drawopaquerenderables;
extern ConVar r_entityclips;
// r_ForceWaterLeaf is static in viewrender.cpp
extern ConVar mat_viewportupscale;
extern ConVar mat_viewportscale;
extern ConVar mat_motion_blur_enabled;

extern void MaybeInvalidateLocalPlayerAnimation();
extern bool DoesViewPlaneIntersectWater( float waterZ, int leafWaterDataID );
extern void SetupCurrentView( const Vector &vecOrigin, const QAngle &angles, view_id_t viewID );
extern void FinishCurrentView();
extern void FlushWorldLists();

static ConVar r_deferred_debug_shadow( "r_deferred_debug_shadow", "0", FCVAR_ARCHIVE, "Draw DEBUG_SHADOW (CSM debug) instead of global lighting pass." );

// Entity index to exclude from current deferred shadow pass (e.g. flashlight owner)
int g_iDeferredShadowExcludeEntIndex = -1;

static ConVar r_deferred_skyatmo_lut( "r_deferred_skyatmo_lut", "1" );
static ConVar r_deferred_skyatmo_lut_update_interval( "r_deferred_skyatmo_lut_update_interval", "0.0" );
static ConVar r_deferred_skyatmo_transmittance_lut_update_interval( "r_deferred_skyatmo_transmittance_lut_update_interval", "30.0" );
static ConVar r_deferred_skyatmo_multiscattering_lut_update_interval( "r_deferred_skyatmo_multiscattering_lut_update_interval", "30.0" );
static ConVar r_deferred_skyatmo_skyview_lut_update_interval( "r_deferred_skyatmo_skyview_lut_update_interval", "0.0" );

static ConVar r_deferred_light_global_smooth( "r_deferred_light_global_smooth", "1" );
static ConVar r_deferred_light_global_smooth_tau( "r_deferred_light_global_smooth_tau", "0.6" );

static lightData_Global_t& GetActiveGlobalLightState();

static void DrawSkyLUTPass( IMaterial *pMat, ITexture *pLut )
{
	if ( !pMat || !pLut )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( pLut );
	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false );
	const int w = pLut->GetActualWidth();
	const int h = pLut->GetActualHeight();
	pRenderContext->DrawScreenSpaceRectangle( pMat, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();
}

// HL2RPM: the atmosphere LUTs used to be regenerated (all four, including an
// unused 512x256 one) every single frame while the time of day was running.
// Now each LUT is only rebuilt when something it depends on changed:
//   transmittance / multi-scattering -> air turbidity (weather haze)
//   sky-view                         -> sun direction, light color, haze
static bool s_bForceSkyAtmoLUT = false;
CON_COMMAND( r_deferred_skyatmo_lut_rebuild, "Rebuild all atmosphere LUTs" )
{
	s_bForceSkyAtmoLUT = true;
}

// the global light as committed to the shaders this frame (after the temporal smoothing,
// before a lightning flash): what the LUT shaders will actually see
static lightData_Global_t s_skyLightState;
static bool s_bSkyLightStateValid = false;

static void UpdateSkyAtmoLUT()
{
	if ( !r_deferred_skyatmo_lut.GetBool() )
		return;
	if ( !GetGlobalLight() )
		return;

	// NOT the raw state: the shaders get the smoothed light (r_deferred_light_global_smooth_tau),
	// so after a time of day jump the raw direction was already at noon while the LUT was
	// built for a sun that had barely started to move - and never rebuilt again
	const lightData_Global_t &state = s_bSkyLightStateValid ? s_skyLightState : GetActiveGlobalLightState();
	const Vector curSunDir( state.vecLight.x, state.vecLight.y, state.vecLight.z );
	C_WeatherSystem *pWeather = GetWeatherSystem();
	// the LUT is built for WeatherRender_GetSkyLightIlluminance() when the weather is active
	const float flSkyLight = pWeather->IsActive() ? WeatherRender_GetSkyLightIlluminance() : 0.0f;
	const Vector curDiff = pWeather->IsActive() ? Vector( flSkyLight, flSkyLight, flSkyLight )
		: Vector( state.diff.x, state.diff.y, state.diff.z );
	const float flHaze = pWeather->IsActive() ? pWeather->GetParams().flHaze : 1.0f;

	static bool s_bInitialized = false;
	static Vector s_prevSunDir( 0, 0, 1 );
	static Vector s_prevDiff( 0, 0, 0 );
	static float s_flPrevHaze = -1.0f;
	static float s_flLastSkyView = -1000.0f;
	// The LUT shaders read the light/weather data committed to the shader DLL, which lags
	// this client-side state by a frame. A single rebuild right when the state changed
	// would bake the old sky - and with the time of day stopped (a jump with the F1 slider
	// or sv_timecycle_set_time) nothing would trigger another one: the sky stayed at dusk
	// under the noon sun. So every change is followed by a few more rebuilds.
	static int s_iFollowUp = 0;
	static int s_iFollowUpTransmittance = 0;

	if ( s_bForceSkyAtmoLUT )
	{
		s_bForceSkyAtmoLUT = false;
		s_bInitialized = false;
	}

	const bool bHazeChanged = fabsf( flHaze - s_flPrevHaze ) > 0.01f;
	const bool bTransmittance = !s_bInitialized || bHazeChanged || s_iFollowUpTransmittance > 0;

	const float flDiffDelta = ( curDiff - s_prevDiff ).Length();
	const bool bChanged = bTransmittance
		|| DotProduct( curSunDir, s_prevSunDir ) < 0.9999996f	// ~0.05 degrees
		|| flDiffDelta > 0.004f * Max( 0.05f, s_prevDiff.Length() );

	if ( !bChanged && s_iFollowUp <= 0 )
		return;

	const float flInterval = r_deferred_skyatmo_skyview_lut_update_interval.GetFloat();
	if ( bChanged && !bTransmittance && flInterval > 0.0f && gpGlobals->realtime - s_flLastSkyView < flInterval )
		return;

	if ( bChanged )
	{
		s_iFollowUp = 3;
		if ( !s_bInitialized || bHazeChanged )
			s_iFollowUpTransmittance = 3;
	}
	s_iFollowUp--;
	if ( s_iFollowUpTransmittance > 0 )
		s_iFollowUpTransmittance--;

	s_bInitialized = true;
	s_prevSunDir = curSunDir;
	s_prevDiff = curDiff;
	s_flPrevHaze = flHaze;
	s_flLastSkyView = gpGlobals->realtime;

	static ConVarRef cl_weather_debug( "cl_weather_debug" );
	if ( cl_weather_debug.IsValid() && cl_weather_debug.GetBool() )
		Msg( "[skylut] frame %d: sky-view%s (sun %.3f %.3f %.3f, light %.3f, haze %.2f, follow-up %d)\n",
			gpGlobals->framecount, bTransmittance ? " + transmittance" : "", curSunDir.x, curSunDir.y, curSunDir.z,
			curDiff.x, flHaze, s_iFollowUp );

	if ( bTransmittance )
	{
		DrawSkyLUTPass( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_TRANSMITTANCE_LUTGEN ), GetDefRT_SkyAtmoTransmittanceLUT() );
		DrawSkyLUTPass( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_MULTISCATTERING_LUTGEN ), GetDefRT_SkyAtmoMultiScatteringLUT() );
	}

	DrawSkyLUTPass( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_SKYVIEW_LUTGEN ), GetDefRT_SkyAtmoSkyViewLUT() );
}

// old implementation, kept for reference: regenerated every LUT whenever the sun moved
#if 0
static void UpdateSkyAtmoLUT_Old()
{
	// Transmittance LUT
	{
		IMaterial *pTransMat = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_TRANSMITTANCE_LUTGEN );
		ITexture *pTransLut = GetDefRT_SkyAtmoTransmittanceLUT();
		if ( pTransMat && pTransLut )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( pTransLut );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			const int tw = pTransLut->GetActualWidth();
			const int th = pTransLut->GetActualHeight();
			pRenderContext->DrawScreenSpaceRectangle( pTransMat,
			                                          0, 0, tw, th,
			                                          0, 0, tw - 1, th - 1,
			                                          tw, th );
			pRenderContext->PopRenderTargetAndViewport();
		}
	}

	// Multi-scattering LUT
	{
		IMaterial *pMsMat = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_MULTISCATTERING_LUTGEN );
		ITexture *pMsLut = GetDefRT_SkyAtmoMultiScatteringLUT();
		if ( pMsMat && pMsLut )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( pMsLut );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			const int mw = pMsLut->GetActualWidth();
			const int mh = pMsLut->GetActualHeight();
			pRenderContext->DrawScreenSpaceRectangle( pMsMat,
			                                          0, 0, mw, mh,
			                                          0, 0, mw - 1, mh - 1,
			                                          mw, mh );
			pRenderContext->PopRenderTargetAndViewport();
		}
	}

	// SkyView LUT
	{
		IMaterial *pSvMat = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_SKYVIEW_LUTGEN );
		ITexture *pSvLut = GetDefRT_SkyAtmoSkyViewLUT();
		if ( pSvMat && pSvLut )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( pSvLut );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			const int sw = pSvLut->GetActualWidth();
			const int sh = pSvLut->GetActualHeight();
			pRenderContext->DrawScreenSpaceRectangle( pSvMat,
			                                          0, 0, sw, sh,
			                                          0, 0, sw - 1, sh - 1,
			                                          sw, sh );
			pRenderContext->PopRenderTargetAndViewport();
		}
	}

	// SkyAtmo latlong LUT
	{
		IMaterial *pMat = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SKY_ATMO_LUTGEN );
		ITexture *pLut = GetDefRT_SkyAtmoLUT();
		if ( pMat && pLut )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( pLut );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			const int w = pLut->GetActualWidth();
			const int h = pLut->GetActualHeight();
			pRenderContext->DrawScreenSpaceRectangle( pMat,
			                                          0, 0, w, h,
			                                          0, 0, w - 1, h - 1,
			                                          w, h );
			pRenderContext->PopRenderTargetAndViewport();
		}
	}
}
#endif

void SetClearColorToFogColor()
{
	unsigned char ucFogColor[3];
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->GetFogColor( ucFogColor );
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
	{
		float scale = LinearToGammaFullRange( pRenderContext->GetToneMappingScaleLinear().x );
		ucFogColor[0] *= scale;
		ucFogColor[1] *= scale;
		ucFogColor[2] *= scale;
	}
	pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
}

void GetSkyboxFogColor( float *pColor )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if ( !pbp )
	{
		return;
	}
	CPlayerLocalData *local = &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	static ConVarRef fog_colorskybox( "fog_colorskybox" );

	const char *fogColorString = fog_colorskybox.IsValid() ? fog_colorskybox.GetString() : NULL;
	if ( fog_override.IsValid() && fog_override.GetInt() && fogColorString )
	{
		sscanf( fogColorString, "%f%f%f", pColor, pColor + 1, pColor + 2 );
	}
	else
	{
		if ( local->m_skybox3d.fog.blend )
		{
			Vector forward;
			pbp->EyeVectors( &forward, NULL, NULL );

			Vector vNormalized = local->m_skybox3d.fog.dirPrimary;
			VectorNormalize( vNormalized );
			local->m_skybox3d.fog.dirPrimary = vNormalized;

			float flBlendFactor = 0.5f * forward.Dot( local->m_skybox3d.fog.dirPrimary ) + 0.5f;

			pColor[0] = local->m_skybox3d.fog.colorPrimary.GetR() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetR() * ( 1 - flBlendFactor );
			pColor[1] = local->m_skybox3d.fog.colorPrimary.GetG() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetG() * ( 1 - flBlendFactor );
			pColor[2] = local->m_skybox3d.fog.colorPrimary.GetB() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetB() * ( 1 - flBlendFactor );
		}
		else
		{
			pColor[0] = local->m_skybox3d.fog.colorPrimary.GetR();
			pColor[1] = local->m_skybox3d.fog.colorPrimary.GetG();
			pColor[2] = local->m_skybox3d.fog.colorPrimary.GetB();
		}
	}

	VectorScale( pColor, 1.0f / 255.0f, pColor );
}

float GetSkyboxFogStart( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if ( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData *local = &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	static ConVarRef fog_startskybox( "fog_startskybox" );

	if ( fog_override.IsValid() && fog_override.GetInt() )
	{
		if ( fog_startskybox.IsValid() && fog_startskybox.GetFloat() != -1.0f )
		{
			return fog_startskybox.GetFloat();
		}
		return local->m_skybox3d.fog.start;
	}
	return local->m_skybox3d.fog.start;
}

float GetSkyboxFogEnd( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if ( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData *local = &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	static ConVarRef fog_endskybox( "fog_endskybox" );

	if ( fog_override.IsValid() && fog_override.GetInt() )
	{
		if ( fog_endskybox.IsValid() && fog_endskybox.GetFloat() != -1.0f )
		{
			return fog_endskybox.GetFloat();
		}
		return local->m_skybox3d.fog.end;
	}
	return local->m_skybox3d.fog.end;
}

float GetSkyboxFogMaxDensity()
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if ( !pbp )
		return 1.0f;

	static ConVarRef cl_leveloverview( "cl_leveloverview" );
	if ( cl_leveloverview.IsValid() && cl_leveloverview.GetFloat() > 0 )
		return 1.0f;

	if ( !g_pClientMode->ShouldDrawFog() )
		return 1.0f;

	CPlayerLocalData *local = &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	static ConVarRef fog_maxdensityskybox( "fog_maxdensityskybox" );

	if ( fog_override.IsValid() && fog_override.GetInt() )
	{
		if ( fog_maxdensityskybox.IsValid() && fog_maxdensityskybox.GetFloat() != -1.0f )
			return fog_maxdensityskybox.GetFloat();
		return local->m_skybox3d.fog.maxdensity;
	}

	return local->m_skybox3d.fog.maxdensity;
}

void DrawCube( CMeshBuilder &meshBuilder, Vector vecPos, float flRadius, float *pfl2Texcoords )
{
	const float flOffsets[24][3] = {
		-flRadius, -flRadius, -flRadius,
		flRadius, -flRadius, -flRadius,
		flRadius, flRadius, -flRadius,
		-flRadius, flRadius, -flRadius,

		flRadius, flRadius, flRadius,
		flRadius, flRadius, -flRadius,
		flRadius, -flRadius, -flRadius,
		flRadius, -flRadius, flRadius,

		-flRadius, flRadius, flRadius,
		-flRadius, -flRadius, flRadius,
		-flRadius, -flRadius, -flRadius,
		-flRadius, flRadius, -flRadius,

		flRadius, flRadius, flRadius,
		-flRadius, flRadius, flRadius,
		-flRadius, flRadius, -flRadius,
		flRadius, flRadius, -flRadius,

		flRadius, -flRadius, flRadius,
		flRadius, -flRadius, -flRadius,
		-flRadius, -flRadius, -flRadius,
		-flRadius, -flRadius, flRadius,

		flRadius, flRadius, flRadius,
		flRadius, -flRadius, flRadius,
		-flRadius, -flRadius, flRadius,
		-flRadius, flRadius, flRadius,
	};

	for ( int i = 0; i < 24; i++ )
	{
		meshBuilder.Position3f( vecPos.x + flOffsets[i][0],
			vecPos.y + flOffsets[i][1],
			vecPos.z + flOffsets[i][2] );
		meshBuilder.TexCoord2fv( 0, pfl2Texcoords );
		meshBuilder.AdvanceVertex();
	}
}


class CBaseWorldViewDeferred : public CRendering3dView
{
	DECLARE_CLASS( CBaseWorldViewDeferred, CRendering3dView );
protected:
	CBaseWorldViewDeferred(CViewRender *pMainView) : CRendering3dView( pMainView ), m_bRenderablesListBuilt( false ) {}

	virtual bool	AdjustView( float waterHeight );

	void			DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf = -1, bool bShadowDepth = false );
	void			DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth = false );

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	// BUGBUG this causes all sorts of problems
	virtual bool	ShouldCacheLists(){ return false; };

	virtual void	DrawWorldDeferred( float waterZAdjust );
	virtual void	DrawOpaqueRenderablesDeferred(bool);

	// HL2RPM: called after the opaque scene, before translucents
	virtual void	OnPostOpaque() {}

	// HL2RPM: the main G-buffer view also draws the opaque parts of translucent models and
	// the detail sprites (grass) into the G-buffer
	virtual bool	IsMainGBufferView() const { return false; }
	void			DrawTranslucentModelsOpaquePartsToGBuffer();
	void			DrawDetailSpritesToGBuffer();

	// HL2RPM: shadow views skip casters whose angular size from the camera is below this
	// (their shadow would be a couple of pixels) - 0 = keep everything
	virtual float	GetShadowCasterCullRatio() { return 0.0f; }
	void			CullSmallShadowCasters();

protected:

	static void PushComposite();
	static void PopComposite();

	// HL2RPM: DrawSetup built this view's renderables list (it does only with entities)
	bool m_bRenderablesListBuilt;
};

//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
class CSimpleWorldViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CSimpleWorldViewDeferred, CBaseWorldViewDeferred );
public:
	CSimpleWorldViewDeferred(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

	void			Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& info, ViewCustomVisibility_t *pCustomVisibility = NULL );
	void			Draw();

	virtual bool	ShouldCacheLists() { return true; }

	// HL2RPM: wet surfaces + weather fog on the lit opaque scene
	virtual void	OnPostOpaque() { WeatherRender_PostOpaque( *this ); }

private:
	VisibleFogVolumeInfo_t m_fogInfo;
};

//-----------------------------------------------------------------------------
// HL2RPM: func_reflective_glass (mirrors, windows) - the reflection / refraction views were
// never ported to the deferred renderer ("#if 0 // TODO"): the glass showed its render
// targets as they were (black). Drawn like the water views: composite only, a reflection
// looks its lighting up in the main view's light buffer (REFLECTVIEW, approximate light
// where the main view doesn't see the point).
//-----------------------------------------------------------------------------
class CGlassViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CGlassViewDeferred, CBaseWorldViewDeferred );
public:
	CGlassViewDeferred( CViewRender *pMainView, bool bReflection ) :
		CBaseWorldViewDeferred( pMainView ), m_pRenderTarget( NULL ), m_bReflection( bReflection ) {}

	void			Setup( const CViewSetup &view, bool bDrawSkybox, const cplane_t &plane, ITexture *pRenderTarget );
	void			Draw();

	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

private:
	cplane_t		m_Plane;
	ITexture		*m_pRenderTarget;
	bool			m_bReflection;
};

class CGBufferView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CGBufferView, CBaseWorldViewDeferred );
public:
	CGBufferView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView )
	{
	}

	void			Setup( const CViewSetup &view, bool bDrewSkybox );
	void			Draw();

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	static void PushGBuffer( bool bInitial, float zScale = 1.0f, bool bClearDepth = true );
	static void PopGBuffer();

	virtual bool	IsMainGBufferView() const { return true; }

private:
	VisibleFogVolumeInfo_t m_fogInfo;
	bool m_bDrewSkybox;
};

class CSkyboxViewDeferred : public CGBufferView
{
	DECLARE_CLASS( CSkyboxViewDeferred, CRendering3dView );
public:
	CSkyboxViewDeferred(CViewRender *pMainView) :
		CGBufferView( pMainView ),
		m_pSky3dParams( NULL )
	  {
	  }

	bool			Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible );
	void			Draw();

protected:

	virtual SkyboxVisibility_t	ComputeSkyboxVisibility();
	bool			GetSkyboxFogEnable();

	void			Enable3dSkyboxFog( void );
	void			DrawInternal( view_id_t iSkyBoxViewID = VIEW_3DSKY, ITexture *pRenderTarget = NULL, ITexture *pDepthTarget = NULL );

	sky3dparams_t *	PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible );
	sky3dparams_t *m_pSky3dParams;

	bool		m_bGBufferPass;
};

class CPostLightingView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CPostLightingView, CBaseWorldViewDeferred );
public:
	CPostLightingView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView )
	{
	}

	void			Setup( const CViewSetup &view );
	void			Draw();

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	DrawWorldDeferred( float waterZAdjust );
	virtual void	DrawOpaqueRenderablesDeferred(bool);

	static void		PushDeferredShadingFrameBuffer();
	static void		PopDeferredShadingFrameBuffer();

private:
	VisibleFogVolumeInfo_t m_fogInfo;
};

class CBaseShadowView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CBaseShadowView, CBaseWorldViewDeferred );
public:
	CBaseShadowView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView )
	{
		m_bOutputRadiosity = false;
		m_iShadowExcludeEntIndex = -1;
	};

	int m_iShadowExcludeEntIndex;

	void			Setup( const CViewSetup &view,
						ITexture *pDepthTexture,
						ITexture *pDummyTexture );
	void			SetupRadiosityTargets(
						ITexture *pAlbedoTexture,
						ITexture *pNormalTexture );

	void SetRadiosityOutputEnabled( bool bEnabled );

	void			Draw();
	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	CalcShadowView() = 0;
	virtual void	CommitData(){};

	virtual int		GetShadowMode() = 0;

	void			AddExtraVisOrigin( const Vector &vecOrigin ) { shadowVis.AddVisOrigin( vecOrigin ); }

private:

	ITexture *m_pDepthTexture;
	ITexture *m_pDummyTexture;
	ITexture *m_pRadAlbedoTexture;
	ITexture *m_pRadNormalTexture;
	ViewCustomVisibility_t shadowVis;

	bool m_bOutputRadiosity;
};

// HL2RPM: top-down depth map of everything that can block rain ("rain occlusion map").
// Rendered through the regular shadow depth pass, only when the player moved or
// every second, and only while it's raining or surfaces are wet.
class CRainOcclusionView : public CBaseShadowView
{
	DECLARE_CLASS( CRainOcclusionView, CBaseShadowView );
public:
	CRainOcclusionView( CViewRender *pMainView, const Vector &vecCenter, float flSize )
		: CBaseShadowView( pMainView )
	{
		m_vecCenter = vecCenter;
		m_flSize = flSize;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();
	virtual int		GetShadowMode() { return DEFERRED_SHADOW_MODE_ORTHO; }

private:
	Vector m_vecCenter;
	float m_flSize;
};

#define RAINMAP_HEIGHT_ABOVE	6000.0f
#define RAINMAP_ZFAR			14000.0f

void CRainOcclusionView::CalcShadowView()
{
	const int iRes = GetWeatherRainMapResolution();

	origin = m_vecCenter + Vector( 0, 0, RAINMAP_HEIGHT_ABOVE );
	angles.Init( 90.0f, 0.0f, 0.0f );	// straight down

	x = 0;
	y = 0;
	width = iRes;
	height = iRes;

	const float flHalf = m_flSize * 0.5f;
	m_bOrtho = true;
	m_OrthoLeft = -flHalf;
	m_OrthoTop = -flHalf;
	m_OrthoRight = flHalf;
	m_OrthoBottom = flHalf;

	zNear = zNearViewmodel = 0.0f;
	zFar = zFarViewmodel = RAINMAP_ZFAR;
	m_flAspectRatio = 1.0f;
	fov = fovViewmodel = 90.0f;
}

void CRainOcclusionView::CommitData()
{
	VMatrix a, b, c, d, screenToTexture;
	render->GetMatricesForView( *this, &a, &b, &c, &d );
	MatrixBuildScale( screenToTexture, 0.5f, -0.5f, 1.0f );
	screenToTexture[0][3] = 0.5f;
	screenToTexture[1][3] = 0.5f;

	VMatrix matWorldToTexture;
	MatrixMultiply( screenToTexture, c, matWorldToTexture );
	WeatherRender_OnRainMapRendered( matWorldToTexture, m_vecCenter, m_flSize, RAINMAP_ZFAR );

	// depth written by the shadow pass = distance straight down from this camera / zFar
	shadowData_ortho_t shadowData;
	shadowData.iRes_x = width;
	shadowData.iRes_y = height;
	shadowData.matWorldToTexture = matWorldToTexture;
	shadowData.vecUVTransform.Init( 0, 0, 1, 1 );
	shadowData.vecSlopeSettings.Init( 0, 0, 0, 1.0f / RAINMAP_ZFAR );
	shadowData.vecOrigin.Init( origin, 1.0f );
	QUEUE_FIRE( CommitShadowData_Rain, shadowData );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, DEFERRED_SHADOW_INDEX_RAIN );
	// ortho maps take their slope bias from the rasterizer (see COrthoShadowView::CommitData)
	const float flTexelDepth = ( m_flSize / (float)Max( width, 1 ) ) / RAINMAP_ZFAR;
	pRenderContext->SetShadowDepthBiasFactors( 2.0f, flTexelDepth * 0.5f );
}

class COrthoShadowView : public CBaseShadowView
{
	DECLARE_CLASS( COrthoShadowView, CBaseShadowView );
public:
	COrthoShadowView(CViewRender *pMainView, const int &index)
		: CBaseShadowView( pMainView )
	{
			iCascadeIndex = index;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_ORTHO;
	};

	// the first cascade keeps everything (contact shadows up close)
	virtual float	GetShadowCasterCullRatio();

private:
	int iCascadeIndex;
};

// HL2RPM: small - the receivers get theirs (moved along the geometric normal, see
// lightingpass_global_ps30). The old 3x pushed the casters' steep faces back so far that the
// floor at the foot of a wall was in front of the wall: a lit gap along the base of walls.
static ConVar r_csm_slope_bias( "r_csm_slope_bias", "0.5", 0, "Slope-scaled depth bias of the sun shadow cascades (rasterizer)" );
// HL2RPM dev: moves the cascade fits by a random offset every frame (units) with the camera
// still - with texel snapping the shadows must not change at all; anything that does flickers
// when the player walks
static ConVar r_csm_debug_log( "r_csm_debug_log", "0", 0, "Dev: print every sun cascade fit (frame, cascade, fov, radius, texel, snapped origin, light)" );
// (0 by default: with the camera at the center the culling of the casters ignored the negative near
// plane - whatever stood toward the sun from the cascade's center was dropped, and parts of the
// shadows of things over the player (the floating boxes of test_deferred) came and went with a
// step or a turn of the camera)
static ConVar r_csm_center_origin( "r_csm_center_origin", "0", 0, "Sun cascades: the light camera at the cascade's center with a negative near plane (0 = 12000 units toward the sun)" );
static ConVar r_csm_debug_jitter( "r_csm_debug_jitter", "0", FCVAR_CHEAT, "Dev: random offset of the sun cascade fits every frame (units), to test their stability" );
static ConVar r_csm_const_bias( "r_csm_const_bias", "0.5", 0, "Constant depth bias of the sun shadow cascades (rasterizer), in shadow map texels" );
// HL2RPM: the spot / point light maps don't write the depth from the shader any more either
// (shadowpass_ps30): the old "d + ( fwidth( d ) + 1e-6 ) * 3" bias is the rasterizer's now
static ConVar r_deferred_shadow_proj_slope_bias( "r_deferred_shadow_proj_slope_bias", "4.0", 0, "Slope-scaled depth bias of the spot / point light shadow maps (rasterizer)" );
static ConVar r_deferred_shadow_proj_depth_bias( "r_deferred_shadow_proj_depth_bias", "0.000003", 0, "Constant depth bias of the spot / point light shadow maps (rasterizer)" );

static void SetProjectedShadowDepthBias()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetShadowDepthBiasFactors( r_deferred_shadow_proj_slope_bias.GetFloat(),
		r_deferred_shadow_proj_depth_bias.GetFloat() );
}
// HL2RPM: the old order (the weather constants carry the matrix of the previous rain map
// render): for A/B of the one-frame flash it gave while walking
static ConVar r_deferred_rainmap_debug_late( "r_deferred_rainmap_debug_late", "0", FCVAR_CHEAT,
	"Debug: render the rain occlusion map after the weather constants of the frame (old, wrong order)" );
static ConVar r_csm_novis( "r_csm_novis", "1", 0, "Sun shadow cascades draw casters from every leaf (0 = only the PVS of the camera: shadows of what's outside it pop in and out)" );

// HL2RPM: open maps with thousands of props rendered them into every sun cascade;
// a prop far away that covers a few pixels casts a shadow of a few pixels
static ConVar r_csm_caster_cull( "r_csm_caster_cull", "0.006", FCVAR_ARCHIVE,
	"Distant sun shadow cascades skip props smaller than this fraction of their distance to the camera (0 = off)", true, 0.0f, true, 0.05f );

float COrthoShadowView::GetShadowCasterCullRatio()
{
	return ( iCascadeIndex > 0 ) ? r_csm_caster_cull.GetFloat() : 0.0f;
}

class CDualParaboloidShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CDualParaboloidShadowView, CBaseShadowView );
public:
	CDualParaboloidShadowView(CViewRender *pMainView,
		def_light_t *pLight,
		const bool &bSecondary)
		: CBaseShadowView( pMainView )
	{
			m_pLight = pLight;
			m_bSecondary = bSecondary;
			m_iShadowExcludeEntIndex = pLight->iShadowExcludeEntIndex;
	}
	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	CalcShadowView();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_DPSM;
	};

private:
	bool m_bSecondary;
	def_light_t *m_pLight;
};

class CPointLightCubeFaceShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CPointLightCubeFaceShadowView, CBaseShadowView );
public:
	CPointLightCubeFaceShadowView( CViewRender *pMainView,
		def_light_t *pLight, int faceIndex, int shadowMapIndex )
		: CBaseShadowView( pMainView )
	{
		m_pLight = pLight;
		m_iFaceIndex = faceIndex;
		m_iShadowMapIndex = shadowMapIndex;
		m_iShadowExcludeEntIndex = pLight->iShadowExcludeEntIndex;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_PROJECTED;
	};

private:
	def_light_t *m_pLight;
	int m_iFaceIndex;
	int m_iShadowMapIndex;
};

class CSpotLightShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CSpotLightShadowView, CBaseShadowView );
public:
	CSpotLightShadowView(CViewRender *pMainView,
		def_light_t *pLight, int index )
		: CBaseShadowView( pMainView )
	{
			m_pLight = pLight;
			m_iIndex = index;
			m_iShadowExcludeEntIndex = pLight->iShadowExcludeEntIndex;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_PROJECTED;
	};

private:
	def_light_t *m_pLight;
	int m_iIndex;
};

//-----------------------------------------------------------------------------
// Base class for scenes with water
//-----------------------------------------------------------------------------
class CBaseWaterViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CBaseWaterViewDeferred, CBaseWorldViewDeferred );
public:
	CBaseWaterViewDeferred(CViewRender *pMainView) :
		CBaseWorldViewDeferred( pMainView ),
		m_SoftwareIntersectionView( pMainView )
	{}

	//	void Setup( const CViewSetup &, const WaterRenderInfo_t& info );

protected:
	void			CalcWaterEyeAdjustments( const VisibleFogVolumeInfo_t &fogInfo, float &newWaterHeight, float &waterZAdjust, bool bSoftwareUserClipPlane );

	class CSoftwareIntersectionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CSoftwareIntersectionView, CBaseWorldViewDeferred );
	public:
		CSoftwareIntersectionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup( bool bAboveWater );
		void Draw();

	private:
		CBaseWaterViewDeferred *GetOuter() { return GET_OUTER( CBaseWaterViewDeferred, m_SoftwareIntersectionView ); }
	};

	friend class CSoftwareIntersectionView;

	CSoftwareIntersectionView m_SoftwareIntersectionView;

	WaterRenderInfo_t m_waterInfo;
	float m_waterHeight;
	float m_waterZAdjust;
	bool m_bSoftwareUserClipPlane;
	VisibleFogVolumeInfo_t m_fogInfo;
};


//-----------------------------------------------------------------------------
// Scenes above water
//-----------------------------------------------------------------------------
class CAboveWaterViewDeferred : public CBaseWaterViewDeferred
{
	DECLARE_CLASS( CAboveWaterViewDeferred, CBaseWaterViewDeferred );
public:
	CAboveWaterViewDeferred(CViewRender *pMainView) :
		CBaseWaterViewDeferred( pMainView ),
		m_ReflectionView( pMainView ),
		m_RefractionView( pMainView ),
		m_IntersectionView( pMainView )
	{}

	void Setup(  const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& waterInfo );
	void			Draw();

	// HL2RPM: wet surfaces + weather fog on the lit opaque scene, as CSimpleWorldViewDeferred.
	// With water in sight the main view is drawn by this class, which had none: the wet
	// look and the height fog vanished whenever water came into view. (Only this view's own
	// pass - the reflection/refraction views below keep the empty one.)
	virtual void	OnPostOpaque() { WeatherRender_PostOpaque( *this ); }

	class CReflectionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CReflectionView, CBaseWorldViewDeferred );
	public:
		CReflectionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup( bool bReflectEntities );
		void Draw();

	private:
		CAboveWaterViewDeferred *GetOuter() { return GET_OUTER( CAboveWaterViewDeferred, m_ReflectionView ); }
	};

	class CRefractionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CRefractionView, CBaseWorldViewDeferred );
	public:
		CRefractionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup();
		void Draw();

	private:
		CAboveWaterViewDeferred *GetOuter() { return GET_OUTER( CAboveWaterViewDeferred, m_RefractionView ); }
	};

	class CIntersectionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CIntersectionView, CBaseWorldViewDeferred );
	public:
		CIntersectionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup();
		void Draw();

	private:
		CAboveWaterViewDeferred *GetOuter() { return GET_OUTER( CAboveWaterViewDeferred, m_IntersectionView ); }
	};


	friend class CRefractionView;
	friend class CReflectionView;
	friend class CIntersectionView;

	bool m_bViewIntersectsWater;

	CReflectionView m_ReflectionView;
	CRefractionView m_RefractionView;
	CIntersectionView m_IntersectionView;
};


//-----------------------------------------------------------------------------
// Scenes below water
//-----------------------------------------------------------------------------
class CUnderWaterViewDeferred : public CBaseWaterViewDeferred
{
	DECLARE_CLASS( CUnderWaterViewDeferred, CBaseWaterViewDeferred );
public:
	CUnderWaterViewDeferred(CViewRender *pMainView) :
		CBaseWaterViewDeferred( pMainView ),
		m_RefractionView( pMainView )
	{}

	void			Setup( const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& info );
	void			Draw();

	class CRefractionView : public CBaseWorldViewDeferred
	{
		DECLARE_CLASS( CRefractionView, CBaseWorldViewDeferred );
	public:
		CRefractionView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

		void Setup();
		void Draw();

	private:
		CUnderWaterViewDeferred *GetOuter() { return GET_OUTER( CUnderWaterViewDeferred, m_RefractionView ); }
	};

	friend class CRefractionView;

	bool m_bDrawSkybox; // @MULTICORE (toml 8/17/2006): remove after setup hoisted

	CRefractionView m_RefractionView;
};

bool CBaseWorldViewDeferred::AdjustView( float waterHeight )
{
	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		ITexture *pTexture = GetWaterRefractionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();

		return true;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();
		angles[0] = -angles[0];
		angles[2] = -angles[2];
		origin[2] -= 2.0f * ( origin[2] - (waterHeight));
		return true;
	}

	return false;
}

// HL2RPM: shadow views build their world lists as shadow depth lists. A normal list drops
// the faces turned away from the view (the side of each BSP node plane the view is on),
// so a wall whose sunlit side is nodraw - vbsp keeps no face there - cast only the thin
// shadow of its edges. Together with the shadow pass drawing both sides (defpass_shadow)
// every face casts.
static ConVar r_deferred_shadow_worldlists( "r_deferred_shadow_worldlists", "1", 0, "Shadow views keep world faces turned away from the light (single-sided walls with nodraw backs cast shadows)" );

void CBaseWorldViewDeferred::DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf,
	bool bShadowDepth )
{
	const view_id_t savedViewID = CurrentViewID();
	const Vector savedOrigin = CurrentViewOrigin();
	const QAngle savedAngles = CurrentViewAngles();

	const bool bViewChanged = AdjustView( waterHeight );
	SetupCurrentView( origin, angles, bShadowDepth ? VIEW_DEFERRED_SHADOW : VIEW_MAIN );

	if ( bViewChanged )
	{
		render->Push3DView( *this, 0, NULL, GetFrustum() );
	}

	const bool bDrawEntities = ( nSetupFlags & DF_DRAW_ENTITITES ) != 0;
	const bool bDrawReflection = ( nSetupFlags & DF_RENDER_REFLECTION ) != 0;
	BuildWorldRenderLists( bDrawEntities, iForceViewLeaf, ShouldCacheLists(), bShadowDepth && r_deferred_shadow_worldlists.GetBool(), bDrawReflection ? &waterHeight : NULL );

	PruneWorldListInfo();

	m_bRenderablesListBuilt = bDrawEntities;
	if ( bDrawEntities )
	{
		const bool bOptimized = bShadowDepth;
		BuildRenderableRenderLists( bOptimized ? VIEW_SHADOW_DEPTH_TEXTURE : savedViewID );

		if ( bShadowDepth )
			CullSmallShadowCasters();
	}

	if ( bViewChanged )
	{
		render->PopView( GetFrustum() );
	}

	SetupCurrentView( savedOrigin, savedAngles, savedViewID );
}

void CBaseWorldViewDeferred::CullSmallShadowCasters()
{
	const float flRatio = GetShadowCasterCullRatio();
	if ( flRatio <= 0.0f || !m_pRenderablesList )
		return;

	const Vector &vecCamera = MainViewOrigin();
	for ( int g = RENDER_GROUP_OPAQUE_STATIC_HUGE; g <= RENDER_GROUP_OPAQUE_ENTITY; g++ )
	{
		CClientRenderablesList::CEntry *pEntries = m_pRenderablesList->m_RenderGroups[g];
		const int nCount = m_pRenderablesList->m_RenderGroupCounts[g];
		int nKept = 0;
		for ( int i = 0; i < nCount; i++ )
		{
			IClientRenderable *pRenderable = pEntries[i].m_pRenderable;
			if ( pRenderable )
			{
				Vector vecMins, vecMaxs;
				pRenderable->GetRenderBounds( vecMins, vecMaxs );
				const float flRadius = ( vecMaxs - vecMins ).Length() * 0.5f;
				const Vector vecCenter = pRenderable->GetRenderOrigin() + ( vecMins + vecMaxs ) * 0.5f;
				if ( flRadius < vecCenter.DistTo( vecCamera ) * flRatio )
					continue;
			}
			pEntries[nKept++] = pEntries[i];
		}
		m_pRenderablesList->m_RenderGroupCounts[g] = nKept;
	}
}

void CBaseWorldViewDeferred::DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth )
{
	const view_id_t savedViewID = CurrentViewID();
	const Vector savedOrigin = CurrentViewOrigin();
	const QAngle savedAngles = CurrentViewAngles();

	// @MULTICORE (toml 8/16/2006): rethink how, where, and when this is done...
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );
	//MaybeInvalidateLocalPlayerAnimation();
	//g_pClientShadowMgr->ComputeShadowTextures( *this, m_pWorldListInfo->m_LeafCount, m_pWorldListInfo->m_pLeafList );
	MaybeInvalidateLocalPlayerAnimation();

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	SetupCurrentView( origin, angles, viewID );

	// Update our render view flags.
	const int iDrawFlagsBackup = m_DrawFlags;
	m_DrawFlags |= m_pMainView->GetBaseDrawFlags();

	PushView( waterHeight );

	CMatRenderContextPtr pRenderContext( materials );

	ITexture *pSaveFrameBufferCopyTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
	if ( engine->GetDXSupportLevel() >= 80 )
	{
		pRenderContext->SetFrameBufferCopyTexture( GetPowerOfTwoFrameBufferTexture() );
	}

	pRenderContext.SafeRelease();

	//helper::bDisableDecalRendering = m_bDrawWorldNormal;

	// During shadow pass, disable world decals to prevent engine-internal
	// Subrect shader from writing decals into the shadow depth buffer.
	static ConVarRef r_drawdecals( "r_drawdecals" );
	const int iOldDrawDecals = r_drawdecals.IsValid() ? r_drawdecals.GetInt() : -1;
	if ( bShadowDepth && r_drawdecals.IsValid() )
		r_drawdecals.SetValue( 0 );

	DrawWorldDeferred( waterZAdjust );

	if ( bShadowDepth && iOldDrawDecals >= 0 )
		r_drawdecals.SetValue( iOldDrawDecals );

	// HL2RPM: only with a renderables list of this view's own. A view drawn without entities
	// (the reflection of a water without $reflectentities) builds none and read whatever the
	// pointer held - freed memory: a crash in DrawOpaqueRenderables_DrawBrushModels
	if ( m_bRenderablesListBuilt && m_pRenderablesList )
		DrawOpaqueRenderablesDeferred( false );

	if ( IsMainGBufferView() )
	{
		DrawTranslucentModelsOpaquePartsToGBuffer();
		DrawDetailSpritesToGBuffer();
	}

	if ( !bShadowDepth )
		OnPostOpaque();

	//if (!m_bDrawWorldNormal)
	{
		if ( m_DrawFlags & DF_DRAW_ENTITITES )
		{
			if (!bShadowDepth)
			{
				DrawTranslucentRenderables( false, false );
				DrawNoZBufferTranslucentRenderables();
			}
		}
		else
		{
			// Draw translucent world brushes only, no entities
			DrawTranslucentWorldInLeaves( false );
		}
	}

	pRenderContext.GetFrom( materials );
	pRenderContext->SetFrameBufferCopyTexture( pSaveFrameBufferCopyTexture );
	PopView();

	m_DrawFlags = iDrawFlagsBackup;

	SetupCurrentView( savedOrigin, savedAngles, savedViewID );
	//helper::bDisableDecalRendering = false;
}

void CBaseWorldViewDeferred::PushView( float waterHeight )
{
	float spread = 2.0f;
	if( m_DrawFlags & DF_FUDGE_UP )
	{
		waterHeight += spread;
	}
	else
	{
		waterHeight -= spread;
	}

	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	static ConVarRef mat_clipz( "mat_clipz" );
	if ( ( m_DrawFlags & DF_CLIP_Z ) && mat_clipz.IsValid() && mat_clipz.GetBool() )
	{
		if( m_DrawFlags & DF_CLIP_BELOW )
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		}
		else
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
		}
	}

	CMatRenderContextPtr pRenderContext( materials );

	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		pRenderContext->SetFogZ( waterHeight );
		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		// Have to re-set up the view since we reset the size
		render->Push3DView( *this, m_ClearFlags, GetWaterRefractionTexture(), GetFrustum() );

		return;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		pRenderContext->SetFogZ( waterHeight );

		bool bSoftwareUserClipPlane = g_pMaterialSystemHardwareConfig->UseFastClipping();
		static ConVarRef r_eyewaterepsilon( "r_eyewaterepsilon" );
		const float flEyeWaterEpsilon = r_eyewaterepsilon.IsValid() ? r_eyewaterepsilon.GetFloat() : 10.0f;
		if( bSoftwareUserClipPlane && ( origin[2] > waterHeight - flEyeWaterEpsilon ) )
		{
			waterHeight = origin[2] + flEyeWaterEpsilon;
		}

		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		render->Push3DView( *this, m_ClearFlags, pTexture, GetFrustum() );
		return;
	}

	if ( m_ClearFlags & ( VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR | VIEW_CLEAR_STENCIL ) )
	{
		if ( m_ClearFlags & VIEW_CLEAR_OBEY_STENCIL )
		{
			pRenderContext->ClearBuffersObeyStencil( m_ClearFlags & VIEW_CLEAR_COLOR, m_ClearFlags & VIEW_CLEAR_DEPTH );
		}
		else
		{
			pRenderContext->ClearBuffers( m_ClearFlags & VIEW_CLEAR_COLOR, m_ClearFlags & VIEW_CLEAR_DEPTH, m_ClearFlags & VIEW_CLEAR_STENCIL );
		}
	}

	pRenderContext->SetHeightClipMode( clipMode );
	if ( clipMode != MATERIAL_HEIGHTCLIPMODE_DISABLE )
	{
		pRenderContext->SetHeightClipZ( waterHeight );
	}
}

void CBaseWorldViewDeferred::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );
	if( m_DrawFlags & (DF_RENDER_REFRACTION | DF_RENDER_REFLECTION) )
	{
		if ( IsX360() )
		{
			// these renders paths used their surfaces, so blit their results
			if ( m_DrawFlags & DF_RENDER_REFRACTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterRefractionTexture(), NULL, NULL );
			}
			if ( m_DrawFlags & DF_RENDER_REFLECTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterReflectionTexture(), NULL, NULL );
			}
		}

		render->PopView( GetFrustum() );
	}
}

void CBaseWorldViewDeferred::DrawWorldDeferred( float waterZAdjust )
{
	DrawWorld( waterZAdjust );
}

// HL2RPM dev: what the renderables lists of the views hold (two-pass models by name)
static ConVar r_deferred_debug_lists( "r_deferred_debug_lists", "0", 0, "Dev: print the renderables lists of the deferred views once a second" );
static void DebugPrintRenderablesList( CClientRenderablesList *pList, const char *pszView )
{
	static float s_flNext = 0.0f;
	static int s_nPrinted = 0;
	if ( gpGlobals->realtime >= s_flNext )
	{
		s_flNext = gpGlobals->realtime + 1.0f;
		s_nPrinted = 0;
	}
	if ( s_nPrinted++ > 8 )
		return;
	Msg( "[lists] %s:", pszView );
	for ( int g = 0; g < RENDER_GROUP_COUNT; g++ )
		Msg( " %d", pList->m_RenderGroupCounts[g] );
	Msg( "\n" );
	for ( int g = 0; g < RENDER_GROUP_COUNT; g++ )
	{
		for ( int i = 0; i < pList->m_RenderGroupCounts[g]; i++ )
		{
			const CClientRenderablesList::CEntry &e = pList->m_RenderGroups[g][i];
			if ( !e.m_pRenderable || !e.m_pRenderable->GetModel() )
				continue;
			const char *pszModel = modelinfo->GetModelName( e.m_pRenderable->GetModel() );
			if ( e.m_TwoPass || ( pszModel && V_stristr( pszModel, "fullscale" ) ) )
				Msg( "[lists]   group %d twopass %d %s\n", g, e.m_TwoPass ? 1 : 0, pszModel ? pszModel : "?" );
		}
	}
}

// HL2RPM: a model with one translucent material among opaque ones is a translucent renderable;
// only when the engine flags it two-pass (not always: the full scale row houses of demo_map
// with their translucent antenna aren't) is it drawn in the opaque pass as well. Otherwise it was
// drawn only with the translucents, after the lighting: its opaque facade never got into the
// G-buffer and its composite read the light of whatever was behind it - the 3D skybox city: the
// shapes of the skybox buildings showed "through" the houses of the map. Their opaque parts are
// drawn into the G-buffer here (STUDIO_TWOPASS without STUDIO_TRANSPARENCY = opaque meshes only);
// the translucent pass draws them as before and now reads their own light.
static ConVar r_deferred_gbuffer_translucent_models( "r_deferred_gbuffer_translucent_models", "1", 0,
	"Draw the opaque parts of translucent (not two-pass) models into the G-buffer" );
void CBaseWorldViewDeferred::DrawTranslucentModelsOpaquePartsToGBuffer()
{
	if ( !m_pRenderablesList || !( m_DrawFlags & DF_DRAW_ENTITITES ) || !r_deferred_gbuffer_translucent_models.GetBool() )
		return;

	CClientRenderablesList::CEntry *pEntities = m_pRenderablesList->m_RenderGroups[ RENDER_GROUP_TRANSLUCENT_ENTITY ];
	const int nEntities = m_pRenderablesList->m_RenderGroupCounts[ RENDER_GROUP_TRANSLUCENT_ENTITY ];
	for ( int i = 0; i < nEntities; i++ )
	{
		IClientRenderable *pRenderable = pEntities[i].m_pRenderable;
		// (two-pass ones are in the opaque list already)
		if ( !pRenderable || pEntities[i].m_TwoPass )
			continue;
		const model_t *pModel = pRenderable->GetModel();
		if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
			continue;
		// (see-through as a whole: fading, render alpha)
		if ( pRenderable->GetFxBlend() < 255 )
			continue;

		float color[3];
		pRenderable->GetColorModulation( color );
		render->SetColorModulation( color );
		render->SetBlend( 1.0f );
		pRenderable->DrawModel( STUDIO_RENDER | STUDIO_TWOPASS );
	}
}

// HL2RPM: the detail sprites (grass tufts) are drawn with the translucents, after the
// G-buffer: lit by the light buffer of whatever was behind them, or with their stock
// material by a color baked per sprite - grass in the shade of a wall was as bright as in the
// sun. With a deferred material (materialsystem_passthru.cpp: alpha-tested DEFERRED_BRUSH)
// they are drawn into the G-buffer here as well (up normal), and the composite of the
// translucent pass lights them with their own depth and normal.
void CBaseWorldViewDeferred::DrawDetailSpritesToGBuffer()
{
	static ConVarRef r_DrawDetailProps( "r_DrawDetailProps" );
	if ( !m_pWorldListInfo || !( m_DrawFlags & DF_DRAW_ENTITITES ) || ( r_DrawDetailProps.IsValid() && !r_DrawDetailProps.GetBool() ) )
		return;

	IMaterial *pMat = DetailObjectSystem()->GetDetailSpriteMaterial();
	const char *pszShader = pMat ? pMat->GetShaderName() : NULL;
	if ( !pszShader || V_strnicmp( pszShader, "DEFERRED_", 9 ) != 0 )
		return;

	DrawDetailPropsInAllLeaves();
}

void CBaseWorldViewDeferred::DrawOpaqueRenderablesDeferred(bool k)
{
	if ( r_deferred_debug_lists.GetBool() && m_pRenderablesList && CurrentViewID() != VIEW_DEFERRED_SHADOW )
		DebugPrintRenderablesList( m_pRenderablesList, ( CurrentViewID() == VIEW_MAIN ) ? "main" : "other" );
	DrawOpaqueRenderables(k ? DEPTH_MODE_SHADOW : DEPTH_MODE_NORMAL);
}

void CBaseWorldViewDeferred::PushComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_COMPOSITION );
}

void CBaseWorldViewDeferred::PopComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );
}

//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
void CSimpleWorldViewDeferred::Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox,
	const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t &waterInfo, ViewCustomVisibility_t *pCustomVisibility )
{
	BaseClass::Setup( view );

	m_ClearFlags = nClearFlags;
	m_DrawFlags = DF_DRAW_ENTITITES;

	if ( !waterInfo.m_bOpaqueWater )
	{
		m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	}
	else
	{
		const bool bViewIntersectsWater = DoesViewPlaneIntersectWater( fogInfo.m_flWaterHeight, fogInfo.m_nVisibleFogVolume );
		if( bViewIntersectsWater )
		{
			// have to draw both sides if we can see both.
			m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
		}
		else if ( fogInfo.m_bEyeInFogVolume )
		{
			m_DrawFlags |= DF_RENDER_UNDERWATER;
		}
		else
		{
			m_DrawFlags |= DF_RENDER_ABOVEWATER;
		}
	}
	if ( waterInfo.m_bDrawWaterSurface )
	{
		m_DrawFlags |= DF_RENDER_WATER;
	}

	if ( !fogInfo.m_bEyeInFogVolume && bDrawSkybox )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	m_pCustomVisibility = pCustomVisibility;
	m_fogInfo = fogInfo;
}

void CSimpleWorldViewDeferred::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldView::Draw" );

	pRenderContext.SafeRelease();

	PushComposite();

	DrawSetup( 0, m_DrawFlags, 0 );

	if ( !m_fogInfo.m_bEyeInFogVolume )
	{
		EnableWorldFog();
	}
	else
	{
		m_ClearFlags |= VIEW_CLEAR_COLOR;

		SetFogVolumeState( m_fogInfo, false );

		pRenderContext.GetFrom( materials );

		unsigned char ucFogColor[3];
		pRenderContext->GetFogColor( ucFogColor );
		pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
	}

	pRenderContext.SafeRelease();

	DrawExecute( 0, CurrentViewID(), 0 );

	PopComposite();

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );
}

//-----------------------------------------------------------------------------
// HL2RPM: reflective glass (see the class)
//-----------------------------------------------------------------------------
void CGlassViewDeferred::Setup( const CViewSetup &view, bool bDrawSkybox, const cplane_t &plane, ITexture *pRenderTarget )
{
	BaseClass::Setup( view );

	m_ClearFlags = VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR;
	m_DrawFlags = DF_DRAW_ENTITITES | DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	if ( bDrawSkybox )
		m_DrawFlags |= DF_DRAWSKYBOX;

	m_Plane = plane;
	m_pRenderTarget = pRenderTarget;
}

bool CGlassViewDeferred::AdjustView( float waterHeight )
{
	// the aspect ratio of the main view is kept
	x = y = 0;
	width = m_pRenderTarget->GetActualWidth();
	height = m_pRenderTarget->GetActualHeight();

	if ( m_bReflection )
	{
		// the camera mirrored in the glass plane
		const float flDist = DotProduct( origin, m_Plane.normal ) - m_Plane.dist;
		VectorMA( origin, -2.0f * flDist, m_Plane.normal, origin );

		Vector vecForward, vecUp;
		AngleVectors( angles, &vecForward, NULL, &vecUp );

		float flDot = DotProduct( vecForward, m_Plane.normal );
		VectorMA( vecForward, -2.0f * flDot, m_Plane.normal, vecForward );

		flDot = DotProduct( vecUp, m_Plane.normal );
		VectorMA( vecUp, -2.0f * flDot, m_Plane.normal, vecUp );

		VectorAngles( vecForward, vecUp, angles );
	}
	return true;
}

void CGlassViewDeferred::PushView( float waterHeight )
{
	render->Push3DView( *this, m_ClearFlags, m_pRenderTarget, GetFrustum() );

	// a reflection shows what is in front of the glass, a refraction what is behind it
	Vector4D plane;
	if ( m_bReflection )
	{
		VectorCopy( m_Plane.normal, plane.AsVector3D() );
		plane.w = m_Plane.dist + 0.1f;
	}
	else
	{
		VectorMultiply( m_Plane.normal, -1, plane.AsVector3D() );
		plane.w = -m_Plane.dist + 0.1f;
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushCustomClipPlane( plane.Base() );
}

void CGlassViewDeferred::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopCustomClipPlane();
	render->PopView( GetFrustum() );
}

void CGlassViewDeferred::Draw()
{
	VPROF( "CGlassViewDeferred::Draw" );

	const view_id_t nSaveViewID = CurrentViewID();
	const Vector vecSaveOrigin = CurrentViewOrigin();
	const QAngle angSaveAngles = CurrentViewAngles();
	const view_id_t nViewID = m_bReflection ? VIEW_REFLECTION : VIEW_REFRACTION;
	SetupCurrentView( origin, angles, nViewID );

	// no occlusion visualization in the reflection
	static ConVarRef r_visocclusion( "r_visocclusion" );
	const int iVisOcclusion = r_visocclusion.IsValid() ? r_visocclusion.GetInt() : 0;
	if ( r_visocclusion.IsValid() )
		r_visocclusion.SetValue( 0 );

	PushComposite();
	DrawSetup( 0.0f, m_DrawFlags, 0.0f );

	// as the water reflection: the lighting comes from the main view's light buffer, the
	// screen-space clouds belong to the main view (a refraction sees what the main view
	// sees behind the glass - the same pixels)
	const bool bCloudsValid = m_bReflection && WeatherRender_IsCloudTextureValid();
	if ( bCloudsValid )
		WeatherRender_SetCloudTextureValid( false );
	if ( m_bReflection )
	{
		const bool bReflectionOn = true;
		QUEUE_FIRE( CommitReflectionView, bReflectionOn );
	}

	EnableWorldFog();
	DrawExecute( 0.0f, nViewID, 0.0f );

	if ( m_bReflection )
	{
		const bool bReflectionOff = false;
		QUEUE_FIRE( CommitReflectionView, bReflectionOff );
	}
	if ( bCloudsValid )
		WeatherRender_SetCloudTextureValid( true );

	PopComposite();

	if ( r_visocclusion.IsValid() )
		r_visocclusion.SetValue( iVisOcclusion );

	SetupCurrentView( vecSaveOrigin, angSaveAngles, nSaveViewID );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );
	pRenderContext->Flush();
}

void CGBufferView::Setup( const CViewSetup &view, bool bDrewSkybox )
{
	m_fogInfo.m_bEyeInFogVolume = false;
	m_bDrewSkybox = bDrewSkybox;

	BaseClass::Setup( view );

	m_ClearFlags = 0;
	m_DrawFlags = DF_DRAW_ENTITITES;

	m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;

#if DEFCFG_DEFERRED_SHADING
	if ( !bDrewSkybox )
		m_DrawFlags |= DF_DRAWSKYBOX;
#endif
}

void CGBufferView::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif

	SetupCurrentView( origin, angles, VIEW_MAIN );

	DrawSetup( 0, m_DrawFlags, 0 );

	const bool bOptimizedGbuffer = DEFCFG_DEFERRED_SHADING == 0;
	DrawExecute( 0, CurrentViewID(), 0, bOptimizedGbuffer );

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CGBufferView::PushView( float waterHeight )
{
	PushGBuffer( !m_bDrewSkybox );
}

void CGBufferView::PopView()
{
	PopGBuffer();
}

void CGBufferView::PushGBuffer( bool bInitial, float zScale, bool bClearDepth )
{
	ITexture *pNormals = GetDefRT_Normals();
	ITexture *pDepth = GetDefRT_Depth();

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );

	if ( bInitial )
	{
		pRenderContext->PushRenderTargetAndViewport( pDepth );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

#if DEFCFG_DEFERRED_SHADING == 1
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Albedo() );
#else
	pRenderContext->PushRenderTargetAndViewport( pNormals );
#endif

	if ( bClearDepth )
		pRenderContext->ClearBuffers( false, true );

	pRenderContext->SetRenderTargetEx( 1, pDepth );

#if DEFCFG_DEFERRED_SHADING == 1
	pRenderContext->SetRenderTargetEx( 2, pNormals );
	pRenderContext->SetRenderTargetEx( 3, GetDefRT_Specular() );
#elif !DEFCFG_LIGHTCTRL_PACKING
	pRenderContext->SetRenderTargetEx( 2, GetDefRT_LightCtrl() );
#endif

	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_GBUFFER );

	struct defData_setZScale
	{
	public:
		float zScale;

		static void Fire( defData_setZScale d )
		{
			GetDeferredExt()->CommitZScale( d.zScale );
		};
	};

	defData_setZScale data;
	data.zScale = zScale;
	#pragma warning(suppress: 4456)
	QUEUE_FIRE( CommitZScale, zScale );
}

void CGBufferView::PopGBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	pRenderContext->PopRenderTargetAndViewport();
}

SkyboxVisibility_t CSkyboxViewDeferred::ComputeSkyboxVisibility()
{
	if ( ( enginetrace->GetPointContents( origin ) & CONTENTS_SOLID ) != 0 )
		return SKYBOX_NOT_VISIBLE;

	return engine->IsSkyboxVisibleFromPoint( origin );
}

bool CSkyboxViewDeferred::GetSkyboxFogEnable()
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return false;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	static ConVarRef fog_enableskybox( "fog_enableskybox" );
	if( fog_override.IsValid() && fog_override.GetInt() )
	{
		return fog_enableskybox.IsValid() && fog_enableskybox.GetBool();
	}
	else
	{
		return !!local->m_skybox3d.fog.enable;
	}
}

void CSkyboxViewDeferred::Enable3dSkyboxFog( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData *local = &pbp->m_Local;

	CMatRenderContextPtr pRenderContext( materials );

	if( GetSkyboxFogEnable() )
	{
		float fogColor[3];
		GetSkyboxFogColor( fogColor );
		// HL2RPM: the distant skybox city fades into the current weather / time of day,
		// not into the map's fixed daylight fog color (glowing silhouettes at night)
		WeatherRender_GetSkyboxFogColor( fogColor );
		float scale = 1.0f;
		if ( local->m_skybox3d.scale > 0.0f )
		{
			scale = 1.0f / local->m_skybox3d.scale;
		}
		pRenderContext->FogMode( MATERIAL_FOG_LINEAR );
		pRenderContext->FogColor3fv( fogColor );
		pRenderContext->FogStart( GetSkyboxFogStart() * scale );
		pRenderContext->FogEnd( GetSkyboxFogEnd() * scale );
		pRenderContext->FogMaxDensity( GetSkyboxFogMaxDensity() );
	}
	else
	{
		pRenderContext->FogMode( MATERIAL_FOG_NONE );
	}
}

sky3dparams_t *CSkyboxViewDeferred::PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible )
{
	static ConVarRef r_3dsky( "r_3dsky" );
	if ( ( nSkyboxVisible != SKYBOX_3DSKYBOX_VISIBLE ) && ( !r_3dsky.IsValid() || r_3dsky.GetInt() != 2 ) )
		return NULL;

	// render the 3D skybox
	if ( !r_3dsky.IsValid() || !r_3dsky.GetInt() )
		return NULL;

	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();

	// No local player object yet...
	if ( !pbp )
		return NULL;

	CPlayerLocalData* local = &pbp->m_Local;
	if ( local->m_skybox3d.area == 255 )
		return NULL;

	return &local->m_skybox3d;
}

void CSkyboxViewDeferred::DrawInternal( view_id_t iSkyBoxViewID, ITexture *pRenderTarget, ITexture *pDepthTarget )
{
	const bool bInvokePreAndPostRender = !m_bGBufferPass;

	if ( m_bGBufferPass )
	{
#if DEFCFG_DEFERRED_SHADING
		m_DrawFlags |= DF_DRAWSKYBOX;
#endif
	}

	unsigned char **areabits = render->GetAreaBits();
	unsigned char *savebits;
	unsigned char tmpbits[ 32 ];
	savebits = *areabits;
	memset( tmpbits, 0, sizeof(tmpbits) );

	// set the sky area bit
	tmpbits[m_pSky3dParams->area>>3] |= 1 << (m_pSky3dParams->area&7);

	*areabits = tmpbits;

	// if you can get really close to the skybox geometry it's possible that you'll be able to clip into it
	// with this near plane.  If so, move it in a bit.  It's at 2.0 to give us more precision.  That means you
	// need to keep the eye position at least 2 * scale away from the geometry in the skybox
	zNear = 2.0;
	zFar = 10000.0f; //MAX_TRACE_LENGTH;

	float skyScale = 1.0f;
	// scale origin by sky scale and translate to sky origin
	{
		skyScale = (m_pSky3dParams->scale > 0) ? m_pSky3dParams->scale : 1.0f;
		const float scale = 1.0f / skyScale;

		const Vector& vSkyOrigin = m_pSky3dParams->origin;
		VectorScale( origin, scale, origin );
		VectorAdd( origin, vSkyOrigin, origin );
	}

	if ( !m_bGBufferPass )
		Enable3dSkyboxFog();

	// BUGBUG: Fix this!!!  We shouldn't need to call setup vis for the sky if we're connecting
	// the areas.  We'd have to mark all the clusters in the skybox area in the PVS of any
	// cluster with sky.  Then we could just connect the areas to do our vis.
	//m_bOverrideVisOrigin could hose us here, so call direct
	render->ViewSetupVis( false, 1, &m_pSky3dParams->origin.Get() );
	render->Push3DView( (*this), m_ClearFlags, pRenderTarget, GetFrustum(), pDepthTarget );

	if ( m_bGBufferPass )
		PushGBuffer( true, skyScale );
	else
		PushComposite();

	// Store off view origin and angles
	SetupCurrentView( origin, angles, iSkyBoxViewID );

#if defined( _X360 )
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushVertexShaderGPRAllocation( 32 );
	pRenderContext.SafeRelease();
#endif

	// Invoke pre-render methods
	if ( bInvokePreAndPostRender )
	{
		IGameSystem::PreRenderAllSystems();
	}

	BuildWorldRenderLists( true, -1, ShouldCacheLists() );

	BuildRenderableRenderLists( m_bGBufferPass ? VIEW_SHADOW_DEPTH_TEXTURE : iSkyBoxViewID );

	DrawWorldDeferred( 0.0f );

	// Iterate over all leaves and render objects in those leaves
	DrawOpaqueRenderablesDeferred( false );

	if ( !m_bGBufferPass )
	{
		// Iterate over all leaves and render objects in those leaves
		DrawTranslucentRenderables( true, false );
		DrawNoZBufferTranslucentRenderables();
	}

	if ( !m_bGBufferPass )
	{
		m_pMainView->DisableFog();

		CGlowOverlay::UpdateSkyOverlays( zFar, m_bCacheFullSceneState );

		PixelVisibility_EndCurrentView();
	}

	// restore old area bits
	*areabits = savebits;

	// Invoke post-render methods
	if( bInvokePreAndPostRender )
	{
		IGameSystem::PostRenderAllSystems();
		FinishCurrentView();
	}

	if ( m_bGBufferPass )
		PopGBuffer();
	else
		PopComposite();

	render->PopView( GetFrustum() );

#if defined( _X360 )
	pRenderContext.GetFrom( materials );
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

bool CSkyboxViewDeferred::Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible )
{
	BaseClass::Setup( view );

	// The skybox might not be visible from here
	*pSkyboxVisible = ComputeSkyboxVisibility();
	m_pSky3dParams = PreRender3dSkyboxWorld( *pSkyboxVisible );

	if ( !m_pSky3dParams )
	{
		return false;
	}

	m_bGBufferPass = bGBuffer;
	// At this point, we've cleared everything we need to clear
	// The next path will need to clear depth, though.
	m_ClearFlags = VIEW_CLEAR_DEPTH; //*pClearFlags;
	//*pClearFlags &= ~( VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH | VIEW_CLEAR_STENCIL | VIEW_CLEAR_FULL_TARGET );
	//*pClearFlags |= VIEW_CLEAR_DEPTH; // Need to clear depth after rednering the skybox

	m_DrawFlags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_RENDER_WATER;
	static ConVarRef r_skybox( "r_skybox" );
	if( !m_bGBufferPass && r_skybox.IsValid() && r_skybox.GetBool() )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	return true;
}

void CSkyboxViewDeferred::Draw()
{
	VPROF_BUDGET( "CViewRender::Draw3dSkyboxworld", "3D Skybox" );

	ITexture *pRTColor = NULL;
	ITexture *pRTDepth = NULL;
	if( m_eStereoEye != STEREO_EYE_MONO )
	{
		pRTColor = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(m_eStereoEye-1), ISourceVirtualReality::RT_Color );
		pRTDepth = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(m_eStereoEye-1), ISourceVirtualReality::RT_Depth );
	}

	DrawInternal(VIEW_3DSKY, pRTColor, pRTDepth);
}

void CPostLightingView::Setup( const CViewSetup &view )
{
	m_fogInfo.m_bEyeInFogVolume = false;

	BaseClass::Setup( view );

	m_ClearFlags = 0;

	m_DrawFlags = DF_DRAW_ENTITITES;
	m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
}

void CPostLightingView::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif

	ITexture *pTexAlbedo = GetDefRT_Albedo();
	pRenderContext->CopyRenderTargetToTexture( pTexAlbedo );
	pRenderContext->PushRenderTargetAndViewport( pTexAlbedo );
	pRenderContext.SafeRelease();
	PushComposite();

	SetupCurrentView( origin, angles, VIEW_MAIN );

	DrawSetup( 0, m_DrawFlags, 0 );

	DrawExecute( 0, CurrentViewID(), 0, false );

	PopComposite();

	pRenderContext.GetFrom( materials );
	pRenderContext->PopRenderTargetAndViewport();

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CPostLightingView::PushView( float waterHeight )
{
	//PushGBuffer( !m_bDrewSkybox );
	BaseClass::PushView( waterHeight );
}

void CPostLightingView::PopView()
{
	BaseClass::PopView();
}

void CPostLightingView::DrawWorldDeferred( float waterZAdjust )
{
#if 0
	int iOldDrawFlags = m_DrawFlags;

	m_DrawFlags &= ~DF_DRAW_ENTITITES;
	m_DrawFlags &= ~DF_RENDER_UNDERWATER;
	m_DrawFlags &= ~DF_RENDER_ABOVEWATER;
	m_DrawFlags &= ~DF_DRAW_ENTITITES;

	BaseClass::DrawWorldDeferred( waterZAdjust );

	m_DrawFlags = iOldDrawFlags;
#endif
}

void CPostLightingView::DrawOpaqueRenderablesDeferred(bool)
{
}

void CPostLightingView::PushDeferredShadingFrameBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Albedo() );
}

void CPostLightingView::PopDeferredShadingFrameBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopRenderTargetAndViewport();
}

void CBaseShadowView::Setup( const CViewSetup &view, ITexture *pDepthTexture, ITexture *pDummyTexture )
{
	m_pDepthTexture = pDepthTexture;
	m_pDummyTexture = pDummyTexture;

	BaseClass::Setup( view );

	m_DrawFlags = DF_DRAW_ENTITITES | DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	m_ClearFlags = 0;

	CalcShadowView();

	m_pCustomVisibility = &shadowVis;
	shadowVis.AddVisOrigin( origin );
}

void CBaseShadowView::SetupRadiosityTargets( ITexture *pAlbedoTexture, ITexture *pNormalTexture )
{
	m_pRadAlbedoTexture = pAlbedoTexture;
	m_pRadNormalTexture = pNormalTexture;
}

void CBaseShadowView::Draw()
{
	const view_id_t oldViewID = CurrentViewID();
	const Vector oldOrigin = CurrentViewOrigin();
	const QAngle oldAngles = CurrentViewAngles();
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );

	// Set the shadow exclude entity index for this shadow pass
	g_iDeferredShadowExcludeEntIndex = m_iShadowExcludeEntIndex;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_SHADOWPASS );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_MODE,
		GetShadowMode() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DEFERRED_SHADOW_RADIOSITY,
		m_bOutputRadiosity ? 1.0f : 0.0f );
	pRenderContext.SafeRelease();

	DrawSetup( 0, m_DrawFlags, 0, -1, true );

	DrawExecute( 0, CurrentViewID(), 0, true );

	pRenderContext.GetFrom( materials );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DEFERRED_SHADOW_RADIOSITY, 0.0f );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	// Clear the shadow exclude entity index
	g_iDeferredShadowExcludeEntIndex = -1;

	SetupCurrentView( oldOrigin, oldAngles, oldViewID );
}

bool CBaseShadowView::AdjustView( float waterHeight )
{
	CommitData();

	return true;
}

void CBaseShadowView::PushView( float waterHeight )
{
	render->Push3DView( *this, 0, m_pDummyTexture, GetFrustum(), m_pDepthTexture );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( m_pDummyTexture, m_pDepthTexture, x, y, width, height );

#if defined( DEBUG ) || defined( SHADOWMAPPING_USE_COLOR )
	pRenderContext->ClearColor4ub( 255, 255, 255, 255 );
	pRenderContext->ClearBuffers( true, true );
#else
	// HL2RPM: the sun atlas keeps the caster depth as color (PCSS blocker search):
	// clear it to "far" like the depth, or empty areas keep stale casters
	if ( m_pDummyTexture && m_pDummyTexture->GetImageFormat() == IMAGE_FORMAT_R32F )
	{
		pRenderContext->ClearColor4ub( 255, 255, 255, 255 );
		pRenderContext->ClearBuffers( true, true );
	}
	else
		pRenderContext->ClearBuffers( false, true );
#endif

	if ( m_bOutputRadiosity )
	{
		Assert( !IsErrorTexture( m_pRadAlbedoTexture ) );
		Assert( !IsErrorTexture( m_pRadNormalTexture ) );

		pRenderContext->SetRenderTargetEx( 1, m_pRadAlbedoTexture );
		pRenderContext->SetRenderTargetEx( 2, m_pRadNormalTexture );
	}
}

void CBaseShadowView::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopRenderTargetAndViewport();

	render->PopView( GetFrustum() );
}

void CBaseShadowView::SetRadiosityOutputEnabled( bool bEnabled )
{
	m_bOutputRadiosity = bEnabled;
}

static lightData_Global_t& GetActiveGlobalLightState()
{
	static lightData_Global_t data;
	CLightingEditor *pEditor = GetLightingEditor();

	if ( pEditor->IsEditorLightingActive() && pEditor->GetKVGlobalLight() != NULL )
	{
		data = pEditor->GetGlobalState();
	}
	else if ( GetGlobalLight() != NULL )
	{
		data = GetGlobalLight()->GetState();
	}

	return data;
}

void COrthoShadowView::CalcShadowView()
{
	const cascade_t &m_data = GetCascadeInfo( iCascadeIndex );

	// HL2RPM: at this point 'this' still holds the main camera (copied in Setup).
	// Fit the cascade to the bounding sphere of its slice of the view frustum
	// instead of centering every cascade on the player's feet: the old way
	// wasted half of each shadow map on what's behind the camera.
	const Vector vecViewOrigin = origin;
	Vector vecViewFwd;
	AngleVectors( angles, &vecViewFwd );

	float flSphereCenter, flSphereRadius;
	{
		const float flTanX = tanf( DEG2RAD( clamp( fov, 1.0f, 170.0f ) * 0.5f ) );
		const float flTanY = flTanX / Max( m_flAspectRatio, 0.01f );
		const float k2 = flTanX * flTanX + flTanY * flTanY;
		const float n = m_data.flSplitNear;
		const float f = Max( m_data.flSplitFar, n + 1.0f );

		// sphere through the near and far corners of the slice, centered on the view axis
		flSphereCenter = 0.5f * ( n + f ) * ( 1.0f + k2 );
		if ( flSphereCenter >= f )
		{
			flSphereCenter = f;
			flSphereRadius = Max( f * sqrtf( k2 ), sqrtf( ( f - n ) * ( f - n ) + n * n * k2 ) );
		}
		else
		{
			flSphereRadius = sqrtf( ( f - flSphereCenter ) * ( f - flSphereCenter ) + f * f * k2 );
		}

		// Quantize so tiny fov/aspect jitter doesn't change the texel size,
		// and pad for the shadow filter kernel.
		flSphereRadius = ceilf( flSphereRadius / 16.0f ) * 16.0f;
		flSphereRadius *= (float)m_data.iResolution / (float)Max( m_data.iResolution - 8, 64 );
	}

	origin = vecViewOrigin + vecViewFwd * flSphereCenter;
	if ( r_csm_debug_jitter.GetFloat() > 0.0f )
	{
		const float flAmp = r_csm_debug_jitter.GetFloat();
		const int iFrame = gpGlobals->framecount;
		origin += Vector( sinf( iFrame * 12.9898f ) , cosf( iFrame * 78.233f ), sinf( iFrame * 37.719f ) * 0.5f ) * flAmp;
	}

	const lightData_Global_t& state = GetActiveGlobalLightState();
	QAngle lightAng;
	VectorAngles( -state.vecLight.AsVector3D(), lightAng );

	// Smooth the light direction to prevent shadow jitter at high time speeds
	static QAngle s_smoothedLightAng( 0, 0, 0 );
	static bool s_bLightAngInit = false;
	static int s_iLightAngFrame = -1;
	if ( !s_bLightAngInit )
	{
		s_smoothedLightAng = lightAng;
		s_bLightAngInit = true;
		s_iLightAngFrame = gpGlobals->framecount;
	}
	else if ( s_iLightAngFrame != gpGlobals->framecount ) // once per frame, not once per cascade
	{
		s_iLightAngFrame = gpGlobals->framecount;
		float dt = gpGlobals->frametime;
		float rate = 1.0f - expf( -dt * 15.0f );
		for ( int i = 0; i < 3; i++ )
		{
			float delta = AngleNormalize( lightAng[i] - s_smoothedLightAng[i] );
			s_smoothedLightAng[i] = AngleNormalize( s_smoothedLightAng[i] + delta * rate );
		}
	}
	lightAng = s_smoothedLightAng;

	Vector viewFwd, viewRight, viewUp;
	AngleVectors( lightAng, &viewFwd, &viewRight, &viewUp );

	const float halfOrthoSize = flSphereRadius;

	// HL2RPM: the light camera sits at the cascade's center, the casters toward the sun are in
	// front of a negative near plane. It used to be moved 12000 units toward the sun: every
	// tiny turn of the sun (the time of day) swung the texel grid around that far pivot by about
	// half a texel per frame, and float math with 12000 unit components made the snapped grid
	// jitter by ~0.001 unit whenever the cascade moved - with the player. Edges of small
	// casters (foliage) flipped texels: sparkles over the shadowed ground while walking.
	const bool bCenter = r_csm_center_origin.GetBool();
	if ( !bCenter )
		origin += -viewFwd * m_data.flOriginOffset;

	angles = lightAng;

	x = 0;
	y = 0;
	height = m_data.iResolution;
	width = m_data.iResolution;

	m_bOrtho = true;
	m_OrthoLeft = -halfOrthoSize;
	m_OrthoTop = -halfOrthoSize;
	m_OrthoRight = halfOrthoSize;
	m_OrthoBottom = halfOrthoSize;

	if ( bCenter )
	{
		zNear = zNearViewmodel = -m_data.flOriginOffset;
		zFar = zFarViewmodel = flSphereRadius + 1024.0f;
	}
	else
	{
		zNear = zNearViewmodel = 0;
		zFar = zFarViewmodel = m_data.flOriginOffset + flSphereRadius + 1024.0f;
	}
	m_flAspectRatio = 1.0f;

	// Snap to the shadow texel grid in light space so the shadow doesn't shimmer
	// when the camera moves (texels always land on the same world positions).
	// HL2RPM: in double precision - the light space coordinates are exact multiples of the texel
	// (and of the depth step), the same bits every frame the cascade stays within a texel
	const float mapping_world = ( 2.0f * halfOrthoSize ) / m_data.iResolution;
	{
		const double flTexel = mapping_world;
		const double flDepthStep = GetDepthMapDepthResolution( zFar - zNear );
		const double ox = origin.x, oy = origin.y, oz = origin.z;
		double r = (double)viewRight.x * ox + (double)viewRight.y * oy + (double)viewRight.z * oz;
		double u = (double)viewUp.x * ox + (double)viewUp.y * oy + (double)viewUp.z * oz;
		double f = (double)viewFwd.x * ox + (double)viewFwd.y * oy + (double)viewFwd.z * oz;
		r = floor( r / flTexel ) * flTexel;
		u = floor( u / flTexel ) * flTexel;
		if ( flDepthStep > 0.0 )
			f = floor( f / flDepthStep ) * flDepthStep;
		origin.x = (float)( r * viewRight.x + u * viewUp.x + f * viewFwd.x );
		origin.y = (float)( r * viewRight.y + u * viewUp.y + f * viewFwd.y );
		origin.z = (float)( r * viewRight.z + u * viewUp.z + f * viewFwd.z );
	}

	if ( r_csm_debug_log.GetBool() )
		Msg( "[csm] f%d c%d fov %.3f R %.3f texel %.5f org %.3f %.3f %.3f light %.4f %.4f %.4f\n", gpGlobals->framecount, iCascadeIndex, fov, halfOrthoSize, mapping_world,
			DotProduct( viewRight, origin ), DotProduct( viewUp, origin ), DotProduct( viewFwd, origin ), viewFwd.x, viewFwd.y, viewFwd.z );

#if CSM_USE_COMPOSITED_TARGET
	x = m_data.iViewport_x;
	y = m_data.iViewport_y;
#endif
}

void COrthoShadowView::CommitData()
{
	struct sendShadowDataOrtho
	{
		shadowData_ortho_t data;
		int index;
		static void Fire( sendShadowDataOrtho d )
		{
			GetDeferredExt()->CommitShadowData_Ortho( d.index, d.data );
		};
	};

	Vector fwd, right, down;
	AngleVectors( angles, &fwd, &right, &down );
	down *= -1.0f;

	shadowData_ortho_t shadowData;

#if CSM_USE_COMPOSITED_TARGET
	shadowData.iRes_x = CSM_COMP_RES_X;
	shadowData.iRes_y = CSM_COMP_RES_Y;
#else
	shadowData.iRes_x = width;
	shadowData.iRes_y = height;
#endif

	// Compute adaptive per-cascade bias parameters from texel size & depth range.
	// These replace the old hand-tuned flSlopeScaleMin / flSlopeScaleMax / flNormalScaleMax.
	const float texelWorldSize = ( m_OrthoRight - m_OrthoLeft ) / (float)Max( width, 1 );	// HL2RPM: real fitted size
	const float oneTexelDepth  = texelWorldSize / Max( zFar - zNear, 1.0f );   // one-texel depth in [0,1] range (the near plane can be negative)

	shadowData.vecSlopeSettings.Init(
		oneTexelDepth * 0.5f,        // .x = constant depth bias (half texel depth)
		oneTexelDepth * 3.0f,        // .y = slope bias factor  (3 texels per tan unit)
		texelWorldSize * 0.5f,       // .z = normal offset (half texel in world units)
		1.0f / Max( zFar - zNear, 1.0f )	// .w = projection depth (used by VS)
		);
	shadowData.vecOrigin.Init( origin, 1.0f );

	// HL2RPM: the caster bias comes from the rasterizer (the shadow pass writes no depth for
	// the sun cascades, which keeps early-z): a little slope (r_csm_slope_bias) and a part of
	// a texel (r_csm_const_bias) - at least a step and a half of the depth map (16 bit ones
	// are that coarse)
	{
		const float flDepthStep = GetDepthMapDepthResolution( zFar - zNear ) / Max( zFar - zNear, 1.0f );
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->SetShadowDepthBiasFactors( r_csm_slope_bias.GetFloat(), Max( oneTexelDepth * r_csm_const_bias.GetFloat(), flDepthStep * 1.5f ) );
	}

	Vector4D matrix_scale_offset( 0.5f, -0.5f, 0.5f, 0.5f );

#if CSM_USE_COMPOSITED_TARGET
	shadowData.vecUVTransform.Init( x / (float)CSM_COMP_RES_X,
		y / (float)CSM_COMP_RES_Y,
		width / (float) CSM_COMP_RES_X,
		height / (float) CSM_COMP_RES_Y );
#endif

	VMatrix a,b,c,d,screenToTexture;
	render->GetMatricesForView( *this, &a, &b, &c, &d );
	MatrixBuildScale( screenToTexture, matrix_scale_offset.x,
		matrix_scale_offset.y,
		1.0f );

	screenToTexture[0][3] = matrix_scale_offset.z;
	screenToTexture[1][3] = matrix_scale_offset.w;

	MatrixMultiply( screenToTexture, c, shadowData.matWorldToTexture );

	QUEUE_FIRE( CommitShadowData_Ortho, iCascadeIndex, shadowData );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, iCascadeIndex );
}

bool CDualParaboloidShadowView::AdjustView( float waterHeight )
{
	BaseClass::AdjustView( waterHeight );

	// HACK: when pushing our actual view the renderer fails building the worldlist right!
	// So we can't. Shit.
	return false;
}

void CDualParaboloidShadowView::PushView( float waterHeight )
{
	BaseClass::PushView( waterHeight );
}

void CDualParaboloidShadowView::PopView()
{
	BaseClass::PopView();
}

void CDualParaboloidShadowView::CalcShadowView()
{
	float flRadius = m_pLight->flRadius;

	m_bOrtho = true;
	m_OrthoTop = m_OrthoLeft = -flRadius;
	m_OrthoBottom = m_OrthoRight = flRadius;

	const int dpsmRes = GetShadowResolution_Point();

	width = dpsmRes;
	height = dpsmRes;

	zNear = zNearViewmodel = DEFLIGHT_SPOT_ZNEAR;
	zFar = zFarViewmodel = Max( flRadius, DEFLIGHT_SPOT_ZNEAR + 1.0f );

	if ( m_bSecondary )
	{
		y = dpsmRes;

		Vector fwd, up;
		AngleVectors( angles, &fwd, NULL, &up );
		VectorAngles( -fwd, up, angles );
	}
}

void CPointLightCubeFaceShadowView::CalcShadowView()
{
	static const QAngle s_faceAngles[6] = {
		QAngle( 0, 0, 0 ),       // Face 0: +X
		QAngle( 0, 180, 0 ),     // Face 1: -X
		QAngle( 0, 90, 0 ),      // Face 2: +Y
		QAngle( 0, -90, 0 ),     // Face 3: -Y
		QAngle( -90, 0, 0 ),     // Face 4: +Z
		QAngle( 90, 0, 0 ),      // Face 5: -Z
	};

	const float flRadius = m_pLight->flRadius;
	const int faceRes = GetShadowResolution_Point();

	angles = s_faceAngles[m_iFaceIndex];

	const int col = m_iFaceIndex % 3;
	const int row = m_iFaceIndex / 3;
	x = col * faceRes;
	y = row * faceRes;
	width = faceRes;
	height = faceRes;

	m_bOrtho = false;
	m_flAspectRatio = 1.0f;
	fov = fovViewmodel = 92.0f; // 1 degree guard band per edge for PCF at cube face boundaries
	zNear = zNearViewmodel = DEFLIGHT_SPOT_ZNEAR;
	zFar = zFarViewmodel = Max( flRadius, DEFLIGHT_SPOT_ZNEAR + 1.0f );
}

void CPointLightCubeFaceShadowView::CommitData()
{
	struct sendShadowDataProj
	{
		shadowData_proj_t data;
		int index;
		static void Fire( sendShadowDataProj d )
		{
			GetDeferredExt()->CommitShadowData_Proj( d.index, d.data );
		};
	};

	Vector fwd;
	AngleVectors( angles, &fwd );

	shadowData_proj_t data;
	data.vecForward.Init( fwd, 0.0f );
	data.vecOrigin.Init( origin, 1.0f );
	data.vecSlopeSettings.Init( 0.0f, 0.0f,
		DEFLIGHT_SPOT_ZNEAR,
		Max( m_pLight->flRadius, DEFLIGHT_SPOT_ZNEAR + 1.0f ) );

	QUEUE_FIRE( CommitShadowData_Proj, m_iShadowMapIndex, data );

	SetProjectedShadowDepthBias();

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, m_iShadowMapIndex );
}

void CSpotLightShadowView::CalcShadowView()
{
	float flRadius = m_pLight->flRadius;

	int spotRes = GetShadowResolution_Spot();

	width = spotRes;
	height = spotRes;

	zNear = zNearViewmodel = DEFLIGHT_SPOT_ZNEAR;
	zFar = zFarViewmodel = Max( flRadius, DEFLIGHT_SPOT_ZNEAR + 1.0f );

	fov = fovViewmodel = m_pLight->GetFOV();
}

void CSpotLightShadowView::CommitData()
{
	struct sendShadowDataProj
	{
		shadowData_proj_t data;
		int index;
		static void Fire( sendShadowDataProj d )
		{
			GetDeferredExt()->CommitShadowData_Proj( d.index, d.data );
		};
	};

	Vector fwd;
	AngleVectors( angles, &fwd );

	shadowData_proj_t data;
	data.vecForward.Init( fwd, 0.0f );
	data.vecOrigin.Init( origin, 1.0f );
	const float zNear = DEFLIGHT_SPOT_ZNEAR;
	const float zFar = Max( m_pLight->flRadius, zNear + 1.0f );
	const float res = (float)Max( 1, GetShadowResolution_Spot() );
	const float tanHalfFov = tanf( DEG2RAD( fov ) * 0.5f );
	const float depthDerivScale = ( zNear * zFar ) / ( zFar - zNear );
	data.vecSlopeSettings.Init( ( 2.0f * tanHalfFov / res ) * depthDerivScale, tanHalfFov / res, zNear, zFar );

	QUEUE_FIRE( CommitShadowData_Proj, m_iIndex, data );

	SetProjectedShadowDepthBias();

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, m_iIndex );
}

CDeferredViewRender::CDeferredViewRender()
{
	m_pMesh_RadiosityScreenGrid[0] = NULL;
	m_pMesh_RadiosityScreenGrid[1] = NULL;
}

void CDeferredViewRender::Shutdown()
{
	CMatRenderContextPtr pRenderContext( materials );
	for ( IMesh* &mesh : m_pMesh_RadiosityScreenGrid)
	{
		if ( mesh != NULL )
			pRenderContext->DestroyStaticMesh( mesh );

		mesh = NULL;
	}

	for ( CUtlVector<IMesh*>& list : m_hRadiosityDebugMeshList )
	{
        const int count = list.Count();
		for ( int i = 0; i < count; i++ )
		{
            IMesh* mesh = list[i];
			Assert( mesh != NULL );

			pRenderContext->DestroyStaticMesh( mesh );
		}
	}

	BaseClass::Shutdown();
}

void CDeferredViewRender::LevelInit()
{
	BaseClass::LevelInit();

	ResetCascadeDelay();
}

// HL2RPM: staggered cascade updates. A cascade that isn't re-rendered this frame keeps
// both its shadow map tile and its world->texture matrix from the frame it was drawn,
// so it stays correct in world space; it only has to be redrawn when it may no longer
// cover what the camera sees, or when the sun moved.
struct CascadeUpdateState_t
{
	bool bValid;
	bool bZeroed;
	int iLastFrame;
	Vector vecViewPos;
	Vector vecViewFwd;
	Vector vecLightDir;
	float flFov;
	float flSplitFar;
};
static CascadeUpdateState_t s_CascadeUpdateState[SHADOW_NUM_CASCADES];

static ConVar r_csm_stagger( "r_csm_stagger", "1", FCVAR_ARCHIVE, "Update distant sun shadow cascades less often (big performance win)" );

void CDeferredViewRender::ResetCascadeDelay()
{
	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		m_flRenderDelay[i] = 0;
		s_CascadeUpdateState[i].bValid = false;
		s_CascadeUpdateState[i].bZeroed = false;
	}
}

static bool ShouldUpdateCascade( int iCascade, const cascade_t &cascade, const CViewSetup &view,
	const Vector &vecViewFwd, const Vector &vecLightDir )
{
	const CascadeUpdateState_t &st = s_CascadeUpdateState[iCascade];
	if ( !st.bValid || !r_csm_stagger.GetBool() )
		return true;

	if ( gpGlobals->framecount - st.iLastFrame >= cascade.iUpdateInterval )
		return true;

	// camera moved far enough that the stale tile may not cover the slice anymore
	const float flMoveTolerance = Max( 16.0f, cascade.flSplitFar * 0.04f );
	if ( ( view.origin - st.vecViewPos ).LengthSqr() > flMoveTolerance * flMoveTolerance )
		return true;

	// camera turned (cascades are centered in front of the camera)
	if ( DotProduct( vecViewFwd, st.vecViewFwd ) < 0.9925f )	// ~7 degrees
		return true;

	// sun moved
	if ( DotProduct( vecLightDir, st.vecLightDir ) < 0.99996f )	// ~0.5 degree
		return true;

	if ( fabsf( view.fov - st.flFov ) > 0.5f || fabsf( cascade.flSplitFar - st.flSplitFar ) > 1.0f )
		return true;

	return false;
}

void CDeferredViewRender::ViewDrawSceneDeferred( const CViewSetup &view, int nClearFlags, view_id_t viewID, bool bDrawViewModel )
{
	VPROF( "CViewRender::ViewDrawScene" );

	bool bDrew3dSkybox = false;
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;

	ViewDrawGBuffer( view, bDrew3dSkybox, nSkyboxVisible, bDrawViewModel );

	PerformLighting( view );

	// HL2RPM: volumetric clouds, read by the sky shader during the composite
	WeatherRender_Clouds( view );

#if DEFCFG_DEFERRED_SHADING
	ViewCombineDeferredShading( view, viewID );
#else
	ViewDrawComposite( view, bDrew3dSkybox, nSkyboxVisible, nClearFlags, viewID, bDrawViewModel );
#endif

	// other views (monitors, cubemaps...) must not sample this view's screen-space clouds
	WeatherRender_SetCloudTextureValid( false );

#if DEFCFG_ENABLE_RADIOSITY
	if ( r_deferred_radiosity_nodes.GetBool() )
		DebugRadiosity( view );
#endif

#if DEFCFG_DEFERRED_SHADING == 1
	CPostLightingView::PushDeferredShadingFrameBuffer();
#endif

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->UpdateSkymask( bDrew3dSkybox, view.x, view.y, view.width, view.height );
	#endif

	GetLightingManager()->RenderVolumetrics( view );

	// Disable fog for the rest of the stuff
	DisableFog();

	// UNDONE: Don't do this with masked brush models, they should probably be in a separate list
	// render->DrawMaskEntities()

	// Here are the overlays...
	CGlowOverlay::DrawOverlays( view.m_bCacheFullSceneState );

	// issue the pixel visibility tests
	PixelVisibility_EndCurrentView();

	// Draw rain..
	DrawPrecipitation();

	// HL2RPM: lightning channel in the sky, then the dynamic weather rain in front of it
	WeatherRender_Lightning( view );
	WeatherRender_Rain( view );

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	// Debugging info goes over the top
	CDebugViewRender::Draw3DDebuggingInfo( view );

	// Draw client side effects
	// NOTE: These are not sorted against the rest of the frame
	clienteffects->DrawEffects( gpGlobals->frametime );

	// Mark the frame as locked down for client fx additions
	SetFXCreationAllowed( false );

	// Invoke post-render methods
	IGameSystem::PostRenderAllSystems();

#if DEFCFG_DEFERRED_SHADING == 1
	CPostLightingView::PopDeferredShadingFrameBuffer();

	ViewOutputDeferredShading( view );

	// Forward translucent after deferred composite so glass/particles render
	DrawTranslucentRenderables( true, false );
	DrawNoZBufferTranslucentRenderables();
#endif

	FinishCurrentView();

	// Set int rendering parameters back to defaults
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_ENABLE_FIXED_LIGHTING, 0 );
}

void CDeferredViewRender::ViewDrawGBuffer( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible,
	bool bDrawViewModel )
{
	MDLCACHE_CRITICAL_SECTION();

	const view_id_t oldViewID = CurrentViewID();
	const Vector oldOrigin = CurrentViewOrigin();
	const QAngle oldAngles = CurrentViewAngles();
	SetupCurrentView( view.origin, view.angles, VIEW_MAIN );

	CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
	if ( ( bDrew3dSkybox = pSkyView->Setup( view, true, &nSkyboxVisible ) ) != false )
		AddViewToScene( pSkyView );

	SafeRelease( pSkyView );

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	CRefPtr<CGBufferView> pGBufferView = new CGBufferView( this );
	pGBufferView->Setup( view, bDrew3dSkybox );
	AddViewToScene( pGBufferView );

	DrawViewModels( view, bDrawViewModel, true );

	SetupCurrentView( oldOrigin, oldAngles, oldViewID );
}

void CDeferredViewRender::ViewDrawComposite( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible,
		int nClearFlags, view_id_t viewID, bool bDrawViewModel )
{
	UpdateSkyAtmoLUT();
	DrawSkyboxComposite( view, bDrew3dSkybox );

	// this allows the refract texture to be updated once per *scene* on 360
	// (e.g. once for a monitor scene and once for the main scene)
	g_viewscene_refractUpdateFrame = gpGlobals->framecount - 1;

	m_BaseDrawFlags = 0;

	SetupCurrentView( view.origin, view.angles, viewID );

	// Invoke pre-render methods
	IGameSystem::PreRenderAllSystems();

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	if ( !bDrew3dSkybox &&
		( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) && ( visFlags & IVRenderView::VIEW_SETUP_VIS_EX_RETURN_FLAGS_USES_RADIAL_VIS ) )
	{
		// This covers the case where we don't see a 3dskybox, yet radial vis is clipping
		// the far plane.  Need to clear to fog color in this case.
		nClearFlags |= VIEW_CLEAR_COLOR;
		SetClearColorToFogColor( );
	}
	else
		nClearFlags |= VIEW_CLEAR_DEPTH;

	static ConVarRef r_skybox( "r_skybox" );
	bool drawSkybox = r_skybox.IsValid() && r_skybox.GetBool();
	if ( bDrew3dSkybox || ( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) )
		drawSkybox = false;

	ParticleMgr()->IncrementFrameCode();

	DrawWorldComposite( view, nClearFlags, drawSkybox );

	#ifdef SHADEREDITOR
	VisibleFogVolumeInfo_t fogVolumeInfo;
	render->GetVisibleFogVolume(view.origin, &fogVolumeInfo);
	WaterRenderInfo_t info;
	DetermineWaterRenderInfo(fogVolumeInfo, info);
	int nShaderEditorViewID = (int)CurrentViewID();
	g_ShaderEditorSystem->CustomViewRender( &nShaderEditorViewID, fogVolumeInfo, info );
	SetupCurrentView( CurrentViewOrigin(), CurrentViewAngles(), (view_id_t)nShaderEditorViewID );
	#endif

	DrawViewModels( view, bDrawViewModel, false );
}

void CDeferredViewRender::ViewCombineDeferredShading( const CViewSetup &view, view_id_t viewID )
{
#if DEFCFG_DEFERRED_SHADING == 1

	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SCREENSPACE_SHADING ),
		view.width, view.height );

	g_viewscene_refractUpdateFrame = gpGlobals->framecount - 1;

	m_BaseDrawFlags = 0;

	SetupCurrentView( view.origin, view.angles, viewID );

	IGameSystem::PreRenderAllSystems();

	ParticleMgr()->IncrementFrameCode();

	MDLCACHE_CRITICAL_SECTION();

	CRefPtr<CPostLightingView> pPostLightingView = new CPostLightingView( this );
	pPostLightingView->Setup( view );
	AddViewToScene( pPostLightingView );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearBuffers( false, true );

#else

#endif
}

void CDeferredViewRender::ViewOutputDeferredShading( const CViewSetup &view )
{
#if DEFCFG_DEFERRED_SHADING
	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_SCREENSPACE_COMBINE ),
		view.width, view.height );
#endif
}

void CDeferredViewRender::DrawSkyboxComposite( const CViewSetup &view, const bool &bDrew3dSkybox )
{
	if ( !bDrew3dSkybox )
		return;

	CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;
	if ( pSkyView->Setup( view, false, &nSkyboxVisible ) )
	{
		AddViewToScene( pSkyView );

		#ifdef SHADEREDITOR
		g_ShaderEditorSystem->UpdateSkymask(false, view.x, view.y, view.width, view.height);
		#endif
	}

	SafeRelease( pSkyView );
	Assert( nSkyboxVisible == SKYBOX_3DSKYBOX_VISIBLE );
}

static ConVar r_deferred_water_views( "r_deferred_water_views", "1", FCVAR_ARCHIVE, "Render water reflection/refraction views (0 = water shows its fog color only)" );

// HL2RPM: the screen effect under water came from the water material's $underwateroverlay -
// most waters have none (on test_deferred only one of the pools had one) - and only from the
// expensive water path (not with cheap water or r_deferred_water_views 0)
static ConVar r_deferred_water_all_targets( "r_deferred_water_all_targets", "1", FCVAR_ARCHIVE,
	"Draw the water reflection and refraction targets whenever a water is in sight, not only when the closest water's material asks for them" );
// which water targets the expensive waters loaded with the map use (cached until the number
// of loaded materials changes)
static void GetMapWaterTargets( bool &bAnyReflect, bool &bAnyRefract )
{
	static int s_nMaterialCount = -1;
	static bool s_bReflect = false, s_bRefract = false;
	const int nCount = materials->GetNumMaterials();
	if ( nCount != s_nMaterialCount )
	{
		s_nMaterialCount = nCount;
		s_bReflect = s_bRefract = false;
		static ConVarRef r_waterforceexpensive( "r_waterforceexpensive" );
		const bool bForceExpensive = r_waterforceexpensive.IsValid() && r_waterforceexpensive.GetBool();
		for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
		{
			IMaterial *pMat = materials->GetMaterial( h );
			if ( !pMat || pMat->IsErrorMaterial() || !pMat->IsPrecached() )
				continue;
			const char *pszShader = pMat->GetShaderName();
			if ( !pszShader || !V_stristr( pszShader, "Water" ) )
				continue;
			bool bFound = false;
			IMaterialVar *pVar = pMat->FindVar( "$forcecheap", &bFound, false );
			if ( bFound && pVar && pVar->GetIntValue() != 0 )
				continue;
			pVar = pMat->FindVar( "$refracttexture", &bFound, false );
			if ( bFound && pVar && pVar->IsTexture() )
				s_bRefract = true;
			pVar = pMat->FindVar( "$reflecttexture", &bFound, false );
			if ( bFound && pVar && pVar->IsTexture() )
			{
				IMaterialVar *pExpensive = pMat->FindVar( "$forceexpensive", &bFound, false );
				if ( bForceExpensive || ( bFound && pExpensive && pExpensive->GetIntValue() != 0 ) )
					s_bReflect = true;
			}
		}
	}
	bAnyReflect = s_bReflect;
	bAnyRefract = s_bRefract;
}

static ConVar r_deferred_water_debug( "r_deferred_water_debug", "0", 0, "Dev: print the visible water (fog volume) and how it's drawn, once a second" );
static ConVar r_deferred_underwater_overlay( "r_deferred_underwater_overlay", "effects/water_warp01", FCVAR_ARCHIVE,
	"Screen overlay under water when the water material has no $underwateroverlay (empty = none)" );

static void SetUnderwaterOverlay( CViewRender *pMainView, IMaterial *pWaterMaterial )
{
	if ( !pMainView || engine->GetDXSupportLevel() < 90 )	// screen overlays underwater are a dx9 feature
		return;

	const char *pOverlayName = NULL;
	if ( pWaterMaterial )
	{
		IMaterialVar *pScreenOverlayVar = pWaterMaterial->FindVar( "$underwateroverlay", NULL, false );
		if ( pScreenOverlayVar && pScreenOverlayVar->IsDefined() )
			pOverlayName = pScreenOverlayVar->GetStringValue();
	}
	if ( !pOverlayName || !pOverlayName[0] || pOverlayName[0] == '0' )
		pOverlayName = r_deferred_underwater_overlay.GetString();
	if ( !pOverlayName || !pOverlayName[0] || pOverlayName[0] == '0' )
		return;

	IMaterial *pOverlayMaterial = materials->FindMaterial( pOverlayName, TEXTURE_GROUP_OTHER, false );
	if ( pOverlayMaterial && !pOverlayMaterial->IsErrorMaterial() )
		pMainView->SetWaterOverlayMaterial( pOverlayMaterial );
}
static ConVar r_deferred_glass_views( "r_deferred_glass_views", "1", FCVAR_ARCHIVE, "Render the reflection/refraction views of func_reflective_glass (mirrors; 0 = black glass)" );

// HL2RPM: the deferred renderer has no working MSAA - the composite reads one lit surface
// per pixel, whatever mat_antialias says. Thin geometry (the ribs of corrugated metal, roof
// trims, railings, wires) became dotted lines that crawled while the player walked (turning
// hides it: the engine's motion blur). FXAA on the final frame instead.
static ConVar r_deferred_fxaa( "r_deferred_fxaa", "1", FCVAR_ARCHIVE,
	"Anti-aliasing of the final frame (FXAA) - the deferred renderer has no working MSAA" );

static void DrawDeferredFXAA( const CViewSetup &view )
{
	static IMaterial *s_pMatFXAA = NULL;
	if ( !s_pMatFXAA )
	{
		s_pMatFXAA = materials->CreateMaterial( "__hl2rpm_fxaa", new KeyValues( "HL2RPM_FXAA" ) );
		if ( !s_pMatFXAA )
			return;
		s_pMatFXAA->IncrementReferenceCount();
		s_pMatFXAA->Refresh();
	}
	if ( s_pMatFXAA->IsErrorMaterial() )
		return;

	ITexture *pFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	if ( !pFrame || pFrame->IsError() )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	Rect_t rect;
	rect.x = view.x;
	rect.y = view.y;
	rect.width = view.width;
	rect.height = view.height;
	pRenderContext->CopyRenderTargetToTextureEx( pFrame, 0, &rect, &rect );
	pRenderContext->DrawScreenSpaceRectangle( s_pMatFXAA, view.x, view.y, view.width, view.height,
		view.x, view.y, view.x + view.width - 1, view.y + view.height - 1,
		pFrame->GetActualWidth(), pFrame->GetActualHeight() );
}

void CDeferredViewRender::DrawWorldComposite( const CViewSetup &view, int nClearFlags, bool bDrawSkybox )
{
	// HL2RPM: the water views were compiled out (#if 0): the reflection and refraction
	// textures were never drawn and every expensive water was a flat, unlit fog color
	if ( r_deferred_water_views.GetBool() )
	{
	MDLCACHE_CRITICAL_SECTION();

	VisibleFogVolumeInfo_t fogVolumeInfo;

	render->GetVisibleFogVolume( view.origin, &fogVolumeInfo );

	WaterRenderInfo_t info;
	DetermineWaterRenderInfo( fogVolumeInfo, info );

	// HL2RPM: the engine picks one water - the one closest to the eye - and draws the
	// reflection / refraction targets that *its* material asks for. Different waters in sight
	// lost theirs as soon as another one was closer: walking away from a reflective pool toward
	// a refract-only one (test_deferred), or past the $forcecheap part of the demo_map canal,
	// the targets stopped updating and the other waters' shader looked switched off. Whenever a
	// water is in sight, draw both (at the height of the picked one).
	// (only the targets some water of the map can use: demo_map's waters - a refract-only one
	// and a $forcecheap one - would pay for a reflection view nobody reads)
	if ( r_deferred_water_all_targets.GetBool() && fogVolumeInfo.m_nVisibleFogVolume != -1 && fogVolumeInfo.m_pFogVolumeMaterial
		&& info.m_bDrawWaterSurface && engine->GetDXSupportLevel() >= 90 )
	{
		bool bAnyReflect, bAnyRefract;
		GetMapWaterTargets( bAnyReflect, bAnyRefract );
		info.m_bReflect = info.m_bReflect || bAnyReflect;
		info.m_bRefract = info.m_bRefract || bAnyRefract;
		if ( info.m_bRefract )
			info.m_bOpaqueWater = false;
		info.m_bCheapWater = !info.m_bReflect && !info.m_bRefract;
	}

	if ( r_deferred_water_debug.GetBool() )
	{
		static float s_flNextPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextPrint )
		{
			s_flNextPrint = gpGlobals->realtime + 1.0f;
			Msg( "[water] eye %.0f %.0f %.0f: fog volume %d (leaf %d) eye in %d, dist %.0f, height %.1f, cheap %d draw %d refract %d reflect %d, %s\n",
				view.origin.x, view.origin.y, view.origin.z, fogVolumeInfo.m_nVisibleFogVolume, fogVolumeInfo.m_nVisibleFogVolumeLeaf,
				fogVolumeInfo.m_bEyeInFogVolume ? 1 : 0, fogVolumeInfo.m_flDistanceToWater, fogVolumeInfo.m_flWaterHeight,
				info.m_bCheapWater ? 1 : 0, info.m_bDrawWaterSurface ? 1 : 0, info.m_bRefract ? 1 : 0, info.m_bReflect ? 1 : 0,
				fogVolumeInfo.m_pFogVolumeMaterial ? fogVolumeInfo.m_pFogVolumeMaterial->GetName() : "-" );
		}
	}

	if ( info.m_bCheapWater )
	{
		// HL2RPM: mirrors and reflective windows (func_reflective_glass, its own render
		// targets with Mapbase) - this was "#if 0 // TODO": the glass stayed black
		if ( r_deferred_glass_views.GetBool() )
		{
			cplane_t glassPlane;
			Frustum_t frustum;
			GeneratePerspectiveFrustum( view.origin, view.angles, view.zNear, view.zFar, view.fov, view.m_flAspectRatio, frustum );

			ITexture *pTargets[2] = { NULL, NULL };
			C_BaseEntity *pGlass = NextReflectiveGlass( NULL, view, glassPlane, frustum, pTargets );
			while ( pGlass != NULL )
			{
				if ( pTargets[0] )
				{
					CRefPtr<CGlassViewDeferred> pReflectionView = new CGlassViewDeferred( this, true );
					pReflectionView->Setup( view, bDrawSkybox, glassPlane, pTargets[0] );
					AddViewToScene( pReflectionView );
				}
				if ( pTargets[1] )
				{
					CRefPtr<CGlassViewDeferred> pRefractionView = new CGlassViewDeferred( this, false );
					pRefractionView->Setup( view, bDrawSkybox, glassPlane, pTargets[1] );
					AddViewToScene( pRefractionView );
				}
				pGlass = NextReflectiveGlass( pGlass, view, glassPlane, frustum, pTargets );
			}
		}

		// (the eye can be under cheap water as well)
		if ( fogVolumeInfo.m_bEyeInFogVolume )
			SetUnderwaterOverlay( this, fogVolumeInfo.m_pFogVolumeMaterial );

		CRefPtr<CSimpleWorldViewDeferred> pNoWaterView = new CSimpleWorldViewDeferred( this );
		pNoWaterView->Setup( view, nClearFlags, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pNoWaterView );
		return;
	}

	// Blat out the visible fog leaf if we're not going to use it
	static ConVarRef r_ForceWaterLeaf( "r_ForceWaterLeaf" );
	if ( !r_ForceWaterLeaf.IsValid() || !r_ForceWaterLeaf.GetBool() )
	{
		fogVolumeInfo.m_nVisibleFogVolumeLeaf = -1;
	}

	// We can see water of some sort
	if ( !fogVolumeInfo.m_bEyeInFogVolume )
	{
		CRefPtr<CAboveWaterViewDeferred> pAboveWaterView = new CAboveWaterViewDeferred( this );
		pAboveWaterView->Setup( view, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pAboveWaterView );
	}
	else
	{
		CRefPtr<CUnderWaterViewDeferred> pUnderWaterView = new CUnderWaterViewDeferred( this );
		pUnderWaterView->Setup( view, bDrawSkybox, fogVolumeInfo, info );
		AddViewToScene( pUnderWaterView );
	}
	return;
	}

	MDLCACHE_CRITICAL_SECTION();
	VisibleFogVolumeInfo_t fogVolumeInfo;
	render->GetVisibleFogVolume( view.origin, &fogVolumeInfo );

	WaterRenderInfo_t info;
	DetermineWaterRenderInfo( fogVolumeInfo, info );

	if ( fogVolumeInfo.m_bEyeInFogVolume )
		SetUnderwaterOverlay( this, fogVolumeInfo.m_pFogVolumeMaterial );

	CRefPtr<CSimpleWorldViewDeferred> pNoWaterView = new CSimpleWorldViewDeferred( this );
	pNoWaterView->Setup( view, nClearFlags, bDrawSkybox, fogVolumeInfo, info );
	AddViewToScene( pNoWaterView );
}

void CDeferredViewRender::PerformLighting( const CViewSetup &view )
{
	bool bResetLightAccum = false;
	const bool bRadiosityEnabled = DEFCFG_ENABLE_RADIOSITY != 0 && r_deferred_radiosity.GetBool() && AreRadiosityRTsAvailable();
	bool bDrawDebugShadow = false;

	if ( bRadiosityEnabled )
		BeginRadiosity( view );

	if ( GetGlobalLight() != NULL )
	{
		struct defData_setGlobalLightState
		{
		public:
			lightData_Global_t state;

			static void Fire( defData_setGlobalLightState d )
			{
				GetDeferredExt()->CommitLightData_Global( d.state );
			};
		};

		lightData_Global_t lightDataState = GetActiveGlobalLightState();

		if ( !GetLightingEditor()->IsEditorLightingActive() &&
			r_deferred_light_global_override.GetBool() )
		{
			lightDataState.bShadow = r_deferred_light_global_override_shadow.GetBool();
			UTIL_StringToVector( lightDataState.diff.AsVector3D().Base(), r_deferred_light_global_override_diffuse.GetString() );
			UTIL_StringToVector( lightDataState.ambh.AsVector3D().Base(), r_deferred_light_global_override_ambient_high.GetString() );
			UTIL_StringToVector( lightDataState.ambl.AsVector3D().Base(), r_deferred_light_global_override_ambient_low.GetString() );

			lightDataState.bEnabled = true;
		}

		if ( r_deferred_light_global_smooth.GetBool() )
		{
			static bool s_hasSmoothed = false;
			static lightData_Global_t s_smoothed;

			const float dt = gpGlobals ? Max( 0.0f, gpGlobals->frametime ) : 0.0f;
			const float tau = Max( 0.001f, r_deferred_light_global_smooth_tau.GetFloat() );
			const float a = 1.0f - expf( -dt / tau );

			if ( !s_hasSmoothed )
			{
				s_smoothed = lightDataState;
				s_hasSmoothed = true;
			}
			else
			{
				s_smoothed.bEnabled = lightDataState.bEnabled;
				s_smoothed.bShadow = lightDataState.bShadow;
				s_smoothed.flFadeTime = lightDataState.flFadeTime;
				s_smoothed.flShadowBlend = lightDataState.flShadowBlend;

				Vector curDir = lightDataState.vecLight.AsVector3D();
				Vector prevDir = s_smoothed.vecLight.AsVector3D();
				Vector newDir = prevDir * ( 1.0f - a ) + curDir * a;
				const float len = newDir.Length();
				if ( len > 0.0001f )
					newDir /= len;
				else
					newDir.Init( 0, 0, 1 );
				s_smoothed.vecLight.Init( newDir.x, newDir.y, newDir.z );

				const float invA = 1.0f - a;

				float *prevDiff = s_smoothed.diff.Base();
				const float *curDiff = lightDataState.diff.Base();
				float *prevAmbH = s_smoothed.ambh.Base();
				const float *curAmbH = lightDataState.ambh.Base();
				float *prevAmbL = s_smoothed.ambl.Base();
				const float *curAmbL = lightDataState.ambl.Base();

				for ( int i = 0; i < 3; i++ )
				{
					prevDiff[i] = prevDiff[i] * invA + curDiff[i] * a;
					prevAmbH[i] = prevAmbH[i] * invA + curAmbH[i] * a;
					prevAmbL[i] = prevAmbL[i] * invA + curAmbL[i] * a;
				}
			}

			lightDataState = s_smoothed;
		}

		// HL2RPM: the lightning flash lasts a few frames, it must not go through the smoothing;
		// the sky and the clouds keep the sun / moon
		WeatherRender_SetSkyLight( lightDataState );
		s_skyLightState = lightDataState;
		s_bSkyLightStateValid = true;
		if ( GetGlobalLight() && !GetLightingEditor()->IsEditorLightingActive() )
			GetWeatherSystem()->ApplyLightningFlash( lightDataState, GetGlobalLight()->HasShadow() );

		QUEUE_FIRE( CommitLightData_Global, lightDataState );

		// HL2RPM: top-down depth map that tells where rain can fall. Before the weather
		// constants of the frame, which carry its matrix: the other way round the frame that
		// re-rendered the map (every second while walking) read the new map through the
		// previous matrix - sky light, wet surfaces and rain shifted by the distance walked
		// for one frame (a flash over everything near walls and roofs, only when moving)
		if ( !r_deferred_rainmap_debug_late.GetBool() )
			RenderRainOcclusion( view );

		// HL2RPM: weather constants for this frame (clouds, sky, fog, rain...)
		WeatherRender_CommitFrame( view, lightDataState );

		if ( r_deferred_rainmap_debug_late.GetBool() )
			RenderRainOcclusion( view );

		if ( lightDataState.bEnabled )
		{
			if ( lightDataState.bShadow )
			{
				bDrawDebugShadow = r_deferred_debug_shadow.GetBool();
				// HL2RPM: casters come from every leaf, culled by each cascade's own box. The PVS
				// of the camera (and of a point toward the sun) dropped walls and props outside
				// it: their shadows popped in and out as the camera crossed leaves (the main view
				// sets up its own vis again before the composite)
				Vector origins[2] = { view.origin, view.origin + lightDataState.vecLight.AsVector3D() * 1024 };
				render->ViewSetupVis( r_csm_novis.GetBool(), 2, origins );

				RenderCascadedShadows( view, bRadiosityEnabled );
			}
		}
		else
			bResetLightAccum = true;
	}
	else
	{
		bResetLightAccum = true;

		// no global light: weather is disabled for this frame
		lightData_Global_t noLight;
		WeatherRender_CommitFrame( view, noLight );
	}

	CViewSetup lightingView = view;

	if ( building_cubemaps.GetBool() )
		engine->GetScreenSize( lightingView.width, lightingView.height );

	// HL2RPM: ambient occlusion of the sky light (SSAO + sky visibility), read by the global light pass
	DeferredSSAO_Render( lightingView );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Lightaccum() );

	if ( bResetLightAccum )
	{
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
	}
	else
		DrawLightPassFullscreen(
			GetDeferredManager()->GetDeferredMaterial( bDrawDebugShadow ? DEF_MAT_DEBUG_SHADOW_ORTHO : DEF_MAT_LIGHT_GLOBAL ),
			lightingView.width, lightingView.height );

	pRenderContext.SafeRelease();

	GetLightingManager()->RenderLights( lightingView, this );

	if ( bRadiosityEnabled )
		EndRadiosity( view );

	pRenderContext.GetFrom( materials );
	pRenderContext->PopRenderTargetAndViewport();
}

static int GetSourceRadBufferIndex( const int index )
{
	Assert( index == 0 || index == 1 );

	const bool bFar = index == 1;
	const int iNumSteps = (bFar ? r_deferred_radiosity_propagate_far_count.GetInt() : r_deferred_radiosity_propagate_count.GetInt())
		+ (bFar ? r_deferred_radiosity_blur_far_count.GetInt() : r_deferred_radiosity_blur_count.GetInt());
	return ( iNumSteps % 2 == 0 ) ? 0 : 1;
}

void CDeferredViewRender::BeginRadiosity( const CViewSetup &view )
{
	Vector fwd;
	AngleVectors( view.angles, &fwd );

	float flAmtVertical = abs( DotProduct( fwd, Vector( 0, 0, 1 ) ) );
	flAmtVertical = RemapValClamped( flAmtVertical, 0, 1, 1, 0.5f );

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		const bool bFar = iCascade == 1;
		const Vector gridSize( RADIOSITY_BUFFER_SAMPLES_XY, RADIOSITY_BUFFER_SAMPLES_XY,
								RADIOSITY_BUFFER_SAMPLES_Z );
		const Vector gridSizeHalf = gridSize / 2;
		const float gridStepSize = bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR
			: RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE;
		const float flGridDistance = bFar ? RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_FAR
			: RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_CLOSE;

		Vector vecFwd;
		AngleVectors( view.angles, &vecFwd );

		m_vecRadiosityOrigin[iCascade] = view.origin
			+ vecFwd * gridStepSize * RADIOSITY_BUFFER_SAMPLES_XY * flGridDistance * flAmtVertical;

		for ( int i = 0; i < 3; i++ )
			m_vecRadiosityOrigin[iCascade][i] -= fmod( m_vecRadiosityOrigin[iCascade][i], gridStepSize );

		m_vecRadiosityOrigin[iCascade] -= gridSizeHalf * gridStepSize;

		const int iSourceBuffer = GetSourceRadBufferIndex( iCascade );
		static int iLastSourceBuffer[2] = { iSourceBuffer, GetSourceRadBufferIndex( 1 ) };

		const int clearSizeY = RADIOSITY_BUFFER_RES_Y / 2;
		const int clearOffset = (iCascade == 1) ? clearSizeY : 0;

		CMatRenderContextPtr pRenderContext( materials );

		pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( iSourceBuffer ), NULL,
			0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
		pRenderContext->ClearColor3ub( 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();

		if ( iLastSourceBuffer[iCascade] != iSourceBuffer )
		{
			iLastSourceBuffer[iCascade] = iSourceBuffer;

			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( 1 - iSourceBuffer ), NULL,
				0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
			pRenderContext->PopRenderTargetAndViewport();

			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityNormal( 1 - iSourceBuffer ), NULL,
				0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
			pRenderContext->ClearColor3ub( 127, 127, 127 );
			pRenderContext->ClearBuffers( true, false );
			pRenderContext->PopRenderTargetAndViewport();
		}

		pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityNormal( iSourceBuffer ), NULL,
			0, clearOffset, RADIOSITY_BUFFER_RES_X, clearSizeY );
		pRenderContext->ClearColor3ub( 127, 127, 127 );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	UpdateRadiosityPosition();
}

void CDeferredViewRender::UpdateRadiosityPosition()
{
	struct defData_setupRadiosity
	{
	public:
		radiosityData_t data;

		static void Fire( defData_setupRadiosity d )
		{
			GetDeferredExt()->CommitRadiosityData( d.data );
		};
	};

	radiosityData_t radSetup;
	radSetup.vecOrigin[0] = m_vecRadiosityOrigin[0];
	radSetup.vecOrigin[1] = m_vecRadiosityOrigin[1];

	QUEUE_FIRE( CommitRadiosityData, radSetup );
}

void CDeferredViewRender::PerformRadiosityGlobal( const int iRadiosityCascade, const CViewSetup &view )
{
	const int iSourceBuffer = GetSourceRadBufferIndex( iRadiosityCascade );
	const int iOffsetY = (iRadiosityCascade == 1) ? RADIOSITY_BUFFER_RES_Y/2 : 0;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DEFERRED_RADIOSITY_CASCADE, (float)iRadiosityCascade );

	pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( iSourceBuffer ), NULL,
		0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
	pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( iSourceBuffer ) );

	pRenderContext->Bind( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_GLOBAL ) );
	GetRadiosityScreenGrid( iRadiosityCascade )->Draw();

	pRenderContext->PopRenderTargetAndViewport();
}

void CDeferredViewRender::EndRadiosity( const CViewSetup &view )
{
	const int iNumPropagateSteps[2] = { r_deferred_radiosity_propagate_count.GetInt(),
		r_deferred_radiosity_propagate_far_count.GetInt() };
	const int iNumBlurSteps[2] = { r_deferred_radiosity_blur_count.GetInt(),
		r_deferred_radiosity_blur_far_count.GetInt() };

	IMaterial *pPropagateMat[2] = {
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_PROPAGATE_0 ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_PROPAGATE_1 ),
	};

	IMaterial *pBlurMat[2] = {
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLUR_0 ),
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLUR_1 ),
	};

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		bool bSecondDestBuffer = GetSourceRadBufferIndex( iCascade ) == 0;
		const int iOffsetY = (iCascade==1) ? RADIOSITY_BUFFER_RES_Y / 2 : 0;

		for ( int i = 0; i < iNumPropagateSteps[iCascade]; i++ )
		{
			const int index = bSecondDestBuffer ? 1 : 0;
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( index ), NULL,
				0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
			pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( index ) );

			pRenderContext->Bind( pPropagateMat[ 1 - index ] );

			GetRadiosityScreenGrid( iCascade )->Draw();

			pRenderContext->PopRenderTargetAndViewport();
			bSecondDestBuffer = !bSecondDestBuffer;
		}

		for ( int i = 0; i < iNumBlurSteps[iCascade]; i++ )
		{
			const int index = bSecondDestBuffer ? 1 : 0;
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushRenderTargetAndViewport( GetDefRT_RadiosityBuffer( index ), NULL,
				0, iOffsetY, RADIOSITY_BUFFER_VIEWPORT_SX, RADIOSITY_BUFFER_VIEWPORT_SY );
			pRenderContext->SetRenderTargetEx( 1, GetDefRT_RadiosityNormal( index ) );

			pRenderContext->Bind( pBlurMat[ 1 - index ] );

			GetRadiosityScreenGrid( iCascade )->Draw();

			pRenderContext->PopRenderTargetAndViewport();
			bSecondDestBuffer = !bSecondDestBuffer;
		}
	}

#if ( DEFCFG_DEFERRED_SHADING == 0 )
	DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_BLEND ),
		view.width, view.height );
#endif
}

void CDeferredViewRender::DebugRadiosity( const CViewSetup &view )
{
#if 0
	Vector tmp[3] = { m_vecRadiosityOrigin[1],
		m_vecRadiosityOrigin[1],
		m_vecRadiosityOrigin[1] };

	const int directions[3][2] = {
		1, 2,
		0, 2,
		0, 1,
	};

	const Vector vecCross[3] = {
		Vector( 1, 0, 0 ),
		Vector( 0, 1, 0 ),
		Vector( 0, 0, 1 ),
	};

	const int iColors[3][3] = {
		255, 0, 0,
		0, 255, 0,
		0, 0, 255,
	};

	for ( int i = 0; i < 3; i++ )
	{
		for ( int x = 0; x < RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR; x++ )
		{
			Vector tmp2 = tmp[i];

			for ( int y = 0; y < RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR; y++ )
			{
				NDebugOverlay::Line( tmp2, tmp2 + vecCross[i] * RADIOSITY_BUFFER_GRID_STEP_DISTANCEMULT_FAR * RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR,
					iColors[i][0], iColors[i][1], iColors[i][2], true, -1 );

				tmp2[ directions[i][1] ] += RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR;
			}

			tmp[i][ directions[i][0] ] += RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR;
		}
	}
#endif

	IMaterial *pMatDbgRadGrid = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_DEBUG );

	if ( m_hRadiosityDebugMeshList[0].Count() == 0 )
	{
		for ( int iCascade = 0; iCascade < 2; iCascade++ )
		{
			const bool bFar = iCascade == 1;
			const float flGridSize = bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR : RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE;
			const float flCubesize = flGridSize * 0.15f;
			const Vector directions[3] = {
				Vector( flGridSize, 0, 0 ),
				Vector( 0, flGridSize, 0 ),
				Vector( 0, 0, flGridSize ),
			};

			const float flUVOffsetY = bFar ? 0.5f : 0.0f;

			int nMaxVerts, nMaxIndices;
			CMatRenderContextPtr pRenderContext( materials );
			CMeshBuilder meshBuilder;

			IMesh *pMesh = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
				TEXTURE_GROUP_OTHER,
				pMatDbgRadGrid );
			m_hRadiosityDebugMeshList[iCascade].AddToTail( pMesh );

			IMesh *pMeshDummy = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMatDbgRadGrid );
			pRenderContext->GetMaxToRender( pMeshDummy, false, &nMaxVerts, &nMaxIndices );
			pMeshDummy->Draw();

			int nMaxCubes = nMaxIndices / 36;
			if ( nMaxCubes > nMaxVerts / 24 )
				nMaxCubes = nMaxVerts / 24;

			int nRenderRemaining = nMaxCubes;
			meshBuilder.Begin( pMesh, MATERIAL_QUADS, nMaxCubes * 6 );

			const Vector2D flUVTexelSize( 1.0f / RADIOSITY_BUFFER_RES_X,
				1.0f / RADIOSITY_BUFFER_RES_Y );
			const Vector2D flUVTexelSizeHalf = flUVTexelSize * 0.5f;
			const Vector2D flUVGridSize =
				Vector2D( RADIOSITY_UVRATIO_X, RADIOSITY_UVRATIO_Y )
				* 1.0f / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

			for ( int x = 0; x < RADIOSITY_BUFFER_SAMPLES_XY; x++ )
			for ( int y = 0; y < RADIOSITY_BUFFER_SAMPLES_XY; y++ )
			for ( int z = 0; z < RADIOSITY_BUFFER_SAMPLES_Z; z++ )
			{
				if ( nRenderRemaining <= 0 )
				{
					nRenderRemaining = nMaxCubes;
					meshBuilder.End();
					pMesh = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
								TEXTURE_GROUP_OTHER,
								GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_RADIOSITY_DEBUG ) );
					m_hRadiosityDebugMeshList[iCascade].AddToTail( pMesh );
					meshBuilder.Begin( pMesh, MATERIAL_QUADS, nMaxCubes * 6 );
				}

				int grid_x = z % RADIOSITY_BUFFER_GRIDS_PER_AXIS;
				int grid_y = z / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

				float flUV[2] = {
					grid_x * flUVGridSize.x + x * flUVTexelSize.x + flUVTexelSizeHalf.x,
					grid_y * flUVGridSize.y + y * flUVTexelSize.y + flUVTexelSizeHalf.y + flUVOffsetY,
				};

				DrawCube( meshBuilder, directions[ 0 ] * x
					+ directions[ 1 ] * y
					+ directions[ 2 ] * z,
					flCubesize,
					flUV );

				nRenderRemaining--;
			}

			if ( nRenderRemaining != nMaxCubes )
				meshBuilder.End();
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMatDbgRadGrid );

	for ( int iCascade = 0; iCascade < 2; iCascade++ )
	{
		VMatrix pos;
		pos.SetupMatrixOrgAngles( m_vecRadiosityOrigin[iCascade], vec3_angle );

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PushMatrix();
		pRenderContext->LoadMatrix( pos );

		for ( int i = 0; i < m_hRadiosityDebugMeshList[iCascade].Count(); i++ )
			m_hRadiosityDebugMeshList[iCascade][ i ]->Draw();

		pRenderContext->MatrixMode( MATERIAL_MODEL );
		pRenderContext->PopMatrix();
	}
}

void CDeferredViewRender::RenderCascadedShadows( const CViewSetup &view, const bool bEnableRadiosity )
{
	// Set minimal hardware depth bias for cascade shadow maps.
	// The receiver-side shader bias (Normal Offset + Slope Depth Bias) handles
	// shadow acne adaptively per-pixel, so we only need a tiny rasteriser bias
	// as a safety net against floating-point coincidence on perfectly flat surfaces.
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->SetShadowDepthBiasFactors( 0.5f, 0.000005f );
	}

	// HL2RPM: fit cascades to the current camera and only redraw what's needed.
	UpdateCascadeSplits( view.zNear, view.fov, view.m_flAspectRatio );
	const int iActiveCascades = GetActiveCascadeCount();

	Vector vecViewFwd;
	AngleVectors( view.angles, &vecViewFwd );
	const Vector vecLightDir = GetActiveGlobalLightState().vecLight.AsVector3D();

	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		CascadeUpdateState_t &st = s_CascadeUpdateState[i];

		if ( i >= iActiveCascades )
		{
			// Unused slot: an all-zero matrix projects everything outside the tile,
			// so the shader's cascade search skips it.
			if ( !st.bZeroed )
			{
				shadowData_ortho_t zeroData;
				Q_memset( &zeroData, 0, sizeof( zeroData ) );
				zeroData.iRes_x = CSM_COMP_RES_X;
				zeroData.iRes_y = CSM_COMP_RES_Y;
				QUEUE_FIRE( CommitShadowData_Ortho, i, zeroData );
				st.bZeroed = true;
				st.bValid = false;
			}
			continue;
		}
		st.bZeroed = false;

		const cascade_t &cascade = GetCascadeInfo(i);
		const bool bDoRadiosity = bEnableRadiosity && cascade.bOutputRadiosityData;
		const int iRadTarget = cascade.iRadiosityCascadeTarget;

		if ( !bDoRadiosity && !ShouldUpdateCascade( i, cascade, view, vecViewFwd, vecLightDir ) )
			continue;

		st.bValid = true;
		st.iLastFrame = gpGlobals->framecount;
		st.vecViewPos = view.origin;
		st.vecViewFwd = vecViewFwd;
		st.vecLightDir = vecLightDir;
		st.flFov = view.fov;
		st.flSplitFar = cascade.flSplitFar;

#if CSM_USE_COMPOSITED_TARGET == 0
		int textureIndex = i;
#else
		int textureIndex = 0;
#endif

		CRefPtr<COrthoShadowView> pOrthoDepth = new COrthoShadowView( this, i );
		pOrthoDepth->Setup( view, GetShadowDepthRT_Ortho( textureIndex ), GetShadowColorRT_Ortho( textureIndex ) );
		if ( bDoRadiosity )
		{
			pOrthoDepth->SetRadiosityOutputEnabled( true );
			pOrthoDepth->SetupRadiosityTargets( GetRadiosityAlbedoRT_Ortho( textureIndex ),
				GetRadiosityNormalRT_Ortho( textureIndex ) );
		}
		AddViewToScene( pOrthoDepth );

		if ( bDoRadiosity )
			PerformRadiosityGlobal( iRadTarget, view );
	}

	// Restore hardware bias for spot-light shadow maps.
	// Reduced from (16.0, 0.00005) now that spot lights use a proper
	// receiver-side adaptive bias.  The old aggressive slope-scale of 16
	// was a caster-only workaround; it over-biased angled surfaces while
	// barely helping face-on geometry where the slope term is near-zero.
	// (2.0, 0.00001) acts as a thin safety-net without conflicting with
	// the receiver-side system.
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->SetShadowDepthBiasFactors( 2.0f, 0.00001f );
	}
}

void CDeferredViewRender::RenderRainOcclusion( const CViewSetup &view )
{
	Vector vecCenter;
	float flSize = 3072.0f;
	if ( !WeatherRender_ShouldUpdateRainMap( view, vecCenter, flSize ) )
		return;

	ITexture *pDepth = GetWeatherRT_RainMapDepth();
	ITexture *pDummy = GetWeatherRT_RainMapDummy();
	if ( !pDepth || !pDummy )
		return;

	CRefPtr<CRainOcclusionView> pRainView = new CRainOcclusionView( this, vecCenter, flSize );
	pRainView->Setup( view, pDepth, pDummy );
	pRainView->AddExtraVisOrigin( view.origin );

	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal )
		pRainView->m_iShadowExcludeEntIndex = pLocal->entindex();

	// Everything inside the map's box must block rain, not only what the PVS of the
	// camera contains: brush entities and props over the player were missing, so the
	// ground under them got wet. (Shadow views build their lists from the engine's
	// current vis - m_bForceNoVis only affects CViewRender::SetupVis, which they don't
	// call; the main view sets up its own vis again before the composite.)
	render->ViewSetupVis( true, 1, &view.origin );
	AddViewToScene( pRainView );

	// the sky visibility of the ambient light is filtered from it once per render
	DeferredSSAO_UpdateSkyVisibility();
}

void CDeferredViewRender::DrawLightShadowView( const CViewSetup &view, int iDesiredShadowmap, def_light_t *l )
{
	CViewSetup setup;
	setup.origin = l->pos;
	setup.angles = l->ang;
	setup.m_bOrtho = false;
	setup.m_flAspectRatio = 1;
	setup.x = setup.y = 0;

	Vector origins[2] = { view.origin, l->pos };
	render->ViewSetupVis( false, 2, origins );

	switch ( l->iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
		{
			if ( r_deferred_shadowpoint_legacy.GetBool() )
			{
				CRefPtr<CDualParaboloidShadowView> pDPView0 = new CDualParaboloidShadowView( this, l, false );
				pDPView0->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
				AddViewToScene( pDPView0 );

				CRefPtr<CDualParaboloidShadowView> pDPView1 = new CDualParaboloidShadowView( this, l, true );
				pDPView1->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
				AddViewToScene( pDPView1 );
			}
			else
			{
				for ( int face = 0; face < 6; face++ )
				{
					CRefPtr<CPointLightCubeFaceShadowView> pFaceView =
						new CPointLightCubeFaceShadowView( this, l, face, iDesiredShadowmap );
					pFaceView->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
					AddViewToScene( pFaceView );
				}
			}
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			CRefPtr<CSpotLightShadowView> pProjView = new CSpotLightShadowView( this, l, iDesiredShadowmap );

			pProjView->Setup( setup, GetShadowDepthRT_Proj( iDesiredShadowmap ), GetShadowColorRT_Proj( iDesiredShadowmap ) );
			AddViewToScene( pProjView );
		}
		break;
	}
}

void CDeferredViewRender::DrawViewModels( const CViewSetup &view, bool drawViewmodel, bool bGBuffer )
{
	VPROF( "CViewRender::DrawViewModel" );
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

#ifdef PORTAL //in portal, we'd like a copy of the front buffer without the gun in it for use with the depth doubler
	g_pPortalRender->UpdateDepthDoublerTexture( view );
#endif

	bool bShouldDrawPlayerViewModel = ShouldDrawViewModel( drawViewmodel );
	bool bShouldDrawToolViewModels = ToolsEnabled();

	CMatRenderContextPtr pRenderContext( materials );

	PIXEVENT( pRenderContext, "DrawViewModels" );

	// Restore the matrices
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PushMatrix();

	CViewSetup viewModelSetup( view );
	viewModelSetup.zNear = view.zNearViewmodel;
	viewModelSetup.zFar = view.zFarViewmodel;
	viewModelSetup.fov = view.fovViewmodel;
	viewModelSetup.m_flAspectRatio = engine->GetScreenAspectRatio();

	ITexture *pRTColor = NULL;
	ITexture *pRTDepth = NULL;
	if( view.m_eStereoEye != STEREO_EYE_MONO )
	{
		pRTColor = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(view.m_eStereoEye-1), ISourceVirtualReality::RT_Color );
		pRTDepth = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(view.m_eStereoEye-1), ISourceVirtualReality::RT_Depth );
	}

	render->Push3DView( viewModelSetup, 0, pRTColor, GetFrustum(), pRTDepth );

	if ( bGBuffer )
	{
		const float flViewmodelScale = view.zFarViewmodel / view.zFar;
		CGBufferView::PushGBuffer( false, flViewmodelScale, false );
	}
	else
	{
		pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
			DEFERRED_RENDER_STAGE_COMPOSITION );
	}

#ifdef PORTAL //the depth range hack doesn't work well enough for the portal mod (and messing with the depth hack values makes some models draw incorrectly)
				//step up to a full depth clear if we're extremely close to a portal (in a portal environment)
	extern bool LocalPlayerIsCloseToPortal( void ); //defined in C_Portal_Player.cpp, abstracting to a single bool function to remove explicit dependence on c_portal_player.h/cpp, you can define the function as a "return true" in other build configurations at the cost of some perf
	bool bUseDepthHack = !LocalPlayerIsCloseToPortal();
	if( !bUseDepthHack )
		pRenderContext->ClearBuffers( false, true, false );
#else
	const bool bUseDepthHack = true;
#endif

	// FIXME: Add code to read the current depth range
	float depthmin = 0.0f;
	float depthmax = 1.0f;

	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	// Force clipped down range
	if( bUseDepthHack )
		pRenderContext->DepthRange( 0.0f, 0.1f );

	if ( bShouldDrawPlayerViewModel || bShouldDrawToolViewModels )
	{
		CUtlVector< IClientRenderable * > opaqueViewModelList( 32 );
		CUtlVector< IClientRenderable * > translucentViewModelList( 32 );

		ClientLeafSystem()->CollateViewModelRenderables( opaqueViewModelList, translucentViewModelList );

		if ( ToolsEnabled() && ( !bShouldDrawPlayerViewModel || !bShouldDrawToolViewModels ) )
		{
			int nOpaque = opaqueViewModelList.Count();
			for ( int i = nOpaque-1; i >= 0; --i )
			{
				IClientRenderable *pRenderable = opaqueViewModelList[ i ];
				bool bEntity = pRenderable->GetIClientUnknown()->GetBaseEntity();
				if ( ( bEntity && !bShouldDrawPlayerViewModel ) || ( !bEntity && !bShouldDrawToolViewModels ) )
				{
					opaqueViewModelList.FastRemove( i );
				}
			}

			int nTranslucent = translucentViewModelList.Count();
			for ( int i = nTranslucent-1; i >= 0; --i )
			{
				IClientRenderable *pRenderable = translucentViewModelList[ i ];
				bool bEntity = pRenderable->GetIClientUnknown()->GetBaseEntity();
				if ( ( bEntity && !bShouldDrawPlayerViewModel ) || ( !bEntity && !bShouldDrawToolViewModels ) )
				{
					translucentViewModelList.FastRemove( i );
				}
			}
		}

		if ( !UpdateRefractIfNeededByList( opaqueViewModelList ) && !bGBuffer )
		{
			UpdateRefractIfNeededByList( translucentViewModelList );
		}

		DrawRenderablesInList( opaqueViewModelList );
		if (!bGBuffer)
			DrawRenderablesInList( translucentViewModelList, STUDIO_TRANSPARENCY );
		else
			pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
			                                          DEFERRED_RENDER_STAGE_INVALID );
	}

	// Reset the depth range to the original values
	if( bUseDepthHack )
		pRenderContext->DepthRange( depthmin, depthmax );

	if ( bGBuffer )
	{
		CGBufferView::PopGBuffer();
	}

	render->PopView( GetFrustum() );

	// Restore the matrices
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PopMatrix();
}

void CDeferredViewRender::RenderView( const CViewSetup &view, int nClearFlags, int whatToDraw )
{
	m_UnderWaterOverlayMaterial.Shutdown();					// underwater view will set

	CViewSetup worldView = view;

	CLightingEditor *pLightEditor = GetLightingEditor();

	if ( pLightEditor->IsEditorActive() && !building_cubemaps.GetBool() )
		pLightEditor->GetEditorView( &worldView.origin, &worldView.angles );
	else
		pLightEditor->SetEditorView( &worldView.origin, &worldView.angles );

	m_CurrentView = worldView;

	C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, true );
	VPROF( "CViewRender::RenderView" );
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

	CMatRenderContextPtr pRenderContext( materials );
	ITexture *saveRenderTarget = pRenderContext->GetRenderTarget();
	pRenderContext.SafeRelease(); // don't want to hold for long periods in case in a locking active share thread mode

	g_pClientShadowMgr->AdvanceFrame();
	ServiceDeferredRTRefresh();

	// Must be first
	render->SceneBegin();

	pRenderContext.GetFrom( materials );
	pRenderContext->TurnOnToneMapping();
	pRenderContext.SafeRelease();

	// clear happens here probably
	SetupMain3DView( worldView, nClearFlags );

	SetupCurrentView( worldView.origin, worldView.angles, VIEW_MAIN );

	unsigned int visFlags;
	SetupVis( worldView, visFlags, NULL );

	ProcessDeferredGlobals( worldView );
	GetLightingManager()->LightSetup( worldView );

	// Force it to clear the framebuffer if they're in solid space.
	if ( ( nClearFlags & VIEW_CLEAR_COLOR ) == 0 )
	{
		if ( enginetrace->GetPointContents( worldView.origin ) == CONTENTS_SOLID )
		{
			nClearFlags |= VIEW_CLEAR_COLOR;
		}
	}

	// Force tone-mapping scale to neutral before the deferred pipeline.
	// The deferred system manages its own ambient / brightness through CDefLightGlobal
	// and does not rely on the engine's auto-exposure (cLightScale register c30).
	// Leaving the engine's auto-exposure active would multiply the composite output
	// by a frame-lagged HDR scalar, which is incorrect for the deferred pipeline and
	// causes the scene to go black at night.
	{
		CMatRenderContextPtr pRC( materials );
		pRC->SetToneMappingScaleLinear( Vector( 1.0f, 1.0f, 1.0f ) );
	}

	// Render world and all entities, particles, etc.
	ViewDrawSceneDeferred( worldView, nClearFlags, VIEW_MAIN, whatToDraw & RENDERVIEW_DRAWVIEWMODEL );

	// We can still use the 'current view' stuff set up in ViewDrawScene
	AllowCurrentViewAccess( true );

	// must happen before teardown
	pLightEditor->OnRender();

	GetLightingManager()->LightTearDown();

	engine->DrawPortals();

	DisableFog();

	// Finish scene
	render->SceneEnd();

	// Draw lightsources if enabled
	//render->DrawLights();

	RenderPlayerSprites();

	// Image-space motion blur
	if ( !building_cubemaps.GetBool() && worldView.m_bDoBloomAndToneMapping ) // We probably should use a different view. variable here
	{
		if ( ( mat_motion_blur_enabled.GetInt() ) && ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 90 ) )
		{
			pRenderContext.GetFrom( materials );
			{
				PIXEVENT( pRenderContext, "DoImageSpaceMotionBlur" );
				DoImageSpaceMotionBlur( worldView );
			}
			pRenderContext.SafeRelease();
		}
	}

	GetClientModeNormal()->DoPostScreenSpaceEffects( &worldView );

	if ( r_deferred_water_debug.GetBool() && m_UnderWaterOverlayMaterial.IsValid() )
	{
		static float s_flNextOverlayPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextOverlayPrint )
		{
			s_flNextOverlayPrint = gpGlobals->realtime + 1.0f;
			Msg( "[water] underwater overlay %s\n", m_UnderWaterOverlayMaterial->GetName() );
		}
	}

	DrawUnderwaterOverlay();

	PixelVisibility_EndScene();

	// Draw fade over entire screen if needed
	byte color[4];
	bool blend;
	vieweffects->GetFadeParams( &color[0], &color[1], &color[2], &color[3], &blend );

	// Draw an overlay to make it even harder to see inside smoke particle systems.
	DrawSmokeFogOverlay();

	// Overlay screen fade on entire screen
	IMaterial* pMaterial = blend ? m_ModulateSingleColor : m_TranslucentSingleColor;
	render->ViewDrawFade( color, pMaterial );
	PerformScreenOverlay( worldView.x, worldView.y, worldView.width, worldView.height );

	// Prevent sound stutter if going slow
	engine->Sound_ExtraUpdate();

	if ( !building_cubemaps.GetBool() && worldView.m_bDoBloomAndToneMapping )
	{
		pRenderContext.GetFrom( materials );
		{
			PIXEVENT( pRenderContext, "DoEnginePostProcessing" );

			bool bFlashlightIsOn = false;
			C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
			if ( pLocal )
			{
				bFlashlightIsOn = pLocal->IsEffectActive( EF_DIMLIGHT );
			}
			DoEnginePostProcessing( worldView.x, worldView.y, worldView.width, worldView.height, bFlashlightIsOn );
		}
		pRenderContext.SafeRelease();
	}

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->CustomPostRender();
	#endif

	// HL2RPM: anti-aliasing of the final frame (the viewmodel included, the HUD not)
	if ( r_deferred_fxaa.GetBool() && !building_cubemaps.GetBool() )
		DrawDeferredFXAA( worldView );

	// And here are the screen-space effects

	if ( IsPC() )
	{
		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "GrabPreColorCorrectedFrame" );

		// Grab the pre-color corrected frame for editing purposes
		engine->GrabPreColorCorrectedFrame( worldView.x, worldView.y, worldView.width, worldView.height );
	}

	PerformScreenSpaceEffects( 0, 0, worldView.width, worldView.height );

	if ( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
	{
		pRenderContext.GetFrom( materials );
		pRenderContext->SetToneMappingScaleLinear(Vector(1,1,1));
		pRenderContext.SafeRelease();
	}

	CleanupMain3DView( worldView );

	pRenderContext = materials->GetRenderContext();
	pRenderContext->SetRenderTarget( saveRenderTarget );
	pRenderContext.SafeRelease();

	// Draw the overlay
	if ( m_bDrawOverlay )
	{
		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "DrawOverlay" );

		// This allows us to be ok if there are nested overlay views
		const CViewSetup currentView = m_CurrentView;
		CViewSetup tempView = m_OverlayViewSetup;
		tempView.fov = ScaleFOVByWidthRatio( tempView.fov, tempView.m_flAspectRatio / ( 4.0f / 3.0f ) );
		tempView.m_bDoBloomAndToneMapping = false;	// FIXME: Hack to get Mark up and running
		m_bDrawOverlay = false;
		RenderView( tempView, m_OverlayClearFlags, m_OverlayDrawFlags );
		m_CurrentView = currentView;
	}

	if ( mat_viewportupscale.GetBool() && mat_viewportscale.GetFloat() < 1.0f )
	{
		#pragma warning(suppress: 4456)
		CMatRenderContextPtr pRenderContext( materials );

		ITexture	*pFullFrameFB1 = materials->FindTexture( "_rt_FullFrameFB1", TEXTURE_GROUP_RENDER_TARGET );
		IMaterial	*pCopyMaterial = materials->FindMaterial( "dev/upscale", TEXTURE_GROUP_OTHER );
		pCopyMaterial->IncrementReferenceCount();

		Rect_t	DownscaleRect, UpscaleRect;

		DownscaleRect.x = worldView.x;
		DownscaleRect.y = worldView.y;
		DownscaleRect.width = worldView.width;
		DownscaleRect.height = worldView.height;

		UpscaleRect.x = worldView.m_nUnscaledX;
		UpscaleRect.y = worldView.m_nUnscaledY;
		UpscaleRect.width = worldView.m_nUnscaledWidth;
		UpscaleRect.height = worldView.m_nUnscaledHeight;

		pRenderContext->CopyRenderTargetToTextureEx( pFullFrameFB1, 0, &DownscaleRect, &DownscaleRect );
		pRenderContext->DrawScreenSpaceRectangle( pCopyMaterial, UpscaleRect.x, UpscaleRect.y, UpscaleRect.width, UpscaleRect.height,
			DownscaleRect.x, DownscaleRect.y, DownscaleRect.x+DownscaleRect.width-1, DownscaleRect.y+DownscaleRect.height-1,
			pFullFrameFB1->GetActualWidth(), pFullFrameFB1->GetActualHeight() );

		pCopyMaterial->DecrementReferenceCount();
	}

	// if we're in VR mode we might need to override the render target
	if( UseVR() )
	{
		saveRenderTarget = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(worldView.m_eStereoEye - 1), ISourceVirtualReality::RT_Color );
	}

	// Draw the 2D graphics
	render->Push2DView( worldView, 0, saveRenderTarget, GetFrustum() );

	Render2DEffectsPreHUD( worldView );

	if ( whatToDraw & RENDERVIEW_DRAWHUD )
	{
		VPROF_BUDGET( "VGui_DrawHud", VPROF_BUDGETGROUP_OTHER_VGUI );
		int viewWidth = worldView.m_nUnscaledWidth;
		int viewHeight = worldView.m_nUnscaledHeight;
		int viewActualWidth = worldView.m_nUnscaledWidth;
		int viewActualHeight = worldView.m_nUnscaledHeight;
		int viewX = worldView.m_nUnscaledX;
		int viewY = worldView.m_nUnscaledY;
		int viewFramebufferX = 0;
		int viewFramebufferY = 0;
		int viewFramebufferWidth = viewWidth;
		int viewFramebufferHeight = viewHeight;
		bool bClear = false;
		bool bPaintMainMenu = false;
		ITexture *pTexture = NULL;
		if( UseVR() )
		{
			if( g_ClientVirtualReality.ShouldRenderHUDInWorld() )
			{
				pTexture = materials->FindTexture( "_rt_gui", NULL, false );
				if( pTexture )
				{
					bPaintMainMenu = true;
					bClear = true;
					viewX = 0;
					viewY = 0;
					viewActualWidth = pTexture->GetActualWidth();
					viewActualHeight = pTexture->GetActualHeight();

					vgui::surface()->GetScreenSize( viewWidth, viewHeight );

					viewFramebufferX = 0;
					if( worldView.m_eStereoEye == STEREO_EYE_RIGHT && !saveRenderTarget )
						viewFramebufferX = viewFramebufferWidth;
					viewFramebufferY = 0;
				}
			}
			else
			{
				viewFramebufferX = worldView.m_eStereoEye == STEREO_EYE_RIGHT ? viewWidth : 0;
				viewFramebufferY = 0;
			}
		}

		// Get the render context out of materials to avoid some debug stuff.
		// WARNING THIS REQUIRES THE .SafeRelease below or it'll never release the ref
		pRenderContext = materials->GetRenderContext();

		// clear depth in the backbuffer before we push the render target
		if( bClear )
		{
			pRenderContext->ClearBuffers( false, true, true );
		}

		// constrain where VGUI can render to the view
		pRenderContext->PushRenderTargetAndViewport( pTexture, NULL, viewX, viewY, viewActualWidth, viewActualHeight );
		// If drawing off-screen, force alpha for that pass
		if (pTexture)
		{
			pRenderContext->OverrideAlphaWriteEnable( true, true );
		}

		// let vgui know where to render stuff for the forced-to-framebuffer panels
		if( UseVR() )
		{
			g_pMatSystemSurface->SetFullscreenViewportAndRenderTarget( viewFramebufferX, viewFramebufferY, viewFramebufferWidth, viewFramebufferHeight, saveRenderTarget );
		}

		// clear the render target if we need to
		if( bClear )
		{
			pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}
		pRenderContext.SafeRelease();

		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "VGui_DrawHud", __FUNCTION__ );

		// paint the vgui screen
		VGui_PreRender();

		// Make sure the client .dll root panel is at the proper point before doing the "SolveTraverse" calls
		vgui::VPANEL root = enginevgui->GetPanel( PANEL_CLIENTDLL );
		if ( root != 0 )
		{
			vgui::ipanel()->SetSize( root, viewWidth, viewHeight );
		}
		// Same for client .dll tools
		root = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
		if ( root != 0 )
		{
			vgui::ipanel()->SetSize( root, viewWidth, viewHeight );
		}

		// The crosshair, etc. needs to get at the current setup stuff
		AllowCurrentViewAccess( true );

		// Draw the in-game stuff based on the actual viewport being used
		render->VGui_Paint( PAINT_INGAMEPANELS );

		// maybe paint the main menu and cursor too if we're in stereo hud mode
		if( bPaintMainMenu )
			render->VGui_Paint( PAINT_UIPANELS | PAINT_CURSOR );

		AllowCurrentViewAccess( false );

		VGui_PostRender();

		g_pClientMode->PostRenderVGui();
		pRenderContext = materials->GetRenderContext();
		if (pTexture)
		{
			pRenderContext->OverrideAlphaWriteEnable( false, true );
		}
		pRenderContext->PopRenderTargetAndViewport();

		if ( UseVR() )
		{
			// figure out if we really want to draw the HUD based on freeze cam
			C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
			bool bInFreezeCam = ( pPlayer && pPlayer->GetObserverMode() == OBS_MODE_FREEZECAM );

			// draw the HUD after the view model so its "I'm closer" depth queues work right.
			if( !bInFreezeCam && g_ClientVirtualReality.ShouldRenderHUDInWorld() )
			{
				// Now we've rendered the HUD to its texture, actually get it on the screen.
				// Since we're drawing it as a 3D object, we need correctly set up frustum, etc.
				int ClearFlags = 0;
				SetupMain3DView( worldView, ClearFlags );

				// TODO - a bit of a shonky test - basically trying to catch the main menu, the briefing screen, the loadout screen, etc.
				bool bTranslucent = !g_pMatSystemSurface->IsCursorVisible();
				g_ClientVirtualReality.RenderHUDQuad( g_pClientMode->ShouldBlackoutAroundHUD(), bTranslucent );
				CleanupMain3DView( worldView );
			}
		}

		pRenderContext->Flush();
		pRenderContext.SafeRelease();
	}

	CDebugViewRender::Draw2DDebuggingInfo( worldView );

	Render2DEffectsPostHUD( worldView );

	// We can no longer use the 'current view' stuff set up in ViewDrawScene
	AllowCurrentViewAccess( false );

	if ( IsPC() )
	{
		CDebugViewRender::GenerateOverdrawForTesting();
	}

	render->PopView( GetFrustum() );
	FlushWorldLists();

	m_CurrentView = worldView;
}

void CDeferredViewRender::ProcessDeferredGlobals( const CViewSetup &view )
{
	VMatrix matPerspective, matView, matViewProj, screen2world;
	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, view.angles );

	MatrixSourceToDeviceSpace( matView );

	#ifdef SHADEREDITOR
	g_ShaderEditorSystem->SetMainViewMatrix( matView );
	#endif

	matView = matView.Transpose3x3();
	Vector viewPosition;

	Vector3DMultiply( matView, view.origin, viewPosition );
	matView.SetTranslation( -viewPosition );
	MatrixBuildPerspectiveX( matPerspective, view.fov, view.m_flAspectRatio,
		view.zNear, view.zFar );
	MatrixMultiply( matPerspective, matView, matViewProj );

	MatrixInverseGeneral( matViewProj, screen2world );

	GetLightingManager()->SetRenderConstants( screen2world, view );

	Vector frustum_c0, frustum_cc, frustum_1c;
	float projDistance = 1.0f;
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,projDistance,projDistance), frustum_c0 );
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,0,projDistance), frustum_cc );
	Vector3DMultiplyPositionProjective( screen2world, Vector(projDistance,0,projDistance), frustum_1c );

	frustum_c0 -= view.origin;
	frustum_cc -= view.origin;
	frustum_1c -= view.origin;

	Vector frustum_up = frustum_c0 - frustum_cc;
	Vector frustum_right = frustum_1c - frustum_cc;

	frustum_cc /= view.zFar;
	frustum_right /= view.zFar;
	frustum_up /= view.zFar;

	Vector fwd;
	AngleVectors( view.angles, &fwd );

	VMatrix frustumDeltas;
	frustumDeltas.Identity();
	frustumDeltas.SetBasisVectors( frustum_cc, frustum_right, frustum_up );

	// HL2RPM: world -> screen texture coords of this (main) view, for the planar
	// reflection views that look their lighting up in its light buffer
	{
		VMatrix matScaleBias;
		MatrixBuildScale( matScaleBias, 0.5f, -0.5f, 1.0f );
		matScaleBias[0][3] = matScaleBias[1][3] = 0.5f;
		VMatrix matWorldToScreenTex;
		MatrixMultiply( matScaleBias, matViewProj, matWorldToScreenTex );
		QUEUE_FIRE( CommitMainViewToScreenTex, matWorldToScreenTex );
	}

#if DEFCFG_BILATERAL_DEPTH_TEST
	VMatrix matWorldToCameraDepthTex;
	MatrixBuildScale( matWorldToCameraDepthTex, 0.5f, -0.5f, 1.0f );
	matWorldToCameraDepthTex[0][3] = matWorldToCameraDepthTex[1][3] = 0.5f;
	MatrixMultiply( matWorldToCameraDepthTex, matViewProj, matWorldToCameraDepthTex );

	QUEUE_FIRE( CommitCommonData, view.origin, fwd, view.zNear, view.zFar, frustumDeltas.Transpose3x3(), matWorldToCameraDepthTex.Transpose() );
#else
	QUEUE_FIRE( CommitCommonData, view.origin, fwd, view.zNear, view.zFar, frustumDeltas.Transpose3x3() );
#endif
}

IMesh *CDeferredViewRender::GetRadiosityScreenGrid( const int iCascade )
{
	if ( m_pMesh_RadiosityScreenGrid[iCascade] == NULL )
	{
		Assert( m_pMesh_RadiosityScreenGrid[iCascade] == NULL );

		const bool bFar = iCascade == 1;

		m_pMesh_RadiosityScreenGrid[iCascade] = CreateRadiosityScreenGrid(
			Vector2D( 0, (bFar?0.5f:0) ),
			bFar ? RADIOSITY_BUFFER_GRID_STEP_SIZE_FAR : RADIOSITY_BUFFER_GRID_STEP_SIZE_CLOSE );
	}

	Assert( m_pMesh_RadiosityScreenGrid[iCascade] != NULL );

	return m_pMesh_RadiosityScreenGrid[iCascade];
}

IMesh *CDeferredViewRender::CreateRadiosityScreenGrid( const Vector2D &vecViewportBase,
	const float flWorldStepSize )
{
	VertexFormat_t format = VERTEX_POSITION
		| VERTEX_TEXCOORD_SIZE( 0, 4 )
		| VERTEX_TEXCOORD_SIZE( 1, 4 )
		| VERTEX_TANGENT_S;

	const float flTexelGridMargin = 1.5f / RADIOSITY_BUFFER_SAMPLES_XY;
	const float flTexelHalf[2] = { 0.5f / RADIOSITY_BUFFER_VIEWPORT_SX,
		0.5f / RADIOSITY_BUFFER_VIEWPORT_SY };

	const float flLocalCoordSingle = 1.0f / RADIOSITY_BUFFER_GRIDS_PER_AXIS;
	const float flLocalCoords[4][2] = {
		0, 0,
		flLocalCoordSingle, 0,
		flLocalCoordSingle, flLocalCoordSingle,
		0, flLocalCoordSingle,
	};

	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pRet = pRenderContext->CreateStaticMesh(
		format, TEXTURE_GROUP_OTHER );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pRet, MATERIAL_QUADS,
		RADIOSITY_BUFFER_GRIDS_PER_AXIS * RADIOSITY_BUFFER_GRIDS_PER_AXIS );

	float flGridOrigins[RADIOSITY_BUFFER_SAMPLES_Z][2];
	for ( int i = 0; i < RADIOSITY_BUFFER_SAMPLES_Z; i++ )
	{
		int x = i % RADIOSITY_BUFFER_GRIDS_PER_AXIS;
		int y = i / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

		flGridOrigins[i][0] = x * flLocalCoordSingle + flTexelHalf[0];
		flGridOrigins[i][1] = y * flLocalCoordSingle + flTexelHalf[1];
	}

	const float flGridSize = flWorldStepSize * RADIOSITY_BUFFER_SAMPLES_XY;
	const float flLocalGridSize[4][2] = {
		0, 0,
		flGridSize, 0,
		flGridSize, flGridSize,
		0, flGridSize,
	};
	const float flLocalGridLimits[4][2] = {
		-flTexelGridMargin, -flTexelGridMargin,
		1 + flTexelGridMargin, -flTexelGridMargin,
		1 + flTexelGridMargin, 1 + flTexelGridMargin,
		-flTexelGridMargin, 1 + flTexelGridMargin,
	};

	for ( int x = 0; x < RADIOSITY_BUFFER_GRIDS_PER_AXIS; x++ )
	{
		for ( int y = 0; y < RADIOSITY_BUFFER_GRIDS_PER_AXIS; y++ )
		{
			const int iIndexLocal = x + y * RADIOSITY_BUFFER_GRIDS_PER_AXIS;
			const int iIndicesOne[2] = { Min( RADIOSITY_BUFFER_SAMPLES_Z - 1, iIndexLocal + 1 ), Max( 0, iIndexLocal - 1 ) };

			for ( int q = 0; q < 4; q++ )
			{
				meshBuilder.Position3f(
					(x * flLocalCoordSingle + flLocalCoords[q][0]) * 2 - flLocalCoordSingle * RADIOSITY_BUFFER_GRIDS_PER_AXIS,
					flLocalCoordSingle * RADIOSITY_BUFFER_GRIDS_PER_AXIS - (y * flLocalCoordSingle + flLocalCoords[q][1]) * 2,
					0 );

				meshBuilder.TexCoord4f( 0,
					(flGridOrigins[iIndexLocal][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndexLocal][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y,
					flLocalGridLimits[q][0],
					flLocalGridLimits[q][1] );

				meshBuilder.TexCoord4f( 1,
					(flGridOrigins[iIndicesOne[0]][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndicesOne[0]][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y,
					(flGridOrigins[iIndicesOne[1]][0] + flLocalCoords[q][0]) * RADIOSITY_UVRATIO_X + vecViewportBase.x,
					(flGridOrigins[iIndicesOne[1]][1] + flLocalCoords[q][1]) * RADIOSITY_UVRATIO_Y + vecViewportBase.y );

				meshBuilder.TangentS3f( flLocalGridSize[q][0],
					flLocalGridSize[q][1],
					iIndexLocal * flWorldStepSize );

				meshBuilder.AdvanceVertex();
			}
		}
	}

	meshBuilder.End();

	return pRet;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CBaseWaterViewDeferred::CalcWaterEyeAdjustments( const VisibleFogVolumeInfo_t &fogInfo,
											 float &newWaterHeight, float &waterZAdjust, bool bSoftwareUserClipPlane )
{
	if( !bSoftwareUserClipPlane )
	{
		newWaterHeight = fogInfo.m_flWaterHeight;
		waterZAdjust = 0.0f;
		return;
	}

	newWaterHeight = fogInfo.m_flWaterHeight;
	float eyeToWaterZDelta = origin[2] - fogInfo.m_flWaterHeight;
	static ConVarRef r_eyewaterepsilon( "r_eyewaterepsilon" );
	float epsilon = r_eyewaterepsilon.IsValid() ? r_eyewaterepsilon.GetFloat() : 10.0f;
	waterZAdjust = 0.0f;
	if( fabs( eyeToWaterZDelta ) < epsilon )
	{
		if( eyeToWaterZDelta > 0 )
		{
			newWaterHeight = origin[2] - epsilon;
		}
		else
		{
			newWaterHeight = origin[2] + epsilon;
		}
		waterZAdjust = newWaterHeight - fogInfo.m_flWaterHeight;
	}

	//	Warning( "view.origin[2]: %f newWaterHeight: %f fogInfo.m_flWaterHeight: %f waterZAdjust: %f\n",
	//		( float )view.origin[2], newWaterHeight, fogInfo.m_flWaterHeight, waterZAdjust );
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CBaseWaterViewDeferred::CSoftwareIntersectionView::Setup( bool bAboveWater )
{
	BaseClass::Setup( *GetOuter() );

	m_DrawFlags = 0;
	m_DrawFlags = ( bAboveWater ) ? DF_RENDER_UNDERWATER : DF_RENDER_ABOVEWATER;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CBaseWaterViewDeferred::CSoftwareIntersectionView::Draw()
{
	PushComposite();
	DrawSetup( GetOuter()->m_waterHeight, m_DrawFlags, GetOuter()->m_waterZAdjust );
	DrawExecute( GetOuter()->m_waterHeight, CurrentViewID(), GetOuter()->m_waterZAdjust );
	PopComposite();
}

//-----------------------------------------------------------------------------
// Draws the scene when the view point is above the level of the water
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::Setup( const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& waterInfo )
{
	BaseClass::Setup( view );

	m_bSoftwareUserClipPlane = g_pMaterialSystemHardwareConfig->UseFastClipping();

	CalcWaterEyeAdjustments( fogInfo, m_waterHeight, m_waterZAdjust, m_bSoftwareUserClipPlane );

	// BROKEN STUFF!
	if ( m_waterZAdjust == 0.0f )
	{
		m_bSoftwareUserClipPlane = false;
	}

	m_DrawFlags = DF_RENDER_ABOVEWATER | DF_DRAW_ENTITITES;
	m_ClearFlags = VIEW_CLEAR_DEPTH;

#ifdef PORTAL
	if( g_pPortalRender->ShouldObeyStencilForClears() )
		m_ClearFlags |= VIEW_CLEAR_OBEY_STENCIL;
#endif

	if ( bDrawSkybox )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	if ( waterInfo.m_bDrawWaterSurface )
	{
		m_DrawFlags |= DF_RENDER_WATER;
	}
	if ( !waterInfo.m_bRefract && !waterInfo.m_bOpaqueWater )
	{
		m_DrawFlags |= DF_RENDER_UNDERWATER;
	}

	m_fogInfo = fogInfo;
	m_waterInfo = waterInfo;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_EyeAboveWater" );

	// eye is outside of water

	CMatRenderContextPtr pRenderContext( materials );

	// render the reflection
	if( m_waterInfo.m_bReflect )
	{
		m_ReflectionView.Setup( m_waterInfo.m_bReflectEntities );
		m_pMainView->AddViewToScene( &m_ReflectionView );
	}

	bool bViewIntersectsWater = false;

	// render refraction
	if ( m_waterInfo.m_bRefract )
	{
		m_RefractionView.Setup();
		m_pMainView->AddViewToScene( &m_RefractionView );

		if( !m_bSoftwareUserClipPlane )
		{
			bViewIntersectsWater = DoesViewPlaneIntersectWater( m_fogInfo.m_flWaterHeight, m_fogInfo.m_nVisibleFogVolume );
		}
	}
	else if ( !( m_DrawFlags & DF_DRAWSKYBOX ) )
	{
		m_ClearFlags |= VIEW_CLEAR_COLOR;
	}

#ifdef PORTAL
	if( g_pPortalRender->ShouldObeyStencilForClears() )
		m_ClearFlags |= VIEW_CLEAR_OBEY_STENCIL;
#endif

	// NOTE!!!!!  YOU CAN ONLY DO THIS IF YOU HAVE HARDWARE USER CLIP PLANES!!!!!!
	bool bHardwareUserClipPlanes = !g_pMaterialSystemHardwareConfig->UseFastClipping();
	if( bViewIntersectsWater && bHardwareUserClipPlanes )
	{
		// This is necessary to keep the non-water fogged world from drawing underwater in
		// the case where we want to partially see into the water.
		m_DrawFlags |= DF_CLIP_Z | DF_CLIP_BELOW;
	}

	// render the world
	PushComposite();
	DrawSetup( m_waterHeight, m_DrawFlags, m_waterZAdjust );
	EnableWorldFog();
	DrawExecute( m_waterHeight, CurrentViewID(), m_waterZAdjust );
	PopComposite();

	if ( m_waterInfo.m_bRefract )
	{
		if ( m_bSoftwareUserClipPlane )
		{
			m_SoftwareIntersectionView.Setup( true );
			m_SoftwareIntersectionView.Draw( );
		}
		else if ( bViewIntersectsWater )
		{
			m_IntersectionView.Setup();
			m_pMainView->AddViewToScene( &m_IntersectionView );
		}
	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CReflectionView::Setup( bool bReflectEntities )
{
	BaseClass::Setup( *GetOuter() );

	m_ClearFlags = VIEW_CLEAR_DEPTH;

	// NOTE: Clearing the color is unnecessary since we're drawing the skybox
	// and dest-alpha is never used in the reflection
	m_DrawFlags = DF_RENDER_REFLECTION | DF_CLIP_Z | DF_CLIP_BELOW |
		DF_RENDER_ABOVEWATER;

	// NOTE: This will cause us to draw the 2d skybox in the reflection
	// (which we want to do instead of drawing the 3d skybox)
	m_DrawFlags |= DF_DRAWSKYBOX;

	if( bReflectEntities )
	{
		m_DrawFlags |= DF_DRAW_ENTITITES;
	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CReflectionView::Draw()
{
#ifdef PORTAL
	g_pPortalRender->WaterRenderingHandler_PreReflection();
#endif

	// Store off view origin and angles and set the new view
	const view_id_t nSaveViewID = CurrentViewID();
	const Vector vecSaveOrigin = CurrentViewOrigin();
	const QAngle angSaveAngles = CurrentViewAngles();
	SetupCurrentView( origin, angles, VIEW_REFLECTION );

	// Disable occlusion visualization in reflection
	static ConVarRef r_visocclusion( "r_visocclusion" );
	const int bVisOcclusion = r_visocclusion.IsValid() ? r_visocclusion.GetInt() : 0;
	if ( r_visocclusion.IsValid() )
		r_visocclusion.SetValue( 0 );

	PushComposite();

	DrawSetup( GetOuter()->m_fogInfo.m_flWaterHeight, m_DrawFlags, 0.0f, GetOuter()->m_fogInfo.m_nVisibleFogVolumeLeaf );

	// HL2RPM: the composite shaders take the lighting from the main view's light buffer;
	// in a reflection they have to find the reflected point there first (REFLECTVIEW).
	// The screen-space clouds belong to the main view too: the sky in the reflection
	// shows the clear atmosphere only.
	const bool bCloudsValid = WeatherRender_IsCloudTextureValid();
	if ( bCloudsValid )
		WeatherRender_SetCloudTextureValid( false );
	const bool bReflectionOn = true;
	QUEUE_FIRE( CommitReflectionView, bReflectionOn );

	EnableWorldFog();
	DrawExecute( GetOuter()->m_fogInfo.m_flWaterHeight, VIEW_REFLECTION, 0.0f );

	const bool bReflectionOff = false;
	QUEUE_FIRE( CommitReflectionView, bReflectionOff );
	if ( bCloudsValid )
		WeatherRender_SetCloudTextureValid( true );

	PopComposite();

	if ( r_visocclusion.IsValid() )
		r_visocclusion.SetValue( bVisOcclusion );

#ifdef PORTAL
	// deal with stencil
	g_pPortalRender->WaterRenderingHandler_PostReflection();
#endif

	// finish off the view and restore the previous view.
	SetupCurrentView( vecSaveOrigin, angSaveAngles, nSaveViewID );

	// This is here for multithreading
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Flush();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CRefractionView::Setup()
{
	BaseClass::Setup( *GetOuter() );

	m_ClearFlags = VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH;

	m_DrawFlags = DF_RENDER_REFRACTION | DF_CLIP_Z |
		DF_RENDER_UNDERWATER | DF_FUDGE_UP |
		DF_DRAW_ENTITITES ;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CRefractionView::Draw()
{
#ifdef PORTAL
	g_pPortalRender->WaterRenderingHandler_PreRefraction();
#endif

	// Store off view origin and angles and set the new view
	int nSaveViewID = CurrentViewID();
	SetupCurrentView( origin, angles, VIEW_REFRACTION );

	PushComposite();

	DrawSetup( GetOuter()->m_waterHeight, m_DrawFlags, GetOuter()->m_waterZAdjust );

	SetFogVolumeState( GetOuter()->m_fogInfo, true );
	SetClearColorToFogColor();
	DrawExecute( GetOuter()->m_waterHeight, VIEW_REFRACTION, GetOuter()->m_waterZAdjust );

	PopComposite();

#ifdef PORTAL
	// deal with stencil
	g_pPortalRender->WaterRenderingHandler_PostRefraction();
#endif

	// finish off the view.  restore the previous view.
	SetupCurrentView( origin, angles, ( view_id_t )nSaveViewID );

	// This is here for multithreading
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );
	pRenderContext->Flush();
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CIntersectionView::Setup()
{
	BaseClass::Setup( *GetOuter() );
	m_DrawFlags = DF_RENDER_UNDERWATER | DF_CLIP_Z | DF_DRAW_ENTITITES;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CAboveWaterViewDeferred::CIntersectionView::Draw()
{
	PushComposite();

	DrawSetup( GetOuter()->m_fogInfo.m_flWaterHeight, m_DrawFlags, 0 );

	SetFogVolumeState( GetOuter()->m_fogInfo, true );
	SetClearColorToFogColor( );
	DrawExecute( GetOuter()->m_fogInfo.m_flWaterHeight, VIEW_NONE, 0 );
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

	PopComposite();
}


//-----------------------------------------------------------------------------
// Draws the scene when the view point is under the level of the water
//-----------------------------------------------------------------------------
void CUnderWaterViewDeferred::Setup( const CViewSetup &view, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& waterInfo )
{
	BaseClass::Setup( view );

	m_bSoftwareUserClipPlane = g_pMaterialSystemHardwareConfig->UseFastClipping();

	CalcWaterEyeAdjustments( fogInfo, m_waterHeight, m_waterZAdjust, m_bSoftwareUserClipPlane );

	IMaterial *pWaterMaterial = fogInfo.m_pFogVolumeMaterial;
	SetUnderwaterOverlay( m_pMainView, pWaterMaterial );
	// NOTE: We're not drawing the 2d skybox under water since it's assumed to not be visible.

	// render the world underwater
	// Clear the color to get the appropriate underwater fog color
	m_DrawFlags = DF_FUDGE_UP | DF_RENDER_UNDERWATER | DF_DRAW_ENTITITES;
	m_ClearFlags = VIEW_CLEAR_DEPTH;

	if( !m_bSoftwareUserClipPlane )
	{
		m_DrawFlags |= DF_CLIP_Z;
	}
	if ( waterInfo.m_bDrawWaterSurface )
	{
		m_DrawFlags |= DF_RENDER_WATER;
	}
	if ( !waterInfo.m_bRefract && !waterInfo.m_bOpaqueWater )
	{
		m_DrawFlags |= DF_RENDER_ABOVEWATER;
	}

	m_fogInfo = fogInfo;
	m_waterInfo = waterInfo;
	m_bDrawSkybox = bDrawSkybox;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CUnderWaterViewDeferred::Draw()
{
	// FIXME: The 3d skybox shouldn't be drawn when the eye is under water

	VPROF( "CViewRender::ViewDrawScene_EyeUnderWater" );

	CMatRenderContextPtr pRenderContext( materials );

	// render refraction (out of water)
	if ( m_waterInfo.m_bRefract )
	{
		m_RefractionView.Setup( );
		m_pMainView->AddViewToScene( &m_RefractionView );
	}

	if ( !m_waterInfo.m_bRefract )
	{
		SetFogVolumeState( m_fogInfo, true );
		unsigned char ucFogColor[3];
		pRenderContext->GetFogColor( ucFogColor );
		pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
	}

	PushComposite();

	DrawSetup( m_waterHeight, m_DrawFlags, m_waterZAdjust );
	SetFogVolumeState( m_fogInfo, false );
	DrawExecute( m_waterHeight, CurrentViewID(), m_waterZAdjust );
	m_ClearFlags = 0;

	PopComposite();

	if( m_waterZAdjust != 0.0f && m_bSoftwareUserClipPlane && m_waterInfo.m_bRefract )
	{
		m_SoftwareIntersectionView.Setup( false );
		m_SoftwareIntersectionView.Draw( );
	}
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

}



//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CUnderWaterViewDeferred::CRefractionView::Setup()
{
	BaseClass::Setup( *GetOuter() );
	// NOTE: Refraction renders into the back buffer, over the top of the 3D skybox
	// It is then blitted out into the refraction target. This is so that
	// we only have to set up 3d sky vis once, and only render it once also!
	m_DrawFlags = DF_CLIP_Z |
		DF_CLIP_BELOW | DF_RENDER_ABOVEWATER |
		DF_DRAW_ENTITITES;

	m_ClearFlags = VIEW_CLEAR_DEPTH;
	if ( GetOuter()->m_bDrawSkybox )
	{
		m_ClearFlags |= VIEW_CLEAR_COLOR;
		m_DrawFlags |= DF_DRAWSKYBOX | DF_CLIP_SKYBOX;
	}
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CUnderWaterViewDeferred::CRefractionView::Draw()
{
	CMatRenderContextPtr pRenderContext( materials );
	SetFogVolumeState( GetOuter()->m_fogInfo, true );
	unsigned char ucFogColor[3];
	pRenderContext->GetFogColor( ucFogColor );
	pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );

	PushComposite();

	DrawSetup( GetOuter()->m_waterHeight, m_DrawFlags, GetOuter()->m_waterZAdjust );

	EnableWorldFog();
	DrawExecute( GetOuter()->m_waterHeight, VIEW_REFRACTION, GetOuter()->m_waterZAdjust );

	PopComposite();

	Rect_t srcRect;
	srcRect.x = x;
	srcRect.y = y;
	srcRect.width = width;
	srcRect.height = height;

	ITexture *pTexture = GetWaterRefractionTexture();
	pRenderContext->CopyRenderTargetToTextureEx( pTexture, 0, &srcRect, NULL );
}

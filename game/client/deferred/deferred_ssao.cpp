//========= HL2RPM ============================================================//
//
// Purpose: Ambient occlusion for the deferred global light.
//
//  - SSAO: half resolution horizon-style occlusion from the G-buffer
//    (deferred_ssao_ps30), depth-aware separable blur (deferred_ssaoblur_ps30).
//  - Sky visibility: the top-down occlusion map of the weather (the rain map)
//    tells how much sky a point sees; under roofs and inside buildings the
//    sky light drops to a dim bounce level instead of lighting interiors like
//    the open street.
//
// Both only affect the sky / bounce light of the global light
// (lightingpass_global_ps30); lamps and the sun keep their own shadows.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_ssao.h"

#include "materialsystem/itexture.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_ssao( "r_deferred_ssao", "1", FCVAR_ARCHIVE, "Screen-space ambient occlusion on the sky light" );
static ConVar r_deferred_ssao_radius( "r_deferred_ssao_radius", "48", FCVAR_ARCHIVE, "SSAO radius (units)" );
static ConVar r_deferred_ssao_intensity( "r_deferred_ssao_intensity", "1.3", FCVAR_ARCHIVE, "SSAO strength" );
static ConVar r_deferred_skyvis( "r_deferred_skyvis", "1", FCVAR_ARCHIVE, "Sky light dims under roofs and indoors (top-down occlusion map)" );
static ConVar r_deferred_skyvis_indoor( "r_deferred_skyvis_indoor", "0.35", FCVAR_ARCHIVE, "Part of the sky light left deep indoors (0..1)" );
static ConVar r_deferred_skyvis_radius( "r_deferred_skyvis_radius", "220", FCVAR_ARCHIVE, "How far around a point the open sky is looked for (units)" );

static CTextureReference g_tex_SSAO;
static CTextureReference g_tex_SSAOBlur;
static IMaterial *g_pMatSSAO = NULL;
static IMaterial *g_pMatSSAOBlurH = NULL;
static IMaterial *g_pMatSSAOBlurV = NULL;

void InitSSAORTs()
{
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	g_tex_SSAO.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_ssao",
		128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
		IMAGE_FORMAT_RGBA8888,
		MATERIAL_RT_DEPTH_NONE,
		flags, 0 ) );

	g_tex_SSAOBlur.Init( materials->CreateNamedRenderTargetTextureEx2(
		"_rt_hl2rpm_ssao_blur",
		128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP,
		IMAGE_FORMAT_RGBA8888,
		MATERIAL_RT_DEPTH_NONE,
		flags, 0 ) );
}

static IMaterial *CreateSSAOMaterial( const char *pszName, const char *pszShader, int iDirection )
{
	KeyValues *pKV = new KeyValues( pszShader );
	if ( iDirection >= 0 )
		pKV->SetInt( "$direction", iDirection );
	IMaterial *pMat = materials->CreateMaterial( pszName, pKV );
	if ( pMat )
		pMat->IncrementReferenceCount();
	return pMat;
}

static bool EnsureSSAOMaterials()
{
	if ( !g_pMatSSAO )
	{
		g_pMatSSAO = CreateSSAOMaterial( "__hl2rpm_ssao", "DEFERRED_SSAO", -1 );
		g_pMatSSAOBlurH = CreateSSAOMaterial( "__hl2rpm_ssao_blur_h", "DEFERRED_SSAOBLUR", 0 );
		g_pMatSSAOBlurV = CreateSSAOMaterial( "__hl2rpm_ssao_blur_v", "DEFERRED_SSAOBLUR", 1 );
	}
	return g_pMatSSAO && g_pMatSSAOBlurH && g_pMatSSAOBlurV &&
		!g_pMatSSAO->IsErrorMaterial() && !g_pMatSSAOBlurH->IsErrorMaterial() && !g_pMatSSAOBlurV->IsErrorMaterial();
}

void ShutdownSSAO()
{
	if ( g_pMatSSAO ) { g_pMatSSAO->DecrementReferenceCount(); g_pMatSSAO = NULL; }
	if ( g_pMatSSAOBlurH ) { g_pMatSSAOBlurH->DecrementReferenceCount(); g_pMatSSAOBlurH = NULL; }
	if ( g_pMatSSAOBlurV ) { g_pMatSSAOBlurV->DecrementReferenceCount(); g_pMatSSAOBlurV = NULL; }
}

bool DeferredSSAO_WantsSkyVisibility()
{
	return r_deferred_skyvis.GetBool();
}

void DeferredSSAO_Render( const CViewSetup &view )
{
	ssaoData_t data;

	const bool bAO = r_deferred_ssao.GetBool() && g_tex_SSAO.IsValid() && g_tex_SSAOBlur.IsValid() && EnsureSSAOMaterials();
	const int w = Max( 1, view.width / 2 );
	const int h = Max( 1, view.height / 2 );
	const float flUVScale = bAO ? (float)w / Max( 1, g_tex_SSAO->GetActualWidth() ) : 0.0f;

	data.bEnabled = bAO;
	data.vecApply.Init( r_deferred_skyvis.GetBool() ? 1.0f : 0.0f,
		clamp( r_deferred_skyvis_indoor.GetFloat(), 0.0f, 1.0f ),
		Max( 16.0f, r_deferred_skyvis_radius.GetFloat() ),
		flUVScale );

	if ( bAO )
	{
		const float flRadius = Max( 4.0f, r_deferred_ssao_radius.GetFloat() );
		// AO pixels per world unit at view depth 1 (view.fov is the horizontal field of view)
		const float flProjScale = ( w * 0.5f ) / tanf( DEG2RAD( clamp( view.fov, 10.0f, 170.0f ) ) * 0.5f );
		data.vecParams0.Init( flRadius, Max( 0.0f, r_deferred_ssao_intensity.GetFloat() ), 0.08f, flProjScale );
		data.vecParams1.Init( 1.0f / w, 1.0f / h, h * 0.15f, (float)( gpGlobals->framecount & 63 ) );
		data.vecBlurH.Init( 1.0f / w, 0.0f, 0.04f, flUVScale );
		data.vecBlurV.Init( 0.0f, 1.0f / h, 0.04f, flUVScale );
	}

	QUEUE_FIRE( CommitSSAOData, data );

	ITexture *pAO = g_tex_SSAO;
	ITexture *pBlur = g_tex_SSAOBlur;
	QUEUE_FIRE( CommitTexture_SSAO, pAO, pBlur );

	if ( !bAO )
		return;

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->PushRenderTargetAndViewport( g_tex_SSAO, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatSSAO, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_SSAOBlur, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatSSAOBlurH, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_SSAO, 0, 0, w, h );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatSSAOBlurV, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
	pRenderContext->PopRenderTargetAndViewport();
}

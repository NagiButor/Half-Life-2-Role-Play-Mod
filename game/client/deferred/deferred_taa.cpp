//========= HL2RPM ============================================================//
//
// Purpose: Temporal anti-aliasing (TAA) for the deferred renderer.
//
// The deferred renderer has no working MSAA, and FXAA only smooths the edges it can
// find in one frame: thin geometry (wires, railings, the ribs of corrugated metal,
// foliage), shiny bumps and the noise of the screen space effects kept crawling
// while the player moved. TAA spreads the anti-aliasing over time:
//  - every frame the main view's projection is shifted by a sub-pixel offset
//    (Halton 2, 3 over 8 frames; CViewSetup's off-center frustum - the engine
//    builds the projection of every view copied from it, the 3D skybox and the
//    weapon included, and ProcessDeferredGlobals the deferred passes' rays);
//  - hl2rpm_taa_ps30 blends the frame into the history: the result of the earlier
//    frames, taken from where the surface was in the last frame (the camera's move
//    and turn applied to the G-buffer depth). What moves with the camera - the
//    first person weapon and body - is marked in the stencil and taken from the
//    same pixel instead. History that doesn't fit any more is clipped into the
//    colors of the pixel's neighbourhood in this frame;
//  - the pass draws straight into the frame: with MSAA on (mat_antialias), the
//    frame's depth-stencil - where the weapon's marks are - is not the one render
//    targets get. The result is then copied into the history: two targets in the
//    frame's format, used in turn (the output is dithered for their 8 bits).
// It replaces FXAA (r_deferred_aa 2) and runs in its place, before the
// post-processing (whose sharpening takes back the little softness TAA leaves).
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_taa.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/materialsystem_config.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_aa( "r_deferred_aa", "2", FCVAR_ARCHIVE,
	"Anti-aliasing of the deferred renderer: 0 none, 1 FXAA, 2 TAA (temporal: also steadies thin details and shimmering)", true, 0.0f, true, 2.0f );
static ConVar r_deferred_taa_history( "r_deferred_taa_history", "0.92", 0,
	"TAA: weight of the history where it matches this frame (more = smoother and steadier, slower to follow changes)", true, 0.0f, true, 0.98f );
static ConVar r_deferred_taa_history_min( "r_deferred_taa_history_min", "0.75", 0,
	"TAA: weight of the history where it had to be pulled toward this frame (something moved)", true, 0.0f, true, 0.98f );
static ConVar r_deferred_taa_clip( "r_deferred_taa_clip", "1.25", 0,
	"TAA: how far the history may differ from the pixel's neighbourhood (standard deviations): less = less ghosting, more = smoother", true, 0.25f, true, 4.0f );
static ConVar r_deferred_taa_motion( "r_deferred_taa_motion", "24", 0,
	"TAA: motion (pixels per frame) at which the history weight is lowered by a fifth (resampling softens it)", true, 1.0f, true, 1000.0f );
static ConVar r_deferred_taa_jitter( "r_deferred_taa_jitter", "1", 0,
	"TAA: sub-pixel jitter of the projection (0 = none: only temporal smoothing, for comparison)", true, 0.0f, true, 2.0f );
static ConVar r_deferred_taa_mipbias( "r_deferred_taa_mipbias", "-0.5", 0,
	"TAA: mip bias of the textures (negative = sharper): the jitter averages a pixel's area, on top of the texture filtering", true, -2.0f, true, 0.0f );
static ConVar r_deferred_taa_reset_dist( "r_deferred_taa_reset_dist", "160", 0,
	"TAA: a camera jump longer than this (units per frame) drops the history" );
static ConVar r_deferred_taa_stencil_test( "r_deferred_taa_stencil_test", "0", FCVAR_CHEAT,
	"TAA dev: paint the stencil-marked (camera attached) pixels red straight into the frame" );
static ConVar r_deferred_taa_debug( "r_deferred_taa_debug", "0", FCVAR_CHEAT,
	"TAA debug (in red over the grey frame): 1 motion (16 pixels per frame = full; no history = full), 2 how far the history was pulled toward the frame, 3 weapon / body mask, 4 history weight" );

// stencil bit of the pixels drawn moving with the camera (the engine's tone mapping
// occlusion test uses bit 1)
#define TAA_STENCIL_BIT 0x80

#define TAA_JITTER_SAMPLES 8

static CTextureReference g_tex_TAAHistory[2];

// [history read][0 = world, 1 = camera attached][1 = debug pass]
static IMaterial *g_pMatTAA[2][2][2];
static bool g_bMaterialsTried = false;

struct TAAView_t
{
	Vector origin;
	QAngle angles;
	float fov;
	float aspect;
};

static taaData_t s_TAAData;	// what the shaders get (committed at the jitter and at the resolve)
static TAAView_t s_Prev;
static bool s_bPrevValid = false;
static bool s_bReset = true;
static bool s_bFrameActive = false;
static int s_iJitterIndex = 0;
static int s_iHistory = 0;		// target holding the last result
static int s_iHistoryW = 0, s_iHistoryH = 0;

//-----------------------------------------------------------------------------
// Targets and materials
//-----------------------------------------------------------------------------
void InitTAARTs()
{
	// copies of the frame: its size and format (the copy is a plain StretchRect / MSAA resolve)
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;
	for ( int i = 0; i < 2; i++ )
	{
		g_tex_TAAHistory[i].Init( materials->CreateNamedRenderTargetTextureEx2( VarArgs( "_rt_hl2rpm_taa%d", i ), 1, 1,
			RT_SIZE_FULL_FRAME_BUFFER, materials->GetBackBufferFormat(), MATERIAL_RT_DEPTH_NONE, flags, 0 ) );
	}

	// the targets lost their content (device reset)
	s_bReset = true;
}

static IMaterial *MakeMaterial( const char *pszName, KeyValues *pKV )
{
	IMaterial *pMat = materials->CreateMaterial( pszName, pKV );
	if ( pMat )
		pMat->IncrementReferenceCount();
	return pMat;
}

static bool IsOk( IMaterial *pMat )
{
	return pMat && !pMat->IsErrorMaterial();
}

static bool EnsureMaterials()
{
	if ( !g_bMaterialsTried )
	{
		g_bMaterialsTried = true;
		for ( int h = 0; h < 2; h++ )
		{
			for ( int a = 0; a < 2; a++ )
			{
				for ( int d = 0; d < 2; d++ )
				{
					KeyValues *pKV = new KeyValues( "HL2RPM_TAA" );
					pKV->SetInt( "$viewmodel", a );
					pKV->SetInt( "$debugpass", d );
					pKV->SetString( "$historytexture", VarArgs( "_rt_hl2rpm_taa%d", h ) );
					g_pMatTAA[h][a][d] = MakeMaterial( VarArgs( "__hl2rpm_taa%d_%d_%d", h, a, d ), pKV );
				}
			}
		}
	}

	bool bOk = true;
	for ( int h = 0; h < 2; h++ )
		for ( int a = 0; a < 2; a++ )
			for ( int d = 0; d < 2; d++ )
				bOk = bOk && IsOk( g_pMatTAA[h][a][d] );
	return bOk;
}

void DeferredTAA_Shutdown()
{
	for ( int h = 0; h < 2; h++ )
	{
		for ( int a = 0; a < 2; a++ )
		{
			for ( int d = 0; d < 2; d++ )
			{
				if ( g_pMatTAA[h][a][d] ) { g_pMatTAA[h][a][d]->DecrementReferenceCount(); g_pMatTAA[h][a][d] = NULL; }
			}
		}
	}
	g_bMaterialsTried = false;
}

void DeferredTAA_Reset()
{
	s_bReset = true;
}

int DeferredAA_GetMode()
{
	return r_deferred_aa.GetInt();
}

static bool IsTAAAvailable()
{
	return r_deferred_aa.GetInt() == 2 && GetDeferredManager() && GetDeferredManager()->IsDeferredRenderingEnabled()
		&& g_tex_TAAHistory[0].IsValid() && g_tex_TAAHistory[1].IsValid() && EnsureMaterials();
}

CON_COMMAND( r_deferred_taa_info, "TAA: state, history targets and the frame buffer's multisampling" )
{
	const MaterialSystem_Config_t &config = materials->GetCurrentConfigForVideoCard();
	Msg( "[taa] mode %d (r_deferred_aa), active this frame %d, history %dx%d, back buffer format %d\n",
		r_deferred_aa.GetInt(), s_bFrameActive ? 1 : 0, s_iHistoryW, s_iHistoryH, (int)materials->GetBackBufferFormat() );
	Msg( "[taa] material system config: MSAA samples %d, quality %d (mat_antialias): with the deferred renderer MSAA only costs - TAA/FXAA do the anti-aliasing\n",
		config.m_nAASamples, config.m_nAAQuality );
}

//-----------------------------------------------------------------------------
// The engine's MSAA (Options -> Video -> anti-aliasing, mat_antialias) and the
// deferred renderer: the composite reads one light value per pixel, so the edges
// come out as jagged with 8x MSAA as without it, while the multisampled back buffer
// makes the whole frame slower - 18.3 ms instead of 16.2 on a GTX 960M at 1600x900
// (measured 2026-10-04), and every full screen pass and copy of the frame (TAA,
// post-processing) pays for the 8 samples again. It is turned off for the session
// once, at startup; the stored video settings are not written.
//-----------------------------------------------------------------------------
static ConVar r_deferred_disable_msaa( "r_deferred_disable_msaa", "1", FCVAR_ARCHIVE,
	"Turn the engine's MSAA (Video -> anti-aliasing) off while the deferred renderer runs: it smooths nothing there and costs about 2 ms a frame at 8x. TAA / FXAA (r_deferred_aa) do the anti-aliasing." );

void DeferredAA_DisableMSAA()
{
	static bool s_bDone = false;
	if ( s_bDone || !GetDeferredManager() || !GetDeferredManager()->IsDeferredRenderingEnabled() )
		return;
	s_bDone = true;

	const int nSamples = materials->GetCurrentConfigForVideoCard().m_nAASamples;
	if ( nSamples <= 1 )
		return;

	if ( !r_deferred_disable_msaa.GetBool() )
	{
		Msg( "[deferred] MSAA x%d is on (Options -> Video -> anti-aliasing): the deferred renderer gets nothing from it and it costs frame time (r_deferred_disable_msaa 1 turns it off)\n", nSamples );
		return;
	}

	Msg( "[deferred] MSAA x%d smooths nothing in the deferred renderer and costs frame time: off for this session (mat_antialias 0; r_deferred_disable_msaa 0 keeps it). TAA / FXAA do the anti-aliasing.\n", nSamples );
	engine->ClientCmd_Unrestricted( "mat_antialias 0\n" );
}

//-----------------------------------------------------------------------------
// Jitter
//-----------------------------------------------------------------------------
static float Halton( int index, int base )
{
	float f = 1.0f;
	float r = 0.0f;
	while ( index > 0 )
	{
		f /= base;
		r += f * ( index % base );
		index /= base;
	}
	return r;
}

bool DeferredTAA_JitterView( CViewSetup &view, bool bMainView )
{
	s_bFrameActive = false;
	if ( !bMainView || view.m_bOffCenter || view.m_bOrtho || view.width <= 0 || view.height <= 0 || !IsTAAAvailable() )
	{
		// TAA isn't drawn this frame: the history goes stale
		s_bReset = true;
		if ( bMainView && s_TAAData.vecParams3.y != 0.0f )
		{
			s_TAAData.vecParams3.y = 0.0f;
			QUEUE_FIRE( CommitTAAData, s_TAAData );
		}
		return false;
	}

	// the composite samples the textures sharper while the frame is jittered
	s_TAAData.vecParams3.y = r_deferred_taa_mipbias.GetFloat();
	QUEUE_FIRE( CommitTAAData, s_TAAData );

	s_iJitterIndex = ( s_iJitterIndex % TAA_JITTER_SAMPLES ) + 1;
	const float flScale = r_deferred_taa_jitter.GetFloat();
	const float jx = ( Halton( s_iJitterIndex, 2 ) - 0.5f ) * flScale;
	const float jy = ( Halton( s_iJitterIndex, 3 ) - 0.5f ) * flScale;

	// the frustum's 0..1 range covers the view: a shift of 1 / width is one pixel
	view.m_bOffCenter = true;
	view.m_flOffCenterLeft = jx / view.width;
	view.m_flOffCenterRight = 1.0f + jx / view.width;
	view.m_flOffCenterBottom = jy / view.height;
	view.m_flOffCenterTop = 1.0f + jy / view.height;

	s_bFrameActive = true;
	return true;
}

bool DeferredTAA_IsFrameActive()
{
	return s_bFrameActive;
}

void DeferredTAA_MarkCameraAttached( bool bBegin )
{
	if ( !s_bFrameActive )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	if ( bBegin )
	{
		pRenderContext->SetStencilEnable( true );
		pRenderContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilPassOperation( STENCILOPERATION_REPLACE );
		pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_ALWAYS );
		pRenderContext->SetStencilReferenceValue( TAA_STENCIL_BIT );
		pRenderContext->SetStencilTestMask( TAA_STENCIL_BIT );
		pRenderContext->SetStencilWriteMask( TAA_STENCIL_BIT );
	}
	else
	{
		pRenderContext->SetStencilEnable( false );
	}
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------

// rows x, y, w of the unjittered view-projection, for positions relative to the camera
static void BuildViewProjRows( const QAngle &angles, float fov, float aspect, Vector4D *pRows )
{
	VMatrix matView, matPersp, matViewProj;
	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, angles );
	MatrixSourceToDeviceSpace( matView );
	matView = matView.Transpose3x3();

	// (the depth range doesn't matter: only x, y and w are used)
	MatrixBuildPerspectiveX( matPersp, fov, aspect, 4.0f, 4096.0f );
	MatrixMultiply( matPersp, matView, matViewProj );

	pRows[0].Init( matViewProj[0][0], matViewProj[0][1], matViewProj[0][2], matViewProj[0][3] );
	pRows[1].Init( matViewProj[1][0], matViewProj[1][1], matViewProj[1][2], matViewProj[1][3] );
	pRows[2].Init( matViewProj[3][0], matViewProj[3][1], matViewProj[3][2], matViewProj[3][3] );
}

static void SetStencilTest( CMatRenderContextPtr &pRenderContext, bool bCameraAttached )
{
	pRenderContext->SetStencilEnable( true );
	pRenderContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
	pRenderContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
	pRenderContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
	pRenderContext->SetStencilCompareFunction( bCameraAttached ? STENCILCOMPARISONFUNCTION_EQUAL : STENCILCOMPARISONFUNCTION_NOTEQUAL );
	pRenderContext->SetStencilReferenceValue( TAA_STENCIL_BIT );
	pRenderContext->SetStencilTestMask( TAA_STENCIL_BIT );
	pRenderContext->SetStencilWriteMask( 0 );
}

void DeferredTAA_Draw( const CViewSetup &view )
{
	if ( !s_bFrameActive )
		return;
	s_bFrameActive = false;

	ITexture *pFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	if ( !pFrame || pFrame->IsError() || !EnsureMaterials() )
	{
		s_bReset = true;
		return;
	}


	const int vx = view.x, vy = view.y;
	const int vw = Max( 2, view.width ), vh = Max( 2, view.height );
	const int fbw = pFrame->GetActualWidth(), fbh = pFrame->GetActualHeight();
	const int hw = g_tex_TAAHistory[0]->GetActualWidth(), hh = g_tex_TAAHistory[0]->GetActualHeight();
	if ( hw != fbw || hh != fbh || vx + vw > fbw || vy + vh > fbh )
	{
		// the frame doesn't fit the history targets (they follow the frame buffer's size)
		s_bReset = true;
		return;
	}

	TAAView_t cur;
	cur.origin = view.origin;
	cur.angles = view.angles;
	cur.fov = view.fov;
	cur.aspect = view.m_flAspectRatio;

	bool bReset = s_bReset || !s_bPrevValid || hw != s_iHistoryW || hh != s_iHistoryH;
	if ( !bReset && ( s_Prev.origin - cur.origin ).LengthSqr() > Square( r_deferred_taa_reset_dist.GetFloat() ) )
		bReset = true;

	taaData_t &data = s_TAAData;
	const TAAView_t &prev = bReset ? cur : s_Prev;
	BuildViewProjRows( prev.angles, prev.fov, prev.aspect, data.vecPrev );
	BuildViewProjRows( cur.angles, cur.fov, cur.aspect, data.vecCur );
	const Vector vecDelta = prev.origin - cur.origin;
	data.vecCamDelta.Init( vecDelta.x, vecDelta.y, vecDelta.z, 0.0f );
	data.vecParams.Init( r_deferred_taa_history.GetFloat(), r_deferred_taa_history_min.GetFloat(),
		r_deferred_taa_clip.GetFloat(), bReset ? 1.0f : 0.0f );
	data.vecParams2.Init( (float)r_deferred_taa_debug.GetInt(), r_deferred_taa_motion.GetFloat(),
		(float)vw / fbw, (float)vh / fbh );
	data.vecTexel.Init( 1.0f / fbw, 1.0f / fbh, (float)fbw, (float)fbh );
	static int s_iDitherFrame = 0;
	s_iDitherFrame = ( s_iDitherFrame + 1 ) & 1023;
	data.vecParams3.x = (float)s_iDitherFrame;
	QUEUE_FIRE( CommitTAAData, data );

	CMatRenderContextPtr pRenderContext( materials );

	if ( r_deferred_taa_stencil_test.GetBool() )
	{
		static IMaterial *s_pMatRed = NULL;
		if ( !s_pMatRed )
		{
			KeyValues *pKV = new KeyValues( "UnlitGeneric" );
			pKV->SetString( "$basetexture", "vgui/white" );
			pKV->SetString( "$color", "[1 0 0]" );
			pKV->SetInt( "$ignorez", 1 );
			s_pMatRed = MakeMaterial( "__hl2rpm_taa_red", pKV );
		}
		if ( s_pMatRed )
		{
			SetStencilTest( pRenderContext, true );
			pRenderContext->DrawScreenSpaceRectangle( s_pMatRed, vx, vy, vw, vh, 0, 0, 1, 1, 2, 2 );
			pRenderContext->SetStencilEnable( false );
		}
	}

	// this frame
	Rect_t rect;
	rect.x = vx;
	rect.y = vy;
	rect.width = vw;
	rect.height = vh;
	pRenderContext->CopyRenderTargetToTextureEx( pFrame, 0, &rect, &rect );

	// frame + history -> the frame: the world reprojected, then what moves with the camera
	// (stencil) from the same pixel; the result is the next frame's history
	const int iRead = s_iHistory;
	const int iWrite = s_iHistory ^ 1;
	for ( int a = 0; a < 2; a++ )
	{
		SetStencilTest( pRenderContext, a != 0 );
		pRenderContext->DrawScreenSpaceRectangle( g_pMatTAA[ iRead ][ a ][ 0 ], vx, vy, vw, vh,
			vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );
	}
	pRenderContext->SetStencilEnable( false );
	pRenderContext->CopyRenderTargetToTextureEx( g_tex_TAAHistory[ iWrite ], 0, &rect, &rect );

	// debug view over it (the history stays clean)
	if ( r_deferred_taa_debug.GetInt() > 0 )
	{
		for ( int a = 0; a < 2; a++ )
		{
			SetStencilTest( pRenderContext, a != 0 );
			pRenderContext->DrawScreenSpaceRectangle( g_pMatTAA[ iRead ][ a ][ 1 ], vx, vy, vw, vh,
				vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );
		}
		pRenderContext->SetStencilEnable( false );
	}

	s_iHistory = iWrite;
	s_iHistoryW = hw;
	s_iHistoryH = hh;
	s_Prev = cur;
	s_bPrevValid = true;
	s_bReset = false;
}

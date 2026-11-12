//========= HL2RPM ============================================================//
//
// A model turning on the card of an item.
//
// Valve's model panels draw into the back buffer while the panel paints. Here that
// would not do: DEFERRED_MODEL takes its light from the buffers of a scene, and a
// window would get aliased edges (the anti-aliasing of the frame is over by then).
// So the model has a small stage of its own:
//
//   - the window names the model while it paints (RPMItemPreview::Paint) and draws
//     the render target as it stands;
//   - after the scene of the next frame, RPMItemPreview_Render draws the model into
//     that target at twice the size of the window's rectangle, cleared to nothing
//     (the plate of the card shows through around the model);
//   - the materials draw themselves at DEFERRED_RENDER_STAGE_PREVIEW:
//     DEFERRED_MODEL through hl2rpm_itempreview_ps30, with the light of a photo
//     studio; what fell back to the stock shaders (translucent parts) is lit by the
//     lights given to the studio renderer here. Eyes, teeth and glass stay out.
//
// The entity is a client-only animating one, as in CModelPanel, invisible to the
// world (EF_NODRAW): DrawModel is called directly.
//
//   cl_rpm_item_model 0 - cards without a model; cl_rpm_item_model_debug 1 - what is drawn
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_itempreview.h"
#include "c_baseanimating.h"
#include "view_shared.h"
#include "ivrenderview.h"
#include "engine/ivmodelinfo.h"
#include "engine/ivmodelrender.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier3/tier3.h"
#include "istudiorender.h"
#include "datacache/imdlcache.h"
#include "studio.h"
#include "renderparm.h"
#include "model_types.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static ConVar cl_rpm_item_model( "cl_rpm_item_model", "1", FCVAR_ARCHIVE, "HL2RPM: the card of an item shows its 3D model" );
static ConVar cl_rpm_item_model_debug( "cl_rpm_item_model_debug", "0", 0, "HL2RPM: print what the card of an item draws" );

#define PREVIEW_RT_NAME		"_rt_hl2rpm_itempreview"
#define PREVIEW_RT_SIZE		1024
#define PREVIEW_FOV_Y		22.0f		// a long lens: objects look like objects

static CTextureReference s_texPreview;

static struct
{
	// asked for by the window
	CUtlString strModel;
	int iFrame;
	int w, h;
	float flYaw, flPitch, flZoom;
	Vector vecRim;
	// in the target now
	CUtlString strDrawn;
	int iDrawnW, iDrawnH;
	// the entity
	CHandle<C_BaseAnimating> hEnt;
	CUtlString strEnt;
	Vector vecCenter;		// of what it draws, in the model's space
	float flRadiusXY;		// around the vertical axis through the center
	float flHalfZ;
	float flRadius;
	float flYawBase;		// added to the window's yaw: its long side across the view at the start
	float flPitchBase;		// added to the window's pitch: a flat thing is seen from above
	// the window's side
	int iTexture;
	IMaterial *pMaterial;
} s;

void InitItemPreviewRT()
{
	s_texPreview.Init( materials->CreateNamedRenderTargetTextureEx2( PREVIEW_RT_NAME, PREVIEW_RT_SIZE, PREVIEW_RT_SIZE,
		RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_SEPARATE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET, 0 ) );
	s.strDrawn.Clear();		// (a device reset lost it)
}

// what the model really takes up, in the pose it is shown in: the bounds in an .mdl are often far
// larger than what it draws (the world models of the weapons - up to three times), and a model
// framed by them is a speck in the middle of the window
static void MeasureModel( C_BaseAnimating *pEnt )
{
	Vector vecMins, vecMaxs;
	pEnt->GetRenderBounds( vecMins, vecMaxs );
	s.vecCenter = ( vecMins + vecMaxs ) * 0.5f;
	const Vector vecSize = vecMaxs - vecMins;
	s.flRadiusXY = Vector2DLength( vecSize.AsVector2D() ) * 0.5f;
	s.flHalfZ = vecSize.z * 0.5f;
	s.flRadius = vecSize.Length() * 0.5f;
	s.flYawBase = 0.0f;
	s.flPitchBase = 0.0f;

	CStudioHdr *pHdr = pEnt->GetModelPtr();
	if ( !pHdr || pHdr->numbones() <= 0 || pHdr->numbones() > MAXSTUDIOBONES )
		return;
	const vertexFileHeader_t *pVvd = mdlcache->GetVertexData( modelinfo->GetCacheHandle( pEnt->GetModel() ) );
	const mstudiovertex_t *pVerts = pVvd ? pVvd->GetVertexData() : NULL;
	const int nVerts = pVvd ? pVvd->numLODVertexes[0] : 0;
	if ( !pVerts || nVerts <= 0 )
		return;

	// the pose: the vertices are stored in the reference pose, each follows its first bone
	pEnt->SetAbsOrigin( vec3_origin );
	pEnt->SetAbsAngles( vec3_angle );
	pEnt->InvalidateBoneCache();
	static matrix3x4_t s_Bones[MAXSTUDIOBONES], s_Skin[MAXSTUDIOBONES];
	if ( !pEnt->SetupBones( s_Bones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
		return;
	for ( int i = 0; i < pHdr->numbones(); i++ )
		ConcatTransforms( s_Bones[i], pHdr->pBone( i )->poseToBone, s_Skin[i] );

	Vector vecLo( FLT_MAX, FLT_MAX, FLT_MAX ), vecHi( -FLT_MAX, -FLT_MAX, -FLT_MAX ), vecPos;
	for ( int i = 0; i < nVerts; i++ )
	{
		const int iBone = Min( (int)pVerts[i].m_BoneWeights.bone[0], pHdr->numbones() - 1 );
		VectorTransform( pVerts[i].m_vecPosition, s_Skin[iBone], vecPos );
		VectorMin( vecPos, vecLo, vecLo );
		VectorMax( vecPos, vecHi, vecHi );
	}
	const Vector vecCenter = ( vecLo + vecHi ) * 0.5f;
	float flRadiusXY = 0.0f, flRadius = 0.0f;
	for ( int i = 0; i < nVerts; i++ )
	{
		const int iBone = Min( (int)pVerts[i].m_BoneWeights.bone[0], pHdr->numbones() - 1 );
		VectorTransform( pVerts[i].m_vecPosition, s_Skin[iBone], vecPos );
		vecPos -= vecCenter;
		flRadiusXY = Max( flRadiusXY, Vector2DLength( vecPos.AsVector2D() ) );
		flRadius = Max( flRadius, vecPos.Length() );
	}
	if ( flRadius < 0.1f )
		return;
	s.vecCenter = vecCenter;
	s.flRadiusXY = flRadiusXY;
	s.flHalfZ = ( vecHi.z - vecLo.z ) * 0.5f;
	s.flRadius = flRadius;
	// (weapons lie along x; something long lying along y starts a quarter of a turn earlier)
	s.flYawBase = ( vecHi.y - vecLo.y ) > ( vecHi.x - vecLo.x ) * 1.1f ? -90.0f : 0.0f;
	// a key, a note: from the height of the card it is a line - it lies on a table, seen from above, and
	// turns there. Lower than it is wide in both directions: a rifle is narrower than it is tall, and stays
	const Vector vecExtent = vecHi - vecLo;
	s.flPitchBase = vecExtent.z < Min( vecExtent.x, vecExtent.y ) * 0.6f ? 43.0f : 0.0f;
}

// the entity of a model; the last one stays until another is asked for
static C_BaseAnimating *GetEntity( const char *pszModel )
{
	C_BaseAnimating *pEnt = s.hEnt.Get();
	if ( pEnt && !Q_stricmp( s.strEnt, pszModel ) )
		return pEnt;
	if ( pEnt )
		pEnt->Remove();
	s.hEnt = NULL;
	s.strEnt.Clear();

	const model_t *pModel = modelinfo->FindOrLoadModel( pszModel );
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
	{
		DevWarning( "[item preview] no model %s\n", pszModel );
		return NULL;
	}

	pEnt = new C_BaseAnimating;
	if ( !pEnt->InitializeAsClientEntity( NULL, RENDER_GROUP_OPAQUE_ENTITY ) )
	{
		pEnt->Remove();
		return NULL;
	}
	pEnt->AddEffects( EF_NODRAW );		// the world doesn't draw it
	pEnt->SetModelPointer( pModel );
	pEnt->SetSequence( 0 );
	pEnt->SetPlaybackRate( 0.0f );
	pEnt->SetCycle( 0.0f );
	s.hEnt = pEnt;
	s.strEnt = pszModel;
	MeasureModel( pEnt );
	return pEnt;
}

namespace RPMItemPreview
{
	bool HasModel( const char *pszModel )
	{
		return pszModel && pszModel[0] && g_pFullFileSystem->FileExists( pszModel, "GAME" );
	}

	bool Paint( const char *pszModel, int x, int y, int w, int h, float flYaw, float flPitch, float flZoom, int alpha, const Color &rim )
	{
		if ( !cl_rpm_item_model.GetBool() || !s_texPreview.IsValid() || !HasModel( pszModel ) || w < 8 || h < 8 )
			return false;

		s.strModel = pszModel;
		s.iFrame = gpGlobals->framecount;
		s.w = w;
		s.h = h;
		s.flYaw = flYaw;
		s.flPitch = flPitch;
		s.flZoom = flZoom;
		s.vecRim.Init( rim.r() / 255.0f, rim.g() / 255.0f, rim.b() / 255.0f );

		if ( !Q_stricmp( s.strDrawn, pszModel ) && s.iDrawnW > 0 )
		{
			if ( !s.pMaterial )
			{
				KeyValues *pKV = new KeyValues( "UnlitGeneric" );
				pKV->SetString( "$basetexture", PREVIEW_RT_NAME );
				pKV->SetInt( "$translucent", 1 );
				pKV->SetInt( "$vertexcolor", 1 );
				pKV->SetInt( "$vertexalpha", 1 );
				pKV->SetInt( "$ignorez", 1 );
				pKV->SetInt( "$nofog", 1 );
				s.pMaterial = materials->CreateMaterial( "__hl2rpm_itempreview", pKV );
				s.pMaterial->IncrementReferenceCount();
				s.iTexture = surface()->CreateNewTextureID( true );
				g_pMatSystemSurface->DrawSetTextureMaterial( s.iTexture, s.pMaterial );
			}
			surface()->DrawSetTexture( s.iTexture );
			surface()->DrawSetColor( 255, 255, 255, alpha );
			surface()->DrawTexturedSubRect( x, y, x + w, y + h, 0.0f, 0.0f,
				s.iDrawnW / (float)PREVIEW_RT_SIZE, s.iDrawnH / (float)PREVIEW_RT_SIZE );
		}
		return true;
	}
}

// the lights for the materials that fell back to the stock shaders: what the studio of
// hl2rpm_itempreview_ps30 has, roughly
static void SetupStockLights( IMatRenderContext *pRenderContext )
{
	static Vector s_Ambient[6] =
	{
		Vector( 0.3f, 0.3f, 0.3f ), Vector( 0.3f, 0.3f, 0.3f ),
		Vector( 0.3f, 0.3f, 0.3f ), Vector( 0.3f, 0.3f, 0.3f ),
		Vector( 0.4f, 0.4f, 0.42f ), Vector( 0.14f, 0.13f, 0.12f ),
	};
	pRenderContext->SetLightingOrigin( vec3_origin );
	pRenderContext->SetAmbientLight( 0.3f, 0.3f, 0.3f );
	g_pStudioRender->SetAmbientLightColors( s_Ambient );

	LightDesc_t lights[2];
	lights[0].InitDirectional( Vector( 0.542f, -0.497f, -0.678f ), Vector( 1.3f, 1.24f, 1.14f ) );
	lights[1].InitDirectional( Vector( 0.703f, 0.703f, 0.1f ), Vector( 0.26f, 0.3f, 0.38f ) );
	g_pStudioRender->SetLocalLights( 2, lights );
}

void RPMItemPreview_Render()
{
	const bool bAsked = s.strModel.Length() > 0 && gpGlobals->framecount - s.iFrame <= 2;
	if ( !bAsked )
	{
		// the window is closed: the entity goes after a while
		if ( s.hEnt.Get() && gpGlobals->framecount - s.iFrame > 300 )
		{
			s.hEnt->Remove();
			s.hEnt = NULL;
			s.strEnt.Clear();
			s.strDrawn.Clear();
		}
		return;
	}
	if ( !s_texPreview.IsValid() )
		return;

	MDLCACHE_CRITICAL_SECTION();

	C_BaseAnimating *pEnt = GetEntity( s.strModel );
	if ( !pEnt )
		return;

	// twice the rectangle (the window's bilinear filter averages it down), inside the target
	const float flSuper = Min( 2.0f, Min( (float)PREVIEW_RT_SIZE / s.w, (float)PREVIEW_RT_SIZE / s.h ) );
	const int rw = (int)( s.w * flSuper ), rh = (int)( s.h * flSuper );

	// the model turns around its middle
	const float flYaw = s.flYaw + s.flYawBase;
	Vector vecTurned;
	VectorYawRotate( s.vecCenter, flYaw, vecTurned );
	pEnt->SetAbsOrigin( -vecTurned );
	pEnt->SetAbsAngles( QAngle( 0.0f, flYaw, 0.0f ) );
	pEnt->InvalidateBoneCache();

	// a camera that keeps the model in the rectangle all the way round (as seen from the height the
	// card starts at: turning it up and down with the mouse doesn't change its size)
	const float flAspect = rw / (float)rh;
	const float flTanY = tan( DEG2RAD( PREVIEW_FOV_Y * 0.5f ) );
	const float flTanX = flTanY * flAspect;
	const float flTilt = DEG2RAD( RPMItemPreview::PITCH_START + s.flPitchBase );
	const float flFitX = s.flRadiusXY / flTanX;
	const float flFitY = ( s.flHalfZ * cos( flTilt ) + s.flRadiusXY * sin( flTilt ) ) / flTanY;
	const float flDist = Max( flFitX, flFitY ) * 1.1f / Max( s.flZoom, 0.1f );
	const float flRadius = Max( s.flRadius, 1.0f );

	CViewSetup view;
	view.x = 0;
	view.y = 0;
	view.width = rw;
	view.height = rh;
	view.m_bOrtho = false;
	view.m_flAspectRatio = flAspect;
	view.fov = RAD2DEG( atan( flTanX ) ) * 2.0f;
	view.angles.Init( clamp( s.flPitch + s.flPitchBase, -85.0f, 85.0f ), 0.0f, 0.0f );
	Vector vecForward;
	AngleVectors( view.angles, &vecForward );
	view.origin = -vecForward * flDist;
	view.zNear = Max( 1.0f, flDist - flRadius * 1.2f );
	view.zFar = flDist + flRadius * 1.2f;

	CMatRenderContextPtr pRenderContext( materials );

	Frustum dummyFrustum;
	render->Push3DView( view, 0, s_texPreview, dummyFrustum );

	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
	pRenderContext->ClearBuffers( true, true );
	pRenderContext->FogMode( MATERIAL_FOG_NONE );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE, DEFERRED_RENDER_STAGE_PREVIEW );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_HL2RPM_PREVIEW_RIM, s.vecRim );

	SetupStockLights( pRenderContext );
	modelrender->SuppressEngineLighting( true );
	const float color[3] = { 1.0f, 1.0f, 1.0f };
	render->SetColorModulation( color );
	render->SetBlend( 1.0f );

	const int drawn = pEnt->DrawModel( STUDIO_RENDER );

	modelrender->SuppressEngineLighting( false );
	g_pStudioRender->SetLocalLights( 0, NULL );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE, DEFERRED_RENDER_STAGE_INVALID );

	render->PopView( dummyFrustum );

	s.strDrawn = s.strModel;
	s.iDrawnW = rw;
	s.iDrawnH = rh;

	if ( cl_rpm_item_model_debug.GetBool() )
		DevMsg( "[item preview] %s: %d x %d, radius %.1f, dist %.1f, yaw %.0f, drawn %d\n", s.strModel.Get(), rw, rh, flRadius, flDist, s.flYaw, drawn );
}

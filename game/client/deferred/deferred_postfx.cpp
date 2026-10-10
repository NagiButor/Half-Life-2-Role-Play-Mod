//========= HL2RPM ============================================================//
//
// Purpose: Post-processing of the final frame for the deferred renderer.
//
// The GI makes interiors as dark as they are compared to the street - which needs
// what the eye does: adapt. And a modern image wants the rest of the camera too:
//  - eye adaptation: the average (log) luminance of the frame, center weighted, is
//    measured on the GPU (frame -> 32x18 blocks -> 1x1) and followed with a delay -
//    fast toward a brighter scene, slower into the dark; the exposure brightens a
//    dark room only part of the way (r_hl2rpm_exposure_adapt), so it stays darker
//    than the street. All on the GPU: nothing is read back.
//  - bloom: the bright part of the frame (x exposure, soft threshold) through a
//    chain of 5 half-size levels and back (13 tap downsample, tent upsample - wide
//    and smooth, no blocky spikes), plus lens dirt it lights up;
//  - a soft shoulder for the highlights an exposure above 1 pushes past white,
//    saturation, color balance, an S-curve of contrast;
//  - vignette, film grain, chromatic aberration toward the corners, contrast
//    adaptive sharpening (FXAA softens a little).
// The engine's own HDR exposure and bloom (maps compiled with HDR) are turned off
// while this runs: they only reached the forward materials, not the deferred ones.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_postfx.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "view_shared.h"
#include "tier1/KeyValues.h"
#include "tier1/callqueue.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"
#include "weather/c_weather_system.h"
#include "weather/weather_render.h"

#include "tier0/memdbgon.h"

static ConVar r_hl2rpm_post( "r_hl2rpm_post", "1", FCVAR_ARCHIVE,
	"Post-processing of the frame (deferred renderer): eye adaptation, bloom, color grading, lens effects" );
static ConVar r_hl2rpm_exposure( "r_hl2rpm_exposure", "1", FCVAR_ARCHIVE,
	"Eye adaptation: dark places brighten up after a moment, bright ones dim down" );
static ConVar r_hl2rpm_exposure_key( "r_hl2rpm_exposure_key", "0.15", 0,
	"Eye adaptation: average luminance of the frame that keeps the exposure at 1", true, 0.001f, true, 1.0f );
static ConVar r_hl2rpm_exposure_adapt( "r_hl2rpm_exposure_adapt", "0.65", 0,
	"Eye adaptation: how far the eye adapts (0 = not at all, 1 = every scene to the same brightness)", true, 0.0f, true, 1.0f );
static ConVar r_hl2rpm_exposure_min( "r_hl2rpm_exposure_min", "0.8", 0,
	"Eye adaptation: lowest exposure (bright scenes)", true, 0.1f, true, 1.0f );
static ConVar r_hl2rpm_exposure_max( "r_hl2rpm_exposure_max", "3.5", 0,
	"Eye adaptation: highest exposure (dark rooms by day)", true, 1.0f, true, 16.0f );
static ConVar r_hl2rpm_exposure_max_night( "r_hl2rpm_exposure_max_night", "1.6", 0,
	"Eye adaptation: highest exposure when the whole world is dark (night) - the night stays night", true, 1.0f, true, 16.0f );
static ConVar r_hl2rpm_exposure_tau_bright( "r_hl2rpm_exposure_tau_bright", "0.7", 0,
	"Eye adaptation: seconds to get used to a brighter scene", true, 0.01f, true, 30.0f );
static ConVar r_hl2rpm_exposure_tau_dark( "r_hl2rpm_exposure_tau_dark", "2.5", 0,
	"Eye adaptation: seconds to get used to a darker scene", true, 0.01f, true, 30.0f );
static ConVar r_hl2rpm_exposure_debug( "r_hl2rpm_exposure_debug", "0", 0,
	"Eye adaptation: print the measured / adapted luminance and the exposure twice a second" );

static ConVar r_hl2rpm_bloom( "r_hl2rpm_bloom", "1", FCVAR_ARCHIVE, "Bloom: the glow around bright light" );
static ConVar r_hl2rpm_bloom_strength( "r_hl2rpm_bloom_strength", "0.05", 0, "Bloom strength", true, 0.0f, true, 2.0f );
static ConVar r_hl2rpm_bloom_threshold( "r_hl2rpm_bloom_threshold", "0.95", 0, "Bloom: brightness (after the exposure) it starts at", true, 0.0f, true, 8.0f );
static ConVar r_hl2rpm_bloom_knee( "r_hl2rpm_bloom_knee", "0.5", 0, "Bloom: softness of the threshold", true, 0.01f, true, 4.0f );
static ConVar r_hl2rpm_lensdirt( "r_hl2rpm_lensdirt", "0.25", 0, "Bloom: lens dirt lit by bright light", true, 0.0f, true, 4.0f );

static ConVar r_hl2rpm_grade( "r_hl2rpm_grade", "1", FCVAR_ARCHIVE, "Color grading: contrast, saturation, soft highlights" );
static ConVar r_hl2rpm_contrast( "r_hl2rpm_contrast", "0.1", 0, "Color grading: S-curve of contrast (0..1)", true, 0.0f, true, 1.0f );
static ConVar r_hl2rpm_saturation( "r_hl2rpm_saturation", "1.05", 0, "Color grading: saturation", true, 0.0f, true, 2.0f );
static ConVar r_hl2rpm_shoulder( "r_hl2rpm_shoulder", "0.7", 0, "Color grading: where the highlights start to roll off (0.5..1)", true, 0.3f, true, 1.0f );
static ConVar r_hl2rpm_tint( "r_hl2rpm_tint", "1 1 1", 0, "Color grading: color balance (r g b)" );

static ConVar r_hl2rpm_lens( "r_hl2rpm_lens", "1", FCVAR_ARCHIVE, "Lens effects: vignette, film grain, chromatic aberration" );
static ConVar r_hl2rpm_vignette( "r_hl2rpm_vignette", "0.2", 0, "Lens: vignette (darker corners)", true, 0.0f, true, 1.0f );
static ConVar r_hl2rpm_grain( "r_hl2rpm_grain", "0.018", 0, "Lens: film grain", true, 0.0f, true, 0.2f );
static ConVar r_hl2rpm_ca( "r_hl2rpm_ca", "0.8", 0, "Lens: chromatic aberration at the corners (pixels)", true, 0.0f, true, 8.0f );
static ConVar r_hl2rpm_flare( "r_hl2rpm_flare", "0.5", 0, "Lens: flare of the sun (ghosts along the line through the center of the frame, a halo)", true, 0.0f, true, 4.0f );
static ConVar r_hl2rpm_flare_threshold( "r_hl2rpm_flare_threshold", "0.06", 0, "Lens flare: brightness of the bloom at the sun it starts at", true, 0.0f, true, 4.0f );
static ConVar r_hl2rpm_flare_debug( "r_hl2rpm_flare_debug", "0", 0, "Lens flare: print where the sun is on screen and what the visibility trace hit, twice a second" );
static ConVar r_hl2rpm_sharpen( "r_hl2rpm_sharpen", "0.4", FCVAR_ARCHIVE, "Sharpening of the frame (contrast adaptive, 0..1)", true, 0.0f, true, 1.0f );

static ConVar r_hl2rpm_dof( "r_hl2rpm_dof", "1", FCVAR_ARCHIVE,
	"Depth of field (what lies beyond the focus goes soft): 0 = off, 1 = while zoomed in and in dialogs, 2 = always", true, 0.0f, true, 2.0f );
static ConVar r_hl2rpm_dof_strength( "r_hl2rpm_dof_strength", "1", 0, "Depth of field: size of the blur (1 = an f/2.8 lens with the current field of view)", true, 0.0f, true, 16.0f );
static ConVar r_hl2rpm_dof_zoom( "r_hl2rpm_dof_zoom", "2", 0, "Depth of field: blur multiplier while zoomed in", true, 0.0f, true, 32.0f );
static ConVar r_hl2rpm_dof_dialog( "r_hl2rpm_dof_dialog", "4", 0, "Depth of field: blur multiplier in dialogs (a portrait lens)", true, 0.0f, true, 32.0f );
static ConVar r_hl2rpm_dof_always( "r_hl2rpm_dof_always", "1.5", 0, "Depth of field: blur multiplier of mode 2 while not zoomed", true, 0.0f, true, 32.0f );
static ConVar r_hl2rpm_dof_max( "r_hl2rpm_dof_max", "10", 0, "Depth of field: largest blur radius (pixels at 900 lines)", true, 1.0f, true, 32.0f );
static ConVar r_hl2rpm_dof_focus_time( "r_hl2rpm_dof_focus_time", "0.18", 0, "Depth of field: seconds the focus takes to follow what is in the middle of the screen", true, 0.01f, true, 5.0f );
static ConVar r_hl2rpm_dof_debug( "r_hl2rpm_dof_debug", "0", FCVAR_CHEAT, "Depth of field: 1 = show the circle of confusion (orange) and print the focus twice a second" );

#define LUM_BLOCKS_X 32
#define LUM_BLOCKS_Y 18
#define DIRT_W 512
#define DIRT_H 256

static CTextureReference g_tex_LumBlocks;
static CTextureReference g_tex_LumAvg;
static CTextureReference g_tex_LumTmp;
static CTextureReference g_tex_LumCur;
static CTextureReference g_tex_BloomA;
static CTextureReference g_tex_BloomB;
static CTextureReference g_tex_LensDirt;

static IMaterial *g_pMatLum[3] = { NULL, NULL, NULL };
static IMaterial *g_pMatBloomPre = NULL;
static IMaterial *g_pMatBloomDown[ POSTFX_LEVELS ];
static IMaterial *g_pMatBloomUp[ POSTFX_LEVELS ];
static IMaterial *g_pMatFinal = NULL;
static IMaterial *g_pMatDoF = NULL;
static bool g_bMaterialsTried = false;

static bool s_bResetAdaptation = true;

//-----------------------------------------------------------------------------
// Lens dirt: generated once (soft smudges and specks, more toward the edges)
//-----------------------------------------------------------------------------
class CLensDirtRegen : public ITextureRegenerator
{
public:
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
	{
		const int w = pVTFTexture->Width();
		const int h = pVTFTexture->Height();
		CUtlVector< float > img;
		img.SetCount( w * h );
		for ( int i = 0; i < w * h; i++ )
			img[i] = 0.0f;

		unsigned int seed = 0x2F6B3A1Du;
		struct Rng
		{
			static float Next( unsigned int &s )
			{
				s = s * 1664525u + 1013904223u;
				return ( ( s >> 8 ) & 0xFFFFFF ) / 16777216.0f;
			}
		};

		// blobs: big and faint, small and stronger
		for ( int pass = 0; pass < 2; pass++ )
		{
			const int n = pass ? 260 : 70;
			for ( int b = 0; b < n; b++ )
			{
				const float cx = Rng::Next( seed ) * w;
				const float cy = Rng::Next( seed ) * h;
				const float r = pass ? 1.5f + Rng::Next( seed ) * 5.0f : 14.0f + Rng::Next( seed ) * 60.0f;
				const float a = pass ? 0.12f + Rng::Next( seed ) * 0.35f : 0.05f + Rng::Next( seed ) * 0.18f;
				// smudges: stretched along a random direction
				const float ang = Rng::Next( seed ) * 6.2831853f;
				const float stretch = pass ? 1.0f : 1.0f + Rng::Next( seed ) * 1.8f;
				const float ca = cosf( ang ), sa = sinf( ang );
				const int x0 = Max( 0, (int)( cx - r * stretch ) ), x1 = Min( w - 1, (int)( cx + r * stretch ) );
				const int y0 = Max( 0, (int)( cy - r * stretch ) ), y1 = Min( h - 1, (int)( cy + r * stretch ) );
				for ( int y = y0; y <= y1; y++ )
				{
					for ( int x = x0; x <= x1; x++ )
					{
						const float dx = x - cx, dy = y - cy;
						const float u = ( dx * ca + dy * sa ) / stretch;
						const float v = -dx * sa + dy * ca;
						const float d2 = ( u * u + v * v ) / ( r * r );
						if ( d2 >= 1.0f )
							continue;
						const float f = ( 1.0f - d2 ) * ( 1.0f - d2 );
						img[ y * w + x ] += a * f;
					}
				}
			}
		}

		CPixelWriter pixelWriter;
		pixelWriter.SetPixelMemory( pVTFTexture->Format(), pVTFTexture->ImageData( 0, 0, 0 ), pVTFTexture->RowSizeInBytes( 0 ) );
		for ( int y = 0; y < h; y++ )
		{
			pixelWriter.Seek( 0, y );
			for ( int x = 0; x < w; x++ )
			{
				// more dirt toward the edges of the lens
				const float ex = ( x + 0.5f ) / w * 2.0f - 1.0f;
				const float ey = ( y + 0.5f ) / h * 2.0f - 1.0f;
				const float edge = 0.3f + 0.7f * clamp( ( sqrtf( ex * ex + ey * ey ) - 0.25f ) / 0.9f, 0.0f, 1.0f );
				const int v = clamp( (int)( img[ y * w + x ] * edge * 255.0f + 0.5f ), 0, 255 );
				pixelWriter.WritePixel( v, v, v, 255 );
			}
		}
	}

	virtual void Release() {}
};
static CLensDirtRegen g_LensDirtRegen;

//-----------------------------------------------------------------------------
// Targets and materials
//-----------------------------------------------------------------------------
void InitPostFXRTs()
{
	const unsigned int flags = TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_RENDERTARGET;

	g_tex_LumBlocks.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_lumblocks", LUM_BLOCKS_X, LUM_BLOCKS_Y,
		RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	g_tex_LumAvg.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_lumavg", 1, 1,
		RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	g_tex_LumTmp.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_lumtmp", 1, 1,
		RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	g_tex_LumCur.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_lumcur", 1, 1,
		RT_SIZE_NO_CHANGE, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags | TEXTUREFLAGS_POINTSAMPLE, 0 ) );

	// bloom levels: 1, 3, 5 in A, 2, 4 in B (a level is read while the next one is written)
	g_tex_BloomA.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_bloomA", 128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags, 0 ) );
	g_tex_BloomB.Init( materials->CreateNamedRenderTargetTextureEx2( "_rt_hl2rpm_bloomB", 128, 128,
		RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, flags, 0 ) );

	// the render targets lost their content (device reset): the adaptation starts over
	s_bResetAdaptation = true;
}

static ITexture *GetLensDirt()
{
	if ( !g_tex_LensDirt.IsValid() )
	{
		ITexture *pTex = materials->CreateProceduralTexture( "_rt_hl2rpm_lensdirt", TEXTURE_GROUP_OTHER,
			DIRT_W, DIRT_H, IMAGE_FORMAT_RGBA8888, TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL
			| TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT );
		if ( pTex )
		{
			pTex->SetTextureRegenerator( &g_LensDirtRegen );
			pTex->Download();
			g_tex_LensDirt.Init( pTex );
			pTex->DecrementReferenceCount();
		}
	}
	return g_tex_LensDirt;
}

static inline bool IsLevelInA( int k )
{
	return ( k & 1 ) != 0;
}

static const char *LevelRTName( int k )
{
	return IsLevelInA( k ) ? "_rt_hl2rpm_bloomA" : "_rt_hl2rpm_bloomB";
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
		GetLensDirt();

		for ( int i = 0; i < 3; i++ )
		{
			KeyValues *pKV = new KeyValues( "HL2RPM_LUM" );
			pKV->SetInt( "$pass", i );
			if ( i == 1 )
				pKV->SetString( "$basetexture", "_rt_hl2rpm_lumblocks" );
			if ( i == 2 )
			{
				pKV->SetString( "$basetexture", "_rt_hl2rpm_lumavg" );
				pKV->SetString( "$prevtexture", "_rt_hl2rpm_lumcur" );
			}
			g_pMatLum[i] = MakeMaterial( VarArgs( "__hl2rpm_lum%d", i ), pKV );
		}

		{
			KeyValues *pKV = new KeyValues( "HL2RPM_BLOOM" );
			pKV->SetInt( "$pass", 0 );
			pKV->SetString( "$lumtexture", "_rt_hl2rpm_lumcur" );
			g_pMatBloomPre = MakeMaterial( "__hl2rpm_bloom_pre", pKV );
		}
		for ( int k = 0; k < POSTFX_LEVELS; k++ )
		{
			g_pMatBloomDown[k] = NULL;
			g_pMatBloomUp[k] = NULL;
		}
		for ( int k = 2; k < POSTFX_LEVELS; k++ )
		{
			// down k: reads level k - 1, writes level k
			KeyValues *pDown = new KeyValues( "HL2RPM_BLOOM" );
			pDown->SetInt( "$pass", 1 );
			pDown->SetInt( "$level", k - 1 );
			pDown->SetString( "$basetexture", LevelRTName( k - 1 ) );
			g_pMatBloomDown[k] = MakeMaterial( VarArgs( "__hl2rpm_bloom_down%d", k ), pDown );

			// up k: reads level k, added onto level k - 1
			KeyValues *pUp = new KeyValues( "HL2RPM_BLOOM" );
			pUp->SetInt( "$pass", 2 );
			pUp->SetInt( "$level", k );
			pUp->SetString( "$basetexture", LevelRTName( k ) );
			g_pMatBloomUp[k] = MakeMaterial( VarArgs( "__hl2rpm_bloom_up%d", k ), pUp );
		}

		{
			KeyValues *pKV = new KeyValues( "HL2RPM_POSTFX" );
			pKV->SetString( "$bloomtexture", "_rt_hl2rpm_bloomA" );
			pKV->SetString( "$lumtexture", "_rt_hl2rpm_lumcur" );
			pKV->SetString( "$dirttexture", "_rt_hl2rpm_lensdirt" );
			g_pMatFinal = MakeMaterial( "__hl2rpm_postfx", pKV );
		}

		g_pMatDoF = MakeMaterial( "__hl2rpm_dof", new KeyValues( "HL2RPM_DOF" ) );
	}

	bool bOk = IsOk( g_pMatLum[0] ) && IsOk( g_pMatLum[1] ) && IsOk( g_pMatLum[2] ) && IsOk( g_pMatBloomPre ) && IsOk( g_pMatFinal );
	for ( int k = 2; k < POSTFX_LEVELS; k++ )
		bOk = bOk && IsOk( g_pMatBloomDown[k] ) && IsOk( g_pMatBloomUp[k] );
	return bOk;
}

void DeferredPostFX_Reset()
{
	s_bResetAdaptation = true;
}

void DeferredPostFX_Shutdown()
{
	for ( int i = 0; i < 3; i++ )
	{
		if ( g_pMatLum[i] ) { g_pMatLum[i]->DecrementReferenceCount(); g_pMatLum[i] = NULL; }
	}
	if ( g_pMatBloomPre ) { g_pMatBloomPre->DecrementReferenceCount(); g_pMatBloomPre = NULL; }
	for ( int k = 0; k < POSTFX_LEVELS; k++ )
	{
		if ( g_bMaterialsTried && g_pMatBloomDown[k] ) { g_pMatBloomDown[k]->DecrementReferenceCount(); g_pMatBloomDown[k] = NULL; }
		if ( g_bMaterialsTried && g_pMatBloomUp[k] ) { g_pMatBloomUp[k]->DecrementReferenceCount(); g_pMatBloomUp[k] = NULL; }
	}
	if ( g_pMatFinal ) { g_pMatFinal->DecrementReferenceCount(); g_pMatFinal = NULL; }
	if ( g_pMatDoF ) { g_pMatDoF->DecrementReferenceCount(); g_pMatDoF = NULL; }
	g_bMaterialsTried = false;

	// (the material system outlives the client DLL: a texture still pointing at a regenerator
	// in its unloaded memory crashed on quit)
	if ( g_tex_LensDirt.IsValid() )
	{
		g_tex_LensDirt->SetTextureRegenerator( NULL );
		g_tex_LensDirt.Shutdown();
	}
}

static bool IsPostFXActive()
{
	return r_hl2rpm_post.GetBool() && GetDeferredManager() && GetDeferredManager()->IsDeferredRenderingEnabled()
		&& g_tex_LumCur.IsValid() && g_tex_BloomA.IsValid();
}

bool DeferredPostFX_OwnsExposure()
{
	return IsPostFXActive();
}

bool DeferredPostFX_OwnsBloom()
{
	return IsPostFXActive();
}

//-----------------------------------------------------------------------------
// Debug readback of the adapted luminance (a stall - only while asked for)
//-----------------------------------------------------------------------------
static float HalfToFloat( unsigned short h )
{
	const int s = ( h >> 15 ) & 1;
	const int e = ( h >> 10 ) & 31;
	const int m = h & 1023;
	float f;
	if ( e == 0 )
		f = m / 1024.0f * powf( 2.0f, -14.0f );
	else if ( e == 31 )
		f = 65504.0f;
	else
		f = ( 1.0f + m / 1024.0f ) * powf( 2.0f, (float)( e - 15 ) );
	return s ? -f : f;
}

static void PrintAdaptation( const postfxData_t &data )
{
	static float s_flNext = 0.0f;
	if ( gpGlobals->realtime < s_flNext )
		return;
	s_flNext = gpGlobals->realtime + 0.5f;

	CMatRenderContextPtr pRenderContext( materials );
	unsigned short buf[4] = { 0, 0, 0, 0 };
	pRenderContext->PushRenderTargetAndViewport( g_tex_LumCur, 0, 0, 1, 1 );
	pRenderContext->ReadPixels( 0, 0, 1, 1, (unsigned char *)buf, IMAGE_FORMAT_RGBA16161616F );
	pRenderContext->PopRenderTargetAndViewport();

	const float flAdapted = HalfToFloat( buf[0] );
	const float flCurrent = HalfToFloat( buf[1] );
	const Vector4D &p = data.vecExposure;
	const float flExposure = clamp( powf( p.x / Max( expf( flAdapted ), 1e-4f ), p.y ), p.z, p.w );
	Msg( "[postfx] luminance now %.4f, adapted %.4f -> exposure %.2f\n", expf( flCurrent ), expf( flAdapted ), flExposure );
}

//-----------------------------------------------------------------------------
// Depth of field (hl2rpm_dof_ps30). Before the anti-aliasing: TAA evens out the
// noise of its gather. The focus follows what is in the middle of the screen (a
// trace); the blur is the circle of confusion of a real lens with the current
// field of view: next to nothing at the normal one, shallow when zoomed in (a long
// lens). In a dialog the lens "opens up" - a portrait of the NPC. Only what lies
// beyond the focus is blurred: the weapon and whatever is in front stay sharp.
//-----------------------------------------------------------------------------
extern bool HL2RPM_IsDialogOpen();	// vgui_dialogpanel.cpp

void DeferredPostFX_DrawDoF( const CViewSetup &view )
{
	static float s_flFocus = 512.0f;
	static float s_flDialog = 0.0f;

	const int iMode = r_hl2rpm_dof.GetInt();
	if ( iMode <= 0 || !IsPostFXActive() || !EnsureMaterials() || !IsOk( g_pMatDoF ) )
	{
		s_flDialog = 0.0f;
		return;
	}

	const float flDt = Max( 0.0f, gpGlobals->absoluteframetime );

	// how much: zoomed in (a long lens), a dialog (a portrait lens), or always
	float flZoom = 0.0f;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		const float flDefault = Max( 1.0f, (float)pPlayer->GetDefaultFOV() );
		const float flFov = Max( 1.0f, pPlayer->GetFOV() );
		flZoom = clamp( ( flDefault / flFov - 1.1f ) / 0.4f, 0.0f, 1.0f );
	}
	s_flDialog = Approach( HL2RPM_IsDialogOpen() ? 1.0f : 0.0f, s_flDialog, flDt / 0.5f );

	float flFactor = Max( flZoom * r_hl2rpm_dof_zoom.GetFloat(), s_flDialog * r_hl2rpm_dof_dialog.GetFloat() );
	if ( iMode >= 2 )
		flFactor = Max( flFactor, r_hl2rpm_dof_always.GetFloat() );
	flFactor *= r_hl2rpm_dof_strength.GetFloat();

	const bool bDebug = r_hl2rpm_dof_debug.GetBool();
	if ( flFactor <= 0.001f && !bDebug )
		return;

	ITexture *pFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	if ( !pFrame || pFrame->IsError() )
		return;

	const int vx = view.x, vy = view.y;
	const int vw = Max( 2, view.width ), vh = Max( 2, view.height );
	const int fbw = pFrame->GetActualWidth(), fbh = pFrame->GetActualHeight();

	// the focus: what is in the middle of the screen, followed with a little lag (in
	// 1 / distance: the lens moves evenly through the near range)
	Vector vecForward;
	AngleVectors( view.angles, &vecForward );
	trace_t tr;
	UTIL_TraceLine( view.origin, view.origin + vecForward * 8192.0f, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr );
	const float flTarget = clamp( tr.fraction * 8192.0f, 24.0f, 8192.0f );
	const float flRate = 1.0f - expf( -flDt / r_hl2rpm_dof_focus_time.GetFloat() );
	s_flFocus = 1.0f / Lerp( flRate, 1.0f / s_flFocus, 1.0f / flTarget );

	// the lens: the focal length of a 24 mm tall frame with this field of view, at f/2.8.
	// Circle diameter / frame height = f^2 / ( N * 24 ) * | 1 / focus - 1 / depth | (in mm);
	// a unit is 19.05 mm
	const float flTanV = tanf( DEG2RAD( view.fov * 0.5f ) ) / Max( 0.1f, view.m_flAspectRatio );
	const float flFocal = 12.0f / Max( flTanV, 0.01f );
	const float flScale = 0.5f * vh * ( flFocal * flFocal / ( 2.8f * 24.0f ) ) / 19.05f * flFactor;

	static int s_iFrame = 0;
	s_iFrame = ( s_iFrame + 1 ) & 1023;

	postfxData_t data;
	data.vecFrameTexel.Init( 1.0f / fbw, 1.0f / fbh, (float)fbw, (float)fbh );
	data.vecDoF.Init( flScale, 1.0f / s_flFocus, r_hl2rpm_dof_max.GetFloat() * vh / 900.0f, bDebug ? 1.0f : 0.0f );
	data.vecLens.Init( 0.0f, 0.0f, 0.0f, (float)s_iFrame );
	QUEUE_FIRE( CommitPostFXData, data );

	if ( bDebug )
	{
		static float s_flNextPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextPrint )
		{
			s_flNextPrint = gpGlobals->realtime + 0.5f;
			Msg( "[dof] focus %.0f (target %.0f), zoom %.2f dialog %.2f -> factor %.2f, focal length %.0f mm, circle scale %.0f\n",
				s_flFocus, flTarget, flZoom, s_flDialog, flFactor, flFocal, flScale );
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	Rect_t rect;
	rect.x = vx;
	rect.y = vy;
	rect.width = vw;
	rect.height = vh;
	pRenderContext->CopyRenderTargetToTextureEx( pFrame, 0, &rect, &rect );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatDoF, vx, vy, vw, vh,
		vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );
}

//-----------------------------------------------------------------------------
// Frame
//-----------------------------------------------------------------------------
void DeferredPostFX_Draw( const CViewSetup &view )
{
	if ( !IsPostFXActive() || !EnsureMaterials() )
		return;

	ITexture *pFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	if ( !pFrame || pFrame->IsError() )
		return;

	const int vx = view.x, vy = view.y;
	const int vw = Max( 2, view.width ), vh = Max( 2, view.height );
	const int fbw = pFrame->GetActualWidth(), fbh = pFrame->GetActualHeight();
	const int aw = g_tex_BloomA->GetActualWidth(), ah = g_tex_BloomA->GetActualHeight();

	const bool bBloom = r_hl2rpm_bloom.GetBool();
	const bool bExposure = r_hl2rpm_exposure.GetBool();
	const bool bGrade = r_hl2rpm_grade.GetBool();
	const bool bLens = r_hl2rpm_lens.GetBool();

	// bloom levels: size halves each time; placement in their targets
	int lx[ POSTFX_LEVELS ], ly[ POSTFX_LEVELS ], lw[ POSTFX_LEVELS ], lh[ POSTFX_LEVELS ];
	lx[0] = vx; ly[0] = vy; lw[0] = vw; lh[0] = vh;
	for ( int k = 1; k < POSTFX_LEVELS; k++ )
	{
		lw[k] = Max( 1, vw >> k );
		lh[k] = Max( 1, vh >> k );
	}
	lx[1] = 0; ly[1] = 0;						// A
	lx[2] = 0; ly[2] = 0;						// B
	lx[3] = 0; ly[3] = lh[1] + 4;				// A, under level 1
	lx[4] = 0; ly[4] = lh[2] + 4;				// B, under level 2
	lx[5] = lw[3] + 4; ly[5] = lh[1] + 4;		// A, right of level 3

	postfxData_t data;

	const float flDt = Max( 0.0f, gpGlobals->absoluteframetime );
	const float flRateUp = 1.0f - expf( -flDt / r_hl2rpm_exposure_tau_bright.GetFloat() );
	const float flRateDown = 1.0f - expf( -flDt / r_hl2rpm_exposure_tau_dark.GetFloat() );
	if ( bExposure )
	{
		// how bright the world is compared to the day (weather, time of day): at night the eye
		// adapts only a little - a dark room by day brightens up, the night doesn't turn into day
		float flWorld = 1.0f;
		const C_WeatherSystem *pWeather = GetWeatherSystem();
		if ( pWeather && pWeather->IsActive() )
			flWorld = pWeather->GetEnvLightScale();
		const float flDay = clamp( ( flWorld - 0.05f ) / 0.45f, 0.0f, 1.0f );
		const float flMax = Lerp( flDay * flDay * ( 3.0f - 2.0f * flDay ), r_hl2rpm_exposure_max_night.GetFloat(), r_hl2rpm_exposure_max.GetFloat() );
		data.vecExposure.Init( r_hl2rpm_exposure_key.GetFloat(), r_hl2rpm_exposure_adapt.GetFloat(),
			Min( r_hl2rpm_exposure_min.GetFloat(), flMax ), flMax );
	}
	else
		data.vecExposure.Init( r_hl2rpm_exposure_key.GetFloat(), 0.0f, 1.0f, 1.0f );
	data.vecAdapt.Init( flRateUp, flRateDown, s_bResetAdaptation ? 1.0f : 0.0f, 0.0f );
	s_bResetAdaptation = false;

	data.vecBloom.Init( bBloom ? r_hl2rpm_bloom_strength.GetFloat() : 0.0f, bBloom ? r_hl2rpm_lensdirt.GetFloat() : 0.0f,
		r_hl2rpm_bloom_threshold.GetFloat(), r_hl2rpm_bloom_knee.GetFloat() );
	if ( bGrade )
		data.vecGrade.Init( r_hl2rpm_contrast.GetFloat(), r_hl2rpm_saturation.GetFloat(), r_hl2rpm_shoulder.GetFloat(), r_hl2rpm_sharpen.GetFloat() );
	else
		data.vecGrade.Init( 0.0f, 1.0f, r_hl2rpm_shoulder.GetFloat(), r_hl2rpm_sharpen.GetFloat() );

	static int s_iFrame = 0;
	s_iFrame = ( s_iFrame + 1 ) & 1023;
	if ( bLens )
		data.vecLens.Init( r_hl2rpm_vignette.GetFloat(), r_hl2rpm_grain.GetFloat(), r_hl2rpm_ca.GetFloat(), (float)s_iFrame );
	else
		data.vecLens.Init( 0.0f, 0.0f, 0.0f, (float)s_iFrame );

	Vector vecTint( 1, 1, 1 );
	if ( bGrade )
		UTIL_StringToVector( vecTint.Base(), r_hl2rpm_tint.GetString() );
	data.vecTint.Init( vecTint.x, vecTint.y, vecTint.z, (float)vw / vh );

	// lens flare of the sun: where it is on screen, and whether anything solid is in the way
	// (a trace - what the clouds and the exposure leave of it, the shader reads from the bloom)
	{
		static float s_flSunSeen = 0.0f;
		float flTarget = 0.0f;
		Vector2D vecSun( 0.5f, 0.5f );
		Vector vecSunDir( 0, 0, 1 );
		const C_WeatherSystem *pWeather = GetWeatherSystem();
		if ( bLens && r_hl2rpm_flare.GetFloat() > 0.0f && pWeather && pWeather->IsActive()
			&& WeatherRender_GetSunScreenPos( vecSun, vecSunDir ) )
		{
			// on screen, fading out toward its edges
			const float flOff = Max( fabsf( vecSun.x - 0.5f ), fabsf( vecSun.y - 0.5f ) );
			const float flOnScreen = clamp( ( 0.5f - flOff ) / 0.06f, 0.0f, 1.0f );
			if ( flOnScreen > 0.0f )
			{
				trace_t tr;
				UTIL_TraceLine( view.origin, view.origin + vecSunDir * 30000.0f, MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );
				if ( tr.fraction >= 1.0f || ( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) )
					flTarget = flOnScreen;

				if ( r_hl2rpm_flare_debug.GetBool() )
				{
					static float s_flNextPrint = 0.0f;
					if ( gpGlobals->realtime >= s_flNextPrint )
					{
						s_flNextPrint = gpGlobals->realtime + 0.5f;
						Msg( "[flare] sun at %.3f %.3f, on screen %.2f, trace fraction %.3f surface %s flags 0x%x -> %.2f\n",
							vecSun.x, vecSun.y, flOnScreen, tr.fraction, tr.surface.name ? tr.surface.name : "-", (int)tr.surface.flags, flTarget );
					}
				}
			}
		}
		// (a moment to appear and to go: no popping when the sun passes a pole)
		const float flStep = flDt / 0.12f;
		s_flSunSeen = ( flTarget > s_flSunSeen ) ? Min( flTarget, s_flSunSeen + flStep ) : Max( flTarget, s_flSunSeen - flStep );
		data.vecFlare.Init( vecSun.x, vecSun.y, s_flSunSeen * r_hl2rpm_flare.GetFloat(), r_hl2rpm_flare_threshold.GetFloat() );
	}

	data.vecLumBlocks.Init( ( (float)vw / LUM_BLOCKS_X ) / fbw, ( (float)vh / LUM_BLOCKS_Y ) / fbh, LUM_BLOCKS_X, LUM_BLOCKS_Y );
	data.vecFrameTexel.Init( 1.0f / fbw, 1.0f / fbh, (float)fbw, (float)fbh );
	const float flBx = (float)lw[1] / vw / aw, flBy = (float)lh[1] / vh / ah;
	data.vecFrameToBloom.Init( fbw * flBx, fbh * flBy, -vx * flBx, -vy * flBy );

	data.vecLevelTexel[0].Init( 1.0f / fbw, 1.0f / fbh, 1.0f, 1.0f );
	data.vecLevelRect[0].Init( ( vx + 0.5f ) / fbw, ( vy + 0.5f ) / fbh, ( vx + vw - 0.5f ) / fbw, ( vy + vh - 0.5f ) / fbh );
	for ( int k = 1; k < POSTFX_LEVELS; k++ )
	{
		data.vecLevelTexel[k].Init( 1.0f / aw, 1.0f / ah, 1.0f, 1.0f );
		data.vecLevelRect[k].Init( ( lx[k] + 0.5f ) / aw, ( ly[k] + 0.5f ) / ah, ( lx[k] + lw[k] - 0.5f ) / aw, ( ly[k] + lh[k] - 0.5f ) / ah );
	}

	QUEUE_FIRE( CommitPostFXData, data );

	CMatRenderContextPtr pRenderContext( materials );

	// the frame as it is now (after FXAA)
	Rect_t rect;
	rect.x = vx;
	rect.y = vy;
	rect.width = vw;
	rect.height = vh;
	pRenderContext->CopyRenderTargetToTextureEx( pFrame, 0, &rect, &rect );

	// metering: blocks -> average -> adapted (into a temporary, then copied back)
	pRenderContext->PushRenderTargetAndViewport( g_tex_LumBlocks, 0, 0, LUM_BLOCKS_X, LUM_BLOCKS_Y );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatLum[0], 0, 0, LUM_BLOCKS_X, LUM_BLOCKS_Y,
		vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_LumAvg, 0, 0, 1, 1 );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatLum[1], 0, 0, 1, 1, 0, 0, LUM_BLOCKS_X - 1, LUM_BLOCKS_Y - 1, LUM_BLOCKS_X, LUM_BLOCKS_Y );
	pRenderContext->PopRenderTargetAndViewport();

	pRenderContext->PushRenderTargetAndViewport( g_tex_LumTmp, 0, 0, 1, 1 );
	pRenderContext->DrawScreenSpaceRectangle( g_pMatLum[2], 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	pRenderContext->CopyRenderTargetToTextureEx( g_tex_LumCur, 0, NULL, NULL );
	pRenderContext->PopRenderTargetAndViewport();

	// bloom: the frame's bright part -> level 1, down to level 5, back up adding each level
	if ( bBloom )
	{
		pRenderContext->PushRenderTargetAndViewport( g_tex_BloomA, lx[1], ly[1], lw[1], lh[1] );
		pRenderContext->DrawScreenSpaceRectangle( g_pMatBloomPre, 0, 0, lw[1], lh[1],
			vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );
		pRenderContext->PopRenderTargetAndViewport();

		for ( int k = 2; k < POSTFX_LEVELS; k++ )
		{
			pRenderContext->PushRenderTargetAndViewport( IsLevelInA( k ) ? g_tex_BloomA : g_tex_BloomB, lx[k], ly[k], lw[k], lh[k] );
			pRenderContext->DrawScreenSpaceRectangle( g_pMatBloomDown[k], 0, 0, lw[k], lh[k],
				lx[k - 1], ly[k - 1], lx[k - 1] + lw[k - 1] - 1, ly[k - 1] + lh[k - 1] - 1, aw, ah );
			pRenderContext->PopRenderTargetAndViewport();
		}

		for ( int k = POSTFX_LEVELS - 1; k >= 2; k-- )
		{
			pRenderContext->PushRenderTargetAndViewport( IsLevelInA( k - 1 ) ? g_tex_BloomA : g_tex_BloomB, lx[k - 1], ly[k - 1], lw[k - 1], lh[k - 1] );
			pRenderContext->DrawScreenSpaceRectangle( g_pMatBloomUp[k], 0, 0, lw[k - 1], lh[k - 1],
				lx[k], ly[k], lx[k] + lw[k] - 1, ly[k] + lh[k] - 1, aw, ah );
			pRenderContext->PopRenderTargetAndViewport();
		}
	}

	// the combine, over the frame
	pRenderContext->DrawScreenSpaceRectangle( g_pMatFinal, vx, vy, vw, vh,
		vx, vy, vx + vw - 1, vy + vh - 1, fbw, fbh );

	if ( r_hl2rpm_exposure_debug.GetBool() )
		PrintAdaptation( data );
}

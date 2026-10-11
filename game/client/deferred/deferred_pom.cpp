//========= HL2RPM ============================================================//
//
// Purpose: Height maps for parallax occlusion mapping, made of the bump maps.
//
// The stock materials have no height maps - but a bump map is the slope of one.
// When a brush material with a bump map is given its deferred shader
// (materialsystem_passthru.cpp), the normal map is read from its .vtf at a
// small size, turned into slopes and integrated (the Poisson equation, solved
// in the frequency domain - textures tile, so the periodic solution is the
// right one). The result is a procedural texture "_pom/<bump map>", shared by
// all the materials with that bump map; the shaders march it
// (stdshaders/hl2rpm_pom_fxc.h).
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_pom.h"

#include "filesystem.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "tier1/utldict.h"
#include "tier1/utlbuffer.h"
#include "hl2rpm_crashdebug.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_pom_size( "r_deferred_pom_size", "256", 0,
	"Parallax occlusion mapping: the size of the height maps made of the bump maps (takes effect as materials load)" );
static ConVar r_deferred_pom_info( "r_deferred_pom_info", "0", 0, "Dev: print every height map as it is made" );

class CPomHeightRegen : public ITextureRegenerator
{
public:
	CPomHeightRegen() : m_nSize( 0 ) {}

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		if ( !pVTFTexture || m_Bits.Count() == 0 )
			return;
		const int w = pVTFTexture->Width(), h = pVTFTexture->Height();
		unsigned char *pDest = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pDest || w != m_nSize || h != m_nSize )
			return;
		const int nBytes = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		for ( int i = 0; i < w * h; i++ )
		{
			for ( int c = 0; c < nBytes; c++ )
				pDest[ i * nBytes + c ] = m_Bits[i];
		}
	}

	virtual void Release() {}

	CUtlVector< unsigned char > m_Bits;
	int m_nSize;
};

struct PomEntry_t
{
	char szTexture[ MAX_PATH ];
	CPomHeightRegen *pRegen;
	CTextureReference tex;
	bool bOk;
};

static CUtlDict< PomEntry_t *, int > s_PomEntries;

// ---- a radix-2 FFT (in place; nCount a power of two) ----
static void FFT( float *pRe, float *pIm, int nCount, int nStride, bool bInverse )
{
	for ( int i = 1, j = 0; i < nCount; i++ )
	{
		int nBit = nCount >> 1;
		for ( ; j & nBit; nBit >>= 1 )
			j ^= nBit;
		j ^= nBit;
		if ( i < j )
		{
			V_swap( pRe[ i * nStride ], pRe[ j * nStride ] );
			V_swap( pIm[ i * nStride ], pIm[ j * nStride ] );
		}
	}
	for ( int nLen = 2; nLen <= nCount; nLen <<= 1 )
	{
		const float flAngle = 2.0f * M_PI_F / nLen * ( bInverse ? 1.0f : -1.0f );
		const float flWRe = cosf( flAngle ), flWIm = sinf( flAngle );
		for ( int i = 0; i < nCount; i += nLen )
		{
			float flRe = 1.0f, flIm = 0.0f;
			for ( int k = 0; k < nLen / 2; k++ )
			{
				const int a = ( i + k ) * nStride, b = ( i + k + nLen / 2 ) * nStride;
				const float flTRe = pRe[b] * flRe - pIm[b] * flIm;
				const float flTIm = pRe[b] * flIm + pIm[b] * flRe;
				pRe[b] = pRe[a] - flTRe;
				pIm[b] = pIm[a] - flTIm;
				pRe[a] += flTRe;
				pIm[a] += flTIm;
				const float flNRe = flRe * flWRe - flIm * flWIm;
				flIm = flRe * flWIm + flIm * flWRe;
				flRe = flNRe;
			}
		}
	}
}

static void FFT2D( float *pRe, float *pIm, int n, bool bInverse )
{
	for ( int y = 0; y < n; y++ )
		FFT( pRe + y * n, pIm + y * n, n, 1, bInverse );
	for ( int x = 0; x < n; x++ )
		FFT( pRe + x, pIm + x, n, n, bInverse );
}

// the normal map at ( nSize x nSize ) as RGBA; false = no such texture / not square powers of two
static bool LoadNormalMap( const char *pszName, int nWanted, CUtlVector< unsigned char > &rgba, int &nSize )
{
	char szPath[ MAX_PATH ];
	V_snprintf( szPath, sizeof( szPath ), "materials/%s.vtf", pszName );
	V_FixSlashes( szPath, '/' );

	CUtlBuffer buf;
	if ( !filesystem->ReadFile( szPath, "GAME", buf ) )
		return false;

	IVTFTexture *pVTF = CreateVTFTexture();
	bool bOk = false;
	if ( pVTF->Unserialize( buf ) && pVTF->Width() == pVTF->Height() && IsPowerOfTwo( pVTF->Width() ) && pVTF->Width() >= 16 )
	{
		// the mip that is no larger than wanted
		int iMip = 0;
		int nMipSize = pVTF->Width();
		while ( nMipSize > nWanted && iMip < pVTF->MipCount() - 1 )
		{
			nMipSize >>= 1;
			iMip++;
		}
		int w, h, d;
		pVTF->ComputeMipLevelDimensions( iMip, &w, &h, &d );
		if ( w == h && IsPowerOfTwo( w ) && w >= 16 )
		{
			rgba.SetCount( w * h * 4 );
			bOk = ImageLoader::ConvertImageFormat( pVTF->ImageData( 0, 0, iMip ), pVTF->Format(),
				rgba.Base(), IMAGE_FORMAT_RGBA8888, w, h );
			nSize = w;
		}
	}
	DestroyVTFTexture( pVTF );
	return bOk;
}

static int __cdecl CompareFloats( const void *pA, const void *pB )
{
	const float a = *(const float *)pA, b = *(const float *)pB;
	return ( a < b ) ? -1 : ( a > b ) ? 1 : 0;
}

static bool BuildHeightMap( const char *pszBump, bool bSSBump, CPomHeightRegen *pRegen, bool &bFlippedGreen )
{
	bFlippedGreen = false;
	CUtlVector< unsigned char > rgba;
	int n = 0;
	if ( !LoadNormalMap( pszBump, clamp( r_deferred_pom_size.GetInt(), 32, 1024 ), rgba, n ) )
		return false;

	const int nCount = n * n;
	CUtlVector< float > gxRe, gxIm, gyRe, gyIm;
	gxRe.SetCount( nCount ); gxIm.SetCount( nCount ); gyRe.SetCount( nCount ); gyIm.SetCount( nCount );

	// the slopes of the surface the normals describe: dh/du = -nx / nz, dh/dv = -ny / nz
	static const Vector s_vecBasis[3] = {
		Vector( 0.81649658f, 0.0f, 0.57735027f ),
		Vector( -0.40824829f, 0.70710678f, 0.57735027f ),
		Vector( -0.40824829f, -0.70710678f, 0.57735027f ) };
	for ( int i = 0; i < nCount; i++ )
	{
		const float r = rgba[ i * 4 ] * ( 1.0f / 255.0f ), g = rgba[ i * 4 + 1 ] * ( 1.0f / 255.0f ), b = rgba[ i * 4 + 2 ] * ( 1.0f / 255.0f );
		Vector vecNormal;
		if ( bSSBump )
			vecNormal = s_vecBasis[0] * r + s_vecBasis[1] * g + s_vecBasis[2] * b;
		else
			vecNormal.Init( r * 2.0f - 1.0f, g * 2.0f - 1.0f, b * 2.0f - 1.0f );
		VectorNormalize( vecNormal );
		const float flInvZ = 1.0f / Max( vecNormal.z, 0.25f );
		gxRe[i] = clamp( -vecNormal.x * flInvZ, -3.0f, 3.0f );
		gyRe[i] = clamp( -vecNormal.y * flInvZ, -3.0f, 3.0f );
		gxIm[i] = gyIm[i] = 0.0f;
	}

	// the height whose slopes come closest: h^ = -i ( kx gx^ + ky gy^ ) / ( kx^2 + ky^2 )
	FFT2D( gxRe.Base(), gxIm.Base(), n, false );
	FFT2D( gyRe.Base(), gyIm.Base(), n, false );

	// Which way the green channel points differs from texture to texture (the tools that made
	// them don't agree). The slopes of a real surface have no curl ( kx gy^ = ky gx^ ): the sign
	// of gy that leaves less of it is the right one. With the wrong one the "height" is a field
	// of saddles - every tile a chevron.
	{
		double flCurl = 0.0, flCurlFlipped = 0.0;
		for ( int y = 0; y < n; y++ )
		{
			const float ky = ( y < n / 2 ) ? y : y - n;
			for ( int x = 0; x < n; x++ )
			{
				const float kx = ( x < n / 2 ) ? x : x - n;
				const int i = y * n + x;
				const float aRe = kx * gyRe[i], aIm = kx * gyIm[i];
				const float bRe = ky * gxRe[i], bIm = ky * gxIm[i];
				flCurl += ( aRe - bRe ) * ( aRe - bRe ) + ( aIm - bIm ) * ( aIm - bIm );
				flCurlFlipped += ( aRe + bRe ) * ( aRe + bRe ) + ( aIm + bIm ) * ( aIm + bIm );
			}
		}
		bFlippedGreen = flCurlFlipped < flCurl;
		if ( bFlippedGreen )
		{
			for ( int i = 0; i < nCount; i++ )
			{
				gyRe[i] = -gyRe[i];
				gyIm[i] = -gyIm[i];
			}
		}
	}

	// (the broad swells go: a normal map holds them badly - 8 bits a slope - and integrated they
	// are domes and bowls across the whole texture that the eye reads as the surface swimming)
	const float flCut = 2.0f * M_PI_F * 2.5f / n;
	const float flCut2 = flCut * flCut;
	const float flSmooth = 1.1f * n / 256.0f;
	const float flSmooth2 = flSmooth * flSmooth * 0.5f;
	for ( int y = 0; y < n; y++ )
	{
		const float ky = 2.0f * M_PI_F * ( ( y < n / 2 ) ? y : y - n ) / n;
		for ( int x = 0; x < n; x++ )
		{
			const float kx = 2.0f * M_PI_F * ( ( x < n / 2 ) ? x : x - n ) / n;
			const int i = y * n + x;
			const float k2 = kx * kx + ky * ky;
			if ( k2 < 1e-12f )
			{
				gxRe[i] = gxIm[i] = 0.0f;
				continue;
			}
			const float flRe = kx * gxRe[i] + ky * gyRe[i];
			const float flIm = kx * gxIm[i] + ky * gyIm[i];
			// -i ( re + i im ) = im - i re
			// (and the grain of the map: every texel a step, and the ray catches on each)
			const float flGain = expf( -k2 * flSmooth2 ) / ( k2 + flCut2 );
			gxRe[i] = flIm * flGain;
			gxIm[i] = -flRe * flGain;
		}
	}
	FFT2D( gxRe.Base(), gxIm.Base(), n, true );

	// 0 .. 1 between the 2nd and the 55th percentile: the faces of the stones, tiles and bricks
	// are the top of the relief and stay where the flat texture has them; what sinks is what lies
	// between them. (And a single deep texel must not flatten the rest.)
	CUtlVector< float > sorted;
	sorted.SetCount( nCount );
	for ( int i = 0; i < nCount; i++ )
		sorted[i] = gxRe[i];
	qsort( sorted.Base(), nCount, sizeof( float ), CompareFloats );
	const float flLow = sorted[ nCount / 50 ], flHigh = sorted[ nCount * 55 / 100 ];
	// (in texels of this map over its size: a flat bump map has no relief worth marching)
	if ( flHigh - flLow < 0.02f * n / 256.0f )
		return false;

	pRegen->m_nSize = n;
	pRegen->m_Bits.SetCount( nCount );
	const float flScale = 255.0f / ( flHigh - flLow );
	for ( int i = 0; i < nCount; i++ )
		pRegen->m_Bits[i] = (unsigned char)clamp( ( gxRe[i] - flLow ) * flScale, 0.0f, 255.0f );
	return true;
}

const char *DeferredPOM_GetHeightTexture( const char *pszBumpName, bool bSSBump )
{
	if ( !pszBumpName || !pszBumpName[0] )
		return NULL;

	char szKey[ MAX_PATH ];
	V_strncpy( szKey, pszBumpName, sizeof( szKey ) );
	V_FixSlashes( szKey, '/' );
	V_strlower( szKey );
	V_StripExtension( szKey, szKey, sizeof( szKey ) );

	int idx = s_PomEntries.Find( szKey );
	if ( idx == s_PomEntries.InvalidIndex() )
	{
		PomEntry_t *pEntry = new PomEntry_t;
		V_snprintf( pEntry->szTexture, sizeof( pEntry->szTexture ), "_pom/%s", szKey );
		pEntry->pRegen = new CPomHeightRegen;
		pEntry->bOk = false;

		RPM_CRUMB( "pom: height of %s", szKey );
		const double flStart = Plat_FloatTime();
		bool bFlippedGreen = false;
		if ( BuildHeightMap( szKey, bSSBump, pEntry->pRegen, bFlippedGreen ) )
		{
			const int n = pEntry->pRegen->m_nSize;
			ITexture *pTex = materials->CreateProceduralTexture( pEntry->szTexture, TEXTURE_GROUP_OTHER, n, n, IMAGE_FORMAT_I8,
				TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL );
			if ( pTex && !pTex->IsError() )
			{
				pTex->SetTextureRegenerator( pEntry->pRegen );
				pTex->Download();
				pEntry->tex.Init( pTex );
				pTex->DecrementReferenceCount();
				pEntry->bOk = true;
			}
		}
		if ( r_deferred_pom_info.GetBool() )
			Msg( "[pom] %s: %s%s, %.1f ms\n", szKey, pEntry->bOk ? "height map made" : "no relief (flat, missing or not a square power of two)",
				bFlippedGreen ? " (green points the other way)" : "", ( Plat_FloatTime() - flStart ) * 1000.0 );
		idx = s_PomEntries.Insert( szKey, pEntry );
	}

	PomEntry_t *pEntry = s_PomEntries[ idx ];
	return pEntry->bOk ? pEntry->szTexture : NULL;
}

void DeferredPOM_Shutdown()
{
	for ( int i = s_PomEntries.First(); i != s_PomEntries.InvalidIndex(); i = s_PomEntries.Next( i ) )
	{
		PomEntry_t *pEntry = s_PomEntries[i];
		if ( pEntry->tex.IsValid() )
		{
			pEntry->tex->SetTextureRegenerator( NULL );
			pEntry->tex.Shutdown();
		}
		delete pEntry->pRegen;
		delete pEntry;
	}
	s_PomEntries.RemoveAll();
}

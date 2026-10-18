//========= HL2RPM ============================================================//
//
// Purpose: Height maps for parallax occlusion mapping, made of the bump maps -
//          and, for materials without one, of the texture itself.
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
// Many stock brick, stone and tile materials have no bump map at all (the
// brick wall of test_deferred, brick/brickfloor001a): flat under any light and
// at any angle. Their relief is guessed from the texture: what is lighter than
// its surroundings stands out, what is darker sinks (the mortar, the gaps, the
// cracks hold dirt and shade). Measured on the 174 stock materials of those
// folders that do have bump maps, that guess has the sign of the real relief
// in 95 % of the cases where the relief is clear. The exception is light
// mortar: a light network that connects across the whole texture is taken for
// the joints (the tiles of tilefloor008a); the rest can be named in
// scripts/hl2rpm_relief.txt. Such a material gets a normal map made of that
// height as well ("_pomn/..."): its light and shade follow the relief.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_pom.h"

#include "filesystem.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "pixelwriter.h"
#include "tier1/utldict.h"
#include "tier1/utlbuffer.h"
#include "tier1/KeyValues.h"
#include "hl2rpm_crashdebug.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_pom_size( "r_deferred_pom_size", "256", 0,
	"Parallax occlusion mapping: the size of the height maps made of the bump maps (takes effect as materials load)" );
static ConVar r_deferred_pom_info( "r_deferred_pom_info", "0", 0, "Dev: print every height map as it is made" );
static ConVar r_deferred_pom_texture_size( "r_deferred_pom_texture_size", "512", 0,
	"Relief of materials without a bump map: the size of its height and normal maps (the joints of a 512 texture are a texel or two wide - at 256 they blur into slopes the march smears)" );
static ConVar r_deferred_pom_texture_normal( "r_deferred_pom_texture_normal", "1.1", 0,
	"Relief of materials without a bump map: the slope of the normal map made of it (per unit of height over one texel of a 256 map)", true, 0.0f, true, 16.0f );

// ---- the procedural textures ----

class CPomHeightRegen : public ITextureRegenerator
{
public:
	CPomHeightRegen() : m_nWidth( 0 ), m_nHeight( 0 ) {}

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		if ( !pVTFTexture || m_Bits.Count() == 0 )
			return;
		const int w = pVTFTexture->Width(), h = pVTFTexture->Height();
		unsigned char *pDest = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pDest || w != m_nWidth || h != m_nHeight )
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
	int m_nWidth, m_nHeight;
};

// a normal map (RGBA, with its mip levels made from the first)
class CPomNormalRegen : public ITextureRegenerator
{
public:
	CPomNormalRegen() : m_nWidth( 0 ), m_nHeight( 0 ) {}

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		if ( !pVTFTexture || m_Bits.Count() == 0 || pVTFTexture->Width() != m_nWidth || pVTFTexture->Height() != m_nHeight )
			return;
		CPixelWriter pixelWriter;
		pixelWriter.SetPixelMemory( pVTFTexture->Format(), pVTFTexture->ImageData( 0, 0, 0 ), pVTFTexture->RowSizeInBytes( 0 ) );
		for ( int y = 0; y < m_nHeight; y++ )
		{
			pixelWriter.Seek( 0, y );
			const unsigned char *pSrc = m_Bits.Base() + y * m_nWidth * 4;
			for ( int x = 0; x < m_nWidth; x++, pSrc += 4 )
				pixelWriter.WritePixel( pSrc[0], pSrc[1], pSrc[2], pSrc[3] );
		}
		if ( pVTFTexture->MipCount() > 1 )
			pVTFTexture->GenerateMipmaps();
	}

	virtual void Release() {}

	CUtlVector< unsigned char > m_Bits;
	int m_nWidth, m_nHeight;
};

struct PomEntry_t
{
	char szTexture[ MAX_PATH ];
	char szNormal[ MAX_PATH ];
	CPomHeightRegen *pRegen;
	CPomNormalRegen *pNormalRegen;
	CTextureReference tex;
	CTextureReference texNormal;
	bool bOk;
};

static CUtlDict< PomEntry_t *, int > s_PomEntries;

// ---- the rules of scripts/hl2rpm_relief.txt ----

struct ReliefRule_t
{
	char szKey[ 128 ];
	int iRule;
};
static CUtlVector< ReliefRule_t > s_ReliefRules;
static bool s_bReliefRulesLoaded = false;

static void LoadReliefRules()
{
	s_bReliefRulesLoaded = true;
	s_ReliefRules.RemoveAll();
	KeyValues *pKV = new KeyValues( "hl2rpm_relief" );
	if ( pKV->LoadFromFile( filesystem, "scripts/hl2rpm_relief.txt", "MOD" ) )
	{
		for ( KeyValues *pSub = pKV->GetFirstValue(); pSub; pSub = pSub->GetNextValue() )
		{
			ReliefRule_t &r = s_ReliefRules[ s_ReliefRules.AddToTail() ];
			V_strncpy( r.szKey, pSub->GetName(), sizeof( r.szKey ) );
			V_FixSlashes( r.szKey, '/' );
			const char *pszValue = pSub->GetString();
			r.iRule = !V_stricmp( pszValue, "invert" ) ? RELIEF_RULE_INVERT
				: !V_stricmp( pszValue, "none" ) ? RELIEF_RULE_NONE
				: !V_stricmp( pszValue, "keep" ) ? RELIEF_RULE_KEEP
				: RELIEF_RULE_AUTO;
		}
	}
	pKV->deleteThis();
}

int DeferredPOM_GetReliefRule( const char *pszMaterial )
{
	if ( !s_bReliefRulesLoaded )
		LoadReliefRules();
	if ( !pszMaterial )
		return RELIEF_RULE_AUTO;
	char szName[ MAX_PATH ];
	V_strncpy( szName, pszMaterial, sizeof( szName ) );
	V_FixSlashes( szName, '/' );
	for ( int i = 0; i < s_ReliefRules.Count(); i++ )
	{
		if ( V_stristr( szName, s_ReliefRules[i].szKey ) )
			return s_ReliefRules[i].iRule;
	}
	return RELIEF_RULE_AUTO;
}

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
	if ( bInverse )
	{
		const float flInv = 1.0f / nCount;
		for ( int i = 0; i < nCount; i++ )
		{
			pRe[ i * nStride ] *= flInv;
			pIm[ i * nStride ] *= flInv;
		}
	}
}

// w x h, both powers of two (rows of w)
static void FFT2D( float *pRe, float *pIm, int w, int h, bool bInverse )
{
	for ( int y = 0; y < h; y++ )
		FFT( pRe + y * w, pIm + y * w, w, 1, bInverse );
	for ( int x = 0; x < w; x++ )
		FFT( pRe + x, pIm + x, h, w, bInverse );
}

// angular frequency of bin i of n (radians per texel)
static inline float FreqOf( int i, int n )
{
	return 2.0f * M_PI_F * ( ( i < n / 2 ) ? i : i - n ) / n;
}

// a texture of the game at no more than nWanted on its longer side, as RGBA; false = no such
// texture / not powers of two
static bool LoadTextureRGBA( const char *pszName, int nWanted, CUtlVector< unsigned char > &rgba, int &w, int &h )
{
	char szPath[ MAX_PATH ];
	V_snprintf( szPath, sizeof( szPath ), "materials/%s.vtf", pszName );
	V_FixSlashes( szPath, '/' );

	CUtlBuffer buf;
	if ( !filesystem->ReadFile( szPath, "GAME", buf ) )
		return false;

	IVTFTexture *pVTF = CreateVTFTexture();
	bool bOk = false;
	if ( pVTF->Unserialize( buf ) && IsPowerOfTwo( pVTF->Width() ) && IsPowerOfTwo( pVTF->Height() )
		&& pVTF->Width() >= 16 && pVTF->Height() >= 16 && pVTF->Depth() == 1 )
	{
		// the mip that is no larger than wanted (and no smaller than 16 on either side)
		int iMip = 0;
		int mw = pVTF->Width(), mh = pVTF->Height();
		while ( Max( mw, mh ) > nWanted && Min( mw, mh ) > 16 && iMip < pVTF->MipCount() - 1 )
		{
			mw >>= 1;
			mh >>= 1;
			iMip++;
		}
		int d;
		pVTF->ComputeMipLevelDimensions( iMip, &w, &h, &d );
		if ( IsPowerOfTwo( w ) && IsPowerOfTwo( h ) && w >= 16 && h >= 16 )
		{
			rgba.SetCount( w * h * 4 );
			bOk = ImageLoader::ConvertImageFormat( pVTF->ImageData( 0, 0, iMip ), pVTF->Format(),
				rgba.Base(), IMAGE_FORMAT_RGBA8888, w, h );
		}
	}
	DestroyVTFTexture( pVTF );
	return bOk;
}

// the k-th smallest of p[ lo .. hi ] (quickselect: the array is reordered around it)
static float SelectKth( float *p, int lo, int hi, int k )
{
	while ( lo < hi )
	{
		const float flPivot = p[ ( lo + hi ) >> 1 ];
		int i = lo, j = hi;
		while ( i <= j )
		{
			while ( p[i] < flPivot ) i++;
			while ( p[j] > flPivot ) j--;
			if ( i <= j )
			{
				V_swap( p[i], p[j] );
				i++;
				j--;
			}
		}
		if ( k <= j )
			hi = j;
		else if ( k >= i )
			lo = i;
		else
			return p[k];
	}
	return p[k];
}

// two percentiles of the values (flFractionA < flFractionB) without sorting all of them
static void Percentiles( const CUtlVector< float > &values, float flFractionA, float flFractionB, float &flA, float &flB )
{
	CUtlVector< float > tmp;
	tmp.CopyArray( values.Base(), values.Count() );
	const int n = tmp.Count();
	const int a = clamp( (int)( flFractionA * n ), 0, n - 1 );
	const int b = clamp( (int)( flFractionB * n ), a, n - 1 );
	flA = SelectKth( tmp.Base(), 0, n - 1, a );
	// (everything after a is no smaller than it now)
	flB = ( b > a ) ? SelectKth( tmp.Base(), a + 1, n - 1, b ) : flA;
}

// the heights 0 .. 1 between the 2nd and the 55th percentile: the faces of the stones, tiles and
// bricks are the top of the relief and stay where the flat texture has them; what sinks is what
// lies between them (and a single deep texel must not flatten the rest). False = no relief worth
// marching (the spread under flMinSpread).
static bool NormalizeHeights( const CUtlVector< float > &heights, float flMinSpread, CPomHeightRegen *pRegen, int w, int h )
{
	float flLow, flHigh;
	Percentiles( heights, 0.02f, 0.55f, flLow, flHigh );
	if ( flHigh - flLow < flMinSpread )
		return false;
	pRegen->m_nWidth = w;
	pRegen->m_nHeight = h;
	pRegen->m_Bits.SetCount( w * h );
	const float flScale = 255.0f / ( flHigh - flLow );
	for ( int i = 0; i < w * h; i++ )
		pRegen->m_Bits[i] = (unsigned char)clamp( ( heights[i] - flLow ) * flScale, 0.0f, 255.0f );
	return true;
}

// ---- of a bump map ----

static bool BuildHeightMap( const char *pszBump, bool bSSBump, CPomHeightRegen *pRegen, bool &bFlippedGreen )
{
	bFlippedGreen = false;
	CUtlVector< unsigned char > rgba;
	int w = 0, h = 0;
	if ( !LoadTextureRGBA( pszBump, clamp( r_deferred_pom_size.GetInt(), 32, 1024 ), rgba, w, h ) )
		return false;

	const int nCount = w * h;
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
	FFT2D( gxRe.Base(), gxIm.Base(), w, h, false );
	FFT2D( gyRe.Base(), gyIm.Base(), w, h, false );

	// The stock bump maps agree on which way the green channel points (this code's way); a few
	// made by other tools don't. The slopes of a real surface have no curl ( kx gy^ = ky gx^ ):
	// with the wrong sign the "height" is a field of saddles - every tile a chevron. Turned only
	// when the other sign leaves clearly less curl: a pattern of one direction (boards, stripes)
	// has as much either way.
	{
		double flCurl = 0.0, flCurlFlipped = 0.0;
		for ( int y = 0; y < h; y++ )
		{
			const float ky = FreqOf( y, h );
			for ( int x = 0; x < w; x++ )
			{
				const float kx = FreqOf( x, w );
				const int i = y * w + x;
				const float aRe = kx * gyRe[i], aIm = kx * gyIm[i];
				const float bRe = ky * gxRe[i], bIm = ky * gxIm[i];
				flCurl += ( aRe - bRe ) * ( aRe - bRe ) + ( aIm - bIm ) * ( aIm - bIm );
				flCurlFlipped += ( aRe + bRe ) * ( aRe + bRe ) + ( aIm + bIm ) * ( aIm + bIm );
			}
		}
		bFlippedGreen = flCurlFlipped < flCurl * 0.5;
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
	// are domes and bowls across the whole texture that the eye reads as the surface swimming;
	// and the grain of the map: every texel a step, and the ray catches on each)
	const int n = Max( w, h );
	const float flCut = 2.0f * M_PI_F * 2.5f / n;
	const float flCut2 = flCut * flCut;
	const float flSmooth = 1.1f * n / 256.0f;
	const float flSmooth2 = flSmooth * flSmooth * 0.5f;
	for ( int y = 0; y < h; y++ )
	{
		const float ky = FreqOf( y, h );
		for ( int x = 0; x < w; x++ )
		{
			const float kx = FreqOf( x, w );
			const int i = y * w + x;
			const float k2 = kx * kx + ky * ky;
			if ( k2 < 1e-12f )
			{
				gxRe[i] = gxIm[i] = 0.0f;
				continue;
			}
			const float flRe = kx * gxRe[i] + ky * gyRe[i];
			const float flIm = kx * gxIm[i] + ky * gyIm[i];
			// -i ( re + i im ) = im - i re
			const float flGain = expf( -k2 * flSmooth2 ) / ( k2 + flCut2 );
			gxRe[i] = flIm * flGain;
			gxIm[i] = -flRe * flGain;
		}
	}
	FFT2D( gxRe.Base(), gxIm.Base(), w, h, true );

	// (in texels of this map over its size: a flat bump map has no relief worth marching)
	return NormalizeHeights( gxRe, 0.02f * n / 256.0f, pRegen, w, h );
}

// ---- of the texture itself ----

// the largest piece of the mask (4-neighbour, the texture wraps) as a part of all of it
static float LargestPiece( const CUtlVector< bool > &mask, int w, int h )
{
	CUtlVector< int > parent;
	parent.SetCount( w * h );
	int nSet = 0;
	for ( int i = 0; i < w * h; i++ )
	{
		parent[i] = i;
		nSet += mask[i] ? 1 : 0;
	}
	if ( nSet == 0 )
		return 0.0f;
	struct Local
	{
		static int Find( CUtlVector< int > &p, int x )
		{
			while ( p[x] != x )
			{
				p[x] = p[ p[x] ];
				x = p[x];
			}
			return x;
		}
	};
	for ( int y = 0; y < h; y++ )
	{
		for ( int x = 0; x < w; x++ )
		{
			const int i = y * w + x;
			if ( !mask[i] )
				continue;
			const int iRight = y * w + ( x + 1 ) % w;
			const int iDown = ( ( y + 1 ) % h ) * w + x;
			if ( mask[ iRight ] )
				parent[ Local::Find( parent, i ) ] = Local::Find( parent, iRight );
			if ( mask[ iDown ] )
				parent[ Local::Find( parent, i ) ] = Local::Find( parent, iDown );
		}
	}
	CUtlVector< int > count;
	count.SetCount( w * h );
	int nMost = 0;
	for ( int i = 0; i < w * h; i++ )
		count[i] = 0;
	for ( int i = 0; i < w * h; i++ )
	{
		if ( mask[i] )
			nMost = Max( nMost, ++count[ Local::Find( parent, i ) ] );
	}
	return (float)nMost / nSet;
}

// the luminance of a texture, its surroundings taken off (over a 16th of its size) and smoothed
// (over flSmooth texels)
static void TextureDetail( const CUtlVector< unsigned char > &rgba, int w, int h, CUtlVector< float > &out, float flSmooth )
{
	const int nCount = w * h;
	CUtlVector< float > re, im;
	re.SetCount( nCount );
	im.SetCount( nCount );
	for ( int i = 0; i < nCount; i++ )
	{
		re[i] = ( rgba[ i * 4 ] * 0.3f + rgba[ i * 4 + 1 ] * 0.59f + rgba[ i * 4 + 2 ] * 0.11f ) * ( 1.0f / 255.0f );
		im[i] = 0.0f;
	}
	FFT2D( re.Base(), im.Base(), w, h, false );
	const int n = Max( w, h );
	const float flBig = n / 16.0f, flSmall = flSmooth;
	for ( int y = 0; y < h; y++ )
	{
		const float ky = FreqOf( y, h );
		for ( int x = 0; x < w; x++ )
		{
			const float kx = FreqOf( x, w );
			const float k2 = kx * kx + ky * ky;
			const float flGain = ( 1.0f - expf( -k2 * flBig * flBig * 0.5f ) ) * expf( -k2 * flSmall * flSmall * 0.5f );
			re[ y * w + x ] *= flGain;
			im[ y * w + x ] *= flGain;
		}
	}
	FFT2D( re.Base(), im.Base(), w, h, true );
	out.Swap( re );
}

// light joints: does the lightest part of the detail connect across the texture while the
// darkest falls apart? (measured at no more than 128 on a side, where the stock joints are a
// texel or two wide)
static bool HasLightJoints( const CUtlVector< unsigned char > &rgba, int w, int h )
{
	CUtlVector< unsigned char > small;
	int sw = w, sh = h;
	const unsigned char *pSrc = rgba.Base();
	CUtlVector< unsigned char > tmp;
	while ( Max( sw, sh ) > 128 && Min( sw, sh ) > 16 )
	{
		const int nw = sw / 2, nh = sh / 2;
		tmp.SetCount( nw * nh * 4 );
		for ( int y = 0; y < nh; y++ )
		{
			for ( int x = 0; x < nw; x++ )
			{
				for ( int c = 0; c < 4; c++ )
				{
					const int s = pSrc[ ( ( 2 * y ) * sw + 2 * x ) * 4 + c ] + pSrc[ ( ( 2 * y ) * sw + 2 * x + 1 ) * 4 + c ]
						+ pSrc[ ( ( 2 * y + 1 ) * sw + 2 * x ) * 4 + c ] + pSrc[ ( ( 2 * y + 1 ) * sw + 2 * x + 1 ) * 4 + c ];
					tmp[ ( y * nw + x ) * 4 + c ] = (unsigned char)( s / 4 );
				}
			}
		}
		small.Swap( tmp );
		pSrc = small.Base();
		sw = nw;
		sh = nh;
	}
	CUtlVector< unsigned char > src;
	src.CopyArray( pSrc, sw * sh * 4 );
	CUtlVector< float > detail;
	TextureDetail( src, sw, sh, detail, 0.4f );
	float flDark, flLight;
	Percentiles( detail, 0.30f, 0.70f, flDark, flLight );
	CUtlVector< bool > maskDark, maskLight;
	maskDark.SetCount( sw * sh );
	maskLight.SetCount( sw * sh );
	for ( int i = 0; i < sw * sh; i++ )
	{
		maskDark[i] = detail[i] < flDark;
		maskLight[i] = detail[i] > flLight;
	}
	const float flDarkNet = LargestPiece( maskDark, sw, sh ), flLightNet = LargestPiece( maskLight, sw, sh );
	return flLightNet > flDarkNet + 0.1f && flLightNet > 0.5f;
}

// a normal map of the heights: the slope of a height of 1 over a texel of a 256 map is flSlope
static void BuildNormalMap( const CPomHeightRegen *pHeight, float flSlope, CPomNormalRegen *pNormal )
{
	const int w = pHeight->m_nWidth, h = pHeight->m_nHeight;
	pNormal->m_nWidth = w;
	pNormal->m_nHeight = h;
	pNormal->m_Bits.SetCount( w * h * 4 );
	const float k = flSlope * ( Max( w, h ) / 256.0f ) * ( 1.0f / 255.0f ) * 0.5f;
	for ( int y = 0; y < h; y++ )
	{
		for ( int x = 0; x < w; x++ )
		{
			const float dx = (float)pHeight->m_Bits[ y * w + ( x + 1 ) % w ] - pHeight->m_Bits[ y * w + ( x + w - 1 ) % w ];
			const float dy = (float)pHeight->m_Bits[ ( ( y + 1 ) % h ) * w + x ] - pHeight->m_Bits[ ( ( y + h - 1 ) % h ) * w + x ];
			Vector n( -dx * k, -dy * k, 1.0f );
			VectorNormalize( n );
			unsigned char *p = pNormal->m_Bits.Base() + ( y * w + x ) * 4;
			p[0] = (unsigned char)clamp( n.x * 127.5f + 127.5f, 0.0f, 255.0f );
			p[1] = (unsigned char)clamp( n.y * 127.5f + 127.5f, 0.0f, 255.0f );
			p[2] = (unsigned char)clamp( n.z * 127.5f + 127.5f, 0.0f, 255.0f );
			p[3] = 255;
		}
	}
}

static bool BuildTextureRelief( const char *pszBase, int iRule, CPomHeightRegen *pRegen, CPomNormalRegen *pNormalRegen, bool &bInverted )
{
	CUtlVector< unsigned char > rgba;
	int w = 0, h = 0;
	if ( !LoadTextureRGBA( pszBase, clamp( r_deferred_pom_texture_size.GetInt(), 32, 1024 ), rgba, w, h ) )
		return false;

	CUtlVector< float > detail;
	TextureDetail( rgba, w, h, detail, 0.9f );
	bInverted = ( iRule == RELIEF_RULE_INVERT ) || ( iRule == RELIEF_RULE_AUTO && HasLightJoints( rgba, w, h ) );
	if ( bInverted )
	{
		for ( int i = 0; i < detail.Count(); i++ )
			detail[i] = -detail[i];
	}
	// (a flat texture - a painted wall, a uniform floor - has no relief to guess)
	if ( !NormalizeHeights( detail, 0.035f, pRegen, w, h ) )
		return false;

	// the normal map of the soft heights: of the steep ones below its bevels were narrow and steep -
	// at a glancing view every joint was edged with a thin bright line of mirrored light
	BuildNormalMap( pRegen, r_deferred_pom_texture_normal.GetFloat(), pNormalRegen );

	// steeper sides: the shading of the texture changes over several texels where a brick ends -
	// marched as a slope, that band is the texture stretched along the view ("fur"). Flat tops,
	// flat joints, a short step between them.
	for ( int i = 0; i < pRegen->m_Bits.Count(); i++ )
	{
		float t = clamp( ( pRegen->m_Bits[i] * ( 1.0f / 255.0f ) - 0.15f ) * ( 1.0f / 0.7f ), 0.0f, 1.0f );
		t = t * t * ( 3.0f - 2.0f * t );
		pRegen->m_Bits[i] = (unsigned char)( t * 255.0f + 0.5f );
	}
	return true;
}

static ITexture *CreatePomTexture( const char *pszName, int w, int h, ImageFormat fmt, bool bMips, ITextureRegenerator *pRegen )
{
	const int nFlags = TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | ( bMips ? 0 : TEXTUREFLAGS_NOMIP );
	ITexture *pTex = materials->CreateProceduralTexture( pszName, TEXTURE_GROUP_OTHER, w, h, fmt, nFlags );
	if ( !pTex || pTex->IsError() )
		return NULL;
	pTex->SetTextureRegenerator( pRegen );
	pTex->Download();
	return pTex;
}

static void MakeKey( const char *pszName, char *pszKey, int nSize )
{
	V_strncpy( pszKey, pszName, nSize );
	V_FixSlashes( pszKey, '/' );
	V_strlower( pszKey );
	V_StripExtension( pszKey, pszKey, nSize );
}

const char *DeferredPOM_GetHeightTexture( const char *pszBumpName, bool bSSBump )
{
	if ( !pszBumpName || !pszBumpName[0] )
		return NULL;

	char szKey[ MAX_PATH ];
	MakeKey( pszBumpName, szKey, sizeof( szKey ) );

	int idx = s_PomEntries.Find( szKey );
	if ( idx == s_PomEntries.InvalidIndex() )
	{
		PomEntry_t *pEntry = new PomEntry_t;
		V_snprintf( pEntry->szTexture, sizeof( pEntry->szTexture ), "_pom/%s", szKey );
		pEntry->szNormal[0] = 0;
		pEntry->pRegen = new CPomHeightRegen;
		pEntry->pNormalRegen = NULL;
		pEntry->bOk = false;

		RPM_CRUMB( "pom: height of %s", szKey );
		const double flStart = Plat_FloatTime();
		bool bFlippedGreen = false;
		if ( BuildHeightMap( szKey, bSSBump, pEntry->pRegen, bFlippedGreen ) )
		{
			ITexture *pTex = CreatePomTexture( pEntry->szTexture, pEntry->pRegen->m_nWidth, pEntry->pRegen->m_nHeight, IMAGE_FORMAT_I8, false, pEntry->pRegen );
			if ( pTex )
			{
				pEntry->tex.Init( pTex );
				pTex->DecrementReferenceCount();
				pEntry->bOk = true;
			}
		}
		if ( r_deferred_pom_info.GetBool() )
			Msg( "[pom] %s: %s%s, %.1f ms\n", szKey, pEntry->bOk ? "height map made" : "no relief (flat, missing or not powers of two)",
				bFlippedGreen ? " (green points the other way)" : "", ( Plat_FloatTime() - flStart ) * 1000.0 );
		idx = s_PomEntries.Insert( szKey, pEntry );
	}

	PomEntry_t *pEntry = s_PomEntries[ idx ];
	return pEntry->bOk ? pEntry->szTexture : NULL;
}

bool DeferredPOM_GetTextureRelief( const char *pszBaseTexture, int iRule, const char **ppszHeight, const char **ppszNormal )
{
	*ppszHeight = *ppszNormal = NULL;
	if ( !pszBaseTexture || !pszBaseTexture[0] || iRule == RELIEF_RULE_NONE )
		return false;

	char szName[ MAX_PATH ];
	MakeKey( pszBaseTexture, szName, sizeof( szName ) );
	// (the rule is part of the key: a texture can be used by materials with different rules)
	char szKey[ MAX_PATH ];
	V_snprintf( szKey, sizeof( szKey ), "tex%d/%s", iRule, szName );

	int idx = s_PomEntries.Find( szKey );
	if ( idx == s_PomEntries.InvalidIndex() )
	{
		PomEntry_t *pEntry = new PomEntry_t;
		V_snprintf( pEntry->szTexture, sizeof( pEntry->szTexture ), "_pom/%s", szKey );
		V_snprintf( pEntry->szNormal, sizeof( pEntry->szNormal ), "_pomn/%s", szKey );
		pEntry->pRegen = new CPomHeightRegen;
		pEntry->pNormalRegen = new CPomNormalRegen;
		pEntry->bOk = false;

		RPM_CRUMB( "pom: relief of %s", szName );
		const double flStart = Plat_FloatTime();
		bool bInverted = false;
		double flBuilt = flStart;
		if ( BuildTextureRelief( szName, iRule, pEntry->pRegen, pEntry->pNormalRegen, bInverted ) )
		{
			flBuilt = Plat_FloatTime();
			ITexture *pTex = CreatePomTexture( pEntry->szTexture, pEntry->pRegen->m_nWidth, pEntry->pRegen->m_nHeight, IMAGE_FORMAT_I8, false, pEntry->pRegen );
			ITexture *pNormal = pTex ? CreatePomTexture( pEntry->szNormal, pEntry->pNormalRegen->m_nWidth, pEntry->pNormalRegen->m_nHeight,
				IMAGE_FORMAT_RGBA8888, true, pEntry->pNormalRegen ) : NULL;
			if ( pTex && pNormal )
			{
				pEntry->tex.Init( pTex );
				pEntry->texNormal.Init( pNormal );
				pEntry->bOk = true;
			}
			if ( pTex )
				pTex->DecrementReferenceCount();
			if ( pNormal )
				pNormal->DecrementReferenceCount();
		}
		if ( r_deferred_pom_info.GetBool() )
			Msg( "[pom] %s: %s%s, %.1f ms (textures %.1f)\n", szName, pEntry->bOk ? "relief made of the texture" : "no relief (flat, missing or not powers of two)",
				bInverted ? " (light joints sink)" : "", ( Plat_FloatTime() - flStart ) * 1000.0, ( Plat_FloatTime() - flBuilt ) * 1000.0 );
		idx = s_PomEntries.Insert( szKey, pEntry );
	}

	PomEntry_t *pEntry = s_PomEntries[ idx ];
	if ( !pEntry->bOk )
		return false;
	*ppszHeight = pEntry->szTexture;
	*ppszNormal = pEntry->szNormal;
	return true;
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
		if ( pEntry->texNormal.IsValid() )
		{
			pEntry->texNormal->SetTextureRegenerator( NULL );
			pEntry->texNormal.Shutdown();
		}
		delete pEntry->pRegen;
		delete pEntry->pNormalRegen;
		delete pEntry;
	}
	s_PomEntries.RemoveAll();
	s_bReliefRulesLoaded = false;
}

CON_COMMAND( r_deferred_pom_reload_rules, "Read scripts/hl2rpm_relief.txt again (the materials load anew with mat_reloadallmaterials)" )
{
	LoadReliefRules();
	Msg( "[pom] %d relief rules\n", s_ReliefRules.Count() );
}

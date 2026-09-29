//========= HL2RPM ============================================================//
//
// Purpose: Procedural noise textures for the volumetric clouds.
//
// _rt_hl2rpm_cloudnoise : tileable 3D noise, 64 slices of 62x62 (+1 texel wrap
//                         border) packed 8x8 into a 512x512 RGBA8 atlas.
//                         R = Perlin-Worley (base shape)
//                         G/B/A = Worley at 4, 8, 16 cells (erosion detail)
// _rt_hl2rpm_weathermap : tileable 2D 256x256 RGBA8
//                         R = coverage variation, G = cloud type variation,
//                         B = cirrus streaks, A = ground detail (puddles)
//
// Generated once on the CPU (a fraction of a second) through a texture
// regenerator, so they survive device resets without any files on disk.
//
//=============================================================================//

#include "cbase.h"
#include "weather/weather_render.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"

#include "tier0/memdbgon.h"

namespace
{
	// ------------------------------------------------------------------
	// periodic hash / gradient noise
	// ------------------------------------------------------------------
	inline unsigned int Hash3( int x, int y, int z, unsigned int seed )
	{
		unsigned int h = seed;
		h ^= (unsigned int)x * 0x8da6b343u;
		h ^= (unsigned int)y * 0xd8163841u;
		h ^= (unsigned int)z * 0xcb1ab31fu;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		return h;
	}

	inline int WrapI( int i, int period )
	{
		i %= period;
		return i < 0 ? i + period : i;
	}

	inline float Fade( float t ) { return t * t * t * ( t * ( t * 6.0f - 15.0f ) + 10.0f ); }

	inline float Grad( unsigned int h, float x, float y, float z )
	{
		// 12 gradient directions
		switch ( h % 12 )
		{
		case 0: return  x + y;
		case 1: return -x + y;
		case 2: return  x - y;
		case 3: return -x - y;
		case 4: return  x + z;
		case 5: return -x + z;
		case 6: return  x - z;
		case 7: return -x - z;
		case 8: return  y + z;
		case 9: return -y + z;
		case 10: return y - z;
		default: return -y - z;
		}
	}

	// Perlin noise with integer period in every axis, result roughly -1..1
	float PerlinPeriodic( float x, float y, float z, int period, unsigned int seed )
	{
		const int xi = (int)floorf( x ), yi = (int)floorf( y ), zi = (int)floorf( z );
		const float xf = x - xi, yf = y - yi, zf = z - zi;
		const float u = Fade( xf ), v = Fade( yf ), w = Fade( zf );

		float n[8];
		for ( int c = 0; c < 8; c++ )
		{
			const int dx = c & 1, dy = ( c >> 1 ) & 1, dz = ( c >> 2 ) & 1;
			const unsigned int h = Hash3( WrapI( xi + dx, period ), WrapI( yi + dy, period ), WrapI( zi + dz, period ), seed );
			n[c] = Grad( h, xf - dx, yf - dy, zf - dz );
		}
		const float x00 = Lerp( u, n[0], n[1] ), x10 = Lerp( u, n[2], n[3] );
		const float x01 = Lerp( u, n[4], n[5] ), x11 = Lerp( u, n[6], n[7] );
		return Lerp( w, Lerp( v, x00, x10 ), Lerp( v, x01, x11 ) );
	}

	// Worley (cellular) noise, periodic: 1 - distance to nearest feature point, 0..1
	float WorleyPeriodic( float x, float y, float z, int cells, unsigned int seed )
	{
		const int xi = (int)floorf( x ), yi = (int)floorf( y ), zi = (int)floorf( z );
		float flMinDist = 10.0f;
		for ( int dz = -1; dz <= 1; dz++ )
		for ( int dy = -1; dy <= 1; dy++ )
		for ( int dx = -1; dx <= 1; dx++ )
		{
			const int cx = xi + dx, cy = yi + dy, cz = zi + dz;
			const unsigned int h = Hash3( WrapI( cx, cells ), WrapI( cy, cells ), WrapI( cz, cells ), seed );
			const float px = cx + ( ( h & 0x3FF ) / 1023.0f );
			const float py = cy + ( ( ( h >> 10 ) & 0x3FF ) / 1023.0f );
			const float pz = cz + ( ( ( h >> 20 ) & 0x3FF ) / 1023.0f );
			const float d = ( px - x ) * ( px - x ) + ( py - y ) * ( py - y ) + ( pz - z ) * ( pz - z );
			flMinDist = Min( flMinDist, d );
		}
		return 1.0f - clamp( sqrtf( flMinDist ), 0.0f, 1.0f );
	}

	inline float Remap( float v, float a, float b, float c, float d )
	{
		return c + ( v - a ) / ( b - a ) * ( d - c );
	}

	inline unsigned char ToByte( float f )
	{
		return (unsigned char)clamp( (int)( f * 255.0f + 0.5f ), 0, 255 );
	}

	// ------------------------------------------------------------------
	// 3D cloud noise atlas
	// ------------------------------------------------------------------
	class CCloudNoiseRegen : public ITextureRegenerator
	{
	public:
		enum { SLICES = 64, INNER = 62, TILE = 64, TILES_PER_ROW = 8, SIZE = TILE * TILES_PER_ROW };

		virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
		{
			if ( pVTFTexture->Width() != SIZE || pVTFTexture->Height() != SIZE )
			{
				Warning( "cloud noise: unexpected texture size %dx%d\n", pVTFTexture->Width(), pVTFTexture->Height() );
				return;
			}

			// the texture system may hand us BGRA etc., the pixel writer converts
			CPixelWriter pixelWriter;
			pixelWriter.SetPixelMemory( pVTFTexture->Format(), pVTFTexture->ImageData( 0, 0, 0 ), pVTFTexture->RowSizeInBytes( 0 ) );

			for ( int s = 0; s < SLICES; s++ )
			{
				const int tx = ( s % TILES_PER_ROW ) * TILE;
				const int ty = ( s / TILES_PER_ROW ) * TILE;
				const float fz = s / (float)SLICES;	// noise space z in [0,1)

				for ( int j = 0; j < TILE; j++ )
				{
					// texel j holds voxel j-1 (1 texel wrap border on each side)
					const float fy = WrapI( j - 1, INNER ) / (float)INNER;
					pixelWriter.Seek( tx, ty + j );
					for ( int i = 0; i < TILE; i++ )
					{
						const float fx = WrapI( i - 1, INNER ) / (float)INNER;

						// Perlin fbm (low frequency billows)
						float flPerlin = 0.0f, flAmp = 1.0f, flNorm = 0.0f;
						for ( int o = 0; o < 3; o++ )
						{
							const int p = 4 << o;
							flPerlin += flAmp * PerlinPeriodic( fx * p, fy * p, fz * p, p, 0x1234u + o );
							flNorm += flAmp;
							flAmp *= 0.5f;
						}
						flPerlin = clamp( flPerlin / flNorm * 0.7f + 0.5f, 0.0f, 1.0f );

						const float w4 = WorleyPeriodic( fx * 4, fy * 4, fz * 4, 4, 0xA11u );
						const float w8 = WorleyPeriodic( fx * 8, fy * 8, fz * 8, 8, 0xB22u );
						const float w16 = WorleyPeriodic( fx * 16, fy * 16, fz * 16, 16, 0xC33u );

						// Perlin-Worley: perlin dilated by the worley fbm (Schneider 2015)
						const float flWorleyFbm = w4 * 0.625f + w8 * 0.25f + w16 * 0.125f;
						const float flPerlinWorley = clamp( Remap( flPerlin, flWorleyFbm - 1.0f, 1.0f, 0.0f, 1.0f ), 0.0f, 1.0f );

						pixelWriter.WritePixel( ToByte( flPerlinWorley ), ToByte( w4 ), ToByte( w8 ), ToByte( w16 ) );
					}
				}
			}
		}

		virtual void Release() {}
	};

	// ------------------------------------------------------------------
	// 2D weather map
	// ------------------------------------------------------------------
	class CWeatherMapRegen : public ITextureRegenerator
	{
	public:
		enum { SIZE = 256 };

		virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
		{
			if ( pVTFTexture->Width() != SIZE || pVTFTexture->Height() != SIZE )
			{
				Warning( "weather map: unexpected texture size %dx%d\n", pVTFTexture->Width(), pVTFTexture->Height() );
				return;
			}

			CPixelWriter pixelWriter;
			pixelWriter.SetPixelMemory( pVTFTexture->Format(), pVTFTexture->ImageData( 0, 0, 0 ), pVTFTexture->RowSizeInBytes( 0 ) );

			for ( int y = 0; y < SIZE; y++ )
			{
				const float fy = y / (float)SIZE;
				pixelWriter.Seek( 0, y );
				for ( int x = 0; x < SIZE; x++ )
				{
					const float fx = x / (float)SIZE;

					// big blobs of more / less cloud
					float flCov = 0.0f, flAmp = 1.0f, flNorm = 0.0f;
					for ( int o = 0; o < 4; o++ )
					{
						const int p = 3 << o;
						flCov += flAmp * PerlinPeriodic( fx * p, fy * p, 0.37f, p, 0x51u + o );
						flNorm += flAmp;
						flAmp *= 0.55f;
					}
					flCov = clamp( flCov / flNorm * 0.9f + 0.5f, 0.0f, 1.0f );

					// cloud type variation (very low frequency)
					float flType = PerlinPeriodic( fx * 2, fy * 2, 0.71f, 2, 0x77u ) * 0.5f
						+ PerlinPeriodic( fx * 4, fy * 4, 0.13f, 4, 0x78u ) * 0.25f;
					flType = clamp( flType + 0.5f, 0.0f, 1.0f );

					// cirrus fbm (the shader stretches it into streaks along the wind)
					float flCirrus = 0.0f;
					flAmp = 1.0f; flNorm = 0.0f;
					for ( int o = 0; o < 4; o++ )
					{
						const int p = 4 << o;
						flCirrus += flAmp * PerlinPeriodic( fx * p, fy * p, 0.5f, p, 0x99u + o );
						flNorm += flAmp;
						flAmp *= 0.5f;
					}
					flCirrus = clamp( flCirrus / flNorm + 0.5f, 0.0f, 1.0f );

					// ground detail for puddles (medium frequency)
					float flGround = 0.0f;
					flAmp = 1.0f; flNorm = 0.0f;
					for ( int o = 0; o < 3; o++ )
					{
						const int p = 8 << o;
						flGround += flAmp * PerlinPeriodic( fx * p, fy * p, 0.9f, p, 0xE0u + o );
						flNorm += flAmp;
						flAmp *= 0.5f;
					}
					flGround = clamp( flGround / flNorm + 0.5f, 0.0f, 1.0f );

					pixelWriter.WritePixel( ToByte( flCov ), ToByte( flType ), ToByte( flCirrus ), ToByte( flGround ) );
				}
			}
		}

		virtual void Release() {}
	};

	CCloudNoiseRegen g_CloudNoiseRegen;
	CWeatherMapRegen g_WeatherMapRegen;
	CTextureReference g_tex_CloudNoise;
	CTextureReference g_tex_WeatherMap;
}

ITexture *GetWeatherTexture_CloudNoise()
{
	if ( !g_tex_CloudNoise.IsValid() )
	{
		ITexture *pTex = materials->CreateProceduralTexture( "_rt_hl2rpm_cloudnoise", TEXTURE_GROUP_OTHER,
			CCloudNoiseRegen::SIZE, CCloudNoiseRegen::SIZE, IMAGE_FORMAT_RGBA8888,
			TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL );
		if ( pTex )
		{
			pTex->SetTextureRegenerator( &g_CloudNoiseRegen );
			pTex->Download();
			g_tex_CloudNoise.Init( pTex );
			pTex->DecrementReferenceCount();
		}
	}
	return g_tex_CloudNoise;
}

ITexture *GetWeatherTexture_WeatherMap()
{
	if ( !g_tex_WeatherMap.IsValid() )
	{
		ITexture *pTex = materials->CreateProceduralTexture( "_rt_hl2rpm_weathermap", TEXTURE_GROUP_OTHER,
			CWeatherMapRegen::SIZE, CWeatherMapRegen::SIZE, IMAGE_FORMAT_RGBA8888,
			TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL );
		if ( pTex )
		{
			pTex->SetTextureRegenerator( &g_WeatherMapRegen );
			pTex->Download();
			g_tex_WeatherMap.Init( pTex );
			pTex->DecrementReferenceCount();
		}
	}
	return g_tex_WeatherMap;
}

void ShutdownWeatherTextures()
{
	if ( g_tex_CloudNoise.IsValid() )
	{
		g_tex_CloudNoise->SetTextureRegenerator( NULL );
		g_tex_CloudNoise.Shutdown();
	}
	if ( g_tex_WeatherMap.IsValid() )
	{
		g_tex_WeatherMap->SetTextureRegenerator( NULL );
		g_tex_WeatherMap.Shutdown();
	}
}

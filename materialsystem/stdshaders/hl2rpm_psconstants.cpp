//========= HL2RPM ============================================================//
//
// Purpose: Room for the pixel shader constants c32-c223 in the shader API.
//
// The Source 2013 shader API keeps only 32 pixel shader constants, also on ps_3_0
// hardware (mat_info: m_NumPixelShaderConstants 32). Its SetPixelShaderConstant
// sends the values to D3D and then copies them into two internal arrays of 32
// registers (dynamic and desired state) without any range check. Every constant at
// c32 and above - the deferred passes use up to c71 - was written past the end of
// these heap blocks into whatever lay behind them: random heap corruption that
// crashed the game on map change, on quit and in mat_reloadallmaterials (often in the
// file system, whose VPK find cache happened to follow), and a constant cache that
// compared new values with foreign memory (skipped uploads, stale constants).
//
// Before the first draw of any shader of this DLL (SHADER_DRAW in BaseVSShader.h)
// the two arrays are found - a marker written to the last register is looked up
// through the pointers of the shader API object - and replaced by arrays of 224
// registers (ps_3_0). The shader API frees and re-allocates them only when it
// re-creates the device; the next draw notices that and replaces them again.
//
//=============================================================================//

#include "BaseVSShader.h"
#include "tier0/memalloc.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool HL2RPM_IsReadableMemory( const void *p, unsigned int nBytes );	// hl2rpm_memprobe.cpp

static const int HL2RPM_PS_CONSTANTS = 224;	// ps_3_0

namespace
{
	IShaderDynamicAPI *s_pAPI = NULL;		// shader API whose arrays were replaced
	int s_nSlot[2] = { 0, 0 };				// offsets of the two array pointers in it
	Vector4D *s_pArray[2] = { NULL, NULL };	// our arrays
	int s_nEngineCount = 0;					// registers the shader API allocates itself
	int s_nReplaced = 0;
	bool s_bGaveUp = false;

	inline Vector4D *&ArraySlot( IShaderDynamicAPI *pAPI, int nSlot )
	{
		return *(Vector4D **)( (char *)pAPI + nSlot );
	}

	Vector4D *CreateLargeCopy( const Vector4D *pSmall )
	{
		// tier0's allocator, like the shader API's new[]: it frees the array with delete[]
		// when the device is re-created
		Vector4D *pLarge = (Vector4D *)g_pMemAlloc->Alloc( HL2RPM_PS_CONSTANTS * sizeof( Vector4D ) );
		if ( !pLarge )
			return NULL;

		// NaN bit patterns: never equal to a value set later, so the first write of every
		// new register is sent to the device
		memset( pLarge, 0xFF, HL2RPM_PS_CONSTANTS * sizeof( Vector4D ) );
		memcpy( pLarge, pSmall, s_nEngineCount * sizeof( Vector4D ) );
		return pLarge;
	}

	bool ReplaceArrays( IShaderDynamicAPI *pAPI )
	{
		const Vector4D *pOld[2] = { ArraySlot( pAPI, s_nSlot[0] ), ArraySlot( pAPI, s_nSlot[1] ) };
		const unsigned int nOldSize = s_nEngineCount * sizeof( Vector4D );
		if ( !pOld[0] || !pOld[1] || pOld[0] == pOld[1] ||
			!HL2RPM_IsReadableMemory( pOld[0], nOldSize ) || !HL2RPM_IsReadableMemory( pOld[1], nOldSize ) )
			return false;

		Vector4D *pNew[2] = { CreateLargeCopy( pOld[0] ), CreateLargeCopy( pOld[1] ) };
		if ( !pNew[0] || !pNew[1] )
		{
			if ( pNew[0] )
				g_pMemAlloc->Free( pNew[0] );
			if ( pNew[1] )
				g_pMemAlloc->Free( pNew[1] );
			return false;
		}

		// the small blocks stay allocated (2 x 512 bytes): nothing should point to them any
		// more, but freeing them isn't worth the risk
		ArraySlot( pAPI, s_nSlot[0] ) = pNew[0];
		ArraySlot( pAPI, s_nSlot[1] ) = pNew[1];
		s_pArray[0] = pNew[0];
		s_pArray[1] = pNew[1];
		s_pAPI = pAPI;
		s_nReplaced++;
		return true;
	}

	// Finds the two arrays: a marker written to the last register must show up in both.
	bool FindArrays( IShaderDynamicAPI *pAPI )
	{
		static const float s_flMarker[4] = { 1.2345e-29f, -9.8765e+28f, 3.1415e-27f, -2.7182e+26f };
		static const float s_flZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const int nLast = s_nEngineCount - 1;
		const unsigned int nArraySize = s_nEngineCount * sizeof( Vector4D );

		pAPI->SetPixelShaderConstant( nLast, s_flMarker, 1, true );

		// the arrays hang off the shader API object (a global of shaderapidx9: +0x1408 and
		// +0x2360 in the 2013 SP build)
		const int nScanBegin = -0x1000, nScanEnd = 0x10000;
		const char *pObject = (const char *)pAPI;
		int nSlots[3] = { 0, 0, 0 };
		int nFound = 0;
		for ( int nPage = nScanBegin; nPage < nScanEnd && nFound < 3; nPage += 0x1000 )
		{
			if ( !HL2RPM_IsReadableMemory( pObject + nPage, 0x1000 ) )
				continue;

			for ( int nOffset = nPage; nOffset < nPage + 0x1000 && nFound < 3; nOffset += sizeof( void * ) )
			{
				const Vector4D *pArray = *(const Vector4D * const *)( pObject + nOffset );
				if ( (uintp)pArray < 0x10000 || ( (uintp)pArray & 3 ) != 0 ||
					!HL2RPM_IsReadableMemory( pArray, nArraySize ) )
					continue;

				if ( memcmp( &pArray[nLast], s_flMarker, sizeof( s_flMarker ) ) == 0 )
					nSlots[nFound++] = nOffset;
			}
		}

		pAPI->SetPixelShaderConstant( nLast, s_flZero, 1, true );

		if ( nFound != 2 )
		{
			Warning( "[HL2RPM] shader API constant arrays: %d candidates (2 expected)\n", nFound );
			return false;
		}

		// both must follow every write (the zeros just written)
		const Vector4D *pA = ArraySlot( pAPI, nSlots[0] );
		const Vector4D *pB = ArraySlot( pAPI, nSlots[1] );
		if ( pA == pB || memcmp( &pA[nLast], s_flZero, sizeof( s_flZero ) ) != 0 || memcmp( &pB[nLast], s_flZero, sizeof( s_flZero ) ) != 0 )
		{
			Warning( "[HL2RPM] shader API constant arrays: candidates at +0x%x/+0x%x don't follow the writes\n", nSlots[0], nSlots[1] );
			return false;
		}

		s_nSlot[0] = nSlots[0];
		s_nSlot[1] = nSlots[1];
		return true;
	}
}

void HL2RPM_EnsurePixelShaderConstants( IShaderDynamicAPI *pShaderAPI )
{
	if ( pShaderAPI == s_pAPI )
	{
		if ( ArraySlot( pShaderAPI, s_nSlot[0] ) == s_pArray[0] && ArraySlot( pShaderAPI, s_nSlot[1] ) == s_pArray[1] )
			return;

		// the device was re-created: the shader API has small arrays again
		if ( ReplaceArrays( pShaderAPI ) )
		{
			DevMsg( "[HL2RPM] shader API pixel shader constants enlarged again (device re-created)\n" );
			return;
		}
		s_pAPI = NULL;
	}

	if ( s_bGaveUp )
		return;

	s_nEngineCount = g_pHardwareConfig->NumPixelShaderConstants();
	if ( s_nEngineCount <= 0 || s_nEngineCount >= HL2RPM_PS_CONSTANTS )
	{
		s_bGaveUp = true;	// the engine allocates enough itself
		return;
	}

	if ( !FindArrays( pShaderAPI ) || !ReplaceArrays( pShaderAPI ) )
	{
		s_bGaveUp = true;
		Warning( "[HL2RPM] can't enlarge the shader API's %d pixel shader constants: constants above c%d corrupt memory!\n",
			s_nEngineCount, s_nEngineCount - 1 );
		return;
	}

	Msg( "[HL2RPM] shader API pixel shader constants: %d -> %d (arrays at +0x%x, +0x%x)\n",
		s_nEngineCount, HL2RPM_PS_CONSTANTS, s_nSlot[0], s_nSlot[1] );
}

//-----------------------------------------------------------------------------
// The engine's own pixel shader constants (see BaseVSShader.h): c29 fog colour and
// depth-to-alpha scale, c30 light scales (tone mapping, lightmap, cubemap, gamma),
// c31 flashlight screen scale (the stock shaders set that one on every draw, it is
// saved but left alone). Read back from the arrays above - both follow every write -
// so what is restored is exactly what the engine last sent.
//-----------------------------------------------------------------------------
static const int HL2RPM_ENGINE_PSREG_FIRST = 29;
static int s_nEngineConstantRestores[2] = { 0, 0 };

void HL2RPM_SaveEngineConstants( IShaderDynamicAPI *pShaderAPI, HL2RPM_EngineConstants_t &saved )
{
	saved.m_bValid = ( pShaderAPI == s_pAPI && s_pArray[0] != NULL && ArraySlot( pShaderAPI, s_nSlot[0] ) == s_pArray[0] );
	if ( saved.m_bValid )
		memcpy( saved.m_flValues, &s_pArray[0][HL2RPM_ENGINE_PSREG_FIRST], sizeof( saved.m_flValues ) );
}

void HL2RPM_RestoreEngineConstants( IShaderDynamicAPI *pShaderAPI, const HL2RPM_EngineConstants_t &saved )
{
	if ( !saved.m_bValid || pShaderAPI != s_pAPI || ArraySlot( pShaderAPI, s_nSlot[0] ) != s_pArray[0] )
		return;

	// c29: the engine itself re-sends the fog colour while a pass is drawn (when the fog mode
	// of its snapshot asks for it) - after the shader set its constants. Such a value is the
	// newer one and stays; it keeps the depth scale in w, the data of a shader doesn't.
	const float *pFog = s_pArray[0][29].Base();
	if ( memcmp( pFog, saved.m_flValues[0], sizeof( saved.m_flValues[0] ) ) != 0 &&
		memcmp( &pFog[3], &saved.m_flValues[0][3], sizeof( float ) ) != 0 )
	{
		pShaderAPI->SetPixelShaderConstant( 29, saved.m_flValues[0], 1, true );
		s_nEngineConstantRestores[0]++;
	}

	// c30: only ever sent when the tone mapping scale changes
	if ( memcmp( s_pArray[0][30].Base(), saved.m_flValues[1], sizeof( saved.m_flValues[1] ) ) != 0 )
	{
		pShaderAPI->SetPixelShaderConstant( 30, saved.m_flValues[1], 1, true );
		s_nEngineConstantRestores[1]++;
	}
}

CON_COMMAND( mat_hl2rpm_psconstants, "Shows whether the shader API has room for pixel shader constants above c31" )
{
	// (only while the shader API still uses our arrays: it frees them when it re-creates its
	// own on a map change, until the next draw replaces those)
	if ( s_pAPI && s_pArray[0] && ArraySlot( s_pAPI, s_nSlot[0] ) == s_pArray[0] )
	{
		const Vector4D &fog = s_pArray[0][29];
		const Vector4D &scale = s_pArray[0][30];
		Msg( "engine constants: c29 fog %.3f %.3f %.3f depth scale %.5f (handed back %d times), c30 light scales %.3f %.3f %.3f %.3f (handed back %d times)\n",
			fog.x, fog.y, fog.z, fog.w, s_nEngineConstantRestores[0], scale.x, scale.y, scale.z, scale.w, s_nEngineConstantRestores[1] );
	}
	if ( s_pAPI )
		Msg( "shader API pixel shader constants: %d (engine allocates %d), arrays at +0x%x, +0x%x, replaced %d time(s)\n",
			HL2RPM_PS_CONSTANTS, s_nEngineCount, s_nSlot[0], s_nSlot[1], s_nReplaced );
	else if ( s_bGaveUp )
		Msg( "shader API pixel shader constants: not enlarged (engine allocates %d)\n", s_nEngineCount );
	else
		Msg( "shader API pixel shader constants: nothing drawn yet\n" );
}

//========= HL2RPM ============================================================//
//
// GPU timestamps of the frame's passes - D3D9 side (see hl2rpm_gpuprof_d3d.h).
// No precompiled header: <windows.h> / <d3d9.h> only.
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>	// the IIDs of <d3d9.h> defined here (no dxguid.lib)
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "hl2rpm_gpuprof_d3d.h"
#include "../shared/hl2rpm_crashdebug.h"

static IDirect3DDevice9 *s_pDevice = NULL;

struct GPUProfFrameQueries_t
{
	IDirect3DQuery9 *pDisjoint;
	IDirect3DQuery9 *pFreq;
	IDirect3DQuery9 *pMarks[ GPUPROF_MAX_MARKS ];
	bool bIssued;
};
static GPUProfFrameQueries_t s_Frames[ GPUPROF_MAX_FRAMES ];
static bool s_bQueriesFailed = false;

//-----------------------------------------------------------------------------
// Memory checks without exceptions (VirtualQuery, the last readable region cached)
//-----------------------------------------------------------------------------
static const unsigned char *s_pReadBase = NULL;
static const unsigned char *s_pReadEnd = NULL;

static bool IsReadable( const void *p, unsigned int nBytes )
{
	const unsigned char *pc = (const unsigned char *)p;
	if ( pc >= s_pReadBase && pc + nBytes <= s_pReadEnd )
		return true;

	MEMORY_BASIC_INFORMATION mbi;
	if ( VirtualQuery( p, &mbi, sizeof( mbi ) ) == 0 )
		return false;
	if ( mbi.State != MEM_COMMIT || ( mbi.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) || mbi.Protect == 0 )
		return false;
	s_pReadBase = (const unsigned char *)mbi.BaseAddress;
	s_pReadEnd = s_pReadBase + mbi.RegionSize;
	return pc + nBytes <= s_pReadEnd;
}

// The module whose image holds p, if its file name has "d3d9" in it (d3d9.dll; on Optimus laptops
// the device the game gets can be NVIDIA's wrapper, nvd3d9wrap.dll). Only such modules' objects are
// asked QueryInterface: a call through the vtable of an arbitrary C++ object would not be safe.
static HMODULE D3D9ModuleOf( const void *p, char *pszName, int nNameLen )
{
	MEMORY_BASIC_INFORMATION mbi;
	if ( VirtualQuery( p, &mbi, sizeof( mbi ) ) == 0 || mbi.Type != MEM_IMAGE || mbi.State != MEM_COMMIT )
		return NULL;
	HMODULE hMod = NULL;
	if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &hMod ) || !hMod )
		return NULL;
	char szPath[ MAX_PATH ];
	if ( !GetModuleFileNameA( hMod, szPath, sizeof( szPath ) ) )
		return NULL;
	const char *pszFile = strrchr( szPath, '\\' );
	pszFile = pszFile ? pszFile + 1 : szPath;
	char szLower[ MAX_PATH ];
	int i = 0;
	for ( ; pszFile[ i ] && i < MAX_PATH - 1; i++ )
		szLower[ i ] = (char)tolower( (unsigned char)pszFile[ i ] );
	szLower[ i ] = 0;
	if ( !strstr( szLower, "d3d9" ) )
		return NULL;
	_snprintf( pszName, nNameLen, "%s", pszFile );
	return hMod;
}

static bool IsCodeOf( HMODULE hMod, UINT_PTR uAddr )
{
	HMODULE hOther = NULL;
	if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)uAddr, &hOther ) )
		return false;
	if ( hOther != hMod )
		return false;
	MEMORY_BASIC_INFORMATION mbi;
	if ( VirtualQuery( (const void *)uAddr, &mbi, sizeof( mbi ) ) == 0 )
		return false;
	return ( mbi.Protect & ( PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) != 0;
}

bool GPUProfD3D_FindDevice( char *pszInfo, int nInfoLen )
{
	if ( s_pDevice )
	{
		_snprintf( pszInfo, nInfoLen, "device %p (already found)", s_pDevice );
		return true;
	}

	const unsigned char *pStarts[ 64 ];
	unsigned int nSizes[ 64 ];
	const int nRegions = RPMCrash_GetModuleDataRegions( "shaderapidx9.dll", pStarts, nSizes, 64 );
	if ( nRegions <= 0 )
	{
		_snprintf( pszInfo, nInfoLen, "no data regions in shaderapidx9.dll" );
		return false;
	}

	int nCandidates = 0;
	char szModule[ 64 ] = "";
	char szSeen[ 160 ] = "";
	s_pReadBase = s_pReadEnd = NULL;

	for ( int r = 0; r < nRegions && !s_pDevice; r++ )
	{
		const UINT_PTR *pWords = (const UINT_PTR *)pStarts[ r ];
		const unsigned int nWords = nSizes[ r ] / sizeof( UINT_PTR );
		for ( unsigned int i = 0; i < nWords; i++ )
		{
			const UINT_PTR uObj = pWords[ i ];
			if ( uObj < 0x10000 || ( uObj & 3 ) )
				continue;
			if ( !IsReadable( (const void *)uObj, sizeof( UINT_PTR ) ) )
				continue;
			const UINT_PTR uVtbl = *(const UINT_PTR *)uObj;
			if ( uVtbl < 0x10000 || ( uVtbl & 3 ) || !IsReadable( (const void *)uVtbl, 3 * sizeof( UINT_PTR ) ) )
				continue;
			char szName[ 64 ];
			HMODULE hMod = D3D9ModuleOf( (const void *)uVtbl, szName, sizeof( szName ) );
			if ( !hMod )
				continue;
			// QueryInterface, AddRef, Release: code of the same module
			const UINT_PTR *pVtbl = (const UINT_PTR *)uVtbl;
			if ( !IsCodeOf( hMod, pVtbl[ 0 ] ) || !IsCodeOf( hMod, pVtbl[ 1 ] ) || !IsCodeOf( hMod, pVtbl[ 2 ] ) )
				continue;

			nCandidates++;
			if ( !strstr( szSeen, szName ) && strlen( szSeen ) + strlen( szName ) + 2 < sizeof( szSeen ) )
			{
				if ( szSeen[ 0 ] )
					strcat( szSeen, "," );
				strcat( szSeen, szName );
			}
			// the device itself, or any object of it that tells its device (surfaces, textures,
			// buffers, queries, shaders...): the engine keeps the device pointer elsewhere
			IUnknown *pUnk = (IUnknown *)uObj;
			IDirect3DDevice9 *pDev = NULL;
			const char *pszVia = NULL;
			void *pOut = NULL;
			if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DDevice9, &pOut ) ) && pOut )
			{
				pDev = (IDirect3DDevice9 *)pOut;
				pszVia = "device";
			}
			else if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DResource9, &pOut ) ) && pOut )
			{
				( (IDirect3DResource9 *)pOut )->GetDevice( &pDev );
				( (IDirect3DResource9 *)pOut )->Release();
				pszVia = "resource";
			}
			else if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DQuery9, &pOut ) ) && pOut )
			{
				( (IDirect3DQuery9 *)pOut )->GetDevice( &pDev );
				( (IDirect3DQuery9 *)pOut )->Release();
				pszVia = "query";
			}
			else if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DVertexDeclaration9, &pOut ) ) && pOut )
			{
				( (IDirect3DVertexDeclaration9 *)pOut )->GetDevice( &pDev );
				( (IDirect3DVertexDeclaration9 *)pOut )->Release();
				pszVia = "vertex declaration";
			}
			else if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DPixelShader9, &pOut ) ) && pOut )
			{
				( (IDirect3DPixelShader9 *)pOut )->GetDevice( &pDev );
				( (IDirect3DPixelShader9 *)pOut )->Release();
				pszVia = "pixel shader";
			}
			else if ( SUCCEEDED( pUnk->QueryInterface( IID_IDirect3DVertexShader9, &pOut ) ) && pOut )
			{
				( (IDirect3DVertexShader9 *)pOut )->GetDevice( &pDev );
				( (IDirect3DVertexShader9 *)pOut )->Release();
				pszVia = "vertex shader";
			}
			if ( pDev )
			{
				// keep our reference: the device outlives the profiler
				s_pDevice = pDev;
				_snprintf( szModule, sizeof( szModule ), "%s via %s", szName, pszVia );
				break;
			}
		}
	}

	if ( !s_pDevice )
	{
		_snprintf( pszInfo, nInfoLen, "no IDirect3DDevice9 among %d COM objects (%s) in shaderapidx9 globals", nCandidates, szSeen[ 0 ] ? szSeen : "-" );
		return false;
	}

	// is the timestamp query there at all (a NULL out pointer only asks for support)
	if ( FAILED( s_pDevice->CreateQuery( D3DQUERYTYPE_TIMESTAMP, NULL ) ) )
	{
		_snprintf( pszInfo, nInfoLen, "device %p (%s) found, but the driver has no D3DQUERYTYPE_TIMESTAMP", s_pDevice, szModule );
		s_pDevice->Release();
		s_pDevice = NULL;
		return false;
	}
	_snprintf( pszInfo, nInfoLen, "device %p of %s (%d candidates checked)", s_pDevice, szModule, nCandidates );
	return true;
}

bool GPUProfD3D_HasDevice()
{
	return s_pDevice != NULL;
}

static bool EnsureQueries( int iSlot )
{
	if ( !s_pDevice || s_bQueriesFailed || iSlot < 0 || iSlot >= GPUPROF_MAX_FRAMES )
		return false;

	GPUProfFrameQueries_t &f = s_Frames[ iSlot ];
	if ( f.pDisjoint )
		return true;

	bool bOk = SUCCEEDED( s_pDevice->CreateQuery( D3DQUERYTYPE_TIMESTAMPDISJOINT, &f.pDisjoint ) )
		&& SUCCEEDED( s_pDevice->CreateQuery( D3DQUERYTYPE_TIMESTAMPFREQ, &f.pFreq ) );
	for ( int i = 0; bOk && i < GPUPROF_MAX_MARKS; i++ )
		bOk = SUCCEEDED( s_pDevice->CreateQuery( D3DQUERYTYPE_TIMESTAMP, &f.pMarks[ i ] ) );

	if ( !bOk )
	{
		s_bQueriesFailed = true;
		return false;
	}
	return true;
}

bool GPUProfD3D_BeginFrame( int iSlot )
{
	if ( !EnsureQueries( iSlot ) )
		return false;
	GPUProfFrameQueries_t &f = s_Frames[ iSlot ];
	f.pDisjoint->Issue( D3DISSUE_BEGIN );
	f.bIssued = true;
	return true;
}

bool GPUProfD3D_Timestamp( int iSlot, int iMark )
{
	if ( iMark < 0 || iMark >= GPUPROF_MAX_MARKS || !EnsureQueries( iSlot ) )
		return false;
	s_Frames[ iSlot ].pMarks[ iMark ]->Issue( D3DISSUE_END );
	return true;
}

bool GPUProfD3D_EndFrame( int iSlot )
{
	if ( !EnsureQueries( iSlot ) )
		return false;
	GPUProfFrameQueries_t &f = s_Frames[ iSlot ];
	f.pFreq->Issue( D3DISSUE_END );
	f.pDisjoint->Issue( D3DISSUE_END );
	return true;
}

int GPUProfD3D_Collect( int iSlot, int nMarks, unsigned __int64 *pTicks, unsigned __int64 *pFreq )
{
	if ( !s_pDevice || iSlot < 0 || iSlot >= GPUPROF_MAX_FRAMES )
		return -1;
	GPUProfFrameQueries_t &f = s_Frames[ iSlot ];
	if ( !f.pDisjoint || !f.bIssued )
		return -1;

	// no D3DGETDATA_FLUSH: never wait for the GPU here
	BOOL bDisjoint = TRUE;
	HRESULT hr = f.pDisjoint->GetData( &bDisjoint, sizeof( bDisjoint ), 0 );
	if ( hr == S_FALSE )
		return 0;
	if ( hr != S_OK )
	{
		f.bIssued = false;
		return -1;
	}
	UINT64 uFreq = 0;
	hr = f.pFreq->GetData( &uFreq, sizeof( uFreq ), 0 );
	if ( hr == S_FALSE )
		return 0;
	if ( hr != S_OK || bDisjoint || uFreq == 0 )
	{
		f.bIssued = false;
		return -1;
	}

	if ( nMarks > GPUPROF_MAX_MARKS )
		nMarks = GPUPROF_MAX_MARKS;
	for ( int i = 0; i < nMarks; i++ )
	{
		UINT64 uTick = 0;
		hr = f.pMarks[ i ]->GetData( &uTick, sizeof( uTick ), 0 );
		if ( hr == S_FALSE )
			return 0;
		if ( hr != S_OK )
		{
			f.bIssued = false;
			return -1;
		}
		pTicks[ i ] = uTick;
	}
	*pFreq = uFreq;
	f.bIssued = false;
	return 1;
}

void GPUProfD3D_Release()
{
	for ( int s = 0; s < GPUPROF_MAX_FRAMES; s++ )
	{
		GPUProfFrameQueries_t &f = s_Frames[ s ];
		if ( f.pDisjoint ) f.pDisjoint->Release();
		if ( f.pFreq ) f.pFreq->Release();
		for ( int i = 0; i < GPUPROF_MAX_MARKS; i++ )
		{
			if ( f.pMarks[ i ] )
				f.pMarks[ i ]->Release();
		}
		memset( &f, 0, sizeof( f ) );
	}
	s_bQueriesFailed = false;
}

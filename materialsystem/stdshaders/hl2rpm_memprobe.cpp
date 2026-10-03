//========= HL2RPM ============================================================//
//
// Purpose: Memory probing for hl2rpm_psconstants.cpp. Pure Win32, kept apart
//          from the Source headers.
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// True if [p, p + nBytes) is committed readable memory. Asked through VirtualQuery
// instead of touching the memory: an access violation, even a handled one, would
// reach the crash debugger's vectored handler and be reported as a crash.
bool HL2RPM_IsReadableMemory( const void *p, unsigned int nBytes )
{
	const char *pCur = (const char *)p;
	const char *pEnd = pCur + nBytes;
	if ( pEnd < pCur )
		return false;

	const DWORD dwReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

	while ( pCur < pEnd )
	{
		MEMORY_BASIC_INFORMATION mbi;
		if ( VirtualQuery( pCur, &mbi, sizeof( mbi ) ) != sizeof( mbi ) )
			return false;
		if ( mbi.State != MEM_COMMIT || ( mbi.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) || !( mbi.Protect & dwReadable ) )
			return false;
		pCur = (const char *)mbi.BaseAddress + mbi.RegionSize;
	}
	return true;
}

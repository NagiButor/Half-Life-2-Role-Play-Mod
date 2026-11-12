//========= HL2RPM ============================================================//
//
// Frame profiler - see deferred_gpuprof.h.
//
// Main thread: names the passes (push/pop) and sends the timestamps through the render
// context's call queue, so they land between the very draw calls they bracket (with
// mat_queue_mode 2 the D3D calls are made by the material system's thread). The render
// thread keeps the names of each frame slot, issues the queries and, a few frames later,
// reads them without waiting (GetData without flush) and adds the times up. Once a second
// the main thread prints the averages.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_gpuprof.h"
#include "hl2rpm_gpuprof_d3d.h"
#include "tier0/fasttimer.h"
#include "tier1/callqueue.h"
#include "materialsystem/imaterialsystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar r_hl2rpm_gpuprof( "r_hl2rpm_gpuprof", "0", FCVAR_NONE,
	"HL2RPM: frame profiler - GPU time of each render pass (D3D9 timestamps) and main-thread time of the "
	"renderer's systems, printed once a second as [gpuprof] / [cpuprof] lines" );
static ConVar r_hl2rpm_gpuprof_interval( "r_hl2rpm_gpuprof_interval", "1.0", FCVAR_NONE,
	"HL2RPM: seconds between [gpuprof] lines" );

#define GPUPROF_MAX_NAMES		64
#define GPUPROF_STACK			16

//-----------------------------------------------------------------------------
// Render-thread state
//-----------------------------------------------------------------------------
struct GPUProfSlot_t
{
	const char *pszNames[ GPUPROF_MAX_MARKS ];	// the pass that starts at each mark (NULL = frame end)
	int nMarks;
	int iSeq;									// frame number of the slot, -1 = nothing pending
};
static GPUProfSlot_t s_Slots[ GPUPROF_MAX_FRAMES ];
static int s_iLastCollectedSeq = -1;
static unsigned __int64 s_uLastBeginTick = 0;
static unsigned __int64 s_uLastFreq = 0;

// accumulated results (render thread writes, main thread reads and resets)
struct GPUProfAccum_t
{
	const char *pszNames[ GPUPROF_MAX_NAMES ];
	double flMs[ GPUPROF_MAX_NAMES ];
	int nNames;
	double flFrameMs;
	int nFrames;
	double flPeriodMs;
	int nPeriods;
	int nDropped;
};
static GPUProfAccum_t s_Accum;
static CThreadFastMutex s_AccumMutex;

static int FindOrAddName( GPUProfAccum_t &a, const char *pszName )
{
	for ( int i = 0; i < a.nNames; i++ )
	{
		if ( a.pszNames[ i ] == pszName || !V_strcmp( a.pszNames[ i ], pszName ) )
			return i;
	}
	if ( a.nNames >= GPUPROF_MAX_NAMES )
		return -1;
	a.pszNames[ a.nNames ] = pszName;
	a.flMs[ a.nNames ] = 0.0;
	return a.nNames++;
}

static void RT_CollectPending( int iCurrentSeq )
{
	// oldest first; stop at the first frame the GPU has not finished
	for ( int iSeq = Max( s_iLastCollectedSeq + 1, iCurrentSeq - GPUPROF_MAX_FRAMES + 1 ); iSeq < iCurrentSeq; iSeq++ )
	{
		GPUProfSlot_t &slot = s_Slots[ iSeq % GPUPROF_MAX_FRAMES ];
		if ( slot.iSeq != iSeq || slot.nMarks < 2 )
			continue;

		unsigned __int64 uTicks[ GPUPROF_MAX_MARKS ];
		unsigned __int64 uFreq = 0;
		const int iRes = GPUProfD3D_Collect( iSeq % GPUPROF_MAX_FRAMES, slot.nMarks, uTicks, &uFreq );
		if ( iRes == 0 )
			break;

		slot.iSeq = -1;
		s_iLastCollectedSeq = iSeq;

		AUTO_LOCK( s_AccumMutex );
		if ( iRes < 0 )
		{
			s_Accum.nDropped++;
			s_uLastBeginTick = 0;
			continue;
		}

		const double flToMs = 1000.0 / (double)uFreq;
		for ( int i = 0; i + 1 < slot.nMarks; i++ )
		{
			if ( !slot.pszNames[ i ] )
				continue;
			const int iName = FindOrAddName( s_Accum, slot.pszNames[ i ] );
			if ( iName >= 0 && uTicks[ i + 1 ] >= uTicks[ i ] )
				s_Accum.flMs[ iName ] += (double)( uTicks[ i + 1 ] - uTicks[ i ] ) * flToMs;
		}
		if ( uTicks[ slot.nMarks - 1 ] >= uTicks[ 0 ] )
		{
			s_Accum.flFrameMs += (double)( uTicks[ slot.nMarks - 1 ] - uTicks[ 0 ] ) * flToMs;
			s_Accum.nFrames++;
		}
		// the GPU's frame period: start to start of consecutive frames
		if ( s_uLastBeginTick && uFreq == s_uLastFreq && uTicks[ 0 ] > s_uLastBeginTick )
		{
			const double flPeriod = (double)( uTicks[ 0 ] - s_uLastBeginTick ) * flToMs;
			if ( flPeriod < 1000.0 )
			{
				s_Accum.flPeriodMs += flPeriod;
				s_Accum.nPeriods++;
			}
		}
		s_uLastBeginTick = uTicks[ 0 ];
		s_uLastFreq = uFreq;
	}
}

static void RT_BeginFrame( int iSeq )
{
	RT_CollectPending( iSeq );

	GPUProfSlot_t &slot = s_Slots[ iSeq % GPUPROF_MAX_FRAMES ];
	slot.nMarks = 0;
	slot.iSeq = GPUProfD3D_BeginFrame( iSeq % GPUPROF_MAX_FRAMES ) ? iSeq : -1;
}

static void RT_Mark( int iSeq, const char *pszName )
{
	GPUProfSlot_t &slot = s_Slots[ iSeq % GPUPROF_MAX_FRAMES ];
	if ( slot.iSeq != iSeq || slot.nMarks >= GPUPROF_MAX_MARKS )
		return;
	if ( GPUProfD3D_Timestamp( iSeq % GPUPROF_MAX_FRAMES, slot.nMarks ) )
		slot.pszNames[ slot.nMarks++ ] = pszName;
}

static void RT_EndFrame( int iSeq )
{
	GPUProfSlot_t &slot = s_Slots[ iSeq % GPUPROF_MAX_FRAMES ];
	if ( slot.iSeq != iSeq )
		return;
	RT_Mark( iSeq, NULL );
	if ( !GPUProfD3D_EndFrame( iSeq % GPUPROF_MAX_FRAMES ) )
		slot.iSeq = -1;
}

static void RT_Release()
{
	GPUProfD3D_Release();
	for ( int i = 0; i < GPUPROF_MAX_FRAMES; i++ )
		s_Slots[ i ].iSeq = -1;
	s_iLastCollectedSeq = -1;
	s_uLastBeginTick = 0;
}

//-----------------------------------------------------------------------------
// Main thread
//-----------------------------------------------------------------------------
static bool s_bActive = false;			// this frame is being profiled
static bool s_bWasEnabled = false;
static bool s_bDeviceTried = false;
static int s_iFrameSeq = 0;
static const char *s_pszStack[ GPUPROF_STACK ];
static int s_nStack = 0;
static float s_flNextPrint = 0.0f;
static double s_flCpuFrameMs = 0.0;
static int s_nCpuFrames = 0;
static double s_flLastFrameStart = 0.0;

// main-thread CPU sections
static const char *s_pszCpuNames[ GPUPROF_MAX_NAMES ];
static double s_flCpuMs[ GPUPROF_MAX_NAMES ];
static int s_nCpuCalls[ GPUPROF_MAX_NAMES ];
static int s_nCpuNames = 0;

template < typename FN, typename... ARGS >
static void QueueOrRun( FN pfn, ARGS... args )
{
	CMatRenderContextPtr pRenderContext( materials );
	ICallQueue *pCallQueue = pRenderContext->GetCallQueue();
	if ( pCallQueue )
		pCallQueue->QueueCall( pfn, args... );
	else
		pfn( args... );
}

bool GPUProf_IsActive()
{
	return s_bActive;
}

static void PrintResults()
{
	GPUProfAccum_t a;
	{
		AUTO_LOCK( s_AccumMutex );
		a = s_Accum;
		memset( &s_Accum, 0, sizeof( s_Accum ) );
	}

	const double flCpu = s_nCpuFrames ? s_flCpuFrameMs / s_nCpuFrames : 0.0;
	char szLine[ 2048 ];
	if ( a.nFrames <= 0 )
	{
		V_snprintf( szLine, sizeof( szLine ), "[gpuprof] no GPU frames collected (dropped %d), frame %.2f ms\n", a.nDropped, flCpu );
	}
	else
	{
		int nLen = V_snprintf( szLine, sizeof( szLine ), "[gpuprof] %d fr: gpu %.2f period %.2f frame %.2f ms",
			a.nFrames, a.flFrameMs / a.nFrames, a.nPeriods ? a.flPeriodMs / a.nPeriods : 0.0, flCpu );
		for ( int i = 0; i < a.nNames && nLen < (int)sizeof( szLine ) - 40; i++ )
			nLen += V_snprintf( szLine + nLen, sizeof( szLine ) - nLen, " | %s %.2f", a.pszNames[ i ], a.flMs[ i ] / a.nFrames );
		V_strncat( szLine, "\n", sizeof( szLine ) );
	}
	Msg( "%s", szLine );

	if ( s_nCpuNames > 0 && s_nCpuFrames > 0 )
	{
		int nLen = V_snprintf( szLine, sizeof( szLine ), "[cpuprof] %d fr:", s_nCpuFrames );
		for ( int i = 0; i < s_nCpuNames && nLen < (int)sizeof( szLine ) - 40; i++ )
			nLen += V_snprintf( szLine + nLen, sizeof( szLine ) - nLen, " | %s %.2f x%.1f", s_pszCpuNames[ i ], s_flCpuMs[ i ] / s_nCpuFrames,
				(float)s_nCpuCalls[ i ] / s_nCpuFrames );
		V_strncat( szLine, "\n", sizeof( szLine ) );
		Msg( "%s", szLine );
	}

	for ( int i = 0; i < s_nCpuNames; i++ )
	{
		s_flCpuMs[ i ] = 0.0;
		s_nCpuCalls[ i ] = 0;
	}
	s_flCpuFrameMs = 0.0;
	s_nCpuFrames = 0;
}

void GPUProf_FrameBegin()
{
	s_bActive = false;
	s_nStack = 0;

	const bool bEnabled = r_hl2rpm_gpuprof.GetBool();
	if ( !bEnabled )
	{
		if ( s_bWasEnabled )
		{
			QueueOrRun( RT_Release );
			s_bWasEnabled = false;
		}
		return;
	}

	if ( !s_bDeviceTried )
	{
		s_bDeviceTried = true;
		char szInfo[ 256 ];
		const bool bFound = GPUProfD3D_FindDevice( szInfo, sizeof( szInfo ) );
		Msg( "[gpuprof] %s: %s\n", bFound ? "D3D9 timestamps on" : "GPU timing unavailable", szInfo );
	}
	if ( !GPUProfD3D_HasDevice() )
		return;

	if ( !s_bWasEnabled )
	{
		for ( int i = 0; i < GPUPROF_MAX_FRAMES; i++ )
			s_Slots[ i ].iSeq = -1;
		s_bWasEnabled = true;
		s_flNextPrint = gpGlobals->realtime + r_hl2rpm_gpuprof_interval.GetFloat();
		s_flLastFrameStart = 0.0;
	}

	// main thread's frame time (start to start of RenderView)
	const double flNow = Plat_FloatTime();
	if ( s_flLastFrameStart > 0.0 )
	{
		s_flCpuFrameMs += ( flNow - s_flLastFrameStart ) * 1000.0;
		s_nCpuFrames++;
	}
	s_flLastFrameStart = flNow;

	if ( gpGlobals->realtime >= s_flNextPrint )
	{
		s_flNextPrint = gpGlobals->realtime + Max( 0.1f, r_hl2rpm_gpuprof_interval.GetFloat() );
		PrintResults();
	}

	s_iFrameSeq++;
	s_bActive = true;
	QueueOrRun( RT_BeginFrame, s_iFrameSeq );
	QueueOrRun( RT_Mark, s_iFrameSeq, (const char *)"other" );
}

void GPUProf_FrameEnd()
{
	if ( !s_bActive )
		return;
	QueueOrRun( RT_EndFrame, s_iFrameSeq );
	s_bActive = false;
	s_nStack = 0;
}

void GPUProf_Push( const char *pszName )
{
	if ( !s_bActive )
		return;
	if ( s_nStack < GPUPROF_STACK )
		s_pszStack[ s_nStack ] = pszName;
	s_nStack++;
	QueueOrRun( RT_Mark, s_iFrameSeq, pszName );
}

void GPUProf_Pop()
{
	if ( !s_bActive || s_nStack <= 0 )
		return;
	s_nStack--;
	const char *pszResume = "other";
	if ( s_nStack > 0 )
		pszResume = s_pszStack[ Min( s_nStack, GPUPROF_STACK ) - 1 ];
	QueueOrRun( RT_Mark, s_iFrameSeq, pszResume );
}

void CPUProf_Add( const char *pszName, double flSeconds )
{
	if ( !s_bWasEnabled )
		return;
	int i = 0;
	for ( ; i < s_nCpuNames; i++ )
	{
		if ( s_pszCpuNames[ i ] == pszName || !V_strcmp( s_pszCpuNames[ i ], pszName ) )
			break;
	}
	if ( i == s_nCpuNames )
	{
		if ( s_nCpuNames >= GPUPROF_MAX_NAMES )
			return;
		s_pszCpuNames[ s_nCpuNames ] = pszName;
		s_flCpuMs[ s_nCpuNames ] = 0.0;
		s_nCpuCalls[ s_nCpuNames ] = 0;
		s_nCpuNames++;
	}
	s_flCpuMs[ i ] += flSeconds * 1000.0;
	s_nCpuCalls[ i ]++;
}

CCPUProfScope::CCPUProfScope( const char *pszName ) : m_pszName( pszName ), m_flStart( 0.0 )
{
	if ( r_hl2rpm_gpuprof.GetBool() )
		m_flStart = Plat_FloatTime();
}

CCPUProfScope::~CCPUProfScope()
{
	if ( m_flStart > 0.0 )
		CPUProf_Add( m_pszName, Plat_FloatTime() - m_flStart );
}

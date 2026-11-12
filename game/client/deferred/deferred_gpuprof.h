//========= HL2RPM ============================================================//
//
// Frame profiler: GPU time of every pass (D3D9 timestamp queries, hl2rpm_gpuprof_d3d.cpp)
// and main-thread time of the renderer's CPU work. r_hl2rpm_gpuprof 1 prints once a second:
//   [gpuprof] gpu <ms> period <ms> cpu <ms> | <pass> <ms> | ...
//   [cpuprof] <system> <ms> | ...
// A pass is the time between the timestamp that opened it and the next one; nested passes
// (GPUPROF_SCOPE inside another) take their time out of the outer one, so the passes add up
// to the GPU time of the frame. "period" - between the starts of consecutive frames on the GPU
// (with the HUD and the present; ~ gpu when the GPU is the limit).
//
//=============================================================================//

#ifndef DEFERRED_GPUPROF_H
#define DEFERRED_GPUPROF_H
#pragma once

bool GPUProf_IsActive();

void GPUProf_FrameBegin();
void GPUProf_FrameEnd();

// pszName: a string literal (the pointer is kept until the frame is collected)
void GPUProf_Push( const char *pszName );
void GPUProf_Pop();

class CGPUProfScope
{
public:
	CGPUProfScope( const char *pszName, bool bEnabled = true ) : m_bOn( bEnabled && GPUProf_IsActive() )
	{
		if ( m_bOn )
			GPUProf_Push( pszName );
	}
	~CGPUProfScope()
	{
		if ( m_bOn )
			GPUProf_Pop();
	}
private:
	bool m_bOn;
};

#define GPUPROF_CAT2( a, b )			a##b
#define GPUPROF_CAT( a, b )				GPUPROF_CAT2( a, b )
#define GPUPROF_SCOPE( name )			CGPUProfScope GPUPROF_CAT( __gpuprof_scope_, __LINE__ )( name )
#define GPUPROF_SCOPE_IF( name, cond )	CGPUProfScope GPUPROF_CAT( __gpuprof_scope_, __LINE__ )( name, cond )

// main-thread CPU time of a system (accumulated per frame, printed with the GPU line)
void CPUProf_Add( const char *pszName, double flSeconds );

class CCPUProfScope
{
public:
	CCPUProfScope( const char *pszName );
	~CCPUProfScope();
private:
	const char *m_pszName;
	double m_flStart;
};

#define CPUPROF_SCOPE( name )			CCPUProfScope GPUPROF_CAT( __cpuprof_scope_, __LINE__ )( name )

#endif // DEFERRED_GPUPROF_H

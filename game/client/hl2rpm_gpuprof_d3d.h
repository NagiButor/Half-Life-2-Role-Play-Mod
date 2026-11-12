//========= HL2RPM ============================================================//
//
// GPU timestamps of the frame's passes - the D3D9 side of the GPU profiler
// (deferred/deferred_gpuprof.cpp). Kept apart from the Source headers: this file
// pulls in <d3d9.h>.
//
// The engine gives the game no D3D device (IShaderAPI::GetD3DDevice is X360 only):
// the device pointer is found among the globals of shaderapidx9.dll - a pointer to
// an object whose vtable lies in d3d9.dll and that answers QueryInterface for
// IDirect3DDevice9. Everything but GPUProfD3D_FindDevice runs on the thread that
// issues the D3D calls (the material system's render thread with mat_queue_mode 2):
// the client sends the calls through the render context's call queue.
//
//=============================================================================//

#ifndef HL2RPM_GPUPROF_D3D_H
#define HL2RPM_GPUPROF_D3D_H
#pragma once

#define GPUPROF_MAX_FRAMES	8
#define GPUPROF_MAX_MARKS	128

// Look for the device (main thread, once). pszInfo receives a line for the console.
bool GPUProfD3D_FindDevice( char *pszInfo, int nInfoLen );
bool GPUProfD3D_HasDevice();

// Render thread. Queries are created on first use and kept until GPUProfD3D_Release.
bool GPUProfD3D_BeginFrame( int iSlot );
bool GPUProfD3D_Timestamp( int iSlot, int iMark );
bool GPUProfD3D_EndFrame( int iSlot );

// Render thread: the results of a frame issued a few frames ago. 1 = pTicks[0..nMarks-1] and
// *pFreq valid, 0 = not ready yet, -1 = invalid (disjoint, device lost) - drop the frame.
int GPUProfD3D_Collect( int iSlot, int nMarks, unsigned __int64 *pTicks, unsigned __int64 *pFreq );

void GPUProfD3D_Release();

#endif // HL2RPM_GPUPROF_D3D_H

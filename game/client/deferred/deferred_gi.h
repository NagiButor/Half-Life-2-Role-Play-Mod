//========= HL2RPM ============================================================//
//
// Purpose: Camera sky visibility (0 closed room .. 1 open sky) for indoor
//          volumetric sunshaft dust and dust motes.
//          (All GI passes and targets have been removed.)
//
//=============================================================================//

#ifndef DEFERRED_GI_H
#define DEFERRED_GI_H
#pragma once

class Vector;

void InitGIRTs();			// no-op (GI removed)
void ShutdownGI();

void DeferredGI_Reset();
void DeferredGI_OnRTContentLost();

// Updates camera sky visibility (1 trace/frame) and commits vecParams2.x for volumetrics/motes.
void DeferredGI_UpdateCameraSky( const Vector &origin );
float DeferredGI_GetCameraSky();

#endif // DEFERRED_GI_H

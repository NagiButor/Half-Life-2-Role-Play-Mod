//========= HL2RPM ============================================================//
//
// Purpose: How the hero feels without the HEV suit - see hl2rpm_vitals.cpp.
//
//=============================================================================//

#ifndef HL2RPM_VITALS_H
#define HL2RPM_VITALS_H
#pragma once

class QAngle;
class CUserCmd;

// the trace of the heart monitor: samples per second, one sweep across the HUD box
#define VITALS_TRACE_RATE		250
#define VITALS_TRACE_SAMPLES	400		// 1.6 s

struct VitalsHUDState_t
{
	bool		bActive;		// no suit, alive: the monitor is shown
	float		flHealth;		// 0..1 of the maximum, smoothed
	float		flCritical;		// 0..1: how close to the end
	float		flBeatFlash;	// 1 right at a beat, fading
	float		flDamageFlash;	// 1 right after a hit, fading
	const float	*pTrace;		// VITALS_TRACE_SAMPLES values, 1 = the top of the R wave at its tallest
	int			iHead;			// where the next sample is written (the sweep's write head)
};

const VitalsHUDState_t &HL2RPM_Vitals_GetHUDState();

// the view sways near the end (CBasePlayer::CalcPlayerView)
void HL2RPM_Vitals_ApplyView( QAngle &eyeAngles );

// the aim wanders near the end (ClientModeShared::CreateMove)
void HL2RPM_Vitals_CreateMove( CUserCmd *pCmd );

// the wounded view over the finished frame, before the HUD (the view render)
void HL2RPM_Vitals_DrawScreenEffect( int x, int y, int w, int h );

#endif // HL2RPM_VITALS_H

// simple header-only helper to record when the HUD starts the HealthLow sequence
#pragma once

// Single storage shared via function-local static to avoid separate translation-unit symbols.
static inline float &HealthLowTimeStorage()
{
	static float s_flHealthLowStartTime = -1.0f;
	return s_flHealthLowStartTime;
}

static inline void SetHealthLowStartTime( float flTime )
{
	HealthLowTimeStorage() = flTime;
}

static inline float GetHealthLowStartTime()
{
	return HealthLowTimeStorage();
}

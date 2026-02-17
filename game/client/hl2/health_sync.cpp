#include "health_sync.h"

static float s_flHealthLowStartTime = -1.0f;

void SetHealthLowStartTime( float flTime )
{
    s_flHealthLowStartTime = flTime;
}

float GetHealthLowStartTime()
{
    return s_flHealthLowStartTime;
}

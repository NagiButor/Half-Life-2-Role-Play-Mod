#ifndef FLASHLIGHTEFFECT_DEFERRED_H
#define FLASHLIGHTEFFECT_DEFERRED_H
#ifdef _WIN32
#pragma once
#endif

#ifndef FLASHLIGHTEFFECT_H
#include "flashlighteffect.h"
#endif

struct def_light_t;

class CFlashlightEffectDeferred : public CFlashlightEffect
{
public:
	CFlashlightEffectDeferred( int nEntIndex );
	virtual ~CFlashlightEffectDeferred();

	void SetTuning( float flDistanceScale, float flFovScale, float flIntensityScale, bool bForceVolumetrics )
	{
		m_flDistanceScale = flDistanceScale;
		m_flFovScale = flFovScale;
		m_flIntensityScale = flIntensityScale;
		m_bForceVolumetrics = bForceVolumetrics;
	}

	virtual void UpdateLight( const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance );
	virtual void LightOff();

private:
	def_light_t *m_pDefLight;

	float m_flDistanceScale = 1.0f;
	float m_flFovScale = 1.0f;
	float m_flIntensityScale = 1.0f;
	bool m_bForceVolumetrics = false;
};

#endif // FLASHLIGHTEFFECT_DEFERRED_H

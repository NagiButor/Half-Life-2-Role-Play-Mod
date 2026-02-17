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

	CFlashlightEffectDeferred( int nEntIndex = 0 );
	~CFlashlightEffectDeferred();
	virtual void UpdateLight( const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance ) OVERRIDE;

protected:
	void LightOff();

private:

	def_light_t *m_pDefLight;
};

#endif // FLASHLIGHTEFFECT_DEFERRED_H

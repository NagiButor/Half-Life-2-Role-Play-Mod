// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW
// !$MODEL && $MORPHING_VTEX
// !$MODEL && $COMPRESSED_VERTS
// !$MODEL && $SKINNING
// !$MODEL && $MORPHING_VTEX

#pragma once
#include "shaderlib/cshader.h"
class weather_post_ps30_Static_Index
{
public:
	weather_post_ps30_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_weather_post_ps30 1


class weather_post_ps30_Dynamic_Index
{
	unsigned int m_nWET : 2;
#ifdef _DEBUG
	bool m_bWET : 1;
#endif	// _DEBUG
public:
	void SetWET( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nWET = i;
#ifdef _DEBUG
		m_bWET = true;
#endif	// _DEBUG
	}

	weather_post_ps30_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
		m_nWET = 0;
#ifdef _DEBUG
		m_bWET = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bWET );
		return ( 1 * m_nWET ) + 0;
	}
};

#define shaderDynamicTest_weather_post_ps30 psh_forgot_to_set_dynamic_WET


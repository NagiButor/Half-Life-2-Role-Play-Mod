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
class deferred_decal_ps30_Static_Index
{
	unsigned int m_nPASS : 2;
	unsigned int m_nBUMPMAP : 2;
	unsigned int m_nVERTEXALPHA : 2;
#ifdef _DEBUG
	bool m_bPASS : 1;
	bool m_bBUMPMAP : 1;
	bool m_bVERTEXALPHA : 1;
#endif	// _DEBUG
public:
	void SetPASS( int i )
	{
		Assert( i >= 0 && i <= 2 );
		m_nPASS = i;
#ifdef _DEBUG
		m_bPASS = true;
#endif	// _DEBUG
	}

	void SetBUMPMAP( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nBUMPMAP = i;
#ifdef _DEBUG
		m_bBUMPMAP = true;
#endif	// _DEBUG
	}

	void SetVERTEXALPHA( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nVERTEXALPHA = i;
#ifdef _DEBUG
		m_bVERTEXALPHA = true;
#endif	// _DEBUG
	}

	deferred_decal_ps30_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
		m_nPASS = 0;
		m_nBUMPMAP = 0;
		m_nVERTEXALPHA = 0;
#ifdef _DEBUG
		m_bPASS = false;
		m_bBUMPMAP = false;
		m_bVERTEXALPHA = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPASS && m_bBUMPMAP && m_bVERTEXALPHA );
		return ( 2 * m_nPASS ) + ( 6 * m_nBUMPMAP ) + ( 12 * m_nVERTEXALPHA ) + 0;
	}
};

#define shaderStaticTest_deferred_decal_ps30 psh_forgot_to_set_static_PASS + psh_forgot_to_set_static_BUMPMAP + psh_forgot_to_set_static_VERTEXALPHA


class deferred_decal_ps30_Dynamic_Index
{
	unsigned int m_nPIXELFOGTYPE : 2;
#ifdef _DEBUG
	bool m_bPIXELFOGTYPE : 1;
#endif	// _DEBUG
public:
	void SetPIXELFOGTYPE( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nPIXELFOGTYPE = i;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = true;
#endif	// _DEBUG
	}

	deferred_decal_ps30_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
		m_nPIXELFOGTYPE = 0;
#ifdef _DEBUG
		m_bPIXELFOGTYPE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bPIXELFOGTYPE );
		return ( 1 * m_nPIXELFOGTYPE ) + 0;
	}
};

#define shaderDynamicTest_deferred_decal_ps30 psh_forgot_to_set_dynamic_PIXELFOGTYPE


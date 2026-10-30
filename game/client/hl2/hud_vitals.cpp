//========= HL2RPM ============================================================//
//
// Purpose: The HUD without the HEV suit: a heart monitor instead of the numbers,
//          in the stock HL2 box where the health is shown with the suit
//          (hl2rpm_vitals.cpp keeps the heart). The trace is swept from left to
//          right: what the sweep has just drawn is bright, the last sweep is dim, a
//          short gap runs in front of the write head. White while healthy; faster,
//          taller and yellow, orange, red as health goes down. The box flashes on a
//          hit and, near the end, pulses red with every beat - as the stock health
//          box does below 20.
//
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "hl2rpm_vitals.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include <vgui/IScheme.h>
#include <vgui/IPanel.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

#define VITALS_GAP_SAMPLES	6	// cleared in front of the write head

class CHudVitals : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudVitals, vgui::Panel );

public:
	CHudVitals( const char *pElementName );

	virtual bool ShouldDraw();
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void PaintBackground();
	virtual void Paint();

private:
	void DrawLine( IMaterial *pMaterial, const Vector2D *pAcross, float flCore, float flSoft, const Color &col );
	void DrawHead( const Vector2D &p, float flRadius, const Color &col );

	CPanelAnimationVarAliasType( float, m_flInsetX, "trace_inset_x", "3", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flInsetY, "trace_inset_y", "3", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flLineWidth, "line_width", "1.25", "proportional_float" );
	CPanelAnimationVarAliasType( float, m_flGlowWidth, "glow_width", "3", "proportional_float" );
	CPanelAnimationVar( float, m_flBaseline, "baseline", "0.55" );

	CMaterialReference m_LineMaterial;
	CMaterialReference m_GlowMaterial;
	Color m_BgColor;
	Color m_DamagedBg;

	// this frame's trace on the screen
	Vector2D m_vecPoints[ VITALS_TRACE_SAMPLES ];
	Vector2D m_vecNormals[ VITALS_TRACE_SAMPLES ];	// unit, across the line
	Vector2D m_vecMiters[ VITALS_TRACE_SAMPLES ];	// the same, longer at sharp corners: the line keeps its width
	float m_flAlpha[ VITALS_TRACE_SAMPLES ];
	bool m_bSkip[ VITALS_TRACE_SAMPLES ];			// the segment from this point to the next isn't drawn
};

DECLARE_HUDELEMENT( CHudVitals );

CHudVitals::CHudVitals( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudVitals" )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD );
	SetPaintBackgroundType( 2 );

	m_LineMaterial.Init( "vgui/white", TEXTURE_GROUP_VGUI );
	m_GlowMaterial.Init( "vgui/white_additive", TEXTURE_GROUP_VGUI );
}

bool CHudVitals::ShouldDraw()
{
	return HL2RPM_Vitals_GetHUDState().bActive && CHudElement::ShouldDraw();
}

void CHudVitals::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	m_BgColor = GetSchemeColor( "BgColor", Color( 0, 0, 0, 76 ), pScheme );
	m_DamagedBg = GetSchemeColor( "DamagedBg", Color( 180, 0, 0, 200 ), pScheme );
	SetPaintBackgroundEnabled( true );
}

static Color LerpColor( const Color &a, const Color &b, float t )
{
	t = clamp( t, 0.0f, 1.0f );
	return Color( (int)Lerp( t, (float)a.r(), (float)b.r() ), (int)Lerp( t, (float)a.g(), (float)b.g() ),
		(int)Lerp( t, (float)a.b(), (float)b.b() ), (int)Lerp( t, (float)a.a(), (float)b.a() ) );
}

void CHudVitals::PaintBackground()
{
	// near the end each beat pulses the box (the stock HealthPulse: 100 0 0 80); a hit flashes it
	const VitalsHUDState_t &s = HL2RPM_Vitals_GetHUDState();
	Color c = LerpColor( m_BgColor, Color( 100, 0, 0, 80 ), s.flBeatFlash * s.flCritical );
	c = LerpColor( c, m_DamagedBg, s.flDamageFlash * 0.5f );
	SetBgColor( c );

	BaseClass::PaintBackground();
}

// white while healthy, then the HL2 yellow, orange, and red near the end
static Color LineColor( float flHealth )
{
	static const float s_flStops[][4] =
	{
		{ 0.00f, 255.0f,  40.0f,  24.0f },
		{ 0.12f, 255.0f,  52.0f,  28.0f },
		{ 0.28f, 255.0f, 132.0f,  26.0f },
		{ 0.48f, 255.0f, 212.0f,  44.0f },
		{ 0.72f, 255.0f, 252.0f, 238.0f },
		{ 1.00f, 255.0f, 255.0f, 255.0f },
	};
	const float h = clamp( flHealth, 0.0f, 1.0f );
	for ( int i = 0; i < (int)ARRAYSIZE( s_flStops ) - 1; i++ )
	{
		if ( h <= s_flStops[i + 1][0] )
		{
			const float f = ( h - s_flStops[i][0] ) / ( s_flStops[i + 1][0] - s_flStops[i][0] );
			return Color( (int)Lerp( f, s_flStops[i][1], s_flStops[i + 1][1] ), (int)Lerp( f, s_flStops[i][2], s_flStops[i + 1][2] ),
				(int)Lerp( f, s_flStops[i][3], s_flStops[i + 1][3] ), 255 );
		}
	}
	return Color( 255, 255, 255, 255 );
}

static float SmoothStepF( float a, float b, float x )
{
	const float t = clamp( ( x - a ) / ( b - a ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

static Vector2D SegmentNormal( const Vector2D &a, const Vector2D &b )
{
	Vector2D d = b - a;
	const float flLength = d.Length();
	if ( flLength < 1e-4f )
		return Vector2D( 0.0f, -1.0f );
	return Vector2D( -d.y / flLength, d.x / flLength );
}

static inline void LineVertex( CMeshBuilder &meshBuilder, const Vector2D &p, const Color &col, float flAlpha )
{
	meshBuilder.Color4ub( col.r(), col.g(), col.b(), (unsigned char)( col.a() * clamp( flAlpha, 0.0f, 1.0f ) ) );
	meshBuilder.TexCoord2f( 0, 0.5f, 0.5f );
	meshBuilder.Position3f( p.x, p.y, 0.0f );
	meshBuilder.AdvanceVertex();
}

//-----------------------------------------------------------------------------
// The trace as a ribbon: a solid core flCore wide to each side and a soft edge
// fading out over flSoft (screen pixels), pAcross[] across the line at each point.
// Neighboring segments share their edges - overlapping quads would brighten every
// joint of the half-transparent old trace.
//-----------------------------------------------------------------------------
void CHudVitals::DrawLine( IMaterial *pMaterial, const Vector2D *pAcross, float flCore, float flSoft, const Color &col )
{
	const int N = VITALS_TRACE_SAMPLES;
	const int nPerSegment = ( flCore > 0.0f ) ? 3 : 2;

	CMatRenderContextPtr pRenderContext( materials );
	const int nMaxQuads = Max( nPerSegment, Min( pRenderContext->GetMaxVerticesToRender( pMaterial ) / 4,
		pRenderContext->GetMaxIndicesToRender() / 6 ) );

	int iFirst = 0;
	while ( iFirst < N - 1 )
	{
		// as many segments as one mesh takes
		int nQuads = 0;
		int iEnd = iFirst;
		while ( iEnd < N - 1 && nQuads + nPerSegment <= nMaxQuads )
		{
			if ( !m_bSkip[ iEnd ] )
				nQuads += nPerSegment;
			iEnd++;
		}

		if ( nQuads > 0 )
		{
			IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, pMaterial );
			CMeshBuilder meshBuilder;
			meshBuilder.Begin( pMesh, MATERIAL_QUADS, nQuads );
			for ( int i = iFirst; i < iEnd; i++ )
			{
				if ( m_bSkip[i] )
					continue;

				const Vector2D &p0 = m_vecPoints[i], &p1 = m_vecPoints[i + 1];
				const Vector2D &n0 = pAcross[i], &n1 = pAcross[i + 1];
				const float a0 = m_flAlpha[i], a1 = m_flAlpha[i + 1];
				const Vector2D c0a = p0 + n0 * flCore, c0b = p0 - n0 * flCore;
				const Vector2D c1a = p1 + n1 * flCore, c1b = p1 - n1 * flCore;

				// soft edge, one side
				LineVertex( meshBuilder, c0a + n0 * flSoft, col, 0.0f );
				LineVertex( meshBuilder, c0a, col, a0 );
				LineVertex( meshBuilder, c1a, col, a1 );
				LineVertex( meshBuilder, c1a + n1 * flSoft, col, 0.0f );
				// the core
				if ( nPerSegment == 3 )
				{
					LineVertex( meshBuilder, c0a, col, a0 );
					LineVertex( meshBuilder, c0b, col, a0 );
					LineVertex( meshBuilder, c1b, col, a1 );
					LineVertex( meshBuilder, c1a, col, a1 );
				}
				// soft edge, the other side
				LineVertex( meshBuilder, c0b, col, a0 );
				LineVertex( meshBuilder, c0b - n0 * flSoft, col, 0.0f );
				LineVertex( meshBuilder, c1b - n1 * flSoft, col, 0.0f );
				LineVertex( meshBuilder, c1b, col, a1 );
			}
			meshBuilder.End();
			pMesh->Draw();
		}
		iFirst = iEnd;
	}
}

// the write head: a soft dot
void CHudVitals::DrawHead( const Vector2D &p, float flRadius, const Color &col )
{
	const int nSides = 12;
	CMatRenderContextPtr pRenderContext( materials );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, m_GlowMaterial );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nSides );
	for ( int i = 0; i < nSides; i++ )
	{
		const float a0 = 2.0f * M_PI_F * i / nSides, a1 = 2.0f * M_PI_F * ( i + 1 ) / nSides;
		LineVertex( meshBuilder, p, col, 1.0f );
		LineVertex( meshBuilder, p + Vector2D( cosf( a0 ), sinf( a0 ) ) * flRadius, col, 0.0f );
		LineVertex( meshBuilder, p + Vector2D( cosf( a1 ), sinf( a1 ) ) * flRadius, col, 0.0f );
	}
	meshBuilder.End();
	pMesh->Draw();
}

void CHudVitals::Paint()
{
	const VitalsHUDState_t &s = HL2RPM_Vitals_GetHUDState();
	if ( !s.pTrace )
		return;

	// (meshes go to the screen as they are: the surface's own drawing adds the panel's place itself)
	int ax = 0, ay = 0;
	ipanel()->GetAbsPos( GetVPanel(), ax, ay );
	int wide, tall;
	GetSize( wide, tall );

	const float x0 = ax + m_flInsetX, x1 = ax + wide - m_flInsetX;
	const float yTop = ay + m_flInsetY, yBottom = ay + tall - m_flInsetY;
	const float yBase = ay + tall * m_flBaseline;
	// the tallest R wave reaches the top, the S wave (-0.62 of it) stays inside the bottom
	const float flAmp = Max( 1.0f, Min( yBase - yTop, ( yBottom - yBase ) / 0.65f ) );

	const int N = VITALS_TRACE_SAMPLES;
	for ( int i = 0; i < N; i++ )
	{
		const float f = (float)i / ( N - 1 );
		m_vecPoints[i].x = Lerp( f, x0, x1 );
		m_vecPoints[i].y = clamp( yBase - s.pTrace[i] * flAmp, yTop - 1.0f, yBottom + 1.0f );

		// what the sweep has just drawn is bright, the last sweep dims; the ends fade into the box
		const float flAge = (float)( ( s.iHead - 1 - i + 2 * N ) % N ) / N;
		m_flAlpha[i] = ( 1.0f - 0.68f * SmoothStepF( 0.12f, 0.98f, flAge ) ) * SmoothStepF( 0.0f, 0.06f, f ) * SmoothStepF( 1.0f, 0.94f, f );

		// the gap in front of the head (the oldest samples, about to be written over)
		const bool bInGap = ( ( i - s.iHead + 2 * N ) % N ) < VITALS_GAP_SAMPLES;
		const bool bNextInGap = ( ( i + 1 - s.iHead + 2 * N ) % N ) < VITALS_GAP_SAMPLES;
		m_bSkip[i] = ( i == N - 1 ) || bInGap || bNextInGap;
	}

	// across the line at every point; for the line itself longer at a sharp corner (the spike), so it
	// keeps its width - the wide glow doesn't: mitered, it ran past the tips of the spikes in streaks
	for ( int i = 0; i < N; i++ )
	{
		const bool bLeft = i > 0 && !m_bSkip[i - 1];
		const bool bRight = !m_bSkip[i];
		if ( bLeft && bRight )
		{
			const Vector2D n0 = SegmentNormal( m_vecPoints[i - 1], m_vecPoints[i] );
			const Vector2D n1 = SegmentNormal( m_vecPoints[i], m_vecPoints[i + 1] );
			Vector2D n = n0 + n1;
			const float flLength = n.Length();
			n = ( flLength > 1e-4f ) ? n / flLength : n0;
			m_vecNormals[i] = n;
			m_vecMiters[i] = n / Max( DotProduct2D( n, n0 ), 0.35f );
		}
		else
		{
			if ( bLeft )
				m_vecNormals[i] = SegmentNormal( m_vecPoints[i - 1], m_vecPoints[i] );
			else if ( bRight )
				m_vecNormals[i] = SegmentNormal( m_vecPoints[i], m_vecPoints[i + 1] );
			else
				m_vecNormals[i].Init( 0.0f, -1.0f );
			m_vecMiters[i] = m_vecNormals[i];
		}
	}

	const Color col = LineColor( s.flHealth );
	// the beat (more near the end) and a hit brighten the glow - as the stock numbers flare
	const float flFlash = Max( s.flBeatFlash * ( 0.25f + 0.75f * s.flCritical ), s.flDamageFlash );

	// a soft glow (additive), then the line itself
	DrawLine( m_GlowMaterial, m_vecNormals, 0.0f, m_flGlowWidth, Color( col.r(), col.g(), col.b(), (int)( 255.0f * Min( 1.0f, 0.16f + 0.24f * flFlash ) ) ) );
	DrawLine( m_LineMaterial, m_vecMiters, Max( 0.0f, m_flLineWidth * 0.5f - 0.5f ), 1.0f, Color( col.r(), col.g(), col.b(), 235 ) );

	// the write head
	const int iNewest = ( s.iHead - 1 + N ) % N;
	DrawHead( m_vecPoints[ iNewest ], m_flGlowWidth * 0.9f,
		Color( col.r(), col.g(), col.b(), (int)( 200.0f * m_flAlpha[ iNewest ] ) ) );
}

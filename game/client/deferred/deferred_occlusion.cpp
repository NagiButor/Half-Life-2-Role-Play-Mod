//========= HL2RPM ============================================================//
//
// Purpose: Occlusion culling - what stands behind a wall is not drawn.
//
// The engine draws whatever the map's visibility data lets through: on an open
// map of a town that is nearly everything - the props and people of every yard
// and street behind the houses in front of the player, twice a frame (G-buffer
// and composite). Maps can be given occluder brushes by hand; this needs none.
//
// Every frame the largest walls near the camera - the opaque brush faces of the
// map (deferred_worldmesh.cpp reads them out of the .bsp) - are drawn into a
// small depth buffer on the CPU. A renderable whose bounding box lies behind
// that depth everywhere it covers is taken out of the main view's lists, and a
// lamp whose whole reach is hidden gets neither its pass nor its shadow maps.
// The test errs on the side of drawing: the outline of a wall is drawn a pixel
// smaller than it is (the seams between the faces of one wall are not: vbsp cuts
// a wall into many faces, and a gap at every seam would let everything "show
// through") and at its far depth within a pixel; boxes are tested a pixel larger
// and at their nearest corner. Shadow views are left alone (what is hidden from
// the eye still casts its shadow into view).
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_occlusion.h"
#include "deferred/deferred_worldmesh.h"
#include "view_shared.h"
#include "ivrenderview.h"
#include "engine/ivdebugoverlay.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_occlusion( "r_deferred_occlusion", "1", FCVAR_ARCHIVE,
	"Occlusion culling: props, characters and effects hidden behind the walls of the map are not drawn" );
static ConVar r_deferred_occlusion_max( "r_deferred_occlusion_max", "320", 0, "Occlusion culling: the most walls drawn into the occlusion buffer a frame" );
static ConVar r_deferred_occlusion_distance( "r_deferred_occlusion_distance", "5000", 0, "Occlusion culling: walls farther than this are not used" );
static ConVar r_deferred_occlusion_debug( "r_deferred_occlusion_debug", "0", 0, "Dev: print what the occlusion culling did, once a second (2 = and hide nothing, 3 = and show the boxes of what would be hidden)" );

#define OCC_W 256
#define OCC_H 144
#define OCC_NEAR 6.0f

// 1 / view depth of the nearest wall over each pixel (0 = none)
static float s_flBuffer[ OCC_W * OCC_H ];
static VMatrix s_matWorldToProj;
static Vector s_vecOrigin;
static QAngle s_angView;
static int s_nFrame = -1;
static bool s_bValid = false;
static bool s_bCollecting = false;

void DeferredOcclusion_SetCollecting( bool bMainView )
{
	s_bCollecting = bMainView;
}

bool DeferredOcclusion_IsCollecting()
{
	return s_bCollecting && s_bValid;
}

static int s_nStatTested = 0, s_nStatCulled = 0, s_nStatOccluders = 0, s_nStatFrames = 0;
static double s_flStatBuild = 0.0;

struct OccVert_t
{
	float x, y, w;	// pixels, 1 / depth
};

static void RasterizeOccluder( const Vector *pVerts, int nVerts, unsigned int nInnerEdges )
{
	// to clip space, cut at the near plane
	float flClip[ 16 ][ 3 ];	// x, y, w
	// (the edge that starts at each corner is a seam within a wall - not its outline)
	bool bInner[ 16 ];
	int nClip = 0;
	{
		float flIn[ 12 ][ 3 ];
		for ( int i = 0; i < nVerts; i++ )
		{
			const Vector &p = pVerts[i];
			flIn[i][0] = s_matWorldToProj[0][0] * p.x + s_matWorldToProj[0][1] * p.y + s_matWorldToProj[0][2] * p.z + s_matWorldToProj[0][3];
			flIn[i][1] = s_matWorldToProj[1][0] * p.x + s_matWorldToProj[1][1] * p.y + s_matWorldToProj[1][2] * p.z + s_matWorldToProj[1][3];
			flIn[i][2] = s_matWorldToProj[3][0] * p.x + s_matWorldToProj[3][1] * p.y + s_matWorldToProj[3][2] * p.z + s_matWorldToProj[3][3];
		}
		for ( int i = 0; i < nVerts; i++ )
		{
			const float *a = flIn[i];
			const float *b = flIn[ ( i + 1 ) % nVerts ];
			const bool bInA = a[2] >= OCC_NEAR, bInB = b[2] >= OCC_NEAR;
			const bool bSeam = ( nInnerEdges & ( 1u << i ) ) != 0;
			if ( bInA )
			{
				flClip[ nClip ][0] = a[0]; flClip[ nClip ][1] = a[1]; flClip[ nClip ][2] = a[2];
				bInner[ nClip ] = bSeam;
				nClip++;
			}
			if ( bInA != bInB )
			{
				const float t = ( OCC_NEAR - a[2] ) / ( b[2] - a[2] );
				flClip[ nClip ][0] = a[0] + ( b[0] - a[0] ) * t;
				flClip[ nClip ][1] = a[1] + ( b[1] - a[1] ) * t;
				flClip[ nClip ][2] = OCC_NEAR;
				// (leaving the view: the next edge runs along the near plane - an outline)
				bInner[ nClip ] = bInA ? false : bSeam;
				nClip++;
			}
		}
	}
	if ( nClip < 3 )
		return;

	OccVert_t v[ 16 ];
	float flMinX = 1e9f, flMaxX = -1e9f, flMinY = 1e9f, flMaxY = -1e9f;
	for ( int i = 0; i < nClip; i++ )
	{
		const float flInvW = 1.0f / flClip[i][2];
		v[i].x = ( flClip[i][0] * flInvW * 0.5f + 0.5f ) * OCC_W;
		v[i].y = ( 0.5f - flClip[i][1] * flInvW * 0.5f ) * OCC_H;
		v[i].w = flInvW;
		flMinX = Min( flMinX, v[i].x ); flMaxX = Max( flMaxX, v[i].x );
		flMinY = Min( flMinY, v[i].y ); flMaxY = Max( flMaxY, v[i].y );
	}
	if ( flMaxX < 0.0f || flMinX > OCC_W || flMaxY < 0.0f || flMinY > OCC_H || flMaxX - flMinX < 2.5f || flMaxY - flMinY < 2.5f )
		return;

	// the winding on screen
	float flArea = 0.0f;
	for ( int i = 0; i < nClip; i++ )
	{
		const OccVert_t &a = v[i], &b = v[ ( i + 1 ) % nClip ];
		flArea += a.x * b.y - b.x * a.y;
	}
	if ( fabsf( flArea ) < 8.0f )
		return;

	// 1 / depth is linear on screen: w = A x + B y + C (through three corners that span the most)
	float A, B, C;
	{
		int i1 = 1, i2 = 2;
		float flBest = 0.0f;
		for ( int i = 1; i < nClip - 1; i++ )
		{
			const float flTri = fabsf( ( v[i].x - v[0].x ) * ( v[i + 1].y - v[0].y ) - ( v[i + 1].x - v[0].x ) * ( v[i].y - v[0].y ) );
			if ( flTri > flBest )
			{
				flBest = flTri;
				i1 = i;
				i2 = i + 1;
			}
		}
		const float x1 = v[i1].x - v[0].x, y1 = v[i1].y - v[0].y, w1 = v[i1].w - v[0].w;
		const float x2 = v[i2].x - v[0].x, y2 = v[i2].y - v[0].y, w2 = v[i2].w - v[0].w;
		const float flDet = x1 * y2 - x2 * y1;
		if ( fabsf( flDet ) < 1e-3f )
			return;
		A = ( w1 * y2 - w2 * y1 ) / flDet;
		B = ( x1 * w2 - x2 * w1 ) / flDet;
		C = v[0].w - A * v[0].x - B * v[0].y;
	}
	// (the farthest of the wall within a pixel)
	const float flMargin = ( fabsf( A ) + fabsf( B ) ) * 0.5f;

	// The edges: E( x, y ) = a x + b y + c >= 0 inside (the centroid is). The outline is pushed
	// in by a pixel; a seam is pushed out by a hair, so that the two faces it joins leave no
	// pixel between them.
	float ea[ 16 ], eb[ 16 ], ec[ 16 ];
	{
		float cx = 0.0f, cy = 0.0f;
		for ( int i = 0; i < nClip; i++ ) { cx += v[i].x; cy += v[i].y; }
		cx /= nClip; cy /= nClip;
		for ( int i = 0; i < nClip; i++ )
		{
			const OccVert_t &p = v[i], &q = v[ ( i + 1 ) % nClip ];
			ea[i] = p.y - q.y;
			eb[i] = q.x - p.x;
			ec[i] = -( ea[i] * p.x + eb[i] * p.y );
			if ( ea[i] * cx + eb[i] * cy + ec[i] < 0.0f )
			{
				ea[i] = -ea[i]; eb[i] = -eb[i]; ec[i] = -ec[i];
			}
			const float flLen = sqrtf( ea[i] * ea[i] + eb[i] * eb[i] );
			ec[i] += flLen * ( bInner[i] ? 0.02f : -1.0f );
		}
	}

	const int y0 = Max( (int)floorf( flMinY ), 0 ), y1 = Min( (int)ceilf( flMaxY ), OCC_H - 1 );
	for ( int y = y0; y <= y1; y++ )
	{
		const float fy = y + 0.5f;
		float flLeft = Max( flMinX, 0.0f ), flRight = Min( flMaxX, (float)OCC_W );
		bool bEmpty = false;
		for ( int i = 0; i < nClip && !bEmpty; i++ )
		{
			const float flRest = eb[i] * fy + ec[i];
			if ( fabsf( ea[i] ) < 1e-6f )
			{
				if ( flRest < 0.0f )
					bEmpty = true;
			}
			else if ( ea[i] > 0.0f )
				flLeft = Max( flLeft, -flRest / ea[i] );
			else
				flRight = Min( flRight, -flRest / ea[i] );
		}
		if ( bEmpty )
			continue;
		const int x0 = (int)ceilf( flLeft - 0.5f ), x1 = (int)floorf( flRight - 0.5f );
		if ( x1 < x0 )
			continue;
		float *pRow = s_flBuffer + y * OCC_W;
		float w = A * ( x0 + 0.5f ) + B * fy + C - flMargin;
		for ( int x = x0; x <= x1; x++, w += A )
		{
			if ( w > pRow[x] )
				pRow[x] = w;
		}
	}
}

struct OccPick_t
{
	float flScore;
	int iOccluder;
};

static int __cdecl ComparePicks( const void *pA, const void *pB )
{
	const float a = ( (const OccPick_t *)pA )->flScore, b = ( (const OccPick_t *)pB )->flScore;
	return ( a > b ) ? -1 : ( a < b ) ? 1 : 0;
}

void DeferredOcclusion_Setup( const CViewSetup &view )
{
	// (a second view of the frame - a monitor, a scripted camera - draws its own)
	if ( s_nFrame == gpGlobals->framecount && view.origin == s_vecOrigin && view.angles == s_angView )
		return;
	s_nFrame = gpGlobals->framecount;
	s_bValid = false;
	if ( !r_deferred_occlusion.GetBool() || view.m_bOrtho )
		return;

	int nOccluders = 0;
	const WorldOccluder_t *pOccluders = DeferredWorldMesh_GetOccluders( nOccluders );
	if ( !pOccluders || nOccluders == 0 )
		return;

	const double flStart = Plat_FloatTime();

	VMatrix matWorldToView, matViewToProj, matWorldToPixels;
	render->GetMatricesForView( view, &matWorldToView, &matViewToProj, &s_matWorldToProj, &matWorldToPixels );
	s_vecOrigin = view.origin;
	s_angView = view.angles;
	memset( s_flBuffer, 0, sizeof( s_flBuffer ) );

	// the walls that cover the most of the view: turned to the eye, near and large
	static CUtlVector< OccPick_t > s_Picks;
	s_Picks.RemoveAll();
	const float flMaxDist = r_deferred_occlusion_distance.GetFloat();
	for ( int i = 0; i < nOccluders; i++ )
	{
		const WorldOccluder_t &o = pOccluders[i];
		const Vector vecTo = o.vecCenter - view.origin;
		const float flFacing = -DotProduct( vecTo, o.vecNormal );
		if ( flFacing <= 4.0f )
			continue;
		const float flDistSqr = vecTo.LengthSqr();
		if ( flDistSqr > flMaxDist * flMaxDist )
			continue;
		// (the solid angle it takes up, roughly)
		const float flScore = o.flArea * flFacing / ( flDistSqr * sqrtf( flDistSqr ) + 1.0f );
		if ( flScore < 0.0015f )
			continue;
		OccPick_t &pick = s_Picks[ s_Picks.AddToTail() ];
		pick.flScore = flScore;
		pick.iOccluder = i;
	}
	const int nMax = r_deferred_occlusion_max.GetInt();
	if ( s_Picks.Count() > nMax )
		qsort( s_Picks.Base(), s_Picks.Count(), sizeof( OccPick_t ), ComparePicks );
	const int nDraw = Min( s_Picks.Count(), nMax );
	for ( int i = 0; i < nDraw; i++ )
	{
		const WorldOccluder_t &o = pOccluders[ s_Picks[i].iOccluder ];
		RasterizeOccluder( o.vecVerts, o.nVerts, o.nInnerEdges );
	}

	s_bValid = nDraw > 0;
	s_nStatOccluders += nDraw;
	s_nStatFrames++;
	s_flStatBuild += Plat_FloatTime() - flStart;

	if ( r_deferred_occlusion_debug.GetBool() )
	{
		static float s_flNext = 0.0f;
		if ( gpGlobals->realtime > s_flNext || gpGlobals->realtime < s_flNext - 2.0f )
		{
			s_flNext = gpGlobals->realtime + 1.0f;
			int nCovered = 0;
			for ( int i = 0; i < OCC_W * OCC_H; i++ )
				nCovered += ( s_flBuffer[i] > 0.0f ) ? 1 : 0;
			Msg( "[occlusion] %d frames: %.0f walls a frame (%.2f ms), %d %% of the view covered; %.0f boxes tested, %.0f hidden a frame\n",
				s_nStatFrames, (float)s_nStatOccluders / s_nStatFrames, s_flStatBuild * 1000.0 / s_nStatFrames,
				nCovered * 100 / ( OCC_W * OCC_H ), (float)s_nStatTested / s_nStatFrames, (float)s_nStatCulled / s_nStatFrames );
			s_nStatTested = s_nStatCulled = s_nStatOccluders = s_nStatFrames = 0;
			s_flStatBuild = 0.0;
		}
	}
}

bool DeferredOcclusion_IsForView( const Vector &vecOrigin, const QAngle &angles )
{
	return s_bValid && s_nFrame == gpGlobals->framecount && vecOrigin == s_vecOrigin && angles == s_angView;
}

bool DeferredOcclusion_IsBoxHidden( const Vector &vecMins, const Vector &vecMaxs )
{
	if ( !s_bValid )
		return false;
	s_nStatTested++;

	float flMinX = 1e9f, flMaxX = -1e9f, flMinY = 1e9f, flMaxY = -1e9f, flMaxW = 0.0f;
	for ( int i = 0; i < 8; i++ )
	{
		const Vector p( ( i & 1 ) ? vecMaxs.x : vecMins.x, ( i & 2 ) ? vecMaxs.y : vecMins.y, ( i & 4 ) ? vecMaxs.z : vecMins.z );
		const float w = s_matWorldToProj[3][0] * p.x + s_matWorldToProj[3][1] * p.y + s_matWorldToProj[3][2] * p.z + s_matWorldToProj[3][3];
		// (a corner at or behind the eye: the box is around the camera)
		if ( w < OCC_NEAR )
			return false;
		const float flInvW = 1.0f / w;
		const float x = ( ( s_matWorldToProj[0][0] * p.x + s_matWorldToProj[0][1] * p.y + s_matWorldToProj[0][2] * p.z + s_matWorldToProj[0][3] ) * flInvW * 0.5f + 0.5f ) * OCC_W;
		const float y = ( 0.5f - ( s_matWorldToProj[1][0] * p.x + s_matWorldToProj[1][1] * p.y + s_matWorldToProj[1][2] * p.z + s_matWorldToProj[1][3] ) * flInvW * 0.5f ) * OCC_H;
		flMinX = Min( flMinX, x ); flMaxX = Max( flMaxX, x );
		flMinY = Min( flMinY, y ); flMaxY = Max( flMaxY, y );
		flMaxW = Max( flMaxW, flInvW );
	}

	// a pixel more on every side; what is off the screen is the frustum culling's business
	const int x0 = Max( (int)floorf( flMinX ) - 1, 0 ), x1 = Min( (int)floorf( flMaxX ) + 1, OCC_W - 1 );
	const int y0 = Max( (int)floorf( flMinY ) - 1, 0 ), y1 = Min( (int)floorf( flMaxY ) + 1, OCC_H - 1 );
	if ( x1 < x0 || y1 < y0 )
		return false;

	for ( int y = y0; y <= y1; y++ )
	{
		const float *pRow = s_flBuffer + y * OCC_W;
		for ( int x = x0; x <= x1; x++ )
		{
			// (no wall here, or the wall is farther than the nearest corner of the box)
			if ( pRow[x] <= flMaxW )
				return false;
		}
	}

	s_nStatCulled++;
	if ( r_deferred_occlusion_debug.GetInt() >= 2 )
	{
		// (2 = what would be hidden is drawn all the same; 3 = and its box with it)
		if ( r_deferred_occlusion_debug.GetInt() >= 3 )
			debugoverlay->AddBoxOverlay( vec3_origin, vecMins, vecMaxs, vec3_angle, 255, 40, 40, 24, 0.0f );
		return false;
	}
	return true;
}

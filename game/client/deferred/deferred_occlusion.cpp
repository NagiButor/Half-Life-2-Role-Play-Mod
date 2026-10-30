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
// The map itself: the engine draws every leaf of the BSP tree the visibility data
// marks (leaf->visframe == the current mark) and the view frustum keeps. An open
// map has no hint brushes and areaportals to make that data tight - from a street
// it marks most of the town. For the main view the leaves whose bounds lie behind
// the walls lose their mark while the engine builds the view's world lists, and
// get it back right after: their faces, displacements, water, detail props and
// whatever stands only in them are not drawn. The engine's leaf array is found in
// the world model and checked leaf by leaf against the .bsp before it is touched.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_occlusion.h"
#include "deferred/deferred_worldmesh.h"
#include "view_shared.h"
#include "ivrenderview.h"
#include "engine/ivdebugoverlay.h"
#include "engine/ivmodelinfo.h"
#include "filesystem.h"
#include "bspfile.h"
#include "clientleafsystem.h"
#include "collisionutils.h"
#include "tier1/utlbuffer.h"
#include "hl2rpm_crashdebug.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_occlusion( "r_deferred_occlusion", "1", FCVAR_ARCHIVE,
	"Occlusion culling: props, characters and effects hidden behind the walls of the map are not drawn" );
static ConVar r_deferred_occlusion_world( "r_deferred_occlusion_world", "1", 0,
	"Occlusion culling: the parts of the map itself (its leaves) behind the walls are not drawn either" );
static ConVar r_deferred_occlusion_max( "r_deferred_occlusion_max", "320", 0, "Occlusion culling: the most walls drawn into the occlusion buffer a frame" );
static ConVar r_deferred_occlusion_distance( "r_deferred_occlusion_distance", "5000", 0, "Occlusion culling: walls farther than this are not used" );
static ConVar r_deferred_occlusion_debug( "r_deferred_occlusion_debug", "0", 0, "Dev: print what the occlusion culling did, once a second (2 = and hide nothing, 3 = and show the boxes of what would be hidden, 4 = show the walls drawn into the buffer)" );

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
static int s_nStatLeaves = 0, s_nStatLeavesHidden = 0, s_nStatLeafFrames = 0;
static int s_nStatLights = 0, s_nStatLightsHidden = 0;
static double s_flStatBuild = 0.0, s_flStatLeaves = 0.0;

// The buffer at coarser levels: a texel of level n holds the farthest wall (the least 1 / depth)
// of its 2^n x 2^n pixels - a box hidden at a coarse texel is hidden at all its pixels
#define OCC_LEVELS 6
static float s_flLevelData[ ( OCC_W / 2 + 1 ) * ( OCC_H / 2 + 1 ) * 2 ];
static float *s_pLevel[ OCC_LEVELS ];
static int s_nLevelW[ OCC_LEVELS ], s_nLevelH[ OCC_LEVELS ];

static void BuildLevels()
{
	s_pLevel[0] = s_flBuffer;
	s_nLevelW[0] = OCC_W;
	s_nLevelH[0] = OCC_H;
	float *pNext = s_flLevelData;
	for ( int n = 1; n < OCC_LEVELS; n++ )
	{
		const int nW = s_nLevelW[ n - 1 ], nH = s_nLevelH[ n - 1 ];
		const int nNewW = ( nW + 1 ) / 2, nNewH = ( nH + 1 ) / 2;
		const float *pSrc = s_pLevel[ n - 1 ];
		s_pLevel[n] = pNext;
		s_nLevelW[n] = nNewW;
		s_nLevelH[n] = nNewH;
		for ( int y = 0; y < nNewH; y++ )
		{
			const int y0 = y * 2, y1 = Min( y * 2 + 1, nH - 1 );
			for ( int x = 0; x < nNewW; x++ )
			{
				const int x0 = x * 2, x1 = Min( x * 2 + 1, nW - 1 );
				pNext[ y * nNewW + x ] = Min( Min( pSrc[ y0 * nW + x0 ], pSrc[ y0 * nW + x1 ] ),
											  Min( pSrc[ y1 * nW + x0 ], pSrc[ y1 * nW + x1 ] ) );
			}
		}
		pNext += nNewW * nNewH;
	}
}

// The pixels a world space box covers (a pixel more on every side) and its nearest 1 / depth.
// False = a corner is at or behind the eye (the box is around the camera), or it is off screen.
static bool ProjectBox( const Vector &vecMins, const Vector &vecMaxs, int &x0, int &x1, int &y0, int &y1, float &flMaxW )
{
	float flMinX = 1e9f, flMaxX = -1e9f, flMinY = 1e9f, flMaxY = -1e9f;
	flMaxW = 0.0f;
	for ( int i = 0; i < 8; i++ )
	{
		const Vector p( ( i & 1 ) ? vecMaxs.x : vecMins.x, ( i & 2 ) ? vecMaxs.y : vecMins.y, ( i & 4 ) ? vecMaxs.z : vecMins.z );
		const float w = s_matWorldToProj[3][0] * p.x + s_matWorldToProj[3][1] * p.y + s_matWorldToProj[3][2] * p.z + s_matWorldToProj[3][3];
		if ( w < OCC_NEAR )
			return false;
		const float flInvW = 1.0f / w;
		const float x = ( ( s_matWorldToProj[0][0] * p.x + s_matWorldToProj[0][1] * p.y + s_matWorldToProj[0][2] * p.z + s_matWorldToProj[0][3] ) * flInvW * 0.5f + 0.5f ) * OCC_W;
		const float y = ( 0.5f - ( s_matWorldToProj[1][0] * p.x + s_matWorldToProj[1][1] * p.y + s_matWorldToProj[1][2] * p.z + s_matWorldToProj[1][3] ) * flInvW * 0.5f ) * OCC_H;
		flMinX = Min( flMinX, x ); flMaxX = Max( flMaxX, x );
		flMinY = Min( flMinY, y ); flMaxY = Max( flMaxY, y );
		flMaxW = Max( flMaxW, flInvW );
	}

	// what is off the screen is the frustum culling's business
	x0 = Max( (int)floorf( flMinX ) - 1, 0 ); x1 = Min( (int)floorf( flMaxX ) + 1, OCC_W - 1 );
	y0 = Max( (int)floorf( flMinY ) - 1, 0 ); y1 = Min( (int)floorf( flMaxY ) + 1, OCC_H - 1 );
	return x1 >= x0 && y1 >= y0;
}

// The same test at the level where the box takes up no more than 8 x 8 texels (for many large
// boxes a frame: the leaves of the map). A little less is hidden than by the exact test.
static bool IsBoxHiddenCoarse( const Vector &vecMins, const Vector &vecMaxs )
{
	int x0, x1, y0, y1;
	float flMaxW;
	if ( !ProjectBox( vecMins, vecMaxs, x0, x1, y0, y1, flMaxW ) )
		return false;

	int n = 0;
	while ( n < OCC_LEVELS - 1 && ( ( x1 >> n ) - ( x0 >> n ) >= 8 || ( y1 >> n ) - ( y0 >> n ) >= 8 ) )
		n++;
	const float *pLevel = s_pLevel[n];
	const int nW = s_nLevelW[n];
	for ( int y = y0 >> n; y <= ( y1 >> n ); y++ )
	{
		const float *pRow = pLevel + y * nW;
		for ( int x = x0 >> n; x <= ( x1 >> n ); x++ )
		{
			if ( pRow[x] <= flMaxW )
				return false;
		}
	}
	return true;
}

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
	const bool bShowWalls = r_deferred_occlusion_debug.GetInt() == 4;
	for ( int i = 0; i < nDraw; i++ )
	{
		const WorldOccluder_t &o = pOccluders[ s_Picks[i].iOccluder ];
		RasterizeOccluder( o.vecVerts, o.nVerts, o.nInnerEdges );
		// (4 = the outlines of the walls drawn into the buffer)
		if ( bShowWalls )
		{
			for ( int k = 0; k < o.nVerts; k++ )
				debugoverlay->AddLineOverlay( o.vecVerts[k], o.vecVerts[ ( k + 1 ) % o.nVerts ], 40, 255, 40, true, 0.0f );
		}
	}

	s_bValid = nDraw > 0;
	if ( s_bValid )
		BuildLevels();
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
			Msg( "[occlusion] %d frames: %.0f walls a frame (%.2f ms), %d %% of the view covered; %.0f boxes tested, %.0f hidden a frame; "
				"map leaves: %.0f in view, %.0f hidden a frame (%.2f ms); lamps by leaf: %.0f tested, %.0f hidden a frame\n",
				s_nStatFrames, (float)s_nStatOccluders / s_nStatFrames, s_flStatBuild * 1000.0 / s_nStatFrames,
				nCovered * 100 / ( OCC_W * OCC_H ), (float)s_nStatTested / s_nStatFrames, (float)s_nStatCulled / s_nStatFrames,
				(float)s_nStatLeaves / Max( s_nStatLeafFrames, 1 ), (float)s_nStatLeavesHidden / Max( s_nStatLeafFrames, 1 ),
				s_flStatLeaves * 1000.0 / Max( s_nStatLeafFrames, 1 ),
				(float)s_nStatLights / s_nStatFrames, (float)s_nStatLightsHidden / s_nStatFrames );
			s_nStatTested = s_nStatCulled = s_nStatOccluders = s_nStatFrames = 0;
			s_nStatLeaves = s_nStatLeavesHidden = s_nStatLeafFrames = 0;
			s_nStatLights = s_nStatLightsHidden = 0;
			s_flStatBuild = s_flStatLeaves = 0.0;
		}
	}
}

// Dev: the buffer as a picture (the mod folder, occlusion_buffer.pgm: white = a wall near, black = none)
CON_COMMAND( r_deferred_occlusion_save, "Dev: write the occlusion buffer of the last frame to occlusion_buffer.pgm in the mod folder" )
{
	float flMax = 0.0f;
	for ( int i = 0; i < OCC_W * OCC_H; i++ )
		flMax = Max( flMax, s_flBuffer[i] );
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	buf.Printf( "P2\n%d %d\n255\n", OCC_W, OCC_H );
	for ( int y = 0; y < OCC_H; y++ )
	{
		for ( int x = 0; x < OCC_W; x++ )
		{
			// (by distance: a wall 64 units away is white, one 4096 away dark grey)
			const float w = s_flBuffer[ y * OCC_W + x ];
			const int v = ( w <= 0.0f ) ? 0 : clamp( (int)( 40.0f + 215.0f * sqrtf( Min( w * 64.0f, 1.0f ) ) ), 40, 255 );
			buf.Printf( "%d ", v );
		}
		buf.Printf( "\n" );
	}
	filesystem->WriteFile( "occlusion_buffer.pgm", "MOD", buf );
	Msg( "[occlusion] wrote occlusion_buffer.pgm (%s, nearest wall 1/depth %.4f)\n", s_bValid ? "valid" : "no buffer", flMax );
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

	int x0, x1, y0, y1;
	float flMaxW;
	if ( !ProjectBox( vecMins, vecMaxs, x0, x1, y0, y1, flMaxW ) )
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
		if ( r_deferred_occlusion_debug.GetInt() == 3 )
			debugoverlay->AddBoxOverlay( vec3_origin, vecMins, vecMaxs, vec3_angle, 255, 40, 40, 24, 0.0f );
		return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// The leaves of the map
//-----------------------------------------------------------------------------

// mleaf_t of the engine (gl_model_private.h; the same in the 2013 engine - checked leaf by leaf
// against the .bsp before use): 64 bytes, the visibility mark at +4
#define ENGINE_LEAF_SIZE		64
#define ENGINE_LEAF_CONTENTS	0
#define ENGINE_LEAF_VISFRAME	4
#define ENGINE_LEAF_CLUSTER		48
// (how far into model_t the pointer to the world's brush data is looked for)
#define ENGINE_MODEL_SCAN		1024

// A leaf is tested by what of the map is drawn because of it, not by its volume: the air leaves
// of an open map reach from the ground to the top of the sky box and are seen above any house.
// What is drawn: its faces (cut to the leaf - a face is listed in every leaf it touches, the
// leaves together hold all of it), the parts of displacements within it, its detail props
// (the grass stands on its faces), and the renderables that stand in it (tested each time:
// they move; the leaf list of the view is also what they are collected from).
struct OccWorldLeaf_t
{
	Vector	vecMins;		// the leaf
	Vector	vecMaxs;
	Vector	vecDrawnMins;	// what is drawn because of it (mins > maxs: nothing)
	Vector	vecDrawnMaxs;
	int		nContents;
	short	nCluster;
};
static CUtlVector< OccWorldLeaf_t > s_WorldLeaves;
static int s_nLeavesReadState = 0;		// the leaves of the .bsp: 0 = not read on this map yet, 1 = read, -1 = can't be
static unsigned char *s_pEngineLeaves = NULL;
static int s_nWorldLeavesState = 0;		// the engine's leaves: 0 = not looked for on this map yet, 1 = found, -1 = not to be used
static CUtlVector< int > s_HiddenLeaves;
static int s_nHiddenMark = 0;

extern bool ClientLeafSystem_AreRenderablesInLeafHidden( int leaf, bool ( *pfnIsHidden )( const Vector &vecMins, const Vector &vecMaxs ) );

template< class T >
static bool ReadOccLump( FileHandle_t hFile, const dheader_t &header, int iLump, CUtlVector< T > &out )
{
	const lump_t &lump = header.lumps[ iLump ];
	// (these four bytes hold the uncompressed size of an LZMA-compressed lump)
	const bool bCompressed = lump.fourCC[0] || lump.fourCC[1] || lump.fourCC[2] || lump.fourCC[3];
	if ( bCompressed || lump.filelen < 0 || ( lump.filelen % sizeof( T ) ) != 0 )
		return false;
	out.SetCount( lump.filelen / sizeof( T ) );
	if ( lump.filelen == 0 )
		return true;
	filesystem->Seek( hFile, lump.fileofs, FILESYSTEM_SEEK_HEAD );
	return filesystem->Read( out.Base(), lump.filelen, hFile ) == lump.filelen;
}

static void AddClippedBox( OccWorldLeaf_t &leaf, const Vector &vecMins, const Vector &vecMaxs )
{
	// (a unit more than the leaf: its bounds in the .bsp are rounded to whole units)
	Vector vecClipMins, vecClipMaxs;
	for ( int k = 0; k < 3; k++ )
	{
		vecClipMins[k] = Max( vecMins[k], leaf.vecMins[k] - 1.0f );
		vecClipMaxs[k] = Min( vecMaxs[k], leaf.vecMaxs[k] + 1.0f );
		if ( vecClipMins[k] > vecClipMaxs[k] )
			return;
	}
	VectorMin( leaf.vecDrawnMins, vecClipMins, leaf.vecDrawnMins );
	VectorMax( leaf.vecDrawnMaxs, vecClipMaxs, leaf.vecDrawnMaxs );
}

// The leaves as the .bsp has them, and the sizes the engine's brush data must agree with
static bool ReadWorldLeaves( const char *pszLevel, int &nModels, int &nPlanes )
{
	s_WorldLeaves.RemoveAll();
	const FileHandle_t hFile = filesystem->Open( pszLevel, "rb", "GAME" );
	if ( !hFile )
		return false;

	CUtlVector< unsigned char > leafData;
	CUtlVector< unsigned short > leafFaces;
	CUtlVector< dface_t > faces;
	CUtlVector< int > surfedges;
	CUtlVector< dedge_t > edges;
	CUtlVector< dvertex_t > vertexes;
	CUtlVector< ddispinfo_t > dispinfos;
	CUtlVector< CDispVert > dispverts;
	int nLeafSize = sizeof( dleaf_t );

	dheader_t header;
	bool bOk = filesystem->Read( &header, sizeof( header ), hFile ) == sizeof( header ) && header.ident == IDBSPHEADER;
	if ( bOk )
	{
		nModels = header.lumps[ LUMP_MODELS ].filelen / sizeof( dmodel_t );
		nPlanes = header.lumps[ LUMP_PLANES ].filelen / sizeof( dplane_t );
		// (version 0 carries the ambient light in each leaf; the first fields are the same)
		nLeafSize = ( header.lumps[ LUMP_LEAFS ].version == 0 ) ? sizeof( dleaf_version_0_t ) : sizeof( dleaf_t );
		bOk = ReadOccLump( hFile, header, LUMP_LEAFS, leafData )
			&& ReadOccLump( hFile, header, LUMP_LEAFFACES, leafFaces )
			&& ReadOccLump( hFile, header, LUMP_FACES, faces )
			&& ReadOccLump( hFile, header, LUMP_SURFEDGES, surfedges )
			&& ReadOccLump( hFile, header, LUMP_EDGES, edges )
			&& ReadOccLump( hFile, header, LUMP_VERTEXES, vertexes )
			&& ReadOccLump( hFile, header, LUMP_DISPINFO, dispinfos )
			&& ReadOccLump( hFile, header, LUMP_DISP_VERTS, dispverts );
		// (a map compiled for HDR only: the geometry of the two face lumps is the same)
		if ( bOk && faces.Count() == 0 )
			bOk = ReadOccLump( hFile, header, LUMP_FACES_HDR, faces );
	}
	filesystem->Close( hFile );
	if ( !bOk || leafData.Count() < nLeafSize )
		return false;

	// the bounds of each face (a displacement: of its surface - the base face moved by its offsets)
	CUtlVector< Vector > faceMins, faceMaxs;
	faceMins.SetCount( faces.Count() );
	faceMaxs.SetCount( faces.Count() );
	for ( int f = 0; f < faces.Count(); f++ )
	{
		const dface_t &face = faces[f];
		Vector vecMins( FLT_MAX, FLT_MAX, FLT_MAX ), vecMaxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
		for ( int e = 0; e < face.numedges; e++ )
		{
			const int iSurfEdge = face.firstedge + e;
			if ( iSurfEdge < 0 || iSurfEdge >= surfedges.Count() )
				break;
			const int iEdge = surfedges[ iSurfEdge ];
			const int iEdgeIndex = abs( iEdge );
			if ( iEdgeIndex >= edges.Count() )
				break;
			const int iVert = edges[ iEdgeIndex ].v[ iEdge < 0 ? 1 : 0 ];
			if ( iVert >= vertexes.Count() )
				break;
			VectorMin( vecMins, vertexes[ iVert ].point, vecMins );
			VectorMax( vecMaxs, vertexes[ iVert ].point, vecMaxs );
		}
		if ( face.dispinfo >= 0 && face.dispinfo < dispinfos.Count() )
		{
			const ddispinfo_t &disp = dispinfos[ face.dispinfo ];
			const int nSide = ( 1 << disp.power ) + 1;
			Vector vecMove( 0.0f, 0.0f, 0.0f );
			for ( int v = 0; v < nSide * nSide; v++ )
			{
				const int iVert = disp.m_iDispVertStart + v;
				if ( iVert < 0 || iVert >= dispverts.Count() )
					break;
				const Vector vecOffset = dispverts[ iVert ].m_vVector * dispverts[ iVert ].m_flDist;
				for ( int k = 0; k < 3; k++ )
					vecMove[k] = Max( vecMove[k], fabsf( vecOffset[k] ) );
			}
			vecMins -= vecMove;
			vecMaxs += vecMove;
		}
		faceMins[f] = vecMins;
		faceMaxs[f] = vecMaxs;
	}

	const int nLeaves = leafData.Count() / nLeafSize;
	s_WorldLeaves.SetCount( nLeaves );
	for ( int i = 0; i < nLeaves; i++ )
	{
		const dleaf_t *pIn = (const dleaf_t *)( leafData.Base() + i * nLeafSize );
		OccWorldLeaf_t &leaf = s_WorldLeaves[i];
		leaf.vecMins.Init( pIn->mins[0], pIn->mins[1], pIn->mins[2] );
		leaf.vecMaxs.Init( pIn->maxs[0], pIn->maxs[1], pIn->maxs[2] );
		leaf.vecDrawnMins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
		leaf.vecDrawnMaxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
		leaf.nContents = pIn->contents;
		leaf.nCluster = pIn->cluster;
		for ( int j = 0; j < pIn->numleaffaces; j++ )
		{
			const int iLeafFace = pIn->firstleafface + j;
			if ( iLeafFace >= leafFaces.Count() || leafFaces[ iLeafFace ] >= faces.Count() )
				break;
			const int f = leafFaces[ iLeafFace ];
			AddClippedBox( leaf, faceMins[f], faceMaxs[f] );
		}
	}

	// displacements are not listed in the leaves: the engine links each to the leaves its box touches
	for ( int f = 0; f < faces.Count(); f++ )
	{
		if ( faces[f].dispinfo < 0 )
			continue;
		for ( int i = 0; i < nLeaves; i++ )
		{
			OccWorldLeaf_t &leaf = s_WorldLeaves[i];
			if ( leaf.nCluster >= 0 && IsBoxIntersectingBox( leaf.vecMins, leaf.vecMaxs, faceMins[f], faceMaxs[f] ) )
				AddClippedBox( leaf, faceMins[f], faceMaxs[f] );
		}
	}

	return s_WorldLeaves.Count() > 0;
}

// What is drawn because of the leaf is hidden
static bool IsLeafHidden( int iLeaf, const OccWorldLeaf_t &leaf )
{
	if ( leaf.vecDrawnMins.x <= leaf.vecDrawnMaxs.x )
	{
		Vector vecMins = leaf.vecDrawnMins, vecMaxs = leaf.vecDrawnMaxs;
		// the detail props stand on its faces: the sprites half a width to the sides, their height up
		int nFirstDetail = 0, nDetails = 0;
		ClientLeafSystem()->GetDetailObjectsInLeaf( iLeaf, nFirstDetail, nDetails );
		if ( nDetails > 0 )
		{
			vecMins -= Vector( 64.0f, 64.0f, 8.0f );
			vecMaxs += Vector( 64.0f, 64.0f, 128.0f );
		}
		if ( !IsBoxHiddenCoarse( vecMins, vecMaxs ) )
			return false;
	}
	else
	{
		// (nothing of the map: detail props without a face of the leaf under them are tested by the leaf)
		int nFirstDetail = 0, nDetails = 0;
		ClientLeafSystem()->GetDetailObjectsInLeaf( iLeaf, nFirstDetail, nDetails );
		if ( nDetails > 0 && !IsBoxHiddenCoarse( leaf.vecMins, leaf.vecMaxs ) )
			return false;
	}
	return ClientLeafSystem_AreRenderablesInLeafHidden( iLeaf, IsBoxHiddenCoarse );
}

static int s_nBspModels = 0, s_nBspPlanes = 0;

static bool EnsureWorldLeaves()
{
	if ( s_nLeavesReadState == 0 )
	{
		const char *pszLevel = UTIL_GetLevelFileName();
		s_nLeavesReadState = ReadWorldLeaves( pszLevel, s_nBspModels, s_nBspPlanes ) ? 1 : -1;
		if ( s_nLeavesReadState < 0 )
			Warning( "[occlusion] can't read the leaves of %s: the map is drawn as its visibility data says\n", pszLevel );
	}
	return s_nLeavesReadState > 0;
}

// The engine's array of leaves: model 1 (the world) points at its brush data, whose counts of
// models, planes and leaves must be the .bsp's; then every leaf's contents and cluster must be
static bool FindEngineLeaves()
{
	const char *pszLevel = UTIL_GetLevelFileName();
	if ( !EnsureWorldLeaves() )
		return false;
	const int nModels = s_nBspModels, nPlanes = s_nBspPlanes;
	const int nLeaves = s_WorldLeaves.Count();

	const unsigned char *pModel = (const unsigned char *)modelinfo->GetModel( 1 );
	if ( pModel && RPMCrash_IsReadable( pModel, ENGINE_MODEL_SCAN ) )
	{
		for ( int nOffset = 0; nOffset + 4 <= ENGINE_MODEL_SCAN; nOffset += 4 )
		{
			// worldbrushdata_t: numsubmodels, numplanes, planes, numleafs, leafs
			const int *pShared = *(const int * const *)( pModel + nOffset );
			if ( !RPMCrash_IsReadable( pShared, 5 * sizeof( int ) ) )
				continue;
			if ( pShared[0] != nModels || pShared[1] != nPlanes || pShared[3] != nLeaves )
				continue;
			unsigned char *pLeaves = (unsigned char *)(intp)pShared[4];
			if ( !RPMCrash_IsReadable( pLeaves, nLeaves * ENGINE_LEAF_SIZE ) )
				continue;
			int i;
			for ( i = 0; i < nLeaves; i++ )
			{
				const unsigned char *pLeaf = pLeaves + i * ENGINE_LEAF_SIZE;
				if ( *(const int *)( pLeaf + ENGINE_LEAF_CONTENTS ) != s_WorldLeaves[i].nContents ||
					 *(const short *)( pLeaf + ENGINE_LEAF_CLUSTER ) != s_WorldLeaves[i].nCluster )
					break;
			}
			if ( i == nLeaves )
			{
				s_pEngineLeaves = pLeaves;
				DevMsg( "[occlusion] %d leaves of %s (the world model's brush data at +%d)\n", nLeaves, pszLevel, nOffset );
				return true;
			}
		}
	}
	Warning( "[occlusion] the engine's leaves of %s were not found: the map is drawn as its visibility data says\n", pszLevel );
	return false;
}

void DeferredOcclusion_HideWorldLeaves( const Vector &vecOrigin, const QAngle &angles )
{
	s_HiddenLeaves.RemoveAll();
	if ( !r_deferred_occlusion_world.GetBool() || !DeferredOcclusion_IsForView( vecOrigin, angles ) )
		return;
	if ( s_nWorldLeavesState == 0 )
		s_nWorldLeavesState = FindEngineLeaves() ? 1 : -1;
	if ( s_nWorldLeavesState != 1 )
		return;

	const double flStart = Plat_FloatTime();
	const int nLeaves = s_WorldLeaves.Count();

	// the current mark - the engine raises it with each new visibility setup, so it is the highest
	int nMark = INT_MIN;
	for ( int i = 0; i < nLeaves; i++ )
		nMark = Max( nMark, *(const int *)( s_pEngineLeaves + i * ENGINE_LEAF_SIZE + ENGINE_LEAF_VISFRAME ) );

	const int nDebug = r_deferred_occlusion_debug.GetInt();
	for ( int i = 0; i < nLeaves; i++ )
	{
		int *pMark = (int *)( s_pEngineLeaves + i * ENGINE_LEAF_SIZE + ENGINE_LEAF_VISFRAME );
		const OccWorldLeaf_t &leaf = s_WorldLeaves[i];
		if ( *pMark != nMark || leaf.nCluster < 0 )
			continue;
		s_nStatLeaves++;
		if ( !IsLeafHidden( i, leaf ) )
			continue;
		s_nStatLeavesHidden++;
		// (2 = what would be hidden is drawn all the same; 3 = and the box of what is drawn in it)
		if ( nDebug == 3 && leaf.vecDrawnMins.x <= leaf.vecDrawnMaxs.x )
			debugoverlay->AddBoxOverlay( vec3_origin, leaf.vecDrawnMins, leaf.vecDrawnMaxs, vec3_angle, 40, 120, 255, 12, 0.0f );
		if ( nDebug >= 2 )
			continue;
		*pMark = nMark - 1;
		s_HiddenLeaves.AddToTail( i );
	}
	s_nHiddenMark = nMark;
	s_nStatLeafFrames++;
	s_flStatLeaves += Plat_FloatTime() - flStart;
}

bool DeferredOcclusion_AreLeafPartsHidden( const int *pLeaves, int nLeaves, const Vector &vecMins, const Vector &vecMaxs )
{
	if ( !s_bValid || nLeaves <= 0 || !r_deferred_occlusion_world.GetBool() || !EnsureWorldLeaves() )
		return false;
	s_nStatLights++;

	for ( int i = 0; i < nLeaves; i++ )
	{
		int iLeaf = pLeaves[i];
		if ( iLeaf < 0 || iLeaf >= s_WorldLeaves.Count() )
			return false;
		const OccWorldLeaf_t &leaf = s_WorldLeaves[ iLeaf ];
		// (inside the walls; not in the visibility data's view)
		if ( leaf.nCluster < 0 || !render->AreAnyLeavesVisible( &iLeaf, 1 ) )
			continue;
		Vector vecPartMins, vecPartMaxs;
		bool bEmpty = false;
		for ( int k = 0; k < 3; k++ )
		{
			vecPartMins[k] = Max( vecMins[k], leaf.vecMins[k] - 1.0f );
			vecPartMaxs[k] = Min( vecMaxs[k], leaf.vecMaxs[k] + 1.0f );
			bEmpty = bEmpty || vecPartMins[k] > vecPartMaxs[k];
		}
		if ( !bEmpty && !IsBoxHiddenCoarse( vecPartMins, vecPartMaxs ) )
			return false;
	}

	s_nStatLightsHidden++;
	if ( r_deferred_occlusion_debug.GetInt() >= 2 )
	{
		if ( r_deferred_occlusion_debug.GetInt() == 3 )
			debugoverlay->AddBoxOverlay( vec3_origin, vecMins, vecMaxs, vec3_angle, 255, 200, 40, 16, 0.0f );
		return false;
	}
	return true;
}

void DeferredOcclusion_RestoreWorldLeaves()
{
	for ( int i = 0; i < s_HiddenLeaves.Count(); i++ )
		*(int *)( s_pEngineLeaves + s_HiddenLeaves[i] * ENGINE_LEAF_SIZE + ENGINE_LEAF_VISFRAME ) = s_nHiddenMark;
	s_HiddenLeaves.RemoveAll();
}

class COcclusionLevelSystem : public CAutoGameSystem
{
public:
	COcclusionLevelSystem() : CAutoGameSystem( "COcclusionLevelSystem" ) {}

	virtual void LevelInitPostEntity() { Reset(); }
	virtual void LevelShutdownPostEntity() { Reset(); }

private:
	void Reset()
	{
		s_WorldLeaves.Purge();
		s_HiddenLeaves.Purge();
		s_nLeavesReadState = 0;
		s_pEngineLeaves = NULL;
		s_nWorldLeavesState = 0;
		s_bValid = false;
	}
};
static COcclusionLevelSystem s_OcclusionLevelSystem;

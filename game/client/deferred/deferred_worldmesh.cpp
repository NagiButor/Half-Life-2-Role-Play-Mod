//========= HL2RPM ============================================================//
//
// Purpose: The map's faces as static meshes of our own, for the shadow views.
//
// The engine draws the world of a view material by material, in the order its
// walk through the BSP met them, out of static meshes it keeps per vertex
// format - and starts a new entry of a list on its stack whenever the mesh
// changes from one material to the next: 127 entries, then "Too many vertex
// format changes in frame, whole world not rendered", and every material after
// that is NOT drawn in that view. Plain and bump mapped materials alternate all
// the way down such a list. The views that see all of a map - the sun's shadow
// cascades, the rain map - went over the limit on a map of 800 materials
// (map_v11): walls cast no shadow in the far cascades, and which walls changed
// with the order of the walk (the shadows of whole houses coming and going).
// (One vertex format for all brush materials made it worse: the engine fills
// its meshes first-fit, the small materials of the whole list go on topping up
// the first mesh while the others fill the next.)
//
// A shadow map needs no materials. The client reads the faces and the
// displacements of the world out of the .bsp itself and keeps them as static
// meshes on a grid: everything opaque with one brush material of the renderer
// (in a shadow view it draws its shadow pass - the depth with the bias of that
// view, both sides, the caster depth as color - exactly as the materials of
// the map did), each alpha tested material (fences, grates) with itself. A
// cascade is a few draw calls instead of several hundred, and all of the world
// is in it.
//
// Only in the shadow views (CBaseShadowView): the main view's G-buffer pass
// runs the same DrawExecute with its "depth" flag set.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_worldmesh.h"

#include "bspfile.h"
#include "bspflags.h"
#include "filesystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialvar.h"
#include "tier1/KeyValues.h"
#include "tier1/utlmap.h"
#include "hl2rpm_crashdebug.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_shadow_worldmesh( "r_deferred_shadow_worldmesh", "2", FCVAR_ARCHIVE,
	"The world in the shadow maps comes from the renderer's own meshes of the map: 0 = from the engine's world lists (drops materials on maps with hundreds of them), 1 = the sun's cascades and the rain map, 2 = the lamps' shadows too" );
static ConVar r_deferred_shadow_worldmesh_add( "r_deferred_shadow_worldmesh_add", "0", 0,
	"Dev: the engine's world on top of the meshes (for A/B)" );
static ConVar r_deferred_shadow_worldmesh_cull( "r_deferred_shadow_worldmesh_cull", "1", 0,
	"Dev: the cells of the world meshes outside a shadow view are skipped" );
static ConVar r_deferred_shadow_worldmesh_show( "r_deferred_shadow_worldmesh_show", "0", FCVAR_CHEAT,
	"Dev: draw the world meshes of the shadow maps into the main view as a wireframe (2 = solid)" );
static ConVar r_deferred_shadow_worldmesh_debug( "r_deferred_shadow_worldmesh_debug", "0", 0,
	"Dev: print what the shadow views drew of the world meshes, once a second" );

// the grid the meshes are cut on (units): a near cascade or a lamp touches a few cells
#define WORLDMESH_CELL			1024.0f
#define WORLDMESH_MAX_VERTS		32000
#define WORLDMESH_CASTER_FORMAT	( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD_SIZE( 0, 2 ) )

struct WMVertex_t
{
	Vector		vecPos;
	Vector		vecNormal;
	Vector2D	vecUV;
	Vector		vecS;
	Vector		vecT;
};

struct WMBatch_t
{
	int		iGroup;
	CUtlVector< WMVertex_t > verts;
	CUtlVector< unsigned short > indices;
};

struct WMMesh_t
{
	IMesh			*pMesh;
	IMaterial		*pMaterial;
	VertexFormat_t	fmt;	// what the mesh holds: the material's when it was built
	Vector			vecMins;
	Vector			vecMaxs;
	int				nTris;
};

// what a face's vertices share
struct WMFaceBasis_t
{
	Vector	vecNormal;
	Vector	vecTangentS;
	Vector	vecTangentT;
	Vector	vecS;			// texels per unit
	Vector	vecT;
	float	flOffsetS;
	float	flOffsetT;
	float	flInvWidth;
	float	flInvHeight;

	void Set( WMVertex_t &v, const Vector &vecFlat ) const
	{
		v.vecNormal = vecNormal;
		v.vecS = vecTangentS;
		v.vecT = vecTangentT;
		v.vecUV.x = ( DotProduct( vecFlat, vecS ) + flOffsetS ) * flInvWidth;
		v.vecUV.y = ( DotProduct( vecFlat, vecT ) + flOffsetT ) * flInvHeight;
	}
};

static CUtlVector< WMMesh_t > s_Meshes;
// what a group is drawn with: [0] everything opaque (the material of our own), then one per
// alpha tested material of the map
enum
{
	GROUP_OPAQUE = 0,
	GROUP_FIRST_MATERIAL,
};
static CUtlVector< IMaterial * > s_GroupMaterials;
static IMaterial *s_pCasterMaterial = NULL;
static bool s_bBuildTried = true;	// (no map yet)
static bool s_bReady = false;
// a material's vertex format isn't the one its mesh was built with any more (its shader was
// replaced after the map came up): the meshes are built anew
static bool s_bFormatChanged = false;
static float s_flNextRebuild = 0.0f;

static int s_nDebugViews = 0, s_nDebugMeshes = 0, s_nDebugTris = 0;

// the walls for the occlusion culling (the smallest face that counts, units^2)
#define WORLD_OCCLUDER_MIN_AREA 2048.0f
// (a triangle of a displacement: they come in hundreds, the frame's budget of walls takes the largest on screen)
#define WORLD_OCCLUDER_MIN_DISP_AREA 512.0f
static CUtlVector< WorldOccluder_t > s_Occluders;

const WorldOccluder_t *DeferredWorldMesh_GetOccluders( int &nCount )
{
	nCount = s_Occluders.Count();
	return nCount ? s_Occluders.Base() : NULL;
}

int DeferredWorldMesh_Mode()
{
	return r_deferred_shadow_worldmesh.GetInt();
}

bool DeferredWorldMesh_AlsoEngine()
{
	return r_deferred_shadow_worldmesh_add.GetBool();
}

template< class T >
static bool ReadLump( FileHandle_t hFile, const dheader_t &header, int iLump, CUtlVector< T > &out )
{
	const lump_t &lump = header.lumps[ iLump ];
	// (LZMA: console maps, "bspzip -repack -compress")
	// (these four bytes hold the uncompressed size of a compressed lump)
	const bool bCompressed = lump.fourCC[0] || lump.fourCC[1] || lump.fourCC[2] || lump.fourCC[3];
	if ( bCompressed || lump.filelen < 0 || ( lump.filelen % sizeof( T ) ) != 0 )
		return false;
	out.SetCount( lump.filelen / sizeof( T ) );
	if ( lump.filelen == 0 )
		return true;
	filesystem->Seek( hFile, lump.fileofs, FILESYSTEM_SEEK_HEAD );
	return filesystem->Read( out.Base(), lump.filelen, hFile ) == lump.filelen;
}

static void DestroyWorldMeshes()
{
	if ( s_Meshes.Count() > 0 )
	{
		CMatRenderContextPtr pRenderContext( materials );
		for ( int i = 0; i < s_Meshes.Count(); i++ )
		{
			if ( s_Meshes[i].pMesh )
				pRenderContext->DestroyStaticMesh( s_Meshes[i].pMesh );
		}
		s_Meshes.RemoveAll();
	}
	// (the first is ours and outlives the map)
	for ( int i = GROUP_FIRST_MATERIAL; i < s_GroupMaterials.Count(); i++ )
		s_GroupMaterials[i]->DecrementReferenceCount();
	s_GroupMaterials.RemoveAll();
	s_Occluders.Purge();
	s_bReady = false;
}

static IMaterial *GetCasterMaterial()
{
	if ( !s_pCasterMaterial )
	{
		// a brush material of the renderer: in a shadow view it draws its shadow pass, as the
		// materials of the map do
		KeyValues *pKV = new KeyValues( "DEFERRED_BRUSH" );
		pKV->SetString( "$basetexture", "white" );
		s_pCasterMaterial = materials->CreateMaterial( "__hl2rpm_world_caster", pKV );
		if ( s_pCasterMaterial )
			s_pCasterMaterial->IncrementReferenceCount();
	}
	return ( s_pCasterMaterial && !s_pCasterMaterial->IsErrorMaterial() ) ? s_pCasterMaterial : NULL;
}

// -1 = casts nothing, 0 = opaque, > 0 = the group of its alpha tested material
static int ClassifyMaterial( const char *pszName )
{
	IMaterial *pMat = materials->FindMaterial( pszName, TEXTURE_GROUP_WORLD, false );
	if ( !pMat || pMat->IsErrorMaterial() )
		return 0;	// (drawn all the same - as the checkerboard)
	if ( pMat->IsTranslucent() || pMat->GetMaterialVarFlag( MATERIAL_VAR_NO_DRAW ) )
		return -1;
	const char *pszShader = pMat->GetShaderName();
	if ( pszShader && ( V_stristr( pszShader, "Water" ) || V_stristr( pszShader, "Refract" ) ) )
		return -1;
	if ( !pMat->IsAlphaTested() )
		return 0;

	int iGroup = s_GroupMaterials.Find( pMat );
	if ( iGroup < GROUP_FIRST_MATERIAL )
	{
		pMat->IncrementReferenceCount();
		iGroup = s_GroupMaterials.AddToTail( pMat );
	}
	return iGroup;
}

static WMBatch_t *GetBatch( CUtlVector< WMBatch_t * > &batches, CUtlMap< uint64, int > &lookup,
	int iGroup, const Vector &vecCenter, int nVerts )
{
	const int iCellX = (int)floorf( vecCenter.x / WORLDMESH_CELL ) + 4096;
	const int iCellY = (int)floorf( vecCenter.y / WORLDMESH_CELL ) + 4096;
	const uint64 key = ( (uint64)iGroup << 40 ) | ( (uint64)( iCellX & 0xFFFFF ) << 20 ) | (uint64)( iCellY & 0xFFFFF );

	const unsigned short idx = lookup.Find( key );
	if ( idx != lookup.InvalidIndex() )
	{
		WMBatch_t *pBatch = batches[ lookup[ idx ] ];
		if ( pBatch->verts.Count() + nVerts <= WORLDMESH_MAX_VERTS )
			return pBatch;
		// full: it stays in the list as it is, the cell goes on in a new one
		lookup.RemoveAt( idx );
	}

	WMBatch_t *pBatch = new WMBatch_t;
	pBatch->iGroup = iGroup;
	lookup.Insert( key, batches.AddToTail( pBatch ) );
	return pBatch;
}

static IMesh *CreateMesh( IMatRenderContext *pRenderContext, IMaterial *pMat, VertexFormat_t fmt, const WMBatch_t &batch )
{
	IMesh *pMesh = pRenderContext->CreateStaticMesh( fmt, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_WORLD, pMat );
	if ( !pMesh )
		return NULL;

	// (an alpha tested material asks for whatever its other passes read - tangents, a color,
	// lightmap coordinates: its mesh has to hold them, the shadow pass reads none of it)
	const bool bNormal = ( fmt & VERTEX_NORMAL ) != 0;
	const bool bColor = ( fmt & VERTEX_COLOR ) != 0;
	const bool bTangentS = ( fmt & VERTEX_TANGENT_S ) != 0;
	const bool bTangentT = ( fmt & VERTEX_TANGENT_T ) != 0;
	int nTexCoordSize[ VERTEX_MAX_TEXTURE_COORDINATES ];
	for ( int t = 0; t < VERTEX_MAX_TEXTURE_COORDINATES; t++ )
		nTexCoordSize[t] = TexCoordSize( t, fmt );

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, batch.verts.Count(), batch.indices.Count() );
	for ( int i = 0; i < batch.verts.Count(); i++ )
	{
		const WMVertex_t &v = batch.verts[i];
		meshBuilder.Position3fv( v.vecPos.Base() );
		if ( bNormal )
			meshBuilder.Normal3fv( v.vecNormal.Base() );
		if ( bColor )
			meshBuilder.Color4ub( 255, 255, 255, 255 );
		if ( bTangentS )
			meshBuilder.TangentS3fv( v.vecS.Base() );
		if ( bTangentT )
			meshBuilder.TangentT3fv( v.vecT.Base() );
		for ( int t = 0; t < VERTEX_MAX_TEXTURE_COORDINATES; t++ )
		{
			const float flU = ( t == 0 ) ? v.vecUV.x : 0.0f;
			const float flV = ( t == 0 ) ? v.vecUV.y : 0.0f;
			switch ( nTexCoordSize[t] )
			{
			case 1: meshBuilder.TexCoord1f( t, flU ); break;
			case 2: meshBuilder.TexCoord2f( t, flU, flV ); break;
			case 3: meshBuilder.TexCoord3f( t, flU, flV, 0.0f ); break;
			case 4: meshBuilder.TexCoord4f( t, flU, flV, 0.0f, 0.0f ); break;
			}
		}
		meshBuilder.AdvanceVertex();
	}
	for ( int i = 0; i < batch.indices.Count(); i++ )
		meshBuilder.FastIndex( batch.indices[i] );
	meshBuilder.End();
	return pMesh;
}

static int __cdecl CompareMeshes( const WMMesh_t *pA, const WMMesh_t *pB )
{
	// the opaque faces first (most of the world: one Bind for all of them), then by material
	const bool bCasterA = ( pA->pMaterial == s_pCasterMaterial ), bCasterB = ( pB->pMaterial == s_pCasterMaterial );
	if ( bCasterA != bCasterB )
		return bCasterA ? -1 : 1;
	if ( pA->pMaterial != pB->pMaterial )
		return ( pA->pMaterial < pB->pMaterial ) ? -1 : 1;
	return 0;
}

static bool BuildWorldMeshes()
{
	// (engine->GetLevelName() cuts the name of a long map short)
	const char *pszLevel = UTIL_GetLevelFileName();
	if ( !pszLevel || !pszLevel[0] )
		return false;

	if ( !GetCasterMaterial() )
	{
		Warning( "[worldmesh] no depth material: the engine draws the world into the shadow maps\n" );
		return false;
	}

	RPM_CRUMB( "worldmesh: build %s", pszLevel );
	const double flStart = Plat_FloatTime();

	const FileHandle_t hFile = filesystem->Open( pszLevel, "rb", "GAME" );
	if ( !hFile )
	{
		Warning( "[worldmesh] can't open %s: the engine draws the world into the shadow maps\n", pszLevel );
		return false;
	}

	dheader_t header;
	bool bOk = filesystem->Read( &header, sizeof( header ), hFile ) == sizeof( header )
		&& header.ident == IDBSPHEADER && header.version >= MINBSPVERSION && header.version <= 21;

	CUtlVector< dmodel_t > models;
	CUtlVector< dface_t > faces;
	CUtlVector< int > surfedges;
	CUtlVector< dedge_t > edges;
	CUtlVector< dvertex_t > vertexes;
	CUtlVector< dplane_t > planes;
	CUtlVector< texinfo_t > texinfos;
	CUtlVector< dtexdata_t > texdatas;
	CUtlVector< int > stringTable;
	CUtlVector< char > stringData;
	CUtlVector< ddispinfo_t > dispinfos;
	CUtlVector< CDispVert > dispverts;

	if ( bOk )
	{
		bOk = ReadLump( hFile, header, LUMP_MODELS, models )
			&& ReadLump( hFile, header, LUMP_FACES, faces )
			&& ReadLump( hFile, header, LUMP_SURFEDGES, surfedges )
			&& ReadLump( hFile, header, LUMP_EDGES, edges )
			&& ReadLump( hFile, header, LUMP_VERTEXES, vertexes )
			&& ReadLump( hFile, header, LUMP_PLANES, planes )
			&& ReadLump( hFile, header, LUMP_TEXINFO, texinfos )
			&& ReadLump( hFile, header, LUMP_TEXDATA, texdatas )
			&& ReadLump( hFile, header, LUMP_TEXDATA_STRING_TABLE, stringTable )
			&& ReadLump( hFile, header, LUMP_TEXDATA_STRING_DATA, stringData )
			&& ReadLump( hFile, header, LUMP_DISPINFO, dispinfos )
			&& ReadLump( hFile, header, LUMP_DISP_VERTS, dispverts );
		// (a map compiled for HDR only: the geometry of the two face lumps is the same)
		if ( bOk && faces.Count() == 0 )
			bOk = ReadLump( hFile, header, LUMP_FACES_HDR, faces );
	}
	filesystem->Close( hFile );
	const double flRead = Plat_FloatTime();

	if ( !bOk || models.Count() < 1 || faces.Count() == 0 )
	{
		Warning( "[worldmesh] can't read the faces of %s: the engine draws the world into the shadow maps\n", pszLevel );
		return false;
	}
	if ( stringData.Count() > 0 )
		stringData[ stringData.Count() - 1 ] = 0;

	s_GroupMaterials.AddToTail( s_pCasterMaterial );

	// what each texture of the map is: -2 = not looked at yet
	CUtlVector< int > texdataGroup;
	texdataGroup.SetCount( texdatas.Count() );
	for ( int i = 0; i < texdataGroup.Count(); i++ )
		texdataGroup[i] = -2;

	CUtlVector< WMBatch_t * > batches;
	CUtlMap< uint64, int > lookup( DefLessFunc( uint64 ) );
	CUtlVector< WMVertex_t > poly;
	int nFaces = 0, nDisps = 0, nSkipped = 0, nBadDisps = 0;

	// the occluders: which face each edge of the map belongs to, per wall (plane and side) - an
	// edge two faces of one wall share is no silhouette
	s_Occluders.RemoveAll();
	CUtlVector< int > occluderEdges;		// the map edges of each occluder's sides, in order
	CUtlVector< int > occluderEdgeStart;	// per occluder: where its edges start in occluderEdges
	CUtlVector< int > occluderEdgeSides;	// per side of each occluder (in order): how many edges it is made of
	CUtlVector< int > occluderPlane;		// per occluder: plane * 2 + side
	CUtlMap< uint64, int > edgeUse( DefLessFunc( uint64 ) );	// ( plane, side, edge ) -> faces using it

	const dmodel_t &world = models[0];
	const int iLastFace = Min( world.firstface + world.numfaces, faces.Count() );
	for ( int iFace = Max( world.firstface, 0 ); iFace < iLastFace; iFace++ )
	{
		const dface_t &face = faces[ iFace ];
		if ( face.numedges < 3 || face.texinfo < 0 || face.texinfo >= texinfos.Count()
			|| face.firstedge < 0 || face.firstedge + face.numedges > surfedges.Count()
			|| face.planenum >= planes.Count() )
			continue;

		const texinfo_t &ti = texinfos[ face.texinfo ];
		if ( ( ti.flags & ( SURF_SKY | SURF_SKY2D | SURF_NODRAW | SURF_HINT | SURF_SKIP | SURF_TRIGGER | SURF_WARP | SURF_TRANS ) ) != 0
			|| ti.texdata < 0 || ti.texdata >= texdatas.Count() )
		{
			nSkipped++;
			continue;
		}

		const dtexdata_t &td = texdatas[ ti.texdata ];
		int &iTexGroup = texdataGroup[ ti.texdata ];
		if ( iTexGroup == -2 )
		{
			iTexGroup = 0;
			if ( td.nameStringTableID >= 0 && td.nameStringTableID < stringTable.Count() )
			{
				const int iOffset = stringTable[ td.nameStringTableID ];
				if ( iOffset >= 0 && iOffset < stringData.Count() )
					iTexGroup = ClassifyMaterial( &stringData[ iOffset ] );
			}
		}
		if ( iTexGroup < 0 )
		{
			nSkipped++;
			continue;
		}
		const int iGroup = ( iTexGroup >= GROUP_FIRST_MATERIAL ) ? iTexGroup : GROUP_OPAQUE;

		// the corners of the face
		poly.RemoveAll();
		bool bBad = false;
		for ( int k = 0; k < face.numedges && !bBad; k++ )
		{
			const int iSurfEdge = surfedges[ face.firstedge + k ];
			const int iEdge = abs( iSurfEdge );
			if ( iEdge >= edges.Count() )
			{
				bBad = true;
				break;
			}
			const int iVertex = edges[ iEdge ].v[ iSurfEdge >= 0 ? 0 : 1 ];
			if ( iVertex >= vertexes.Count() )
			{
				bBad = true;
				break;
			}
			WMVertex_t &v = poly[ poly.AddToTail() ];
			v.vecPos = vertexes[ iVertex ].point;
		}
		if ( bBad )
			continue;

		WMFaceBasis_t basis;
		// (planenum is the face's own plane - vbsp writes it so; side only says it is turned away
		// from the plane of the node that holds the face. Flipped by side, half the walls of a
		// map faced away from the eye and were never occluders.)
		basis.vecNormal = planes[ face.planenum ].normal;
		basis.vecS.Init( ti.textureVecsTexelsPerWorldUnits[0][0], ti.textureVecsTexelsPerWorldUnits[0][1], ti.textureVecsTexelsPerWorldUnits[0][2] );
		basis.vecT.Init( ti.textureVecsTexelsPerWorldUnits[1][0], ti.textureVecsTexelsPerWorldUnits[1][1], ti.textureVecsTexelsPerWorldUnits[1][2] );
		basis.flOffsetS = ti.textureVecsTexelsPerWorldUnits[0][3];
		basis.flOffsetT = ti.textureVecsTexelsPerWorldUnits[1][3];
		basis.flInvWidth = 1.0f / (float)Max( td.width, 1 );
		basis.flInvHeight = 1.0f / (float)Max( td.height, 1 );
		basis.vecTangentS = basis.vecS;
		basis.vecTangentT = basis.vecT;
		VectorNormalize( basis.vecTangentS );
		VectorNormalize( basis.vecTangentT );

		if ( face.dispinfo >= 0 )
		{
			// A displacement: the face is its base quad, cut into ( 2^power + 1 )^2 points; each
			// moved along its own vector. The first corner is the one at the start position.
			if ( face.numedges != 4 || face.dispinfo >= dispinfos.Count() )
			{
				nBadDisps++;
				continue;
			}
			const ddispinfo_t &disp = dispinfos[ face.dispinfo ];
			const int nSide = ( 1 << disp.power ) + 1;
			if ( disp.power < 2 || disp.power > 4 || disp.m_iDispVertStart < 0
				|| disp.m_iDispVertStart + nSide * nSide > dispverts.Count() )
			{
				nBadDisps++;
				continue;
			}

			int iStart = 0;
			float flBest = FLT_MAX;
			for ( int k = 0; k < 4; k++ )
			{
				const float flDist = poly[k].vecPos.DistToSqr( disp.startPosition );
				if ( flDist < flBest )
				{
					flBest = flDist;
					iStart = k;
				}
			}
			Vector vecCorner[4];
			for ( int k = 0; k < 4; k++ )
				vecCorner[k] = poly[ ( k + iStart ) & 3 ].vecPos;

			const Vector vecCenter = ( vecCorner[0] + vecCorner[1] + vecCorner[2] + vecCorner[3] ) * 0.25f;
			WMBatch_t *pBatch = GetBatch( batches, lookup, iGroup, vecCenter, nSide * nSide );
			const int iBase = pBatch->verts.Count();

			const float flInv = 1.0f / (float)( nSide - 1 );
			for ( int i = 0; i < nSide; i++ )
			{
				const Vector vecEnd0 = vecCorner[0] + ( vecCorner[1] - vecCorner[0] ) * ( i * flInv );
				const Vector vecEnd1 = vecCorner[3] + ( vecCorner[2] - vecCorner[3] ) * ( i * flInv );
				for ( int j = 0; j < nSide; j++ )
				{
					const Vector vecFlat = vecEnd0 + ( vecEnd1 - vecEnd0 ) * ( j * flInv );
					const CDispVert &dv = dispverts[ disp.m_iDispVertStart + i * nSide + j ];
					WMVertex_t &v = pBatch->verts[ pBatch->verts.AddToTail() ];
					v.vecPos = vecFlat + dv.m_vVector * dv.m_flDist;
					basis.Set( v, vecFlat );
				}
			}
			// two triangles per cell, the diagonal turning from cell to cell (as the engine cuts them)
			for ( int i = 0; i < nSide - 1; i++ )
			{
				for ( int j = 0; j < nSide - 1; j++ )
				{
					const int ndx = i * nSide + j;
					const unsigned short a = (unsigned short)( iBase + ndx );
					const unsigned short b = (unsigned short)( iBase + ndx + nSide );
					const unsigned short c = (unsigned short)( iBase + ndx + nSide + 1 );
					const unsigned short d = (unsigned short)( iBase + ndx + 1 );
					if ( ndx & 1 )
					{
						pBatch->indices.AddToTail( a ); pBatch->indices.AddToTail( b ); pBatch->indices.AddToTail( d );
						pBatch->indices.AddToTail( d ); pBatch->indices.AddToTail( b ); pBatch->indices.AddToTail( c );
					}
					else
					{
						pBatch->indices.AddToTail( a ); pBatch->indices.AddToTail( b ); pBatch->indices.AddToTail( c );
						pBatch->indices.AddToTail( a ); pBatch->indices.AddToTail( c ); pBatch->indices.AddToTail( d );
					}
				}
			}

			// HL2RPM: the triangles of an opaque displacement are walls for the occlusion culling
			// too (the hills of an open map). Its edges inside the grid are seams; a triangle hides
			// only from its front - the side of the base face (it is drawn one-sided).
			if ( iGroup == GROUP_OPAQUE )
			{
				#define DISP_POS( i, j ) pBatch->verts[ iBase + ( i ) * nSide + ( j ) ].vecPos
				Vector vecSum( 0, 0, 0 );
				for ( int i = 0; i < nSide - 1; i++ )
					for ( int j = 0; j < nSide - 1; j++ )
						vecSum += CrossProduct( DISP_POS( i + 1, j ) - DISP_POS( i, j ), DISP_POS( i, j + 1 ) - DISP_POS( i, j ) );
				const float flSign = ( DotProduct( vecSum, basis.vecNormal ) >= 0.0f ) ? 1.0f : -1.0f;
				for ( int i = 0; i < nSide - 1; i++ )
				{
					for ( int j = 0; j < nSide - 1; j++ )
					{
						const Vector &a = DISP_POS( i, j ), &b = DISP_POS( i + 1, j ), &c = DISP_POS( i + 1, j + 1 ), &d = DISP_POS( i, j + 1 );
						const bool bInAB = j > 0, bInBC = i + 1 < nSide - 1, bInCD = j + 1 < nSide - 1, bInDA = i > 0;
						const Vector *pTris[2][3];
						unsigned int nInner[2];
						if ( ( i * nSide + j ) & 1 )
						{
							pTris[0][0] = &a; pTris[0][1] = &b; pTris[0][2] = &d;	// a-b, b-d (the diagonal), d-a
							nInner[0] = ( bInAB ? 1u : 0u ) | 2u | ( bInDA ? 4u : 0u );
							pTris[1][0] = &d; pTris[1][1] = &b; pTris[1][2] = &c;	// d-b (the diagonal), b-c, c-d
							nInner[1] = 1u | ( bInBC ? 2u : 0u ) | ( bInCD ? 4u : 0u );
						}
						else
						{
							pTris[0][0] = &a; pTris[0][1] = &b; pTris[0][2] = &c;	// a-b, b-c, c-a (the diagonal)
							nInner[0] = ( bInAB ? 1u : 0u ) | ( bInBC ? 2u : 0u ) | 4u;
							pTris[1][0] = &a; pTris[1][1] = &c; pTris[1][2] = &d;	// a-c (the diagonal), c-d, d-a
							nInner[1] = 1u | ( bInCD ? 2u : 0u ) | ( bInDA ? 4u : 0u );
						}
						for ( int t = 0; t < 2; t++ )
						{
							const Vector &p0 = *pTris[t][0], &p1 = *pTris[t][1], &p2 = *pTris[t][2];
							Vector vecN = CrossProduct( p1 - p0, p2 - p0 );
							const float flTriArea = VectorNormalize( vecN ) * 0.5f;
							if ( flTriArea < WORLD_OCCLUDER_MIN_DISP_AREA )
								continue;
							WorldOccluder_t &occ = s_Occluders[ s_Occluders.AddToTail() ];
							occ.nVerts = 3;
							occ.vecVerts[0] = p0; occ.vecVerts[1] = p1; occ.vecVerts[2] = p2;
							occ.vecCenter = ( p0 + p1 + p2 ) / 3.0f;
							occ.vecNormal = vecN * flSign;
							occ.flArea = flTriArea;
							occ.nInnerEdges = nInner[t];
							// (no wall seams to look up: -1)
							occluderPlane.AddToTail( -1 );
							occluderEdgeStart.AddToTail( occluderEdges.Count() );
							for ( int k = 0; k < 3; k++ )
								occluderEdgeSides.AddToTail( 0 );
						}
					}
				}
				#undef DISP_POS
			}
			nDisps++;
			continue;
		}

		Vector vecCenter( 0, 0, 0 );
		for ( int k = 0; k < poly.Count(); k++ )
		{
			basis.Set( poly[k], poly[k].vecPos );
			vecCenter += poly[k].vecPos;
		}
		vecCenter /= (float)poly.Count();

		// a wall for the occlusion culling: opaque, flat, large
		if ( iGroup == GROUP_OPAQUE )
		{
			Vector vecCross( 0, 0, 0 );
			for ( int k = 1; k < poly.Count() - 1; k++ )
				vecCross += CrossProduct( poly[k].vecPos - poly[0].vecPos, poly[k + 1].vecPos - poly[0].vecPos );
			const float flArea = vecCross.Length() * 0.5f;
			const int iPlaneSide = face.planenum * 2 + ( face.side ? 1 : 0 );

			// every edge of the face counts towards the seams of its wall
			for ( int k = 0; k < poly.Count(); k++ )
			{
				const uint64 key = ( (uint64)iPlaneSide << 32 ) | (uint64)(unsigned int)abs( surfedges[ face.firstedge + k ] );
				const unsigned short idx = edgeUse.Find( key );
				if ( idx == edgeUse.InvalidIndex() )
					edgeUse.Insert( key, 1 );
				else
					edgeUse[ idx ]++;
			}

			// The corners on a straight side go: vbsp puts one wherever another brush meets the
			// side (t-junctions), and a long wall had dozens - more than an occluder holds. A
			// corner is dropped only where that does not make the wall larger than it is (a
			// convex one, or one off the side by less than a hair). A side made of several edges
			// is a seam only if all of them are.
			int nCorners = poly.Count();
			int iCorner[ 64 ];
			int nSideEdges[ 64 ];		// the edges from corner k to the next one kept
			bool bOccluder = nCorners <= 64 && flArea >= WORLD_OCCLUDER_MIN_AREA;
			if ( bOccluder )
			{
				for ( int k = 0; k < nCorners; k++ )
				{
					iCorner[k] = k;
					nSideEdges[k] = 1;
				}
				const Vector vecTurn = vecCross;	// (the winding's own normal: its turns are convex this way)
				bool bDropped = true;
				while ( bDropped && nCorners > 3 )
				{
					bDropped = false;
					for ( int k = 0; k < nCorners && nCorners > 3; k++ )
					{
						const Vector &a = poly[ iCorner[ ( k + nCorners - 1 ) % nCorners ] ].vecPos;
						const Vector &b = poly[ iCorner[k] ].vecPos;
						const Vector &c = poly[ iCorner[ ( k + 1 ) % nCorners ] ].vecPos;
						const Vector vecAC = c - a;
						const float flLen = vecAC.Length();
						if ( flLen < 0.01f )
							continue;
						const Vector vecTurnB = CrossProduct( b - a, c - b );
						const float flOff = vecTurnB.Length() / flLen;	// how far b is from the line a-c
						const bool bConvex = DotProduct( vecTurnB, vecTurn ) >= 0.0f;
						if ( !( ( bConvex && flOff <= 0.3f ) || flOff <= 0.02f ) || DotProduct( b - a, c - b ) <= 0.0f )
							continue;
						// the side before b takes over b's
						const int iPrev = ( k + nCorners - 1 ) % nCorners;
						nSideEdges[ iPrev ] += nSideEdges[k];
						for ( int m = k; m < nCorners - 1; m++ )
						{
							iCorner[m] = iCorner[ m + 1 ];
							nSideEdges[m] = nSideEdges[ m + 1 ];
						}
						nCorners--;
						k--;
						bDropped = true;
					}
				}
				bOccluder = nCorners <= WORLD_OCCLUDER_MAX_VERTS;
			}
			if ( bOccluder )
			{
				WorldOccluder_t &occ = s_Occluders[ s_Occluders.AddToTail() ];
				occ.nVerts = nCorners;
				occ.vecCenter = vecCenter;
				occ.vecNormal = basis.vecNormal;
				occ.flArea = flArea;
				occ.nInnerEdges = 0;
				occluderPlane.AddToTail( iPlaneSide );
				occluderEdgeStart.AddToTail( occluderEdges.Count() );
				for ( int k = 0; k < nCorners; k++ )
				{
					occ.vecVerts[k] = poly[ iCorner[k] ].vecPos;
					occluderEdgeSides.AddToTail( nSideEdges[k] );
					for ( int m = 0; m < nSideEdges[k]; m++ )
						occluderEdges.AddToTail( abs( surfedges[ face.firstedge + ( iCorner[k] + m ) % poly.Count() ] ) );
				}
			}
		}

		WMBatch_t *pBatch = GetBatch( batches, lookup, iGroup, vecCenter, poly.Count() );
		const int iBase = pBatch->verts.Count();
		pBatch->verts.AddMultipleToTail( poly.Count(), poly.Base() );
		for ( int k = 1; k < poly.Count() - 1; k++ )
		{
			pBatch->indices.AddToTail( (unsigned short)iBase );
			pBatch->indices.AddToTail( (unsigned short)( iBase + k ) );
			pBatch->indices.AddToTail( (unsigned short)( iBase + k + 1 ) );
		}
		nFaces++;
	}

	for ( int i = 0, iSide = 0; i < s_Occluders.Count(); i++ )
	{
		WorldOccluder_t &occ = s_Occluders[i];
		if ( occluderPlane[i] < 0 )
		{
			// (a triangle of a displacement: its seams are known already)
			iSide += occ.nVerts;
			continue;
		}
		int iEdge = occluderEdgeStart[i];
		for ( int k = 0; k < occ.nVerts; k++, iSide++ )
		{
			bool bInner = true;
			for ( int m = 0; m < occluderEdgeSides[ iSide ]; m++, iEdge++ )
			{
				const uint64 key = ( (uint64)occluderPlane[i] << 32 ) | (uint64)(unsigned int)occluderEdges[ iEdge ];
				const unsigned short idx = edgeUse.Find( key );
				if ( idx == edgeUse.InvalidIndex() || edgeUse[ idx ] <= 1 )
					bInner = false;
			}
			if ( bInner )
				occ.nInnerEdges |= ( 1u << k );
		}
	}

	const double flFaces = Plat_FloatTime();

	// the meshes (with the queued material system the render thread may be at work: wait for it)
	int nVerts = 0, nTris = 0;
	{
		const MaterialLock_t hLock = materials->Lock();
		CMatRenderContextPtr pRenderContext( materials );
		for ( int i = 0; i < batches.Count(); i++ )
		{
			const WMBatch_t &batch = *batches[i];
			if ( batch.verts.Count() < 3 || batch.indices.Count() < 3 )
				continue;

			WMMesh_t mesh;
			mesh.pMaterial = s_GroupMaterials[ batch.iGroup ];
			// Our material: what the shadow pass of a brush material reads. An alpha tested
			// material is drawn as itself and has to find all it asks for in its mesh. (A material
			// has no format before it is loaded, and a mesh of the format "nothing" can't be
			// locked: Error() and out.)
			mesh.fmt = ( batch.iGroup < GROUP_FIRST_MATERIAL ) ? WORLDMESH_CASTER_FORMAT
				: ( mesh.pMaterial->GetVertexFormat() & ~VERTEX_FORMAT_COMPRESSED );
			if ( !( mesh.fmt & VERTEX_POSITION ) )
				continue;
			mesh.pMesh = CreateMesh( pRenderContext, mesh.pMaterial, mesh.fmt, batch );
			if ( !mesh.pMesh )
				continue;
			ClearBounds( mesh.vecMins, mesh.vecMaxs );
			for ( int k = 0; k < batch.verts.Count(); k++ )
				AddPointToBounds( batch.verts[k].vecPos, mesh.vecMins, mesh.vecMaxs );
			mesh.nTris = batch.indices.Count() / 3;
			nVerts += batch.verts.Count();
			nTris += mesh.nTris;

			s_Meshes.AddToTail( mesh );
		}
		pRenderContext.SafeRelease();
		materials->Unlock( hLock );
	}
	batches.PurgeAndDeleteElements();
	// by material: the opaque ones (the depth material) first - one Bind for all of them
	s_Meshes.Sort( CompareMeshes );

	if ( s_Meshes.Count() == 0 )
	{
		Warning( "[worldmesh] %s: no faces made it into a mesh: the engine draws the world into the shadow maps\n", pszLevel );
		DestroyWorldMeshes();
		return false;
	}

	const double flEnd = Plat_FloatTime();
	Msg( "[worldmesh] %s: %d faces + %d displacements (%d skipped%s) -> %d meshes, %d vertices, %d triangles, %d alpha tested materials, %d occluders; %.0f ms (file %.0f, faces %.0f, meshes %.0f)\n",
		pszLevel, nFaces, nDisps, nSkipped, nBadDisps ? ", some displacements unreadable" : "",
		s_Meshes.Count(), nVerts, nTris, s_GroupMaterials.Count() - GROUP_FIRST_MATERIAL, s_Occluders.Count(), ( flEnd - flStart ) * 1000.0,
		( flRead - flStart ) * 1000.0, ( flFaces - flRead ) * 1000.0, ( flEnd - flFaces ) * 1000.0 );
	return true;
}

bool DeferredWorldMesh_DrawShadowCasters()
{
	// (built ahead of the frame's views - CDeferredWorldMeshSystem::PreRender)
	if ( !s_bReady )
		return false;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	IMaterial *pBound = NULL;
	int nMeshes = 0, nTris = 0;
	const bool bCull = r_deferred_shadow_worldmesh_cull.GetBool();
	for ( int i = 0; i < s_Meshes.Count(); i++ )
	{
		const WMMesh_t &mesh = s_Meshes[i];
		// (the engine's frustum of the view pushed last - this shadow view's)
		if ( bCull && engine->CullBox( mesh.vecMins, mesh.vecMaxs ) )
			continue;
		if ( pBound != mesh.pMaterial )
		{
			// (a mesh that lacks what its material asks for is not drawn at all)
			if ( mesh.pMaterial != s_pCasterMaterial
				&& ( mesh.pMaterial->GetVertexFormat() & ~VERTEX_FORMAT_COMPRESSED ) != mesh.fmt )
			{
				s_bFormatChanged = true;
				continue;
			}
			pRenderContext->Bind( mesh.pMaterial );
			pBound = mesh.pMaterial;
		}
		mesh.pMesh->Draw();
		nMeshes++;
		nTris += mesh.nTris;
	}

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();

	if ( r_deferred_shadow_worldmesh_debug.GetBool() )
	{
		s_nDebugViews++;
		s_nDebugMeshes += nMeshes;
		s_nDebugTris += nTris;
		static float s_flNext = 0.0f;
		if ( gpGlobals->realtime > s_flNext || gpGlobals->realtime < s_flNext - 2.0f )
		{
			s_flNext = gpGlobals->realtime + 1.0f;
			Msg( "[worldmesh] %d shadow views in the last second: %.1f of %d meshes, %.0f triangles a view\n",
				s_nDebugViews, (float)s_nDebugMeshes / s_nDebugViews, s_Meshes.Count(), (float)s_nDebugTris / s_nDebugViews );
			s_nDebugViews = s_nDebugMeshes = s_nDebugTris = 0;
		}
	}
	return true;
}

// Dev: what the shadow maps get, seen from the player's camera
void DeferredWorldMesh_DebugDraw()
{
	const int iShow = r_deferred_shadow_worldmesh_show.GetInt();
	if ( iShow <= 0 || !s_bReady )
		return;

	static IMaterial *s_pWire = NULL, *s_pSolid = NULL;
	if ( !s_pWire )
	{
		s_pWire = materials->FindMaterial( "debug/debugwireframe", TEXTURE_GROUP_OTHER );
		s_pSolid = materials->FindMaterial( "debug/debugdrawflat", TEXTURE_GROUP_OTHER );
	}
	IMaterial *pMat = ( iShow >= 2 ) ? s_pSolid : s_pWire;
	if ( !pMat || pMat->IsErrorMaterial() )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->Bind( pMat );
	for ( int i = 0; i < s_Meshes.Count(); i++ )
	{
		if ( !engine->CullBox( s_Meshes[i].vecMins, s_Meshes[i].vecMaxs ) )
			s_Meshes[i].pMesh->Draw();
	}
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}

class CDeferredWorldMeshSystem : public CAutoGameSystemPerFrame
{
public:
	CDeferredWorldMeshSystem() : CAutoGameSystemPerFrame( "CDeferredWorldMeshSystem" ) {}

	virtual void LevelInitPostEntity()
	{
		DestroyWorldMeshes();
		s_bBuildTried = false;
		s_bFormatChanged = false;
	}

	virtual void LevelShutdownPreEntity()
	{
		// (the materials of the map are let go of here: held any longer they outlive the map)
		DestroyWorldMeshes();
		s_bBuildTried = true;
	}

	// ahead of the frame's views, not in the middle of one
	virtual void PreRender()
	{
		if ( DeferredWorldMesh_Mode() <= 0 || !engine->IsInGame() )
			return;

		if ( s_bFormatChanged && s_bBuildTried && gpGlobals->realtime > s_flNextRebuild )
		{
			DestroyWorldMeshes();
			s_bBuildTried = false;
		}
		if ( !s_bBuildTried )
		{
			s_bBuildTried = true;
			s_bFormatChanged = false;
			s_flNextRebuild = gpGlobals->realtime + 2.0f;
			s_bReady = BuildWorldMeshes();
		}
	}

	virtual void Shutdown()
	{
		DestroyWorldMeshes();
		if ( s_pCasterMaterial )
		{
			s_pCasterMaterial->DecrementReferenceCount();
			s_pCasterMaterial = NULL;
		}

	}
};
static CDeferredWorldMeshSystem s_DeferredWorldMeshSystem;

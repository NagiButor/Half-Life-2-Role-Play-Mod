//========= HL2RPM ============================================================//
//
// Purpose: The engine's world lists put in the order of its static meshes.
//
// The engine draws the opaque faces of the map of a view in batches of up to
// 32768 indices (Shader_DrawChainsStatic): the material groups in the order its
// walk through the BSP met them, then each batch mesh by mesh. Whenever the
// static mesh of the next group differs from the one before, the batch gets a
// new entry - 127 entries, then "Too many vertex format changes in frame, whole
// world not rendered", and every group after that is NOT drawn in that view.
// The faces of a map live in many static meshes (a few for each vertex format,
// filled first-fit), and a view that sees a few hundred materials alternates
// between them at nearly every group. The shadow views were taken off the
// engine's world for this (deferred_worldmesh.cpp); with an open map - no
// areaportals, a long street - the main view went over the limit as well:
// buildings at the end of the street were missing, every frame.
//
// Right after the engine builds the lists of a view, the groups of each sort
// group are put in the order of their static mesh, stably (within a mesh they
// stay in the order the walk met them). The engine sorts each batch by mesh
// before drawing it anyway, so the same faces are drawn the same way; a batch
// now changes meshes only as many times as it has meshes. The order of the
// groups means nothing else to the engine (projected texture decals, the depth
// fill and the lightmap page lists go through them in any order).
//
// The list is found in the engine's world list object and each group pointer is
// checked before anything is moved; the meshes of the sort IDs are read from the
// engine's own table (g_WorldStaticMeshes), found once in engine.dll's data by
// its shape. Without it the groups are ordered by vertex format and sort ID -
// the order the engine fills its meshes in, nearly the same thing.
//
//=============================================================================//

#include "cbase.h"
#include "deferred/deferred_shared_common.h"
#include "deferred/deferred_worldlists.h"
#include "ivrenderview.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "hl2rpm_crashdebug.h"

#include "tier0/memdbgon.h"

static ConVar r_deferred_worldlists_sort( "r_deferred_worldlists_sort", "1", 0,
	"Put the engine's world lists in the order of its static meshes (no \"Too many vertex format changes\" with a lot of the map in view)" );
static ConVar r_deferred_worldlists_debug( "r_deferred_worldlists_debug", "0", 0,
	"Dev: print the mesh changes of the engine's world batches before and after the sort, once a second" );

// The engine's structures (32 bit; checked on the objects before use)
struct EngineUtlVector_t
{
	unsigned char *pMemory;
	int nAllocationCount;
	int nGrowSize;
	int nSize;
	unsigned char *pElements;
};

// surfacesortgroup_t
struct EngineSortGroup_t
{
	short listHead;
	short listTail;
	unsigned short vertexCount;
	short groupListIndex;
	unsigned short vertexCountNoDetail;
	unsigned short indexCountNoDetail;
	unsigned short triangleCount;
	unsigned short surfaceCount;
};

// CMSurfaceSortList
struct EngineSortList_t
{
	EngineUtlVector_t list;
	EngineUtlVector_t groups;
	EngineUtlVector_t groupUsed;
	EngineUtlVector_t sortGroupLists[ MAX_MAT_SORT_GROUPS ];
	EngineSortGroup_t emptyGroup;
	int nMaxSortIDs;
	int groupOffset[ MAX_MAT_SORT_GROUPS ];
};

COMPILE_TIME_ASSERT( sizeof( EngineUtlVector_t ) == 20 );
COMPILE_TIME_ASSERT( sizeof( EngineSortGroup_t ) == 16 );
COMPILE_TIME_ASSERT( sizeof( EngineSortList_t ) == 176 );

// The opaque sort list is the first member of the engine's world list, after its vtables and
// reference count
#define WORLDLIST_SCAN 128
// Indices of a batch (INDEX_BUFFER_SIZE) and entries of its mesh list
#define ENGINE_BATCH_INDICES 32768
#define ENGINE_BATCH_MESHES 127

static int s_nSortListOffset = -1;				// -1 not looked for, -2 not found
static const EngineUtlVector_t *s_pWorldMeshes;	// g_WorldStaticMeshes
static int s_nWorldMeshesState;					// 0 not looked for, 1 found, -1 not found
static CUtlVector<uint64> s_FallbackKeys;		// by sort ID: vertex format rank, sort ID
static bool s_bWarned;

// stats of the last second
static int s_nStatLists, s_nStatGroups, s_nStatBefore, s_nStatAfter, s_nStatOver;
static float s_flStatTime;

static bool IsSortListHeader( const EngineSortList_t *p )
{
	if ( p->nMaxSortIDs <= 0 || p->nMaxSortIDs > 65536 )
		return false;
	for ( int g = 0; g < MAX_MAT_SORT_GROUPS; g++ )
	{
		if ( p->groupOffset[g] != g * p->nMaxSortIDs )
			return false;
	}
	if ( p->emptyGroup.listHead != -1 || p->emptyGroup.listTail != -1 || p->emptyGroup.groupListIndex != -1 )
		return false;
	if ( p->groups.pElements != p->groups.pMemory || p->groups.nSize < p->nMaxSortIDs * MAX_MAT_SORT_GROUPS ||
		 !RPMCrash_IsReadable( p->groups.pMemory, p->groups.nSize * sizeof( EngineSortGroup_t ) ) )
		return false;
	for ( int g = 0; g < MAX_MAT_SORT_GROUPS; g++ )
	{
		const EngineUtlVector_t &v = p->sortGroupLists[g];
		if ( v.nSize < 0 || v.nSize > p->nMaxSortIDs || v.pElements != v.pMemory ||
			 ( v.nSize && !RPMCrash_IsReadable( v.pMemory, v.nSize * sizeof( void * ) ) ) )
			return false;
	}
	return true;
}

static EngineSortList_t *GetSortList( IWorldRenderList *pList )
{
	unsigned char *pObject = (unsigned char *)pList;
	if ( !RPMCrash_IsReadable( pObject, WORLDLIST_SCAN + sizeof( EngineSortList_t ) ) )
		return NULL;
	if ( s_nSortListOffset == -1 )
	{
		s_nSortListOffset = -2;
		for ( int nOffset = 0; nOffset <= WORLDLIST_SCAN; nOffset += 4 )
		{
			if ( IsSortListHeader( (const EngineSortList_t *)( pObject + nOffset ) ) )
			{
				s_nSortListOffset = nOffset;
				DevMsg( "[worldlists] the engine's opaque sort list is at +%d of its world list\n", nOffset );
				break;
			}
		}
		if ( s_nSortListOffset < 0 )
			Warning( "[worldlists] the engine's world list was not recognized: its order is left as it is\n" );
	}
	if ( s_nSortListOffset < 0 )
		return NULL;
	EngineSortList_t *pSortList = (EngineSortList_t *)( pObject + s_nSortListOffset );
	return IsSortListHeader( pSortList ) ? pSortList : NULL;
}

// One of the engine's static meshes: an object made by the shader API or the material system
static bool IsMeshObject( const unsigned char *pMesh, const unsigned char *pApi, unsigned int nApi, const unsigned char *pMat, unsigned int nMat )
{
	if ( !RPMCrash_IsReadable( pMesh, sizeof( void * ) ) )
		return false;
	const unsigned char *pVTable = *(const unsigned char * const *)pMesh;
	return ( pVTable >= pApi && pVTable < pApi + nApi ) || ( pVTable >= pMat && pVTable < pMat + nMat );
}

// g_WorldStaticMeshes: CUtlVector<IMesh *> with an entry per sort ID - a few distinct meshes
static void FindWorldMeshes( int nSortIDs )
{
	s_nWorldMeshesState = -1;
	const unsigned char *pApi = NULL, *pMat = NULL;
	unsigned int nApi = 0, nMat = 0;
	RPMCrash_GetModuleRange( "shaderapidx9.dll", &pApi, &nApi );
	RPMCrash_GetModuleRange( "materialsystem.dll", &pMat, &nMat );

	const unsigned char *pStarts[64];
	unsigned int nSizes[64];
	const int nRegions = RPMCrash_GetModuleDataRegions( "engine.dll", pStarts, nSizes, ARRAYSIZE( pStarts ) );
	const EngineUtlVector_t *pFound = NULL;
	int nFound = 0, nMeshes = 0;
	for ( int r = 0; r < nRegions; r++ )
	{
		for ( unsigned int nOffset = 0; nOffset + sizeof( EngineUtlVector_t ) <= nSizes[r]; nOffset += 4 )
		{
			const EngineUtlVector_t *v = (const EngineUtlVector_t *)( pStarts[r] + nOffset );
			if ( v->nSize != nSortIDs || v->pMemory != v->pElements || !v->pMemory ||
				 v->nAllocationCount < nSortIDs || v->nAllocationCount > nSortIDs * 2 + 256 ||
				 !RPMCrash_IsReadable( v->pMemory, nSortIDs * sizeof( void * ) ) )
				continue;
			const unsigned char * const *pEntries = (const unsigned char * const *)v->pMemory;
			const unsigned char *pDistinct[256];
			int nDistinct = 0;
			bool bOk = true;
			for ( int i = 0; i < nSortIDs && bOk; i++ )
			{
				if ( !pEntries[i] )
					continue;
				int k = 0;
				while ( k < nDistinct && pDistinct[k] != pEntries[i] )
					k++;
				if ( k < nDistinct )
					continue;
				bOk = nDistinct < ARRAYSIZE( pDistinct ) && IsMeshObject( pEntries[i], pApi, nApi, pMat, nMat );
				pDistinct[ nDistinct++ ] = pEntries[i];
			}
			if ( bOk && nDistinct > 0 )
			{
				pFound = v;
				nFound++;
				nMeshes = nDistinct;
			}
		}
	}
	if ( nFound == 1 )
	{
		s_pWorldMeshes = pFound;
		s_nWorldMeshesState = 1;
		DevMsg( "[worldlists] the engine's world meshes: %d for %d sort IDs\n", nMeshes, nSortIDs );
	}
	else
	{
		DevMsg( "[worldlists] the engine's world meshes were not found (%d candidates): the lists are ordered by vertex format\n", nFound );
	}
}

// By sort ID: the rank of the vertex format of its material, then the sort ID
static void BuildFallbackKeys( int nSortIDs )
{
	s_FallbackKeys.SetCount( nSortIDs );
	CUtlVector<MaterialSystem_SortInfo_t> info;
	info.SetCount( nSortIDs );
	materials->GetSortInfo( info.Base() );
	CUtlVector<VertexFormat_t> formats;
	for ( int i = 0; i < nSortIDs; i++ )
	{
		VertexFormat_t format = info[i].material ? ( info[i].material->GetVertexFormat() & ~(VertexFormat_t)VERTEX_FORMAT_COMPRESSED ) : 0;
		int nRank = formats.Find( format );
		if ( nRank < 0 )
			nRank = formats.AddToTail( format );
		s_FallbackKeys[i] = ( (uint64)nRank << 32 ) | (uint64)i;
	}
}

struct GroupEntry_t
{
	uint64 nKey;
	int nOrder;
	EngineSortGroup_t *pGroup;
};

static int __cdecl CompareGroupEntries( const GroupEntry_t *a, const GroupEntry_t *b )
{
	if ( a->nKey != b->nKey )
		return a->nKey < b->nKey ? -1 : 1;
	return a->nOrder - b->nOrder;
}

// The most entries one batch of the engine's static draw gets for this order of groups
static int CountBatchMeshes( const GroupEntry_t *pEntries, int nEntries )
{
	int nMost = 0, nCount = 0, nIndices = 0;
	uint64 nLast = ~(uint64)0;
	for ( int i = 0; i < nEntries; i++ )
	{
		const GroupEntry_t &e = pEntries[i];
		const int nGroupIndices = e.pGroup->triangleCount * 3;
		if ( nIndices + nGroupIndices > ENGINE_BATCH_INDICES )
		{
			nIndices = 0;
			nCount = 0;
			nLast = ~(uint64)0;
		}
		nIndices += nGroupIndices;
		if ( e.nKey != nLast )
		{
			nCount++;
			nLast = e.nKey;
		}
		nMost = Max( nMost, nCount );
	}
	return nMost;
}

void DeferredWorldLists_SortByMesh( IWorldRenderList *pList )
{
	if ( !pList || !r_deferred_worldlists_sort.GetBool() )
		return;
	const int nSortIDs = materials->GetNumSortIDs();
	if ( nSortIDs <= 0 )
		return;
	EngineSortList_t *pSortList = GetSortList( pList );
	if ( !pSortList )
		return;

	if ( s_nWorldMeshesState == 0 )
		FindWorldMeshes( nSortIDs );
	const unsigned char * const *pMeshes = NULL;
	int nMeshes = 0;
	if ( s_nWorldMeshesState == 1 && s_pWorldMeshes->nSize > 0 && s_pWorldMeshes->pMemory == s_pWorldMeshes->pElements &&
		 RPMCrash_IsReadable( s_pWorldMeshes->pMemory, s_pWorldMeshes->nSize * sizeof( void * ) ) )
	{
		pMeshes = (const unsigned char * const *)s_pWorldMeshes->pMemory;
		nMeshes = s_pWorldMeshes->nSize;
	}
	else if ( s_FallbackKeys.Count() != nSortIDs )
	{
		BuildFallbackKeys( nSortIDs );
	}

	const bool bDebug = r_deferred_worldlists_debug.GetBool();
	EngineSortGroup_t *pGroups = (EngineSortGroup_t *)pSortList->groups.pMemory;
	CUtlVector<GroupEntry_t> entries;
	for ( int g = 0; g < MAX_MAT_SORT_GROUPS; g++ )
	{
		EngineUtlVector_t &v = pSortList->sortGroupLists[g];
		const int nCount = v.nSize;
		if ( nCount < 2 )
			continue;
		EngineSortGroup_t **ppList = (EngineSortGroup_t **)v.pMemory;

		// every pointer is checked before anything moves
		entries.SetCount( nCount );
		for ( int i = 0; i < nCount; i++ )
		{
			EngineSortGroup_t *pGroup = ppList[i];
			const int nIndex = (int)( pGroup - pGroups );
			const int nSortID = nIndex - pSortList->groupOffset[g];
			if ( pGroup < pGroups || ( (unsigned char *)pGroup - (unsigned char *)pGroups ) % sizeof( EngineSortGroup_t ) != 0 ||
				 nSortID < 0 || nSortID >= pSortList->nMaxSortIDs || nIndex >= pSortList->groups.nSize || pGroup->groupListIndex != i )
			{
				if ( !s_bWarned )
				{
					s_bWarned = true;
					Warning( "[worldlists] an unexpected group in the engine's world list: its order is left as it is\n" );
				}
				return;
			}
			GroupEntry_t &e = entries[i];
			e.nOrder = i;
			e.pGroup = pGroup;
			if ( pMeshes )
				e.nKey = nSortID < nMeshes ? (uint64)(uintp)pMeshes[ nSortID ] : ~(uint64)0;
			else
				e.nKey = nSortID < s_FallbackKeys.Count() ? s_FallbackKeys[ nSortID ] : ~(uint64)0;
		}

		if ( bDebug )
		{
			s_nStatGroups += nCount;
			const int nBefore = CountBatchMeshes( entries.Base(), nCount );
			s_nStatBefore = Max( s_nStatBefore, nBefore );
			s_nStatOver += nBefore > ENGINE_BATCH_MESHES ? 1 : 0;
		}

		entries.Sort( CompareGroupEntries );
		for ( int i = 0; i < nCount; i++ )
		{
			ppList[i] = entries[i].pGroup;
			entries[i].pGroup->groupListIndex = (short)i;
		}

		if ( bDebug )
			s_nStatAfter = Max( s_nStatAfter, CountBatchMeshes( entries.Base(), nCount ) );
	}

	if ( bDebug )
	{
		s_nStatLists++;
		if ( gpGlobals->realtime - s_flStatTime > 1.0f )
		{
			Msg( "[worldlists] %d lists, %d groups; most mesh changes of a batch %d -> %d (limit %d, over it: %d sort groups); meshes %s\n",
				s_nStatLists, s_nStatGroups, s_nStatBefore, s_nStatAfter, ENGINE_BATCH_MESHES, s_nStatOver,
				pMeshes ? "of the engine" : "by vertex format" );
			s_flStatTime = gpGlobals->realtime;
			s_nStatLists = s_nStatGroups = s_nStatBefore = s_nStatAfter = s_nStatOver = 0;
		}
	}
}

class CWorldListsLevelSystem : public CAutoGameSystem
{
public:
	CWorldListsLevelSystem() : CAutoGameSystem( "CWorldListsLevelSystem" ) {}
	virtual void LevelShutdownPostEntity()
	{
		s_FallbackKeys.Purge();
		// (the table of a map that had more sort IDs than it at first is looked for again)
		if ( s_nWorldMeshesState < 0 )
			s_nWorldMeshesState = 0;
	}
};
static CWorldListsLevelSystem s_WorldListsLevelSystem;

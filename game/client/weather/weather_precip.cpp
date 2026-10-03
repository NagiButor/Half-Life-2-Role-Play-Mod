//========= HL2RPM ============================================================//
//
// Purpose: Weather precipitation (see weather_precip.h).
//
//=============================================================================//

#include "cbase.h"
#include "weather/weather_precip.h"
#include "view.h"
#include "engine/IStaticPropMgr.h"
#include "particles/particles.h"
#include "tier0/threadtools.h"

#include "hl2rpm_crashdebug.h"
#include "tier0/memdbgon.h"

static ConVar cl_weather_precip( "cl_weather_precip", "1", FCVAR_ARCHIVE, "Particle rain / snow around the camera" );
static ConVar cl_weather_precip_density( "cl_weather_precip_density", "1.0", FCVAR_ARCHIVE, "Density of the weather particle precipitation" );
static ConVar r_precip_cull( "r_precip_cull", "1", 0, "Remove precipitation particles that spawn without open sky above them" );
static ConVar r_precip_cull_debug( "r_precip_cull_debug", "0", FCVAR_CHEAT, "Count culled precipitation particles" );

// ---------------------------------------------------------------------------
// Open sky cache
//
// Precipitation spawns thousands of particles per second in a small volume
// around the camera, so the "is there open sky above this point" answer is
// cached per cell. Particles are initialized on the particle worker threads.
// ---------------------------------------------------------------------------
#define SKYCACHE_CELL_XY	24.0f
#define SKYCACHE_CELL_Z		48.0f
#define SKYCACHE_SIZE		16384		// power of two
#define SKYCACHE_LIFETIME	10.0f		// seconds; doors and moving brushes are rare above rain
#define SKY_TRACE_LENGTH	16384.0f

struct SkyCacheEntry_t
{
	int x, y, z;
	float flTime;
	bool bOpen;
};

static SkyCacheEntry_t s_SkyCache[SKYCACHE_SIZE];
static CThreadFastMutex s_SkyCacheMutex;
static CInterlockedInt s_nSkyTraces;
static CInterlockedInt s_nCulled;
static CInterlockedInt s_nTested;
static CInterlockedInt s_nOpInstances;
static CInterlockedInt s_nOpCalls;

static void SkyCache_Clear()
{
	AUTO_LOCK_FM( s_SkyCacheMutex );
	for ( int i = 0; i < SKYCACHE_SIZE; i++ )
	{
		s_SkyCache[i].x = s_SkyCache[i].y = s_SkyCache[i].z = INT_MAX;
		s_SkyCache[i].flTime = -FLT_MAX;
		s_SkyCache[i].bOpen = true;
	}
}

// World, static props (awnings, bus stops) and solid brush entities stop rain
class CPrecipSkyTraceFilter : public CTraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		C_BaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( !pEntity )
			return true;	// static prop
		return pEntity->GetSolid() == SOLID_BSP && !pEntity->IsSolidFlagSet( FSOLID_NOT_SOLID | FSOLID_TRIGGER );
	}
	virtual TraceType_t GetTraceType() const
	{
		return TRACE_EVERYTHING;
	}
};

bool WeatherPrecip_TraceOpenSky( const Vector &vecStart )
{
	s_nSkyTraces++;

	// grates and fences let rain through, windows and solid geometry don't
	const int nMask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_MOVEABLE;

	Ray_t ray;
	ray.Init( vecStart, vecStart + Vector( 0, 0, SKY_TRACE_LENGTH ) );
	CPrecipSkyTraceFilter filter;
	trace_t tr;
	enginetrace->TraceRay( ray, nMask, &filter, &tr );

	if ( tr.startsolid )
		return false;
	return tr.fraction >= 1.0f || ( tr.surface.flags & SURF_SKY ) != 0;
}

bool WeatherPrecip_IsOpenSky( const Vector &vecPos )
{
	const int x = (int)floorf( vecPos.x / SKYCACHE_CELL_XY );
	const int y = (int)floorf( vecPos.y / SKYCACHE_CELL_XY );
	const int z = (int)floorf( vecPos.z / SKYCACHE_CELL_Z );
	const unsigned int h = ( (unsigned int)x * 73856093u ^ (unsigned int)y * 19349663u ^ (unsigned int)z * 83492791u ) & ( SKYCACHE_SIZE - 1 );
	const float flNow = gpGlobals->curtime;

	{
		AUTO_LOCK_FM( s_SkyCacheMutex );
		const SkyCacheEntry_t &e = s_SkyCache[h];
		if ( e.x == x && e.y == y && e.z == z && e.flTime <= flNow && flNow - e.flTime < SKYCACHE_LIFETIME )
			return e.bOpen;
	}

	// every particle of a cell gets the same answer: trace from the cell center
	const Vector vecCenter( ( x + 0.5f ) * SKYCACHE_CELL_XY, ( y + 0.5f ) * SKYCACHE_CELL_XY, ( z + 0.5f ) * SKYCACHE_CELL_Z );
	const bool bOpen = WeatherPrecip_TraceOpenSky( vecCenter );

	{
		AUTO_LOCK_FM( s_SkyCacheMutex );
		SkyCacheEntry_t &e = s_SkyCache[h];
		e.x = x;
		e.y = y;
		e.z = z;
		e.flTime = flNow;
		e.bOpen = bOpen;
	}
	return bOpen;
}

// ---------------------------------------------------------------------------
// "Cull relative to Ray Trace Environment"
//
// The precipitation systems carry a stack of these: "cull on miss" against the
// func_precipitation volumes, then tests against func_precipitation_blocker.
// Source 2013 has neither the operator nor the ray trace environments, so the
// volume test becomes "open sky above the spawn point" (the brush volume only
// placed the systems) and the blocker tests are no-ops (the client never
// filled the blocker environment anyway).
//
// Killing from an initializer is not safe in this particle library (the kill
// list must stay sorted), so a culled particle is made invisible and old:
// Lifespan Decay removes it on the next update. Initializers that run later
// (Lifetime from Time to Impact, Lifetime Random) only touch the lifespan.
// ---------------------------------------------------------------------------
class C_INIT_HL2RPM_PrecipCull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipCull );

public:
	C_INIT_HL2RPM_PrecipCull()
	{
		s_nOpInstances++;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	// The attributes written here were already set by the emitter and "Alpha Random":
	// without this the collection skips the initializer (it only runs initializers that
	// still have something left to initialize). Overriding initializers run in a second
	// pass, in list order, like "Lifetime from Time to Impact".
	virtual bool InitMultipleOverride()
	{
		return true;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
		int nParticleCount, int nAttributeWriteMask, void *pContext ) const;

	Vector m_vecTestDir;
	Vector m_vecTestNormal;
	bool m_bUseVelocity;
	bool m_bCullOnMiss;
	bool m_bLifeAdjust;
	char m_szRtEnvName[128];
};

DEFINE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipCull, "Cull relative to Ray Trace Environment", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipCull )
	DMXELEMENT_UNPACK_FIELD( "test direction", "0 0 1", Vector, m_vecTestDir )
	DMXELEMENT_UNPACK_FIELD( "cull normal", "0 0 0", Vector, m_vecTestNormal )
	DMXELEMENT_UNPACK_FIELD( "use velocity for test direction", "0", bool, m_bUseVelocity )
	DMXELEMENT_UNPACK_FIELD( "cull on miss", "0", bool, m_bCullOnMiss )
	DMXELEMENT_UNPACK_FIELD( "velocity test adjust lifespan", "0", bool, m_bLifeAdjust )
	DMXELEMENT_UNPACK_FIELD_STRING( "ray trace environment name", "PRECIPITATION", m_szRtEnvName )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipCull )

void C_INIT_HL2RPM_PrecipCull::InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	const bool bDebug = r_precip_cull_debug.GetBool();
	if ( bDebug )
		s_nOpCalls++;

	// only the volume test of the stack does anything, see above
	if ( !m_bCullOnMiss || !r_precip_cull.GetBool() )
		return;

	for ( ; nParticleCount--; start_p++ )
	{
		const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const Vector vecPos( pXYZ[0], pXYZ[4], pXYZ[8] );

		if ( bDebug )
			s_nTested++;

		if ( WeatherPrecip_IsOpenSky( vecPos ) )
			continue;

		float *pCreationTime = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		*pCreationTime = pParticles->m_flCurTime - 1000.0f;
		float *pAlpha = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_ALPHA, start_p );
		*pAlpha = 0.0f;

		if ( bDebug )
			s_nCulled++;
	}
}

// ---------------------------------------------------------------------------
// Roof map: height of the first surface a drop falling from the sky meets at
// (x, y) - roofs, awnings, bridges, func_brush/func_detail, static props, water.
// Cached per 16x16 cell for the camera's height band; filled from the particle
// worker threads and warmed up around the camera on the main thread.
//
// A column is probed from a point P 1200 u above the camera (above every
// spawn): up from P must reach the sky (else a roof covers the whole column),
// down from P the first surface is where the drops land. P inside a solid
// (hill, thick roof): the column is covered below P... unless the solid ends
// above P, then that end is the ground the rain falls on.
// ---------------------------------------------------------------------------
#define ROOF_CELL			16.0f
#define ROOF_CACHE_SIZE		16384		// 128 x 128 cells
#define ROOF_LOCKS			64			// power of two
#define ROOF_LIFETIME		8.0f		// seconds (doors and lifts move)
#define ROOF_PROBE_UP		1200.0f		// probe point above the camera
#define ROOF_BAND			400.0f		// camera height change that invalidates an entry
#define ROOF_TRACE_DOWN		6000.0f
#define ROOF_COVERED		1.0e9f		// no open sky in this column

struct RoofCacheEntry_t
{
	int x, y;
	float flTop;
	float flTime;
	float flRefZ;
};

static RoofCacheEntry_t s_RoofCache[ROOF_CACHE_SIZE];
static CThreadFastMutex s_RoofLocks[ROOF_LOCKS];
static CInterlockedInt s_nRoofTraces;
static float s_flRoofRefZ = 0.0f;	// camera height (main thread)

static void RoofCache_Clear()
{
	for ( int l = 0; l < ROOF_LOCKS; l++ )
		s_RoofLocks[l].Lock();
	for ( int i = 0; i < ROOF_CACHE_SIZE; i++ )
	{
		s_RoofCache[i].x = s_RoofCache[i].y = INT_MAX;
		s_RoofCache[i].flTime = -FLT_MAX;
		s_RoofCache[i].flTop = -FLT_MAX;
		s_RoofCache[i].flRefZ = 0.0f;
	}
	for ( int l = 0; l < ROOF_LOCKS; l++ )
		s_RoofLocks[l].Unlock();
}

static const int ROOF_MASK = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_MOVEABLE | CONTENTS_WATER | CONTENTS_SLIME;

static float RoofTrace( float x, float y, float flRefZ )
{
	s_nRoofTraces++;
	CPrecipSkyTraceFilter filter;
	const Vector vecProbe( x, y, flRefZ + ROOF_PROBE_UP );
	Ray_t ray;
	trace_t tr;

	// 1. sky above the probe?
	ray.Init( vecProbe, vecProbe + Vector( 0, 0, 16384.0f ) );
	enginetrace->TraceRay( ray, ROOF_MASK, &filter, &tr );
	if ( tr.startsolid )
	{
		// the probe is inside something: where does it end going up?
		if ( tr.fractionleftsolid <= 0.0f || tr.fractionleftsolid >= 1.0f )
			return ROOF_COVERED;
		const float flExit = vecProbe.z + 16384.0f * tr.fractionleftsolid;
		// open sky above that exit -> it is the ground the rain lands on
		ray.Init( Vector( x, y, flExit + 1.0f ), Vector( x, y, flExit + 16384.0f ) );
		trace_t tr2;
		enginetrace->TraceRay( ray, ROOF_MASK, &filter, &tr2 );
		if ( tr2.startsolid || ( tr2.fraction < 1.0f && !( tr2.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) ) )
			return ROOF_COVERED;
		return flExit;
	}
	if ( tr.fraction < 1.0f && !( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) )
		return ROOF_COVERED;

	// 2. the first surface below the probe
	ray.Init( vecProbe, vecProbe - Vector( 0, 0, ROOF_TRACE_DOWN ) );
	enginetrace->TraceRay( ray, ROOF_MASK, &filter, &tr );
	if ( tr.fraction >= 1.0f )
		return vecProbe.z - ROOF_TRACE_DOWN;
	return tr.endpos.z;
}

// height of the surface drops land on at (x, y); ROOF_COVERED if no rain reaches it
float WeatherPrecip_GetRoofHeight( float x, float y )
{
	const int cx = (int)floorf( x / ROOF_CELL );
	const int cy = (int)floorf( y / ROOF_CELL );
	// toroidal 128 x 128 grid (2048 u around the camera): neighbouring cells never collide
	const unsigned int h = ( ( (unsigned int)cx & 127u ) << 7 ) | ( (unsigned int)cy & 127u );
	const float flNow = gpGlobals->curtime;
	const float flRefZ = s_flRoofRefZ;
	CThreadFastMutex &lock = s_RoofLocks[h & ( ROOF_LOCKS - 1 )];

	// per-cell lifetime 0.75..1.25 x ROOF_LIFETIME: the cells don't all expire in the same frame
	const float flLifetime = ROOF_LIFETIME * ( 0.75f + ( ( h * 2654435761u ) >> 24 ) * ( 0.5f / 255.0f ) );

	lock.Lock();
	{
		const RoofCacheEntry_t &e = s_RoofCache[h];
		if ( e.x == cx && e.y == cy && e.flTime <= flNow && flNow - e.flTime < flLifetime && fabsf( e.flRefZ - flRefZ ) < ROOF_BAND )
		{
			const float flTop = e.flTop;
			lock.Unlock();
			return flTop;
		}
	}
	lock.Unlock();

	const float flTop = RoofTrace( ( cx + 0.5f ) * ROOF_CELL, ( cy + 0.5f ) * ROOF_CELL, flRefZ );

	lock.Lock();
	{
		RoofCacheEntry_t &e = s_RoofCache[h];
		e.x = cx;
		e.y = cy;
		e.flTop = flTop;
		e.flTime = flNow;
		e.flRefZ = flRefZ;
	}
	lock.Unlock();
	return flTop;
}

// main thread: remember the camera height band, fill a few cells around it per frame
static void RoofCache_Warm( const Vector &vecCenter, float flRadius, int nMaxTraces )
{
	if ( fabsf( vecCenter.z - s_flRoofRefZ ) > ROOF_BAND * 0.5f )
		s_flRoofRefZ = vecCenter.z;

	static int s_iRing = 0;
	const int nCells = (int)( flRadius / ROOF_CELL );
	const int nSide = nCells * 2 + 1;
	const int nTotal = nSide * nSide;
	const int cx0 = (int)floorf( vecCenter.x / ROOF_CELL ) - nCells;
	const int cy0 = (int)floorf( vecCenter.y / ROOF_CELL ) - nCells;
	const int nStart = s_nRoofTraces;
	for ( int i = 0; i < 400 && ( s_nRoofTraces - nStart ) < nMaxTraces; i++ )
	{
		s_iRing = ( s_iRing + 1 ) % nTotal;
		const int cx = cx0 + s_iRing % nSide;
		const int cy = cy0 + s_iRing / nSide;
		WeatherPrecip_GetRoofHeight( ( cx + 0.5f ) * ROOF_CELL, ( cy + 0.5f ) * ROOF_CELL );
	}
}

// ---------------------------------------------------------------------------
// Impact points of each precipitation collection, consumed by its splash
// children ("Position from Parent Cache" only knew 48 cached points, so the
// splashes always popped up at the same spots).
// ---------------------------------------------------------------------------
#define IMPACT_RING_SIZE	512
#define IMPACT_RINGS		32

struct ImpactRing_t
{
	const void *pOwner;
	float flLastUse;
	int nHead;
	int nCount;
	Vector vecPoints[IMPACT_RING_SIZE];
};

static ImpactRing_t s_ImpactRings[IMPACT_RINGS];
static CThreadFastMutex s_ImpactMutex;

static void ImpactRings_Clear()
{
	AUTO_LOCK_FM( s_ImpactMutex );
	for ( int i = 0; i < IMPACT_RINGS; i++ )
	{
		s_ImpactRings[i].pOwner = NULL;
		s_ImpactRings[i].nCount = 0;
		s_ImpactRings[i].nHead = 0;
		s_ImpactRings[i].flLastUse = -FLT_MAX;
	}
}

static ImpactRing_t *ImpactRing_Find( const void *pOwner, bool bCreate )
{
	int iFree = -1;
	int iOldest = 0;
	for ( int i = 0; i < IMPACT_RINGS; i++ )
	{
		if ( s_ImpactRings[i].pOwner == pOwner )
			return &s_ImpactRings[i];
		if ( !s_ImpactRings[i].pOwner && iFree < 0 )
			iFree = i;
		if ( s_ImpactRings[i].flLastUse < s_ImpactRings[iOldest].flLastUse )
			iOldest = i;
	}
	if ( !bCreate )
		return NULL;
	ImpactRing_t &r = s_ImpactRings[iFree >= 0 ? iFree : iOldest];
	r.pOwner = pOwner;
	r.nHead = 0;
	r.nCount = 0;
	return &r;
}

static void Impact_Push( const void *pOwner, const Vector &vecPos )
{
	AUTO_LOCK_FM( s_ImpactMutex );
	ImpactRing_t *r = ImpactRing_Find( pOwner, true );
	r->flLastUse = gpGlobals->curtime;
	const int iSlot = ( r->nHead + r->nCount ) % IMPACT_RING_SIZE;
	r->vecPoints[iSlot] = vecPos;
	if ( r->nCount < IMPACT_RING_SIZE )
		r->nCount++;
	else
		r->nHead = ( r->nHead + 1 ) % IMPACT_RING_SIZE;	// full: drop the oldest
}

static bool Impact_Pop( const void *pOwner, Vector &vecPos )
{
	AUTO_LOCK_FM( s_ImpactMutex );
	ImpactRing_t *r = ImpactRing_Find( pOwner, false );
	if ( !r || r->nCount <= 0 )
		return false;
	vecPos = r->vecPoints[r->nHead];
	r->nHead = ( r->nHead + 1 ) % IMPACT_RING_SIZE;
	r->nCount--;
	r->flLastUse = gpGlobals->curtime;
	return true;
}

// ---------------------------------------------------------------------------
// Random numbers for the HL2RPM operators. The particle library's own
// RandomFloat walks a 4096 entry table, so with a steady emitter the same
// positions came back every second or so (rain falling on the same spots).
// ---------------------------------------------------------------------------
static CInterlockedInt s_nPrecipRandomCounter;

static inline uint32 PrecipHash( uint32 x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static inline uint32 PrecipSeed()
{
	return PrecipHash( (uint32)( s_nPrecipRandomCounter++ ) * 0x9E3779B9u ^ (uint32)( gpGlobals->realtime * 1000.0f ) );
}

static inline float PrecipRand( uint32 &state )
{
	state = PrecipHash( state + 0x9E3779B9u );
	return ( state >> 8 ) * ( 1.0f / 16777216.0f );
}

static void PrecipKill( CParticleCollection *pParticles, int i )
{
	float *pCreationTime = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
	*pCreationTime = pParticles->m_flCurTime - 1000.0f;
	float *pAlpha = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_ALPHA, i );
	*pAlpha = 0.0f;
}

// ---------------------------------------------------------------------------
// "HL2RPM Precip Spawn": drops/flakes in a cylinder around the control point
// (the camera), falling along the control point's -up axis (tilted by the
// wind). A drop spawned at height h is moved upwind so that it crosses eye
// level inside the cylinder: looking up, the rain comes from everywhere.
// ---------------------------------------------------------------------------
class C_INIT_HL2RPM_PrecipSpawn : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipSpawn );

public:
	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}
	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}
	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPoint;
	}
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
		int nParticleCount, int nAttributeWriteMask, void *pContext ) const;

	int m_nControlPoint;
	float m_flRadiusMin;
	float m_flRadiusMax;
	float m_flHeightMin;
	float m_flHeightMax;
	float m_flSpeedMin;
	float m_flSpeedMax;
	float m_flCenterBias;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipSpawn, "HL2RPM Precip Spawn", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipSpawn )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "1", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "radius_min", "0", float, m_flRadiusMin )
	DMXELEMENT_UNPACK_FIELD( "radius_max", "400", float, m_flRadiusMax )
	DMXELEMENT_UNPACK_FIELD( "height_min", "-40", float, m_flHeightMin )
	DMXELEMENT_UNPACK_FIELD( "height_max", "500", float, m_flHeightMax )
	DMXELEMENT_UNPACK_FIELD( "speed_min", "760", float, m_flSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "speed_max", "840", float, m_flSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "center_bias", "1", float, m_flCenterBias )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipSpawn )

void C_INIT_HL2RPM_PrecipSpawn::InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	const Vector vecCenter = pParticles->GetControlPointAtCurrentTime( m_nControlPoint );
	Vector vecFwd, vecRight, vecUp;
	pParticles->GetControlPointOrientationAtCurrentTime( m_nControlPoint, &vecFwd, &vecRight, &vecUp );
	Vector vecFall = -vecUp;
	if ( vecFall.z > -0.2f || vecFall.LengthSqr() < 0.5f )
		vecFall.Init( 0, 0, -1 );
	VectorNormalize( vecFall );
	const float flInvFallZ = 1.0f / -vecFall.z;
	// center_bias 1 = uniform over the disc, 2 = uniform in radius (denser at the camera)
	const float flRadiusExp = 0.5f * Max( 0.25f, m_flCenterBias );
	const float flDt = Max( pParticles->m_flPreviousDt, 1.0f / 1000.0f );

	uint32 st = PrecipSeed();
	for ( ; nParticleCount--; start_p++ )
	{
		const float flR = m_flRadiusMin + ( m_flRadiusMax - m_flRadiusMin ) * powf( PrecipRand( st ), flRadiusExp );
		float s, c;
		SinCos( PrecipRand( st ) * 2.0f * M_PI_F, &s, &c );
		const float flHeight = m_flHeightMin + ( m_flHeightMax - m_flHeightMin ) * PrecipRand( st );
		const float flSpeed = m_flSpeedMin + ( m_flSpeedMax - m_flSpeedMin ) * PrecipRand( st );

		// upwind: after falling flHeight the drop is over the chosen disc point
		const float flTravel = flHeight * flInvFallZ;
		Vector vecPos( vecCenter.x + c * flR - vecFall.x * flTravel,
			vecCenter.y + s * flR - vecFall.y * flTravel,
			vecCenter.z + flHeight );
		const Vector vecVel = vecFall * flSpeed;

		float *pXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pPrev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		pXYZ[0] = vecPos.x;
		pXYZ[4] = vecPos.y;
		pXYZ[8] = vecPos.z;
		pPrev[0] = vecPos.x - vecVel.x * flDt;
		pPrev[4] = vecPos.y - vecVel.y * flDt;
		pPrev[8] = vecPos.z - vecVel.z * flDt;
	}
}

// ---------------------------------------------------------------------------
// "HL2RPM Precip Cover": uses the roof map. Removes drops born under a roof,
// ends every drop's life where its path meets a surface and records that
// point for the splash children. Runs after the velocity initializers.
// ---------------------------------------------------------------------------
class C_INIT_HL2RPM_PrecipCover : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipCover );

public:
	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}
	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}
	virtual bool InitMultipleOverride()
	{
		return true;	// runs in the second pass, after position/velocity/lifetime
	}
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
		int nParticleCount, int nAttributeWriteMask, void *pContext ) const;

	bool m_bCullCovered;
	bool m_bLifeToImpact;
	bool m_bRecordImpacts;
	float m_flStep;
	Vector m_vecAcceleration;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipCover, "HL2RPM Precip Cover", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipCover )
	DMXELEMENT_UNPACK_FIELD( "cull covered spawn", "1", bool, m_bCullCovered )
	DMXELEMENT_UNPACK_FIELD( "lifetime to impact", "1", bool, m_bLifeToImpact )
	DMXELEMENT_UNPACK_FIELD( "record impacts", "1", bool, m_bRecordImpacts )
	DMXELEMENT_UNPACK_FIELD( "step", "16", float, m_flStep )
	DMXELEMENT_UNPACK_FIELD( "acceleration", "0 0 0", Vector, m_vecAcceleration )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipCover )

void C_INIT_HL2RPM_PrecipCover::InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( !r_precip_cull.GetBool() )
		return;

	const bool bDebug = r_precip_cull_debug.GetBool();
	const float flDt = Max( pParticles->m_flPreviousDt, 1.0f / 1000.0f );

	for ( ; nParticleCount--; start_p++ )
	{
		const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *pPrev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		const Vector vecPos( pXYZ[0], pXYZ[4], pXYZ[8] );
		const Vector vecVel( ( pXYZ[0] - pPrev[0] ) / flDt, ( pXYZ[4] - pPrev[4] ) / flDt, ( pXYZ[8] - pPrev[8] ) / flDt );

		if ( bDebug )
			s_nTested++;

		float flTop = WeatherPrecip_GetRoofHeight( vecPos.x, vecPos.y );
		if ( m_bCullCovered && vecPos.z < flTop - 2.0f )
		{
			PrecipKill( pParticles, start_p );
			if ( bDebug )
				s_nCulled++;
			continue;
		}

		if ( !m_bLifeToImpact )
			continue;

		// march along the path until it meets the roof map
		float *pLife = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
		const float flLife = *pLife;
		const float flSpeed = Max( vecVel.Length(), 1.0f );
		const float flStepT = clamp( Max( m_flStep, 2.0f ) / flSpeed, 0.002f, 0.25f );
		float t = 0.0f;
		float flPrevGap = vecPos.z - flTop;
		int iLastCellX = INT_MAX, iLastCellY = INT_MAX;
		while ( t < flLife )
		{
			const float t2 = Min( t + flStepT, flLife );
			const Vector p = vecPos + vecVel * t2 + m_vecAcceleration * ( 0.5f * t2 * t2 );
			const int cx = (int)floorf( p.x / ROOF_CELL );
			const int cy = (int)floorf( p.y / ROOF_CELL );
			if ( cx != iLastCellX || cy != iLastCellY )
			{
				flTop = WeatherPrecip_GetRoofHeight( p.x, p.y );
				iLastCellX = cx;
				iLastCellY = cy;
			}
			const float flGap = p.z - flTop;
			if ( flGap <= 0.0f )
			{
				// between t (above) and t2 (below)
				const float f = ( flPrevGap > 0.0f ) ? clamp( flPrevGap / ( flPrevGap - flGap ), 0.0f, 1.0f ) : 0.0f;
				const float tHit = t + ( t2 - t ) * f;
				*pLife = Max( tHit, 0.01f );
				if ( m_bRecordImpacts && flTop < ROOF_COVERED * 0.5f )
				{
					const Vector vecHit = vecPos + vecVel * tHit + m_vecAcceleration * ( 0.5f * tHit * tHit );
					Impact_Push( pParticles, Vector( vecHit.x, vecHit.y, flTop + 1.0f ) );
				}
				break;
			}
			flPrevGap = flGap;
			t = t2;
		}
	}
}

// ---------------------------------------------------------------------------
// "HL2RPM Precip Splash Position": splash children spawn where the parent's
// drops really hit (replaces "Position from Parent Cache"); without a recorded
// impact the splash is dropped.
// ---------------------------------------------------------------------------
class C_INIT_HL2RPM_PrecipSplashPosition : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipSplashPosition );

public:
	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}
	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}
	virtual bool InitMultipleOverride()
	{
		return true;
	}
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
		int nParticleCount, int nAttributeWriteMask, void *pContext ) const;

	float m_flOffset;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_HL2RPM_PrecipSplashPosition, "HL2RPM Precip Splash Position", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipSplashPosition )
	DMXELEMENT_UNPACK_FIELD( "offset radius", "4", float, m_flOffset )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_HL2RPM_PrecipSplashPosition )

void C_INIT_HL2RPM_PrecipSplashPosition::InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	const void *pOwner = pParticles->m_pParent;
	// control point 2 is the eye (CWeatherPrecipitation::Update)
	const Vector vecEye = pParticles->GetControlPointAtCurrentTime( 2 );
	uint32 st = PrecipSeed();
	for ( ; nParticleCount--; start_p++ )
	{
		Vector vecHit;
		if ( !pOwner || !Impact_Pop( pOwner, vecHit ) )
		{
			PrecipKill( pParticles, start_p );
			continue;
		}

		// no splashes on surfaces above the eye: the top of a roof can't be seen from below,
		// and a splash sprite turned toward a camera underneath reached through a thin roof
		// into the room below it
		if ( vecHit.z > vecEye.z + 2.0f )
		{
			PrecipKill( pParticles, start_p );
			continue;
		}
		float *pXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pPrev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		// keep the velocity the velocity initializers gave it
		const float vx = pXYZ[0] - pPrev[0], vy = pXYZ[4] - pPrev[4], vz = pXYZ[8] - pPrev[8];
		const float ox = ( PrecipRand( st ) * 2.0f - 1.0f ) * m_flOffset;
		const float oy = ( PrecipRand( st ) * 2.0f - 1.0f ) * m_flOffset;
		pXYZ[0] = vecHit.x + ox;
		pXYZ[4] = vecHit.y + oy;
		pXYZ[8] = vecHit.z;
		pPrev[0] = pXYZ[0] - vx;
		pPrev[4] = pXYZ[4] - vy;
		pPrev[8] = pXYZ[8] - vz;
	}
}

void WeatherPrecip_AddParticleOperators()
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_HL2RPM_PrecipCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_HL2RPM_PrecipSpawn );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_HL2RPM_PrecipCover );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_HL2RPM_PrecipSplashPosition );
	Msg( "Registered HL2RPM precipitation particle initializers\n" );
	SkyCache_Clear();
	RoofCache_Clear();
	ImpactRings_Clear();
}

// ---------------------------------------------------------------------------
// Precipitation layers: the same systems and placement as func_precipitation
// (c_effects.cpp), control point 1 above the camera, 2 = camera entity (the
// systems only render for it, and tint from the light there), 3 = density.
// ---------------------------------------------------------------------------
enum PrecipKind_e
{
	PRECIP_KIND_RAIN = 0,
	PRECIP_KIND_STORM,
	PRECIP_KIND_SNOW,
	PRECIP_KIND_ASH,
};

struct PrecipLayer_t
{
	const char *pszSystem;
	int iKind;
	float flForward;	// emitter center ahead of the camera
	bool bOuter;		// ring around the camera: uses the outer exposure
};

static const PrecipLayer_t s_PrecipLayers[] =
{
	// HL2RPM systems (particles/hl2rpm_weather.pcf, built from rain_fx.pcf by
	// hl2rpm_extras/tools/make_weather_pcf.py): one cylinder around the camera each,
	// spawn/cover/splash by the HL2RPM initializers above
	{ "hl2rpm_rain",		PRECIP_KIND_RAIN,	0.0f,	false },
	{ "hl2rpm_rain_storm",	PRECIP_KIND_STORM,	0.0f,	false },
	{ "hl2rpm_snow",		PRECIP_KIND_SNOW,	0.0f,	false },
	{ "hl2rpm_ash",			PRECIP_KIND_ASH,	0.0f,	false },
};

#define PRECIP_PCF "particles/hl2rpm_weather.pcf"

#define PRECIP_HEIGHT 180.0f

CWeatherPrecipitation::CWeatherPrecipitation()
{
	COMPILE_TIME_ASSERT( ARRAYSIZE( s_PrecipLayers ) <= MAX_PRECIP_LAYERS );
	for ( int i = 0; i < MAX_PRECIP_LAYERS; i++ )
		m_flDensity[i] = 0.0f;
	m_bPrecached = false;
}

void CWeatherPrecipitation::LevelInit()
{
	SkyCache_Clear();
	RoofCache_Clear();
	ImpactRings_Clear();
	m_bPrecached = false;

	// our systems are not in the particle manifest
	static bool s_bLoaded = false;
	if ( !s_bLoaded )
	{
		s_bLoaded = true;
		if ( !g_pParticleSystemMgr->ReadParticleConfigFile( PRECIP_PCF, true, false ) )
			Warning( "Weather: can't read %s\n", PRECIP_PCF );
	}
	for ( int i = 0; i < MAX_PRECIP_LAYERS; i++ )
		m_flDensity[i] = 0.0f;
}

void CWeatherPrecipitation::LevelShutdown()
{
	// the particle manager deletes every effect on level change: never hold one past it
	for ( int i = 0; i < MAX_PRECIP_LAYERS; i++ )
	{
		m_pLayers[i] = NULL;
		m_flDensity[i] = 0.0f;
	}
}

void CWeatherPrecipitation::StopLayer( int iLayer )
{
	if ( m_pLayers[iLayer].IsValid() )
	{
		// the drops in the air finish their fall, then the effect deletes itself
		m_pLayers[iLayer]->StopEmission();
		m_pLayers[iLayer] = NULL;
	}
}

void CWeatherPrecipitation::Update( bool bActive, const WeatherPrecipInput_t &in )
{
	const int nLayers = ARRAYSIZE( s_PrecipLayers );

	if ( !bActive || !cl_weather_precip.GetBool() )
	{
		for ( int i = 0; i < nLayers; i++ )
		{
			StopLayer( i );
			m_flDensity[i] = 0.0f;
		}
		return;
	}

	C_BaseEntity *pViewEntity = cl_entitylist->GetEnt( render->GetViewEntity() );
	if ( !pViewEntity )
		pViewEntity = C_BasePlayer::GetLocalPlayer();
	if ( !pViewEntity )
		return;

	const float dt = engine->IsPaused() ? 0.0f : Clamp( gpGlobals->frametime, 0.0f, 0.1f );

	// light rain uses "Particle Rain", a downpour turns into "Particle Rainstorm"
	const float flStorm = clamp( ( in.flRain - 0.8f ) / 0.2f, 0.0f, 1.0f );
	float flKind[4];
	flKind[PRECIP_KIND_RAIN] = Min( in.flRain / 0.75f, 1.0f ) * ( 1.0f - flStorm * flStorm * ( 3.0f - 2.0f * flStorm ) );
	flKind[PRECIP_KIND_STORM] = flStorm * flStorm * ( 3.0f - 2.0f * flStorm );
	flKind[PRECIP_KIND_SNOW] = Clamp( in.flSnow, 0.0f, 1.0f );
	flKind[PRECIP_KIND_ASH] = Clamp( in.flAsh, 0.0f, 1.0f );

	const float flScale = Max( cl_weather_precip_density.GetFloat(), 0.0f );

	// camera
	const Vector vecEye = MainViewOrigin();
	Vector vecForward = MainViewForward();
	vecForward.z = 0.0f;
	if ( vecForward.NormalizeInPlace() < 0.01f )
		vecForward.Init( 1, 0, 0 );

	// wind: tilt the emitters so the drops fall along the wind
	Vector vecUp( -in.vecSlant.x, -in.vecSlant.y, 1.0f );
	VectorNormalize( vecUp );
	Vector vecCPForward = CrossProduct( vecUp, Vector( 0, 1, 0 ) );
	if ( vecCPForward.NormalizeInPlace() < 0.01f )
		vecCPForward.Init( 1, 0, 0 );
	const Vector vecCPRight = CrossProduct( vecCPForward, vecUp );

	for ( int i = 0; i < nLayers; i++ )
	{
		const PrecipLayer_t &layer = s_PrecipLayers[i];

		// the drops are culled per particle under roofs: indoors next to a window the
		// rain outside must still be there, only a fully enclosed place stops emitting
		const float flExposure = Max( in.flInnerExposure, in.flOuterExposure );
		// partly covered is still full rain: the particles themselves are culled under roofs
		const float flTarget = flKind[layer.iKind] * flScale * Clamp( flExposure * 2.0f, 0.0f, 1.0f );
		m_flDensity[i] = Approach( flTarget, m_flDensity[i], dt * 0.75f );

		if ( m_flDensity[i] < 0.01f )
		{
			StopLayer( i );
			continue;
		}

		if ( !m_pLayers[i].IsValid() )
		{
			if ( !m_bPrecached )
			{
				for ( int j = 0; j < nLayers; j++ )
					PrecacheParticleSystem( s_PrecipLayers[j].pszSystem );
				m_bPrecached = true;
			}

			RPM_CRUMB( "weather: create precipitation layer %s", layer.pszSystem );
			m_pLayers[i] = CNewParticleEffect::Create( NULL, layer.pszSystem, "weather_precip" );
			if ( !m_pLayers[i].IsValid() )
				continue;

			if ( !m_pLayers[i]->IsValid() )
			{
				ParticleMgr()->RemoveEffect( m_pLayers[i].GetObject() );
				m_pLayers[i] = NULL;
				continue;
			}

			// screen-space systems ("rain_storm_screen") are placed relative to control point 0
			m_pLayers[i]->SetControlPoint( 0, vec3_origin );
		}

		CNewParticleEffect *pEffect = m_pLayers[i].GetObject();
		// the HL2RPM spawn initializer builds its cylinder around control point 1 (the eye)
		const Vector vecEmitter = vecEye + vecForward * layer.flForward;

		pEffect->SetSortOrigin( vecEye );
		pEffect->SetControlPoint( 1, vecEmitter );
		pEffect->SetControlPointOrientation( 1, vecCPForward, vecCPRight, vecUp );
		pEffect->SetControlPointEntity( 2, pViewEntity );
		pEffect->SetControlPoint( 2, vecEye );
		pEffect->SetControlPoint( 3, Vector( m_flDensity[i], 0, 0 ) );
	}

	// fill the roof map around the camera on the main thread (a few traces per frame)
	bool bAnyLayer = false;
	for ( int i = 0; i < nLayers; i++ )
		bAnyLayer = bAnyLayer || m_pLayers[i].IsValid();
	if ( bAnyLayer )
		RoofCache_Warm( vecEye, 520.0f, 40 );

	if ( r_precip_cull_debug.GetBool() )
	{
		static float s_flNextReport = 0.0f;
		if ( gpGlobals->realtime >= s_flNextReport )
		{
			s_flNextReport = gpGlobals->realtime + 1.0f;
			Msg( "[precip] ops %d calls %d tested %d culled %d traces sky %d roof %d | rain %.2f storm %.2f snow %.2f ash %.2f | exposure in %.2f out %.2f\n",
				(int)s_nOpInstances, (int)s_nOpCalls, (int)s_nTested, (int)s_nCulled, (int)s_nSkyTraces, (int)s_nRoofTraces,
				flKind[PRECIP_KIND_RAIN], flKind[PRECIP_KIND_STORM], flKind[PRECIP_KIND_SNOW], flKind[PRECIP_KIND_ASH],
				in.flInnerExposure, in.flOuterExposure );
			s_nTested = 0;
			s_nOpCalls = 0;
			s_nCulled = 0;
			s_nSkyTraces = 0;
			s_nRoofTraces = 0;
		}
	}
}

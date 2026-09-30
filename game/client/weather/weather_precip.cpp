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

void WeatherPrecip_AddParticleOperators()
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_HL2RPM_PrecipCull );
	Msg( "Registered particle initializer \"Cull relative to Ray Trace Environment\"\n" );
	SkyCache_Clear();
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
	{ "rain",				PRECIP_KIND_RAIN,	32.0f,	false },
	{ "rain",				PRECIP_KIND_RAIN,	180.0f,	false },
	{ "rain_outer",			PRECIP_KIND_RAIN,	0.0f,	true },
	// "rain_storm_screen" (func_precipitation's second storm layer) is left out: it is a
	// screen-space effect that this particle library draws in world space near the origin
	{ "rain_storm",			PRECIP_KIND_STORM,	32.0f,	false },
	{ "rain_storm",			PRECIP_KIND_STORM,	150.0f,	false },
	{ "rain_storm_outer",	PRECIP_KIND_STORM,	0.0f,	true },
	{ "snow",				PRECIP_KIND_SNOW,	32.0f,	false },
	{ "snow",				PRECIP_KIND_SNOW,	280.0f,	false },
	{ "snow_outer",			PRECIP_KIND_SNOW,	0.0f,	true },
	{ "ash",				PRECIP_KIND_ASH,	32.0f,	false },
	{ "ash",				PRECIP_KIND_ASH,	280.0f,	false },
	{ "ash_outer",			PRECIP_KIND_ASH,	0.0f,	true },
};

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
	m_bPrecached = false;
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
	const Vector vecCenter = vecEye + Vector( 0, 0, PRECIP_HEIGHT );

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

		const float flExposure = layer.bOuter ? in.flOuterExposure : in.flInnerExposure;
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
		const Vector vecEmitter = vecCenter + vecForward * layer.flForward;

		pEffect->SetSortOrigin( vecEye );
		pEffect->SetControlPoint( 1, vecEmitter );
		pEffect->SetControlPointOrientation( 1, vecCPForward, vecCPRight, vecUp );
		pEffect->SetControlPointEntity( 2, pViewEntity );
		pEffect->SetControlPoint( 2, vecEye );
		pEffect->SetControlPoint( 3, Vector( m_flDensity[i], 0, 0 ) );
	}

	if ( r_precip_cull_debug.GetBool() )
	{
		static float s_flNextReport = 0.0f;
		if ( gpGlobals->realtime >= s_flNextReport )
		{
			s_flNextReport = gpGlobals->realtime + 1.0f;
			Msg( "[precip] ops %d calls %d tested %d culled %d traces %d | rain %.2f storm %.2f snow %.2f ash %.2f | exposure in %.2f out %.2f\n",
				(int)s_nOpInstances, (int)s_nOpCalls, (int)s_nTested, (int)s_nCulled, (int)s_nSkyTraces,
				flKind[PRECIP_KIND_RAIN], flKind[PRECIP_KIND_STORM], flKind[PRECIP_KIND_SNOW], flKind[PRECIP_KIND_ASH],
				in.flInnerExposure, in.flOuterExposure );
			s_nTested = 0;
			s_nOpCalls = 0;
			s_nCulled = 0;
			s_nSkyTraces = 0;
		}
	}
}

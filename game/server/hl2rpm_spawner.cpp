//========= HL2RPM ============================================================//
//
// rpm_spawner: a spawn point of the Creation Kit (encounter). While the player is
// near and the time / conditions fit, keeps a number of NPCs alive around itself,
// picked from a weighted list of NPC definitions (or plain classnames); after they
// are dead it refills after the given game hours. New NPCs appear out of the
// player's sight.
//
// keys: npcs "def_a:3;npc_zombie:1", count_min, count_max, radius, activate_dist,
//       from_hour, to_hour (-1 = always), respawn_hours (0 = never), require_flag
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_world.h"
#include "hl2rpm_rpg.h"
#include "ai_basenpc.h"
#include "player.h"
#include "saverestore_utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CRPMSpawner : public CLogicalEntity
{
public:
	DECLARE_CLASS( CRPMSpawner, CLogicalEntity );
	DECLARE_DATADESC();

	CRPMSpawner()
	{
		m_iCountMin = 1;
		m_iCountMax = 3;
		m_flRadius = 384.0f;
		m_flActivateDist = 2500.0f;
		m_flFromHour = m_flToHour = -1.0f;
		m_flRespawnHours = 24.0f;
		m_iTarget = -1;
		m_flEmptySince = -1.0f;
		m_bEnabled = true;
	}

	void Spawn()
	{
		BaseClass::Spawn();
		SetThink( &CRPMSpawner::SpawnerThink );
		SetNextThink( gpGlobals->curtime + RandomFloat( 0.5f, 1.5f ) );
	}

	void InputEnable( inputdata_t & ) { m_bEnabled = true; }
	void InputDisable( inputdata_t & ) { m_bEnabled = false; }

	int Alive()
	{
		int n = 0;
		for ( int i = m_hNPCs.Count() - 1; i >= 0; i-- )
		{
			CBaseEntity *p = m_hNPCs[i].Get();
			if ( !p || !p->IsAlive() )
				m_hNPCs.Remove( i );
			else
				n++;
		}
		return n;
	}

	bool TimeFits()
	{
		if ( m_flFromHour < 0.0f || m_flToHour < 0.0f )
			return true;
		float h = RPMWorld::GetHours();
		if ( m_flFromHour <= m_flToHour )
			return h >= m_flFromHour && h < m_flToHour;
		return h >= m_flFromHour || h < m_flToHour;	// over midnight
	}

	const char *PickNPC()
	{
		CUtlStringList parts;
		V_SplitString( STRING( m_iszNPCs ), ";", parts );
		int total = 0;
		CUtlVector<int> w;
		for ( int i = 0; i < parts.Count(); i++ )
		{
			const char *c = Q_strstr( parts[i], ":" );
			int wt = c ? Max( 1, atoi( c + 1 ) ) : 1;
			w.AddToTail( wt );
			total += wt;
		}
		if ( !total )
			return NULL;
		int r = RandomInt( 1, total );
		for ( int i = 0; i < parts.Count(); i++ )
		{
			r -= w[i];
			if ( r <= 0 )
			{
				static char buf[128];
				Q_strncpy( buf, parts[i], sizeof( buf ) );
				char *c = Q_strstr( buf, ":" );
				if ( c )
					*c = 0;
				return buf;
			}
		}
		return NULL;
	}

	// a spot on the ground near the spawner, a hull fits, the player doesn't see it
	bool FindSpot( CBasePlayer *pPlayer, Vector &out )
	{
		for ( int attempt = 0; attempt < 12; attempt++ )
		{
			float a = RandomFloat( 0, 2 * M_PI ), d = RandomFloat( 0, m_flRadius );
			Vector start = GetAbsOrigin() + Vector( cos( a ) * d, sin( a ) * d, 128.0f );
			trace_t tr;
			UTIL_TraceLine( start, start - Vector( 0, 0, 1024 ), MASK_NPCSOLID, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 1.0f || tr.startsolid || tr.plane.normal.z < 0.7f )
				continue;
			Vector spot = tr.endpos + Vector( 0, 0, 2 );
			UTIL_TraceHull( spot, spot + Vector( 0, 0, 1 ), Vector( -16, -16, 0 ), Vector( 16, 16, 72 ), MASK_NPCSOLID, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.startsolid || tr.allsolid )
				continue;
			if ( pPlayer )
			{
				if ( ( spot - pPlayer->GetAbsOrigin() ).Length() < 400.0f )
					continue;
				trace_t vis;
				UTIL_TraceLine( pPlayer->EyePosition(), spot + Vector( 0, 0, 48 ), MASK_BLOCKLOS, pPlayer, COLLISION_GROUP_NONE, &vis );
				Vector toSpot = spot - pPlayer->EyePosition();
				VectorNormalize( toSpot );
				Vector fwd;
				AngleVectors( pPlayer->EyeAngles(), &fwd );
				if ( vis.fraction >= 0.99f && DotProduct( fwd, toSpot ) > 0.3f )
					continue;		// in plain sight
			}
			out = spot;
			return true;
		}
		return false;
	}

	void SpawnerThink()
	{
		SetNextThink( gpGlobals->curtime + 1.0f );
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !m_bEnabled || !pPlayer || !TimeFits() )
			return;
		if ( m_iszFlag != NULL_STRING && !RPMState::GetFlag( STRING( m_iszFlag ) ) )
			return;
		if ( ( pPlayer->GetAbsOrigin() - GetAbsOrigin() ).Length() > m_flActivateDist )
			return;
		int alive = Alive();
		double now = RPMState::GameHours();
		if ( m_iTarget < 0 )
			m_iTarget = RandomInt( m_iCountMin, Max( m_iCountMin, m_iCountMax ) );
		if ( alive == 0 && m_iSpawned > 0 )
		{
			// everyone is dead: refill later (or never)
			if ( m_flEmptySince < 0.0f )
				m_flEmptySince = (float)now;
			if ( m_flRespawnHours <= 0.0f || now - m_flEmptySince < m_flRespawnHours )
				return;
			m_iSpawned = 0;
			m_flEmptySince = -1.0f;
			m_iTarget = RandomInt( m_iCountMin, Max( m_iCountMin, m_iCountMax ) );
		}
		if ( m_iSpawned >= m_iTarget )
			return;
		const char *pszNPC = PickNPC();
		Vector spot;
		if ( !pszNPC || !FindSpot( pPlayer, spot ) )
			return;
		QAngle ang( 0, RandomFloat( 0, 360 ), 0 );
		CBaseEntity *pNPC = RPMWorld::SpawnNPC( pszNPC, spot, ang, CFmtStr( "spawner:%s", GetEntityName().ToCStr() ) );
		if ( !pNPC )
		{
			pNPC = CreateEntityByName( pszNPC );
			if ( pNPC )
			{
				pNPC->SetAbsOrigin( spot );
				pNPC->SetAbsAngles( ang );
				if ( DispatchSpawn( pNPC ) < 0 )
					pNPC = NULL;
				else
					pNPC->Activate();
			}
		}
		if ( pNPC )
		{
			m_hNPCs.AddToTail( pNPC );
			m_iSpawned++;
			m_OnSpawnNPC.Set( pNPC, pNPC, this );
		}
	}

	string_t m_iszNPCs;
	string_t m_iszFlag;
	int m_iCountMin, m_iCountMax;
	float m_flRadius, m_flActivateDist, m_flFromHour, m_flToHour, m_flRespawnHours;
	int m_iTarget;
	int m_iSpawned = 0;
	float m_flEmptySince;
	bool m_bEnabled;
	CUtlVector<EHANDLE> m_hNPCs;
	COutputEHANDLE m_OnSpawnNPC;
};

LINK_ENTITY_TO_CLASS( rpm_spawner, CRPMSpawner );

BEGIN_DATADESC( CRPMSpawner )
	DEFINE_KEYFIELD( m_iszNPCs, FIELD_STRING, "npcs" ),
	DEFINE_KEYFIELD( m_iszFlag, FIELD_STRING, "require_flag" ),
	DEFINE_KEYFIELD( m_iCountMin, FIELD_INTEGER, "count_min" ),
	DEFINE_KEYFIELD( m_iCountMax, FIELD_INTEGER, "count_max" ),
	DEFINE_KEYFIELD( m_flRadius, FIELD_FLOAT, "radius" ),
	DEFINE_KEYFIELD( m_flActivateDist, FIELD_FLOAT, "activate_dist" ),
	DEFINE_KEYFIELD( m_flFromHour, FIELD_FLOAT, "from_hour" ),
	DEFINE_KEYFIELD( m_flToHour, FIELD_FLOAT, "to_hour" ),
	DEFINE_KEYFIELD( m_flRespawnHours, FIELD_FLOAT, "respawn_hours" ),
	DEFINE_FIELD( m_iTarget, FIELD_INTEGER ),
	DEFINE_FIELD( m_iSpawned, FIELD_INTEGER ),
	DEFINE_FIELD( m_flEmptySince, FIELD_FLOAT ),
	DEFINE_FIELD( m_bEnabled, FIELD_BOOLEAN ),
	DEFINE_UTLVECTOR( m_hNPCs, FIELD_EHANDLE ),
	DEFINE_THINKFUNC( SpawnerThink ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_OUTPUT( m_OnSpawnNPC, "OnSpawnNPC" ),
END_DATADESC()

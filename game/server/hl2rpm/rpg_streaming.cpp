//========= HL2RPM ============//
//
// Подгрузка «жизни» сектора по расстоянию до игрока (RPG_SPEC §11.2, как ячейки Fallout: New Vegas).
// Геометрию BSP движок по частям грузить не умеет, а предел 2048 edicts и бюджет ИИ - обходится: энтити
// карты с ключом rpg_stream 1 (NPC, ящики, лут, патрули) не создаются при загрузке карты - их блоки ключей
// вынимаются из entity lump и хранятся здесь. Энтити появляется, когда игрок ближе rpg_stream_radius к её
// месту, и удаляется, когда он дальше радиуса + гистерезис и её не видит; ушедшая запоминает место,
// углы и здоровье, убитая больше не появляется. Состояние лежит в сохранении уровня (блок HL2RPMStreaming),
// поэтому переживает save/load и возвращение на посещённую карту.
// Чего нельзя стримить (пропускается с предупреждением): энтити с parentname, шаблоны, мир.
// Ссылки на стримленную энтити из I/O карты работают, только пока она существует (как у любой энтити).
//
//=============================//

#include "cbase.h"
#include "mapentities.h"
#include "mapentities_shared.h"
#include "saverestore.h"
#include "saverestore_utlvector.h"
#include "isaverestore.h"
#include "player.h"
#include "ai_basenpc.h"
#include "tier1/fmtstr.h"
#include "hl2rpm/rpg_dialog.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar rpg_stream( "rpg_stream", "1", FCVAR_GAMEDLL, "HL2RPM: stream map entities marked rpg_stream 1 by the player's distance (RPG_SPEC 11.2; applied on map load)" );
static ConVar rpg_stream_radius( "rpg_stream_radius", "2600", FCVAR_GAMEDLL, "HL2RPM: streamed entities appear within this distance of the player" );
static ConVar rpg_stream_hysteresis( "rpg_stream_hysteresis", "900", FCVAR_GAMEDLL, "HL2RPM: streamed entities go away beyond radius + this, and only out of sight" );
static ConVar rpg_stream_per_update( "rpg_stream_per_update", "12", FCVAR_GAMEDLL, "HL2RPM: at most this many streamed entities appear or go away per update (4 per second)" );
static ConVar rpg_stream_debug( "rpg_stream_debug", "0", FCVAR_GAMEDLL, "HL2RPM: 1 - log streaming, 2 - also draw the homes of streamed entities" );

#define STREAM_KEY		"rpg_stream"
#define STREAM_ID_BASE	-1000000		// m_iHammerID стримленных энтити: STREAM_ID_BASE - индекс записи

enum StreamState_t
{
	STREAM_PENDING = 0,		// ещё ни разу не появлялась
	STREAM_SPAWNED,			// существует
	STREAM_STORED,			// ушла, состояние запомнено
	STREAM_GONE,			// убита или удалена картой - больше не появится
};

struct StreamRecord_t
{
	CUtlString text;		// блок ключей карты без '{' (как у шаблонов), с '}'
	CUtlString classname, targetname;
	Vector home;
	int state;
	EHANDLE hEnt;
	bool bHasPose;
	Vector pos;
	QAngle ang;
	int health;
};

// разбор entity lump: "{ "key" "value" ... }" без вложенности, кавычки без экранирования
static const char *LumpNextBlock( const char *p, const char *&pBlockStart, const char *&pBlockEnd )
{
	while ( *p && *p != '{' )
		++p;
	if ( !*p )
		return NULL;
	pBlockStart = p;
	bool bInQuote = false;
	for ( ++p; *p; ++p )
	{
		if ( *p == '"' )
			bInQuote = !bInQuote;
		else if ( *p == '}' && !bInQuote )
		{
			pBlockEnd = p;
			return p + 1;
		}
	}
	return NULL;
}

static bool LumpBlockValue( const char *pStart, const char *pEnd, const char *pszKey, CUtlString &out )
{
	const char *p = pStart;
	CUtlString tok[2];
	int n = 0;
	while ( p < pEnd )
	{
		while ( p < pEnd && *p != '"' )
			++p;
		if ( p >= pEnd )
			break;
		const char *q = ++p;
		while ( q < pEnd && *q != '"' )
			++q;
		tok[n].SetDirect( p, q - p );
		p = q + 1;
		if ( ++n == 2 )
		{
			if ( !Q_stricmp( tok[0].Get(), pszKey ) )
			{
				out = tok[1];
				return true;
			}
			n = 0;
		}
	}
	return false;
}

class CRPGStreaming : public CAutoGameSystemPerFrame, public CDefSaveRestoreBlockHandler
{
public:
	CRPGStreaming() : CAutoGameSystemPerFrame( "CRPGStreaming" ), m_flNextUpdate( 0.0f ), m_iCursor( 0 ) {}

	// ---- вызывается из CServerGameDLL::LevelInit до разбора энтити: вынимает стримленные блоки ----
	const char *PrepareMapEntities( const char *pszMap, const char *pszEntities )
	{
		m_Records.RemoveAll();
		m_Map = pszMap ? pszMap : "";
		m_Filtered.Clear();
		m_iCursor = 0;
		if ( !pszEntities || !rpg_stream.GetBool() )
			return pszEntities;

		CUtlBuffer buf( 0, Q_strlen( pszEntities ) + 1, 0 );
		const char *p = pszEntities, *pCopied = pszEntities;
		const char *pStart = NULL, *pEnd = NULL;
		int nSkippedParented = 0;
		while ( ( p = LumpNextBlock( p, pStart, pEnd ) ) != NULL )
		{
			CUtlString val;
			if ( !LumpBlockValue( pStart, pEnd, STREAM_KEY, val ) || atoi( val.Get() ) == 0 )
				continue;
			CUtlString cls, parent;
			LumpBlockValue( pStart, pEnd, "classname", cls );
			if ( LumpBlockValue( pStart, pEnd, "parentname", parent ) && !parent.IsEmpty() )
			{
				++nSkippedParented;
				continue;
			}
			if ( cls.IsEmpty() || !Q_stricmp( cls.Get(), "worldspawn" ) || !Q_stricmp( cls.Get(), "point_template" ) )
				continue;
			// блок уходит из строки для движка
			buf.Put( pCopied, pStart - pCopied );
			pCopied = pEnd + 1;

			StreamRecord_t &r = m_Records[m_Records.AddToTail()];
			r.text.SetDirect( pStart + 1, pEnd - pStart );		// после '{', с '}'
			r.classname = cls;
			LumpBlockValue( pStart, pEnd, "targetname", r.targetname );
			CUtlString org;
			r.home.Init();
			if ( LumpBlockValue( pStart, pEnd, "origin", org ) )
				UTIL_StringToVector( r.home.Base(), org.Get() );
			r.state = STREAM_PENDING;
			r.bHasPose = false;
			r.pos = r.home;
			r.ang.Init();
			r.health = 0;
		}
		if ( m_Records.Count() == 0 )
			return pszEntities;
		buf.Put( pCopied, Q_strlen( pCopied ) );
		buf.PutChar( 0 );
		m_Filtered.SetDirect( (const char *)buf.Base(), buf.TellPut() - 1 );
		Msg( "[stream] %s: %d entities streamed by distance (radius %.0f)%s\n", m_Map.Get(), m_Records.Count(),
			rpg_stream_radius.GetFloat(), nSkippedParented ? CFmtStr( ", %d parented left to the map", nSkippedParented ).Access() : "" );
		return m_Filtered.Get();
	}

	// ---- IGameSystem ----
	virtual void LevelInitPostEntity()
	{
		// после загрузки сохранения энтити, которые тогда существовали, восстановлены движком: найти их
		for ( CBaseEntity *pEnt = gEntList.FirstEnt(); pEnt; pEnt = gEntList.NextEnt( pEnt ) )
		{
			const int i = STREAM_ID_BASE - pEnt->m_iHammerID;
			if ( pEnt->m_iHammerID <= STREAM_ID_BASE && m_Records.IsValidIndex( i ) )
			{
				m_Records[i].hEnt = pEnt;
				m_Records[i].state = STREAM_SPAWNED;
			}
		}
		for ( int i = 0; i < m_Records.Count(); ++i )
		{
			if ( m_Records[i].state == STREAM_SPAWNED && !m_Records[i].hEnt.Get() )
				m_Records[i].state = m_Records[i].bHasPose ? STREAM_STORED : STREAM_PENDING;
		}
		m_flNextUpdate = 0.0f;
	}

	virtual void LevelShutdownPostEntity()
	{
		m_Records.RemoveAll();
		m_Filtered.Clear();
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( m_Records.Count() == 0 || gpGlobals->curtime < m_flNextUpdate )
			return;
		m_flNextUpdate = gpGlobals->curtime + 0.25f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer )
			return;
		Update( pPlayer );
	}

	// ---- сохранение уровня ----
	virtual const char *GetBlockName() { return "HL2RPMStreaming"; }
	virtual void WriteSaveHeaders( ISave *pSave )
	{
		short nVersion = 1;
		pSave->WriteShort( &nVersion );
	}
	virtual void ReadRestoreHeaders( IRestore *pRestore )
	{
		short nVersion;
		pRestore->ReadShort( &nVersion );
		m_bDoLoad = ( nVersion == 1 );
	}
	virtual void Save( ISave *pSave )
	{
		pSave->StartBlock( "Streaming" );
		char szMap[128];
		Q_strncpy( szMap, m_Map.Get(), sizeof( szMap ) );
		pSave->WriteString( szMap );
		int n = m_Records.Count();
		pSave->WriteInt( &n );
		for ( int i = 0; i < n; ++i )
		{
			StreamRecord_t &r = m_Records[i];
			int state = r.state;
			// существующая энтити сохраняется движком; убитая между обновлениями - уже GONE
			if ( state == STREAM_SPAWNED && !r.hEnt.Get() )
				state = STREAM_GONE;
			pSave->WriteInt( &state );
			int pose = r.bHasPose ? 1 : 0;
			pSave->WriteInt( &pose );
			pSave->WriteFloat( r.pos.Base(), 3 );
			pSave->WriteFloat( (float *)r.ang.Base(), 3 );
			pSave->WriteInt( &r.health );
		}
		pSave->EndBlock();
	}
	virtual void Restore( IRestore *pRestore, bool fCreatePlayers )
	{
		if ( !m_bDoLoad )
			return;
		pRestore->StartBlock();
		char szMap[128];
		pRestore->ReadString( szMap, sizeof( szMap ), 0 );
		int n = 0;
		pRestore->ReadInt( &n );
		const bool bSameMap = !Q_stricmp( szMap, m_Map.Get() ) && n == m_Records.Count();
		if ( !bSameMap )
			Warning( "[stream] saved streaming state is for %s (%d entities), the map is %s (%d) - ignored\n",
				szMap, n, m_Map.Get(), m_Records.Count() );
		for ( int i = 0; i < n; ++i )
		{
			int state = 0, pose = 0, health = 0;
			Vector pos;
			QAngle ang;
			pRestore->ReadInt( &state );
			pRestore->ReadInt( &pose );
			pRestore->ReadFloat( pos.Base(), 3 );
			pRestore->ReadFloat( (float *)ang.Base(), 3 );
			pRestore->ReadInt( &health );
			if ( !bSameMap )
				continue;
			StreamRecord_t &r = m_Records[i];
			r.state = state;
			r.bHasPose = pose != 0;
			r.pos = pos;
			r.ang = ang;
			r.health = health;
			r.hEnt = NULL;		// LevelInitPostEntity найдёт восстановленную энтити по m_iHammerID
		}
		pRestore->EndBlock();
	}

	void Print()
	{
		int nState[4] = { 0, 0, 0, 0 };
		for ( int i = 0; i < m_Records.Count(); ++i )
			++nState[clamp( m_Records[i].state, 0, 3 )];
		Msg( "[stream] %s: %d records: %d live, %d stored, %d never spawned, %d gone; edicts in use %d\n",
			m_Map.Get(), m_Records.Count(), nState[STREAM_SPAWNED], nState[STREAM_STORED], nState[STREAM_PENDING],
			nState[STREAM_GONE], engine->GetEntityCount() );
	}

private:
	bool CanStoreNow( CBaseEntity *pEnt, CBasePlayer *pPlayer )
	{
		CAI_BaseNPC *pNPC = pEnt->MyNPCPointer();
		if ( pNPC )
		{
			if ( pNPC->GetEnemy() || pNPC->IsInAScript() || pNPC->GetState() == NPC_STATE_SCRIPT )
				return false;
			if ( RPGDialog::IsActive() )
				return false;		// идёт разговор - никто вокруг не исчезает
		}
		if ( pEnt->GetMoveParent() )
			return false;
		// на глазах не исчезает
		return !( pPlayer->FInViewCone( pEnt ) && pPlayer->FVisible( pEnt ) );
	}

	void Spawn( int i )
	{
		StreamRecord_t &r = m_Records[i];
		CBaseEntity *pEnt = NULL;
		MapEntity_ParseEntity( pEnt, r.text.Get(), NULL );
		if ( !pEnt )
		{
			r.state = STREAM_GONE;
			return;
		}
		pEnt->m_iHammerID = STREAM_ID_BASE - i;
		if ( DispatchSpawn( pEnt ) < 0 )
		{
			r.state = STREAM_GONE;
			return;
		}
		pEnt->Activate();
		if ( r.bHasPose )
		{
			pEnt->Teleport( &r.pos, &r.ang, NULL );
			if ( r.health > 0 && pEnt->GetMaxHealth() > 0 )
				pEnt->SetHealth( Min( r.health, pEnt->GetMaxHealth() ) );
		}
		r.hEnt = pEnt;
		r.state = STREAM_SPAWNED;
		if ( rpg_stream_debug.GetBool() )
			Msg( "[stream] + %s %s\n", r.classname.Get(), r.targetname.Get() );
	}

	void Store( int i )
	{
		StreamRecord_t &r = m_Records[i];
		CBaseEntity *pEnt = r.hEnt.Get();
		if ( pEnt )
		{
			r.pos = pEnt->GetAbsOrigin();
			r.ang = pEnt->GetAbsAngles();
			r.health = pEnt->GetHealth();
			r.bHasPose = true;
			UTIL_Remove( pEnt );
		}
		r.hEnt = NULL;
		r.state = STREAM_STORED;
		if ( rpg_stream_debug.GetBool() )
			Msg( "[stream] - %s %s\n", r.classname.Get(), r.targetname.Get() );
	}

	void Update( CBasePlayer *pPlayer )
	{
		const Vector eye = pPlayer->EyePosition();
		const float r0 = rpg_stream_radius.GetFloat();
		const float r1 = r0 + Max( 0.0f, rpg_stream_hysteresis.GetFloat() );
		int budget = Max( 1, rpg_stream_per_update.GetInt() );
		const int n = m_Records.Count();
		// все записи - дёшево (расстояние); создание/удаление - не больше budget за обновление
		for ( int k = 0; k < n && budget > 0; ++k )
		{
			const int i = ( m_iCursor + k ) % n;
			StreamRecord_t &r = m_Records[i];
			switch ( r.state )
			{
			case STREAM_PENDING:
			case STREAM_STORED:
			{
				const Vector &at = r.bHasPose ? r.pos : r.home;
				if ( ( at - eye ).LengthSqr() < r0 * r0 )
				{
					Spawn( i );
					--budget;
				}
				break;
			}
			case STREAM_SPAWNED:
			{
				CBaseEntity *pEnt = r.hEnt.Get();
				if ( !pEnt || ( pEnt->MyNPCPointer() && !pEnt->IsAlive() ) )
				{
					// убита (труп остаётся как есть) или удалена картой
					r.state = STREAM_GONE;
					r.hEnt = NULL;
					break;
				}
				if ( ( pEnt->GetAbsOrigin() - eye ).LengthSqr() > r1 * r1 && CanStoreNow( pEnt, pPlayer ) )
				{
					Store( i );
					--budget;
				}
				break;
			}
			default:
				break;
			}
			if ( rpg_stream_debug.GetInt() >= 2 && r.state != STREAM_GONE )
				NDebugOverlay::Cross3D( r.bHasPose ? r.pos : r.home, 16.0f, r.state == STREAM_SPAWNED ? 0 : 255,
					r.state == STREAM_SPAWNED ? 255 : 128, 0, true, 0.3f );
		}
		m_iCursor = n ? ( m_iCursor + 64 ) % n : 0;
	}

	CUtlVector<StreamRecord_t> m_Records;
	CUtlString m_Map;
	CUtlString m_Filtered;
	float m_flNextUpdate;
	int m_iCursor;
	bool m_bDoLoad;
};

static CRPGStreaming g_RPGStreaming;

const char *RPGStreaming_PrepareMapEntities( const char *pszMap, const char *pszEntities )
{
	return g_RPGStreaming.PrepareMapEntities( pszMap, pszEntities );
}

ISaveRestoreBlockHandler *GetRPGStreamingSaveRestoreBlockHandler()
{
	return &g_RPGStreaming;
}

CON_COMMAND_F( rpg_stream_info, "HL2RPM: streamed entities of this map (live / stored / never spawned / gone) and edicts in use", FCVAR_GAMEDLL )
{
	g_RPGStreaming.Print();
}

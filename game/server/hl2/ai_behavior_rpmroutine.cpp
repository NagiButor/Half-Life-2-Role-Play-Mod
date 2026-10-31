//=============================================================================
// HL2RPM: daily routine of an NPC (see ai_behavior_rpmroutine.h, hl2rpm_world.h)
//=============================================================================
#include "cbase.h"
#include "ai_behavior_rpmroutine.h"
#include "ai_behavior_actbusy.h"
#include "ai_hint.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "hl2rpm_world.h"
#include "dialogsystem_server.h"
#include "dialog_definitions.h"
#include "player.h"
#include "ndebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_DATADESC( CAI_RPMRoutineBehavior )
	DEFINE_FIELD( m_iPackage, FIELD_INTEGER ),
	DEFINE_FIELD( m_iszPackage, FIELD_STRING ),
	DEFINE_FIELD( m_nState, FIELD_INTEGER ),
	DEFINE_FIELD( m_flNextEval, FIELD_TIME ),
	DEFINE_FIELD( m_vecHome, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_bHomeSet, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecStartPos, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecGoal, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flGoalTolerance, FIELD_FLOAT ),
	DEFINE_FIELD( m_bGoalRun, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bStandHasYaw, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flStandYaw, FIELD_FLOAT ),
	DEFINE_FIELD( m_flWaitUntil, FIELD_TIME ),
	DEFINE_FIELD( m_iPathPoint, FIELD_INTEGER ),
	DEFINE_FIELD( m_iPathDir, FIELD_INTEGER ),
	DEFINE_FIELD( m_bDelegated, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bStopRequested, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flBusyUntil, FIELD_TIME ),
	DEFINE_FIELD( m_hBusyHint, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hTempHint, FIELD_EHANDLE ),
	DEFINE_FIELD( m_iszDone, FIELD_STRING ),
	DEFINE_FIELD( m_flNextDebug, FIELD_TIME ),
	DEFINE_FIELD( m_nFailures, FIELD_INTEGER ),
	DEFINE_FIELD( m_bBusyEntered, FIELD_BOOLEAN ),
END_DATADESC();

CAI_RPMRoutineBehavior::CAI_RPMRoutineBehavior()
{
	m_iPackage = -1;
	m_iszPackage = NULL_STRING;
	m_nState = RS_START;
	m_flNextEval = 0;
	m_vecHome.Init();
	m_bHomeSet = false;
	m_vecStartPos.Init();
	m_vecGoal.Init();
	m_flGoalTolerance = 32;
	m_bGoalRun = false;
	m_bStandHasYaw = false;
	m_flStandYaw = 0;
	m_flWaitUntil = 0;
	m_iPathPoint = -1;
	m_iPathDir = 1;
	m_bDelegated = false;
	m_bStopRequested = false;
	m_flBusyUntil = 0;
	m_iszDone = NULL_STRING;
	m_flNextDebug = 0;
	m_nFailures = 0;
	m_bBusyEntered = false;
}

const RPMNPCDef_t *CAI_RPMRoutineBehavior::GetDef()
{
	return RPMWorld::FindNPCDef( GetOuter()->GetEntityName().ToCStr() );
}

const RPMPackage_t *CAI_RPMRoutineBehavior::GetPackage()
{
	const RPMNPCDef_t *pDef = GetDef();
	if ( !pDef || m_iPackage < 0 || m_iPackage >= pDef->packages.Count() )
		return NULL;
	return &pDef->packages[m_iPackage];
}

CAI_ActBusyBehavior *CAI_RPMRoutineBehavior::GetActBusy()
{
	CAI_ActBusyBehavior *pActBusy = NULL;
	GetOuter()->GetBehavior( &pActBusy );
	return pActBusy;
}

bool CAI_RPMRoutineBehavior::IsDone( const RPMPackage_t &pkg )
{
	if ( m_iszDone == NULL_STRING )
		return false;
	char sz[160];
	Q_snprintf( sz, sizeof( sz ), ";%s;", pkg.name.Get() );
	return Q_stristr( STRING( m_iszDone ), sz ) != NULL;
}

void CAI_RPMRoutineBehavior::MarkDone( const RPMPackage_t &pkg )
{
	if ( IsDone( pkg ) )
		return;
	char sz[1024];
	Q_snprintf( sz, sizeof( sz ), "%s%s%s;", ( m_iszDone != NULL_STRING ) ? STRING( m_iszDone ) : "", ( m_iszDone != NULL_STRING ) ? "" : ";", pkg.name.Get() );
	m_iszDone = AllocPooledString( sz );
}

//-----------------------------------------------------------------------------
// Which package runs now
//-----------------------------------------------------------------------------
void CAI_RPMRoutineBehavior::Evaluate( bool bForce )
{
	if ( !bForce && gpGlobals->curtime < m_flNextEval )
		return;
	m_flNextEval = gpGlobals->curtime + 1.0f + random->RandomFloat( 0.0f, 0.3f );

	if ( !m_bHomeSet )
	{
		m_vecHome = GetOuter()->GetAbsOrigin();
		m_bHomeSet = true;
	}

	const RPMNPCDef_t *pDef = GetDef();
	int iNew = -1;
	if ( pDef && GetOuter()->IsAlive() )
	{
		const float flHours = RPMWorld::GetHours();
		for ( int i = 0; i < pDef->packages.Count(); i++ )
		{
			const RPMPackage_t &pkg = pDef->packages[i];
			if ( pkg.type == RPMPKG_NONE )
				continue;
			if ( !RPMWorld::IsPackageTime( pkg, flHours ) || !RPMWorld::PackageConditionsMet( pkg ) )
				continue;
			if ( pkg.once && IsDone( pkg ) )
				continue;
			iNew = i;
			break;
		}
	}
	const string_t iszNew = ( iNew >= 0 ) ? AllocPooledString( pDef->packages[iNew].name ) : NULL_STRING;
	if ( iNew != m_iPackage || iszNew != m_iszPackage )
	{
		m_iPackage = iNew;
		m_iszPackage = iszNew;
		OnPackageChanged();
	}
}

void CAI_RPMRoutineBehavior::OnPackageChanged()
{
	StopDelegatedBusy();
	m_nState = RS_START;
	m_iPathPoint = -1;
	m_iPathDir = 1;
	m_flWaitUntil = 0;
	m_nFailures = 0;
	m_vecStartPos = GetOuter()->GetAbsOrigin();
	if ( IsRunning() )
		SetCondition( COND_RPM_REEVALUATE );
}

const char *CAI_RPMRoutineBehavior::BlockedReason()
{
	CAI_BaseNPC *pOuter = GetOuter();
	if ( !pOuter->IsAlive() )
		return "dead";
	const NPC_STATE state = pOuter->GetState();
	if ( state != NPC_STATE_IDLE && state != NPC_STATE_ALERT )
		return ( state == NPC_STATE_COMBAT ) ? "combat" : ( state == NPC_STATE_SCRIPT ) ? "script state" : "npc state";
	if ( pOuter->GetEnemy() )
		return "enemy";
	if ( pOuter->IsInAScript() || pOuter->IsRunningDynamicInteraction() )
		return "script";
	if ( pOuter->IsInPlayerSquad() )
		return "player squad";
	if ( RPMWorld::IsInDialog( pOuter ) )
		return "dialog";
	return NULL;
}

bool CAI_RPMRoutineBehavior::BlockedByOtherAI()
{
	return BlockedReason() != NULL;
}

bool CAI_RPMRoutineBehavior::WantsControl()
{
	if ( m_iPackage < 0 || m_bDelegated )
		return false;
	return !BlockedByOtherAI();
}

bool CAI_RPMRoutineBehavior::CanSelectSchedule()
{
	Evaluate( false );
	return WantsControl();
}

//-----------------------------------------------------------------------------
// The actbusy behavior plays marker animations for us
//-----------------------------------------------------------------------------
void CAI_RPMRoutineBehavior::StopDelegatedBusy()
{
	if ( !m_bDelegated || m_bStopRequested )
		return;
	CAI_ActBusyBehavior *pActBusy = GetActBusy();
	if ( pActBusy && pActBusy->IsActive() )
	{
		pActBusy->StopBusying();
		m_bStopRequested = true;
	}
}

void CAI_RPMRoutineBehavior::MaintainDelegatedBusy()
{
	if ( !m_bDelegated )
		return;
	CAI_ActBusyBehavior *pActBusy = GetActBusy();
	if ( !pActBusy )
	{
		m_bDelegated = false;
		return;
	}
	if ( pActBusy->IsInsideActBusy() )
	{
		m_bBusyEntered = true;
		m_nFailures = 0;
	}
	if ( !pActBusy->IsActive() )
	{
		// finished: time over, interrupted, stopped or it could not get there
		if ( pActBusy->IsEnabled() )
			pActBusy->Disable();
		m_bDelegated = false;
		m_bStopRequested = false;
		m_hBusyHint = NULL;
		if ( !m_bBusyEntered )
			m_nFailures++;
		if ( m_nState == RS_BUSY )
			m_nState = RS_ARRIVED;
		return;
	}
	if ( m_bStopRequested )
		return;
	CAI_BaseNPC *pOuter = GetOuter();
	const bool bTimeUp = m_flBusyUntil > 0 && gpGlobals->curtime > m_flBusyUntil;
	// a sleeping NPC wakes when shot at or attacked (the bed animations ignore everything)
	const bool bDanger = pOuter->GetEnemy() != NULL || pOuter->HasCondition( COND_LIGHT_DAMAGE ) || pOuter->HasCondition( COND_HEAVY_DAMAGE ) ||
		pOuter->HasCondition( COND_HEAR_DANGER ) || pOuter->GetState() == NPC_STATE_COMBAT;
	if ( bTimeUp || bDanger )
	{
		pActBusy->StopBusying();
		m_bStopRequested = true;
	}
}

//-----------------------------------------------------------------------------
void CAI_RPMRoutineBehavior::GatherConditions()
{
	BaseClass::GatherConditions();
	Evaluate( false );
	MaintainDelegatedBusy();
	if ( BlockedByOtherAI() )
		SetCondition( COND_RPM_REEVALUATE );

	if ( RPMWorld::IsDebugEnabled() && gpGlobals->curtime >= m_flNextDebug )
	{
		m_flNextDebug = gpGlobals->curtime + 0.5f;
		char sz[256];
		GetDebugText( sz, sizeof( sz ) );
		NDebugOverlay::Text( GetOuter()->EyePosition() + Vector( 0, 0, 14 ), sz, false, 0.55f );
		if ( RPMWorld::DebugLevel() >= 2 && m_nState == RS_MOVING )
			NDebugOverlay::Line( GetOuter()->GetAbsOrigin(), m_vecGoal, 0, 255, 0, true, 0.55f );
	}
}

void CAI_RPMRoutineBehavior::GatherConditionsNotActive()
{
	Evaluate( false );
	MaintainDelegatedBusy();

	if ( WantsControl() && GetOuter()->ConditionInterruptsCurSchedule( COND_IDLE_INTERRUPT ) )
		GetOuter()->ClearSchedule( "RPM routine package" );

	if ( RPMWorld::IsDebugEnabled() && m_iPackage >= 0 && gpGlobals->curtime >= m_flNextDebug )
	{
		m_flNextDebug = gpGlobals->curtime + 0.5f;
		char sz[256];
		GetDebugText( sz, sizeof( sz ) );
		NDebugOverlay::Text( GetOuter()->EyePosition() + Vector( 0, 0, 14 ), sz, false, 0.55f );
	}
}

void CAI_RPMRoutineBehavior::BeginScheduleSelection()
{
	BaseClass::BeginScheduleSelection();
	Evaluate( true );
}

//-----------------------------------------------------------------------------
Vector CAI_RPMRoutineBehavior::ResolveLocation( const RPMPackage_t &pkg, bool *pbHasYaw, float *pflYaw )
{
	bool bHasYaw = pkg.hasYaw;
	float flYaw = pkg.yaw;
	Vector pos = m_vecHome;
	switch ( pkg.locKind )
	{
	case RPMLOC_REF:
		{
			CBaseEntity *pEnt = pkg.ref.IsEmpty() ? NULL : gEntList.FindEntityByName( NULL, pkg.ref );
			if ( pEnt )
			{
				pos = pEnt->GetAbsOrigin();
				if ( !bHasYaw )
				{
					bHasYaw = true;
					flYaw = pEnt->GetAbsAngles().y;
				}
			}
			else if ( const RPMPath_t *pPath = RPMWorld::FindPath( pkg.ref ) )
			{
				pos = pPath->points[0].pos;
			}
		}
		break;
	case RPMLOC_POINT:
		pos = pkg.point;
		break;
	case RPMLOC_PLAYER:
		if ( CBasePlayer *pPlayer = UTIL_GetLocalPlayer() )
			pos = pPlayer->GetAbsOrigin();
		break;
	case RPMLOC_CURRENT:
		pos = m_vecStartPos;
		break;
	default:
		break;
	}
	if ( pbHasYaw )
		*pbHasYaw = bHasYaw;
	if ( pflYaw )
		*pflYaw = flYaw;
	return pos;
}

bool CAI_RPMRoutineBehavior::FindWanderPoint( const Vector &center, float flRadius, Vector &result )
{
	for ( int i = 0; i < 6; i++ )
	{
		const float flAngle = random->RandomFloat( 0, 2 * M_PI );
		const float flDist = random->RandomFloat( flRadius * 0.25f, flRadius );
		Vector p = center + Vector( cosf( flAngle ) * flDist, sinf( flAngle ) * flDist, 0 );
		trace_t tr;
		UTIL_TraceLine( p + Vector( 0, 0, 64 ), p - Vector( 0, 0, 256 ), MASK_NPCSOLID, GetOuter(), COLLISION_GROUP_NONE, &tr );
		if ( tr.startsolid || tr.fraction >= 1.0f || tr.plane.normal.z < 0.7f )
			continue;
		if ( enginetrace->GetPointContents( tr.endpos + Vector( 0, 0, 16 ) ) & MASK_WATER )
			continue;
		result = tr.endpos;
		return true;
	}
	return false;
}

int CAI_RPMRoutineBehavior::ScheduleMoveTo( const Vector &goal, float flTolerance, bool bRun )
{
	m_vecGoal = goal;
	m_flGoalTolerance = MAX( flTolerance, 16.0f );
	m_bGoalRun = bRun;
	m_nState = RS_MOVING;
	return SCHED_RPM_MOVE;
}

int CAI_RPMRoutineBehavior::ScheduleStand( float flSeconds, bool bHasYaw, float flYaw )
{
	m_bStandHasYaw = bHasYaw;
	m_flStandYaw = flYaw;
	m_flWaitUntil = gpGlobals->curtime + MAX( flSeconds, 0.5f );
	if ( m_nState != RS_ARRIVED )
		m_nState = RS_IDLE;
	return SCHED_RPM_STAND;
}

int CAI_RPMRoutineBehavior::ScheduleUseHint( CAI_Hint *pHint, float flMaxTime )
{
	if ( !pHint )
		return SCHED_RPM_FAILED;
	const char *pszAnim = STRING( pHint->HintActivityName() );
	CAI_ActBusyBehavior *pActBusy = GetActBusy();
	if ( !pszAnim[0] || !pActBusy || !pActBusy->CanPlayActBusy( pszAnim ) || m_nFailures >= 3 )
	{
		// a marker without an animation (or one this model can't play): stand there
		const Vector pos = pHint->GetAbsOrigin();
		if ( ( GetAbsOrigin() - pos ).Length2D() > 24.0f && m_nState != RS_MOVING )
		{
			m_hBusyHint = pHint;
			return ScheduleMoveTo( pos, 16.0f, false );
		}
		m_hBusyHint = NULL;
		m_nState = RS_ARRIVED;
		return ScheduleStand( flMaxTime > 0 ? flMaxTime : 10.0f, true, pHint->Yaw() );
	}
	m_bDelegated = true;
	m_bStopRequested = false;
	m_bBusyEntered = false;
	m_hBusyHint = pHint;
	m_flBusyUntil = ( flMaxTime > 0 ) ? gpGlobals->curtime + flMaxTime + 5.0f : 0.0f;
	m_nState = RS_BUSY;
	// 0 - loop until we stop it; the entries of actbusy.txt end on their own after 20-40 s
	pActBusy->ForceActBusy( NULL, pHint, ( flMaxTime > 0 ) ? flMaxTime : 0.0f );
	return SCHED_RPM_YIELD;
}

int CAI_RPMRoutineBehavior::ScheduleAnimAt( const char *pszAnim, const Vector &pos, float flYaw, float flMaxTime )
{
	CAI_ActBusyBehavior *pActBusy = GetActBusy();
	if ( !pszAnim || !pszAnim[0] || !pActBusy || !pActBusy->CanPlayActBusy( pszAnim ) || m_nFailures >= 3 )
		return ScheduleStand( flMaxTime > 0 ? flMaxTime : 8.0f, true, flYaw );

	CAI_Hint *pHint = dynamic_cast<CAI_Hint *>( m_hTempHint.Get() );
	if ( !pHint )
	{
		HintNodeData data;
		char szName[64];
		Q_snprintf( szName, sizeof( szName ), "rpm_tmp_%d", GetOuter()->entindex() );
		data.strEntityName = AllocPooledString( szName );
		data.vecPosition = pos;
		data.nHintType = HINT_WORLD_WORK_POSITION;
		data.nNodeID = NO_NODE;
		data.strGroup = AllocPooledString( "rpm_marker" );
		data.iDisabled = 0;
		data.iszActivityName = AllocPooledString( pszAnim );
		data.nTargetWCNodeID = -1;
		data.fIgnoreFacing = HIF_NO;
		data.minState = NPC_STATE_IDLE;
		data.maxState = NPC_STATE_ALERT;
#ifdef MAPBASE
		data.nRadius = 0;
		data.flWeight = 1.0f;
		data.flWeightInverse = 1.0f;
#endif
		data.nWCNodeID = -1;
		pHint = CAI_HintManager::CreateHint( &data, NULL );
		if ( !pHint )
			return ScheduleStand( 8.0f, true, flYaw );
		pHint->KeyValue( "nodeFOV", "360" );
		pHint->Activate();
		m_hTempHint = pHint;
	}
	pHint->SetAbsOrigin( pos );
	pHint->SetLocalAngles( QAngle( 0, flYaw, 0 ) );
	pHint->SetHintActivityName( AllocPooledString( pszAnim ) );
	return ScheduleUseHint( pHint, flMaxTime );
}

int CAI_RPMRoutineBehavior::SchedulePatrolStep( const RPMPackage_t &pkg )
{
	const RPMPath_t *pPath = RPMWorld::FindPath( pkg.path.IsEmpty() ? pkg.ref.Get() : pkg.path.Get() );
	if ( !pPath || !pPath->points.Count() )
		return ScheduleStand( 5.0f, false, 0 );
	const int nPoints = pPath->points.Count();

	if ( m_iPathPoint < 0 || m_iPathPoint >= nPoints )
	{
		// start at the nearest point
		float flBest = FLT_MAX;
		m_iPathPoint = 0;
		for ( int i = 0; i < nPoints; i++ )
		{
			const float d = ( pPath->points[i].pos - GetAbsOrigin() ).LengthSqr();
			if ( d < flBest )
			{
				flBest = d;
				m_iPathPoint = i;
			}
		}
		m_nState = RS_START;
	}

	const RPMPathPoint_t &pt = pPath->points[m_iPathPoint];
	if ( m_nState == RS_MOVING )
	{
		// arrived: the point's wait / animation
		m_nState = RS_ARRIVED;
		if ( !pt.anim.IsEmpty() )
			return ScheduleAnimAt( pt.anim, pt.pos, pt.hasYaw ? pt.yaw : GetAbsAngles().y, pt.wait > 0 ? pt.wait : 10.0f );
		if ( pt.wait > 0 )
			return ScheduleStand( pt.wait, pt.hasYaw, pt.yaw );
	}
	if ( m_nState == RS_ARRIVED || m_nState == RS_IDLE )
	{
		// next point
		if ( pkg.pathMode == RPMPATH_PINGPONG )
		{
			if ( m_iPathPoint + m_iPathDir < 0 || m_iPathPoint + m_iPathDir >= nPoints )
				m_iPathDir = -m_iPathDir;
			m_iPathPoint = clamp( m_iPathPoint + m_iPathDir, 0, nPoints - 1 );
		}
		else if ( pkg.pathMode == RPMPATH_ONCE )
		{
			if ( m_iPathPoint + 1 >= nPoints )
			{
				m_nState = RS_DONE;
				return ScheduleStand( 10.0f, pt.hasYaw, pt.yaw );
			}
			m_iPathPoint++;
		}
		else
		{
			m_iPathPoint = ( m_iPathPoint + 1 ) % nPoints;
		}
	}
	else if ( m_nState == RS_DONE )
	{
		return ScheduleStand( 10.0f, pt.hasYaw, pt.yaw );
	}
	return ScheduleMoveTo( pPath->points[m_iPathPoint].pos, 24.0f, pkg.run );
}

int CAI_RPMRoutineBehavior::ScheduleSandboxStep( const RPMPackage_t &pkg, const Vector &center, float flRadius )
{
	// walked to a marker without an animation: stand there
	CAI_Hint *pPending = dynamic_cast<CAI_Hint *>( m_hBusyHint.Get() );
	if ( pPending && !m_bDelegated && m_nState == RS_MOVING )
	{
		m_hBusyHint = NULL;
		m_nState = RS_ARRIVED;
		return ScheduleStand( random->RandomFloat( pkg.minUse, pkg.maxUse ), true, pPending->Yaw() );
	}

	if ( ( GetAbsOrigin() - center ).Length2D() > flRadius * 1.25f && m_nState != RS_MOVING )
		return ScheduleMoveTo( center, flRadius * 0.5f, pkg.run );

	const int roll = random->RandomInt( 0, 99 );
	if ( roll < 55 && m_nFailures < 3 )
	{
		const char *pszKinds = pkg.markerKinds.IsEmpty() ? "sit;lean;eat;ground;work;idle" : pkg.markerKinds.Get();
		CAI_Hint *pHint = RPMWorld::FindFreeMarker( GetOuter(), pszKinds, center, flRadius, true );
		if ( pHint )
			return ScheduleUseHint( pHint, random->RandomFloat( pkg.minUse, pkg.maxUse ) );
	}
	if ( roll < 80 && m_nState != RS_MOVING )
	{
		Vector p;
		if ( FindWanderPoint( center, flRadius, p ) )
			return ScheduleMoveTo( p, 32.0f, false );
	}
	m_nState = RS_IDLE;
	return ScheduleStand( random->RandomFloat( 5.0f, 15.0f ), false, 0 );
}

//-----------------------------------------------------------------------------
int CAI_RPMRoutineBehavior::SelectSchedule()
{
	const RPMPackage_t *pPkg = GetPackage();
	if ( !pPkg )
		return SCHED_RPM_FAILED;
	const RPMPackage_t &pkg = *pPkg;
	CAI_BaseNPC *pOuter = GetOuter();

	bool bHasYaw = false;
	float flYaw = 0;
	const Vector loc = ResolveLocation( pkg, &bHasYaw, &flYaw );
	const float flDist = ( pOuter->GetAbsOrigin() - loc ).Length2D();
	const float flDz = fabsf( pOuter->GetAbsOrigin().z - loc.z );

	switch ( pkg.type )
	{
	case RPMPKG_WAIT:
	case RPMPKG_TRAVEL:
	case RPMPKG_GUARD:
		{
			float flTol = ( pkg.type == RPMPKG_GUARD ) ? 24.0f : MAX( pkg.radius, 48.0f );
			if ( pkg.type == RPMPKG_WAIT && pkg.locKind == RPMLOC_SELF && pkg.radius <= 0 )
				flTol = 96.0f;
			if ( ( flDist > flTol || flDz > 72.0f ) && m_nState != RS_MOVING && !( pkg.type == RPMPKG_WAIT && pkg.locKind == RPMLOC_CURRENT ) )
				return ScheduleMoveTo( loc, MIN( flTol, 48.0f ), pkg.run );
			m_nState = RS_ARRIVED;
			if ( !pkg.anim.IsEmpty() )
				return ScheduleAnimAt( pkg.anim, ( pkg.type == RPMPKG_GUARD ) ? loc : pOuter->GetAbsOrigin(), bHasYaw ? flYaw : pOuter->GetAbsAngles().y, 0 );
			return ScheduleStand( random->RandomFloat( 6.0f, 12.0f ), bHasYaw, flYaw );
		}

	case RPMPKG_PATROL:
		return SchedulePatrolStep( pkg );

	case RPMPKG_WANDER:
		{
			const float flRadius = ( pkg.radius > 0 ) ? pkg.radius : 512.0f;
			if ( flDist > flRadius * 1.25f && m_nState != RS_MOVING )
				return ScheduleMoveTo( loc, flRadius * 0.5f, pkg.run );
			if ( m_nState == RS_MOVING )
			{
				m_nState = RS_IDLE;
				return ScheduleStand( random->RandomFloat( 4.0f, 12.0f ), false, 0 );
			}
			Vector p;
			if ( FindWanderPoint( loc, flRadius, p ) )
				return ScheduleMoveTo( p, 32.0f, pkg.run );
			return ScheduleStand( 4.0f, false, 0 );
		}

	case RPMPKG_SANDBOX:
		return ScheduleSandboxStep( pkg, loc, ( pkg.radius > 0 ) ? pkg.radius : 768.0f );

	case RPMPKG_SLEEP:
	case RPMPKG_EAT:
	case RPMPKG_USEMARKER:
		{
			CAI_Hint *pHint = NULL;
			const float flRadius = ( pkg.radius > 0 ) ? pkg.radius : 2048.0f;
			if ( pkg.type == RPMPKG_USEMARKER && !pkg.ref.IsEmpty() )
				pHint = dynamic_cast<CAI_Hint *>( gEntList.FindEntityByName( NULL, pkg.ref ) );
			if ( !pHint && m_nFailures < 3 )
			{
				const char *pszKinds = pkg.markerKinds.Get();
				if ( pkg.type == RPMPKG_SLEEP )
					pszKinds = "sleep";
				else if ( pkg.type == RPMPKG_EAT )
					pszKinds = "eat";
				pHint = RPMWorld::FindFreeMarker( pOuter, pszKinds, loc, flRadius, false );
				if ( !pHint && pkg.type == RPMPKG_EAT )
					pHint = RPMWorld::FindFreeMarker( pOuter, "sit", loc, flRadius, false );
			}
			if ( pHint && m_nState != RS_ARRIVED )
			{
				CAI_Hint *pPending = dynamic_cast<CAI_Hint *>( m_hBusyHint.Get() );
				if ( pPending == pHint && m_nState == RS_MOVING )
					m_nState = RS_ARRIVED;
				return ScheduleUseHint( pHint, 0 );
			}
			// no marker: at the location, sleeping on the ground / standing
			if ( ( flDist > 64.0f || flDz > 72.0f ) && m_nState != RS_MOVING && m_nState != RS_ARRIVED )
				return ScheduleMoveTo( loc, 48.0f, pkg.run );
			m_nState = RS_ARRIVED;
			const char *pszAnim = pkg.anim.Get();
			if ( !pszAnim[0] && pkg.type == RPMPKG_SLEEP )
				pszAnim = "ACT_BUSY_SIT_GROUND";
			if ( pszAnim[0] && m_nFailures < 3 )
				return ScheduleAnimAt( pszAnim, pOuter->GetAbsOrigin(), bHasYaw ? flYaw : pOuter->GetAbsAngles().y, 0 );
			return ScheduleStand( 15.0f, bHasYaw, flYaw );
		}

	case RPMPKG_FOLLOW:
		{
			CBaseEntity *pTarget = NULL;
			if ( pkg.target.IsEmpty() || !Q_stricmp( pkg.target, "!player" ) )
				pTarget = UTIL_GetLocalPlayer();
			else
				pTarget = gEntList.FindEntityByName( NULL, pkg.target );
			if ( !pTarget )
				return ScheduleStand( 3.0f, false, 0 );
			const float flKeep = ( pkg.radius > 0 ) ? pkg.radius : 128.0f;
			const float flTargetDist = ( pTarget->GetAbsOrigin() - pOuter->GetAbsOrigin() ).Length();
			m_nState = RS_START;
			if ( flTargetDist > flKeep )
				return ScheduleMoveTo( pTarget->GetAbsOrigin(), flKeep * 0.75f, pkg.run || flTargetDist > 400.0f );
			return ScheduleStand( 1.0f, true, UTIL_VecToYaw( pTarget->GetAbsOrigin() - pOuter->GetAbsOrigin() ) );
		}

	case RPMPKG_TALK:
		{
			CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
			const float flReach = ( pkg.radius > 0 ) ? pkg.radius : 1500.0f;
			if ( !pPlayer || !pPlayer->IsAlive() )
				return ScheduleStand( 3.0f, false, 0 );
			const float flPlayerDist = ( pPlayer->GetAbsOrigin() - pOuter->GetAbsOrigin() ).Length();
			if ( flPlayerDist > flReach )
				return ScheduleStand( 3.0f, false, 0 );
			if ( flPlayerDist > 90.0f || !pOuter->FVisible( pPlayer ) )
			{
				m_nState = RS_START;
				return ScheduleMoveTo( pPlayer->GetAbsOrigin(), 64.0f, pkg.run );
			}
			return SCHED_RPM_TALK;
		}
	}
	return ScheduleStand( 5.0f, false, 0 );
}

int CAI_RPMRoutineBehavior::SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode )
{
	m_nFailures++;
	m_nState = RS_IDLE;
	m_hBusyHint = NULL;
	return SCHED_RPM_FAILED;
}

//-----------------------------------------------------------------------------
void CAI_RPMRoutineBehavior::StartTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_RPM_GET_PATH:
		{
			if ( ( m_vecGoal - GetAbsOrigin() ).Length() <= m_flGoalTolerance )
			{
				TaskComplete();
				break;
			}
			AI_NavGoal_t goal( m_vecGoal, m_bGoalRun ? ACT_RUN : ACT_WALK, m_flGoalTolerance );
			if ( GetNavigator()->SetGoal( goal ) )
				TaskComplete();
			else
				TaskFail( FAIL_NO_ROUTE );
		}
		break;

	case TASK_RPM_SET_YAW:
		GetMotor()->SetIdealYaw( m_bStandHasYaw ? m_flStandYaw : GetAbsAngles().y );
		TaskComplete();
		break;

	case TASK_RPM_WAIT:
		break;

	case TASK_RPM_TALK:
		{
			const RPMPackage_t *pPkg = GetPackage();
			CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
			if ( pPkg )
				MarkDone( *pPkg );
			if ( pPlayer && DialogDefinitions::FindDialogForEntity( GetOuter()->GetEntityName().ToCStr() ) )
			{
				RPMWorld::NoteDialogActivity( GetOuter() );
				DialogSystemServer::StartDialogForEntity( pPlayer, GetOuter() );
			}
			Evaluate( true );
			TaskComplete();
		}
		break;

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}

void CAI_RPMRoutineBehavior::RunTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
	case TASK_RPM_WAIT:
		if ( gpGlobals->curtime >= m_flWaitUntil )
			TaskComplete();
		break;

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}

//-----------------------------------------------------------------------------
void CAI_RPMRoutineBehavior::GetDebugText( char *psz, int n )
{
	static const char *s_pszStates[] = { "start", "moving", "arrived", "busy", "idle", "done" };
	const RPMPackage_t *pPkg = GetPackage();
	if ( !pPkg )
	{
		Q_snprintf( psz, n, "%s: no package (%.1f h)", GetOuter()->GetEntityName().ToCStr(), RPMWorld::GetHours() );
		return;
	}
	const char *pszHint = m_hBusyHint ? m_hBusyHint->GetEntityName().ToCStr() : "";
	Q_snprintf( psz, n, "%s: %s [%s] %s%s%s%s%s", GetOuter()->GetEntityName().ToCStr(), pPkg->name.Get(),
		RPMWorld::PackageTypeName( pPkg->type ), ( m_nState >= 0 && m_nState < ARRAYSIZE( s_pszStates ) ) ? s_pszStates[m_nState] : "?",
		m_bDelegated ? " actbusy" : "", pszHint[0] ? " @" : "", pszHint, BlockedByOtherAI() ? CFmtStr( " (blocked: %s)", BlockedReason() ).Access() : "" );
}

void RPMRoutine_DumpNPC( CAI_BaseNPC *pNPC )
{
	CAI_RPMRoutineBehavior *pRoutine = NULL;
	if ( !pNPC->GetBehavior( &pRoutine ) || !pRoutine )
		return;
	if ( !RPMWorld::FindNPCDef( pNPC->GetEntityName().ToCStr() ) )
		return;
	char sz[256];
	pRoutine->GetDebugText( sz, sizeof( sz ) );
	Msg( "  %s (%s) at %.0f %.0f %.0f\n", sz, pNPC->GetClassname(), pNPC->GetAbsOrigin().x, pNPC->GetAbsOrigin().y, pNPC->GetAbsOrigin().z );
}

//-----------------------------------------------------------------------------
AI_BEGIN_CUSTOM_SCHEDULE_PROVIDER( CAI_RPMRoutineBehavior )

	DECLARE_TASK( TASK_RPM_GET_PATH )
	DECLARE_TASK( TASK_RPM_SET_YAW )
	DECLARE_TASK( TASK_RPM_WAIT )
	DECLARE_TASK( TASK_RPM_TALK )

	DECLARE_CONDITION( COND_RPM_REEVALUATE )

	DEFINE_SCHEDULE
	(
		SCHED_RPM_MOVE,

		"	Tasks"
		"		TASK_RPM_GET_PATH			0"
		"		TASK_WAIT_FOR_MOVEMENT		0"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_SEE_ENEMY"
		"		COND_SEE_FEAR"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_HEAR_DANGER"
		"		COND_HEAR_COMBAT"
		"		COND_PROVOKED"
		"		COND_RPM_REEVALUATE"
	)

	DEFINE_SCHEDULE
	(
		SCHED_RPM_STAND,

		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_RPM_SET_YAW			0"
		"		TASK_FACE_IDEAL				0"
		"		TASK_SET_ACTIVITY			ACTIVITY:ACT_IDLE"
		"		TASK_RPM_WAIT				0"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_SEE_ENEMY"
		"		COND_SEE_FEAR"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_HEAR_DANGER"
		"		COND_HEAR_COMBAT"
		"		COND_PROVOKED"
		"		COND_GIVE_WAY"
		"		COND_PLAYER_PUSHING"
		"		COND_RPM_REEVALUATE"
	)

	DEFINE_SCHEDULE
	(
		SCHED_RPM_YIELD,

		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_WAIT					0.1"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_PROVOKED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_RPM_FAILED,

		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_SET_ACTIVITY			ACTIVITY:ACT_IDLE"
		"		TASK_WAIT					3"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_SEE_ENEMY"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
		"		COND_HEAR_DANGER"
		"		COND_PROVOKED"
		"		COND_RPM_REEVALUATE"
	)

	DEFINE_SCHEDULE
	(
		SCHED_RPM_TALK,

		"	Tasks"
		"		TASK_STOP_MOVING			0"
		"		TASK_FACE_PLAYER			0"
		"		TASK_RPM_TALK				0"
		""
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_LIGHT_DAMAGE"
		"		COND_HEAVY_DAMAGE"
	)

AI_END_CUSTOM_SCHEDULE_PROVIDER()

//=============================================================================
// HL2RPM: daily routine of an NPC - the AI packages of its definition
// (scripts/npcs/<targetname>.txt, HL2RPM Creation Kit): sleep, eat, sit,
// patrol, wander, sandbox, guard, follow, come and talk. The first package whose
// hours and conditions match runs; combat, scripts and dialogs take over as usual.
// Animations at markers (beds, chairs, tables, walls) are played through the
// NPC's actbusy behavior.
//=============================================================================
#ifndef AI_BEHAVIOR_RPMROUTINE_H
#define AI_BEHAVIOR_RPMROUTINE_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_behavior.h"

struct RPMPackage_t;
struct RPMNPCDef_t;
class CAI_ActBusyBehavior;
class CAI_Hint;

class CAI_RPMRoutineBehavior : public CAI_SimpleBehavior
{
	DECLARE_CLASS( CAI_RPMRoutineBehavior, CAI_SimpleBehavior );
	DECLARE_DATADESC();

public:
	CAI_RPMRoutineBehavior();

	virtual const char *GetName() { return "RPMRoutine"; }

	virtual bool	CanSelectSchedule();
	virtual void	GatherConditions();
	virtual void	GatherConditionsNotActive();
	virtual void	BeginScheduleSelection();
	virtual void	StartTask( const Task_t *pTask );
	virtual void	RunTask( const Task_t *pTask );
	virtual int		SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode );

	void			GetDebugText( char *psz, int n );

	enum
	{
		SCHED_RPM_MOVE = BaseClass::NEXT_SCHEDULE,
		SCHED_RPM_STAND,
		SCHED_RPM_YIELD,
		SCHED_RPM_FAILED,
		SCHED_RPM_TALK,
		NEXT_SCHEDULE,

		TASK_RPM_GET_PATH = BaseClass::NEXT_TASK,
		TASK_RPM_SET_YAW,
		TASK_RPM_WAIT,
		TASK_RPM_TALK,
		NEXT_TASK,

		COND_RPM_REEVALUATE = BaseClass::NEXT_CONDITION,
		NEXT_CONDITION,
	};

	DEFINE_CUSTOM_SCHEDULE_PROVIDER;

private:
	enum State_t
	{
		RS_START = 0,
		RS_MOVING,
		RS_ARRIVED,
		RS_BUSY,		// the actbusy behavior plays the animation for us
		RS_IDLE,
		RS_DONE,
	};

	virtual int		SelectSchedule();

	const RPMNPCDef_t	*GetDef();
	const RPMPackage_t	*GetPackage();
	CAI_ActBusyBehavior *GetActBusy();

	void			Evaluate( bool bForce );
	void			OnPackageChanged();
	bool			WantsControl();
	bool			BlockedByOtherAI();
	const char		*BlockedReason();
	void			MaintainDelegatedBusy();
	void			StopDelegatedBusy();
	Vector			ResolveLocation( const RPMPackage_t &pkg, bool *pbHasYaw = NULL, float *pflYaw = NULL );
	bool			FindWanderPoint( const Vector &center, float flRadius, Vector &result );

	// schedules for the package steps
	int				ScheduleMoveTo( const Vector &goal, float flTolerance, bool bRun );
	int				ScheduleStand( float flSeconds, bool bHasYaw, float flYaw );
	int				ScheduleUseHint( CAI_Hint *pHint, float flMaxTime );
	int				ScheduleAnimAt( const char *pszAnim, const Vector &pos, float flYaw, float flMaxTime );
	int				ScheduleSandboxStep( const RPMPackage_t &pkg, const Vector &center, float flRadius );
	int				SchedulePatrolStep( const RPMPackage_t &pkg );

	bool			IsDone( const RPMPackage_t &pkg );
	void			MarkDone( const RPMPackage_t &pkg );

	int				m_iPackage;
	string_t		m_iszPackage;		// name of the running package (index may move on reload)
	int				m_nState;
	float			m_flNextEval;
	Vector			m_vecHome;
	bool			m_bHomeSet;
	Vector			m_vecStartPos;		// where the package started (location "current")
	Vector			m_vecGoal;
	float			m_flGoalTolerance;
	bool			m_bGoalRun;
	bool			m_bStandHasYaw;
	float			m_flStandYaw;
	float			m_flWaitUntil;
	int				m_iPathPoint;
	int				m_iPathDir;
	bool			m_bDelegated;
	bool			m_bStopRequested;
	bool			m_bBusyEntered;		// the NPC got into the busy animation (not a failed walk)
	float			m_flBusyUntil;
	EHANDLE			m_hBusyHint;
	EHANDLE			m_hTempHint;
	string_t		m_iszDone;			// ";name;name;" - "once" packages already done
	float			m_flNextDebug;
	int				m_nFailures;
};

void RPMRoutine_DumpNPC( CAI_BaseNPC *pNPC );

#endif // AI_BEHAVIOR_RPMROUTINE_H

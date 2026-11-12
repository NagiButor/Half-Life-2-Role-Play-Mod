//========= HL2RPM ============================================================//
//
// The third person camera while the player sits / lies / leans on a furniture marker
// (server: hl2rpm_seat.cpp). The server sends rpm_seat_cam <body entity> <verb>; the
// camera glides out of the eyes to behind the head of the body and orbits it with the
// player's view angles (the mouse still turns the view); rpm_seat_cam 0 glides back.
//
//=============================================================================//
#include "cbase.h"
#include "view_shared.h"
#include "c_baseanimating.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar cl_rpm_seat_cam_dist( "cl_rpm_seat_cam_dist", "90", FCVAR_ARCHIVE, "HL2RPM: distance of the third person camera while sitting" );

static int s_iSeatEnt = 0;			// the body (may not exist on the client yet: looked up every frame)
static int s_iSeatVerb = 0;
static float s_flSeatT = 0.0f;			// 0..1 how far out of the eyes
static float s_flSeatLast = 0.0f;
static bool s_bSeated = false;
static Vector s_vecSeatCam;
static bool s_bSeatCamValid = false;

bool RPMSeat_IsSeatedClient()
{
	return s_bSeated || s_flSeatT > 0.0f;
}

CON_COMMAND_F( rpm_seat_cam, "HL2RPM (internal): rpm_seat_cam <body entindex> <verb> | 0 - third person while sitting (sent by the server)", FCVAR_CLIENTDLL )
{
	int ent = args.ArgC() > 1 ? atoi( args[1] ) : 0;
	if ( ent > 0 )
	{
		s_iSeatEnt = ent;
		s_iSeatVerb = args.ArgC() > 2 ? atoi( args[2] ) : 8;
		s_bSeated = true;
	}
	else
		s_bSeated = false;
}

void RPMSeat_OverrideView( CViewSetup *pSetup )
{
	float now = gpGlobals->realtime;
	float dt = clamp( now - s_flSeatLast, 0.0f, 0.1f );
	s_flSeatLast = now;
	s_flSeatT = s_bSeated ? Min( 1.0f, s_flSeatT + dt / 0.5f ) : Max( 0.0f, s_flSeatT - dt / 0.4f );
	if ( s_flSeatT <= 0.0f )
	{
		s_bSeatCamValid = false;
		return;
	}
	C_BaseEntity *pEnt = s_iSeatEnt > 0 ? ClientEntityList().GetBaseEntity( s_iSeatEnt ) : NULL;
	C_BaseAnimating *pBody = pEnt ? pEnt->GetBaseAnimating() : NULL;
	Vector head;
	if ( pBody )
	{
		int att = pBody->LookupAttachment( "eyes" );
		QAngle a;
		if ( att <= 0 || !pBody->GetAttachment( att, head, a ) )
			head = pBody->GetAbsOrigin() + Vector( 0, 0, s_iSeatVerb == 9 ? 24.0f : 48.0f );
		s_vecSeatCam = head;
		s_bSeatCamValid = true;
	}
	else if ( s_bSeatCamValid )
		head = s_vecSeatCam;
	else
		return;
	// orbit: behind the head along the view, a little above; not through walls
	Vector fwd, right, up;
	const bool bLying = s_iSeatVerb == 9;
	// lying: from above the head, looking down on the body - behind the head at eye level the camera ended low
	// at the headboard, between its bars (bone positions of this client body are not set up here: the eyes)
	if ( bLying )
	{
		head.z += 8.0f;
		pSetup->angles.x = Max( pSetup->angles.x, 30.0f );
	}
	AngleVectors( pSetup->angles, &fwd, &right, &up );
	float dist = cl_rpm_seat_cam_dist.GetFloat() * ( bLying ? 1.3f : 1.0f );
	Vector want = head - fwd * dist + up * 10.0f + right * ( bLying ? 0.0f : 14.0f );
	trace_t tr;
	// lying: only the walls of the map stop the camera - the bars of a bed (a static prop: any solid mask hits it)
	// around the head held it at the face
	if ( bLying )
	{
		CTraceFilterWorldOnly worldOnly;
		UTIL_TraceHull( head, want, Vector( -6, -6, -6 ), Vector( 6, 6, 6 ), MASK_SOLID_BRUSHONLY, &worldOnly, &tr );
	}
	else
		UTIL_TraceHull( head, want, Vector( -6, -6, -6 ), Vector( 6, 6, 6 ), MASK_SOLID & ~CONTENTS_MONSTER, pBody, COLLISION_GROUP_NONE, &tr );
	Vector cam = tr.endpos;
	float e = s_flSeatT * s_flSeatT * ( 3.0f - 2.0f * s_flSeatT );
	pSetup->origin = Lerp( e, pSetup->origin, cam );
}

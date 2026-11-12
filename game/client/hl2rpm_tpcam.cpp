//========= HL2RPM ============================================================//
//
// Third person like Fallout 4: V (toggle_view) switches between the eyes and an
// over-the-shoulder camera, smoothly; the camera keeps out of walls (a hull trace from
// the eyes), comes closer while aiming (attack2 / zoom) and when the space is tight.
// The engine's own third person mode is used underneath (the whole body and the head
// are drawn, the view model hidden, the player's animation state runs as for any
// player); only the camera position is ours (ClientModeShared::OverrideView).
//
// Convars: cl_rpm_tp (saved), cl_rpm_tp_dist / _side / _up / _aim_dist / _time, cl_rpm_tp_debug.
//
//=============================================================================//
#include "cbase.h"
#include "c_baseplayer.h"
#include "input.h"
#include "view_shared.h"
#include "in_buttons.h"
#include "engine/ivdebugoverlay.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_rpm_tp( "cl_rpm_tp", "0", FCVAR_ARCHIVE, "HL2RPM: third person view (V / toggle_view)" );
static ConVar cl_rpm_tp_dist( "cl_rpm_tp_dist", "88", FCVAR_ARCHIVE, "HL2RPM third person: distance behind the head" );
static ConVar cl_rpm_tp_side( "cl_rpm_tp_side", "19", FCVAR_ARCHIVE, "HL2RPM third person: over the right shoulder (negative - the left)" );
static ConVar cl_rpm_tp_up( "cl_rpm_tp_up", "9", FCVAR_ARCHIVE, "HL2RPM third person: above the eyes" );
static ConVar cl_rpm_tp_aim_dist( "cl_rpm_tp_aim_dist", "40", FCVAR_ARCHIVE, "HL2RPM third person: distance while aiming (attack2 / zoom)" );
static ConVar cl_rpm_tp_time( "cl_rpm_tp_time", "0.35", FCVAR_ARCHIVE, "HL2RPM third person: seconds to switch views" );
static ConVar cl_rpm_tp_debug( "cl_rpm_tp_debug", "0", 0, "HL2RPM third person: print the camera" );

extern bool RPMSeat_IsSeatedClient();

static float s_flBlend = 0.0f;			// 0 - the eyes, 1 - over the shoulder
static float s_flDist = -1.0f;			// the distance actually used (smoothed: walls, aiming)
static double s_flLastTime = 0.0;

static void ToggleView()
{
	cl_rpm_tp.SetValue( cl_rpm_tp.GetBool() ? 0 : 1 );
}
static ConCommand toggle_view( "toggle_view", ToggleView, "HL2RPM: first / third person (Fallout 4 style)" );
static ConCommand rpm_thirdperson( "rpm_thirdperson", ToggleView, "HL2RPM: first / third person (the same as toggle_view)" );

static bool ThirdPersonAllowed( C_BasePlayer *pPlayer )
{
	if ( !pPlayer || !pPlayer->IsAlive() || pPlayer->IsInAVehicle() || pPlayer->GetObserverMode() != OBS_MODE_NONE )
		return false;
	if ( RPMSeat_IsSeatedClient() )
		return false;
	return true;
}

// called from ClientModeShared::OverrideView after the stock camera (before the seat / terminal cameras)
void RPMTP_OverrideView( CViewSetup *pSetup )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	const double now = Plat_FloatTime();
	float dt = (float)( now - s_flLastTime );
	s_flLastTime = now;
	dt = clamp( dt, 0.0f, 0.1f );

	const bool bWant = cl_rpm_tp.GetBool() && ThirdPersonAllowed( pPlayer );
	const float speed = 1.0f / MAX( 0.05f, cl_rpm_tp_time.GetFloat() );
	s_flBlend = Approach( bWant ? 1.0f : 0.0f, s_flBlend, dt * speed );

	// the engine's third person (body drawn, view model hidden) while any part of the camera is out of the head
	const bool bEngineTP = s_flBlend > 0.001f;
	if ( bEngineTP != ( ::input->CAM_IsThirdPerson() != 0 ) )
	{
		if ( bEngineTP )
			::input->CAM_ToThirdPerson();
		else
			::input->CAM_ToFirstPerson();
	}
	if ( !pPlayer || s_flBlend <= 0.001f )
	{
		s_flDist = -1.0f;
		return;
	}

	// the eyes and where they look (the stock third person moved pSetup->origin already)
	QAngle ang;
	engine->GetViewAngles( ang );
	Vector eye = pPlayer->EyePosition();
	Vector fwd, right, up;
	AngleVectors( ang, &fwd, &right, &up );

	const bool bAim = ( pPlayer->m_nButtons & ( IN_ATTACK2 | IN_ZOOM ) ) != 0;
	float want = bAim ? cl_rpm_tp_aim_dist.GetFloat() : cl_rpm_tp_dist.GetFloat();
	if ( s_flDist < 0.0f )
		s_flDist = want;
	s_flDist = Approach( want, s_flDist, dt * 220.0f );

	// the shoulder first (sideways and up from the eyes), then back - each a hull trace, so the camera
	// never ends inside a wall and never looks through one
	const float t = SimpleSpline( s_flBlend );
	Vector shoulder = eye + right * ( cl_rpm_tp_side.GetFloat() * t ) + up * ( cl_rpm_tp_up.GetFloat() * t );
	const Vector hull( 5, 5, 5 );
	trace_t tr;
	UTIL_TraceHull( eye, shoulder, -hull, hull, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
	shoulder = tr.endpos;
	Vector back = shoulder - fwd * ( s_flDist * t );
	UTIL_TraceHull( shoulder, back, -hull, hull, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
	Vector cam = tr.endpos;
	if ( tr.fraction < 1.0f )
	{
		// pressed against a wall: keep that distance (no sudden jumps back when it opens up)
		const float got = ( shoulder - cam ).Length() / MAX( 0.01f, t );
		s_flDist = MIN( s_flDist, got );
	}

	pSetup->origin = cam;
	pSetup->angles = ang;
	if ( cl_rpm_tp_debug.GetBool() )
		DevMsg( "[tp] blend %.2f dist %.1f cam %.1f %.1f %.1f\n", s_flBlend, s_flDist, cam.x, cam.y, cam.z );
}

bool RPMTP_IsActive()
{
	return s_flBlend > 0.001f;
}

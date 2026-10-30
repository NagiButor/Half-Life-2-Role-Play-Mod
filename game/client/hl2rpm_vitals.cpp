//========= HL2RPM ============================================================//
//
// Purpose: How the hero feels without the HEV suit.
//
// Without the suit there are no numbers. The HUD shows the trace of a heart monitor
// (hud_vitals.cpp): calm and white while healthy; as health goes down the heart beats
// faster and harder and the line turns yellow, orange, red; near the end the rhythm
// stumbles - uneven beats, now and then one comes early. Heavy breathing comes in
// below 60 % (and for a while after any hit), the heartbeat below 45 %, in time with
// the trace.
//
// The view (hl2rpm_lowhealth_ps30, after the post-processing, before the HUD): red
// creeps in from the edges as health goes down and the vessels of the eye grow in
// with it, throbbing with each beat; a hit flashes the red for a moment. Near the
// end (below 25 %) the sight clouds over and dims from the edges in waves of a few
// seconds, as if about to pass out, the view sways and the aim wanders a little.
// Dead: the red stays and it all goes dark.
//
// Everything follows the local player's health and its maximum (networked for this)
// and whether the suit is on; nothing here is saved or sent anywhere.
//
//=============================================================================//

#include "cbase.h"
#include "hl2rpm_vitals.h"
#include "c_baseplayer.h"
#include "igamesystem.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#include "ienginevgui.h"
#include "usercmd.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "tier1/KeyValues.h"
#include "vstdlib/random.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern bool HL2RPM_IsDialogOpen();	// vgui_dialogpanel.cpp

static ConVar cl_vitals( "cl_vitals", "1", FCVAR_ARCHIVE,
	"Without the HEV suit: the heart monitor in the HUD, breathing and heartbeat, the view of a wounded man" );
static ConVar cl_vitals_screen( "cl_vitals_screen", "1", FCVAR_ARCHIVE,
	"Without the suit: red edges and the vessels of the eye as health goes down; near the end the sight clouds over and dims in waves" );
static ConVar cl_vitals_sway( "cl_vitals_sway", "1", FCVAR_ARCHIVE,
	"Without the suit, near the end: how much the view sways and the aim wanders (0 = not at all)", true, 0.0f, true, 2.0f );
static ConVar cl_vitals_breath_volume( "cl_vitals_breath_volume", "0.75", FCVAR_ARCHIVE,
	"Without the suit: volume of the heavy breathing when hurt", true, 0.0f, true, 1.0f );
static ConVar cl_vitals_heartbeat_volume( "cl_vitals_heartbeat_volume", "0.6", FCVAR_ARCHIVE,
	"Without the suit: volume of the heartbeat when badly hurt", true, 0.0f, true, 1.0f );
static ConVar cl_vitals_force_health( "cl_vitals_force_health", "-1", FCVAR_CHEAT,
	"Test: the share of health (0..1) the vitals show instead of the real one (-1 = the real one)" );
static ConVar cl_vitals_debug( "cl_vitals_debug", "0", 0,
	"Print the heart rate and the strength of the effects twice a second" );

#define VITALS_BREATH_SOUND		"#player/breathe1.wav"					// HL2's heavy breathing (it loops)
#define VITALS_HEART_SOUND		"#hl2rpm/player/heartbeat_once.wav"		// one beat of player/heartbeat1.wav (make_vitals_assets.py)
#define VITALS_VEIN_TEXTURE		"hl2rpm/effects/lowhealth_veins"		// the vessels (make_vitals_assets.py)

#define TRACE_STEP		( 1.0f / VITALS_TRACE_RATE )
#define ECG_LENGTH		0.58f	// from the start of the P wave to the end of the T wave (s, at a calm rate)
#define ECG_R_TIME		0.24f	// the top of the R wave

//-----------------------------------------------------------------------------
// One beat of the trace, t in seconds after the P wave starts (the time is scaled
// on a fast heart). The QRS in straight lines with sharp corners - as a monitor
// draws it; P and T are round bumps. 1 = the top of the R wave.
//-----------------------------------------------------------------------------
static const float s_flQRS[][2] =
{
	{ 0.170f,  0.00f },
	{ 0.190f, -0.07f },		// Q
	{ 0.240f,  1.00f },		// R
	{ 0.295f, -0.62f },		// S
	{ 0.325f,  0.02f },		// J
	{ 0.400f,  0.02f },		// ST
	{ 0.580f,  0.00f },
};

static float Bump( float t, float a, float b, float h )
{
	if ( t <= a || t >= b )
		return 0.0f;
	return h * 0.5f * ( 1.0f - cosf( 2.0f * M_PI_F * ( t - a ) / ( b - a ) ) );
}

static float EcgWave( float t )
{
	if ( t <= 0.0f || t >= ECG_LENGTH )
		return 0.0f;

	float v = Bump( t, 0.05f, 0.15f, 0.10f ) + Bump( t, 0.40f, 0.58f, 0.15f );
	for ( int i = 0; i < (int)ARRAYSIZE( s_flQRS ) - 1; i++ )
	{
		if ( t >= s_flQRS[i][0] && t < s_flQRS[i + 1][0] )
		{
			const float f = ( t - s_flQRS[i][0] ) / ( s_flQRS[i + 1][0] - s_flQRS[i][0] );
			v += Lerp( f, s_flQRS[i][1], s_flQRS[i + 1][1] );
			break;
		}
	}
	return v;
}

// the share of the way to a target this frame, for a time constant
static inline float Rate( float dt, float tau )
{
	return 1.0f - expf( -dt / Max( tau, 0.001f ) );
}

static IMaterial *MakeMaterial( const char *pszName, KeyValues *pKV )
{
	IMaterial *pMat = materials->CreateMaterial( pszName, pKV );
	if ( pMat )
		pMat->IncrementReferenceCount();
	return pMat;
}

static void SetMaterialVec( IMaterial *pMat, const char *pszVar, float a, float b, float c, float d )
{
	bool bFound = false;
	IMaterialVar *pVar = pMat->FindVar( pszVar, &bFound, false );
	if ( bFound && pVar )
		pVar->SetVecValue( a, b, c, d );
}

//-----------------------------------------------------------------------------
// The system
//-----------------------------------------------------------------------------
class CHL2RPMVitals : public CAutoGameSystemPerFrame
{
public:
	CHL2RPMVitals() : CAutoGameSystemPerFrame( "CHL2RPMVitals" )
	{
		m_pBreath = NULL;
		m_pMatDown = NULL;
		m_pMatEffect = NULL;
		m_bMaterialsTried = false;
		Reset();
	}

	virtual void Shutdown();
	virtual void LevelInitPreEntity();
	virtual void LevelShutdownPreEntity();
	virtual void Update( float frametime );

	void ApplyView( QAngle &eyeAngles );
	void CreateMove( CUserCmd *pCmd );
	void DrawScreenEffect( int x, int y, int w, int h );

	VitalsHUDState_t m_HUD;

private:
	void Reset();
	void AdvanceSample( float flCrit );
	void StartBeat( float flCrit );
	void OnBeat();
	void UpdateFaint( float flCrit );
	void UpdateSounds( C_BasePlayer *pPlayer );
	bool EnsureMaterials();

	// the heart
	bool	m_bSnap;			// take the state as it is (a level start, a load)
	bool	m_bActive;			// no suit, alive
	bool	m_bDying;			// it was so and the player died: the view goes dark
	float	m_flTime;			// game time here (stops on a pause)
	float	m_flHealth;			// 0..1, smoothed
	int		m_iLastHealth;
	float	m_flStress;			// adrenaline after a hit, 0..1
	float	m_flBPM;
	float	m_flBeatT, m_flBeatRR, m_flBeatK, m_flBeatAmp;	// this beat: time into it, its length, time scale of its shape, height
	float	m_flPrevT, m_flPrevK, m_flPrevAmp;				// the last one: its tail (T wave) may still be playing out
	bool	m_bBeatFired;		// this beat's R wave has passed
	float	m_flSampleClock;	// time not yet turned into samples
	float	m_flTraceTime;
	float	m_flTrace[ VITALS_TRACE_SAMPLES ];
	int		m_iHead;

	// what follows the beats and the hits
	float	m_flPulse;			// the view: 1 at a beat
	float	m_flBeatFlash;		// the HUD
	float	m_flDamageFlash;
	float	m_flHitRed;			// a hit flashes the red edges

	// near the end: waves of nearly passing out
	float	m_flFaint;
	float	m_flFaintStart, m_flFaintLength, m_flFaintPeak, m_flNextFaint;

	// the sway (only seen) and the wandering aim (applied to the input)
	float	m_flSway;
	float	m_flRoll, m_flPitchKick;
	QAngle	m_angDrift, m_angDriftApplied;
	bool	m_bDriftAllowed;

	// the screen pass
	float	m_flStrength, m_flDim, m_flCloud, m_flDesat, m_flSwell;
	IMaterial *m_pMatDown;
	IMaterial *m_pMatEffect;
	bool	m_bMaterialsTried;

	// sounds
	CSoundPatch *m_pBreath;
	bool	m_bBreathing;
	float	m_flBreathVolSent, m_flBreathPitchSent;
	float	m_flHeartVolume, m_flHeartPitch;
};

static CHL2RPMVitals g_Vitals;

void CHL2RPMVitals::Reset()
{
	m_bSnap = true;
	m_bActive = false;
	m_bDying = false;
	m_flTime = 0.0f;
	m_flHealth = 1.0f;
	m_iLastHealth = 0;
	m_flStress = 0.0f;
	m_flBPM = 64.0f;
	m_flBeatT = 0.0f;
	m_flBeatRR = 60.0f / m_flBPM;
	m_flBeatK = 1.0f;
	m_flBeatAmp = 0.72f;
	m_flPrevT = ECG_LENGTH;
	m_flPrevK = 1.0f;
	m_flPrevAmp = 0.0f;
	m_bBeatFired = false;
	m_flSampleClock = 0.0f;
	m_flTraceTime = 0.0f;
	for ( int i = 0; i < VITALS_TRACE_SAMPLES; i++ )
		m_flTrace[i] = 0.0f;
	m_iHead = 0;

	m_flPulse = m_flBeatFlash = m_flDamageFlash = m_flHitRed = 0.0f;
	m_flFaint = m_flFaintStart = m_flFaintLength = m_flFaintPeak = m_flNextFaint = 0.0f;
	m_flSway = m_flRoll = m_flPitchKick = 0.0f;
	m_angDrift.Init();
	m_angDriftApplied.Init();
	m_bDriftAllowed = false;
	m_flStrength = m_flDim = m_flCloud = m_flDesat = m_flSwell = 0.0f;
	m_bBreathing = false;
	m_flBreathVolSent = m_flBreathPitchSent = 0.0f;
	m_flHeartVolume = 0.0f;
	m_flHeartPitch = 100.0f;

	m_HUD.bActive = false;
	m_HUD.flHealth = 1.0f;
	m_HUD.flCritical = 0.0f;
	m_HUD.flBeatFlash = 0.0f;
	m_HUD.flDamageFlash = 0.0f;
	m_HUD.pTrace = m_flTrace;
	m_HUD.iHead = 0;
}

void CHL2RPMVitals::Shutdown()
{
	if ( m_pMatDown )
	{
		m_pMatDown->DecrementReferenceCount();
		m_pMatDown = NULL;
	}
	if ( m_pMatEffect )
	{
		m_pMatEffect->DecrementReferenceCount();
		m_pMatEffect = NULL;
	}
	m_bMaterialsTried = false;
}

void CHL2RPMVitals::LevelInitPreEntity()
{
	C_BaseEntity::PrecacheScriptSound( VITALS_BREATH_SOUND );
	C_BaseEntity::PrecacheScriptSound( VITALS_HEART_SOUND );
	Reset();
}

void CHL2RPMVitals::LevelShutdownPreEntity()
{
	if ( m_pBreath )
	{
		CSoundEnvelopeController::GetController().SoundDestroy( m_pBreath );
		m_pBreath = NULL;
	}
	Reset();
}

//-----------------------------------------------------------------------------
// The heart
//-----------------------------------------------------------------------------
void CHL2RPMVitals::StartBeat( float flCrit )
{
	float flRR = 60.0f / Max( m_flBPM, 30.0f ) * RandomFloat( 0.975f, 1.025f );
	if ( flCrit > 0.0f )
	{
		// near the end the rhythm stumbles; now and then a beat comes early
		flRR *= 1.0f + RandomFloat( -0.15f, 0.15f ) * flCrit;
		if ( RandomFloat( 0.0f, 1.0f ) < 0.08f * flCrit )
			flRR *= 0.62f;
	}
	m_flBeatRR = Max( flRR, 0.26f );
	// a fast heart squeezes its beat, though less than the pause between beats
	m_flBeatK = clamp( sqrtf( m_flBeatRR / 0.95f ), 0.6f, 1.0f );
	// and beats harder
	m_flBeatAmp = ( 0.8f + 0.2f * powf( 1.0f - m_flHealth, 1.2f ) ) * RandomFloat( 0.96f, 1.04f );
	m_bBeatFired = false;
}

void CHL2RPMVitals::OnBeat()
{
	m_flPulse = 1.0f;
	m_flBeatFlash = 1.0f;

	if ( m_flHeartVolume > 0.01f )
	{
		CLocalPlayerFilter filter;
		EmitSound_t ep;
		ep.m_nChannel = CHAN_STATIC;
		ep.m_pSoundName = VITALS_HEART_SOUND;
		ep.m_flVolume = m_flHeartVolume;
		ep.m_SoundLevel = SNDLVL_NONE;
		ep.m_nPitch = (int)m_flHeartPitch;
		C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, ep );
	}
}

void CHL2RPMVitals::AdvanceSample( float flCrit )
{
	m_flTraceTime += TRACE_STEP;
	m_flBeatT += TRACE_STEP;
	m_flPrevT += TRACE_STEP;

	if ( !m_bBeatFired && m_flBeatT >= ECG_R_TIME * m_flBeatK )
	{
		m_bBeatFired = true;
		OnBeat();
	}

	if ( m_flBeatT >= m_flBeatRR )
	{
		m_flPrevT = m_flBeatT;
		m_flPrevK = m_flBeatK;
		m_flPrevAmp = m_flBeatAmp;
		m_flBeatT -= m_flBeatRR;
		StartBeat( flCrit );
	}

	float v = EcgWave( m_flBeatT / m_flBeatK ) * m_flBeatAmp + EcgWave( m_flPrevT / m_flPrevK ) * m_flPrevAmp;
	// near the end the line isn't clean: the baseline wanders, a tremor (smooth - noise from sample
	// to sample made the glow along the line hairy)
	if ( flCrit > 0.0f )
		v += flCrit * ( 0.035f * sinf( m_flTraceTime * 2.1f ) + 0.018f * sinf( m_flTraceTime * 23.0f + 1.3f ) );

	m_flTrace[ m_iHead ] = v;
	m_iHead = ( m_iHead + 1 ) % VITALS_TRACE_SAMPLES;
}

void CHL2RPMVitals::UpdateFaint( float flCrit )
{
	if ( m_flFaintLength > 0.0f )
	{
		const float u = ( m_flTime - m_flFaintStart ) / m_flFaintLength;
		if ( u >= 1.0f )
		{
			m_flFaintLength = 0.0f;
			m_flFaint = 0.0f;
			m_flNextFaint = m_flTime + RandomFloat( 1.5f, 5.0f ) * ( 1.4f - flCrit );
		}
		else
		{
			// it comes over, holds, lets go
			const float flIn = SimpleSpline( clamp( u / 0.4f, 0.0f, 1.0f ) );
			const float flOut = 1.0f - SimpleSpline( clamp( ( u - 0.55f ) / 0.45f, 0.0f, 1.0f ) );
			m_flFaint = m_flFaintPeak * Min( flIn, flOut );
		}
	}
	else if ( flCrit <= 0.0f || !m_bActive )
	{
		// the first wave comes a moment after it gets this bad
		m_flNextFaint = m_flTime + RandomFloat( 1.0f, 2.5f );
	}
	else if ( m_flTime >= m_flNextFaint )
	{
		m_flFaintStart = m_flTime;
		m_flFaintLength = RandomFloat( 2.5f, 4.5f );
		m_flFaintPeak = flCrit * RandomFloat( 0.6f, 1.0f );
	}
}

//-----------------------------------------------------------------------------
// Breathing (a loop that follows the state) and the heartbeat (a beat at each beat)
//-----------------------------------------------------------------------------
void CHL2RPMVitals::UpdateSounds( C_BasePlayer *pPlayer )
{
	const float h = m_flHealth;

	// breathing: below 60 % and for a while after any hit; faster the worse it is
	const float flBreathe = clamp( ( 0.6f - h ) / 0.5f, 0.0f, 1.0f );
	float flVolume = 0.0f;
	if ( m_bActive )
	{
		flVolume = Max( ( flBreathe > 0.0f ) ? 0.25f + 0.75f * flBreathe : 0.0f, 0.35f * m_flStress );
		flVolume = Min( 1.0f, flVolume * ( 1.0f + 0.3f * m_flFaint ) ) * cl_vitals_breath_volume.GetFloat();
	}
	const float flPitch = 94.0f + 20.0f * flBreathe + 8.0f * m_flStress;

	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( flVolume > 0.02f && pPlayer )
	{
		if ( !m_pBreath )
		{
			CLocalPlayerFilter filter;
			m_pBreath = controller.SoundCreate( filter, pPlayer->entindex(), CHAN_STATIC, VITALS_BREATH_SOUND, SNDLVL_NONE );
		}
		if ( m_pBreath )
		{
			if ( !m_bBreathing )
			{
				controller.Play( m_pBreath, 0.0f, flPitch );
				controller.SoundChangeVolume( m_pBreath, flVolume, 1.0f );
				m_bBreathing = true;
				m_flBreathVolSent = flVolume;
				m_flBreathPitchSent = flPitch;
			}
			else if ( fabsf( flVolume - m_flBreathVolSent ) > 0.02f || fabsf( flPitch - m_flBreathPitchSent ) > 1.0f )
			{
				controller.SoundChangeVolume( m_pBreath, flVolume, 0.5f );
				controller.SoundChangePitch( m_pBreath, flPitch, 0.5f );
				m_flBreathVolSent = flVolume;
				m_flBreathPitchSent = flPitch;
			}
		}
	}
	else if ( m_bBreathing )
	{
		controller.SoundFadeOut( m_pBreath, m_bDying ? 0.4f : 1.5f, false );
		m_bBreathing = false;
	}

	// the heartbeat: below 45 %, louder the worse it is (each beat plays it - OnBeat)
	const float flHeart = clamp( ( 0.45f - h ) / 0.35f, 0.0f, 1.0f );
	m_flHeartVolume = 0.0f;
	if ( m_bActive && flHeart > 0.0f )
		m_flHeartVolume = Min( 1.0f, ( 0.3f + 0.7f * flHeart ) * ( 1.0f + 0.25f * m_flFaint ) ) * cl_vitals_heartbeat_volume.GetFloat();
	m_flHeartPitch = clamp( 92.0f + ( m_flBPM - 70.0f ) * 0.25f, 90.0f, 115.0f );
}

//-----------------------------------------------------------------------------
// Every frame
//-----------------------------------------------------------------------------
void CHL2RPMVitals::Update( float frametime )
{
	C_BasePlayer *pPlayer = engine->IsInGame() ? C_BasePlayer::GetLocalPlayer() : NULL;
	const float dt = clamp( frametime, 0.0f, 0.1f );
	m_flTime += dt;

	const bool bSuitless = cl_vitals.GetBool() && pPlayer && !pPlayer->IsSuitEquipped();
	const bool bAlive = pPlayer && pPlayer->IsAlive();
	m_bDying = bSuitless && !bAlive && ( m_bActive || m_bDying );
	m_bActive = bSuitless && bAlive;

	// health: a share of the maximum; falls at once, comes back slowly
	float flRaw = 1.0f;
	if ( pPlayer )
	{
		int iMax = pPlayer->GetMaxHealth();
		if ( iMax <= 1 )
			iMax = 100;
		const int iHealth = Max( pPlayer->GetHealth(), 0 );
		flRaw = clamp( (float)iHealth / iMax, 0.0f, 1.0f );

		if ( m_bActive && !m_bSnap && iHealth < m_iLastHealth )
		{
			// a hit: the heart jumps, the HUD box flashes, the red edges too
			const float flLoss = (float)( m_iLastHealth - iHealth ) / iMax;
			m_flStress = Min( 1.0f, m_flStress + 0.15f + 2.5f * flLoss );
			m_flDamageFlash = 1.0f;
			m_flHitRed = Min( 1.0f, m_flHitRed + 0.35f + 2.0f * flLoss );
		}
		m_iLastHealth = iHealth;
	}
	if ( cl_vitals_force_health.GetFloat() >= 0.0f )
		flRaw = clamp( cl_vitals_force_health.GetFloat(), 0.0f, 1.0f );
	if ( m_bDying )
		flRaw = 0.0f;

	const bool bSnap = m_bSnap && pPlayer;
	if ( bSnap )
	{
		m_bSnap = false;
		m_flHealth = flRaw;
	}
	else
	{
		m_flHealth += ( flRaw - m_flHealth ) * Rate( dt, ( flRaw < m_flHealth ) ? 0.12f : 1.2f );
	}

	const float h = m_flHealth;
	const float flCrit = clamp( ( 0.25f - h ) / 0.2f, 0.0f, 1.0f );

	// the heart rate: faster the worse it is, faster still for a while after a hit
	const float flTargetBPM = Min( 64.0f + 82.0f * powf( 1.0f - h, 1.35f ) + 38.0f * m_flStress, 185.0f );
	if ( bSnap )
	{
		m_flBPM = flTargetBPM;
		StartBeat( flCrit );
	}
	else
	{
		m_flBPM += ( flTargetBPM - m_flBPM ) * Rate( dt, 1.5f );
	}
	m_flStress *= expf( -dt / 9.0f );

	// the trace (and the beats) - while there is a heart to show
	if ( m_bActive )
	{
		m_flSampleClock += dt;
		int nSamples = 0;
		while ( m_flSampleClock >= TRACE_STEP )
		{
			m_flSampleClock -= TRACE_STEP;
			AdvanceSample( flCrit );
			if ( ++nSamples >= VITALS_TRACE_SAMPLES )
			{
				m_flSampleClock = 0.0f;
				break;
			}
		}
	}

	m_flPulse *= expf( -dt / 0.2f );
	m_flBeatFlash *= expf( -dt / 0.3f );
	m_flDamageFlash *= expf( -dt / 0.5f );
	m_flHitRed *= expf( -dt / 0.45f );

	UpdateFaint( flCrit );

	// the view
	const float flRed = powf( clamp( ( 0.7f - h ) / 0.62f, 0.0f, 1.0f ), 1.1f );
	if ( m_bActive )
	{
		m_flStrength = clamp( flRed + m_flHitRed * 0.3f * ( 1.0f - flRed ), 0.0f, 1.0f );
		m_flDim = Min( 0.9f, flCrit * 0.15f + m_flFaint * 0.6f + m_flPulse * flCrit * 0.06f );
		m_flCloud = Min( 1.0f, flCrit * 0.2f + m_flFaint * 0.8f );
		m_flDesat = Min( 1.0f, flCrit * 0.2f + m_flFaint * 0.25f );
		m_flSwell = 0.004f * flRed + 0.006f * flCrit;
	}
	else if ( m_bDying )
	{
		// dead: the red stays, it all goes dark
		m_flStrength = Approach( 1.0f, m_flStrength, dt / 1.5f );
		m_flDim = Approach( 0.92f, m_flDim, dt / 2.5f );
		m_flCloud = Approach( 1.0f, m_flCloud, dt / 1.0f );
		m_flDesat = Approach( 0.6f, m_flDesat, dt / 2.0f );
		m_flSwell = 0.0f;
	}
	else
	{
		// the suit is on (or no player): gone in a moment
		m_flStrength = Approach( 0.0f, m_flStrength, dt / 0.3f );
		m_flDim = Approach( 0.0f, m_flDim, dt / 0.3f );
		m_flCloud = Approach( 0.0f, m_flCloud, dt / 0.3f );
		m_flDesat = Approach( 0.0f, m_flDesat, dt / 0.3f );
		m_flSwell = 0.0f;
	}

	// near the end the view sways and the aim wanders: slow, uneven waves, more in a fainting wave
	const float flSwayTarget = m_bActive ? cl_vitals_sway.GetFloat() * powf( flCrit, 1.2f ) * ( 0.65f + 0.7f * m_flFaint ) : 0.0f;
	m_flSway += ( flSwayTarget - m_flSway ) * Rate( dt, 1.0f );
	const float t = m_flTime * 2.0f * M_PI_F;
	m_flRoll = m_flSway * 2.2f * ( 0.6f * sinf( t * 0.23f ) + 0.4f * sinf( t * 0.37f + 1.3f ) );
	m_angDrift.x = m_flSway * 0.5f * ( 0.6f * sinf( t * 0.21f + 2.7f ) + 0.4f * sinf( t * 0.33f + 0.9f ) );
	m_angDrift.y = m_flSway * 0.8f * ( 0.6f * sinf( t * 0.17f + 0.5f ) + 0.4f * sinf( t * 0.29f + 2.1f ) );
	m_angDrift.z = 0.0f;
	// (each beat nods the view a little)
	m_flPitchKick = m_bActive ? -0.15f * flCrit * m_flPulse * cl_vitals_sway.GetFloat() : 0.0f;
	m_bDriftAllowed = m_bActive && !pPlayer->IsInAVehicle() && !engine->IsPaused() && !enginevgui->IsGameUIVisible()
		&& !vgui::surface()->IsCursorVisible() && !HL2RPM_IsDialogOpen();

	UpdateSounds( pPlayer );

	m_HUD.bActive = m_bActive;
	m_HUD.flHealth = h;
	m_HUD.flCritical = flCrit;
	m_HUD.flBeatFlash = m_flBeatFlash;
	m_HUD.flDamageFlash = m_flDamageFlash;
	m_HUD.pTrace = m_flTrace;
	m_HUD.iHead = m_iHead;

	if ( cl_vitals_debug.GetBool() )
	{
		static float s_flNextPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextPrint || gpGlobals->realtime < s_flNextPrint - 1.0f )
		{
			s_flNextPrint = gpGlobals->realtime + 0.5f;
			Msg( "[vitals] %s health %.2f (now %.2f), critical %.2f | %.0f bpm, stress %.2f | red %.2f dim %.2f cloud %.2f faint %.2f sway %.2f | breath %.2f heart %.2f\n",
				m_bActive ? "active" : ( m_bDying ? "dying" : "off" ), h, flRaw, flCrit, m_flBPM, m_flStress,
				m_flStrength, m_flDim, m_flCloud, m_flFaint, m_flSway, m_bBreathing ? m_flBreathVolSent : 0.0f, m_flHeartVolume );
		}
	}
}

//-----------------------------------------------------------------------------
// The sway is only seen: a roll keeps the middle of the screen where the aim is
//-----------------------------------------------------------------------------
void CHL2RPMVitals::ApplyView( QAngle &eyeAngles )
{
	if ( HL2RPM_IsDialogOpen() )
		return;
	eyeAngles.z += m_flRoll;
	eyeAngles.x += m_flPitchKick;
}

//-----------------------------------------------------------------------------
// The aim wanders: the change of the drift since the last input sample is added
// to the view angles (both the per-frame and the per-tick samples come here)
//-----------------------------------------------------------------------------
void CHL2RPMVitals::CreateMove( CUserCmd *pCmd )
{
	const QAngle angDelta = m_angDrift - m_angDriftApplied;
	m_angDriftApplied = m_angDrift;
	if ( !pCmd || !m_bDriftAllowed )
		return;

	pCmd->viewangles += angDelta;
	pCmd->viewangles.x = clamp( pCmd->viewangles.x, -89.0f, 89.0f );
}

//-----------------------------------------------------------------------------
// The wounded view (hl2rpm_lowhealth_ps30) over the finished frame
//-----------------------------------------------------------------------------
bool CHL2RPMVitals::EnsureMaterials()
{
	if ( !m_bMaterialsTried )
	{
		m_bMaterialsTried = true;
		for ( int iPass = 0; iPass < 2; iPass++ )
		{
			KeyValues *pKV = new KeyValues( "HL2RPM_LOWHEALTH" );
			pKV->SetInt( "$pass", iPass );
			pKV->SetString( "$effectparams", "[0 0 0 0]" );
			pKV->SetString( "$effectparams2", "[0 0 0 0]" );
			pKV->SetString( "$viewrect", "[0 0 1 1]" );
			pKV->SetString( "$texelsize", "[0 0 0 0]" );
			pKV->SetString( "$blurrect", "[0 0 1 1]" );
			if ( iPass == 1 )
			{
				pKV->SetString( "$veintexture", VITALS_VEIN_TEXTURE );
				pKV->SetString( "$blurtexture", "_rt_SmallFB0" );
				m_pMatEffect = MakeMaterial( "__hl2rpm_lowhealth", pKV );
			}
			else
			{
				m_pMatDown = MakeMaterial( "__hl2rpm_lowhealth_down", pKV );
			}
		}
	}
	return m_pMatDown && !m_pMatDown->IsErrorMaterial() && m_pMatEffect && !m_pMatEffect->IsErrorMaterial();
}

void CHL2RPMVitals::DrawScreenEffect( int x, int y, int w, int h )
{
	if ( !cl_vitals_screen.GetBool() || w < 8 || h < 8 )
		return;
	if ( m_flStrength < 0.002f && m_flDim < 0.002f && m_flCloud < 0.002f )
		return;
	if ( !EnsureMaterials() )
		return;

	ITexture *pFrame = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	if ( !pFrame || pFrame->IsError() )
		return;
	const int fbw = pFrame->GetActualWidth(), fbh = pFrame->GetActualHeight();

	// clouding: a quarter size copy of the frame (the engine's bloom target - its bloom is done by now)
	ITexture *pSmall = NULL;
	if ( m_flCloud > 0.002f )
	{
		pSmall = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
		if ( pSmall && pSmall->IsError() )
			pSmall = NULL;
	}

	CMatRenderContextPtr pRenderContext( materials );

	// the frame as it is now
	Rect_t rect;
	rect.x = x;
	rect.y = y;
	rect.width = w;
	rect.height = h;
	pRenderContext->CopyRenderTargetToTextureEx( pFrame, 0, &rect, &rect );
	// (the shader binds "the frame buffer copy": make it this copy)
	pRenderContext->SetFrameBufferCopyTexture( pFrame, 0 );

	int qw = 1, qh = 1, sw = 1, sh = 1;
	if ( pSmall )
	{
		sw = pSmall->GetActualWidth();
		sh = pSmall->GetActualHeight();
		qw = clamp( w / 4, 1, sw );
		qh = clamp( h / 4, 1, sh );
		SetMaterialVec( m_pMatDown, "$texelsize", 1.0f / fbw, 1.0f / fbh, 0.0f, 0.0f );
		pRenderContext->PushRenderTargetAndViewport( pSmall, 0, 0, qw, qh );
		pRenderContext->DrawScreenSpaceRectangle( m_pMatDown, 0, 0, qw, qh, x, y, x + w - 1, y + h - 1, fbw, fbh );
		pRenderContext->PopRenderTargetAndViewport();
	}

	SetMaterialVec( m_pMatEffect, "$effectparams", m_flStrength, m_flPulse, m_flDim, pSmall ? m_flCloud : 0.0f );
	SetMaterialVec( m_pMatEffect, "$effectparams2", m_flDesat, m_flSwell, 0.0f, 0.0f );
	SetMaterialVec( m_pMatEffect, "$viewrect", (float)x / fbw, (float)y / fbh, (float)w / fbw, (float)h / fbh );
	SetMaterialVec( m_pMatEffect, "$texelsize", 1.0f / fbw, 1.0f / fbh, 1.0f / sw, 1.0f / sh );
	SetMaterialVec( m_pMatEffect, "$blurrect", 0.0f, 0.0f, (float)qw / sw, (float)qh / sh );
	pRenderContext->DrawScreenSpaceRectangle( m_pMatEffect, x, y, w, h, x, y, x + w - 1, y + h - 1, fbw, fbh );
}

//-----------------------------------------------------------------------------
// Interface
//-----------------------------------------------------------------------------
const VitalsHUDState_t &HL2RPM_Vitals_GetHUDState()
{
	return g_Vitals.m_HUD;
}

void HL2RPM_Vitals_ApplyView( QAngle &eyeAngles )
{
	g_Vitals.ApplyView( eyeAngles );
}

void HL2RPM_Vitals_CreateMove( CUserCmd *pCmd )
{
	g_Vitals.CreateMove( pCmd );
}

void HL2RPM_Vitals_DrawScreenEffect( int x, int y, int w, int h )
{
	g_Vitals.DrawScreenEffect( x, y, w, h );
}

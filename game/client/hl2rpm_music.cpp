//========= HL2RPM ============================================================//
//
// Music of the Creation Kit (client). The server decides the mood and its tracks
// (hl2rpm_audio.cpp: exploration day / night, combat, location, override) and sends
//   rpm_music_set "<mood>~file:volume;file:volume"   rpm_music_stinger "file:volume"
// The client crossfades between the tracks of the mood, picks the next one when a
// track ends (exploration: after a pause, like New Vegas), ducks the music under a
// stinger. Tracks play as "#<file>": the engine treats them as music, so the music
// volume of the game options (snd_musicvolume) applies; snd_rpm_music on top.
//
//=============================================================================//
#include "cbase.h"
#include "engine/IEngineSound.h"
#include "soundflags.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar snd_rpm_music( "snd_rpm_music", "0.8", FCVAR_ARCHIVE, "HL2RPM: volume of the Creation Kit music (on top of snd_musicvolume)", true, 0.0f, true, 1.0f );
ConVar snd_rpm_music_debug( "snd_rpm_music_debug", "0", 0, "HL2RPM: print the music the client plays" );

struct RPMMusicTrack_t
{
	CUtlString file;
	float volume;
};

struct RPMMusicVoice_t
{
	CUtlString name;		// "#music/x.mp3"
	float base;				// track volume
	float cur;				// 0..1 of the fade
	float target;
	float rate;				// per second
	float sent;				// last volume sent to the engine
	float ends;				// game time the track ends (0 - a stinger / unknown)
	bool stinger;
};

class CRPMMusic : public CAutoGameSystemPerFrame
{
public:
	CRPMMusic() : CAutoGameSystemPerFrame( "CRPMMusic" ) {}

	CUtlString m_Mood;
	CUtlVector<RPMMusicTrack_t> m_List;
	CUtlVector<RPMMusicVoice_t> m_Voices;
	float m_flNextStart = 0.0f;
	int m_iLast = -1;
	float m_flDuckUntil = 0.0f;
	float m_flDuck = 1.0f;

	virtual void LevelShutdownPreEntity()
	{
		StopAll();
		m_Mood.Clear();
		m_List.RemoveAll();
	}

	void StopAll()
	{
		for ( int i = 0; i < m_Voices.Count(); i++ )
			enginesound->EmitAmbientSound( m_Voices[i].name, 0.0f, PITCH_NORM, SND_STOP );
		m_Voices.RemoveAll();
	}

	static float Now() { return gpGlobals->realtime; }

	bool IsPlaying( const char *file )
	{
		for ( int i = 0; i < m_Voices.Count(); i++ )
			if ( !m_Voices[i].stinger && m_Voices[i].target > 0.0f && !Q_stricmp( m_Voices[i].name.Get() + 1, file ) )
				return true;
		return false;
	}

	void FadeOutAll( float seconds )
	{
		for ( int i = 0; i < m_Voices.Count(); i++ )
		{
			if ( m_Voices[i].stinger )
				continue;
			m_Voices[i].target = 0.0f;
			m_Voices[i].rate = 1.0f / Max( 0.1f, seconds );
		}
	}

	void Start( const RPMMusicTrack_t &t, float fadeIn, bool bStinger )
	{
		RPMMusicVoice_t v;
		v.name.Format( "#%s", t.file.Get() );
		// the engine knows ambient sounds by name: a second copy of a track still fading out would be
		// stopped together with it - take the fading one back instead (or restart a stinger)
		for ( int i = 0; i < m_Voices.Count(); i++ )
		{
			if ( Q_stricmp( m_Voices[i].name, v.name ) )
				continue;
			if ( !bStinger && !m_Voices[i].stinger )
			{
				m_Voices[i].target = 1.0f;
				m_Voices[i].rate = 1.0f / Max( 0.1f, fadeIn );
				m_Voices[i].base = t.volume;
				if ( snd_rpm_music_debug.GetBool() )
					Msg( "[music] resume %s\n", t.file.Get() );
				return;
			}
			enginesound->EmitAmbientSound( m_Voices[i].name, 0.0f, PITCH_NORM, SND_STOP );
			m_Voices.Remove( i );
			break;
		}
		v.base = t.volume;
		v.cur = bStinger ? 1.0f : 0.0f;
		v.target = 1.0f;
		v.rate = 1.0f / Max( 0.1f, fadeIn );
		v.sent = -1.0f;
		v.stinger = bStinger;
		enginesound->PrecacheSound( v.name, true );
		float dur = enginesound->GetSoundDuration( t.file );
		v.ends = dur > 0.5f ? Now() + dur : 0.0f;
		enginesound->EmitAmbientSound( v.name, Volume( v ), PITCH_NORM, 0 );
		v.sent = Volume( v );
		m_Voices.AddToTail( v );
		if ( snd_rpm_music_debug.GetBool() )
			Msg( "[music] %s %s (%.0f s)\n", bStinger ? "stinger" : "play", t.file.Get(), dur );
	}

	float Volume( const RPMMusicVoice_t &v )
	{
		float duck = v.stinger ? 1.0f : m_flDuck;
		return clamp( v.base * v.cur * duck * snd_rpm_music.GetFloat(), 0.0f, 1.0f );
	}

	void PlayNext( float fadeIn )
	{
		if ( !m_List.Count() )
			return;
		int i = RandomInt( 0, m_List.Count() - 1 );
		if ( m_List.Count() > 1 && i == m_iLast )
			i = ( i + 1 ) % m_List.Count();
		m_iLast = i;
		Start( m_List[i], fadeIn, false );
	}

	void Set( const char *msg )
	{
		const char *tilde = Q_strstr( msg, "~" );
		CUtlString mood;
		mood.SetDirect( msg, tilde ? (int)( tilde - msg ) : Q_strlen( msg ) );
		CUtlVector<RPMMusicTrack_t> list;
		CUtlStringList parts;
		V_SplitString( tilde ? tilde + 1 : "", ";", parts );
		for ( int i = 0; i < parts.Count(); i++ )
		{
			RPMMusicTrack_t t;
			char *colon = Q_strrchr( parts[i], ':' );
			t.volume = 1.0f;
			if ( colon && colon > parts[i] + 1 )
			{
				t.volume = atof( colon + 1 );
				*colon = 0;
			}
			t.file = parts[i];
			if ( !t.file.IsEmpty() )
				list.AddToTail( t );
		}
		const bool bCombat = !Q_stricmp( mood, "combat" );
		const bool bWasCombat = !Q_stricmp( m_Mood, "combat" );
		m_Mood = mood;
		m_List.RemoveAll();
		m_List.AddVectorToTail( list );
		m_iLast = -1;
		// the track that plays now belongs to the new mood too: let it play
		for ( int i = 0; i < m_List.Count(); i++ )
			if ( IsPlaying( m_List[i].file ) )
			{
				m_iLast = i;
				return;
			}
		FadeOutAll( bCombat ? 1.5f : ( bWasCombat ? 4.0f : 3.0f ) );
		m_flNextStart = 0.0f;
		if ( m_List.Count() )
			PlayNext( bCombat ? 1.0f : 3.0f );
	}

	void Stinger( const char *msg )
	{
		RPMMusicTrack_t t;
		char buf[256];
		Q_strncpy( buf, msg, sizeof( buf ) );
		char *colon = Q_strrchr( buf, ':' );
		t.volume = 1.0f;
		if ( colon && colon > buf + 1 )
		{
			t.volume = atof( colon + 1 );
			*colon = 0;
		}
		t.file = buf;
		Start( t, 0.1f, true );
		float dur = enginesound->GetSoundDuration( t.file );
		m_flDuckUntil = Now() + ( dur > 0.5f ? dur : 4.0f );
	}

	virtual void Update( float frametime )
	{
		if ( !engine->IsInGame() )
			return;
		const float now = Now();
		const float dt = Max( 0.0f, gpGlobals->absoluteframetime );
		// duck under a stinger
		float duckTarget = now < m_flDuckUntil ? 0.35f : 1.0f;
		m_flDuck = Approach( duckTarget, m_flDuck, dt * 1.5f );
		bool bMain = false;
		for ( int i = m_Voices.Count() - 1; i >= 0; i-- )
		{
			RPMMusicVoice_t &v = m_Voices[i];
			v.cur = Approach( v.target, v.cur, v.rate * dt );
			bool bEnded = v.ends > 0.0f && now >= v.ends;
			if ( ( v.target <= 0.0f && v.cur <= 0.0f ) || bEnded )
			{
				if ( snd_rpm_music_debug.GetBool() )
					Msg( "[music] stop %s (%s, now %.1f, ends %.1f)\n", v.name.Get(), bEnded ? "ended" : "faded out", now, v.ends );
				enginesound->EmitAmbientSound( v.name, 0.0f, PITCH_NORM, SND_STOP );
				m_Voices.Remove( i );
				continue;
			}
			// the end of a track: fade the last seconds (the next one comes after a pause / at once in combat)
			if ( !v.stinger && v.ends > 0.0f && now >= v.ends - 3.0f && v.target > 0.0f )
			{
				v.target = 0.0f;
				v.rate = 1.0f / 3.0f;
				const bool bCombat = !Q_stricmp( m_Mood, "combat" );
				m_flNextStart = now + ( bCombat ? 2.5f : RandomFloat( 12.0f, 40.0f ) );
			}
			float vol = Volume( v );
			if ( fabsf( vol - v.sent ) > 0.01f )
			{
				enginesound->EmitAmbientSound( v.name, vol, PITCH_NORM, SND_CHANGE_VOL );
				v.sent = vol;
			}
			if ( !v.stinger && v.target > 0.0f )
				bMain = true;
		}
		if ( !bMain && m_List.Count() && m_flNextStart > 0.0f && now >= m_flNextStart )
		{
			m_flNextStart = 0.0f;
			PlayNext( 3.0f );
		}
	}
};

static CRPMMusic g_RPMMusic;

CON_COMMAND( rpm_music_set, "HL2RPM (server -> client): \"<mood>~file:volume;...\" - the music of the moment" )
{
	if ( args.ArgC() < 2 )
		return;
	if ( snd_rpm_music_debug.GetBool() )
		Msg( "[music] set %s\n", args[1] );
	g_RPMMusic.Set( args[1] );
}

CON_COMMAND( rpm_music_stinger, "HL2RPM (server -> client): \"file:volume\" - a short piece over the music" )
{
	if ( args.ArgC() >= 2 && args[1][0] )
		g_RPMMusic.Stinger( args[1] );
}

CON_COMMAND( rpm_music_status, "HL2RPM: what the client music plays" )
{
	Msg( "mood '%s', %d track(s) in the mood, duck %.2f\n", g_RPMMusic.m_Mood.Get(), g_RPMMusic.m_List.Count(), g_RPMMusic.m_flDuck );
	for ( int i = 0; i < g_RPMMusic.m_Voices.Count(); i++ )
	{
		const RPMMusicVoice_t &v = g_RPMMusic.m_Voices[i];
		Msg( "  %s  fade %.2f -> %.2f  volume %.2f%s\n", v.name.Get(), v.cur, v.target, v.sent, v.stinger ? "  (stinger)" : "" );
	}
}

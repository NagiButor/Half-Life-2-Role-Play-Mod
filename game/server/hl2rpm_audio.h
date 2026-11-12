//========= HL2RPM ============================================================//
//
// Sound of the Creation Kit (server): what HL2 NPCs may say (no lines about
// Gordon Freeman, stock chatter per NPC), barks of NPCs with conditions and
// subtitles, music (exploration day / night, combat, locations, overrides,
// stingers) and the locations of the map layer (discovery, XP).
//
//=============================================================================//
#ifndef HL2RPM_AUDIO_H
#define HL2RPM_AUDIO_H
#pragma once

class CBaseEntity;

namespace RPMSpeech
{
	// a response of the HL2 response system the NPC is about to say: false - it stays silent
	// (a line about Freeman of scripts/hl2rpm_speech_block.txt, or the NPC's "stock_speech" says no)
	bool Allow( CBaseEntity *pSpeaker, const char *pszConcept, const char *pszResponse );
}

namespace RPMAudio
{
	void NoteDeath( CBaseEntity *pVictim, CBaseEntity *pAttacker );		// "ally_death" barks
	void NoteDialogEnd( CBaseEntity *pNPC );							// "farewell" barks
	void NotePackage( CBaseEntity *pNPC, const char *pszPackage );		// "package" barks
	bool Bark( CBaseEntity *pNPC, const char *pszEvent, const char *pszValue = "" );	// say a line of that event now
	// say a line that is not a bark of the Creation Kit (built-in lines: the weapon ban): sound + subtitle
	bool Say( CBaseEntity *pNPC, const char *pszText, const char *pszSound, bool bForce = false );	// bForce: over a line it is saying
	void ZoneEnter( int iZone );		// locations: discovery, XP
	void SetMusicOverride( const char *pszTracks );	// action "music": track ids (;), "" = back to automatic
	void Stinger( const char *pszTrack );			// action "stinger" / events: a short piece over the music
	void StingerEvent( const char *pszEvent );		// quest_complete, quest_fail, location, ...
	bool InCombat();								// an enemy fights the player (combat music)
}

#endif // HL2RPM_AUDIO_H

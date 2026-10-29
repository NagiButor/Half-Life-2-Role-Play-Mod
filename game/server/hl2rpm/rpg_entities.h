//========= HL2RPM ============//
//
// RPG-система: энтити карт (RPG_SPEC §7, §11): logic_rpg, logic_rpg_watch, trigger_rpg,
// trigger_rpg_zone, prop_rpg_usable. Здесь - функции, которые зовёт система персонажа.
//
//=============================//

#ifndef RPG_ENTITIES_H
#define RPG_ENTITIES_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;

// смена уровня подозрения -> выходы logic_rpg_watch (OnSuspicion25/50/75/100, OnSuspicionCalm)
void RPG_FireWatchOutputs( int iOldTier, int iNewTier );
// logic_rpg с test_on_spawn: проверить условие после загрузки карты (не на загрузке сохранения)
void RPG_RunSpawnTests();
// зона контроля Альянса, в которой стоит игрок (RPGZone_t)
int RPG_GetPlayerZone( CBasePlayer *pPlayer );
// тревога: NPC с rpg_faction cp/overwatch враждебны игроку (или возвращаются к прежнему)
void RPG_ApplyAlarm( bool bOn );
// сон: rpg_sleep <часы> после +use на кровати (prop_rpg_usable use_action sleep)
bool RPG_Sleep( CBasePlayer *pPlayer, float flHours, CUtlString *pError );

#endif // RPG_ENTITIES_H

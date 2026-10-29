//========= HL2RPM ============//
//
// RPG-система (сервер): закон Альянса - что делает Гражданская оборона с подозрением
// (Part I/06 §3, Part I/07 §3, RPG_SPEC §2 «Подозрение и закон»). См. rpg_law.cpp.
//
//=============================//

#ifndef RPG_LAW_H
#define RPG_LAW_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;
class CBaseEntity;
class CTakeDamageInfo;

// тревога включилась / снялась (CRPGCharacter::SetAlarm): голос Надзора, подсказка
void RPG_Law_OnAlarm( bool bOn );
// урон игроку (CBasePlayer::OnTakeDamage): удар ГО, который уложил бы героя, - задержание
// (урон урезается); во время задержания урон от ГО - 0. true - info изменён
bool RPG_Law_FilterPlayerDamage( CBasePlayer *pPlayer, CTakeDamageInfo &info );
// эффект диалога law=<действие>: release, frisk, arrest, flee, fine:N, pay_debt
void RPG_Law_Effect( CBasePlayer *pPlayer, CBaseEntity *pNPC, const char *pszAction );
// условие law <что>: contraband, corrupt, debt, record, papers; 1/0, -1 - неизвестно
int RPG_Law_Query( CBasePlayer *pPlayer, CBaseEntity *pNPC, const char *pszWhat );
// метрокоп оттолкнул героя / сделал замечание (CNPC_MetroPolice::IncrementPlayerCriminalStatus):
// подозрение вместо своего счёта замечаний юнита; false - закон выключен
bool RPG_Law_CopWarned( CBaseEntity *pCop, CBasePlayer *pPlayer );

#endif // RPG_LAW_H

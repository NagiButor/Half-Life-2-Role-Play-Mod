//========= HL2RPM ============//
//
// RPG-система (сервер): квесты v2 (RPG_SPEC §6). Определения - shared/hl2rpm/rpg_quest_data.h,
// состояние - флаги персонажа q_<id>*. Цели продвигаются событиями RPG (talk/reach/event),
// флагами, инвентарём и убийствами. Метка отслеживаемого квеста - локатор HL2 (компас HUD).
// Квесты старого формата (блоки "quests" в диалогах тестовых карт) остаются в CQuestSystem.
//
//=============================//

#ifndef RPG_QUESTS_H
#define RPG_QUESTS_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_quest_data.h"

class CBasePlayer;

namespace RPGQuests
{
	// есть ли такой квест v2 (иначе - старый CQuestSystem)
	bool IsV2( const char *pszId );

	bool Grant( CBasePlayer *pPlayer, const char *pszId );
	bool Complete( CBasePlayer *pPlayer, const char *pszId );
	bool Fail( CBasePlayer *pPlayer, const char *pszId );
	bool SetStage( CBasePlayer *pPlayer, const char *pszId, int iStage );
	bool Track( CBasePlayer *pPlayer, const char *pszId );		// "" - снять

	int GetState( const char *pszId );
	int GetStage( const char *pszId );

	void Reload();
}

#endif // RPG_QUESTS_H

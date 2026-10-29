//========= HL2RPM ============//
//
// RPG-система (сервер): диалоги v2 (RPG_SPEC §5). Файлы scripts/dialogs/*.txt с ключом "voice"
// или "greetings" (их пишет build_content.py) идут сюда, остальные - старой системе
// (dialog_definitions.cpp, тестовые карты). Клиент - client/hl2rpm/rpg_dialog_panel.cpp.
//
// «Уже говорили» и «испорчен» - флаги персонажа dlg_spoken_<entity> / dlg_spoiled_<entity>
// (сохраняются и переживают переходы между картами).
//
//=============================//

#ifndef RPG_DIALOG_H
#define RPG_DIALOG_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;
class CBaseEntity;
class KeyValues;

namespace RPGDialog
{
	// перечитать все диалоги v2 (вызывается при загрузке карты и dialog_reload)
	void Load();
	// файл диалога v2? (есть voice/greetings) - старый загрузчик такие пропускает
	bool IsV2File( KeyValues *pKV );

	// есть ли диалог v2 у энтити (точное имя или шаблон вида npc_cp_*)
	bool Has( const char *pszEntityName );
	// подпись собеседника для подсказки у прицела; NULL - сейчас с ним не поговорить
	const char *GetSpeaker( CBaseEntity *pNPC );
	// начать разговор; iNode >= 0 - с этого узла (возврат после экрана интерфейса)
	bool Start( CBasePlayer *pPlayer, CBaseEntity *pNPC, int iNode = -1 );
	bool IsActive();
	void Stop();
}

#endif // RPG_DIALOG_H

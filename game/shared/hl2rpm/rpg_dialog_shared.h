//========= HL2RPM ============//
//
// RPG-система: диалоги v2 (RPG_SPEC §5) - общее для сервера и клиента.
//
// User message "RPG_Dialog" (сервер -> клиент), первый байт - тип:
//   BEGIN   short npc entindex, string entity, string speaker, byte flags (RPG_DLGF_*)
//   LINE    string кусок реплики (длинная реплика без токена приходит частями до NODE)
//   NODE    short node id, string line (токен/текст или "" - взять из LINE), string speaker,
//           short auto_next (-1), byte end, float sound duration (-1 - нет звука), short actor entindex
//   OPTION  byte index, byte state (RPG_DLGOPT_*), string text, string label (kind:id:value), string tag
//   SHOW    узел собран - показать
//   END     разговор окончен
// Клиент -> сервер: rpg_dialog_choose <node> <index>, rpg_dialog_continue <node>, rpg_dialog_close
//
//=============================//

#ifndef RPG_DIALOG_SHARED_H
#define RPG_DIALOG_SHARED_H
#ifdef _WIN32
#pragma once
#endif

enum RPGDialogMsg_t
{
	RPG_DLGMSG_BEGIN = 1,
	RPG_DLGMSG_LINE = 2,
	RPG_DLGMSG_NODE = 3,
	RPG_DLGMSG_OPTION = 4,
	RPG_DLGMSG_SHOW = 5,
	RPG_DLGMSG_END = 6,
};

// флаги BEGIN
#define RPG_DLGF_AUTOFACE	0x01		// повернуть камеру к собеседнику

// состояние варианта ответа
enum RPGDialogOptionState_t
{
	RPG_DLGOPT_NORMAL = 0,
	RPG_DLGOPT_CHECK_PASS = 1,		// проверка проходит (зелёная)
	RPG_DLGOPT_CHECK_FAIL = 2,		// не проходит, но выбрать можно (красная)
	RPG_DLGOPT_UNMET = 3,			// условие не выполнено: видно, выбрать нельзя (серая)
	RPG_DLGOPT_EXIT = 4,			// выход из разговора
	RPG_DLGOPT_DONE = 5,			// проваленная проверка check_once: больше нельзя
};

#define RPG_DLG_MAX_LINE_CHUNK	200		// байт строки в одном сообщении

#endif // RPG_DIALOG_SHARED_H

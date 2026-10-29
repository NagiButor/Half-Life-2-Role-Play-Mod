//========= HL2RPM ============//
//
// RPG-интерфейс: окно диалога v2 (сервер - server/hl2rpm/rpg_dialog.cpp).
//
//=============================//

#ifndef RPG_DIALOG_PANEL_H
#define RPG_DIALOG_PANEL_H
#ifdef _WIN32
#pragma once
#endif

// открыто ли окно диалога v2 (ввод движения блокируется, глубина резкости - на собеседника)
bool RPGDialogPanel_IsOpen();
// ентиндекс собеседника открытого диалога (-1)
int RPGDialogPanel_GetNPCIndex();

#endif // RPG_DIALOG_PANEL_H

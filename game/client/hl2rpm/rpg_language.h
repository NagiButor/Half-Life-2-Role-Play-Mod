//========= HL2RPM ============//
//
// Язык игры (Настройки -> Игра -> Язык): см. rpg_language.cpp.
//
//=============================//

#ifndef RPG_LANGUAGE_H
#define RPG_LANGUAGE_H
#ifdef _WIN32
#pragma once
#endif

// применить сохранённый выбор (cfg/hl2rpm_language.txt); зовётся из VGui_CreateGlobalPanels
void HL2RPM_Language_Init();
// "russian" / "english" / язык Steam
const char *HL2RPM_Language_Get();

#endif // RPG_LANGUAGE_H

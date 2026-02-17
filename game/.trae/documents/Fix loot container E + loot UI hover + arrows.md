## Причины
- **Loot container (E не с первого раза)**: в [loot_container.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/loot_container.cpp) открытие сейчас ловится только по `m_afButtonPressed & IN_USE` в `Think()` раз в 0.05с. Если игрок нажал и отпустил E между тиками, нажатие легко пропускается.
- **Подсветка основной кнопки в луте при видимых Take/Drop/Put/Drop**: в [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp) фон красится ещё и по `IsDepressed()`. Даже если мы подавили `IsArmed()`, “depressed” может давать красный фон.
- **Стрелки/Enter в луте**: VGUI может не получать эти клавиши (фокус/маршрутизация). Сам обработчик есть, но события не всегда доходят до панели.

## Что сделаю
### 1) Сделать, чтобы loot container открывался по E стабильно
- В `LootThink()` поменяю условие на:
  - `(pPlayer->m_afButtonPressed & IN_USE) || (pPlayer->m_nButtons & IN_USE)`
- Уменьшу частоту Think (например, 0.01–0.02с) и оставлю per-player cooldown (0.25с), чтобы не спамить открытием.
- Файл: [loot_container.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/loot_container.cpp)

### 2) Лут UI: базовая кнопка предмета не краснеет, пока видны action-кнопки
- В `CInvOptionButton::PaintBackground()` (loot panel) буду подавлять красный фон **для основной кнопки предмета**, когда `AreActionsVisibleForItem(...) == true`:
  - игнорировать `IsArmed()`, `selected` и также `IsDepressed()` (то есть красный фон будет только у action-кнопок).
- Файл: [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

### 3) Лут UI: стрелки/Enter работают всегда
- Добавлю перехват в [ClientModeShared::KeyInput](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/clientmode_shared.cpp):
  - если лут-панель видима и `down==1`, то для `KEY_UP/DOWN/LEFT/RIGHT/ENTER/PAD_ENTER` вызывать `lootPanel->OnKeyCodePressed((vgui::KeyCode)keynum)` и возвращать `0` (съесть событие), чтобы движок не уводил его мимо VGUI.
  - аналогично для инвентаря можно расширить позже, но сейчас сфокусируюсь на луте.

## Проверка
- Проверю диагностики в IDE.
- В игре:
  - контейнер открывается по E с первого нажатия.
  - при видимых Take/Drop/Put/Drop основная кнопка предмета не краснеет от наведения/клика.
  - стрелки и Enter в луте работают независимо от фокуса/кликов.
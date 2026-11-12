## Цели
- Добавить звуки открытия/закрытия Loot и Inventory с отдельными каналами и комфортной громкостью.
- Добавить 2 разных клика: для выбора предмета и для кнопок действий.
- Переделать Inventory: убрать окно подтверждения, сделать кнопки Use/Drop как в Loot, перевести на английский.
- Запретить перемещение при открытом окне Loot.

## Звуки (клиент)
- Добавлю небольшой helper в UI-файлах для воспроизведения .wav через EmitSound_t с заданными: каналом (CHAN_USER_BASE + N) и громкостью (0..1).
- **Loot**
  - Open: npc\\combine_soldier\\zipline_hitground2.wav
  - Close: npc\\combine_soldier\\zipline_hitground1.wav
- **Inventory** (одновременно по 2 звука, поэтому дам разные каналы внутри одной операции, чтобы они не перебивали друг друга)
  - Open: npc\\combine_soldier\\zipline_clothing1.wav + npc\\combine_soldier\\zipline1.wav
  - Close: npc\\combine_soldier\\zipline_clothing2.wav + npc\\combine_soldier\\zipline2.wav
- **Клики**
  - По предмету (когда появляется выбор действий): buttonclickrelease.wav
  - По кнопкам действий (Use/Drop/Take/Put/Put all/Take all и т.п.): ui/buttonclickrelease.wav
- Каналы разнесу минимум на: loot open/close, inventory open/close, item-select click, action-button click.
- Громкости задам константами (примерно: open/close 0.20–0.30, клики 0.12–0.20) так, чтобы не раздражало и не перекрывало важные игровые звуки.

## Inventory UI (без confirm-окна)
- В [vgui_inventorypanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.h) и [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp):
  - Уберу confirm panel (русские строки “Использовать/Выбросить/Действие для”) и связанную навигацию.
  - Добавлю 2 маленькие action-кнопки (Use/Drop) поверх выбранного элемента списка, с позиционированием и логикой как в Loot (анимации можно не делать, но стиль и размеры будут близкие).
  - Клик по предмету будет: показывать/прятать эти кнопки + играть buttonclickrelease.wav.
  - Нажатие Use/Drop будет: отправлять inventory_use / inventory_drop, играть ui/buttonclickrelease.wav.
  - Клик мимо списка/кнопок будет прятать action-кнопки (как в Loot).

## Loot: клики и корректное закрытие
- В [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp):
  - При выборе предмета (loot_select_left/right → когда реально показываем кнопки) добавить buttonclickrelease.wav.
  - На все action-команды (Put/Take/Drop/Put all/Take all) добавить ui/buttonclickrelease.wav.
  - Закрытие по X (“loot_close”) переведу на вызов EndLoot() (вместо просто SetVisible(false)), чтобы всегда отрабатывал звук закрытия и корректно выключался ввод.
  - Open/Close звуки сделаю в BeginLoot()/EndLoot() чтобы работали и при закрытии через loot_open_end_local.

## Запрет движения при Loot
- В [clientmode_shared.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/clientmode_shared.cpp) в ClientModeShared::CreateMove:
  - Если Loot-панель видима, зануляю cmd->forwardmove/sidemove/upmove и убираю movement-кнопки (IN_FORWARD/IN_BACK/IN_MOVELEFT/IN_MOVERIGHT/IN_JUMP/IN_DUCK/IN_SPEED).
  - Это блокирует перемещение без риска “забыть разморозить” на сервере.

## Проверка
- Сборка client/server DLL.
- Проверка в игре:
  - Loot open/close: правильные звуки и громкость.
  - Inventory open/close: оба звука играют одновременно.
  - Клик по предмету: правильный звук.
  - Нажатия на кнопки действий: правильный UI-звук.
  - Inventory: нет confirm-окна, все тексты на английском.
  - Loot: при открытом окне WASD/прыжок/присед не двигают игрока.

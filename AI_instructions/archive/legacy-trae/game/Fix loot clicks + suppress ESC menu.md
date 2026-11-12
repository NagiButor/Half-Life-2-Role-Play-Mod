## Диагноз
- Клики по предметам в лут-панели перестали работать, потому что кнопки предметов теперь являются детьми `m_pLeftListPanel/m_pRightListPanel`, и их `OnCommand`/action-сигналы не доходят до `CVGuiLootPanel`. В инвентаре это уже решено через `AddActionSignalTarget(this)`.
- Esc всё ещё открывает GameUI, потому что это не только vgui-событие: движок может активировать GameUI на Esc независимо от того, что мы закрыли панель. Нужно принудительно спрятать GameUI командой.

## Что сделаю
### 1) Починить клики по предметам и действиям в луте
- Для всех item-кнопок (лево/право) добавить `btn->AddActionSignalTarget(this)` сразу после `SetCommand(...)`.
- Для action-кнопок (primary/secondary) и, на всякий случай, для `Put all/Take all/X` тоже добавить `AddActionSignalTarget(this)` (чтобы после перепарентинга команды всегда приходили в панель).
- Оставить `SetKeyBoardInputEnabled(false)` у кнопок, но **mouse input не трогать** (он должен оставаться включённым).

### 2) Сделать, чтобы Esc закрывал окна без открытия меню
- В `OnKeyCodeTyped(KEY_ESCAPE)`:
  - **инвентарь**: сначала `engine->ClientCmd_Unrestricted("gameui_hide\n")`, затем `engine->ClientCmd("inventory_open_end\n")`, и `return`.
  - **лут**: `engine->ClientCmd_Unrestricted("gameui_hide\n")`, затем `EndLoot()`, и `return`.
- Если menu всё равно всплывает (на следующий тик/keyup), добавлю страховку: `engine->ClientCmd_Unrestricted("wait;gameui_hide\n")` вместе с первым вызовом.

## Проверка
- В игре: клики по предметам в обеих колонках снова вызывают actions и выполняют put/take/drop.
- Esc закрывает инвентарь/лут и меню игры не появляется.
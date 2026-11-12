## Причина проблем
- **Esc**: в VGUI часто обрабатывается в `OnKeyCodeTyped()`. У нас закрытие сделано в `OnKeyCodePressed()`, поэтому Esc может успеть дойти до GameUI и открыть меню даже если окно уже закрылось.
- **Стрелки/Enter в луте**: у лут-панели много `Button`-детей с включённым keyboard input. Фокус уходит в кнопку, и события клавиатуры перестают попадать в `CVGuiLootPanel::OnKeyCodePressed()`.

## Что сделаю
### 1) Esc закрывает окна и НЕ открывает меню игры
- Добавлю `OnKeyCodeTyped()` в:
  - [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp)
  - [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)
- В `OnKeyCodeTyped(KEY_ESCAPE)`:
  - **инвентарь**: выполнить `engine->ClientCmd("inventory_open_end\n")` и `return` без `BaseClass::OnKeyCodeTyped`
  - **лут**: вызвать `EndLoot()` и `return` без `BaseClass::OnKeyCodeTyped`
- При желании (если меню всё равно всплывает) добавлю `engine->ClientCmd_Unrestricted("gameui_hide\n")` в момент закрытия по Esc.

### 2) В луте заработают стрелки и Enter
- Сделаю как в инвентаре: **отключу keyboard input у всех кнопок** лут-панели, чтобы фокус не перехватывался:
  - item-кнопки в обеих колонках
  - action-кнопки primary/secondary
  - `Put all`, `Take all`, кнопка `X`
- Оставлю keyboard input включённым только у самого `CVGuiLootPanel` (frame) и принудительно `RequestFocus()` при открытии.

## Проверка
- Открыть инвентарь/лут:
  - Esc закрывает окно и меню игры не появляется.
  - В луте работают стрелки вверх/вниз/влево/вправо и Enter.
- Проверить отсутствие ошибок компиляции/диагностик.
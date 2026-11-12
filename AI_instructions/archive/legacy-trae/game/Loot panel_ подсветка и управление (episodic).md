## Цель
Сделать максимально подробный DevMsg-дебаг лут-панели, чтобы по логу из консоли однозначно понять:
- какая DLL реально загружена (твоя / mapbase_episodic / episodic),
- почему выбранный элемент красится красным при видимых Take/Drop,
- почему стрелки/Enter не работают (нет фокуса / не вызывается обработчик / ранний return).

## Блок 0: Проверка, что отладка вообще попадает в твою DLL (критично для episodic)
- В `BeginLoot()` и в обработчиках `loot_open_payload*_local` добавить `DevMsg("[LootPanel] BUILD MARKER ...")`.
- Если при открытии лута в консоли **нет** этих строк — игра грузит не твою `hl2rpm/bin/client.dll`, а `../mapbase_episodic/bin` или `episodic/bin` (это следует из твоего [gameinfo.txt](file:///e:/Steam/steamapps/sourcemods/hl2rpm/gameinfo.txt#L19-L55)).

## Блок 1: Управляемый “жёсткий” дебаг через ConVar’ы (чтобы лог не убил игру)
Добавить набор client ConVar’ов:
- `cl_lootpanel_debug 1` — главный рубильник.
- `cl_lootpanel_debug_keys 1` — лог клавиш.
- `cl_lootpanel_debug_mouse 1` — лог мыши/ховера.
- `cl_lootpanel_debug_paint 1` — лог расчёта подсветки/закраски.
- `cl_lootpanel_debug_throttle 0.05` — минимальный интервал (сек) для периодических сообщений.

Плюс ConCommand:
- `lootpanel_debug_dump` — печатает одним блоком весь текущий state панели (выбор, actionColumn, offsets, фокус, appmodal, hovered item и т.д.), чтобы можно было снять “снимок” в нужный момент без спама.

## Блок 2: Логирование клавиатуры (стрелки/Enter)
В [CVGuiLootPanel::OnKeyCodePressed](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp#L1206-L1366) и `OnKeyCodeTyped`:
- Печатать: `KeyCode`, флаг раннего выхода (WASD/Space), и полный state до/после обработки:
  - `m_nFocusedColumn`, `m_nActionColumn`, `m_nSelectedLeftIndex`, `m_nSelectedRightIndex`, `m_nSelectedAction`, `m_bSelectionVisible`, `m_nLeftContentOffset`, `m_nRightContentOffset`.
- Дополнительно печатать текущий `input()->GetFocus()` и `input()->GetAppModalSurface()` (если доступно), чтобы видеть — есть ли фокус вообще.

Если окажется, что обработчик **не вызывается**:
- Внести правку порядка вызовов в `BeginLoot()` по образцу инвентаря (включить `SetKeyBoardInputEnabled(true)` до `RequestFocus()`), и залогировать результат (фокус до/после).

## Блок 3: Логирование мыши и ховера
В `OnTick()` (или `OnCursorMoved`, если есть в цепочке) при `cl_lootpanel_debug_mouse`:
- Печатать screen-pos мыши, local-pos относительно LootPanel, и определять:
  - над какой колонкой (`LeftListPanel`/`RightListPanel`) курсор,
  - над какой конкретной кнопкой (индекс) курсор,
  - находится ли курсор над action-кнопками.

Также печатать `RequestFocus()` события (когда реально делаем) и факт смены focus.

## Блок 4: “Окрашивание элементов” (почему строка красная при Take/Drop)
Внутри `CInvOptionButton::PaintBackground()`:
- Для релевантных элементов (текущий selected/hovered и элементы с открытыми action-кнопками) логировать вычисления:
  - `m_nColumn`, `m_nIndex`, `IsArmed()`, `IsDepressed()`, `selected`, `suppressArmed` (из `AreActionsVisibleForItem`), `AreAnyActionsVisible()`, итог `willFillRed`.
- Чтобы не печатать тысячи строк каждый кадр:
  - хранить “последнее состояние” внутри кнопки и печатать только при изменении `willFillRed/selected/suppressArmed/armed/depressed`, либо печатать раз в `cl_lootpanel_debug_throttle`.

## Блок 5: Логирование команд и кликов
В `OnCommand()` и `OnMousePressed()`:
- Печатать команду/тип клика.
- Печатать state до/после (то же, что в блоке клавиш).

## Как ты будешь снимать лог
- Включить `developer 2`.
- Опционально: `con_filter_enable 1` и `con_filter_text [LootPanel]`.
- Воспроизвести проблему (открыть лут, выбрать Battery справа, увидеть Take/Drop + красную строку; попробовать стрелки/Enter).
- Сразу вызвать `lootpanel_debug_dump`.
- Скинуть мне консольный лог — по нему можно будет точно сказать, где ломается логика.

Если подтверждаешь этот план — я внесу инструментирование DevMsg + ConVar/ConCommand и (при необходимости) поправлю порядок focus/keyboard в `BeginLoot()` так, чтобы стрелки/Enter начали доходить гарантированно.
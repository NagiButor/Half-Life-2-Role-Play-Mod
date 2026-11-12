## Почему в lootdebug.txt нет [PAINT]
- По файлу видно, что **твоя DLL загружена** (`[LootPanel] BeginLoot build=...`), и DevMsg из `OnTick/OnCommand/OnKeyCodePressed` печатается.
- Но **DevMsg из `CInvOptionButton::PaintBackground()` вообще не попал в лог** (0 совпадений `[PAINT]`).
- Значит проблема не в том, что ConVar «не включился», а в том, что **печать из PaintBackground либо не вызывается в твоей сборке (маловероятно), либо движок/спуф ограничивает/дропает spew во время VGUI-отрисовки**.

## Что уже ясно по твоему логу
- Стрелки работают: `code=90` и `code=88` меняют `selRIdx` 0→1→2→3… (то есть `OnKeyCodePressed` вызывается).
- `lootpanel_debug_dump` ты вызвал **после ESC** (код 70), поэтому панель к моменту дампа уже была закрыта (`visible=0`).

## План (без гаданий)
### 1) Перенести «дебаг окраски» из PaintBackground в OnTick (гарантированно попадёт в консоль)
- Добавить режим, который **раз в N секунд** (и/или при изменении состояния) печатает строку вида:
  - выбранный элемент (колонка/индекс),
  - `IsArmed/IsDepressed` у кнопки,
  - `selected`,
  - `suppressArmed` (результат `AreActionsVisibleForItem`),
  - `anyActions`,
  - итог `willFillRed`.
- Дополнительно печатать такую же строку для кнопок действий (`Take/Drop`) и для hovered item (если есть).

### 2) Добавить команду «однократный снимок подсветки»
- ConCommand `lootpanel_debug_paint_once`:
  - печатает полный «снимок» подсветки/кнопок именно в этот момент,
  - не зависит от PaintBackground.

### 3) Повторный сбор лога (как надо)
- В игре:
  - `developer 2`
  - `cl_lootpanel_debug 1`
  - `cl_lootpanel_debug_keys 1`
  - `cl_lootpanel_debug_mouse 1`
  - `cl_lootpanel_debug_throttle 0.20`
- Открыть лут → довести до состояния «Take/Drop видны, но строка красная».
- **Не нажимая ESC** выполнить:
  - `lootpanel_debug_dump`
  - `lootpanel_debug_paint_once`
- Нажать Enter/NumPad Enter несколько раз.
- Сохранить консоль в файл и прислать.

### 4) Финальный фикс по результату нового лога
- Если `willFillRed=1` из-за `dep=1` → лечим «залипший depressed».
- Если `willFillRed=1` из-за `sup=0` при `anyActions=1` и `selected=1` → правим логику `AreActionsVisibleForItem`/индексы.
- Если Enter приходит только как Typed → делаем безопасную обработку Enter без двойных срабатываний.

Подтверди — и я внесу изменения в код (перенос PAINT-дебага в OnTick + новая команда) и подготовлю тебе точный набор команд для следующего лога.
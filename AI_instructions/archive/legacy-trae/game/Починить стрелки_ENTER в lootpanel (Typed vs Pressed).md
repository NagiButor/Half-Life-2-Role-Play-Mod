## Почему сейчас ломается
- В VGUI у клавиш обычно приходят два события: `OnKeyCodePressed` и потом `OnKeyCodeTyped`.
- В lootpanel `OnKeyCodePressed` у нас делает логику навигации/ENTER, но `OnKeyCodeTyped` для ENTER/стрелок ничего не перехватывает и отдаёт в `BaseClass::OnKeyCodeTyped()`.
- Для `Frame` это может триггерить стандартное поведение (активация default/close), из-за чего:
  - при простом открытии + ENTER окно закрывается,
  - после действия (take/put) следующий ENTER тоже уходит в base и закрывает.
- Параллельно возможно, что на некоторых конфигурациях стрелки приходят именно как Typed (а не Pressed), поэтому «стрелки не работают, а ENTER работает странно».

## Что сделаю
- В [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp):
  1. Верну защиту от двойной обработки (как было раньше): добавлю `static vgui::KeyCode g_LootPanelLastPressedCode` и `static float g_LootPanelLastPressedTime`.
  2. В `OnKeyCodePressed` буду записывать lastPressedCode/Time.
  3. В `OnKeyCodeTyped` перехвачу `KEY_UP/DOWN/LEFT/RIGHT/ENTER/PAD_ENTER`:
     - если это тот же код, что только что пришёл в Pressed (в пределах ~0.05с) — просто `return` (не даём base закрыть окно и не делаем двойное действие),
     - иначе вызову `OnKeyCodePressed(code)` и `return` (это покроет кейсы, когда Pressed не приходит).
  4. `KEY_ESCAPE` оставлю как есть (закрытие окна), остальные клавиши — в `BaseClass::OnKeyCodeTyped`.

## Ожидаемое поведение после
- Стрелки всегда двигают выделение (в зависимости от колонки), не закрывают окно.
- ENTER:
  - без выбора: ставит выбор на 1й элемент и открывает actions,
  - при открытых actions: выполняет выбранное действие,
  - повторный ENTER больше не закрывает окно сам по себе.

## Проверка
- Проверю диагностикой IDE, что ошибок нет.
- Для отладки при необходимости можно включить `cl_lootpanel_debug 1` и `cl_lootpanel_debug_keys 1` и убедиться, что Pressed/Typed приходят ожидаемо и не дублируют действия.
## 1) Esc: убрать вспышку «ПАУЗА»/главного меню
- Сейчас мы прячем GameUI через `gameui_hide`, но иногда меню успевает активироваться на один кадр (и появляется «ПАУЗА»).
- Сделаю «глушение Esc» безопаснее: при закрытии по Esc **не буду сразу отпускать модальность** (`SetAppModalSurface(NULL)`), а отложу отпускание на короткое время (например, 0.1с) через `OnTick()`.
  - Окно уже будет скрыто визуально, но модальная поверхность ещё будет держать ввод, чтобы GameUI не успевал активироваться на key-up.
- Оставлю `gameui_hide` (с `wait;gameui_hide`) как страховку.
- Файлы: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp), [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

## 2) Инвентарь: кнопка USE всегда красная после клика мышью
- Причина: после клика по предмету `ShowActions(idx)` не сбрасывает `m_nSelectedAction`, и «USE» остаётся «выбранным» (красным) даже без наведения.
- Исправлю: при открытии actions мышью буду ставить `m_nSelectedAction = -1` (нет выбранного действия), чтобы красный фон появлялся только:
  - при наведении мышью (IsArmed)
  - при управлении клавиатурой (когда мы явно выставляем выбор действия)
- Реализую это локально в обработчике команды клика по предмету.
- Файл: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp)

## 3) Лут: стрелки всё ещё не работают
- Скорее всего фокус/клавиатурный ввод перехватывают контейнеры списков (`LeftListPanel/RightListPanel`) или другие дочерние панели.
- Исправлю:
  - `m_pLeftListPanel->SetKeyBoardInputEnabled(false)` и `m_pRightListPanel->SetKeyBoardInputEnabled(false)`
  - при открытии и после клика по любому элементу буду делать `RequestFocus()` у `CVGuiLootPanel`, чтобы гарантировать получение `OnKeyCodePressed`.
- Файл: [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

## Проверка
- Диагностики компиляции.
- В игре:
  - Esc закрывает инвентарь/лут без вспышки меню/«ПАУЗА».
  - В инвентаре USE не подсвечен красным постоянно после клика.
  - В луте стрелки и Enter работают стабильно (в т.ч. после кликов мышью).
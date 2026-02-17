## 1) Прокрутка колесом в окне инвентаря (как в луте)
- Сейчас инвентарь уже умеет «скроллить оффсетом» (`m_nContentOffset`, `AdjustScrollToSelection()`), но колесо мыши двигает **выбор**, а не сам скролл.
- Сделаю поведение как в луте:
  - Если курсор над `m_pListPanel`, колесо будет менять `m_nContentOffset` (шаг ~60px), клампить в диапазон и **репозиционировать** кнопки.
  - При таком скролле буду скрывать action-кнопки (как в луте), чтобы они не “висели”.
  - Если курсор не над списком — можно оставить старое поведение (колесо = смена выбора), чтобы не сломать привычное.
- Файл: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp)

## 2) Управление клавишами в луте — визуально как в инвентаре
- Сейчас в инвентаре подсветка/выбор реализованы через кнопку `CInvOptionButton`, которая рисует красный фон, если элемент выбран (`IsItemSelected/IsUseActionSelected/IsDropActionSelected`).
- В луте сделаю аналогичную механику:
  - Заменю текущую кнопку предметов/действий на вариант, который умеет подсвечивать выбранный элемент/действие на основании состояния `CVGuiLootPanel`.
  - Добавлю состояние выбора по аналогии с инвентарём:
    - выделение строки (item index) в активной колонке
    - показ action-кнопок и выбор primary/secondary действия
  - Правила клавиш будут совпадать с инвентарём:
    - Up/Down: перемещение по списку
    - Right: открыть actions / переключить primary→secondary
    - Left: secondary→primary / закрыть actions
    - Enter: открыть actions / выполнить выбранное действие
    - Дополнительно: если actions скрыты, Left/Right будут переключать колонку (чтобы не потерять управление двумя списками).
- Файлы: [vgui_lootpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.h), [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

## 3) Esc открывает меню раньше, чем закрывает инвентарь/лут
- Причина почти всегда в том, что панель не является modal для vgui input → ESC уходит в GameUI.
- Сделаю панели **modal while visible**:
  - при открытии (BeginInventory/BeginLoot): `vgui::input()->SetAppModalSurface(GetVPanel());`
  - при закрытии (EndInventory/EndLoot): `vgui::input()->SetAppModalSurface(NULL);`
- Это гарантирует, что ESC будет сначала обработан нашими панелями.

## Проверка
- Сборка без ошибок.
- В игре:
  - инвентарь: колесо над списком реально скроллит список (не наезжает на нижние элементы).
  - лут: навигация визуально подсвечивает выбранные строки/кнопки действий как в инвентаре.
  - ESC закрывает лут/инвентарь и **не** открывает GameUI поверх.
## Что сейчас не так
- `CVGuiLootPanel` создаёт кнопки предметов прямо дочерними к окну, без «контейнера-клиппера», поэтому при большом списке они могут наезжать на нижние кнопки/прочие элементы.
- В лут-панели нет `OnMouseWheeled` и `OnKeyCodePressed`, а в `BeginLoot()` стоит `SetKeyBoardInputEnabled(false)`, поэтому ни прокрутки колесом, ни управления стрелками/Enter, ни закрытия на Esc нет.

## Правка 1: Прокрутка колёсиком + клиппинг (как в инвентаре)
- В [vgui_lootpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.h) добавить поля:
  - два `vgui::Panel*` для списков: `m_pLeftListPanel`, `m_pRightListPanel`
  - оффсеты прокрутки: `m_nLeftContentOffset`, `m_nRightContentOffset`
  - индекс выделения для навигации (отдельно от текущего “клик-выбора”): `m_nSelectedLeftIndex`, `m_nSelectedRightIndex`
  - фокус колонки: `m_nFocusedColumn` (0=left, 1=right)
- В [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp):
  - В `BeginLoot()` создавать/обновлять `m_pLeftListPanel` и `m_pRightListPanel` с нужными bounds (область между заголовками и нижними кнопками).
  - Перенести создание кнопок предметов так, чтобы **они были детьми соответствующего list panel**, а не всего окна.
  - Аналогично перенести `m_pLeftActionPrimary/Secondary` внутрь `m_pLeftListPanel`, а правые экшены внутрь `m_pRightListPanel`, чтобы координаты совпадали после прокрутки.
  - Реализовать функцию/логику репозиционирования кнопок по оффсету (типа `RepositionLeftButtons()` / `RepositionRightButtons()`), чтобы при изменении `m_n*ContentOffset` кнопки «ездили» вверх/вниз и клиппились родителем.
  - Добавить `OnMouseWheeled(int delta)`:
    - определять, над какой колонкой курсор (через `IsPointInsidePanel(m_pLeftListPanel, ...)` / правую), и скроллить именно её
    - ограничивать оффсет по диапазону (0..max) исходя из высоты контента и высоты viewport
    - при скролле скрывать экшены, чтобы они не оставались «в воздухе».

## Правка 2: Управление стрелками + Enter (как в инвентаре)
- В `BeginLoot()` включить клавиатуру: `SetKeyBoardInputEnabled(true)`.
- Добавить `OnKeyCodePressed(vgui::KeyCode code)`:
  - `KEY_UP/KEY_DOWN`: двигать выделение в текущей колонке (left/right), обновлять визуальный выбор и подскролливать так, чтобы выбранный элемент был виден.
  - `KEY_LEFT/KEY_RIGHT`: переключать фокус колонки (0↔1) и подсвечивать элемент в новой колонке.
  - `KEY_ENTER`: выполнять **основное** действие по выбранному элементу (left → `loot_put_slot`, right → `loot_take_slot`).

## Правка 3: Закрытие на Esc
- В `OnKeyCodePressed` обработать `KEY_ESCAPE`:
  - для лут-панели: `EndLoot()`
  - для инвентаря Esc уже закрывает (`inventory_open_end`), при необходимости унифицировать поведение, но основной фикс будет в лут-панели.

## Затрагиваемые файлы
- [vgui_lootpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.h)
- [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)
- (Опционально) проверить, что `CVGuiInventoryPanel` уже закрывается на Esc и ничего не ломаем: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp)

## Проверка
- В игре открыть лут контейнера/трупа с большим количеством предметов:
  - колесо мыши прокручивает список, элементы не налезают на `Put all/Take all`
  - стрелки и Enter работают
  - Esc закрывает окно лута и инвентарь
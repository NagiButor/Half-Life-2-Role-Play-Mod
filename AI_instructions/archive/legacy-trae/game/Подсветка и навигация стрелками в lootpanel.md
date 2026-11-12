## Диагноз
- Подсветка выбранного пункта в lootpanel завязана на `m_bSelectionVisible`:
  - [IsLeftItemSelected](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp#L928-L936) и [IsRightItemSelected](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp#L933-L936) требуют `m_bSelectionVisible==true`.
- При этом `m_bSelectionVisible` может сбрасываться (например колесом мыши; см. присваивание `m_bSelectionVisible = false` в `OnMouseWheeled`). Тогда стрелки меняют индексы, ENTER работает, но красной подсветки нет.

## Что сделаю
1) **Сделаю подсветку независимой от `m_bSelectionVisible`**
- Уберу условие `m_bSelectionVisible` из `IsLeftItemSelected/IsRightItemSelected`.
- Визуальная логика станет как в inventorypanel: если индекс выбран и колонка в фокусе — краснеет.

2) **Закреплю корректное переключение колонок на LEFT/RIGHT**
- В обработчиках `KEY_LEFT/KEY_RIGHT` (когда actions не открыты) оставлю/усилю поведение: LEFT/RIGHT переводит фокус между левой и правой колонкой и ставит валидный индекс (0 если ещё не было выбора).

3) **Подсветка action-кнопок (Put/Drop/Take/Drop)**
- Проверю, что selection для action-кнопок идёт через `IsLeft/RightPrimary/SecondaryActionSelected()` (она уже не зависит от `m_bSelectionVisible`).
- Если после пункта (1) всё равно не будет подсветки действий, добавлю явную перерисовку/инвалидацию после изменения `m_nSelectedAction` (в ветке стрелок LEFT/RIGHT при `m_nActionColumn>=0`).

## Проверка
- Диагностика IDE без ошибок.
- В игре:
  - Открыть lootpanel и сразу стрелками UP/DOWN увидеть красную подсветку выбранного item.
  - LEFT/RIGHT переключают колонку (левая/правая) и подсветка переходит на соответствующую.
  - ENTER открывает actions, LEFT/RIGHT переключают action 0/1 и соответствующая маленькая кнопка подсвечивается красным.
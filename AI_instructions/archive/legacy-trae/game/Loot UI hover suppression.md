## Диагноз
- В лут-панели красная подсветка предмета рисуется в `CInvOptionButton::PaintBackground()` при `IsArmed()`/`IsDepressed()`/`selected`.
- Сейчас подавление подсветки завязано на `AreActionsVisibleForItem(column, idx)`, но оно срабатывает только для «того самого» выбранного предмета (`m_nSelectedLeft/m_nSelectedRight`).
- По требованию: **пока видны action-кнопки (Take/Drop или Put/Drop), никакие основные кнопки предметов не должны краснеть даже при hover**.

## План правки
### 1) Сделать глобальное подавление hover для кнопок предметов, когда actions видны
- Добавлю в `CVGuiLootPanel` метод `AreAnyActionsVisible()` (или расширю текущий), который возвращает true, если хоть одна из action-кнопок видима (левая или правая) и `m_nActionColumn >= 0`.
- В `CInvOptionButton::PaintBackground()` для **кнопок предметов** (`m_nIndex >= 0`) буду ставить `suppressArmed = pOwner->AreAnyActionsVisible()`.
- Логику для самих action-кнопок (`m_nIndex == -100/-101`) не трогаю — они продолжат краснеть по hover/нажатию.

## Проверка
- Открыть лут (труп/ящик), кликнуть предмет → появляются Take/Drop или Put/Drop.
- Навести мышь на любую основную кнопку предмета: **красной заливки нет**, краснеют только action-кнопки.

Файл: [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp), при необходимости добавлю декларацию в [vgui_lootpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.h).
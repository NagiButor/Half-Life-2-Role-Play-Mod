## Диагноз
- **Подсветка предмета в инвентаре**: кнопка предмета краснеет из‑за `IsArmed()` в `CInvOptionButton::PaintBackground()`. Когда открыты `USE/DROP`, наведение на основной предмет всё равно делает его armed, а по требованию подсвечиваться должны только `USE/DROP`.
- **Стрелки/Enter в луте не работают**: логика в `OnKeyCodePressed()` есть, значит события не доходят до `CVGuiLootPanel` (фокус остаётся на дочерних кнопках/панелях и клавиши «теряются»).
- **Мигание меню/«ПАУЗА» на Esc**: `gameui_hide` не всегда успевает, GameUI может активироваться на кадр. В SDK уже есть штатный способ: `gameui_preventescapetoshow`/`gameui_allowescapetoshow` (как в чате).

## Что сделаю
### 1) Инвентарь: убрать красную подсветку основной кнопки, когда видны USE/DROP
- Добавлю в `CVGuiInventoryPanel` простой метод вида `AreActionsVisibleForItem(int idx)`.
- В `CInvOptionButton::PaintBackground()` для кнопок предметов: если actions открыты именно для этого предмета, **игнорировать `IsArmed()`/`selected`** для основной кнопки (оставить только золото-обводку). `USE/DROP` продолжат подсвечиваться как раньше.
- Файл: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp), заголовок при необходимости [vgui_inventorypanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.h)

### 2) Лут: заставить стрелки/Enter стабильно работать
- В `CVGuiLootPanel::OnTick()` если панель видима и фокус не на ней (`input()->GetFocus() != GetVPanel()`), делать `RequestFocus()`.
- В `OnKeyCodeTyped()` дополнительно обработать `KEY_UP/DOWN/LEFT/RIGHT/ENTER/PAD_ENTER`, прокидывая в ту же логику, что и `OnKeyCodePressed()` (на случай если в конкретной конфигурации движок шлёт их как typed).
- Файл: [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

### 3) Esc: убрать мигание меню/«ПАУЗА»
- При открытии инвентаря/лута выполнять `engine->ClientCmd_Unrestricted("gameui_preventescapetoshow\n")`.
- При полном закрытии выполнять `engine->ClientCmd_Unrestricted("gameui_allowescapetoshow\n")`.
- Оставлю `gameui_hide` как страховку, но основной механизм будет prevent/allow.
- Файлы: [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp), [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp)

## Проверка
- Диагностики компиляции.
- В игре:
  - при открытых `USE/DROP` основной предмет не краснеет при наведении.
  - в луте работают стрелки и Enter до/после кликов мышью.
  - Esc закрывает окна без мигания меню/«ПАУЗА». 
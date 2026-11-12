## Что происходит сейчас
- В инвентаре (InventoryPanel) большая кнопка предмета НЕ краснеет, если для неё открыты actions (Use/Drop), потому что и hover (`IsArmed()`), и selected блокируются через `suppressArmed`.
- В лут-панели у тебя по факту получается обратное поведение: когда actions открыты, выбранность не краснит, но при наведении на большую кнопку она снова краснеет.

## Как сделаю «как в окне лутания»
- В [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp) в `CInvOptionButton::PaintBackground()` поменяю условие заливки красным:
  - было: `IsDepressed() || (!suppressArmed && IsArmed()) || (!suppressArmed && selected)`
  - станет: `IsDepressed() || IsArmed() || (!suppressArmed && selected)`

## Результат
- Когда actions открыты: большая кнопка не будет краснеть от selected, но будет краснеть при наведении мыши (как в текущем поведении lootpanel, которое ты описал).
- Когда actions скрыты: всё работает как раньше (hover/selected краснят).

## Проверка
- Диагностика IDE на ошибки.
- В игре: открыть инвентарь, открыть actions на предмете, проверить что при hover большая кнопка краснеет даже при открытых actions; selected при этом не краснит.
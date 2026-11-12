## Причина ошибки
- В `CSplitAmountTextEntry::ApplySchemeSettings` я вызвал `BaseClass::ApplySchemeSettings(pScheme)`, но у `vgui::TextEntry` typedef `BaseClass` недоступен (private), поэтому компилятор даёт C2248.

## Исправление
- В [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp#L19-L35) заменить строку:
  - `BaseClass::ApplySchemeSettings(pScheme);`
  на
  - `TextEntry::ApplySchemeSettings(pScheme);`
- Это корректно вызовет базовую реализацию `TextEntry`, а дальше останутся наши принудительные золотые/чёрные цвета.

## Проверка
- Проверю диагностику IDE/сборку на отсутствие C2248.
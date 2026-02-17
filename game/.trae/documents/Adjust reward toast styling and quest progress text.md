## 1) Reward toast: вернуть стиль/цвет и английский текст
- Сейчас `prefix` рисуется серым, а предмет — золотым. Ты хочешь, чтобы `You received` выглядел **точно так же**, как `Healthkit`.
- Изменения в [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp):
  - заменить `e.prefix = "Вы получили:"` на `e.prefix = "You received"`;
  - в рендере reward-строки убрать серый цвет для prefix и рисовать **и prefix, и item** одним и тем же “золотым” цветом/flash-градиентом (как сейчас рисуется item).

## 2) Quest completion/progress toast: убрать (1/1)
- В [quest_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/quest_system.cpp#L230-L240) заменить текст:
  - было: `Quest progress: %s (%d/%d)`
  - станет: `Quest progress: %s` (без счётчика).
- `Objective complete: ...` оставить.

## 3) Проверка
- Прогоню диагностику IDE.
- Поведение: тост награды выглядит единым стилем (prefix и item одинаковый цвет), текст на английском; квестовый прогресс больше не показывает (1/1).
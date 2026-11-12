## Что сломалось и почему
- `CUtlString::Set` не принимает 2 аргумента: в [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp#L1750-L1775) я пытался сделать `e.item.Set(p, bar-p)` (подстрока), но у CUtlString нет такого overload.
- `UTIL_VarArgs` отсутствует в client project, поэтому строка `CUtlString(UTIL_VarArgs(...))` не компилируется и даёт ещё неоднозначность оператора `=`.
- `LNK4075` — это warning линкера про /EDITANDCONTINUE и /OPT:REF, не критично.

## Исправление
- Заменить парсинг `REWARD|...|...` так:
  - копировать имя предмета в локальный `char buf[256]` с ограничением длины, затем `e.item = buf`.
- Заменить сборку `"<item> xN"`:
  - использовать `Q_snprintf` в `char buf[320]`, затем `e.itemText = buf`.
- Никаких `UTIL_VarArgs` в клиенте.

## Проверка
- Прогоню диагностику IDE для client project после правки.

Если ок — применю патч только в `vgui_inventorypanel.cpp`.
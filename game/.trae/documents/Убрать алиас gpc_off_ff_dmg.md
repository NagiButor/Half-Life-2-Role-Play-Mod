## Что сделаю
- Удалю из `CAI_BaseNPC::KeyValue()` обработку ключа `gpc_off_ff_dmg` (строки около [ai_basenpc.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/ai_basenpc.cpp#L13515-L13519)).
- Ничего в FGD менять не нужно: в FGD я добавлял только `gpc_off_f_dmg`.

## Проверка
- Прогоню диагностику по проекту.
- Быстро проверю, что `gpc_off_f_dmg` по-прежнему парсится и работает как раньше.
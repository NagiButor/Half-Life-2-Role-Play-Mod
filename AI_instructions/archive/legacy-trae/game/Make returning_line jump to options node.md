## Проблема
Сейчас при повторном разговоре `returning_line` отправляется как override **на start_node**, но дальше срабатывает цепочка `auto_next` (например 0→1), и NPC снова проговаривает стартовые фразы.

## Что изменю (сервер)
- В `DialogDefinitions::SendDialogToPlayer` изменю поведение для повторного диалога:
  - Если `returning_line` задан и `returning_no_options == 0`, то вместо `start_node` выберу **первую ноду с опциями** по цепочке `auto_next` от `start_node` (например в `demo_npc_1` это будет node 1).
  - Отправлю эту ноду с `overrideLine = returning_line`, но **принудительно отключу auto-advance** (`forcedAutoNextId = -1`, `forcedAutoClose = 0`), чтобы после returning line сразу появились опции и диалог не пошёл дальше по старым фразам.
  - Если `returning_no_options == 1` — оставлю старую логику (там может быть нужен авто-next/авто-close).

## Проверка
- Повторный разговор с `demo_npc_1`: показывается returning line → сразу опции (без повторного проигрывания старых фраз).
- Прогоню диагностику IDE.
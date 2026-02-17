## Причина ошибки
- `dialog_open_payload "..."` отправляется как **одна** консольная команда через `engine->ClientCommand`.
- В Source лимит длины одной команды, которая проходит через `CCommand`/`Cbuf_AddText`, — ~**511 символов**. Русские реплики + длинные опции легко это превышают → `WARNING: Command too long... ignoring!` и `Cbuf_AddText: buffer overflow`.

## Решение
### 1) Сервер: разбиение payload на чанки
- В `DialogDefinitions::SendDialogNodeToPlayerEx` после сборки `payload`:
  - если итоговая команда `dialog_open_payload "..."` укладывается в лимит — оставить как есть;
  - иначе отправлять так:
    1) `dialog_open_payload_begin "<entity>"` (сбрасывает сборщик на клиенте)
    2) несколько раз `dialog_open_payload_chunk "<часть>"`
    3) `dialog_open_payload_end` (клиент собирает строку и парсит как обычный payload)
- Размер чанка подберу безопасный (учту, что экранирование `\` и `"` увеличивает строку) и **не буду резать внутри UTF-8 символа**.
- Буду использовать уже существующий `EscapeForQuotedArg` для каждой части.

### 2) Клиент: сборка чанков и повторный парсинг
- В `vgui_dialogpanel.cpp` добавлю 3 concommand:
  - `dialog_open_payload_begin`
  - `dialog_open_payload_chunk`
  - `dialog_open_payload_end`
- Хранить накопленный буфер в `static CUtlString s_DialogPayloadAccum;`.
- На `*_end` вызвать ту же логику парсинга, что и у текущего `dialog_open_payload`.
  - Для этого вынесу код разбора payload в отдельную функцию `HandleDialogPayloadString(const char*)`, и буду вызывать её и из старого, и из чанкового пути.

## Проверка
- Открыть диалог `demo_npc_1_rus`: больше нет `Command too long`/`buffer overflow`, и диалог/опции отображаются.
- Прогоню диагностику IDE.
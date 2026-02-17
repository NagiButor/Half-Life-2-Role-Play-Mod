## Причина бага
- Сейчас сервер открывает инвентарь/лут через `engine->ClientCommand(..., UTIL_VarArgs("... \"%s\"", payload))`, т.е. через консольную команду с длинной строкой.
- `UTIL_VarArgs` в game DLL ограничен статическим буфером 1024 байта, поэтому длинные payload обрезаются/ломаются.
- Дальше движок пытается добавить обрезанную/слишком длинную команду в Cbuf и выдаёт `WARNING: Command too long... ignoring!` + `Cbuf_AddText: buffer overflow`, из‑за чего команды открытия панели не доходят до клиента.

## Стратегия фикса
- Убрать передачу списка предметов через консольные команды и заменить на usermessage (с чанкингом), чтобы не упираться в лимиты консольной строки.
- Использовать уже зарегистрированное сообщение `Inventory_Update` как универсальный канал и добавить в него byte‑тип (инвентарь/лут + begin/add/end).

## Серверные изменения
- В [inventory_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/inventory_system.cpp) заменить `SendInventoryToPlayer()`:
  - Вместо одной команды `inventory_open_payload` отправлять серию `UserMessageBegin("Inventory_Update")`:
    - `INV_BEGIN` (очистить буфер на клиенте)
    - `INV_ADD` (добавить 1 предмет или маленький пакет предметов)
    - `INV_END` (команда клиенту применить список и открыть панель)
  - Данные предмета отправлять как короткую строку формата `classname:bullets[:clip1:clip2:reserveIdx:reserveCount]` (по одному/маленькими пачками), без общей мегастроки.
- В [loot_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/loot_system.cpp) заменить `OpenLootForPlayer()`:
  - `LOOT_BEGIN` + контекст (`displayName`, `soundOpen`, `soundClose`)
  - `LOOT_ADD_LEFT` чанками для левого списка (инвентарь игрока)
  - `LOOT_ADD_RIGHT` чанками для правого списка (содержимое контейнера/трупа)
  - `LOOT_END` (применить и открыть панель)
- Размер чанка сделать заведомо безопасным (по умолчанию 1 предмет/сообщение или очень маленькая пачка), чтобы не зависеть от `MAX_USER_MSG_DATA` движка.

## Клиентские изменения
- Добавить обработчик usermessage `Inventory_Update` и хук:
  - В [clientmode_shared.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/clientmode_shared.cpp) добавить `HOOK_MESSAGE( Inventory_Update );`.
  - Реализовать `__MsgFunc_Inventory_Update( bf_read &msg )` (лучше в отдельном новом `inventory_messages.cpp` или рядом с существующими msgfunc), который:
    - держит статический буфер накопления (`pendingInventoryItems`, `pendingLootLeft`, `pendingLootRight`, `pendingLootContext`)
    - на `INV_BEGIN/LOOT_BEGIN` очищает соответствующие буферы
    - на `*_ADD_*` читает строку(и) и добавляет в нужный список
    - на `INV_END` вызывает `GetGlobalInventoryPanel()->SetItems(...)`
    - на `LOOT_END` вызывает `GetGlobalLootPanel()->SetContext(...)` и `SetItems(left,right)`
- Оставить существующие concommand’ы `inventory_open_payload` / `loot_open_payload2_local` как fallback/отладку, но сервер больше ими не пользуется.

## Проверка
- Добавить временный dev‑лог (только при `developer 1`) на сервере: сколько предметов ушло и сколько сообщений отправлено.
- В игре собрать кейс с большим количеством предметов в инвентаре и в контейнере/трупе:
  - убедиться, что панели стабильно открываются
  - убедиться, что в консоли больше нет `Command too long` / `Cbuf_AddText: buffer overflow`
  - проверить основные действия (take/put/use/drop) после открытия.

## Затрагиваемые файлы
- Сервер: [inventory_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/inventory_system.cpp), [loot_system.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/loot_system.cpp)
- Клиент: [clientmode_shared.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/clientmode_shared.cpp) + новый файл под msgfunc или правка `vgui_*panel.cpp` для обработки сообщения
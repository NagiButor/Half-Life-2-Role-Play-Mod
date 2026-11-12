## Причина вылета
- Сервер теперь шлёт usermessage `Inventory_Update` при открытии инвентаря/лута.
- Но в вашей сборке HL2/SP используется регистрация сообщений из [hl2_usermessages.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/hl2/hl2_usermessages.cpp), и там **нет** `usermessages->Register("Inventory_Update", -1)`.
- Поэтому при `UserMessageBegin("Inventory_Update")` движок падает с: `Unregistered message 'Inventory_Update'`.

## Исправление
- Добавить регистрацию `Inventory_Update` в [hl2_usermessages.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/hl2/hl2_usermessages.cpp) внутри `RegisterUserMessages()`:
  - `usermessages->Register( "Inventory_Update", -1 );`
- (Опционально, для надёжности) продублировать регистрацию в [mapbase_usermessages.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/shared/mapbase/mapbase_usermessages.cpp) в `RegisterMapbaseUserMessages()`, чтобы сообщение было доступно вне зависимости от того, какая именно таблица `RegisterUserMessages()` используется.

## Проверка
- Запуск карты и открытие инвентаря/окна лута.
- Убедиться, что:
  - нет окна Engine Error про `Inventory_Update`
  - окна открываются даже при большом количестве предметов
  - в консоли больше нет `Command too long...`/`Cbuf_AddText overflow` (это уже исправляли ранее).
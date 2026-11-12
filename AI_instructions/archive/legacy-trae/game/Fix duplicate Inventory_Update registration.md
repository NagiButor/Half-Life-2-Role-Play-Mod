## Причина нового вылета
- Теперь при запуске падает с `CUserMessages::Register 'Inventory_Update' already registered`.
- В движке `CUserMessages` на старте вызывает **две** функции регистрации: `RegisterUserMessages()` и `RegisterMapbaseUserMessages()` (см. `shared/usermessages.cpp`).
- Мы добавили `Inventory_Update` **и туда, и туда**, поэтому второй вызов пытается зарегистрировать уже существующее сообщение и падает.

## Исправление
- Сделать регистрацию идемпотентной (как уже сделано для `CombineSuit` в `shared/usermessages.cpp`):
  - Перед `Register("Inventory_Update", -1)` проверять `if ( usermessages->LookupUserMessage("Inventory_Update") == -1 )`.
- Применить эту проверку:
  - В `shared/hl2/hl2_usermessages.cpp` вокруг `Inventory_Update`.
  - В `shared/mapbase/mapbase_usermessages.cpp` вокруг `Inventory_Update`.

## Проверка
- Убедиться, что игра больше не падает при запуске.
- Открыть инвентарь и окно лута: не должно быть ни ошибок регистрации, ни переполнения командного буфера.
## Почему сейчас награды не выдаются
- В `rewards { "item" "1" }` это **не subkey-блоки**, а key/value записи KeyValues.
- В парсере я ошибочно использовал обход subkeys, поэтому `def.rewards` остаётся пустым и `CompleteQuest()` ничего не выдаёт.

## 1) Починить rewards (выдача предметов)
- В [dialog_definitions.cpp] заменить обход `rewards` на `FOR_EACH_VALUE(pRewardsKV, kv)`:
  - `kv->GetName()` = classname предмета;
  - `atoi(kv->GetString())` = количество/буллеты;
  - заполнять `def.rewards`.
- В [quest_system.cpp] оставить выдачу как сейчас (ammo через `AddItemToPlayerWithBullets`, остальное — N раз `AddItemToPlayer`), но после выдачи делать `Inventory_Update` без принудительного открытия UI (или сделать отдельный sync), чтобы награда не «терялась» в UX.

## 2) Hover-реакция на Inventory/Quests
- В [vgui_inventorypanel.cpp] для табов:
  - при `IsArmed()` делать лёгкое подчёркивание/изменение альфы текста;
  - активная вкладка остаётся с подчёркиванием как сейчас.

## 3) Ветви диалога после выполнения квеста (и новые квесты)
- Добавить в DSDialogOption новый условный ключ `require_quest_state` в формате `questid:completed|active|failed`.
- Парсинг в [dialog_definitions.cpp], хранение в [dialog_definitions.h], проверка в [dialogsystem_server.cpp] перед выполнением опции.
- Это позволит:
  - показывать новые варианты ответа после completion;
  - открывать новую ветку диалога, где можно `grant_quest` следующего квеста.

## 4) Сделать квесты в UI как: основное название + подназвание (стадия) + описание
- Обновить сетевой формат `Quests_Update`:
  - сервер отправляет **baseTitle** (название квеста), **stageTitle** (подназвание текущей стадии) и **stageDesc**.
  - клиент читает эти 3 строки и хранит в `QuestClientEntry`.
- В списке квестов:
  - рисовать 2 строки в кнопке (baseTitle сверху, stageTitle ниже),
  - высоту строк/кнопки увеличить только для вкладки Quests.
- В панели описания показывать stageDesc.
- Для Completed-квестов в списке показывать **baseTitle + “Done”**, не последнюю стадию.

## 5) Почему описание всё ещё белое
- Усилить принудительное применение цвета:
  - каждый раз перед `SetText()` у `m_pQuestDescLabel` задавать `SetFgColor(gold)`;
  - также после `MakePopup()` в `BeginInventory()`.

## Проверка
- Обновить `scripts/dialogs/test_npc.txt`: добавить `rewards` и опцию, которая появляется только при `require_quest_state "test_headcrab:completed"` и предлагает новый квест.
- Проверить в игре:
  - completion выдаёт предмет;
  - табы реагируют на hover;
  - после completion появляются новые опции;
  - список квестов выглядит как 2 строки + описание (золото), completed сворачивается нормально и не показывает последнюю стадию.
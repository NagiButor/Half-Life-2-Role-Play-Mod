## Что видно по твоему логу (и где реально баг)
- `QuestSystem: rewards.Count=0` означает: **квест деф найден**, но в нём наград нет.
- Это не проблема выдачи/Inventory_Update, это проблема **парсинга rewards** при загрузке `scripts/dialogs/*.txt`.
- В `ParseQuestsKV()` количество награды сейчас читается через `GetInt(NULL, 0)`. Для KeyValues, созданных как `"item_healthkit" "1"`, значение обычно хранится как строка, и `GetInt` у вас возвращает 0 → награда пропускается → `rewards.Count=0`.

## 1) Исправить парсинг rewards так, чтобы всегда читалось число
- В [dialog_definitions.cpp] заменить чтение `rewardAmount` на:
  - `atoi(pRewardKV->GetString(NULL, "0"))` (и для второго прохода тоже).
- Добавить DevMsg под `quest_reward_debug` прямо в парсере: печатать `rewardName` и **сырой** `GetString(NULL)` чтобы по консоли сразу видеть что реально распарсилось.

## 2) Сделать сдачу квеста без закрытия диалога + фраза NPC + обновлённые опции
### Почему сейчас закрывается
- Клиент закрывает диалог локально, если в `flags` у опции стоит бит close (см. [vgui_dialogpanel.cpp:880-886](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp#L880-L886)).

### Как сделаю
- На сервере при отправке опций в payload (`SendDialogNodeToPlayerEx`):
  - если у опции есть `complete_quest`, то **маскировать close-бит** из `opt.flags` при сериализации (чтобы клиент не закрывал окно).
- Добавить в опцию новый KV ключ:
  - `after_line` (строка)
- При выборе опции с `complete_quest`:
  - сервер выполняет `CompleteQuest`,
  - затем отправляет игроку **тот же nodeId** ещё раз, но с `overrideLine=after_line` (или без, если не задано), чтобы NPC сказал фразу,
  - список опций пересоберётся заново (и уже без “Got any work?” и без “I killed…”, потому что фильтры по state/stage их уберут).

## 3) Обновить test_npc.txt под новый flow
- У опции сдачи квеста убрать `close 1`.
- Добавить `after_line` типа "Good. Here’s your reward.".
- (Опционально) поставить `next` не нужно — мы остаёмся в том же узле и просто обновляем строку.

## 4) Проверка
- Включаешь: `developer 1; quest_reward_debug 1`.
- Делаешь `dialog_reload`, открываешь диалог.
- В консоли должно появиться:
  - `DialogDefinitions: parsed quest 'test_headcrab' rewards=1`
  - `QuestSystem: rewards.Count=1`
  - `Inventory: AddItemToPlayer ... item_healthkit`
- При сдаче квеста диалог **не закрывается**, NPC говорит `after_line`, затем доступны обновлённые варианты ответов.

После подтверждения внесу правки в код и в test_npc и проверю диагностику IDE.
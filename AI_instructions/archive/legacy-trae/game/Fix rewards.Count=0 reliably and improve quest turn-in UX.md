## Что видно по твоему логу (точно)
- `QuestSystem: rewards.Count=0` — квест деф найден, но в дефе наград реально нет.
- Значит проблема не в выдаче/Inventory_Update, а в том, что **QuestDef с rewards не попадает в QuestSystem** при загрузке диалогов.
- Отсутствие в логе строк `DialogDefinitions: parsed quest ...` означает, что ты не показал вывод `dialog_reload` (а именно там будет видно, что парсер реально прочитал из `rewards`).

## 1) Сделать парсинг rewards максимально «железным»
- Заменить получение количества награды на чтение через родителя `pRewardsKV`, а не через узел-итератор:
  - `amount = atoi(pRewardsKV->GetString(rewardName, "0"))`
  - это работает даже если KeyValues относит запись к subkey/value по-разному.
- Включить подробный DevMsg на каждом reward при `quest_reward_debug 1`:
  - `rewardName`, `raw`, `parsedAmount`.

## 2) Убрать эффект «старые дефы остаются» при dialog_reload
- Добавить в QuestSystem метод `ClearQuestDefs()` (очищает только `m_QuestDefs`).
- В `DialogDefinitions::ReloadDialogs()` вызывать `QuestSystem().ClearQuestDefs()` перед чтением файлов.
- Это гарантирует, что после `dialog_reload` ты тестируешь именно свежераспарсенные квесты.

## 3) Добавить удобную команду для проверки без гаданий
- Серверная команда `quest_dump_def <id>`:
  - печатает: id/title, stages count, rewards count и список rewards.
- Это позволит одним вызовом подтвердить: «в дефе награды есть/нет».

## 4) Сдача квеста без закрытия диалога
- Оставляю текущее поведение: close-бит не отправляется клиенту для `complete_quest` и работает `after_line`.

## 5) Как ты быстро проверишь после фикса
- В игре: `developer 1; quest_reward_debug 1; dialog_reload; quest_dump_def test_headcrab`.
- Если `quest_dump_def` покажет reward, а `CompleteQuest` всё равно не добавит — тогда уже копаем выдачу.

После подтверждения внесу правки в: `dialog_definitions.cpp`, `quest_system.h/.cpp`, (и точку вызова `ClearQuestDefs` в `ReloadDialogs`), плюс добавлю `quest_dump_def`.
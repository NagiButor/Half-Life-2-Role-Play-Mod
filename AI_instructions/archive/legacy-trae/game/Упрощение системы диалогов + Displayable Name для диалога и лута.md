## Уточнение по требованиям
- `scripts/dialogues.txt` не используем.
- Делаем **отдельный файл на персонажа**: `scripts/dialogs/<npc>.txt`.
- Переносим текущий тестовый диалог из C++ в файл **с сохранением всех текущих “фишек”** (choreo, звук, extra sequence, auto option delay по .vcd, returning line/choreo, server-side scripted_sequence на ноде/опции, spoiled/spoken, закрытие окна).

## Цели
1) Диалоги редактируются как данные (без перекомпиляции), удобно держать много цепочек по персонажам.
2) Диалоговое окно показывает Displayable name собеседника.
3) В Hammer можно задать NPC Displayable name, и оно будет показываться в луте после смерти (как у контейнеров).

## План изменений

## 1) Displayable name в диалоговом окне
- **Источник имени**: использовать keyvalue `loot_displayname` у NPC (одно поле работает и для лута, и для диалога).
- **Сервер**: при отправке узла добавляем в payload поле `displayName`.
- **Клиент**: парсим `displayName` и отображаем:
  - либо как заголовок окна (`SetTitle(displayName, true)`),
  - либо отдельным лейблом над репликой.
- **Совместимость**: если `displayName` не пришёл (старый payload) — UI ведёт себя как сейчас.

## 2) NPC Displayable name в Hammer для лута (как сейчас работает логика)
- В коде `loot_displayname` у NPC уже поддержан (копируется в ragdoll и уходит в лут-панель), но в FGD для NPC нет ключа.
- Добавить `loot_displayname(string) : "Displayable name" : ""` в базовый NPC-класс `BaseNPC` в [base_vizzys.fgd](file:///e:/XBLAH's%20modding%20tool/tools/FGDs/Mapbase/base_vizzys.fgd#L592-L620).

## 3) Data-driven диалоги из `scripts/dialogs/*.txt`
- На сервере реализовать загрузчик, который **сканирует директорию** `scripts/dialogs/` и читает все `*.txt`.
- Каждый файл = один диалог для одного персонажа (проще держать и не путаться). Предлагаемый формат:

```text
"Dialog"
{
  "entity" "test_npc"            // targetname NPC
  "returning_line" "..."         // как сейчас returningLine
  "returning_choreo" "scenes/..." // как сейчас returningChoreography (без .vcd)

  "nodes"
  {
    "0"
    {
      "line" "..."              // speakerLine
      "choreo" "scenes/..."     // choreography (без .vcd)
      "sound" ""                // soundName
      "sequence" ""             // sequenceName (локально на актёре)
      "option_delay" "auto"     // auto = как сейчас вычисление по .vcd; либо число
      "server_sequence" ""       // node->serverSequenceName
      "spoils" "0"              // если 1: MarkEntitySpoiled + закрыть

      "options"
      {
        "0" { "text" "..." "next" "2" "close" "0" "server_sequence" "" "spoils" "0" }
        "1" { "text" "..." "next" "-1" "close" "1" }
      }
    }
  }
}
```

- Реестр диалогов на сервере: map `entityName -> Dialog` (строки как `CUtlString`, узлы/опции как `CUtlVector`), чтобы не зависеть от статических массивов.
- `FindDialogForEntity()` будет искать в этом реестре.
- Добавить серверную dev-команду `dialog_reload` для перечитывания файлов без перезапуска.

## 4) Упростить/защитить выбор опций (без путаницы с id)
- Вместо глобального `optionId` сделаем выбор по (nodeId, optionIndex):
  - Кнопка шлёт: `dialog_choose <nodeId> <optionIndex> "<entityName>" <flags>`.
  - Сервер обрабатывает строго указанную ноду и индекс опции.
- Для совместимости можно оставить fallback: если пришёл старый формат — выполнять старый поиск.

## 5) Перенос текущего тестового диалога из C++ в файл
- Создать `scripts/dialogs/test_npc.txt` и перенести туда:
  - все ноды/реплики,
  - choreography (`scenes/test_npc_*`),
  - returningLine/returningChoreography,
  - node-level `serverSequenceName` (включая случай "kick"),
  - опции с флагом закрытия,
  - автозадержку появления опций по .vcd (`option_delay=auto`).
- В коде: убрать использование `g_AllDialogs[]` как основного источника; оставить C++ пример только как fallback (если файл не найден) либо полностью заменить на data-driven.

## Проверка (после внедрения)
- Hammer: у NPC задать `targetname=test_npc` и `loot_displayname="Доктор Кляйнер"`.
- В игре:
  - диалог показывает имя в заголовке/лейбле;
  - цепочка переходит по `next`;
  - `option_delay=auto` реально задерживает кнопки до окончания речи;
  - серверный `server_sequence` запускается;
  - `spoils=1` закрывает диалог и запрещает повторное открытие;
  - после смерти в лут-панели отображается то же `loot_displayname`.

После подтверждения я внесу изменения в:
- `dialog_definitions.h/.cpp` (реестр + загрузка + payload расширение),
- `vgui_dialogpanel.h/.cpp` (показ имени + новый choose-протокол),
- FGD `base_vizzys.fgd` (ключ `loot_displayname` для BaseNPC),
- добавлю `scripts/dialogs/test_npc.txt` как образец с полным функционалом.
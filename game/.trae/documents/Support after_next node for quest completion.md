## Что нужно
Сделать так, чтобы «реплика после сдачи квеста» была **отдельной node** (с `line/choreo/sound/sequence/option_delay/auto_close`), а не оверрайдом текущей node.

## Изменения в коде (сервер)
- В `DSDialogOption` добавить поле `afterNextId` (int, по умолчанию -1).
- В парсере опций (`ParseOptionKV`) читать новый ключ:
  - `after_next` (node id).
- В `HandleDialogChoice` изменить ветку сдачи квеста:
  - если `complete_quest` и `after_next != -1` → после завершения квеста отправлять **узел `after_next`** как отдельную ноду через `SendDialogNodeToPlayerEx`:
    - `forceNoOptions = true` (чтобы не показывать варианты)
    - `forcedAutoNextId = -1`
    - `forcedAutoClose = 1` (чтобы панель сама закрылась после задержки/сцены)
  - иначе оставить текущий fallback: `after_line` (для обратной совместимости).

## Оформление скриптов
### 1) `test_npc.txt`
- В опции сдачи квеста ("I killed the headcrab."):
  - убрать `after_line`/`after_choreo`
  - поставить `after_next` на новый id (например, `21` — подберу свободный).
- Добавить новую node с этим id:
  - `line` = текущая after_line
  - `choreo` = текущая after_choreo (или пусто)
  - `option_delay` = `auto`
  - `auto_close` = `1`
  - без `options`.

### 2) `demo_npc_1.txt`
- Аналогично для опции сдачи `demo_headcrab_quest`:
  - заменить `after_line` на `after_next`
  - добавить отдельную node для «награды» с `auto_close 1`.

## Проверка
- Диагностика IDE после правок.
- Логика в игре: после выбора сдачи квеста показывается отдельная реплика/сцена, затем окно закрывается само.
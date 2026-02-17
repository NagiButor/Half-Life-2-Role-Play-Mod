## Цель
Сделать, чтобы «после сдачи квеста» в диалоге можно было настраивать не только `after_line`, но и сцену/звуки/sequence так же, как в обычных репликах.

## Изменения
- Структура опции: добавить поля `afterChoreo`, `afterSound`, `afterSequence`.
- Парсер опций: читать `after_choreo`, `after_sound`, `after_sequence` из KV.
- Отправка после-фразы: вместо `SendDialogNodeToPlayer(..., overrideLine)` вызвать расширенный `SendDialogNodeToPlayerEx` с оверрайдами:
  - line = `after_line`
  - choreo = `after_choreo` (приоритет над returning_choreo)
  - sound = `after_sound`
  - sequence = `after_sequence`
- Логика выбора медиа: в `SendDialogNodeToPlayerEx` при наличии override использовать его, иначе — прежние правила (returning_choreo или node choreo/sound/sequence).

## Клиент
- Формат payload уже содержит поля choreo/sound/sequence — клиент умеет проигрывать choreo или sound. Sequence параметр сейчас игнорируется; если нужно, отдельно включим его позже.

## Проверка
- В `test_npc.txt` у опции `complete_quest` добавить `after_choreo`/`after_sound`/`after_sequence` и убедиться, что после сдачи квеста проигрывается заданная сцена/звук. Диагностика IDE.
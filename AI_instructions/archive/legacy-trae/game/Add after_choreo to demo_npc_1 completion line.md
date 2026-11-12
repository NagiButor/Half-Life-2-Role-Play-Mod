## Что изменю
- В [demo_npc_1.txt](file:///e:/Steam/steamapps/sourcemods/hl2rpm/scripts/dialogs/demo_npc_1.txt) у опции сдачи квеста ("complete_quest" для `demo_headcrab_quest`, сейчас строка 94) добавлю настройку:
  - `after_choreo` "scenes/test_npc_again"

## Зачем
- Теперь после `after_line` при сдаче квеста можно отдельно указать сцену (VCD), как и в других репликах.

## Примечание
- Значение `after_choreo` — путь **без .vcd**. Ты можешь заменить на свою сцену, если добавишь её в папку `hl2rpm/scenes/`.

## Проверка
- Только правка скрипта диалога + проверка диагностики IDE.
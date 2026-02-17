## 1) Камера всё ещё «телепортами» — почему
В текущем коде автонаводка считает `dt` через `Plat_FloatTime()`. При тике 1мс время часто меняется **ступенчато** (низкая/неровная точность таймера), поэтому получается: несколько тиков подряд `dt≈0` (движение почти нет) → потом скачок `dt` → резкий рывок.
Плюс `OnTick` может вызываться чаще кадров, а обновление вида реально происходит по кадрам.

## 1A) План исправления камеры (Fallout-плавность)
- Перейти с `Plat_FloatTime()` на **кадровое время**:
  - использовать `gpGlobals->curtime`/`gpGlobals->frametime` (на клиенте) и обновлять автонаводку **не чаще 1 раза за кадр**.
- Сделать демпфированную интерполяцию (мягкий “ease” без рывков):
  - `alpha = 1 - exp(-k * frametime)`
  - `new = cur + AngleDiff(target, cur) * alpha`
- Понизить точку наведения: `+36` → `+21` по Z.
- Останов автонаводки: по таймауту (0.7–0.9с) и/или по малой ошибке.
- DevMsg (под `dialog_debug 1`): печатать dt/alpha/ошибку раз в ~0.2с для контроля.

## 2) Spoils всё равно закрывает окно сразу
Причина в твоём логе: сервер отправляет `dialog_open_end` немедленно.
Это старый кусок в [dialog_definitions.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/server/dialog_definitions.cpp#L666-L671):
- `if (node && node->spoils) { MarkEntitySpoiled...; SendClientCommandSafe(... dialog_open_end) }`

План:
- Удалить/отключить этот блок немедленного закрытия.
- Оставить только новый путь: `pendingSpoil=1` → клиент ждёт окончания реплики/скипа → шлёт `dialog_finish_spoil` → сервер только тогда делает MarkEntitySpoiled+закрытие.
- DevMsg: “pendingSpoil sent” и “finish_spoil received”.

## 3) NPC подбежал, но диалог не начался
По логу видно, что печатается `trigger_dialog_start: npc_approach ...`, но нет последующего `StartDialogForEntity`.
Самая вероятная причина: при `trigger_once=1` триггер удаляется в `StartTouch`, и `ApproachThink` больше не выполняется.

План:
- Если `npc_approach=1`, не удалять триггер в `StartTouch`.
- Удалять его только когда диалог реально стартовал (ветка reached/timeout в `ApproachThink`).
- DevMsg: “approach reached/timeout -> starting dialog”.

## Проверка
- Камера: больше нет ступенчатых рывков, движение равномерно-плавное.
- Spoils: пропадает ранний `dialog_open_end` из SendDialogNodeToPlayer; закрытие происходит после задержки/сцены.
- Approach: после подбега появляется лог старта диалога и триггер удаляется только после старта.
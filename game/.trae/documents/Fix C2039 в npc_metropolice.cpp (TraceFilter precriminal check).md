## Причина ошибки
- В `npc_metropolice.cpp` внутри `CTraceFilterMetroPolice::ShouldHitEntity()` используется `this->IsGordonPrecriminalForMe()`.
- Но `this` там имеет тип `CTraceFilterMetroPolice*`, а не `CNPC_MetroPolice*`/`CAI_BaseNPC*`, поэтому компилятор и пишет `C2039`.

## Исправление
- В `CTraceFilterMetroPolice::ShouldHitEntity()` заменить проверку:
  - `!this->IsGordonPrecriminalForMe()`
  на вычисление `precriminal` через атакующего NPC:
  - взять `CBaseCombatCharacter *pBCC = info.GetAttacker()->MyCombatCharacterPointer();`
  - получить `CAI_BaseNPC *pAttackerNPC = pBCC ? pBCC->MyNPCPointer() : NULL;`
  - если `pAttackerNPC` валиден → `bool bPre = pAttackerNPC->IsGordonPrecriminalForMe();`
  - иначе fallback: `GlobalEntity_GetState("gordon_precriminal") == GLOBAL_ON`
  - и условие сделать `if ( !pEntity->IsPlayer() || !bPre )`.

## Проверка
- Пересобрать `Server (Episodic)` (и лучше `Server (HL2)` тоже).
- Убедиться, что ошибка C2039 исчезла и новых ошибок по `npc_metropolice.cpp` нет.
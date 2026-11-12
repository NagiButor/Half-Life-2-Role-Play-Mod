## Причина ошибки
- В `ApplySpoilDialog()` используется тернарный оператор `pPlayer ? pPlayer : this`.
- Тип левого операнда `CBasePlayer*`, правого — `CAI_BaseNPC*`. В C++ тернарный оператор пытается привести оба к общему типу, и компилятор пытается привести `CAI_BaseNPC*` к `CBasePlayer*`, что и даёт C2446.

## Исправление
- В `ApplySpoilDialog()` заменить оба вызова:
  - `m_OnDialogSpoiled.FireOutput( pPlayer ? pPlayer : this, this );`
  - `m_OnFactionDialogSpoiled.FireOutput( pPlayer ? pPlayer : this, this );`
  на безопасный вариант с явным базовым типом:
  - `CBaseEntity *pActivator = pPlayer ? static_cast<CBaseEntity*>(pPlayer) : static_cast<CBaseEntity*>(this);`
  - и `FireOutput(pActivator, this)`.

## Проверка
- Пересобрать `Server (Episodic)` и `Server (HL2)` (тот же файл участвует в обоих).
- Убедиться, что C2446 исчез и новых предупреждений/ошибок не появилось.
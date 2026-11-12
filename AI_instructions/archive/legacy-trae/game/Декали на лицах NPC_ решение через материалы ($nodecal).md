## Идея
- Да, можно сделать это именно «через исходный код»: на клиенте принудительно снять у материалов флаг `NO_DECAL`, который выставляется через `$nodecal 1`.
- Это даст декали даже на глаза/зубы/всё остальное (как ты и попросил).

## План
### 1) Добавить клиентские ConVar
- `cl_clear_nodecal 0/1` — включить/выключить поведение.
- `cl_clear_nodecal_filter "models/"` — по каким материалам применять (по умолчанию на все model-материалы; при желании можно сузить до `models/humans/`).

### 2) Реализовать снятие флага
- В client.dll добавить функцию:
  - итерация по всем материалам через `IMaterialSystem::FirstMaterial/NextMaterial`;
  - если `IMaterial::GetName()` содержит `cl_clear_nodecal_filter`, то вызвать `SetMaterialVarFlag( MATERIAL_VAR_NO_DECAL, false )`.

### 3) Вызов в момент загрузки карты
- Вставить вызов в `CHLClient::LevelInitPostEntity()` в [cdll_client_int.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/cdll_client_int.cpp) (после `IGameSystem::LevelInitPostEntityAllSystems()`), чтобы материалы уже были доступны.

### 4) Пересборка и проверка
- Собрать client.dll.
- Проверить на citizen/barney/прочих проблемных NPC: декали на лицах должны начать появляться.

## Примечание
- Это решение обходит исходный `$nodecal` без распаковки VPK и без правок сотен VMT.
- Возможный побочный эффект: декали будут появляться буквально на всём материале модели (включая глаза/зубы/стёкла), что может выглядеть странно, но ты это разрешил.
## Почему нынешние правки могли не дать эффекта
- Декали «крови/попадания» на модели в HL2 обычно идут через эффект `Impact` (см. [fx_impact.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/fx_impact.cpp)). Клиент выбирает декаль по `iMaterial` (game material), который приходит с сервера из `trace_t::surface.surfaceProps`.
- Если для удара/попадания по голове у citizen/barney surfaceprop настроен так, что `game.material == '-'` (или другой «непереведённый»/пустой), `TranslateDecalForGameMaterial` может вернуть пустую строку → клиент вообще не ставит декаль.
- Это очень похоже на то, что ты слышал: Valve могли именно так “выключить” декали на лице у дружественных NPC, чтобы не мешать лицевой анимации (не только через `m_fNoDamageDecal`).

## Новый вариант решения (по аналогии с metropolice, но для живых NPC тоже)
### 1) Принудительно подменять surfaceprop на "flesh" для ударов по лицу
- В `CAI_BaseNPC::TraceAttack` (сервер) добавить: если `ai_allow_face_decals_while_alive == 1` и `ptr->hitgroup == HITGROUP_HEAD`, то выставлять `ptr->surface.surfaceProps = physprops->GetSurfaceIndex("flesh")`.
- Это гарантирует, что клиент получит “flesh” материал именно для импакта по голове и выберет кровавую декаль.

### 2) Сделать универсальный “fallback” в `UTIL_ImpactTrace`
- В `UTIL_ImpactTrace` перед `pEntity->ImpactTrace(...)` добавить проверку:
  - получить `surfacedata_t` из `pTrace->surface.surfaceProps`;
  - если `game.material == '-'` (или surface data отсутствует) и модель похожа на human (`modelinfo->GetModelName(...)` содержит `models/humans/`), то подменить `pTrace->surface.surfaceProps` на `"flesh"`.
- Это поймает случаи, когда хитгруппа не проставлена/путь идёт не через `CAI_BaseNPC::TraceAttack`, но импакт всё равно формируется.

### 3) Рэгдоллы людей — не полагаться на `IsRagdoll()`
- Текущий фикс для рэгдоллов завязан на `CBaseAnimating::IsRagdoll()`.
- Расширить условие: если `MOVETYPE_VPHYSICS` и модель `models/humans/`, то тоже применять “flesh”-fallback (даже если `IsRagdoll()` по какой-то причине false).

## Проверка после сборки
- `npc_citizen`/`npc_barney`:
  - попадание/удар по лицу пока жив → должна появляться декаль;
  - после смерти (рэгдолл) попадание/удар по лицу → тоже должна появляться.
- При необходимости добавлю отладочный ConVar, который будет печатать `hitgroup` и `game.material` для попаданий по NPC, чтобы сразу увидеть, какой материал реально приходит для головы.

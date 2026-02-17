## Диагноз (почему это именно из-за моих FGD/keyvalue)
Ты прав: ошибка появилась после моих правок, потому что в VMF теперь появились новые keyvalue с очень длинными именами.

`vvis` падает на `ParseEpar: token too long` когда парсер сущностей встречает **слишком длинный токен**. Я проверил `test_dialogue.vmf`: единственные ключи длиннее 32 символов — это ровно новые propagate-key’и:
- [test_dialogue.vmf:L7702](file:///E:/XBLAH%27s%20modding%20tool/content/Mapbase/hl2rpm/mapsrc/test_dialogue.vmf#L7702) `propagate_spoil_to_faction_on_player_damage`
- [test_dialogue.vmf:L11786](file:///E:/XBLAH%27s%20modding%20tool/content/Mapbase/hl2rpm/mapsrc/test_dialogue.vmf#L11786) `propagate_hate_to_faction_on_player_damage`

То, что ты убрал `angleOverride` и ошибка осталась, подтверждает: проблема не в value, а в **слишком длинном имени key** (ограничение именно у map tools).

## План исправления
1) **Переименовать проблемные keyvalue на короткие (чтобы vvis их съедал)**
- В коде и FGD заменить:
  - `propagate_hate_to_faction_on_player_damage` → `prop_hate_faction_on_dmg`
  - `propagate_spoil_to_faction_on_player_damage` → `prop_spoil_faction_on_dmg`
  (оба короче 32 символов)
- Остальные keyvalue (`hate_player_on_player_damage`, `spoil_dialog_on_player_damage`, `faction_tag`) можно оставить — они короткие.

2) **Сделать обратную совместимость**
- В `CAI_BaseNPC::KeyValue()` добавить поддержку старых длинных имён (если встретились — писать в те же поля), чтобы старые карты/VMF не ломались в игре.

3) **Автоматически починить карту(ы)**
- В `test_dialogue.vmf` заменить ключи на новые короткие (search/replace только по имени ключа).
- По желанию: пробежаться по всем `mapsrc/*.vmf` и заменить везде.

4) **Поправить наложение текста в Hammer**
- В `base_vizzys.fgd` укоротить Display Name’ы для этих двух строк (сам key тоже станет короче, так что проблема уйдёт сама).

5) **Проверка**
- Скомпилировать карту VBSP+VVIS: `ParseEpar: token too long` должен исчезнуть.

Если ок — применю эти переименования в коде+FGD и миграцию VMF автоматически.
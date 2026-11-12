## Почему “Amount:” белый
- Это не поле ввода: цифры в `TextEntry` уже золотые.
- Белым остаётся именно `Label` `SplitAmountLabel` ("Amount:"). Хотя мы задаём ему золото при создании, позже схема/ApplySchemeSettings может перезаписать цвет, а мы его не переустанавливаем.

## Что сделаю
- В `CVGuiInventoryPanel::ApplySchemeSettings` добавить переустановку цвета для `SplitAmountLabel`:
  - найти `m_pSplitPanel->FindChildByName("SplitAmountLabel")` и выставить `SetFgColor(255,200,30,255)` + `SetPaintBackgroundEnabled(false)`.
- Дополнительно в `ShowSplitPopup` после layout тоже принудительно поставить этот цвет (через `FindChildByName`), чтобы гарантировать золото сразу при открытии.

## Проверка
- Открыть Split: `Amount:` в золоте, цифры в инпуте тоже в золоте.
- Прогнать диагностику IDE.
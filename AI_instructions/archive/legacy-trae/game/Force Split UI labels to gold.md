## Почему текст остаётся белым
- `vgui::Label::ApplySchemeSettings()` всегда ставит цвет текста из схемы (`Label.TextColor`). Поэтому даже если мы делаем `SetFgColor` вручную, потом схема снова перекрашивает Label в белый.
- Это касается как заголовка `SplitTitle`, так и подписи `Amount:`.

## Что сделаю
- Добавлю кастомный класс `CSplitGoldLabel : public vgui::Label` с переопределённым `ApplySchemeSettings`:
  - вызывает `Label::ApplySchemeSettings(pScheme)`
  - затем принудительно ставит `SetFgColor(255,200,30,255)` и `SetPaintBackgroundEnabled(false)`.
- В `ShowSplitPopup` буду создавать `SplitTitle` и `SplitAmountLabel` именно как `CSplitGoldLabel`, чтобы они всегда оставались золотыми независимо от scheme.
- Уберу лишние «переустановки» цвета через `FindChildByName`, чтобы не дублировать логику (не обязательно, но сделает код чище).

## Проверка
- Открыть Split: и заголовок, и `Amount:` золотые.
- Проверить диагностику IDE.
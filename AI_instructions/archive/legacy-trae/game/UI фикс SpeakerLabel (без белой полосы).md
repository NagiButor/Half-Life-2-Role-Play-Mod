## Почему это выглядит белым прямоугольником
- В диалоговой панели `SpeakerLabel` сейчас создан как обычный `vgui::Label` и **у него включена отрисовка фона по умолчанию**, поэтому он рисует светлый прямоугольник на всю ширину.
- В лут‑панели у лейблов явно стоит `SetPaintBackgroundEnabled(false)`, поэтому белой полосы нет (см. [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp#L389-L405)).

## Что нужно по UX
- Имя по центру.
- Золотой цвет как у остального текста.
- Под ним небольшой полупрозрачный фон (НЕ на всю ширину панели).

## План исправления
## 1) Заменить SpeakerLabel на кастомный класс
- В [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp) добавить класс `CDialogSpeakerLabel : public vgui::Label`.
- В конструкторе:
  - `SetPaintBackgroundEnabled(false)` (убирает белую полосу)
  - `SetContentAlignment(Label::a_center)`
  - `SetFgColor(Color(255,200,30,255))`

## 2) Рисовать полупрозрачный бокс только под текстом
- Переопределить `Paint()`:
  - если текста нет — ничего не рисовать
  - получить текст через `GetText(...)`
  - измерить размер текста через `surface()->GetTextSize(GetFont(), w, h)`
  - вычислить прямоугольник `boxW = textW + padding*2`, `boxH = высота лейбла`
  - нарисовать `DrawFilledRect` с `Color(0,0,0,200)` **по центру**
  - вывести текст золотым цветом **по центру** через `DrawPrintText`

## 3) Встроить в текущую разметку
- Оставить bounds лейбла на всю ширину (например `12, 8, w-24, 20`), а центрирование/фон делать в `Paint()`.
- `SetSpeakerName()` продолжает просто задавать текст и `SetVisible(...)`.

## Проверка
- Открыть диалог с NPC у которого `loot_displayname` точно задан (раз видно в луте).
- Убедиться:
  - нет белой полосы,
  - имя по центру,
  - фон полупрозрачный и не растягивается на всю ширину.

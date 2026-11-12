## Что происходит
- Ошибка C2220 вызвана тем, что у тебя warnings идут как errors.
- Конкретно C4189: переменная `ty` в `CToastNotificationPanel::Paint()` объявлена, но нигде не используется.

## Исправление
- Удалить строку `int ty = y0 + (h / 2);` в [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp#L1912-L1914).
- Ничего функционально не меняется: переменная реально не использовалась.

## Проверка
- После правки прогоню диагностику IDE, чтобы убедиться, что warning исчез и проект собирается.
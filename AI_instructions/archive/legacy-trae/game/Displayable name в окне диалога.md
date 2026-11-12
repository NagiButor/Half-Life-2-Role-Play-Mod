## Почему сейчас имя не видно
- Сейчас displayable name на клиенте выставляется как **заголовок окна** через `panel->SetTitle(displayName, true)` в [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp#L537-L640).
- Но ваш диалоговый UI визуально выглядит как «чёрная панель без тайтлбара» (Frame-титл практически не рисуется/не заметен из‑за кастомного `Paint/PaintBackground` и компоновки). Поэтому имя как title уходит “в никуда”, хотя в луте оно есть.

## План исправления (надёжно, как в лут‑панели)
## 1) Добавить отдельный лейбл имени в CVGuiDialogPanel
- В [vgui_dialogpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.h): добавить `vgui::Label *m_pSpeakerLabel;` и метод `SetSpeakerName(const char *name)`.
- В [vgui_dialogpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_dialogpanel.cpp):
  - создать `m_pSpeakerLabel` в конструкторе,
  - расположить его сверху внутри панели (например, y=8),
  - сдвинуть `m_pLineLabel` ниже (чтобы текст реплики не наезжал на имя),
  - стиль: тот же золотой цвет, без отдельного фона (или с чёрным, если хочешь как в луте).

## 2) Привязать displayName из payload к этому лейблу
- В `dialog_open_payload` после `BeginDialog(...)` вызывать `panel->SetSpeakerName(displayName)`.
- Если `displayName` пустой — можно показывать fallback:
  - либо ничего,
  - либо `entityName` (targetname) как отладочный вариант.

## 3) Убрать зависимость от SetTitle (опционально)
- Оставить `SetTitle("Dialog")` как есть или вообще больше не менять title динамически — имя будет всегда видно через лейбл.

## Проверка
- В игре: у NPC задан `loot_displayname` (раз он виден в луте — значит задан).
- Открыть диалог: имя появляется вверху диалоговой панели и не перекрывается репликой/кнопками.
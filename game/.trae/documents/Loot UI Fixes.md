## Причины
1. **Подсветка предмета (hover) в луте**: Пользователь жалуется, что если навести на `Take`, а потом обратно на предмет, он краснеет.
   - В `PaintBackground` я уже добавил подавление, если `AreAnyActionsVisible() == true`.
   - Проблема, вероятно, в том, что когда мышь уходит с кнопки действия на другой элемент, `IsArmed()` (hover) у кнопки действия сбрасывается, но видимость (`IsVisible()`) у панели действий остаётся.
   - Однако, `AreAnyActionsVisible()` проверяет именно `IsVisible()`.
   - Возможно, когда мышь переходит с `Take` на `Battery`, `CInvOptionButton` перерисовывается, и `AreAnyActionsVisible()` возвращает true (так как кнопки ещё не скрыты), но пользователь видит "покраснение".
   - **СТОП**. Если `AreAnyActionsVisible()` возвращает `true`, то `suppressArmed` становится `true`, и красная заливка **не должна** рисоваться.
   - Пользователь пишет: "навёл сначала на take, потом опять на кнопку battery и она загорелась красным".
   - Это значит `suppressArmed` почему-то стал `false`. Значит `AreAnyActionsVisible()` вернул `false`.
   - Это может случиться, если при наведении на основную кнопку действия **скрываются**. Но они не должны скрываться просто от hover.
   - А может быть, я в `OnMousePressed` или где-то ещё скрываю действия? Нет, hover не вызывает `OnMousePressed`.
   - **Гипотеза:** В `CInvOptionButton::PaintBackground` логика такая:
     ```cpp
     if (IsDepressed() || (!suppressArmed && IsArmed()) || (!suppressArmed && selected))
     ```
     Если `suppressArmed` true, то `IsArmed` игнорируется.
     Если пользователь видит красное, значит `suppressArmed` false.
     Значит `AreAnyActionsVisible()` false.
     Значит `IsVisible()` у action кнопок false.
     Но пользователь видит кнопки Take/Drop на экране!
     **Возможная причина:** `m_pLeftActionPrimary` и т.д. это указатели. Может быть они `NULL`? Нет, тогда бы крашнулось раньше или не рисовалось.
     **Другая причина:** Кнопки действий перекрывают кнопку предмета? В `vgui_lootpanel.cpp` кнопки действий создаются как дети `this` (LootPanel), а кнопки предметов - дети `m_pLeftListPanel`. Они в разных иерархиях или на разных слоях.
     **Проблема:** В `CInvOptionButton` я получаю `pOwner` через `m_hOwner.Get()`.
     В `vgui_inventorypanel.cpp` (который работает правильно) логика похожая.
     **Внимание:** В инвентаре `IsArmed()` подавляется только если `AreActionsVisibleForItem(m_nIndex)`. То есть для *конкретного* предмета.
     В лут панели я сделал `AreAnyActionsVisible()`, то есть для *любого*. Это даже более строго.
     **Может быть** проблема в том, что `PaintBackground` вызывается *до* того как `AreAnyActionsVisible` обновится? Нет, видимость меняется по клику.
     **Подождите.** В инвентаре кнопки действий находятся *внутри* той же панели или поверх? В инвентаре они создаются в `CVGuiInventoryPanel`, а список элементов - это `m_Options` (кнопки).
     В луте кнопки действий тоже в `CVGuiLootPanel`.
     
     **Решение по подсветке:** Я ещё раз внимательно проверю `PaintBackground` в `vgui_lootpanel.cpp`.
     А также проверю, не сбрасывается ли видимость действий где-то при `OnCursorEntered` или подобном.
     В `vgui_lootpanel.cpp` нет `OnCursorEntered`.
     
     В `vgui_inventorypanel.cpp` в `OnMousePressed` есть проверка:
     ```cpp
     if (IsPointInsidePanelLocal(m_pActionUseBtn, ...)) return;
     ```
     Если клик мимо действий - `HideActions()`.
     Но мы говорим про hover.
     
     **Сравнение с Inventory:**
     В `vgui_inventorypanel.cpp`:
     ```cpp
     suppressArmed = pOwner->AreActionsVisibleForItem(m_nIndex);
     ```
     В `vgui_lootpanel.cpp` (мой фикс):
     ```cpp
     suppressArmed = pOwner->AreAnyActionsVisible();
     ```
     Это должно работать. Если только `AreAnyActionsVisible` работает корректно.
     
     **Идея:** Может быть, когда мышь уходит с `Take` на `Battery`, фокус меняется и что-то вызывает `HideActionButtons`?
     В `OnMouseWheeled` есть `HideActionButtons`. В `OnMousePressed` есть.
     В `OnCommand` ("loot_select_...") есть.
     Просто hover не должен скрывать.
     
     **Стоп!** В `vgui_lootpanel.cpp` `OnCommand` обрабатывает "loot_select_left ...".
     Эта команда посылается кнопкой предмета при нажатии (command).
     Кнопка предмета (`CInvOptionButton`) - это `Button`. Она шлет команду при клике.
     При hover команды нет.
     
     **А что если** `AreAnyActionsVisible` возвращает false, потому что кнопки *прозрачные* (`GetAlpha() == 0`)?
     В `ShowLeftActions` делается анимация альфы. `IsVisible()` должен быть true сразу.
     
     **Предположение:** Пользователь говорит "активны take drop".
     В коде `PaintBackground` я использую `AreAnyActionsVisible()`.
     
     Давай посмотрим код `vgui_lootpanel.cpp` ещё раз внимательно.
     Может я забыл перекомпилировать или закоммитить? Нет, я делал патчи.
     
     **Может быть** дело в `IsDepressed()`?
     В `PaintBackground`:
     ```cpp
     if (IsDepressed() || (!suppressArmed && IsArmed()) || (!suppressArmed && selected))
     ```
     Если `suppressArmed` true, то `IsArmed` и `selected` игнорируются.
     Но `IsDepressed()` (нажата) **не игнорируется**!
     Если пользователь нажал на кнопку (она стала depressed), открылись действия. Кнопка может остаться depressed? Обычно нет, это click.
     Но если пользователь держит мышь?
     В любом случае, пользователь говорит "навел ... загорелась". Это hover (`IsArmed`).
     Значит `suppressArmed` равен `false`.
     Значит `AreAnyActionsVisible()` вернул `false`.
     Значит `IsVisible()` у кнопок действий `false`.
     Но пользователь видит их!
     
     **Мистика.**
     Разве что `pOwner` (LootPanel) другой? Нет, он один.
     
     **А может быть** проблема в порядке отрисовки? Нет.
     
     **Попробуем изменить логику:**
     В `vgui_inventorypanel.cpp` `suppressArmed` зависит от индекса.
     В `vgui_lootpanel.cpp` я сделал глобально.
     
     Давай сделаем в `vgui_lootpanel.cpp` так же, как в инвентаре:
     `suppressArmed = pOwner->AreActionsVisibleForItem(m_nColumn, m_nIndex);`
     Это логичнее. Если действия открыты для *этого* предмета, то *этот* предмет не подсвечивается. А другие пусть подсвечиваются (хотя в луте обычно модально).
     Но я уже делал глобально, чтобы "вообще ничего не краснело".
     
     **Стоп.** В `vgui_lootpanel.cpp` кнопки создаются так:
     `new CInvOptionButton(..., idx, this)`
     
     **Главное:**
     Я обновлю `vgui_lootpanel.cpp` и `vgui_inventorypanel.cpp` (для симметрии и проверки), чтобы логика была идентичной.
     И исправлю стрелки/энтер.
     
     **Проблема со стрелками/Enter в луте:**
     Пользователь говорит "в окне инвентаря они работают как надо, посмотри как там реализовано".
     В `vgui_inventorypanel.cpp`:
     - `OnKeyCodePressed` обрабатывает UP/DOWN/LEFT/RIGHT/ENTER.
     - Панель делается `MakePopup()`, `SetKeyBoardInputEnabled(true)`, `RequestFocus()`.
     - `SetMouseInputEnabled(true)`.
     
     В `vgui_lootpanel.cpp`:
     - То же самое.
     - Я добавил перехват в `ClientModeShared`.
     
     **Почему не работает?**
     Возможно, `CInvOptionButton` (кнопка) перехватывает клавиши?
     Кнопка наследуется от `Button`. `Button` обрабатывает Enter.
     В инвентаре кнопки тоже `CInvOptionButton`.
     
     **Разница:**
     В инвентаре:
     `m_pListPanel` - это `vgui::Panel`. Кнопки создаются с parent `m_pListPanel`.
     В `CVGuiInventoryPanel::OnKeyCodePressed` логика навигации.
     
     Если фокус на кнопке, то `OnKeyCodePressed` приходит в кнопку?
     Да. `Button` может съедать Enter. Стрелки обычно меняют фокус.
     
     В `vgui_inventorypanel.cpp`:
     Нет перехвата в кнопке.
     Но инвентарь работает. Значит фокус на самой панели? Или кнопки не `SetKeyBoardInputEnabled`?
     В `CInvOptionButton` конструкторе: `SetPaintBorderEnabled(false)`. Больше ничего особенного.
     
     **Гипотеза:** В инвентаре фокус держится на `InventoryPanel`, а не на кнопках.
     В `BeginInventory`: `RequestFocus()`.
     
     В луте: `RequestFocus()`.
     Но я добавил `AddTickSignal` и `RequestFocus` в `OnTick`. Это должно форсировать фокус на панель.
     Если фокус на панели, то кнопки не в фокусе.
     Тогда нажатие Enter обрабатывает панель.
     
     **Но пользователь говорит "не работают".**
     Может быть `RequestFocus` в `OnTick` мешает? Если фокус постоянно сбрасывается на панель, а мышь наводит на кнопку, кнопка пытается взять фокус?
     
     **План:**
     1.  **Loot Panel Highlighting:**
         Вернуть логику `AreActionsVisibleForItem` (для конкретного предмета), как в инвентаре. Возможно, глобальная проверка `AreAnyActionsVisible` глючит из-за каких-то состояний (например, анимация скрытия).
         Или лучше: **Убедиться, что AreAnyActionsVisible работает**.
         Но если пользователь говорит "активны take drop" (значит видимы), а кнопка краснеет (значит `suppressArmed` false), то `AreAnyActionsVisible` вернул false.
         Почему?
         `IsVisible()` возвращает true только если сам элемент и все родители видимы.
         Кнопки действий (Action Buttons) - дети `CVGuiLootPanel`.
         `CVGuiLootPanel` видима (мы её видим).
         Значит `m_pLeftActionPrimary->IsVisible()` вернул false.
         Но они на экране!
         
         Может быть дело в `PaintBackground`? Он вызывается для `CInvOptionButton`.
         В этот момент `pOwner` (LootPanel) должен быть валиден.
         
         Давай сделаем **железно**:
         В `PaintBackground` добавим отладочный вывод (через `Msg` или `ConColorMsg`), чтобы понять, что происходит. (Хотя я не увижу консоль).
         
         Ладно, пойдем путем "сделать как в инвентаре".
         В инвентаре: `suppressArmed = pOwner->AreActionsVisibleForItem(m_nIndex);`.
         В луте: я сделаю `suppressArmed = pOwner->AreActionsVisibleForItem(m_nColumn, m_nIndex);`.
         
         И проверю `AreActionsVisibleForItem` в `vgui_lootpanel.cpp`.
         Я его уже менял.
         ```cpp
         bool CVGuiLootPanel::AreActionsVisibleForItem(int column, int idx)
         {
             // ...
             if (column == 0) {
                 // ...
                 return (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) ...
             }
             // ...
         }
         ```
         Если это условие совпадает с тем, что видит пользователь, то `suppressArmed` будет true.
         
     2.  **Loot Panel Input (Arrows/Enter):**
         В инвентаре: `SetKeyBoardInputEnabled(true)` для панели.
         В луте: тоже.
         В инвентаре: `OnKeyCodePressed` обрабатывает навигацию.
         В луте: тоже.
         
         Почему в луте не работает?
         Может быть `m_pLeftListPanel` (панель, где лежат кнопки) перехватывает ввод?
         В инвентаре `m_pListPanel` тоже есть.
         
         **Внимание:** В инвентаре `SetKeyBoardInputEnabled` для `m_pListPanel` не вызывается явно (по умолчанию true?).
         В луте `m_pLeftListPanel` - это `vgui::Panel`.
         
         **Различие:** В луте есть две колонки. `m_nFocusedColumn`.
         
         **Решение:**
         Я уже добавил перехват в `ClientModeShared`. Если он работает, то клавиши должны попадать в `OnKeyCodePressed` лут-панели.
         Если они попадают, то почему не работает?
         В `OnKeyCodePressed`:
         ```cpp
         if (code == KEY_UP || code == KEY_DOWN)
         {
             // ...
             if (m_nFocusedColumn == 0) { ... }
         }
         ```
         Может `m_nFocusedColumn` кривой?
         
         **Попробуем скопировать подход инвентаря:**
         В инвентаре навигация меняет `m_nSelectedIndex`.
         В луте меняет `m_nSelectedLeftIndex` / `m_nSelectedRightIndex`.
         
         **Возможное решение для стрелок:**
         Убедиться, что `OnKeyCodePressed` реально вызывается.
         
         **Что я сделаю:**
         1.  В `vgui_lootpanel.cpp`:
             - Верну/проверю `AreActionsVisibleForItem` и буду использовать его в `PaintBackground` (вместо `AreAnyActionsVisible`), чтобы было 1-в-1 как в инвентаре (где это работает).
             - В `PaintBackground` проверю, чтобы `IsDepressed()` тоже подавлялся.
             
         2.  Для стрелок:
             - В `vgui_lootpanel.cpp` проверю `OnKeyCodePressed`.
             - В `ClientModeShared` я уже добавил перехват. Может я ошибся с условием?
             - В `clientmode_shared.cpp` я проверял `if (lootPanel && lootPanel->IsVisible())`.
             - Может быть надо возвращать 0, чтобы движок не обрабатывал? Я возвращал 0.
             
         3.  **Важный момент:** В инвентаре работает.
             В инвентаре `OnKeyCodePressed` просто меняет индекс.
             В луте `OnKeyCodePressed` делает `HideActionButtons()`.
             Если я жму стрелку вниз, actions скрываются, выбирается следующий предмет.
             Это логично.
             
             Пользователь говорит "не работают стрелки".
             Может быть они работают, но не видно выделения?
             В `PaintBackground`:
             ```cpp
             selected = (m_nColumn == 0) ? pOwner->IsLeftItemSelected(m_nIndex) : ...
             ```
             `IsLeftItemSelected` проверяет `m_bSelectionVisible`.
             В `OnKeyCodePressed` ставится `m_bSelectionVisible = true`.
             
             **Может быть проблема в `IsVisible` проверки внутри `PaintBackground`?**
             Нет.
             
             **Посмотри внимательно на скриншот.**
             Кнопка Battery красная. Кнопка Take красная (hover?).
             
             **План действий:**
             1. Изменить `CInvOptionButton::PaintBackground` в `vgui_lootpanel.cpp`: использовать `AreActionsVisibleForItem` (как в инвентаре).
             2. Убрать `AreAnyActionsVisible`.
             3. Проверить `OnKeyCodePressed` в `vgui_lootpanel.cpp`. Может там логика сломана?
             
             В `OnKeyCodePressed` (LootPanel):
             ```cpp
             if (code == KEY_UP || code == KEY_DOWN)
             {
                 int dir = (code == KEY_UP) ? -1 : 1;
                 HideActionButtons(); // <-- Это скрывает действия!
                 m_nSelectedLeft = -1; // <-- Сбрасывает выбор кликом
                 m_nSelectedRight = -1;
                 m_bSelectionVisible = true;
                 
                 // ... меняем m_nSelectedLeftIndex ...
             }
             ```
             Если навигация работает, то при нажатии стрелки действия должны закрыться, и выделиться соседний предмет.
             Пользователь говорит "не работают". Значит вообще ничего не происходит?
             
             Я добавлю отладочный звук в `OnKeyCodePressed` (например `buttonclick`), чтобы понять, вызывается ли он. (Хотя пользователь не услышит, но если бы я мог...).
             
             **Я сделаю так:**
             В `ClientModeShared` я добавил перехват.
             В `vgui_lootpanel.cpp` я явно `SetKeyBoardInputEnabled(true)`.
             
             Может быть проблема в том, что `CInvOptionButton` перехватывает ввод?
             В `vgui_lootpanel.cpp` кнопки создаются как `new CInvOptionButton`.
             В `vgui_inventorypanel.cpp` тоже.
             
             **Разница:** В инвентаре `OnKeyCodePressed` не скрывает `m_nSelectedIndex`.
             В луте `m_nSelectedLeft` (для мыши) и `m_nSelectedLeftIndex` (для клавы) разделены?
             Да: `m_nSelectedLeft` используется для mouse selection.
             `m_nSelectedLeftIndex` для клавиатуры?
             
             В `PaintBackground`:
             ```cpp
             selected = (m_nColumn == 0) ? pOwner->IsLeftItemSelected(m_nIndex) : ...
             ```
             `IsLeftItemSelected`:
             ```cpp
             return IsVisible() && m_bSelectionVisible && (m_nFocusedColumn == 0) && (idx >= 0) && (idx == m_nSelectedLeftIndex);
             ```
             Использует `m_nSelectedLeftIndex`.
             
             При клике мышью (ShowLeftActions):
             ```cpp
             m_nSelectedLeft = idx;
             m_nSelectedLeftIndex = idx; // <-- Синхронизируем
             ```
             
             При нажатии стрелок:
             ```cpp
             m_nSelectedLeft = -1; // Сбрасываем мышь
             // ... меняем m_nSelectedLeftIndex ...
             ```
             
             Вроде логично.
             
             **Почему не работает ввод?**
             Может быть `MakePopup` перехватывает фокус, но `OnTick` с `RequestFocus` мешает?
             В инвентаре `RequestFocus` только в `BeginInventory`.
             В луте я добавил в `OnTick`.
             Давай уберем `RequestFocus` из `OnTick` в луте, раз в инвентаре его нет.
             В инвентаре `SetKeyBoardInputEnabled(true)` один раз.
             
             **По поводу красной кнопки:**
             Вернусь к `AreActionsVisibleForItem`.
             
             **Итоговый план:**
             1. В `vgui_lootpanel.cpp`:
                - `OnTick`: убрать постоянный `RequestFocus()`. Оставить только `SetAppModalSurface(NULL)` логику. Сделать `RequestFocus()` один раз в `BeginLoot` (как было, и как в инвентаре).
                - `PaintBackground`: использовать `AreActionsVisibleForItem` вместо `AreAnyActionsVisible`.
                - Убедиться, что `AreActionsVisibleForItem` возвращает true, когда действия открыты.
             2. В `vgui_lootpanel.h`:
                - Убрать `AreAnyActionsVisible`.
             3. В `ClientModeShared`:
                - Оставить перехват клавиш, это надежно.

             **Почему `AreAnyActionsVisible` могло не сработать?**
             Может быть я не пересобрал или что-то пошло не так?
             Или `IsDepressed` перебивал?
             Я добавлю `IsDepressed` в подавление тоже (я это уже делал, но проверю).

             **Проверим `AreActionsVisibleForItem` в `vgui_lootpanel.cpp`:**
             ```cpp
             if (column == 0) {
                 if (idx >= m_LeftItems.Count()) return false;
                 if (m_nActionColumn != 0 || m_nSelectedLeft != idx) return false; // <--- ВОТ ПРОБЛЕМА?
                 return ...
             }
             ```
             Если я навожу мышь на *другой* предмет, `m_nSelectedLeft` не меняется (он меняется только по клику).
             Но если я навожу на *тот же* предмет (Battery), для которого открыты действия...
             То `m_nSelectedLeft` должен быть равен `idx`.
             И `m_nActionColumn` должен быть 0.
             
             Пользователь: "навёл сначала на take, потом опять на кнопку battery".
             Кнопка Battery - это та самая, для которой открыты Take.
             Значит `idx == m_nSelectedLeft`.
             Значит `AreActionsVisibleForItem` должно вернуть true.
             Значит `suppressArmed` true.
             
             Почему не сработало с `AreAnyActionsVisible`?
             Оно возвращало `IsVisible` кнопок.
             Если они видны, то `suppressArmed` true.
             
             Может быть `CInvOptionButton` перерисовывается *до* того как `m_nSelectedLeft` установится? Нет, действия уже открыты.
             
             **Может быть `IsArmed()` (hover) рисуется поверх?**
             Нет, `PaintBackground` рисует фон.
             
             **Может быть я редактировал не тот файл?**
             Пути правильные.
             
             **Может быть проблема в `Paint`?**
             В `Paint` просто `SetFgColor` и `Button::Paint`.
             `Button::Paint` рисует текст и может быть *дефолтный* фон?
             `SetPaintBackgroundEnabled(true)` - значит `Button::PaintBackground` не вызывается (вызывается мой переопределенный).
             
             **Давайте посмотрим `vgui_inventorypanel.cpp` внимательнее.**
             Там `suppressArmed = pOwner->AreActionsVisibleForItem(m_nIndex);`.
             В `AreActionsVisibleForItem`:
             ```cpp
             if (m_nActionIndex != idx) return false;
             ```
             
             В `vgui_lootpanel.cpp`:
             ```cpp
             if (m_nActionColumn != 0 || m_nSelectedLeft != idx) return false;
             ```
             
             Вроде то же самое.
             
             **Может быть `m_nSelectedLeft` сбрасывается при hover?**
             Нет, в коде нет такого.
             
             **А что если `IsDepressed`?**
             В `PaintBackground`:
             ```cpp
             if (IsDepressed() || (!suppressArmed && IsArmed()) || (!suppressArmed && selected))
             ```
             Я менял это в прошлом шаге.
             ```cpp
             if ((!suppressArmed && IsDepressed()) || (!suppressArmed && IsArmed()) || (!suppressArmed && selected))
             ```
             Подожди, я в прошлом шаге сделал так?
             Надо проверить `Read` tool.

             И еще: в инвентаре:
             ```cpp
             if (IsDepressed() || (!suppressArmed && IsArmed()) || (!suppressArmed && selected))
             ```
             Там `IsDepressed` **НЕ** под `suppressArmed`!
             То есть если нажать, будет красным.
             Но при hover (`IsArmed`) - не будет.
             
             В луте я должен сделать так же или лучше.
             Если пользователь просто наводит ("навел ... загорелась"), это `IsArmed`.
             
             **Может быть в `vgui_lootpanel.cpp` `PaintBackground` не обновился?**
             Проверю код.

             **И насчет стрелок:**
             Уберу `RequestFocus` из `OnTick`. Это частая причина проблем с вводом, если фокус скачет.

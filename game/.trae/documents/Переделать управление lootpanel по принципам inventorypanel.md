## Что беру за основу из inventorypanel
- Состояние разделяется на 2 режима: навигация по списку и режим действий (когда рядом с выбранным предметом показаны 2 action-кнопки).
- Выделение (selected) живёт отдельно от «показа действий»; при смене выделенного предмета действия скрываются.
- KEY_ENTER: если действий нет — открыть действия для выбранного; если действия есть — выполнить выбранное действие.
- KEY_LEFT/KEY_RIGHT в режиме действий: переключение между action 0/1 и закрытие действий.
- Подсветка кнопок: основной item не должен краснеть, когда для него показаны действия (suppressArmed).

## Как именно изменю lootpanel
- Перепишу логику выделения/действий в [vgui_lootpanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.cpp), сделав её максимально похожей на [vgui_inventorypanel.cpp](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_inventorypanel.cpp):
  - Добавлю/приведу к единому виду хелперы уровня панели: SelectItem(column, idx), SetActionSelection(0/1), PerformSelectedAction(), HideActions(), ShowActions(column, idx), AdjustScrollToSelection/EnsureSelectionVisible.
  - Уберу дублирующие переменные выбора (m_nSelectedLeft/m_nSelectedRight), оставив только индексы выделения по колонкам + «где сейчас открыт action» (action column + action index). Для этого обновлю [vgui_lootpanel.h](file:///e:/SourceModding/HL2RPMSOURCE/source-sdk-2013-mapbase-v8.0/sp/src/game/client/vgui_lootpanel.h).
  - Пересоберу обработчики:
    - OnKeyCodePressed: UP/DOWN двигают выделение в текущей колонке и скрывают действия; ENTER открывает/выполняет; LEFT/RIGHT в режиме действий выбирают action/закрывают, а в режиме навигации — переключают фокус-колонку (сохраняя текущий UX двух колонок).
    - OnKeyCodeTyped: оставлю только ESC (как в inventorypanel), уберу текущий костыль с двойным ENTER.
  - Приведу PaintBackground в CInvOptionButton к тому же правилу, что в inventorypanel: suppressArmed = AreActionsVisibleForItem(column, idx) и никаких дополнительных подавлений из-за anyActions/columnActions. Это синхронизирует подсветку.
  - Приведу OnCommand для кликов по item-кнопкам к той же идее: клик по выбранному предмету с открытыми действиями → скрыть действия; клик по другому → выделить + показать действия.
  - Инициализацию выделения в BeginLoot сделаю как в inventorypanel: если в фокус-колонке есть элементы — стартовать с 0 и сразу подогнать скролл.

## Совместимость и риски
- Не трогаю протокол/console-команды loot_put_slot/loot_take_slot/inventory_drop и т.п.; меняется только клиентский UI state-machine.
- Поведение мыши (клики/колесо) сохраню, но привяжу скрытие/показ действий к новому единому состоянию, чтобы не было рассинхрона.

## Проверка после внесения правок
- Соберу client.dll (или проект client в существующем solution) и проверю, что компиляция проходит.
- В игре: открыть lootpanel, проверить сценарии:
  - UP/DOWN двигают выделение и корректно скроллят длинные списки.
  - ENTER открывает действия, LEFT/RIGHT выбирают действие/закрывают, ENTER выполняет.
  - Переключение колонок LEFT/RIGHT работает, когда actions закрыты.
  - Подсветка item/action кнопок соответствует inventorypanel (без «лишнего» красного).

Если ок — после подтверждения сразу внесу правки в vgui_lootpanel.h/.cpp и прогоню сборку.
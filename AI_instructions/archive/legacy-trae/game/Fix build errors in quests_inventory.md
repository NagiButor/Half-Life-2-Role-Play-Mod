## Что означают ошибки
- C3861 EscapeForQuotedArg не найден: вызов стоит в глобальной функции CC_Inventory_Drop, а сама EscapeForQuotedArg объявлена как static внутри namespace InventorySystem, поэтому из глобальной области видимости она не видна.
- C2280 удалённый copy ctor PlayerData: FindOrCreatePlayerData делает `m_PlayerData.AddToTail(data)`, что требует копирования PlayerData, а у него не копируемый член (внутренний CUtlVector).
- LNK4075 /EDITANDCONTINUE игнорируется из‑за /OPT:REF: это предупреждение линкера про несовместимые флаги (обычно Debug с оптимизациями). На работоспособность не влияет.

## Правки в коде
- Inventory (Server): в [inventory_system.cpp] заменить вызов `EscapeForQuotedArg(...)` в CC_Inventory_Drop на `InventorySystem::EscapeForQuotedArg(...)`.
- Quests (Server): в [quest_system.cpp] переписать создание PlayerData без копирования:
  - вместо локального `PlayerData data; m_PlayerData.AddToTail(data);` сделать `m_PlayerData.AddToTail(); PlayerData &data = m_PlayerData.Tail();` и инициализировать поля напрямую.

## Проверка
- Собрать Server (Episodic) и убедиться, что C3861/C2280 ушли.
- Собрать Client (Episodic) и убедиться, что остался только LNK4075 (или тоже ушёл).

## (Опционально) убрать LNK4075
- Для Debug-конфига отключить Edit and Continue или убрать /OPT:REF (Linker → Optimization → References: No) чтобы не конфликтовало с /EDITANDCONTINUE.
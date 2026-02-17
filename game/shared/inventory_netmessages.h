#ifndef INVENTORY_NETMESSAGES_H
#define INVENTORY_NETMESSAGES_H

enum InventoryUpdateMsgType
{
	kInventoryUpdate_InventoryBegin = 1,
	kInventoryUpdate_InventoryAdd = 2,
	kInventoryUpdate_InventoryEnd = 3,
	kInventoryUpdate_InventoryWeight = 4,

	kInventoryUpdate_LootBegin = 10,
	kInventoryUpdate_LootAddLeft = 11,
	kInventoryUpdate_LootAddRight = 12,
	kInventoryUpdate_LootEnd = 13,
};

#endif

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
	// HL2RPM: trade mode of the loot window (sent between LootBegin and LootEnd):
	// int player money, int vendor money, string currency name, string "price;price;.." of the left
	// (player) column, same of the right (vendor) column; price -1 = the vendor doesn't buy it
	kInventoryUpdate_TradeInfo = 14,
};

#endif

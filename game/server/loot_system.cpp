#include "cbase.h"
#include "hl2rpm_char.h"
#include "loot_system.h"
#include "physics_prop_ragdoll.h"
#include "inventory_system.h"
#include "loot_container.h"
#include "recipientfilter.h"
#include "inventory_netmessages.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_items.h"
#include "hl2rpm_world.h"
#include "ai_basenpc.h"
#include "tier1/fmtstr.h"
#include <stdarg.h>

// memdbgon must be the last include file in a .cpp file
#include "tier0/memdbgon.h"

ConVar sv_loot_debug("sv_loot_debug", "0", FCVAR_GAMEDLL, "Enable server-side loot debug logging");

static void LootDevMsg(const char *fmt, ...)
{
	if (!sv_loot_debug.GetBool())
		return;
	va_list args;
	va_start(args, fmt);
	char buf[2048];
	V_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	DevMsg("%s", buf);
}

static void LootDebugDumpItems(const char *tag, const CUtlVector<CUtlString> &items)
{
	if (!sv_loot_debug.GetBool())
		return;
	LootDevMsg("[loot] %s count=%d\n", tag ? tag : "(null)", items.Count());
	const int maxDump = Min(items.Count(), 16);
	for (int i = 0; i < maxDump; ++i)
	{
		LootDevMsg("[loot]   [%d] '%s'\n", i, items[i].Get());
	}
	if (items.Count() > maxDump)
	{
		LootDevMsg("[loot]   ... (%d more)\n", items.Count() - maxDump);
	}
}

static EHANDLE s_LootTargets[MAX_PLAYERS + 1];

static CBaseEntity *GetLootTargetForPlayer(CBasePlayer *pPlayer)
{
	if (!pPlayer)
		return NULL;
	int idx = pPlayer->entindex();
	if (idx <= 0 || idx > MAX_PLAYERS)
		return NULL;
	return s_LootTargets[idx].Get();
}

static CRagdollProp *AsLootRagdoll(CBaseEntity *pEnt)
{
	if (!pEnt)
		return NULL;
	if (FClassnameIs(pEnt, "prop_ragdoll") || FClassnameIs(pEnt, "physics_prop_ragdoll"))
		return static_cast<CRagdollProp*>(pEnt);
	return NULL;
}

// HL2RPM: func_loot_container and prop_loot_container
static ILootContainer *AsLootContainer(CBaseEntity *pEnt)
{
	if (!pEnt)
		return NULL;
	return dynamic_cast<ILootContainer*>(pEnt);
}

static const char *LootTarget_GetLootItems(CBaseEntity *pEnt)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
		return pRag->GetLootItems();
	if (ILootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootItems();
	return NULL;
}

static const char *LootTarget_GetDisplayName(CBaseEntity *pEnt);

// HL2RPM (M6): what the use prompt shows for a loot target (hl2rpm_prompt.cpp); false if it isn't one
// (a ragdoll of the map without loot and without a name is not)
// HL2RPM: the owner of a container ("" - nobody's; corpses are nobody's)
const char *RPMLoot_GetOwner(CBaseEntity *pEnt)
{
	ILootContainer *pBox = AsLootContainer(pEnt);
	return pBox ? pBox->GetLootOwner() : "";
}

bool RPMLoot_DescribeTarget(CBaseEntity *pEnt, const char *&pszName, bool &bCorpse, bool &bEmpty)
{
	const char *items = LootTarget_GetLootItems(pEnt);
	if (!items)
		return false;
	pszName = LootTarget_GetDisplayName(pEnt);
	if (!pszName)
		pszName = "";
	bCorpse = AsLootRagdoll(pEnt) != NULL;
	bEmpty = !items[0];
	return !( bCorpse && bEmpty && !pszName[0] );
}

static void LootTarget_SetLootItems(CBaseEntity *pEnt, const char *items)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
	{
		pRag->SetLootItems(items);
		return;
	}
	if (ILootContainer *pBox = AsLootContainer(pEnt))
	{
		pBox->SetLootItems(items);
		return;
	}
}

static const char *LootTarget_GetDisplayName(CBaseEntity *pEnt)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
		return pRag->GetLootDisplayName();
	if (ILootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootDisplayName();
	return NULL;
}

static const char *LootTarget_GetSoundOpen(CBaseEntity *pEnt)
{
	if (ILootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootSoundOpen();
	return NULL;
}

static const char *LootTarget_GetSoundClose(CBaseEntity *pEnt)
{
	if (ILootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootSoundClose();
	return NULL;
}

static void ParseLootItems(const char *items, CUtlVector<CUtlString> &out)
{
	out.RemoveAll();
	if (!items || !items[0])
		return;
	CUtlString tmp;
	for (const char *p = items; *p; ++p)
	{
		if (*p == ';')
		{
			if (tmp.Get()[0])
				out.AddToTail(tmp);
			tmp.Clear();
		}
		else
		{
			char t[2] = { *p, '\0' };
			tmp.Append(t);
		}
	}
	if (tmp.Get()[0])
		out.AddToTail(tmp);
}

static void BuildLootItems(const CUtlVector<CUtlString> &items, CUtlString &out)
{
	out.Clear();
	for (int i = 0; i < items.Count(); ++i)
	{
		if (out.Get()[0])
			out.Append(";");
		out.Append(items[i].Get());
	}
}

static void ExtractClassnameAndBullets(const char *raw, char *cls, int clsSize, int &bullets, bool &hasBullets)
{
	bullets = 0;
	hasBullets = false;
	if (!cls || clsSize <= 0)
		return;
	cls[0] = '\0';
	if (!raw)
		return;
	const char *sep = strchr(raw, ':');
	if (sep)
	{
		int len = sep - raw;
		if (len > clsSize - 1) len = clsSize - 1;
		Q_strncpy(cls, raw, len + 1);
		cls[len] = '\0';
		const char *num = sep + 1;
		bullets = atoi(num);
		hasBullets = true;
	}
	else
	{
		Q_strncpy(cls, raw, clsSize);
	}
}

static void NormalizeClassname(const char *raw, char *out, int outSize)
{
	if (!out || outSize <= 0)
		return;
	out[0] = '\0';
	if (!raw)
		return;
	int o = 0;
	for (const char *p = raw; *p && o < outSize - 1; ++p)
	{
		char c = *p;
		if (c == '\\' && p[1])
		{
			c = *++p;
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
		{
			out[o++] = c;
		}
	}
	out[o] = '\0';
}

static void AddLootItem(CUtlVector<CUtlString> &items, const char *classname, int bullets, bool isAmmo);

// HL2RPM: entries that keep a count ":N" in loot strings and merge - ammo (bullets) and the stackable
// items of the item database (tokens, bobby pins, rations...: the count of the stack). Before, a stack
// lost its count here: 60 tokens in a locker gave the player 1, a vendor's 8 bobby pins became 1.
static bool IsLootStack(const char *cls)
{
	return InventorySystem::IsAmmoItem(cls) || RPMItems::IsStackable(cls);
}

static int DefaultStackCount(const char *cls)
{
	return InventorySystem::IsAmmoItem(cls) ? InventorySystem::GetBulletsPerAmmoItem(cls) : 1;
}

// gives the player a non-weapon loot entry: ammo with its bullets, a stack with its count
static void GiveLootEntry(CBasePlayer *pPlayer, const char *cls, int count, bool hasCount)
{
	if (IsLootStack(cls))
		InventorySystem::AddItemToPlayerWithBullets(pPlayer, cls, (hasCount && count > 0) ? count : DefaultStackCount(cls));
	else
		InventorySystem::AddItemToPlayer(pPlayer, cls);
}

static void NormalizeLootItems(CUtlVector<CUtlString> &items);

static void AddLootItem(CUtlVector<CUtlString> &items, const char *classname, int bullets, bool isAmmo)
{
	if (!classname || !classname[0])
		return;

	char ncls[256];
	NormalizeClassname(classname, ncls, sizeof(ncls));
	if (!ncls[0])
		return;

	if (isAmmo || RPMItems::IsStackable(ncls))
	{
		if (bullets <= 0)
			bullets = DefaultStackCount(ncls);
		int idxMatch = -1;
		int existingBullets = 0;
		for (int i = 0; i < items.Count(); ++i)
		{
			char cls[256], nExisting[256];
			int b = 0;
			bool hasB = false;
			ExtractClassnameAndBullets(items[i].Get(), cls, sizeof(cls), b, hasB);
			NormalizeClassname(cls, nExisting, sizeof(nExisting));
			if (!Q_stricmp(nExisting, ncls))
			{
				idxMatch = i;
				existingBullets = hasB ? b : DefaultStackCount(ncls);
				break;
			}
		}
		int total = existingBullets + bullets;
		char buf[256];
		Q_snprintf(buf, sizeof(buf), "%s:%d", ncls, total);
		if (idxMatch >= 0)
			items[idxMatch] = buf;
		else
			items.AddToTail(buf);
		return;
	}
	items.AddToTail(ncls);
}

static void ExtractLootEntryFull(const char *raw, char *cls, int clsSize, int &bullets, bool &hasBullets, bool &isWeapon, int &clip1, int &clip2, int &reserveIdx, int &reserveCount)
{
	bullets = 0;
	hasBullets = false;
	isWeapon = false;
	clip1 = 0;
	clip2 = 0;
	reserveIdx = -1;
	reserveCount = 0;
	if (!cls || clsSize <= 0)
		return;
	cls[0] = '\0';
	if (!raw || !raw[0])
		return;

	char buf[512];
	Q_strncpy(buf, raw, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';

	char *tok[6];
	int ntok = 0;
	char *p = buf;
	tok[ntok++] = p;
	for (; *p && ntok < 6; ++p)
	{
		if (*p == ':')
		{
			*p = '\0';
			tok[ntok++] = p + 1;
		}
	}

	NormalizeClassname(tok[0], cls, clsSize);
	if (!cls[0])
		return;

	if (ntok >= 2)
	{
		bullets = atoi(tok[1]);
		hasBullets = true;
	}

	if (ntok >= 6)
	{
		isWeapon = true;
		clip1 = atoi(tok[2]);
		clip2 = atoi(tok[3]);
		reserveIdx = atoi(tok[4]);
		reserveCount = atoi(tok[5]);
	}
	else if (!Q_strnicmp(cls, "weapon_", 7))
	{
		isWeapon = true;
	}
}

static void NormalizeLootItems(CUtlVector<CUtlString> &items)
{
	CUtlVector<CUtlString> out;
	out.EnsureCapacity(items.Count());

	for (int i = 0; i < items.Count(); ++i)
	{
		char cls[256];
		int bullets = 0;
		bool hasBullets = false;
		bool isWeapon = false;
		int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
		ExtractLootEntryFull(items[i].Get(), cls, sizeof(cls), bullets, hasBullets, isWeapon, clip1, clip2, reserveIdx, reserveCount);
		if (!cls[0])
			continue;

		if (!isWeapon && IsLootStack(cls))
		{
			int b = bullets;
			if (!hasBullets || b <= 0)
				b = DefaultStackCount(cls);

			int match = -1;
			for (int j = 0; j < out.Count(); ++j)
			{
				char ecls[256];
				int ebullets = 0;
				bool ehasBullets = false;
				bool eisWeapon = false;
				int eclip1 = 0, eclip2 = 0, ereserveIdx = -1, ereserveCount = 0;
				ExtractLootEntryFull(out[j].Get(), ecls, sizeof(ecls), ebullets, ehasBullets, eisWeapon, eclip1, eclip2, ereserveIdx, ereserveCount);
				if (!ecls[0] || eisWeapon)
					continue;
				if (!IsLootStack(ecls))
					continue;
				if (Q_stricmp(ecls, cls))
					continue;
				match = j;
				int existing = (ehasBullets && ebullets > 0) ? ebullets : DefaultStackCount(cls);
				int total = existing + b;
				out[j] = UTIL_VarArgs("%s:%d", cls, total);
				break;
			}
			if (match < 0)
				out.AddToTail(UTIL_VarArgs("%s:%d", cls, b));
			continue;
		}

		if (isWeapon)
		{
			out.AddToTail(UTIL_VarArgs("%s:%d:%d:%d:%d:%d", cls, bullets, clip1, clip2, reserveIdx, reserveCount));
			continue;
		}

		out.AddToTail(cls);
	}

	items.RemoveAll();
	for (int i = 0; i < out.Count(); ++i)
		items.AddToTail(out[i]);
}

static void AddLootRawEntry(CUtlVector<CUtlString> &items, const char *rawEntry)
{
	if (!rawEntry || !rawEntry[0])
		return;

	char cls[256];
	int bullets = 0;
	bool hasBullets = false;
	bool isWeapon = false;
	int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
	ExtractLootEntryFull(rawEntry, cls, sizeof(cls), bullets, hasBullets, isWeapon, clip1, clip2, reserveIdx, reserveCount);
	if (!cls[0])
		return;

	bool isAmmo = !isWeapon && IsLootStack(cls);
	if (isAmmo)
	{
		if (!hasBullets || bullets <= 0)
			bullets = DefaultStackCount(cls);
		for (int i = 0; i < items.Count(); ++i)
		{
			char ecls[256];
			int ebullets = 0;
			bool ehasBullets = false;
			bool eisWeapon = false;
			int eclip1 = 0, eclip2 = 0, ereserveIdx = -1, ereserveCount = 0;
			ExtractLootEntryFull(items[i].Get(), ecls, sizeof(ecls), ebullets, ehasBullets, eisWeapon, eclip1, eclip2, ereserveIdx, ereserveCount);
			if (!ecls[0] || eisWeapon)
				continue;
			if (Q_stricmp(ecls, cls))
				continue;
			int total = (ehasBullets ? ebullets : DefaultStackCount(cls)) + bullets;
			items[i] = UTIL_VarArgs("%s:%d", cls, total);
			return;
		}
		items.AddToTail(UTIL_VarArgs("%s:%d", cls, bullets));
		return;
	}

	items.AddToTail(rawEntry);
}

static void RestoreInventoryEntry(CBasePlayer *pPlayer, const InventorySystem::InvItem &inv)
{
	if (!pPlayer)
		return;
	char cls[256];
	NormalizeClassname(inv.classname.Get(), cls, sizeof(cls));
	if (!cls[0])
		return;
	if (inv.isWeapon || !Q_strnicmp(cls, "weapon_", 7))
	{
		InventorySystem::AddWeaponToPlayer(pPlayer, cls, inv.weaponClip1, inv.weaponClip2, inv.reserveAmmoIndex, inv.reserveAmmoCount);
		return;
	}
	GiveLootEntry(pPlayer, cls, inv.bullets, true);
}

//=============================================================================
// HL2RPM: trade. An NPC of the Creation Kit with a "vendor" block (scripts/npcs)
// trades in the loot window: the player's column sells, the vendor's column buys.
// The vendor's stock and money live in the RPG state ("vendors"/<targetname>):
// they go into the save game and over level changes; the stock is rolled from
// the vendor's list again every restock_hours of game time.
//=============================================================================
static bool s_bLootTrade[MAX_PLAYERS + 1];

static bool IsTrading(CBasePlayer *pPlayer)
{
	int idx = pPlayer ? pPlayer->entindex() : 0;
	return idx > 0 && idx <= MAX_PLAYERS && s_bLootTrade[idx] && s_LootTargets[idx].Get();
}

namespace RPMTrade
{
	KeyValues *VendorDef(CBaseEntity *pNPC)
	{
		if (!pNPC || pNPC->GetEntityName() == NULL_STRING)
			return NULL;
		const RPMNPCDef_t *pDef = RPMWorld::FindNPCDef(STRING(pNPC->GetEntityName()));
		return pDef ? pDef->pVendor : NULL;
	}
}

static const char *TradeCurrency(KeyValues *pVendor)
{
	const char *c = pVendor ? pVendor->GetString("currency", "") : "";
	return c[0] ? c : "rpm_credits";
}

// the vendor's state: stock (loot string), money; refilled when the restock time has come
static KeyValues *VendorState(CBaseEntity *pNPC, KeyValues *pVendor, CBasePlayer *pPlayer)
{
	KeyValues *v = RPMState::Section("vendors")->FindKey(STRING(pNPC->GetEntityName()), true);
	double now = RPMState::GameHours();
	float restock = pVendor->GetFloat("restock_hours", 72.0f);
	if (!v->GetInt("init") || (restock > 0.0f && now >= v->GetFloat("restock_at")))
	{
		CUtlString rolled;
		RPMLoot::Resolve(pVendor->GetString("list"), rolled, pPlayer);
		CUtlVector<CUtlString> items;
		ParseLootItems(rolled.Get(), items);
		NormalizeLootItems(items);
		CUtlString stock;
		BuildLootItems(items, stock);
		v->SetString("stock", stock.Get());
		v->SetInt("money", pVendor->GetInt("money", 200));
		v->SetFloat("restock_at", (float)(now + Max(restock, 1.0f)));
		v->SetInt("init", 1);
	}
	return v;
}

// value of a loot / inventory entry: ammo by its bullets (value and weight of ammo are per bullet),
// stackables by their count
static int EntryValue(const char *cls, int bullets, bool hasBullets)
{
	// the database value, else the built-in price of stock items (they had 0 - vendors gave them away)
	const int value = RPMItems::Value(cls);
	if (value <= 0)
		return 0;
	if (InventorySystem::IsAmmoItem(cls))
	{
		int b = (hasBullets && bullets > 0) ? bullets : Max(1, InventorySystem::GetBulletsPerAmmoItem(cls));
		return value * b;
	}
	if (RPMItems::IsStackable(cls))
		return value * Max(1, hasBullets ? bullets : 1);
	return value;
}

static void ParseEntry(const char *raw, char *cls, int clsSize, int &bullets, bool &hasBullets)
{
	bool isWeapon; int c1, c2, ri, rc;
	ExtractLootEntryFull(raw, cls, clsSize, bullets, hasBullets, isWeapon, c1, c2, ri, rc);
}

// what the player pays for a vendor's entry / gets for his own (-1: the vendor doesn't take it)
static bool TradesByOne(const char *cls)
{
	return !InventorySystem::IsAmmoItem(cls) && RPMItems::IsStackable(cls);
}

static int BuyPrice(KeyValues *pVendor, const char *raw)
{
	char cls[256]; int b; bool hb;
	ParseEntry(raw, cls, sizeof(cls), b, hb);
	int v = TradesByOne(cls) ? RPMItems::Value(cls) : EntryValue(cls, b, hb);
	return v > 0 ? Max(1, (int)ceilf(v * pVendor->GetFloat("markup", 1.25f) * RPMChar::PriceScale(true))) : 0;
}

static int SellPrice(KeyValues *pVendor, const char *raw)
{
	char cls[256]; int b; bool hb;
	ParseEntry(raw, cls, sizeof(cls), b, hb);
	const RPMItemDef_t *pDef = RPMItems::Find(cls);
	if ((pDef && (pDef->quest || !Q_stricmp(pDef->category, "currency"))) || !Q_stricmp(cls, TradeCurrency(pVendor)) ||
		!Q_stricmp(cls, "rpm_credits"))
		return -1;
	int v = TradesByOne(cls) ? RPMItems::Value(cls) : EntryValue(cls, b, hb);
	if (v <= 0)
		return -1;
	return Max(1, (int)floorf(v * Min(1.0f, pVendor->GetFloat("pays", 0.5f) * RPMChar::PriceScale(false))));
}

static void TradeToast(CBasePlayer *pPlayer, const char *ru, const char *en, const char *snd)
{
	bool bRu = !Q_stricmp(RPMItems::Language(), "russian");
	engine->ClientCommand(pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"%s\"\n", bRu ? ru : en, snd);
}

static void SendTradeWindow(CBasePlayer *pPlayer, CBaseEntity *pNPC)
{
	KeyValues *pVendor = RPMTrade::VendorDef(pNPC);
	if (!pPlayer || !pNPC || !pVendor)
		return;
	KeyValues *v = VendorState(pNPC, pVendor, pPlayer);
	int idx = pPlayer->entindex();
	if (idx <= 0 || idx > MAX_PLAYERS)
		return;
	s_LootTargets[idx] = pNPC;
	s_bLootTrade[idx] = true;

	CUtlVector<CUtlString> leftItems, rightItems;
	InventorySystem::GetInventoryItemStringsForPlayer(pPlayer, leftItems);
	ParseLootItems(v->GetString("stock"), rightItems);

	CUtlString leftPrices, rightPrices;
	for (int i = 0; i < leftItems.Count(); ++i)
		leftPrices.Append(CFmtStr("%s%d", i ? ";" : "", SellPrice(pVendor, leftItems[i].Get())));
	for (int i = 0; i < rightItems.Count(); ++i)
		rightPrices.Append(CFmtStr("%s%d", i ? ";" : "", BuyPrice(pVendor, rightItems[i].Get())));

	const char *cur = TradeCurrency(pVendor);
	CAI_BaseNPC *pAI = pNPC->MyNPCPointer();
	const char *displayName = pAI ? pAI->GetLootDisplayName() : "";

	CSingleUserRecipientFilter user(pPlayer);
	user.MakeReliable();
	UserMessageBegin(user, "Inventory_Update");
	WRITE_BYTE(kInventoryUpdate_LootBegin);
	WRITE_STRING(displayName ? displayName : "");
	WRITE_STRING("");
	WRITE_STRING("");
	MessageEnd();

	UserMessageBegin(user, "Inventory_Update");
	WRITE_BYTE(kInventoryUpdate_TradeInfo);
	WRITE_LONG(InventorySystem::CountItem(pPlayer, cur));
	WRITE_LONG(v->GetInt("money"));
	WRITE_STRING(RPMItems::Name(cur));
	WRITE_STRING(leftPrices.Get());
	WRITE_STRING(rightPrices.Get());
	MessageEnd();

	for (int i = 0; i < leftItems.Count(); ++i)
	{
		UserMessageBegin(user, "Inventory_Update");
		WRITE_BYTE(kInventoryUpdate_LootAddLeft);
		WRITE_STRING(leftItems[i].Get());
		MessageEnd();
	}
	for (int i = 0; i < rightItems.Count(); ++i)
	{
		UserMessageBegin(user, "Inventory_Update");
		WRITE_BYTE(kInventoryUpdate_LootAddRight);
		WRITE_STRING(rightItems[i].Get());
		MessageEnd();
	}
	UserMessageBegin(user, "Inventory_Update");
	WRITE_BYTE(kInventoryUpdate_LootEnd);
	MessageEnd();
}

// the player buys the vendor's entry <slot>
static void Trade_Buy(CBasePlayer *pPlayer, int slot)
{
	CBaseEntity *pNPC = GetLootTargetForPlayer(pPlayer);
	KeyValues *pVendor = RPMTrade::VendorDef(pNPC);
	if (!pVendor)
		return;
	KeyValues *v = VendorState(pNPC, pVendor, pPlayer);
	CUtlVector<CUtlString> items;
	ParseLootItems(v->GetString("stock"), items);
	if (slot < 0 || slot >= items.Count())
	{
		SendTradeWindow(pPlayer, pNPC);
		return;
	}
	const char *cur = TradeCurrency(pVendor);
	int price = BuyPrice(pVendor, items[slot].Get());
	if (InventorySystem::CountItem(pPlayer, cur) < price)
	{
		TradeToast(pPlayer, CFmtStr("Не хватает: %s %d", RPMItems::Name(cur), price), CFmtStr("Not enough %s: %d", RPMItems::Name(cur), price),
			"buttons/button10.wav");
		SendTradeWindow(pPlayer, pNPC);
		return;
	}
	char cls[256]; int bullets = 0; bool hasBullets = false, isWeapon = false; int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
	ExtractLootEntryFull(items[slot].Get(), cls, sizeof(cls), bullets, hasBullets, isWeapon, clip1, clip2, reserveIdx, reserveCount);
	if (!cls[0])
		return;
	InventorySystem::TakeItems(pPlayer, cur, price);
	if (TradesByOne(cls))
	{
		InventorySystem::AddItemToPlayerWithBullets(pPlayer, cls, 1);
		const int left = (hasBullets ? bullets : 1) - 1;
		if (left > 0)
			items[slot] = UTIL_VarArgs("%s:%d", cls, left);
		else
			items.Remove(slot);
	}
	else
	{
		if (isWeapon && !InventorySystem::IsAmmoItem(cls))
			InventorySystem::AddWeaponToPlayer(pPlayer, cls, clip1, clip2, reserveIdx, reserveCount);
		else
			GiveLootEntry(pPlayer, cls, bullets, hasBullets);
		items.Remove(slot);
	}
	CUtlString stock;
	BuildLootItems(items, stock);
	v->SetString("stock", stock.Get());
	v->SetInt("money", v->GetInt("money") + price);
	CPASAttenuationFilter filter(pPlayer, "ambient/levels/labs/coinslot1.wav");
	filter.MakeReliable();
	CBaseEntity::EmitSound(filter, pPlayer->entindex(), "ambient/levels/labs/coinslot1.wav");
	SendTradeWindow(pPlayer, pNPC);
}

// the player sells his inventory slot <slot>
static void Trade_Sell(CBasePlayer *pPlayer, int slot)
{
	CBaseEntity *pNPC = GetLootTargetForPlayer(pPlayer);
	KeyValues *pVendor = RPMTrade::VendorDef(pNPC);
	if (!pVendor)
		return;
	KeyValues *v = VendorState(pNPC, pVendor, pPlayer);
	CUtlVector<CUtlString> mine;
	InventorySystem::GetInventoryItemStringsForPlayer(pPlayer, mine);
	if (slot < 0 || slot >= mine.Count())
	{
		SendTradeWindow(pPlayer, pNPC);
		return;
	}
	int price = SellPrice(pVendor, mine[slot].Get());
	if (price < 0)
	{
		TradeToast(pPlayer, "Это не продаётся", "Can't sell this", "buttons/button10.wav");
		SendTradeWindow(pPlayer, pNPC);
		return;
	}
	if (v->GetInt("money") < price)
	{
		TradeToast(pPlayer, "У торговца не хватает денег", "The vendor can't afford it", "buttons/button10.wav");
		SendTradeWindow(pPlayer, pNPC);
		return;
	}
	char cls[256];
	int bullets = 0;
	InventorySystem::InvItem inv;
	inv.bullets = 0;
	inv.isWeapon = false;
	inv.weaponClip1 = inv.weaponClip2 = 0;
	inv.reserveAmmoIndex = -1;
	inv.reserveAmmoCount = 0;
	{
		char raw[256]; int b; bool hb;
		ParseEntry(mine[slot].Get(), raw, sizeof(raw), b, hb);
		NormalizeClassname(raw, cls, sizeof(cls));
	}
	if (TradesByOne(cls) && InventorySystem::CountItem(pPlayer, cls) > 1)
	{
		// a stack: one unit goes to the vendor
		InventorySystem::TakeItems(pPlayer, cls, 1);
		inv.classname = cls;
		bullets = 1;
	}
	else
	{
		if (!InventorySystem::PopInventoryEntryAtIndex(pPlayer, slot, inv))
		{
			SendTradeWindow(pPlayer, pNPC);
			return;
		}
		NormalizeClassname(inv.classname.Get(), cls, sizeof(cls));
		bullets = inv.bullets;
		if (IsLootStack(cls) && bullets <= 0)
			bullets = DefaultStackCount(cls);
	}
	CUtlString entry;
	if (inv.isWeapon || !Q_strnicmp(cls, "weapon_", 7))
		entry = UTIL_VarArgs("%s:%d:%d:%d:%d:%d", cls, bullets, inv.weaponClip1, inv.weaponClip2, inv.reserveAmmoIndex, inv.reserveAmmoCount);
	else
		entry = UTIL_VarArgs("%s:%d", cls, bullets);
	CUtlVector<CUtlString> items;
	ParseLootItems(v->GetString("stock"), items);
	AddLootRawEntry(items, entry.Get());
	NormalizeLootItems(items);
	CUtlString stock;
	BuildLootItems(items, stock);
	v->SetString("stock", stock.Get());
	v->SetInt("money", v->GetInt("money") - price);
	InventorySystem::AddItemToPlayerWithBullets(pPlayer, TradeCurrency(pVendor), price);
	CPASAttenuationFilter filter(pPlayer, "ambient/levels/labs/coinslot1.wav");
	filter.MakeReliable();
	CBaseEntity::EmitSound(filter, pPlayer->entindex(), "ambient/levels/labs/coinslot1.wav");
	SendTradeWindow(pPlayer, pNPC);
}

// opening waits a moment: the dialog window closes first (its end releases the modal input)
static CHandle<CBasePlayer> s_hTradePlayer;
static EHANDLE s_hTradeNPC;
static float s_flTradeOpenAt = -1.0f;

class CRPMTradeSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMTradeSystem() : CAutoGameSystemPerFrame("CRPMTradeSystem") {}
	virtual void LevelInitPreEntity()
	{
		s_flTradeOpenAt = -1.0f;
		for (int i = 0; i <= MAX_PLAYERS; ++i)
			s_bLootTrade[i] = false;
	}
	virtual void FrameUpdatePostEntityThink()
	{
		if (s_flTradeOpenAt < 0.0f || gpGlobals->curtime < s_flTradeOpenAt)
			return;
		s_flTradeOpenAt = -1.0f;
		if (s_hTradePlayer.Get() && s_hTradeNPC.Get() && s_hTradeNPC->IsAlive())
			SendTradeWindow(s_hTradePlayer.Get(), s_hTradeNPC.Get());
	}
};
static CRPMTradeSystem g_RPMTradeSystem;

bool RPMTrade::Open(CBasePlayer *pPlayer, CBaseEntity *pNPC)
{
	if (!pPlayer || !pNPC || !VendorDef(pNPC))
	{
		if (pNPC)
			Warning("RPMTrade: %s is not a vendor (no \"vendor\" block in scripts/npcs)\n", STRING(pNPC->GetEntityName()));
		return false;
	}
	s_hTradePlayer = pPlayer;
	s_hTradeNPC = pNPC;
	s_flTradeOpenAt = gpGlobals->curtime + 0.15f;
	return true;
}

static void CC_RPM_Trade(const CCommand &args)
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer || args.ArgC() < 2)
	{
		Msg("rpm_trade <targetname of a vendor NPC>\n");
		return;
	}
	CBaseEntity *pNPC = gEntList.FindEntityByName(NULL, args.Arg(1));
	if (!pNPC)
	{
		Msg("rpm_trade: no entity '%s'\n", args.Arg(1));
		return;
	}
	RPMTrade::Open(pPlayer, pNPC);
}
static ConCommand rpm_trade_cc("rpm_trade", CC_RPM_Trade, "HL2RPM: rpm_trade <vendor> - open the trade window of a vendor NPC", FCVAR_CHEAT);

static void CC_RPM_Vendor_Dump(const CCommand &args)
{
	KeyValues *vs = RPMState::Section("vendors");
	for (KeyValues *k = vs->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey())
		Msg("vendor %s: money %d, restock at %.1f h (now %.1f), stock '%s'\n", k->GetName(), k->GetInt("money"), k->GetFloat("restock_at"),
			RPMState::GameHours(), k->GetString("stock"));
}
static ConCommand rpm_vendor_dump_cc("rpm_vendor_dump", CC_RPM_Vendor_Dump, "HL2RPM: print the vendors' stock and money");

static void CC_Loot_PutSlot(const CCommand &args)
{
	if (args.ArgC() < 2)
		return;
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;

	int slot = atoi(args.Arg(1));
	if (IsTrading(pPlayer))
	{
		Trade_Sell(pPlayer, slot);
		return;
	}
	LootDevMsg("[loot] put_slot player=%d ent=%d slot=%d\n", pPlayer->entindex(), pLootEnt->entindex(), slot);
	InventorySystem::InvItem inv;
	if (!InventorySystem::PopInventoryEntryAtIndex(pPlayer, slot, inv))
	{
		LootDevMsg("[loot] put_slot pop failed\n");
		OpenLootForPlayer(pPlayer, pLootEnt);
		return;
	}

	char cls[256];
	NormalizeClassname(inv.classname.Get(), cls, sizeof(cls));
	if (!cls[0])
	{
		RestoreInventoryEntry(pPlayer, inv);
		OpenLootForPlayer(pPlayer, pLootEnt);
		return;
	}

	int bullets = inv.bullets;
	if (IsLootStack(cls) && bullets <= 0)
		bullets = DefaultStackCount(cls);

	CUtlString entry;
	if (inv.isWeapon || !Q_strnicmp(cls, "weapon_", 7))
		entry = UTIL_VarArgs("%s:%d:%d:%d:%d:%d", cls, bullets, inv.weaponClip1, inv.weaponClip2, inv.reserveAmmoIndex, inv.reserveAmmoCount);
	else
		entry = UTIL_VarArgs("%s:%d", cls, bullets);

	CUtlVector<CUtlString> items;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), items);
	LootDebugDumpItems("put_slot before", items);
	AddLootRawEntry(items, entry.Get());
	NormalizeLootItems(items);
	LootDebugDumpItems("put_slot after", items);
	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}

static void CC_Loot_TakeSlot(const CCommand &args)
{
	if (args.ArgC() < 2)
		return;
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;

	int slot = atoi(args.Arg(1));
	if (IsTrading(pPlayer))
	{
		Trade_Buy(pPlayer, slot);
		return;
	}
	LootDevMsg("[loot] take_slot player=%d ent=%d slot=%d\n", pPlayer->entindex(), pLootEnt->entindex(), slot);
	CUtlVector<CUtlString> items;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), items);
	LootDebugDumpItems("take_slot before", items);
	if (slot < 0 || slot >= items.Count())
	{
		LootDevMsg("[loot] take_slot slot out of range count=%d\n", items.Count());
		OpenLootForPlayer(pPlayer, pLootEnt);
		return;
	}

	char cls[256];
	int bullets = 0;
	bool hasBullets = false;
	bool isWeapon = false;
	int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
	ExtractLootEntryFull(items[slot].Get(), cls, sizeof(cls), bullets, hasBullets, isWeapon, clip1, clip2, reserveIdx, reserveCount);
	if (!cls[0])
	{
		OpenLootForPlayer(pPlayer, pLootEnt);
		return;
	}

	items.Remove(slot);
	NormalizeLootItems(items);
	LootDebugDumpItems("take_slot after", items);

	if (isWeapon && !InventorySystem::IsAmmoItem(cls))
		InventorySystem::AddWeaponToPlayer(pPlayer, cls, clip1, clip2, reserveIdx, reserveCount);
	else
		GiveLootEntry(pPlayer, cls, bullets, hasBullets);

	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	RPMTheft::OnTake(pPlayer, pLootEnt, RPMLoot_GetOwner(pLootEnt));
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}

void OpenLootForPlayer(CBasePlayer *pPlayer, CBaseEntity *pLootEnt)
{
    if (!pPlayer || !pLootEnt)
        return;

	// HL2RPM: loot lists ("list:<id>") are rolled the first time the loot is opened
	{
		const char *raw = LootTarget_GetLootItems(pLootEnt);
		CUtlString rolled;
		if (raw && RPMLoot::Resolve(raw, rolled, pPlayer))
			LootTarget_SetLootItems(pLootEnt, rolled.Get());
	}

	LootDevMsg("[loot] open player=%d ent=%d loot='%s'\n", pPlayer->entindex(), pLootEnt->entindex(), LootTarget_GetLootItems(pLootEnt) ? LootTarget_GetLootItems(pLootEnt) : "(null)");

	int idx = pPlayer->entindex();
	if (idx > 0 && idx <= MAX_PLAYERS)
	{
		s_LootTargets[idx] = pLootEnt;
		s_bLootTrade[idx] = false;
	}

	CUtlVector<CUtlString> leftItems;
	InventorySystem::GetInventoryItemStringsForPlayer(pPlayer, leftItems);

	CUtlVector<CUtlString> rightItems;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), rightItems);
	LootDebugDumpItems("open before", rightItems);
	NormalizeLootItems(rightItems);
	LootDebugDumpItems("open after", rightItems);

	CUtlString rightPayload;
	BuildLootItems(rightItems, rightPayload);
	LootTarget_SetLootItems(pLootEnt, rightPayload.Get());

	const char *displayName = LootTarget_GetDisplayName(pLootEnt);
	const char *soundOpen = LootTarget_GetSoundOpen(pLootEnt);
	const char *soundClose = LootTarget_GetSoundClose(pLootEnt);

	CSingleUserRecipientFilter user(pPlayer);
	user.MakeReliable();

	UserMessageBegin(user, "Inventory_Update");
	WRITE_BYTE(kInventoryUpdate_LootBegin);
	WRITE_STRING(displayName ? displayName : "");
	WRITE_STRING(soundOpen ? soundOpen : "");
	WRITE_STRING(soundClose ? soundClose : "");
	MessageEnd();

	for (int i = 0; i < leftItems.Count(); ++i)
	{
		UserMessageBegin(user, "Inventory_Update");
		WRITE_BYTE(kInventoryUpdate_LootAddLeft);
		WRITE_STRING(leftItems[i].Get());
		MessageEnd();
	}

	for (int i = 0; i < rightItems.Count(); ++i)
	{
		UserMessageBegin(user, "Inventory_Update");
		WRITE_BYTE(kInventoryUpdate_LootAddRight);
		WRITE_STRING(rightItems[i].Get());
		MessageEnd();
	}

	UserMessageBegin(user, "Inventory_Update");
	WRITE_BYTE(kInventoryUpdate_LootEnd);
	MessageEnd();
}

static void CC_Loot_Put(const CCommand &args)
{
	if (args.ArgC() < 2)
		return;
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	if (IsTrading(pPlayer))		// HL2RPM: a vendor's window moves items only through buy / sell
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;
	char clsBuf[256];
	NormalizeClassname(args.Arg(1), clsBuf, sizeof(clsBuf));
	const char *classname = clsBuf;
	if (!classname || !classname[0])
		return;
	int bullets = InventorySystem::RemoveItemFromPlayer(pPlayer, classname);
	if (bullets < 0)
		return;
	bool isAmmo = InventorySystem::IsAmmoItem(classname);
	if (isAmmo && bullets <= 0)
		bullets = InventorySystem::GetBulletsPerAmmoItem(classname);
	CUtlVector<CUtlString> items;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), items);
	NormalizeLootItems(items);
	AddLootItem(items, classname, bullets, isAmmo);
	NormalizeLootItems(items);
	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}


static void CC_Loot_Take(const CCommand &args)
{
	if (args.ArgC() < 2)
		return;
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	if (IsTrading(pPlayer))		// HL2RPM: a vendor's window moves items only through buy / sell
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;
	char clsBuf[256];
	NormalizeClassname(args.Arg(1), clsBuf, sizeof(clsBuf));
	const char *classname = clsBuf;
	if (!classname || !classname[0])
		return;
	CUtlVector<CUtlString> items;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), items);
	NormalizeLootItems(items);
	int match = -1;
	int bullets = 0;
	bool hasBullets = false;
	bool isWeapon = false;
	int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
	for (int i = 0; i < items.Count(); ++i)
	{
		char cls[256];
		int b = 0;
		bool hasB = false;
		bool w = false;
		int c1 = 0, c2 = 0, rIdx = -1, rCnt = 0;
		ExtractLootEntryFull(items[i].Get(), cls, sizeof(cls), b, hasB, w, c1, c2, rIdx, rCnt);
		if (!Q_stricmp(cls, classname))
		{
			match = i;
			bullets = b;
			hasBullets = hasB;
			isWeapon = w;
			clip1 = c1;
			clip2 = c2;
			reserveIdx = rIdx;
			reserveCount = rCnt;
			break;
		}
	}
	if (match < 0)
		return;
	items.Remove(match);
	NormalizeLootItems(items);

	if (isWeapon)
		InventorySystem::AddWeaponToPlayer(pPlayer, classname, clip1, clip2, reserveIdx, reserveCount);
	else
		GiveLootEntry(pPlayer, classname, bullets, hasBullets);
	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	RPMTheft::OnTake(pPlayer, pLootEnt, RPMLoot_GetOwner(pLootEnt));
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}


static void CC_Loot_PutAll(const CCommand &args)
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	if (IsTrading(pPlayer))		// HL2RPM: a vendor's window moves items only through buy / sell
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;

	CUtlString payload = InventorySystem::GetInventoryPayloadForPlayer(pPlayer);
	CUtlVector<CUtlString> invItems;
	ParseLootItems(payload.Get(), invItems);

	CUtlVector<CUtlString> lootItems;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), lootItems);
	NormalizeLootItems(lootItems);

	for (int i = invItems.Count() - 1; i >= 0; --i)
	{
		InventorySystem::InvItem inv;
		if (!InventorySystem::PopInventoryEntryAtIndex(pPlayer, i, inv))
			continue;

		char cls[256];
		NormalizeClassname(inv.classname.Get(), cls, sizeof(cls));
		if (!cls[0])
			continue;

		if (inv.isWeapon || (Q_strnicmp(cls, "weapon_", 7) == 0))
		{
			char buf[256];
			Q_snprintf(buf, sizeof(buf), "%s:%d:%d:%d:%d:%d", cls, inv.bullets, inv.weaponClip1, inv.weaponClip2, inv.reserveAmmoIndex, inv.reserveAmmoCount);
			lootItems.AddToTail(buf);
		}
		else
		{
			const bool isAmmo = InventorySystem::IsAmmoItem(cls);
			int b = inv.bullets;
			if (isAmmo && b <= 0)
				b = InventorySystem::GetBulletsPerAmmoItem(cls);
			AddLootItem(lootItems, cls, b, isAmmo);
		}
	}

	NormalizeLootItems(lootItems);
	CUtlString newLoot;
	BuildLootItems(lootItems, newLoot);
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}

static void CC_Loot_TakeAll(const CCommand &args)
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	if (IsTrading(pPlayer))		// HL2RPM: a vendor's window moves items only through buy / sell
		return;
	CBaseEntity *pLootEnt = GetLootTargetForPlayer(pPlayer);
	if (!pLootEnt)
		return;

	CUtlVector<CUtlString> lootItems;
	ParseLootItems(LootTarget_GetLootItems(pLootEnt), lootItems);
	NormalizeLootItems(lootItems);

	for (int i = 0; i < lootItems.Count(); ++i)
	{
		char rawCls[256];
		int bullets = 0;
		bool hasBullets = false;
		bool isWeapon = false;
		int clip1 = 0, clip2 = 0, reserveIdx = -1, reserveCount = 0;
		ExtractLootEntryFull(lootItems[i].Get(), rawCls, sizeof(rawCls), bullets, hasBullets, isWeapon, clip1, clip2, reserveIdx, reserveCount);

		char cls[256];
		NormalizeClassname(rawCls, cls, sizeof(cls));
		if (!cls[0])
			continue;

		if (isWeapon && !InventorySystem::IsAmmoItem(cls))
			InventorySystem::AddWeaponToPlayer(pPlayer, cls, clip1, clip2, reserveIdx, reserveCount);
		else
			GiveLootEntry(pPlayer, cls, bullets, hasBullets);
	}

	RPMTheft::OnTake(pPlayer, pLootEnt, RPMLoot_GetOwner(pLootEnt));
	LootTarget_SetLootItems(pLootEnt, "");
	OpenLootForPlayer(pPlayer, pLootEnt);
}

static ConCommand loot_put_cc("loot_put", CC_Loot_Put, "Move item from player inventory to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_cc("loot_take", CC_Loot_Take, "Move item from ragdoll to player inventory", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_put_all_cc("loot_put_all", CC_Loot_PutAll, "Move all items from player inventory to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_all_cc("loot_take_all", CC_Loot_TakeAll, "Move all items from ragdoll to player inventory", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_put_slot_cc("loot_put_slot", CC_Loot_PutSlot, "Move inventory slot from player to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_slot_cc("loot_take_slot", CC_Loot_TakeSlot, "Move loot slot from ragdoll to player", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

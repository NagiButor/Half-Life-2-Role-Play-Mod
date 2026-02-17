#include "cbase.h"
#include "loot_system.h"
#include "physics_prop_ragdoll.h"
#include "inventory_system.h"
#include "loot_container.h"
#include "recipientfilter.h"
#include "inventory_netmessages.h"
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

static CFuncLootContainer *AsLootContainer(CBaseEntity *pEnt)
{
	if (!pEnt)
		return NULL;
	if (FClassnameIs(pEnt, "func_loot_container"))
		return static_cast<CFuncLootContainer*>(pEnt);
	return NULL;
}

static const char *LootTarget_GetLootItems(CBaseEntity *pEnt)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
		return pRag->GetLootItems();
	if (CFuncLootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootItems();
	return NULL;
}

static void LootTarget_SetLootItems(CBaseEntity *pEnt, const char *items)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
	{
		pRag->SetLootItems(items);
		return;
	}
	if (CFuncLootContainer *pBox = AsLootContainer(pEnt))
	{
		pBox->SetLootItems(items);
		return;
	}
}

static const char *LootTarget_GetDisplayName(CBaseEntity *pEnt)
{
	if (CRagdollProp *pRag = AsLootRagdoll(pEnt))
		return pRag->GetLootDisplayName();
	if (CFuncLootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootDisplayName();
	return NULL;
}

static const char *LootTarget_GetSoundOpen(CBaseEntity *pEnt)
{
	if (CFuncLootContainer *pBox = AsLootContainer(pEnt))
		return pBox->GetLootSoundOpen();
	return NULL;
}

static const char *LootTarget_GetSoundClose(CBaseEntity *pEnt)
{
	if (CFuncLootContainer *pBox = AsLootContainer(pEnt))
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

static void NormalizeLootItems(CUtlVector<CUtlString> &items);

static void AddLootItem(CUtlVector<CUtlString> &items, const char *classname, int bullets, bool isAmmo)
{
	if (!classname || !classname[0])
		return;

	char ncls[256];
	NormalizeClassname(classname, ncls, sizeof(ncls));
	if (!ncls[0])
		return;

	if (isAmmo)
	{
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
				existingBullets = hasB ? b : InventorySystem::GetBulletsPerAmmoItem(ncls);
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

		if (InventorySystem::IsAmmoItem(cls))
		{
			int b = bullets;
			if (!hasBullets || b <= 0)
				b = InventorySystem::GetBulletsPerAmmoItem(cls);

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
				if (!InventorySystem::IsAmmoItem(ecls))
					continue;
				if (Q_stricmp(ecls, cls))
					continue;
				match = j;
				int existing = (ehasBullets && ebullets > 0) ? ebullets : InventorySystem::GetBulletsPerAmmoItem(cls);
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

	bool isAmmo = InventorySystem::IsAmmoItem(cls);
	if (isAmmo)
	{
		if (!hasBullets || bullets <= 0)
			bullets = InventorySystem::GetBulletsPerAmmoItem(cls);
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
			int total = (ehasBullets ? ebullets : InventorySystem::GetBulletsPerAmmoItem(cls)) + bullets;
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
	if (InventorySystem::IsAmmoItem(cls))
	{
		int b = inv.bullets;
		if (b <= 0)
			b = InventorySystem::GetBulletsPerAmmoItem(cls);
		InventorySystem::AddItemToPlayerWithBullets(pPlayer, cls, b);
		return;
	}
	InventorySystem::AddItemToPlayer(pPlayer, cls);
}

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
	if (InventorySystem::IsAmmoItem(cls) && bullets <= 0)
		bullets = InventorySystem::GetBulletsPerAmmoItem(cls);

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

	if (InventorySystem::IsAmmoItem(cls))
	{
		int b = bullets;
		if (!hasBullets || b <= 0)
			b = InventorySystem::GetBulletsPerAmmoItem(cls);
		InventorySystem::AddItemToPlayerWithBullets(pPlayer, cls, b);
	}
	else if (isWeapon)
	{
		InventorySystem::AddWeaponToPlayer(pPlayer, cls, clip1, clip2, reserveIdx, reserveCount);
	}
	else
	{
		InventorySystem::AddItemToPlayer(pPlayer, cls);
	}

	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}

void OpenLootForPlayer(CBasePlayer *pPlayer, CBaseEntity *pLootEnt)
{
    if (!pPlayer || !pLootEnt)
        return;

	LootDevMsg("[loot] open player=%d ent=%d loot='%s'\n", pPlayer->entindex(), pLootEnt->entindex(), LootTarget_GetLootItems(pLootEnt) ? LootTarget_GetLootItems(pLootEnt) : "(null)");

	int idx = pPlayer->entindex();
	if (idx > 0 && idx <= MAX_PLAYERS)
	{
		s_LootTargets[idx] = pLootEnt;
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

	if (InventorySystem::IsAmmoItem(classname))
	{
		if (!hasBullets || bullets <= 0)
			bullets = InventorySystem::GetBulletsPerAmmoItem(classname);
		InventorySystem::AddItemToPlayerWithBullets(pPlayer, classname, bullets);
	}
	else if (isWeapon)
	{
		InventorySystem::AddWeaponToPlayer(pPlayer, classname, clip1, clip2, reserveIdx, reserveCount);
	}
	else
	{
		InventorySystem::AddItemToPlayer(pPlayer, classname);
	}
	CUtlString newLoot;
	BuildLootItems(items, newLoot);
	LootTarget_SetLootItems(pLootEnt, newLoot.Get());
	OpenLootForPlayer(pPlayer, pLootEnt);
}


static void CC_Loot_PutAll(const CCommand &args)
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
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

		if (InventorySystem::IsAmmoItem(cls))
		{
			int b = bullets;
			if (!hasBullets || b <= 0)
				b = InventorySystem::GetBulletsPerAmmoItem(cls);
			InventorySystem::AddItemToPlayerWithBullets(pPlayer, cls, b);
		}
		else if (isWeapon)
		{
			InventorySystem::AddWeaponToPlayer(pPlayer, cls, clip1, clip2, reserveIdx, reserveCount);
		}
		else
		{
			InventorySystem::AddItemToPlayer(pPlayer, cls);
		}
	}

	LootTarget_SetLootItems(pLootEnt, "");
	OpenLootForPlayer(pPlayer, pLootEnt);
}

static ConCommand loot_put_cc("loot_put", CC_Loot_Put, "Move item from player inventory to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_cc("loot_take", CC_Loot_Take, "Move item from ragdoll to player inventory", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_put_all_cc("loot_put_all", CC_Loot_PutAll, "Move all items from player inventory to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_all_cc("loot_take_all", CC_Loot_TakeAll, "Move all items from ragdoll to player inventory", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_put_slot_cc("loot_put_slot", CC_Loot_PutSlot, "Move inventory slot from player to ragdoll", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand loot_take_slot_cc("loot_take_slot", CC_Loot_TakeSlot, "Move loot slot from ragdoll to player", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

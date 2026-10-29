//========= HL2RPM ============//
//
// RPG-система (сервер): предметы среза (item_rpg и его классы из items.txt), выдача/изъятие,
// использование из инвентаря (еда, лекарство, записка, документ, костюм), костюмы-броня.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character.h"
#include "items.h"
#include "player.h"
#include "hl2_player.h"
#include "inventory_system.h"
#include "recipientfilter.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar rpg_debug;

#define RPG_ITEM_FALLBACK_MODEL "models/props_junk/cardboard_box004a.mdl"

// ---------------------------------------------------------------------------
// Инвентарь
// ---------------------------------------------------------------------------
int RPG_CountItem( CBasePlayer *pPlayer, const char *pszClassname )
{
	if ( !pPlayer || !pszClassname || !pszClassname[0] )
		return 0;
	CUtlVector<CUtlString> items;
	InventorySystem::GetInventoryItemStringsForPlayer( pPlayer, items );
	int n = 0;
	const int len = Q_strlen( pszClassname );
	for ( int i = 0; i < items.Count(); ++i )
	{
		const char *psz = items[i].Get();
		// "classname:bullets[:...]"
		if ( !Q_strnicmp( psz, pszClassname, len ) && ( psz[len] == ':' || psz[len] == '\0' ) )
			++n;
	}
	// надетый костюм и оружие в руках - тоже «есть»
	if ( pPlayer->IsSuitEquipped() )
	{
		const char *pszSuit = InventorySystem::GetEquippedSuitClass( pPlayer );
		if ( !pszSuit || !pszSuit[0] )
			pszSuit = "item_suit";
		if ( !Q_stricmp( pszSuit, pszClassname ) )
			++n;
	}
	if ( !Q_strnicmp( pszClassname, "weapon_", 7 ) && pPlayer->Weapon_OwnsThisType( pszClassname ) )
		++n;
	return n;
}

void RPG_GiveItem( CBasePlayer *pPlayer, const char *pszClassname, int count, bool bNotify )
{
	if ( !pPlayer || !pszClassname || !pszClassname[0] || count <= 0 )
		return;
	if ( InventorySystem::IsAmmoItem( pszClassname ) )
	{
		InventorySystem::AddItemToPlayerWithBullets( pPlayer, pszClassname, InventorySystem::GetBulletsPerAmmoItem( pszClassname ) * count );
	}
	else
	{
		for ( int i = 0; i < count; ++i )
			InventorySystem::AddItemToPlayer( pPlayer, pszClassname );
	}
	InventorySystem::SendInventoryToPlayer( pPlayer );
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	if ( bNotify )
		pChar->Notify( RPG_NOTIFY_ITEM, pszClassname, count );
	RPG_FireItemsChanged( pPlayer, pszClassname, count );
}

int RPG_TakeItem( CBasePlayer *pPlayer, const char *pszClassname, int count, bool bNotify )
{
	if ( !pPlayer || !pszClassname || !pszClassname[0] || count <= 0 )
		return 0;
	int taken = 0;
	while ( taken < count && InventorySystem::RemoveItemFromPlayer( pPlayer, pszClassname ) != -1 )
		++taken;
	// оружие в руках
	if ( taken < count && !Q_strnicmp( pszClassname, "weapon_", 7 ) )
	{
		CBaseCombatWeapon *pWeapon = pPlayer->Weapon_OwnsThisType( pszClassname );
		if ( pWeapon )
		{
			pPlayer->Weapon_Detach( pWeapon );
			UTIL_Remove( pWeapon );
			++taken;
		}
	}
	if ( taken > 0 )
	{
		InventorySystem::SendInventoryToPlayer( pPlayer );
		if ( bNotify )
			RPG_GetCharacter( pPlayer )->Notify( RPG_NOTIFY_ITEM, pszClassname, -taken );
		RPG_FireItemsChanged( pPlayer, pszClassname, -taken );
	}
	return taken;
}

// ---------------------------------------------------------------------------
// Костюмы-броня (RPG_SPEC §8): роба / рабочий / ГО / Надзор / повстанец / HEV
// ---------------------------------------------------------------------------
static void SendCombineSuitStyle( CBasePlayer *pPlayer, int iSuitType )
{
	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	UserMessageBegin( filter, "CombineSuit" );
	WRITE_BYTE( ( iSuitType == RPG_SUIT_CP || iSuitType == RPG_SUIT_OVERWATCH ) ? 1 : 0 );
	MessageEnd();
}

// надеть костюм classname (предмет уже не в мире; из инвентаря его забирает вызывающий)
static bool RPG_EquipSuit( CBasePlayer *pPlayer, const char *pszClassname )
{
	if ( !pPlayer )
		return false;
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClassname );
	const int iType = pDef ? RPG_SuitTypeFromKey( pDef->armor.Get() ) : -1;
	if ( iType < 0 )
		return false;

	// старый костюм - обратно в инвентарь (раньше item_combine_soldier_suit затирал HEV)
	if ( pPlayer->IsSuitEquipped() )
	{
		const char *pszOld = InventorySystem::GetEquippedSuitClass( pPlayer );
		CUtlString old = ( pszOld && pszOld[0] ) ? pszOld : "item_suit";
		CHL2_Player *pHL2Player = dynamic_cast<CHL2_Player *>( pPlayer );
		if ( pHL2Player )
		{
			pHL2Player->FlashlightTurnOff();
			pHL2Player->StopZooming();
			pHL2Player->StopSprinting();
		}
		pPlayer->RemoveSuit();
		InventorySystem::ClearEquippedSuitClass( pPlayer );
		InventorySystem::AddItemToPlayer( pPlayer, old.Get() );
	}

	if ( iType == RPG_SUIT_CITIZEN )
	{
		// роба - это «без костюма»
		pPlayer->m_Local.m_iSuitType = RPG_SUIT_CITIZEN;
	}
	else
	{
		pPlayer->m_Local.m_bWearingSuit = true;
		pPlayer->m_Local.m_iSuitType = iType;
		InventorySystem::SetEquippedSuitClass( pPlayer, pszClassname );
	}
	SendCombineSuitStyle( pPlayer, iType );
	pPlayer->EmitSound( "npc/combine_soldier/zipline_clothing1.wav" );
	RPG_UpdatePlayerSuitType( pPlayer );
	RPG_GetCharacter( pPlayer )->Notify( RPG_NOTIFY_GENERIC, "#HL2RPM_UI_SuitEquipped", iType, pszClassname );
	if ( rpg_debug.GetBool() )
		Msg( "[rpg] suit %s equipped (type %s)\n", pszClassname, g_pszRPGSuitTypeKeys[iType] );
	return true;
}

// ---------------------------------------------------------------------------
// inventory_use: предметы RPG
// ---------------------------------------------------------------------------
bool RPG_HandleInventoryUse( CBasePlayer *pPlayer, const char *pszClassname )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClassname );
	if ( !pPlayer || !pDef )
		return false;
	CRPGCharacter *pChar = RPG_GetCharacter( pPlayer );
	const CRPGCharacterData &d = pChar->Data();

	// костюм
	if ( pDef->armor.Length() && Q_stricmp( pszClassname, "item_suit" ) && Q_stricmp( pszClassname, "item_combine_soldier_suit" ) )
	{
		if ( InventorySystem::RemoveItemFromPlayer( pPlayer, pszClassname ) == -1 )
			return true;
		if ( !RPG_EquipSuit( pPlayer, pszClassname ) )
			InventorySystem::AddItemToPlayer( pPlayer, pszClassname );
		return true;
	}

	if ( pDef->use.IsEmpty() )
	{
		// у предметов мода без действия (документы, ключи) - отказ; ванильные - как раньше
		if ( pDef->model.IsEmpty() )
			return false;
		pChar->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_CannotUse", 0, pszClassname );
		return true;
	}

	char szVerb[32] = { 0 };
	char szArg[96] = { 0 };
	sscanf( pDef->use.Get(), "%31s %95s", szVerb, szArg );

	if ( !Q_stricmp( szVerb, "heal" ) || !Q_stricmp( szVerb, "food" ) )
	{
		const float flBase = (float)atof( szArg );
		float flAmount;
		if ( !Q_stricmp( szVerb, "heal" ) )
		{
			// лечение: 60% + 0.6×Медицина (Part I/12)
			flAmount = flBase * ( 0.6f + 0.006f * (float)d.GetSkill( "medicine" ) ) * d.GetModMult( "medkit_heal" );
			if ( pPlayer->GetHealth() >= pPlayer->GetMaxHealth() )
			{
				pChar->Notify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_FullHealth", 0, pszClassname );
				return true;
			}
		}
		else
		{
			// еда: ×(1 + Выживание/100); паёк Альянса - ration_heal (TR-05), остальное - food_heal
			const bool bRation = !Q_stricmp( pDef->category.Get(), "food" ) && Q_stristr( pszClassname, "ration" ) != NULL;
			flAmount = flBase * ( 1.0f + (float)d.GetSkill( "survival" ) / 100.0f ) * d.GetModMult( bRation ? "ration_heal" : "food_heal" );
		}
		if ( InventorySystem::RemoveItemFromPlayer( pPlayer, pszClassname ) == -1 )
			return true;
		pPlayer->TakeHealth( Max( 1.0f, floorf( flAmount + 0.5f ) ), DMG_GENERIC );
		pPlayer->EmitSound( !Q_stricmp( szVerb, "heal" ) ? "HealthKit.Touch" : "HealthVial.Touch" );
		return true;
	}
	if ( !Q_stricmp( szVerb, "note" ) )
	{
		const char *pszNote = szArg[0] ? szArg : pszClassname;
		pChar->AddNote( pszNote );
		RPG_OpenUI( pPlayer, "note", pszNote );
		return true;
	}
	if ( !Q_stricmp( szVerb, "readable" ) )
	{
		RPG_OpenUI( pPlayer, "read", pszClassname );
		return true;
	}
	Warning( "[rpg] items.txt: '%s' has unknown use '%s'\n", pszClassname, pDef->use.Get() );
	return false;
}

// ---------------------------------------------------------------------------
// item_rpg: предмет среза; модель, вес, цена - из items.txt по classname
// ---------------------------------------------------------------------------
class CItemRPG : public CItem
{
public:
	DECLARE_CLASS( CItemRPG, CItem );
	DECLARE_DATADESC();

	virtual bool KeyValue( const char *szKeyName, const char *szValue )
	{
		// item_rpg с ключом item=<classname> - стать этим предметом
		if ( FStrEq( szKeyName, "item" ) )
		{
			if ( szValue && szValue[0] && RPGData().FindItem( szValue ) && FClassnameIs( this, "item_rpg" ) )
				SetClassname( szValue );
			return true;
		}
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	const char *GetItemModel()
	{
		RPGData().EnsureLoaded();
		const RPGItemDef_t *pDef = RPGData().FindItem( GetClassname() );
		if ( pDef && pDef->model.Length() )
			return pDef->model.Get();
		return RPG_ITEM_FALLBACK_MODEL;
	}

	virtual void Precache()
	{
		PrecacheModel( GetItemModel() );
		BaseClass::Precache();
	}

	virtual void Spawn()
	{
		Precache();
		SetModel( GetItemModel() );
		const RPGItemDef_t *pDef = RPGData().FindItem( GetClassname() );
		if ( pDef && pDef->skin )
			m_nSkin = pDef->skin;
		BaseClass::Spawn();
	}

	// касание (инвентарь выключен или «использование» через мир): в инвентарь / надеть
	virtual bool MyTouch( CBasePlayer *pPlayer )
	{
		if ( !pPlayer )
			return false;
		const RPGItemDef_t *pDef = RPGData().FindItem( GetClassname() );
		if ( pDef && pDef->armor.Length() )
			return RPG_EquipSuit( pPlayer, GetClassname() );
		InventorySystem::AddItemToPlayer( pPlayer, GetClassname() );
		InventorySystem::NotifyPlayerPickup( pPlayer, GetClassname() );
		return true;
	}
};

BEGIN_DATADESC( CItemRPG )
END_DATADESC()

LINK_ENTITY_TO_CLASS( item_rpg, CItemRPG );
// предметы вертикального среза (RPG_SPEC §11) - имена для Hammer и сохранений
LINK_ENTITY_TO_CLASS( item_temp_pass, CItemRPG );
LINK_ENTITY_TO_CLASS( item_account_card, CItemRPG );
LINK_ENTITY_TO_CLASS( item_forged_id_card, CItemRPG );
LINK_ENTITY_TO_CLASS( item_lambda_card, CItemRPG );
LINK_ENTITY_TO_CLASS( item_ration, CItemRPG );
LINK_ENTITY_TO_CLASS( item_water_can, CItemRPG );
LINK_ENTITY_TO_CLASS( item_ration_coupon, CItemRPG );
LINK_ENTITY_TO_CLASS( item_pocket_note, CItemRPG );
LINK_ENTITY_TO_CLASS( item_citizen_memo, CItemRPG );
LINK_ENTITY_TO_CLASS( item_samizdat_package, CItemRPG );
LINK_ENTITY_TO_CLASS( item_kadar_package, CItemRPG );
LINK_ENTITY_TO_CLASS( item_chocolate, CItemRPG );
LINK_ENTITY_TO_CLASS( item_zenit_camera, CItemRPG );
LINK_ENTITY_TO_CLASS( item_letter_113, CItemRPG );
LINK_ENTITY_TO_CLASS( item_work_order, CItemRPG );
LINK_ENTITY_TO_CLASS( item_lockpick, CItemRPG );
LINK_ENTITY_TO_CLASS( item_suit_cp, CItemRPG );
LINK_ENTITY_TO_CLASS( item_suit_worker, CItemRPG );
LINK_ENTITY_TO_CLASS( item_suit_rebel, CItemRPG );

// Новые item_* из items.txt без своего класса (с ключом model) тоже создаются как item_rpg:
// фабрики ставятся при загрузке данных (до разбора энтити карты)
void RPG_InstallItemFactories()
{
	const CRPGData &data = RPGData();
	for ( int i = 0; i < data.GetItemCount(); ++i )
	{
		const RPGItemDef_t &d = data.GetItem( i );
		if ( d.model.IsEmpty() || Q_strnicmp( d.classname.Get(), "item_", 5 ) )
			continue;
		if ( EntityFactoryDictionary()->FindFactory( d.classname.Get() ) )
			continue;
		// имя живёт столько же, сколько фабрика (до выгрузки server.dll)
		char *pszName = new char[d.classname.Length() + 1];
		Q_strncpy( pszName, d.classname.Get(), d.classname.Length() + 1 );
		new CEntityFactory<CItemRPG>( pszName );
		DevMsg( "[rpg] item class %s registered from items.txt\n", pszName );
	}
}

void RPG_PrecacheItems()
{
	CBaseEntity::PrecacheSound( "npc/combine_soldier/zipline_clothing1.wav" );
	CBaseEntity::PrecacheScriptSound( "HealthKit.Touch" );
	CBaseEntity::PrecacheScriptSound( "HealthVial.Touch" );
	const CRPGData &data = RPGData();
	for ( int i = 0; i < data.GetItemCount(); ++i )
	{
		const RPGItemDef_t &d = data.GetItem( i );
		if ( d.model.Length() )
			CBaseEntity::PrecacheModel( d.model.Get() );
	}
}

//========= HL2RPM ============================================================//
//
// item_rpm: an item of the RPG database (scripts/rpg/items.txt) that has no entity
// class of its own - notes, keys, money, food, quest items. Lies in the world with
// the item's model; +use puts it into the inventory as its id.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_items.h"
#include "inventory_system.h"
#include "player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define RPM_ITEM_DEFAULT_MODEL "models/props_lab/clipboard.mdl"

class CRPMItem : public CBaseAnimating
{
public:
	DECLARE_CLASS( CRPMItem, CBaseAnimating );
	DECLARE_DATADESC();

	CRPMItem() : m_iCount( 1 ) {}

	const char *ItemModel()
	{
		const char *pszModel = RPMItems::Model( STRING( m_iszItem ) );		// (the card of the item shows the same)
		return pszModel[0] ? pszModel : RPM_ITEM_DEFAULT_MODEL;
	}

	void Precache()
	{
		PrecacheModel( ItemModel() );
		PrecacheScriptSound( "HealthVial.Touch" );
	}

	void Spawn()
	{
		Precache();
		SetModel( ItemModel() );
		BaseClass::Spawn();
		SetMoveType( MOVETYPE_VPHYSICS );
		SetSolid( SOLID_VPHYSICS );
		if ( !VPhysicsInitNormal( SOLID_VPHYSICS, 0, false ) )
		{
			SetMoveType( MOVETYPE_FLYGRAVITY );
			SetSolid( SOLID_BBOX );
			UTIL_SetSize( this, Vector( -6, -6, 0 ), Vector( 6, 6, 6 ) );
		}
		SetCollisionGroup( COLLISION_GROUP_WEAPON );
	}

	// USE_IN_RADIUS: a thin thing (a key, a note) lying on a crate - the trace under the crosshair hits the crate
	// on every other frame, and USE (and its prompt) flickered between the item and nothing
	int ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS; }

	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
	{
		CBasePlayer *pPlayer = ToBasePlayer( pActivator );
		if ( !pPlayer || !m_iszItem )
			return;
		const char *id = STRING( m_iszItem );
		if ( RPMItems::IsStackable( id ) )
			InventorySystem::AddItemToPlayerWithBullets( pPlayer, id, Max( 1, m_iCount ) );
		else
		{
			for ( int i = 0; i < Max( 1, m_iCount ); i++ )
				InventorySystem::AddItemToPlayer( pPlayer, id );
		}
		InventorySystem::NotifyPlayerPickup( pPlayer, id );
		m_OnPlayerPickup.FireOutput( pActivator, this );
		UTIL_Remove( this );
	}

	string_t m_iszItem;
	int m_iCount;
	COutputEvent m_OnPlayerPickup;
};

// HL2RPM (M6): the item of an item_rpm (the use prompt), NULL for other entities
const char *RPMItemEntity_GetItem( CBaseEntity *pEnt, int &count )
{
	CRPMItem *pItem = dynamic_cast<CRPMItem *>( pEnt );
	if ( !pItem || pItem->m_iszItem == NULL_STRING )
		return NULL;
	count = pItem->m_iCount;
	return STRING( pItem->m_iszItem );
}

LINK_ENTITY_TO_CLASS( item_rpm, CRPMItem );

BEGIN_DATADESC( CRPMItem )
	DEFINE_KEYFIELD( m_iszItem, FIELD_STRING, "item" ),
	DEFINE_KEYFIELD( m_iCount, FIELD_INTEGER, "count" ),
	DEFINE_OUTPUT( m_OnPlayerPickup, "OnPlayerPickup" ),
END_DATADESC()

// A generic item from the inventory back into the world
CBaseEntity *RPMItem_SpawnInWorld( const char *id, int count, const Vector &pos, const QAngle &ang )
{
	CBaseEntity *pEnt = CreateEntityByName( "item_rpm" );
	if ( !pEnt )
		return NULL;
	pEnt->KeyValue( "item", id );
	char sz[16];
	Q_snprintf( sz, sizeof( sz ), "%d", Max( 1, count ) );
	pEnt->KeyValue( "count", sz );
	pEnt->SetAbsOrigin( pos );
	pEnt->SetAbsAngles( ang );
	DispatchSpawn( pEnt );
	return pEnt;
}

// "Use" from the inventory for items of the database: notes open, food and medicine heal.
// Returns true if the item was handled here (the caller only refreshes the inventory).
bool RPMItem_Use( CBasePlayer *pPlayer, const char *id )
{
	const RPMItemDef_t *d = RPMItems::Find( id );
	if ( !d || !pPlayer )
		return false;
	if ( !Q_stricmp( d->base, "note" ) || !Q_stricmp( d->category, "note" ) )
	{
		engine->ClientCommand( pPlayer->edict(), "rpm_note_open \"%s\"\n", id );
		return true;
	}
	if ( d->heal > 0 && ( d->base[0] || !Q_stricmp( d->category, "food" ) ) )
	{
		if ( pPlayer->GetHealth() >= pPlayer->GetMaxHealth() )
		{
			engine->ClientCommand( pPlayer->edict(), "ui_toast \"inventory\" \"%s\" \"\"\n",
				!Q_stricmp( RPMItems::Language(), "russian" ) ? "Вы и так здоровы" : "You are already healthy" );
			return true;
		}
		if ( InventorySystem::RemoveItemFromPlayer( pPlayer, id ) == -1 )
			return true;
		pPlayer->TakeHealth( d->heal, DMG_GENERIC );
		pPlayer->EmitSound( "HealthVial.Touch" );
		return true;
	}
	if ( d->base[0] )
		return true;		// keys, money, quest items: nothing to use
	return false;
}

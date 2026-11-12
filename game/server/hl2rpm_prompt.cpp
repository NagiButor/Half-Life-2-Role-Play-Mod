//========= HL2RPM ============================================================//
//
// The use prompt (M6): what USE would do with the thing under the crosshair -
// "[E] Talk / Guard", "[E] Search / Crate", "[E] Take / Health kit (0.5 kg)",
// "Locked (Lockpick 50)". Ten times a second the server looks at the use target
// of the player (CBasePlayer::FindUseEntity, as USE itself) and sends a change to
// the client: user message "RPMPrompt" (hl2rpm_prompthud.cpp paints it).
//
//   byte verb (RPMPromptVerb_t), byte flags (1 locked, 2 empty, 4 jammed, 8 somebody's - steal), short lock level,
//   string name, string key (the item that opens the lock), float weight
//
//=============================================================================//
#include "cbase.h"
#include "player.h"
#include "ai_basenpc.h"
#include "BasePropDoor.h"
#include "loot_container.h"
#include "dialog_definitions.h"
#include "hl2rpm_world.h"
#include "hl2rpm_items.h"
#include "hl2rpm_rpg.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

enum RPMPromptVerb_t { PV_NONE, PV_TALK, PV_SEARCH, PV_OPEN, PV_OPEN_DOOR, PV_CLOSE_DOOR, PV_TAKE, PV_USE };

extern bool RPMLoot_DescribeTarget( CBaseEntity *pEnt, const char *&pszName, bool &bCorpse, bool &bEmpty );
extern const char *RPMLoot_GetOwner( CBaseEntity *pEnt );
extern const char *RPMItemEntity_GetItem( CBaseEntity *pEnt, int &count );

static ConVar rpm_prompt( "rpm_prompt", "1", FCVAR_GAMEDLL, "HL2RPM: send the use prompt (what USE does with the thing under the crosshair) to the client" );
static ConVar rpm_prompt_debug( "rpm_prompt_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print every prompt sent to the client (entity, verb, name)" );

struct RPMPrompt_t
{
	int verb, flags, level;
	CUtlString name, key;
	float weight;
	bool operator==( const RPMPrompt_t &o ) const
	{
		return verb == o.verb && flags == o.flags && level == o.level && weight == o.weight && !Q_strcmp( name, o.name ) && !Q_strcmp( key, o.key );
	}
};

static void Describe( CBasePlayer *pPlayer, CBaseEntity *pEnt, RPMPrompt_t &p )
{
	p.verb = PV_NONE;
	p.flags = p.level = 0;
	p.weight = 0.0f;
	p.name.Clear();
	p.key.Clear();
	if ( !pEnt )
		return;

	// a living NPC with a dialog that is not hostile: talk
	if ( pEnt->IsNPC() )
	{
		CAI_BaseNPC *pNPC = pEnt->MyNPCPointer();
		const char *tn = STRING( pEnt->GetEntityName() );
		if ( !pNPC || !pNPC->IsAlive() || !tn[0] || !DialogDefinitions::FindDialogForEntity( tn ) || DialogDefinitions::IsEntitySpoiled( tn )
			|| pNPC->IRelationType( pPlayer ) == D_HT )
			return;
		p.verb = PV_TALK;
		const char *n = DialogDefinitions::GetDisplayNameForEntity( tn );
		if ( !n || !n[0] )
			n = RPMWorld::GetNPCDisplayNameFor( pEnt );
		p.name = n ? n : "";
		return;
	}

	// corpses and containers
	const char *pszName = "";
	bool bCorpse = false, bEmpty = false;
	if ( RPMLoot_DescribeTarget( pEnt, pszName, bCorpse, bEmpty ) )
	{
		p.verb = bCorpse ? PV_SEARCH : PV_OPEN;
		p.name = pszName;
		if ( bEmpty )
			p.flags |= 2;
		CPropLootContainer *pBox = dynamic_cast<CPropLootContainer *>( pEnt );
		if ( pBox && pBox->IsLootLocked() )
		{
			p.flags |= 1;
			p.level = pBox->GetLockLevel();
			p.key = pBox->GetLockKey()[0] ? RPMItems::Name( pBox->GetLockKey() ) : "";
			if ( RPMLock::IsJammed( pEnt ) )
				p.flags |= 4;
		}
		// somebody's (not locked): "Steal", the owner in the key field
		const char *pszOwner = RPMLoot_GetOwner( pEnt );
		if ( pszOwner[0] && !( p.flags & 1 ) )
		{
			p.flags |= 8;
			p.key = RPMTheft::OwnerName( pszOwner );
		}
		return;
	}

	// doors
	CBasePropDoor *pDoor = dynamic_cast<CBasePropDoor *>( pEnt );
	if ( pDoor )
	{
		p.verb = pDoor->IsDoorOpen() || pDoor->IsDoorOpening() ? PV_CLOSE_DOOR : PV_OPEN_DOOR;
		if ( pDoor->IsDoorLocked() )
		{
			p.flags |= 1;
			p.level = pDoor->GetRPMLockLevel();
			p.key = pDoor->GetRPMKey()[0] ? RPMItems::Name( pDoor->GetRPMKey() ) : "";
			if ( RPMLock::IsJammed( pEnt ) )
				p.flags |= 4;
		}
		return;
	}

	// things to take: items of the database, weapons and items of HL2
	int count = 1;
	const char *pszItem = RPMItemEntity_GetItem( pEnt, count );
	if ( !pszItem && ( !Q_strnicmp( pEnt->GetClassname(), "weapon_", 7 ) || !Q_strnicmp( pEnt->GetClassname(), "item_", 5 ) ) && !pEnt->GetOwnerEntity() )
		pszItem = pEnt->GetClassname();
	if ( pszItem )
	{
		p.verb = PV_TAKE;
		p.name = RPMItems::Name( pszItem );
		const RPMItemDef_t *d = RPMItems::Find( pszItem );
		p.weight = d && d->weight > 0.0f ? d->weight * Max( 1, count ) : 0.0f;
		if ( count > 1 )
			p.name.Append( CFmtStr( " (%d)", count ) );
		return;
	}

	// terminals: "Use / <title>"
	if ( FClassnameIs( pEnt, "rpm_terminal" ) )
	{
		extern const char *RPMTerminal_GetTitle( CBaseEntity *pEnt );
		p.verb = PV_USE;
		p.name = RPMTerminal_GetTitle( pEnt );
		return;
	}

	// anything else that reacts to USE (buttons, levers)
	if ( pEnt->ObjectCaps() & ( FCAP_IMPULSE_USE | FCAP_CONTINUOUS_USE | FCAP_ONOFF_USE | FCAP_DIRECTIONAL_USE ) )
	{
		if ( FClassnameIs( pEnt, "func_button" ) || FClassnameIs( pEnt, "func_rot_button" ) || FClassnameIs( pEnt, "momentary_rot_button" )
			|| FClassnameIs( pEnt, "func_door" ) || FClassnameIs( pEnt, "func_door_rotating" ) )
			p.verb = PV_USE;
	}
}

class CRPMPromptSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMPromptSystem() : CAutoGameSystemPerFrame( "CRPMPromptSystem" ) { m_flNext = 0.0f; Reset(); }

	void Reset()
	{
		m_Last.verb = -1;
		m_Last.flags = m_Last.level = 0;
		m_Last.weight = 0.0f;
	}

	virtual void LevelInitPostEntity() { Reset(); m_flNext = 0.0f; }

	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.1f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer )
			return;
		RPMPrompt_t p;
		CBaseEntity *pUse = NULL;
		// seated: E stands up first (hl2_player.cpp) - so no "take" of a note lying next to the body
		extern bool RPMSeat_IsSeated( CBasePlayer *pPlayer );
		if ( rpm_prompt.GetBool() && pPlayer->IsAlive() && !pPlayer->IsInAVehicle() && !pPlayer->GetUseEntity() && !RPMSeat_IsSeated( pPlayer ) )
			pUse = pPlayer->FindUseEntity();
		Describe( pPlayer, pUse, p );
		// furniture markers: sit / lie / lean, or stand up while seated (hl2rpm_seat.cpp)
		extern bool RPMSeat_Describe( CBasePlayer *pPlayer, int &verb, CUtlString &name );
		int seatVerb = 0;
		CUtlString seatName;
		if ( rpm_prompt.GetBool() && ( p.verb == PV_NONE || p.verb == PV_USE ) && RPMSeat_Describe( pPlayer, seatVerb, seatName ) )
		{
			Describe( pPlayer, NULL, p );
			p.verb = seatVerb;
			p.name = seatName;
		}
		if ( p == m_Last )
			return;
		m_Last = p;
		if ( rpm_prompt_debug.GetBool() )
			Msg( "[prompt] %s -> verb %d flags %d level %d '%s' key '%s' %.2f kg\n", pUse ? pUse->GetDebugName() : "(none)",
				p.verb, p.flags, p.level, p.name.Get(), p.key.Get(), p.weight );
		CSingleUserRecipientFilter filter( pPlayer );
		filter.MakeReliable();
		UserMessageBegin( filter, "RPMPrompt" );
		WRITE_BYTE( p.verb );
		WRITE_BYTE( p.flags );
		WRITE_SHORT( p.level );
		WRITE_STRING( p.name.Get() );
		WRITE_STRING( p.key.Get() );
		WRITE_FLOAT( p.weight );
		MessageEnd();
	}

	float m_flNext;
	RPMPrompt_t m_Last;
};

static CRPMPromptSystem g_RPMPromptSystem;

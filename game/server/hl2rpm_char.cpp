//========= HL2RPM ============================================================//
//
// The player character of the RPG (server), see hl2rpm_char.h.
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_char.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "hl2rpm_rpg.h"
#include "hl2rpm_logic.h"
#include "hl2rpm_audio.h"
#include "hl2rpm_world.h"
#include "inventory_system.h"
#include "player.h"
#include "hl2_player.h"
#include "ai_basenpc.h"
#include "takedamageinfo.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"
#include "utlbuffer.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar rpm_char_debug( "rpm_char_debug", "0", FCVAR_GAMEDLL, "HL2RPM: print experience, level ups, skill damage factors" );
static ConVar rpm_suit_body( "rpm_suit_body", "1", FCVAR_GAMEDLL, "HL2RPM: the player's body is the one of the suit he wears (Civil Protection, soldiers)" );

static bool IsRussian() { return RPMLogic::IsRussian(); }

static KeyValues *Player() { return RPMState::Section( "player" ); }

static bool ListHas( const char *pszList, const char *pszItem )
{
	if ( !pszList || !pszItem || !pszItem[0] )
		return false;
	CUtlStringList parts;
	V_SplitString( pszList, ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
		if ( !Q_stricmp( parts[i], pszItem ) )
			return true;
	return false;
}

static int ListCount( const char *pszList )
{
	CUtlStringList parts;
	V_SplitString( pszList ? pszList : "", ";", parts );
	int n = 0;
	for ( int i = 0; i < parts.Count(); i++ )
		if ( parts[i][0] )
			n++;
	return n;
}

static void Dirty();

namespace RPMChar
{
	//-------------------------------------------------------------------------
	// effects of perks and traits
	//-------------------------------------------------------------------------
	static float SumEffects( KeyValues *pDef, int times, const char *type, const char *id )
	{
		float sum = 0.0f;
		KeyValues *e = pDef ? pDef->FindKey( "effects" ) : NULL;
		for ( KeyValues *x = e ? e->GetFirstTrueSubKey() : NULL; x; x = x->GetNextTrueSubKey() )
		{
			if ( Q_stricmp( x->GetString( "type" ), type ) )
				continue;
			const char *eid = x->GetString( "id" );
			const bool bExact = !Q_stricmp( type, "stat" ) || !Q_stricmp( type, "skill" );
			if ( bExact ? Q_stricmp( eid, id ) != 0 : ( eid[0] && Q_stricmp( eid, id ) != 0 ) )
				continue;
			sum += x->GetFloat( "value" ) * times;
		}
		return sum;
	}

	float Effect( const char *type, const char *id )
	{
		if ( !id )
			id = "";
		float sum = 0.0f;
		CUtlStringList traits;
		V_SplitString( Player()->GetString( "traits" ), ";", traits );
		for ( int i = 0; i < traits.Count(); i++ )
			sum += SumEffects( RPMRPG::Trait( traits[i] ), 1, type, id );
		KeyValues *perks = Player()->FindKey( "perks" );
		for ( KeyValues *p = perks ? perks->GetFirstValue() : NULL; p; p = p->GetNextValue() )
			sum += SumEffects( RPMRPG::Perk( p->GetName() ), Max( 1, p->GetInt() ), type, id );
		return sum;
	}

	bool HasPerk( const char *id )
	{
		KeyValues *perks = Player()->FindKey( "perks" );
		return perks && perks->GetInt( id, 0 ) > 0;
	}

	//-------------------------------------------------------------------------
	// attributes and skills
	//-------------------------------------------------------------------------
	int BaseStat( const char *id )
	{
		return Player()->FindKey( "stats", true )->GetInt( id, (int)RPMRPG::Rule( "stat_start" ) );
	}

	int Stat( const char *id )
	{
		return clamp( BaseStat( id ) + (int)Effect( "stat", id ), 1, 10 );
	}

	int SkillBase( const char *id )
	{
		if ( RPMRPG::SkillIndex( id ) < 0 )
			return 0;
		const char *stat = RPMRPG::SkillStat( id );
		float div = Max( 1.0f, RPMRPG::Rule( "skill_luck_div" ) );
		int v = (int)RPMRPG::Rule( "skill_base" ) + (int)( RPMRPG::Rule( "skill_per_stat" ) * ( stat[0] ? Stat( stat ) : 5 ) )
			+ (int)ceilf( Stat( "lck" ) / div );
		if ( ListHas( Player()->GetString( "tags" ), id ) )
			v += (int)RPMRPG::Rule( "tag_bonus" );
		return v;
	}

	int Skill( const char *id )
	{
		if ( RPMRPG::SkillIndex( id ) < 0 )
			return Player()->FindKey( "skills", true )->GetInt( id, 0 );	// a skill of the project's own
		int v = SkillBase( id ) + Player()->FindKey( "skills", true )->GetInt( id, 0 ) + (int)Effect( "skill", id ) + (int)Effect( "skills" );
		return clamp( v, 0, (int)RPMRPG::Rule( "skill_max" ) );
	}

	void SetSkillTotal( const char *id, int total )
	{
		KeyValues *k = Player()->FindKey( "skills", true );
		if ( RPMRPG::SkillIndex( id ) < 0 )
		{
			k->SetInt( id, total );
			return;
		}
		int rest = Skill( id ) - k->GetInt( id, 0 );
		k->SetInt( id, total - rest );
		Dirty();
	}

	int Level() { return Max( 1, Player()->GetInt( "level", 1 ) ); }
	int XP() { return Player()->GetInt( "xp", 0 ); }
	bool IsCreated() { return Player()->GetInt( "created", 0 ) != 0; }

	int MaxHealth()
	{
		return Max( 1, (int)( RPMRPG::Rule( "hp_base" ) + RPMRPG::Rule( "hp_per_end" ) * Stat( "end" ) +
			RPMRPG::Rule( "hp_per_level" ) * ( Level() - 1 ) + Effect( "hp" ) ) );
	}

	float CarryWeight()
	{
		return Max( 1.0f, RPMRPG::Rule( "carry_base" ) + RPMRPG::Rule( "carry_per_str" ) * Stat( "str" ) + Effect( "carry" ) );
	}

	float HealScale()
	{
		return Max( 0.1f, 1.0f + Skill( "medicine" ) * RPMRPG::Rule( "heal_per_medicine" ) + Effect( "heal" ) / 100.0f );
	}

	float PriceScale( bool bPlayerBuys )
	{
		float f = Skill( "barter" ) * RPMRPG::Rule( "barter_per_point" ) + Effect( "price" ) / 100.0f;
		return bPlayerBuys ? Max( 0.3f, 1.0f - f ) : 1.0f + f;
	}

	//-------------------------------------------------------------------------
	// experience
	//-------------------------------------------------------------------------
	static int PointsPerLevel()
	{
		return Max( 0, (int)RPMRPG::Rule( "points_base" ) + (int)( Stat( "int" ) * RPMRPG::Rule( "points_per_int" ) ) + (int)Effect( "points" ) );
	}

	void AddXP( CBasePlayer *pPlayer, int xp )
	{
		if ( xp == 0 )
			return;
		if ( xp > 0 )
			xp = Max( 1, (int)( xp * ( 1.0f + Effect( "xp" ) / 100.0f ) + 0.5f ) );
		KeyValues *p = Player();
		p->SetInt( "xp", Max( 0, p->GetInt( "xp", 0 ) + xp ) );
		RPMLogic::Toast( pPlayer, "xp", CFmtStr( IsRussian() ? "%+d опыта" : "%+d XP", xp ), "" );
		int level = Level();
		const int maxLevel = Max( 1, (int)RPMRPG::Rule( "level_max" ) );
		const int perkEvery = Max( 1, (int)RPMRPG::Rule( "perk_every" ) );
		bool bUp = false;
		while ( level < maxLevel && p->GetInt( "xp" ) >= RPMRPG::XPForLevel( level + 1 ) )
		{
			level++;
			bUp = true;
			p->SetInt( "level", level );
			p->SetInt( "points", p->GetInt( "points", 0 ) + PointsPerLevel() );
			if ( level % perkEvery == 0 )
				p->SetInt( "perks_pending", p->GetInt( "perks_pending", 0 ) + 1 );
			if ( rpm_char_debug.GetBool() )
				Msg( "[char] level %d (xp %d), points %d, perks %d\n", level, p->GetInt( "xp" ), p->GetInt( "points" ), p->GetInt( "perks_pending" ) );
		}
		if ( bUp )
		{
			RPMLogic::Toast( pPlayer, "level", CFmtStr( IsRussian() ? "Новый уровень: %d" : "Level up: %d", level ), "" );
			RPMAudio::StingerEvent( "level_up" );
			if ( pPlayer && pPlayer->GetHealth() > 0 )
				pPlayer->SetHealth( Min( MaxHealth(), pPlayer->GetHealth() + (int)RPMRPG::Rule( "hp_per_level" ) ) );
		}
		Dirty();
	}

	//-------------------------------------------------------------------------
	// combat
	//-------------------------------------------------------------------------
	static const char *WeaponSkill( const CTakeDamageInfo &info, CBasePlayer *pPlayer, const RPMItemDef_t **ppDef )
	{
		*ppDef = NULL;
		if ( info.GetDamageType() & DMG_BLAST )
			return "explosives";
		CBaseEntity *pWeapon = info.GetWeapon();
		if ( !pWeapon && info.GetInflictor() == pPlayer )
			pWeapon = pPlayer->GetActiveWeapon();
		if ( pWeapon )
		{
			const RPMItemDef_t *d = RPMItems::Find( pWeapon->GetClassname() );
			*ppDef = d;
			if ( d && d->skill[0] )
				return d->skill;
		}
		if ( info.GetDamageType() & ( DMG_CLUB | DMG_SLASH ) )
			return "melee";
		if ( info.GetDamageType() & ( DMG_DISSOLVE | DMG_SHOCK | DMG_ENERGYBEAM ) )
			return "energy";
		if ( info.GetDamageType() & ( DMG_BULLET | DMG_BUCKSHOT ) )
			return "guns";
		return "";
	}

	float SpreadScale( CBasePlayer *pPlayer )
	{
		CBaseCombatWeapon *pWeapon = pPlayer ? pPlayer->GetActiveWeapon() : NULL;
		if ( !pWeapon )
			return 1.0f;
		const RPMItemDef_t *d = RPMItems::Find( pWeapon->GetClassname() );
		const char *skill = d && d->skill[0] ? d->skill : "guns";
		const int s = Skill( skill );
		float scale = RPMRPG::Rule( "spread_min" ) + ( RPMRPG::Rule( "spread_max" ) - RPMRPG::Rule( "spread_min" ) ) * s / 100.0f;
		if ( d && d->skillReq > s )
			scale *= 1.0f + RPMRPG::Rule( "skill_req_penalty" );
		if ( rpm_char_debug.GetBool() )
			Msg( "[char] spread of %s: %s %d, x%.2f\n", pWeapon->GetClassname(), skill, s, scale );
		return Max( 0.05f, scale );
	}

	void ModifyDamage( CTakeDamageInfo &info, CBaseEntity *pVictim )
	{
		CBaseEntity *pAttacker = info.GetAttacker();
		if ( !pVictim || pAttacker == pVictim )
			return;
		if ( pAttacker && pAttacker->IsPlayer() && pVictim->MyCombatCharacterPointer() )
		{
			CBasePlayer *pPlayer = ToBasePlayer( pAttacker );
			const RPMItemDef_t *pDef = NULL;
			const char *skill = WeaponSkill( info, pPlayer, &pDef );
			if ( !skill[0] )
				return;
			const int s = Skill( skill );
			float mult = RPMRPG::Rule( "dmg_min" ) + ( RPMRPG::Rule( "dmg_max" ) - RPMRPG::Rule( "dmg_min" ) ) * s / 100.0f;
			mult *= 1.0f + Effect( "damage", skill ) / 100.0f;
			if ( pDef && pDef->skillReq > s )
				mult *= 1.0f - RPMRPG::Rule( "skill_req_penalty" );
			bool bCrit = false;
			if ( info.GetDamageType() & ( DMG_BULLET | DMG_BUCKSHOT | DMG_CLUB | DMG_SLASH ) )
			{
				float chance = Stat( "lck" ) * RPMRPG::Rule( "crit_per_luck" ) + Effect( "crit" );
				if ( RandomFloat( 0, 100 ) < chance )
				{
					mult *= RPMRPG::Rule( "crit_mult" );
					bCrit = true;
				}
			}
			if ( rpm_char_debug.GetBool() )
				Msg( "[char] %s -> %s: %s %d, x%.2f%s\n", pAttacker->GetDebugName(), pVictim->GetDebugName(), skill, s, mult, bCrit ? " (critical)" : "" );
			info.ScaleDamage( mult );
		}
		else if ( pVictim->IsPlayer() )
		{
			float dr = Effect( "dr" );
			CBasePlayer *pWearer = ToBasePlayer( pVictim );
			if ( pWearer && pWearer->IsSuitEquipped() )
			{
				const char *pszSuit = InventorySystem::GetEquippedSuitClass( pWearer );
				dr += RPMItems::SuitDefense( pszSuit && pszSuit[0] ? pszSuit : "item_suit" );
			}
			dr = clamp( dr, 0.0f, 85.0f );
			if ( dr > 0.0f )
				info.ScaleDamage( 1.0f - dr / 100.0f );
		}
	}

	//-------------------------------------------------------------------------
	// reputation with factions
	//-------------------------------------------------------------------------
	int FactionRep( const char *faction )
	{
		return RPMState::Section( "rep" )->GetInt( faction, 0 );
	}

	static void FactionHatesPlayer( CBasePlayer *pPlayer, const char *faction )
	{
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
		{
			CAI_BaseNPC *p = ppAIs[i];
			if ( p && p->IsAlive() && !Q_stricmp( p->GetFactionTag(), faction ) )
				p->AcceptInput( "HatePlayer", pPlayer, pPlayer, variant_t(), 0 );
		}
	}

	void AddFactionRep( CBasePlayer *pPlayer, const char *faction, int delta )
	{
		if ( !faction || !faction[0] || !delta )
			return;
		KeyValues *r = RPMState::Section( "rep" );
		int before = r->GetInt( faction, 0 );
		int after = clamp( before + delta, -100, 100 );
		r->SetInt( faction, after );
		const char *name = RPMWorld::FactionName( faction );
		RPMLogic::Toast( pPlayer, "rep", CFmtStr( IsRussian() ? "Репутация: %s %+d" : "Reputation: %s %+d", name, after - before ), "" );
		const int hostile = (int)RPMRPG::Rule( "rep_hostile" );
		if ( before > hostile && after <= hostile && pPlayer )
		{
			RPMState::Section( "rep_hostile" )->SetInt( faction, 1 );
			FactionHatesPlayer( pPlayer, faction );
			RPMLogic::Toast( pPlayer, "rep", CFmtStr( IsRussian() ? "%s: вы враг" : "%s: you are an enemy", name ), "" );
		}
		Dirty();
	}

	void NoteKill( CBaseEntity *pVictim, CBaseEntity *pAttacker )
	{
		CBasePlayer *pPlayer = ToBasePlayer( pAttacker );
		CAI_BaseNPC *pNPC = pVictim ? pVictim->MyNPCPointer() : NULL;
		if ( !pPlayer || !pNPC )
			return;
		const bool bEnemy = pNPC->IRelationType( pPlayer ) == D_HT;
		const RPMNPCDef_t *pDef = RPMWorld::FindNPCDef( pVictim->GetEntityName().ToCStr() );
		// experience for an enemy without its own (the NPC of the Creation Kit gets its "xp" in RPMLogic)
		if ( bEnemy && !( pDef && pDef->xp > 0 ) )
			AddXP( pPlayer, (int)RPMRPG::Rule( "xp_kill" ) );
		// killing a member of a faction that wasn't fighting the player costs reputation with it
		const char *faction = pNPC->GetFactionTag();
		if ( faction && faction[0] && !bEnemy )
			AddFactionRep( pPlayer, faction, -(int)RPMRPG::Rule( "rep_kill" ) );
	}

	//-------------------------------------------------------------------------
	void OpenCreation( CBasePlayer *pPlayer )
	{
		if ( pPlayer )
			engine->ClientCommand( pPlayer->edict(), "rpm_char_open create\n" );
	}
}

using namespace RPMChar;

//=============================================================================
// the state for the client ("RPMChar" user message, in pieces: a message holds 255 bytes)
//=============================================================================
static bool s_bDirty = true;
static CUtlString s_LastSent;
static void Dirty() { s_bDirty = true; }

static KeyValues *BuildClientState( CBasePlayer *pPlayer )
{
	KeyValues *p = Player();
	KeyValues *k = new KeyValues( "Char" );
	k->SetString( "name", p->GetString( "name" ) );
	k->SetInt( "created", IsCreated() ? 1 : 0 );
	k->SetInt( "level", Level() );
	k->SetInt( "xp", XP() );
	k->SetInt( "xp_prev", RPMRPG::XPForLevel( Level() ) );
	k->SetInt( "xp_next", Level() < (int)RPMRPG::Rule( "level_max" ) ? RPMRPG::XPForLevel( Level() + 1 ) : 0 );
	k->SetInt( "points", p->GetInt( "points", 0 ) );
	k->SetInt( "perks_pending", p->GetInt( "perks_pending", 0 ) );
	k->SetInt( "hp", MaxHealth() );
	k->SetFloat( "carry", CarryWeight() );
	k->SetString( "tags", p->GetString( "tags" ) );
	k->SetString( "traits", p->GetString( "traits" ) );
	KeyValues *st = k->FindKey( "stats", true );
	KeyValues *sb = k->FindKey( "stats_base", true );
	for ( int i = 0; i < RPMRPG::StatCount(); i++ )
	{
		st->SetInt( RPMRPG::StatId( i ), Stat( RPMRPG::StatId( i ) ) );
		sb->SetInt( RPMRPG::StatId( i ), BaseStat( RPMRPG::StatId( i ) ) );
	}
	KeyValues *sk = k->FindKey( "skills", true );
	for ( int i = 0; i < RPMRPG::SkillCount(); i++ )
		sk->SetInt( RPMRPG::SkillId( i ), Skill( RPMRPG::SkillId( i ) ) );
	KeyValues *pk = k->FindKey( "perks", true );
	KeyValues *perks = p->FindKey( "perks" );
	for ( KeyValues *x = perks ? perks->GetFirstValue() : NULL; x; x = x->GetNextValue() )
		pk->SetInt( x->GetName(), x->GetInt() );
	KeyValues *rp = k->FindKey( "rep", true );
	for ( KeyValues *x = RPMState::Section( "rep" )->GetFirstValue(); x; x = x->GetNextValue() )
		rp->SetInt( x->GetName(), x->GetInt() );
	return k;
}

static void SendState( CBasePlayer *pPlayer, bool bForce )
{
	KeyValues *k = BuildClientState( pPlayer );
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	k->RecursiveSaveToFile( buf, 0 );
	k->deleteThis();
	CUtlString text;
	text.SetDirect( (const char *)buf.Base(), buf.TellPut() );
	if ( !bForce && text == s_LastSent )
		return;
	s_LastSent = text;
	CSingleUserRecipientFilter filter( pPlayer );
	filter.MakeReliable();
	const int CHUNK = 200;
	int len = text.Length();
	for ( int off = 0; off < len || off == 0; off += CHUNK )
	{
		char piece[CHUNK + 1];
		int n = Min( CHUNK, len - off );
		Q_memcpy( piece, text.Get() + off, Max( 0, n ) );
		piece[Max( 0, n )] = 0;
		int flags = ( off == 0 ? 1 : 0 ) | ( off + CHUNK >= len ? 2 : 0 );
		UserMessageBegin( filter, "RPMChar" );
		WRITE_BYTE( flags );
		WRITE_STRING( piece );
		MessageEnd();
		if ( len == 0 )
			break;
	}
}

//=============================================================================
// new game: scripts/rpg/newgame.txt
//   "NewGame" { "map" "" "spawn" "x y z" "yaw" "90" "create" "1" "name" "Гражданин" "actions" { .. } }
//=============================================================================
static bool s_bNewGamePending = false;
static float s_flNewGameAt = 0.0f;

static void ApplyNewGame( CBasePlayer *pPlayer )
{
	KeyValues *ng = new KeyValues( "NewGame" );
	if ( !ng->LoadFromFile( filesystem, "scripts/rpg/newgame.txt", "GAME" ) )
	{
		ng->deleteThis();
		return;
	}
	const char *map = ng->GetString( "map" );
	if ( map[0] && Q_stricmp( map, STRING( gpGlobals->mapname ) ) )
	{
		ng->deleteThis();
		return;
	}
	Player()->SetInt( "newgame_done", 1 );
	if ( ng->GetString( "name" )[0] && !Player()->GetString( "name" )[0] )
		Player()->SetString( "name", ng->GetString( "name" ) );
	const char *spawn = ng->GetString( "spawn" );
	if ( spawn[0] )
	{
		Vector v;
		if ( sscanf( spawn, "%f %f %f", &v.x, &v.y, &v.z ) == 3 )
		{
			QAngle ang( 0, ng->GetFloat( "yaw", pPlayer->EyeAngles().y ), 0 );
			pPlayer->Teleport( &v, &ang, &vec3_origin );
			pPlayer->SnapEyeAngles( ang );
		}
	}
	RPMLogic::Ctx_t ctx;
	ctx.pPlayer = pPlayer;
	RPMLogic::Run( ng->FindKey( "actions" ), ctx );
	pPlayer->SetHealth( MaxHealth() );
	if ( ng->GetInt( "create", 0 ) && !IsCreated() )
		OpenCreation( pPlayer );
	if ( rpm_char_debug.GetBool() )
		Msg( "[char] new game applied (map %s)\n", STRING( gpGlobals->mapname ) );
	ng->deleteThis();
	Dirty();
}

//=============================================================================
static CUtlString s_strWornTold;

class CRPMCharSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMCharSystem() : CAutoGameSystemPerFrame( "CRPMCharSystem" ) {}
	float m_flNext = 0.0f;

	virtual void LevelInitPostEntity()
	{
		s_strWornTold = "-";		// (a new client state: told again)
		RPMRPG::Load( true );
		s_LastSent.Clear();
		s_bDirty = true;
		s_bNewGamePending = gpGlobals->eLoadType == MapLoad_NewGame;
		s_flNewGameAt = gpGlobals->curtime + 0.5f;
		m_flNext = 0.0f;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( gpGlobals->curtime < m_flNext )
			return;
		m_flNext = gpGlobals->curtime + 0.25f;
		CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
		if ( !pPlayer )
			return;
		RPMRPG::Load();
		if ( s_bNewGamePending && gpGlobals->curtime >= s_flNewGameAt )
		{
			s_bNewGamePending = false;
			if ( !Player()->GetInt( "newgame_done", 0 ) )
				ApplyNewGame( pPlayer );
		}
		// the theme of the suit worn (colors of the HUD and the windows, the boot screen)
		extern int RPMChar_SuitTheme( CBasePlayer *pPlayer );
		int theme = RPMChar_SuitTheme( pPlayer );
		if ( pPlayer->m_Local.m_iSuitTheme != theme )
			pPlayer->m_Local.m_iSuitTheme = theme;
		// the body under the suit: only when the suit changes (a map may give the player a model of its own)
		extern char g_szDefaultPlayerModel[MAX_PATH];
		static CUtlString s_strBody;
		const char *pszWorn = pPlayer->IsSuitEquipped() ? InventorySystem::GetEquippedSuitClass( pPlayer ) : "";
		// (the H.E.V. given by the map or the console is worn without an inventory class: "item_suit")
		const char *pszSuit = pPlayer->IsSuitEquipped() ? ( pszWorn && pszWorn[0] ? pszWorn : "item_suit" ) : "";
		const char *pszBody = rpm_suit_body.GetBool() && pszSuit[0] ? RPMItems::SuitBody( pszSuit ) : "";
		const int bodySkin = pszBody[0] ? RPMItems::SuitBodySkin( pszSuit ) : 0;
		// (the H.E.V. and the P.C.V. differ by the skin only; no suit - "" as before: a map's own model stays)
		CFmtStr bodyKey( "%s#%d", pszBody, bodySkin );
		const char *pszKey = pszBody[0] ? bodyKey.Access() : "";
		if ( Q_stricmp( s_strBody.Get(), pszKey ) )
		{
			s_strBody = pszKey;
			pPlayer->SetModel( pszBody[0] ? pszBody : g_szDefaultPlayerModel );
			pPlayer->m_nSkin = bodySkin;
			if ( rpm_char_debug.GetBool() )
				Msg( "[char] body: suit '%s' -> '%s' skin %d (now %s)\n", pszSuit, pszBody, bodySkin, STRING( pPlayer->GetModelName() ) );
		}
		// first-person hands of the weapons that use them (Mapbase's hand_viewmodel, view model 1, bone-merged
		// onto the weapon's): those of the suit; with a protagonist of Mapbase its own hands stay
		if ( CBaseViewModel *pHandsVM = pPlayer->GetViewModel( 1 ) )
		{
			CHL2_Player *pHL2 = dynamic_cast<CHL2_Player *>( pPlayer );
			if ( !pHL2 || pHL2->GetProtagonistIndex() == -1 )
			{
				int handsSkin = 0;
				const char *pszHands = RPMItems::SuitHands( pszSuit, theme, handsSkin );
				// (SetWeaponModel compares the pointer of the name: a pooled string)
				const char *pszPooled = STRING( AllocPooledString( pszHands ) );
				if ( Q_stricmp( STRING( pHandsVM->GetModelName() ), pszPooled ) )
					pHandsVM->SetWeaponModel( pszPooled, NULL );
				if ( pHandsVM->m_nSkin != handsSkin )
					pHandsVM->m_nSkin = handsSkin;
			}
		}
		// the client knows what is worn (the menu shows it)
		const char *pszTell = pszSuit;
		if ( Q_stricmp( s_strWornTold.Get(), pszTell ) )
		{
			s_strWornTold = pszTell;
			engine->ClientCommand( pPlayer->edict(), "rpm_suit_worn \"%s\"\n", pszTell );
		}
		// the health of the character
		int hp = MaxHealth();
		if ( pPlayer->GetMaxHealth() != hp )
		{
			pPlayer->SetMaxHealth( hp );
			if ( pPlayer->GetHealth() > hp )
				pPlayer->SetHealth( hp );
		}
		// level up: offer the window once per level, out of combat
		KeyValues *p = Player();
		if ( ( p->GetInt( "points", 0 ) > 0 || p->GetInt( "perks_pending", 0 ) > 0 ) && pPlayer->IsAlive() &&
			p->GetInt( "levelup_offered", 0 ) < Level() && !RPMAudio::InCombat() )
		{
			p->SetInt( "levelup_offered", Level() );
			engine->ClientCommand( pPlayer->edict(), "rpm_char_open levelup\n" );
		}
		SendState( pPlayer, false );
		s_bDirty = false;
	}
};

static CRPMCharSystem g_RPMCharSystem;

// HL2RPM (M6): the theme of the HUD and the windows by the suit worn (hl2rpm_ui.h: 0 none, 1 HEV,
// 2 PCV, 3 Combine, 4 elite, 5 worker, 6 hazmat) - an apparel item of the database may name it ("suit_theme" "pcv"),
// else by its class; networked as m_Local.m_iSuitTheme, so a loaded game has the right colors
int RPMChar_SuitTheme( CBasePlayer *pPlayer )
{
	if ( !pPlayer || !pPlayer->IsSuitEquipped() )
		return 0;
	const char *cls = InventorySystem::GetEquippedSuitClass( pPlayer );
	if ( !cls || !cls[0] )
		return 1;
	const RPMItemDef_t *d = RPMItems::Find( cls );
	const char *t = d && d->pKV ? d->pKV->GetString( "suit_theme" ) : "";
	if ( !t[0] )
	{
		if ( Q_stristr( cls, "pcv" ) )
			t = "pcv";
		else if ( Q_stristr( cls, "worker" ) )
			t = "worker";
		else if ( Q_stristr( cls, "hazmat" ) )
			t = "hazmat";
		else if ( Q_stristr( cls, "elite" ) )
			t = "elite";
		else if ( Q_stristr( cls, "combine" ) || Q_stristr( cls, "_cp" ) || Q_stristr( cls, "police" ) || Q_stristr( cls, "prison" ) )
			t = "combine";
		else
			t = "hev";
	}
	static const char *names[] = { "none", "hev", "pcv", "combine", "elite", "worker", "hazmat" };
	for ( int i = 0; i < ARRAYSIZE( names ); i++ )
		if ( !Q_stricmp( t, names[i] ) )
			return i;
	return 1;
}

//=============================================================================
// commands of the client windows
//=============================================================================
static CBasePlayer *CmdPlayer()
{
	CBasePlayer *p = UTIL_GetCommandClient();
	return p ? p : UTIL_GetLocalPlayer();
}

// rpm_char_create_apply "<name>" "str=5;per=6;.." "guns;speech;medicine" "trait1;trait2"
CON_COMMAND( rpm_char_create_apply, "HL2RPM (client window): finish the character creation" )
{
	CBasePlayer *pPlayer = CmdPlayer();
	if ( !pPlayer || args.ArgC() < 5 )
		return;
	const int mn = (int)RPMRPG::Rule( "stat_min" ), mx = (int)RPMRPG::Rule( "stat_max" );
	int vals[RPM_STAT_COUNT];
	int sum = 0;
	for ( int i = 0; i < RPMRPG::StatCount(); i++ )
		vals[i] = (int)RPMRPG::Rule( "stat_start" );
	CUtlStringList parts;
	V_SplitString( args[2], ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
	{
		char *eq = Q_strstr( parts[i], "=" );
		if ( !eq )
			continue;
		*eq = 0;
		int s = RPMRPG::StatIndex( parts[i] );
		if ( s >= 0 )
			vals[s] = atoi( eq + 1 );
	}
	for ( int i = 0; i < RPMRPG::StatCount(); i++ )
	{
		if ( vals[i] < mn || vals[i] > mx )
		{
			Warning( "rpm_char_create_apply: %s = %d out of %d..%d\n", RPMRPG::StatId( i ), vals[i], mn, mx );
			return;
		}
		sum += vals[i];
	}
	if ( sum > (int)RPMRPG::Rule( "stat_total" ) )
	{
		Warning( "rpm_char_create_apply: %d attribute points, only %d allowed\n", sum, (int)RPMRPG::Rule( "stat_total" ) );
		return;
	}
	if ( ListCount( args[3] ) > (int)RPMRPG::Rule( "tags" ) || ListCount( args[4] ) > (int)RPMRPG::Rule( "traits_max" ) )
	{
		Warning( "rpm_char_create_apply: too many tagged skills or traits\n" );
		return;
	}
	KeyValues *p = Player();
	p->SetString( "name", args[1] );
	KeyValues *st = p->FindKey( "stats", true );
	for ( int i = 0; i < RPMRPG::StatCount(); i++ )
		st->SetInt( RPMRPG::StatId( i ), vals[i] );
	p->SetString( "tags", args[3] );
	p->SetString( "traits", args[4] );
	p->SetInt( "created", 1 );
	pPlayer->SetMaxHealth( MaxHealth() );
	pPlayer->SetHealth( MaxHealth() );
	if ( rpm_char_debug.GetBool() )
		Msg( "[char] created '%s': %s, tags %s, traits %s\n", args[1], args[2], args[3], args[4] );
	Dirty();
}

static bool PerkAvailable( const char *id, CUtlString *pWhy )
{
	KeyValues *d = RPMRPG::Perk( id );
	if ( !d )
		return false;
	KeyValues *perks = Player()->FindKey( "perks", true );
	if ( perks->GetInt( id, 0 ) >= Max( 1, d->GetInt( "ranks", 1 ) ) )
		return false;
	if ( Level() < d->GetInt( "level", 1 ) )
		return false;
	KeyValues *req = d->FindKey( "req" );
	for ( KeyValues *r = req ? req->GetFirstValue() : NULL; r; r = r->GetNextValue() )
	{
		int have = RPMRPG::StatIndex( r->GetName() ) >= 0 ? Stat( r->GetName() ) : Skill( r->GetName() );
		if ( have < r->GetInt() )
		{
			if ( pWhy )
				pWhy->Format( "%s %d", RPMRPG::Name( r->GetName() ), r->GetInt() );
			return false;
		}
	}
	return true;
}

// rpm_char_level_apply "guns=5;speech=7" "<perk id or empty>"
CON_COMMAND( rpm_char_level_apply, "HL2RPM (client window): spend skill points and take a perk" )
{
	CBasePlayer *pPlayer = CmdPlayer();
	if ( !pPlayer || args.ArgC() < 2 )
		return;
	KeyValues *p = Player();
	KeyValues *sk = p->FindKey( "skills", true );
	CUtlStringList parts;
	V_SplitString( args[1], ";", parts );
	int left = p->GetInt( "points", 0 );
	for ( int i = 0; i < parts.Count(); i++ )
	{
		char *eq = Q_strstr( parts[i], "=" );
		if ( !eq )
			continue;
		*eq = 0;
		int n = atoi( eq + 1 );
		if ( RPMRPG::SkillIndex( parts[i] ) < 0 || n <= 0 )
			continue;
		n = Min( n, left );
		n = Min( n, (int)RPMRPG::Rule( "skill_max" ) - Skill( parts[i] ) );
		if ( n <= 0 )
			continue;
		sk->SetInt( parts[i], sk->GetInt( parts[i], 0 ) + n );
		left -= n;
	}
	p->SetInt( "points", left );
	if ( args.ArgC() >= 3 && args[2][0] && p->GetInt( "perks_pending", 0 ) > 0 )
	{
		if ( PerkAvailable( args[2], NULL ) )
		{
			KeyValues *perks = p->FindKey( "perks", true );
			perks->SetInt( args[2], perks->GetInt( args[2], 0 ) + 1 );
			p->SetInt( "perks_pending", p->GetInt( "perks_pending" ) - 1 );
			RPMLogic::Toast( pPlayer, "perk", CFmtStr( IsRussian() ? "Новый перк: %s" : "New perk: %s", RPMRPG::Name( args[2] ) ), "" );
		}
		else
			Warning( "rpm_char_level_apply: perk '%s' is not available\n", args[2] );
	}
	Dirty();
}

CON_COMMAND( rpm_char_create, "HL2RPM: open the character creation window" )
{
	OpenCreation( CmdPlayer() );
}

CON_COMMAND( rpm_char_sync, "HL2RPM (client): send the character state again" )
{
	CBasePlayer *pPlayer = CmdPlayer();
	if ( pPlayer )
		SendState( pPlayer, true );
}

CON_COMMAND( rpm_char_dump, "HL2RPM: the player character" )
{
	KeyValues *p = Player();
	Msg( "'%s' created %d, level %d, xp %d (next %d), points %d, perks pending %d, hp %d, carry %.0f kg\n", p->GetString( "name" ),
		IsCreated() ? 1 : 0, Level(), XP(), RPMRPG::XPForLevel( Level() + 1 ), p->GetInt( "points", 0 ), p->GetInt( "perks_pending", 0 ),
		MaxHealth(), CarryWeight() );
	for ( int i = 0; i < RPMRPG::StatCount(); i++ )
		Msg( "  %-4s %2d (base %d)\n", RPMRPG::StatId( i ), Stat( RPMRPG::StatId( i ) ), BaseStat( RPMRPG::StatId( i ) ) );
	for ( int i = 0; i < RPMRPG::SkillCount(); i++ )
		Msg( "  %-11s %3d%s\n", RPMRPG::SkillId( i ), Skill( RPMRPG::SkillId( i ) ), ListHas( p->GetString( "tags" ), RPMRPG::SkillId( i ) ) ? "  (tag)" : "" );
	Msg( "  traits: %s\n", p->GetString( "traits" ) );
	KeyValues *perks = p->FindKey( "perks" );
	for ( KeyValues *x = perks ? perks->GetFirstValue() : NULL; x; x = x->GetNextValue() )
		Msg( "  perk %s rank %d\n", x->GetName(), x->GetInt() );
	for ( KeyValues *x = RPMState::Section( "rep" )->GetFirstValue(); x; x = x->GetNextValue() )
		Msg( "  rep %s %d\n", x->GetName(), x->GetInt() );
	for ( KeyValues *x = RPMRPG::Perks()->GetFirstTrueSubKey(); x; x = x->GetNextTrueSubKey() )
	{
		CUtlString why;
		bool ok = PerkAvailable( x->GetName(), &why );
		Msg( "  [%s] perk %s (level %d)%s\n", ok ? "available" : "-", x->GetName(), x->GetInt( "level", 1 ), why.IsEmpty() ? "" : CFmtStr( ", needs %s", why.Get() ).Access() );
	}
}

CON_COMMAND_F( rpm_xp, "HL2RPM: give experience", FCVAR_CHEAT )
{
	if ( args.ArgC() >= 2 )
		AddXP( CmdPlayer(), atoi( args[1] ) );
}

CON_COMMAND_F( rpm_perk, "HL2RPM: give a perk (rank +1)", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 || !RPMRPG::Perk( args[1] ) )
	{
		Msg( "rpm_perk <id> - a perk of scripts/rpg/rpg.txt\n" );
		return;
	}
	KeyValues *perks = Player()->FindKey( "perks", true );
	perks->SetInt( args[1], perks->GetInt( args[1], 0 ) + 1 );
	Dirty();
}

CON_COMMAND_F( rpm_rep, "HL2RPM: rpm_rep <faction> <delta> - reputation with a faction", FCVAR_CHEAT )
{
	if ( args.ArgC() >= 3 )
		AddFactionRep( CmdPlayer(), args[1], atoi( args[2] ) );
}

//========= HL2RPM ============//
//
// RPG-система: загрузка данных scripts/rpg/*.txt (см. rpg_data.h, RPG_SPEC §2).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_formulas.h"
#include "filesystem.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CRPGData g_RPGData;

CRPGData &RPGData()
{
	return g_RPGData;
}

#ifdef CLIENT_DLL
#define RPG_DATA_SIDE "client"
#else
#define RPG_DATA_SIDE "server"
#endif

CRPGData::CRPGData() : m_ItemIndex( k_eDictCompareTypeCaseInsensitive )
{
	m_bLoaded = false;
	m_iKillDailyLimit = 10;
}

static KeyValues *LoadRPGFile( const char *pszFile, const char *pszRoot )
{
	char szPath[MAX_PATH];
	Q_snprintf( szPath, sizeof( szPath ), "scripts/rpg/%s", pszFile );
	KeyValues *pKV = new KeyValues( pszRoot );
	if ( !pKV->LoadFromFile( filesystem, szPath, "GAME" ) )
	{
		DevWarning( "[rpg] %s: %s not found - built-in defaults\n", RPG_DATA_SIDE, szPath );
		pKV->deleteThis();
		return NULL;
	}
	return pKV;
}

// ---------------------------------------------------------------------------
// Модификаторы: известные ключи (RPG_SPEC §2) + ключи, которые уже есть в данных
// для будущих систем (хранятся и доступны через GetModAdd/GetModMult, без предупреждения)
// ---------------------------------------------------------------------------
static const char *s_pszKnownModKeys[] =
{
	// с эффектом сейчас
	"suspicion_gain", "carry", "hp", "start_credits", "start_civil", "armor",
	"bribe_cost", "suspicion_decay",
	// хранятся для боя/торговли/выживания (Part I/04, 11, 12)
	"melee_damage", "shot_spread", "melee_crit_damage", "fire_rate", "damage", "damage_human",
	"damage_alliance", "damage_explosive", "explosive_radius", "resonance_gain", "ration_heal",
	"food_heal", "water_thirst", "injury_chance", "approval_gain", "underwater_time",
	"water_radiation", "lambda_marks_range", "speed_suspicion", "scanner_range", "loot_consumables",
	"lockpick_break", "book_bonus", "samizdat_hours", "shift_pay", "shift_fatigue", "pistol_damage",
	"craft_bonus", "medkit_heal", "repair_bonus", "lock_tier", "melee_zombie", "silent_steps",
	"vort_prices", "food_poison", "disguise", "trade_sell", "trader_cash", "rep_loss_dual",
	"suspicion_crowd_decay", "mine_highlight", "pulse_damage", "bolt_damage", "plant_items",
	NULL
};

bool CRPGData::IsKnownModKey( const char *pszKey )
{
	if ( !pszKey || !pszKey[0] )
		return false;
	if ( !Q_strnicmp( pszKey, "skill:", 6 ) || !Q_strnicmp( pszKey, "attr:", 5 ) || !Q_strnicmp( pszKey, "start_rep:", 10 ) )
		return true;
	for ( int i = 0; s_pszKnownModKeys[i]; ++i )
	{
		if ( !Q_stricmp( pszKey, s_pszKnownModKeys[i] ) )
			return true;
	}
	return false;
}

void CRPGData::ParseMods( KeyValues *pMods, RPGModList_t &out, const char *pszOwner )
{
	out.RemoveAll();
	if ( !pMods )
		return;
	for ( KeyValues *pKey = pMods->GetFirstValue(); pKey; pKey = pKey->GetNextValue() )
	{
		const char *pszName = pKey->GetName();
		const char *pszVal = pKey->GetString();
		if ( !IsKnownModKey( pszName ) )
			Warning( "[rpg] %s: unknown mod '%s' in %s (ignored)\n", RPG_DATA_SIDE, pszName, pszOwner ? pszOwner : "?" );

		RPGMod_t mod;
		mod.key = pszName;
		mod.bMul = false;
		while ( *pszVal == ' ' )
			++pszVal;
		// "x0.75", "*0.75", "×0.75" (UTF-8 C3 97) - множитель
		if ( *pszVal == 'x' || *pszVal == 'X' || *pszVal == '*' )
		{
			mod.bMul = true;
			++pszVal;
		}
		else if ( (unsigned char)pszVal[0] == 0xC3 && (unsigned char)pszVal[1] == 0x97 )
		{
			mod.bMul = true;
			pszVal += 2;
		}
		mod.value = (float)atof( pszVal );
		if ( !IsKnownModKey( pszName ) )
			continue;
		out.AddToTail( mod );
	}
}

void CRPGData::SetDefaults()
{
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
	{
		m_Attrs[i].id = g_pszRPGAttrIds[i];
		m_Attrs[i].abbr = g_pszRPGAttrAbbr[i];
		m_Attrs[i].name.Format( "#RPG_Attr_%s", g_pszRPGAttrIds[i] );
		m_Attrs[i].desc.Format( "#RPG_Attr_%s_Desc", g_pszRPGAttrIds[i] );
		m_Attrs[i].icon.Format( "vgui/hl2rpm/attr/%s", g_pszRPGAttrIds[i] );
	}
	m_Skills.RemoveAll();
	m_Traits.RemoveAll();
	m_Perks.RemoveAll();
	m_Factions.RemoveAll();
	m_Items.RemoveAll();
	m_ItemIndex.RemoveAll();
	m_Traders.RemoveAll();
	m_Locations.RemoveAll();
	m_Kills.RemoveAll();
	m_iKillDailyLimit = 10;
}

void CRPGData::Load()
{
	SetDefaults();
	LoadAttributes();
	LoadSkills();
	LoadTraits();
	LoadPerks();
	LoadFactions();
	LoadItems();
	LoadTraders();
	LoadLocations();
	LoadKills();
	m_bLoaded = true;
	DevMsg( "[rpg] %s: data loaded: %d skills, %d traits, %d perks, %d factions, %d items, %d traders, %d locations, %d kill XP\n",
		RPG_DATA_SIDE, m_Skills.Count(), m_Traits.Count(), m_Perks.Count(), m_Factions.Count(), m_Items.Count(),
		m_Traders.Count(), m_Locations.Count(), m_Kills.Count() );
}

void CRPGData::LoadAttributes()
{
	KeyValues *pKV = LoadRPGFile( "attributes.txt", "Attributes" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		int i = RPGFormulas::AttrFromName( pSub->GetString( "id", pSub->GetName() ) );
		if ( i < 0 )
		{
			Warning( "[rpg] attributes.txt: unknown attribute '%s'\n", pSub->GetName() );
			continue;
		}
		RPGAttrDef_t &d = m_Attrs[i];
		d.abbr = pSub->GetString( "abbr", d.abbr.Get() );
		d.name = pSub->GetString( "name", d.name.Get() );
		d.desc = pSub->GetString( "desc", d.desc.Get() );
		d.icon = pSub->GetString( "icon", d.icon.Get() );
	}
	pKV->deleteThis();
}

int CRPGData::FindAttr( const char *pszIdOrAbbr ) const
{
	int i = RPGFormulas::AttrFromName( pszIdOrAbbr );
	if ( i >= 0 )
		return i;
	for ( i = 0; i < RPG_ATTR_COUNT; ++i )
	{
		if ( !Q_stricmp( m_Attrs[i].abbr.Get(), pszIdOrAbbr ) )
			return i;
	}
	return -1;
}

// 13 навыков Part I/03 - запасной набор, если skills.txt нет
static const struct { const char *id; int p; int s; } s_DefaultSkills[] =
{
	{ "firearms",   RPG_ATTR_AGILITY,   RPG_ATTR_VIGILANCE },
	{ "pulse",      RPG_ATTR_VIGILANCE, RPG_ATTR_ANALYTICS },
	{ "explosives", RPG_ATTR_VIGILANCE, RPG_ATTR_AGILITY },
	{ "melee",      RPG_ATTR_MIGHT,     RPG_ATTR_AGILITY },
	{ "lockpick",   RPG_ATTR_AGILITY,   RPG_ATTR_VIGILANCE },
	{ "hacking",    RPG_ATTR_ANALYTICS, RPG_ATTR_VIGILANCE },
	{ "science",    RPG_ATTR_ANALYTICS, RPG_ATTR_VIGILANCE },
	{ "medicine",   RPG_ATTR_ANALYTICS, RPG_ATTR_AGILITY },
	{ "repair",     RPG_ATTR_ANALYTICS, RPG_ATTR_MIGHT },
	{ "sneak",      RPG_ATTR_AGILITY,   RPG_ATTR_VIGILANCE },
	{ "speech",     RPG_ATTR_AUTHORITY, RPG_ATTR_ANALYTICS },
	{ "barter",     RPG_ATTR_AUTHORITY, RPG_ATTR_ANALYTICS },
	{ "survival",   RPG_ATTR_SPIRIT,    RPG_ATTR_VIGILANCE },
};

static void InitSkillDef( RPGSkillDef_t &d, const char *pszId, int p, int s )
{
	d.id = pszId;
	d.primary = p;
	d.secondary = s;
	d.name.Format( "#RPG_Skill_%s", pszId );
	d.desc.Format( "#RPG_Skill_%s_Desc", pszId );
	d.icon.Format( "vgui/hl2rpm/skills/%s", pszId );
}

void CRPGData::LoadSkills()
{
	KeyValues *pKV = LoadRPGFile( "skills.txt", "Skills" );
	if ( pKV )
	{
		for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
		{
			if ( m_Skills.Count() >= RPG_MAX_SKILLS )
			{
				Warning( "[rpg] skills.txt: more than %d skills\n", RPG_MAX_SKILLS );
				break;
			}
			const char *pszId = pSub->GetString( "id", pSub->GetName() );
			int p = FindAttr( pSub->GetString( "primary" ) );
			int s = FindAttr( pSub->GetString( "secondary" ) );
			if ( p < 0 || s < 0 )
			{
				Warning( "[rpg] skills.txt: '%s' has bad primary/secondary\n", pszId );
				continue;
			}
			RPGSkillDef_t &d = m_Skills[m_Skills.AddToTail()];
			InitSkillDef( d, pszId, p, s );
			d.name = pSub->GetString( "name", d.name.Get() );
			d.desc = pSub->GetString( "desc", d.desc.Get() );
			d.icon = pSub->GetString( "icon", d.icon.Get() );
		}
		pKV->deleteThis();
	}
	if ( m_Skills.Count() == 0 )
	{
		for ( int i = 0; i < ARRAYSIZE( s_DefaultSkills ); ++i )
			InitSkillDef( m_Skills[m_Skills.AddToTail()], s_DefaultSkills[i].id, s_DefaultSkills[i].p, s_DefaultSkills[i].s );
	}
}

int CRPGData::FindSkill( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return -1;
	for ( int i = 0; i < m_Skills.Count(); ++i )
	{
		if ( !Q_stricmp( m_Skills[i].id.Get(), pszId ) )
			return i;
	}
	return -1;
}

void CRPGData::LoadTraits()
{
	KeyValues *pKV = LoadRPGFile( "traits.txt", "Traits" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		RPGTraitDef_t &d = m_Traits[m_Traits.AddToTail()];
		d.id = pSub->GetString( "id", pSub->GetName() );
		d.slug = pSub->GetString( "slug" );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Trait_%s", d.id.Get() ) );
		d.desc = pSub->GetString( "desc", CFmtStr( "#RPG_Trait_%s_Desc", d.id.Get() ) );
		d.icon = pSub->GetString( "icon" );
		ParseMods( pSub->FindKey( "mods" ), d.mods, d.id.Get() );
	}
	pKV->deleteThis();
}

const RPGTraitDef_t *CRPGData::FindTrait( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Traits.Count(); ++i )
	{
		if ( !Q_stricmp( m_Traits[i].id.Get(), pszId ) || ( m_Traits[i].slug.Length() && !Q_stricmp( m_Traits[i].slug.Get(), pszId ) ) )
			return &m_Traits[i];
	}
	return NULL;
}

void CRPGData::LoadPerks()
{
	KeyValues *pKV = LoadRPGFile( "perks.txt", "Perks" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		RPGPerkDef_t &d = m_Perks[m_Perks.AddToTail()];
		d.id = pSub->GetString( "id", pSub->GetName() );
		d.slug = pSub->GetString( "slug" );
		d.level = Max( 1, pSub->GetInt( "level", 2 ) );
		d.ranks = Max( 1, pSub->GetInt( "ranks", 1 ) );
		d.require = pSub->GetString( "require" );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Perk_%s", d.id.Get() ) );
		d.desc = pSub->GetString( "desc", CFmtStr( "#RPG_Perk_%s_Desc", d.id.Get() ) );
		d.flavor = pSub->GetString( "flavor", CFmtStr( "#RPG_Perk_%s_Flavor", d.id.Get() ) );
		d.icon = pSub->GetString( "icon" );
		ParseMods( pSub->FindKey( "mods" ), d.mods, d.id.Get() );
	}
	pKV->deleteThis();
}

const RPGPerkDef_t *CRPGData::FindPerk( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Perks.Count(); ++i )
	{
		if ( !Q_stricmp( m_Perks[i].id.Get(), pszId ) || ( m_Perks[i].slug.Length() && !Q_stricmp( m_Perks[i].slug.Get(), pszId ) ) )
			return &m_Perks[i];
	}
	return NULL;
}

void CRPGData::LoadFactions()
{
	KeyValues *pKV = LoadRPGFile( "factions.txt", "Factions" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		RPGFactionDef_t &d = m_Factions[m_Factions.AddToTail()];
		d.id = pSub->GetString( "id", pSub->GetName() );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Faction_%s", d.id.Get() ) );
		d.icon = pSub->GetString( "icon" );
		d.start = clamp( pSub->GetInt( "start", 0 ), -100, 100 );
		d.hidden = pSub->GetBool( "hidden", false );
		d.service = pSub->GetBool( "service", false );
	}
	pKV->deleteThis();
}

const RPGFactionDef_t *CRPGData::FindFaction( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Factions.Count(); ++i )
	{
		if ( !Q_stricmp( m_Factions[i].id.Get(), pszId ) )
			return &m_Factions[i];
	}
	return NULL;
}

void CRPGData::LoadItems()
{
	KeyValues *pKV = LoadRPGFile( "items.txt", "Items" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		const char *pszClass = pSub->GetName();
		if ( m_ItemIndex.Find( pszClass ) != m_ItemIndex.InvalidIndex() )
		{
			Warning( "[rpg] items.txt: '%s' defined twice\n", pszClass );
			continue;
		}
		int idx = m_Items.AddToTail();
		RPGItemDef_t &d = m_Items[idx];
		d.classname = pszClass;
		d.category = pSub->GetString( "category", "misc" );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Item_%s", pszClass ) );
		d.desc = pSub->GetString( "desc", CFmtStr( "#RPG_Item_%s_Desc", pszClass ) );
		d.icon = pSub->GetString( "icon" );
		d.model = pSub->GetString( "model" );
		d.use = pSub->GetString( "use" );
		d.armor = pSub->GetString( "armor" );
		d.kg = pSub->FindKey( "kg" ) ? pSub->GetFloat( "kg" ) : -1.0f;
		d.value = Max( 0, pSub->GetInt( "value", 0 ) );
		d.contraband = clamp( pSub->GetInt( "contraband", 0 ), 0, 3 );
		d.skin = pSub->GetInt( "skin", 0 );
		ParseMods( pSub->FindKey( "mods" ), d.mods, pszClass );
		if ( d.armor.Length() && RPG_SuitTypeFromKey( d.armor.Get() ) < 0 )
			Warning( "[rpg] items.txt: '%s' has unknown armor type '%s'\n", pszClass, d.armor.Get() );
		m_ItemIndex.Insert( pszClass, idx );
	}
	pKV->deleteThis();
}

const RPGItemDef_t *CRPGData::FindItem( const char *pszClassname ) const
{
	if ( !pszClassname || !pszClassname[0] )
		return NULL;
	int i = m_ItemIndex.Find( pszClassname );
	if ( i == m_ItemIndex.InvalidIndex() )
		return NULL;
	return &m_Items[m_ItemIndex[i]];
}

void CRPGData::LoadTraders()
{
	KeyValues *pKV = LoadRPGFile( "traders.txt", "Traders" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		RPGTraderDef_t &d = m_Traders[m_Traders.AddToTail()];
		d.id = pSub->GetString( "id", pSub->GetName() );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Trader_%s", d.id.Get() ) );
		d.faction = pSub->GetString( "faction" );
		d.credits = Max( 0, pSub->GetInt( "credits", 0 ) );
		d.buyMult = pSub->GetFloat( "buy_mult", 1.0f );
		d.sellMult = pSub->GetFloat( "sell_mult", 1.0f );
		d.restockHours = pSub->GetFloat( "restock_hours", 72.0f );
		d.acceptsContraband = pSub->GetBool( "contraband", false );
		KeyValues *pItems = pSub->FindKey( "items" );
		if ( pItems )
		{
			for ( KeyValues *pIt = pItems->GetFirstValue(); pIt; pIt = pIt->GetNextValue() )
			{
				RPGTraderItem_t &it = d.items[d.items.AddToTail()];
				it.classname = pIt->GetName();
				it.count = Max( 1, pIt->GetInt() );
			}
		}
	}
	pKV->deleteThis();
}

const RPGTraderDef_t *CRPGData::FindTrader( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Traders.Count(); ++i )
	{
		if ( !Q_stricmp( m_Traders[i].id.Get(), pszId ) )
			return &m_Traders[i];
	}
	return NULL;
}

void CRPGData::LoadLocations()
{
	KeyValues *pKV = LoadRPGFile( "locations.txt", "Locations" );
	if ( !pKV )
		return;
	for ( KeyValues *pSub = pKV->GetFirstTrueSubKey(); pSub; pSub = pSub->GetNextTrueSubKey() )
	{
		RPGLocationDef_t &d = m_Locations[m_Locations.AddToTail()];
		d.id = pSub->GetString( "id", pSub->GetName() );
		d.name = pSub->GetString( "name", CFmtStr( "#RPG_Loc_%s", d.id.Get() ) );
		d.map = pSub->GetString( "map" );
		d.xp = Max( 0, pSub->GetInt( "xp", 20 ) );
	}
	pKV->deleteThis();
}

const RPGLocationDef_t *CRPGData::FindLocation( const char *pszId ) const
{
	if ( !pszId || !pszId[0] )
		return NULL;
	for ( int i = 0; i < m_Locations.Count(); ++i )
	{
		if ( !Q_stricmp( m_Locations[i].id.Get(), pszId ) )
			return &m_Locations[i];
	}
	return NULL;
}

void CRPGData::LoadKills()
{
	KeyValues *pKV = LoadRPGFile( "kills.txt", "Kills" );
	if ( !pKV )
		return;
	m_iKillDailyLimit = pKV->GetInt( "daily_limit", 10 );
	KeyValues *pList = pKV->FindKey( "xp" );
	if ( pList )
	{
		for ( KeyValues *pSub = pList->GetFirstSubKey(); pSub; pSub = pSub->GetNextKey() )
		{
			RPGKillDef_t &d = m_Kills[m_Kills.AddToTail()];
			d.classname = pSub->GetName();
			if ( pSub->GetFirstSubKey() )
			{
				d.xp = pSub->GetInt( "xp", 0 );
				d.humanity = pSub->GetInt( "humanity", 0 );
			}
			else
			{
				d.xp = pSub->GetInt();
				d.humanity = 0;
			}
		}
	}
	pKV->deleteThis();
}

const RPGKillDef_t *CRPGData::FindKill( const char *pszClassname ) const
{
	if ( !pszClassname || !pszClassname[0] )
		return NULL;
	for ( int i = 0; i < m_Kills.Count(); ++i )
	{
		if ( !Q_stricmp( m_Kills[i].classname.Get(), pszClassname ) )
			return &m_Kills[i];
	}
	return NULL;
}

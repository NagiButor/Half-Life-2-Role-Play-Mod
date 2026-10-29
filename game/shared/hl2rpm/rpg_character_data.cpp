//========= HL2RPM ============//
//
// RPG-система: данные персонажа и производные значения (см. rpg_character_data.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_character_data.h"
#include "hl2rpm/rpg_formulas.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CRPGCharacterData::CRPGCharacterData()
	: m_Perks( k_eDictCompareTypeCaseInsensitive ),
	  m_Factions( k_eDictCompareTypeCaseInsensitive ),
	  m_Flags( k_eDictCompareTypeCaseInsensitive ),
	  m_Dispositions( k_eDictCompareTypeCaseInsensitive )
{
	Reset();
}

void CRPGCharacterData::Reset()
{
	m_Name = "#RPG_Name_Default";
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
		m_Attr[i] = RPG_ATTR_START;
	for ( int i = 0; i < RPG_MAX_SKILLS; ++i )
	{
		m_SkillInvested[i] = 0;
		m_SkillTagged[i] = false;
	}
	m_Traits.RemoveAll();
	m_Perks.RemoveAll();
	m_iLevel = 1;
	m_iXP = 0;
	m_iSkillPoints = 0;
	m_iPerkPicks = 0;
	m_iCredits = 0;
	m_iCivil = 0;
	m_flSuspicion = 0.0f;
	m_iHumanity = 50;
	m_iResonance = 0;
	m_iInfluence = 0;
	m_Factions.RemoveAll();
	m_Flags.RemoveAll();
	m_Dispositions.RemoveAll();
	m_Notes.RemoveAll();
	m_Locations.RemoveAll();
	m_bChargenDone = false;
	m_iZone = RPG_ZONE_NONE;
	m_bAlarm = false;
	m_ArmorClass = "";

	const CRPGData &data = RPGData();
	for ( int i = 0; i < data.GetFactionCount(); ++i )
	{
		const RPGFactionDef_t &f = data.GetFaction( i );
		if ( !f.service )
			m_Factions.Insert( f.id.Get(), f.start );
	}

	// акт 1, день 1
	RPGFlagValue_t one;
	one.iValue = 1;
	m_Flags.Insert( RPG_FLAG_ACT, one );
	m_Flags.Insert( RPG_FLAG_DAY, one );
}

// ---------------------------------------------------------------------------
// Модификаторы: черты (по одному разу), перки (× ранг), надетая броня
// ---------------------------------------------------------------------------
static void AccumulateList( const RPGModList_t &mods, int times, const char *pszKey, float &flAdd, float &flMul )
{
	for ( int i = 0; i < mods.Count(); ++i )
	{
		if ( Q_stricmp( mods[i].key.Get(), pszKey ) )
			continue;
		for ( int t = 0; t < times; ++t )
		{
			if ( mods[i].bMul )
				flMul *= mods[i].value;
			else
				flAdd += mods[i].value;
		}
	}
}

void CRPGCharacterData::AccumulateMods( const char *pszKey, float &flAdd, float &flMul ) const
{
	const CRPGData &data = RPGData();
	for ( int i = 0; i < m_Traits.Count(); ++i )
	{
		const RPGTraitDef_t *pTrait = data.FindTrait( m_Traits[i].Get() );
		if ( pTrait )
			AccumulateList( pTrait->mods, 1, pszKey, flAdd, flMul );
	}
	for ( int i = m_Perks.First(); i != m_Perks.InvalidIndex(); i = m_Perks.Next( i ) )
	{
		const RPGPerkDef_t *pPerk = data.FindPerk( m_Perks.GetElementName( i ) );
		if ( pPerk )
			AccumulateList( pPerk->mods, clamp( m_Perks[i], 0, pPerk->ranks ), pszKey, flAdd, flMul );
	}
	if ( m_ArmorClass.Length() )
	{
		const RPGItemDef_t *pItem = data.FindItem( m_ArmorClass.Get() );
		if ( pItem )
			AccumulateList( pItem->mods, 1, pszKey, flAdd, flMul );
	}
}

float CRPGCharacterData::GetModAdd( const char *pszKey ) const
{
	float flAdd = 0.0f, flMul = 1.0f;
	AccumulateMods( pszKey, flAdd, flMul );
	return flAdd;
}

float CRPGCharacterData::GetModMult( const char *pszKey ) const
{
	float flAdd = 0.0f, flMul = 1.0f;
	AccumulateMods( pszKey, flAdd, flMul );
	return flMul;
}

// ---------------------------------------------------------------------------
// Производные значения
// ---------------------------------------------------------------------------
int CRPGCharacterData::GetAttrBase( int attr ) const
{
	if ( attr < 0 || attr >= RPG_ATTR_COUNT )
		return 0;
	return m_Attr[attr];
}

int CRPGCharacterData::GetAttr( int attr ) const
{
	if ( attr < 0 || attr >= RPG_ATTR_COUNT )
		return 0;
	float flAdd = 0.0f, flMul = 1.0f;
	AccumulateMods( CFmtStr( "attr:%s", g_pszRPGAttrIds[attr] ), flAdd, flMul );
	int v = (int)floorf( ( (float)m_Attr[attr] + flAdd ) * flMul + 0.5f );
	return clamp( v, RPG_ATTR_MIN, RPG_ATTR_MAX_EFFECTIVE );
}

int CRPGCharacterData::GetSkillBase( int skill ) const
{
	const CRPGData &data = RPGData();
	if ( skill < 0 || skill >= data.GetSkillCount() )
		return 0;
	const RPGSkillDef_t &d = data.GetSkill( skill );
	return RPGFormulas::SkillBase( GetAttr( d.primary ), GetAttr( d.secondary ) );
}

int CRPGCharacterData::GetSkill( int skill, bool bForCheck ) const
{
	const CRPGData &data = RPGData();
	if ( skill < 0 || skill >= data.GetSkillCount() || skill >= RPG_MAX_SKILLS )
		return 0;
	float flAdd = 0.0f, flMul = 1.0f;
	AccumulateMods( CFmtStr( "skill:%s", data.GetSkill( skill ).id.Get() ), flAdd, flMul );
	int v = GetSkillBase( skill ) + ( m_SkillTagged[skill] ? RPG_TAG_BONUS : 0 ) + m_SkillInvested[skill];
	v = (int)floorf( ( (float)v + flAdd ) * flMul + 0.5f );
	return clamp( v, 0, bForCheck ? RPG_SKILL_CAP_CHECK : RPG_SKILL_CAP );
}

int CRPGCharacterData::GetSkill( const char *pszSkill, bool bForCheck ) const
{
	return GetSkill( RPGData().FindSkill( pszSkill ), bForCheck );
}

int CRPGCharacterData::GetSkillInvested( int skill ) const
{
	return ( skill >= 0 && skill < RPG_MAX_SKILLS ) ? m_SkillInvested[skill] : 0;
}

bool CRPGCharacterData::IsSkillTagged( int skill ) const
{
	return ( skill >= 0 && skill < RPG_MAX_SKILLS ) ? m_SkillTagged[skill] : false;
}

int CRPGCharacterData::GetTagCount() const
{
	int n = 0;
	for ( int i = 0; i < RPG_MAX_SKILLS; ++i )
		n += m_SkillTagged[i] ? 1 : 0;
	return n;
}

int CRPGCharacterData::GetMaxHealth() const
{
	return RPGFormulas::MaxHealth( GetAttr( RPG_ATTR_SPIRIT ), m_iLevel, (int)GetModAdd( "hp" ) );
}

float CRPGCharacterData::GetCarryWeight() const
{
	return RPGFormulas::CarryWeight( GetAttr( RPG_ATTR_MIGHT ), GetModAdd( "carry" ) );
}

int CRPGCharacterData::GetArmor() const
{
	return (int)GetModAdd( "armor" );
}

int CRPGCharacterData::GetXPForNextLevel() const
{
	if ( m_iLevel >= RPG_MAX_LEVEL )
		return RPGFormulas::XPForLevel( RPG_MAX_LEVEL );
	return RPGFormulas::XPForLevel( m_iLevel + 1 );
}

int CRPGCharacterData::GetCivilTier() const
{
	return RPGFormulas::CivilTier( m_iCivil );
}

bool CRPGCharacterData::HasTempPass() const
{
	return IsFlagSet( RPG_FLAG_TEMPPASS );
}

int CRPGCharacterData::GetSuspicionTier() const
{
	return RPGFormulas::SuspicionTier( m_flSuspicion );
}

int CRPGCharacterData::GetHumanityTier() const
{
	return RPGFormulas::HumanityTier( m_iHumanity );
}

int CRPGCharacterData::GetAct() const
{
	return Max( 1, GetFlagInt( RPG_FLAG_ACT, 1 ) );
}

// ---------------------------------------------------------------------------
// Доступ
// ---------------------------------------------------------------------------
bool CRPGCharacterData::HasTrait( const char *pszId ) const
{
	const RPGTraitDef_t *pDef = RPGData().FindTrait( pszId );
	const char *pszCanon = pDef ? pDef->id.Get() : pszId;
	for ( int i = 0; i < m_Traits.Count(); ++i )
	{
		if ( !Q_stricmp( m_Traits[i].Get(), pszCanon ) )
			return true;
	}
	return false;
}

int CRPGCharacterData::GetPerkRank( const char *pszId ) const
{
	const RPGPerkDef_t *pDef = RPGData().FindPerk( pszId );
	int i = m_Perks.Find( pDef ? pDef->id.Get() : pszId );
	return ( i != m_Perks.InvalidIndex() ) ? m_Perks[i] : 0;
}

int CRPGCharacterData::GetFaction( const char *pszId ) const
{
	int i = m_Factions.Find( pszId );
	if ( i != m_Factions.InvalidIndex() )
		return m_Factions[i];
	const RPGFactionDef_t *pDef = RPGData().FindFaction( pszId );
	return pDef ? pDef->start : 0;
}

int CRPGCharacterData::GetFactionTier( const char *pszId ) const
{
	return RPGFormulas::FactionTier( GetFaction( pszId ) );
}

bool CRPGCharacterData::HasDisposition( const char *pszNPC ) const
{
	return pszNPC && pszNPC[0] && m_Dispositions.Find( pszNPC ) != m_Dispositions.InvalidIndex();
}

int CRPGCharacterData::GetDisposition( const char *pszNPC, int iDefault ) const
{
	if ( !pszNPC || !pszNPC[0] )
		return iDefault;
	int i = m_Dispositions.Find( pszNPC );
	return ( i != m_Dispositions.InvalidIndex() ) ? m_Dispositions[i] : iDefault;
}

static bool ListHas( const CUtlVector<CUtlString> &list, const char *pszId )
{
	if ( !pszId || !pszId[0] )
		return false;
	for ( int i = 0; i < list.Count(); ++i )
	{
		if ( !Q_stricmp( list[i].Get(), pszId ) )
			return true;
	}
	return false;
}

bool CRPGCharacterData::HasNote( const char *pszId ) const
{
	return ListHas( m_Notes, pszId );
}

bool CRPGCharacterData::HasLocation( const char *pszId ) const
{
	return ListHas( m_Locations, pszId );
}

const RPGFlagValue_t *CRPGCharacterData::FindFlag( const char *pszName ) const
{
	if ( !pszName || !pszName[0] )
		return NULL;
	int i = m_Flags.Find( pszName );
	return ( i != m_Flags.InvalidIndex() ) ? &m_Flags[i] : NULL;
}

bool CRPGCharacterData::IsFlagSet( const char *pszName ) const
{
	const RPGFlagValue_t *pFlag = FindFlag( pszName );
	return pFlag && pFlag->IsSet();
}

int CRPGCharacterData::GetFlagInt( const char *pszName, int iDefault ) const
{
	const RPGFlagValue_t *pFlag = FindFlag( pszName );
	if ( !pFlag )
		return iDefault;
	if ( pFlag->IsString() )
		return atoi( pFlag->sValue.Get() );
	return pFlag->iValue;
}

const char *CRPGCharacterData::GetFlagString( const char *pszName ) const
{
	const RPGFlagValue_t *pFlag = FindFlag( pszName );
	if ( !pFlag )
		return "";
	if ( pFlag->IsString() )
		return pFlag->sValue.Get();
	static char s_szBuf[16];
	Q_snprintf( s_szBuf, sizeof( s_szBuf ), "%d", pFlag->iValue );
	return s_szBuf;
}

// ---------------------------------------------------------------------------
// Торговля
// ---------------------------------------------------------------------------
void CRPGCharacterData::TraderCreditsFlag( const char *pszTrader, char *pszOut, int nOut )
{
	Q_snprintf( pszOut, nOut, "trd_%s_cr", pszTrader );
}

void CRPGCharacterData::TraderStockFlag( const char *pszTrader, const char *pszClass, char *pszOut, int nOut )
{
	Q_snprintf( pszOut, nOut, "trd_%s.%s", pszTrader, pszClass );
}

void CRPGCharacterData::TraderHaggleFlag( const char *pszTrader, bool bDay, char *pszOut, int nOut )
{
	Q_snprintf( pszOut, nOut, bDay ? "haggle_%s_day" : "haggle_%s", pszTrader );
}

int CRPGCharacterData::GetTraderHaggle( const RPGTraderDef_t &t ) const
{
	char szFlag[96];
	TraderHaggleFlag( t.id.Get(), true, szFlag, sizeof( szFlag ) );
	if ( GetFlagInt( szFlag, 0 ) != Max( 1, GetFlagInt( RPG_FLAG_DAY, 1 ) ) )
		return 0;
	TraderHaggleFlag( t.id.Get(), false, szFlag, sizeof( szFlag ) );
	return clamp( GetFlagInt( szFlag, 0 ), -50, 50 );
}

int CRPGCharacterData::GetTraderCredits( const RPGTraderDef_t &t ) const
{
	char szFlag[96];
	TraderCreditsFlag( t.id.Get(), szFlag, sizeof( szFlag ) );
	const int v = GetFlagInt( szFlag, 0 );
	return v > 0 ? v - 1 : t.credits;
}

int CRPGCharacterData::GetTraderStock( const RPGTraderDef_t &t, const char *pszClass ) const
{
	char szFlag[128];
	TraderStockFlag( t.id.Get(), pszClass, szFlag, sizeof( szFlag ) );
	const int v = GetFlagInt( szFlag, 0 );
	if ( v > 0 )
		return v - 1;
	for ( int i = 0; i < t.items.Count(); ++i )
	{
		if ( !Q_stricmp( t.items[i].classname.Get(), pszClass ) )
			return t.items[i].count;
	}
	return 0;
}

void CRPGCharacterData::GetTraderStockList( const RPGTraderDef_t &t, CUtlVector<RPGTraderItem_t> &out ) const
{
	out.RemoveAll();
	// сначала ассортимент из traders.txt (в его порядке), затем то, что торговцу продал игрок
	for ( int i = 0; i < t.items.Count(); ++i )
	{
		const int n = GetTraderStock( t, t.items[i].classname.Get() );
		if ( n > 0 )
		{
			RPGTraderItem_t &it = out[out.AddToTail()];
			it.classname = t.items[i].classname;
			it.count = n;
		}
	}
	char szPrefix[96];
	Q_snprintf( szPrefix, sizeof( szPrefix ), "trd_%s.", t.id.Get() );
	const int nPrefix = Q_strlen( szPrefix );
	for ( int i = m_Flags.First(); i != m_Flags.InvalidIndex(); i = m_Flags.Next( i ) )
	{
		const char *pszName = m_Flags.GetElementName( i );
		if ( Q_strnicmp( pszName, szPrefix, nPrefix ) || m_Flags[i].iValue <= 1 )
			continue;
		const char *pszClass = pszName + nPrefix;
		bool bListed = false;
		for ( int j = 0; j < t.items.Count() && !bListed; ++j )
			bListed = !Q_stricmp( t.items[j].classname.Get(), pszClass );
		if ( bListed )
			continue;
		RPGTraderItem_t &it = out[out.AddToTail()];
		it.classname = pszClass;
		it.count = m_Flags[i].iValue - 1;
	}
}

int CRPGCharacterData::GetTraderBuyPrice( const RPGTraderDef_t &t, const char *pszClass ) const
{
	const RPGItemDef_t *pItem = RPGData().FindItem( pszClass );
	if ( !pItem || pItem->value <= 0 )
		return -1;
	const float flR = RPGFormulas::FactionPriceBonus( GetFactionTier( t.faction.Get() ) );
	const float flHaggle = 1.0f - 0.01f * (float)GetTraderHaggle( t );
	const float m = Max( 1.0f, t.buyMult * RPGFormulas::BuyPriceMult( GetSkill( "barter" ), flR ) * flHaggle );
	return Max( 1, (int)ceilf( (float)pItem->value * m - 0.001f ) );
}

int CRPGCharacterData::GetTraderSellPrice( const RPGTraderDef_t &t, const char *pszClass ) const
{
	const RPGItemDef_t *pItem = RPGData().FindItem( pszClass );
	if ( !pItem || pItem->value <= 0 )
		return -1;
	if ( pItem->contraband > 0 && !t.acceptsContraband )
		return -1;
	const float flR = RPGFormulas::FactionPriceBonus( GetFactionTier( t.faction.Get() ) );
	const float flHaggle = 1.0f + 0.01f * (float)GetTraderHaggle( t );
	const float m = Min( 0.9f, t.sellMult * RPGFormulas::SellPriceMult( GetSkill( "barter" ), flR ) * flHaggle );
	return Max( 1, (int)floorf( (float)pItem->value * m + 0.001f ) );
}

// ---------------------------------------------------------------------------
// Stat-поля по имени
// ---------------------------------------------------------------------------
bool CRPGCharacterData::GetStat( const char *pszStat, int &iValue ) const
{
	if ( !pszStat )
		return false;
	if ( !Q_stricmp( pszStat, "level" ) ) iValue = m_iLevel;
	else if ( !Q_stricmp( pszStat, "xp" ) ) iValue = m_iXP;
	else if ( !Q_stricmp( pszStat, "skillpoints" ) ) iValue = m_iSkillPoints;
	else if ( !Q_stricmp( pszStat, "perkpicks" ) ) iValue = m_iPerkPicks;
	else if ( !Q_stricmp( pszStat, "credits" ) ) iValue = m_iCredits;
	else if ( !Q_stricmp( pszStat, "civil" ) ) iValue = m_iCivil;
	else if ( !Q_stricmp( pszStat, "humanity" ) ) iValue = m_iHumanity;
	else if ( !Q_stricmp( pszStat, "resonance" ) ) iValue = m_iResonance;
	else if ( !Q_stricmp( pszStat, "influence" ) ) iValue = m_iInfluence;
	else if ( !Q_stricmp( pszStat, "chargen" ) ) iValue = m_bChargenDone ? 1 : 0;
	else if ( !Q_stricmp( pszStat, "suspicion" ) ) iValue = (int)( m_flSuspicion * 100.0f + 0.5f );
	else if ( !Q_stricmp( pszStat, "zone" ) ) iValue = m_iZone;
	else if ( !Q_stricmp( pszStat, "alarm" ) ) iValue = m_bAlarm ? 1 : 0;
	else return false;
	return true;
}

bool CRPGCharacterData::SetStatRaw( const char *pszStat, int iValue )
{
	if ( !pszStat )
		return false;
	if ( !Q_stricmp( pszStat, "level" ) ) m_iLevel = clamp( iValue, 1, RPG_MAX_LEVEL );
	else if ( !Q_stricmp( pszStat, "xp" ) ) m_iXP = Max( 0, iValue );
	else if ( !Q_stricmp( pszStat, "skillpoints" ) ) m_iSkillPoints = Max( 0, iValue );
	else if ( !Q_stricmp( pszStat, "perkpicks" ) ) m_iPerkPicks = Max( 0, iValue );
	else if ( !Q_stricmp( pszStat, "credits" ) ) m_iCredits = Max( 0, iValue );
	else if ( !Q_stricmp( pszStat, "civil" ) ) m_iCivil = clamp( iValue, -100, 100 );
	else if ( !Q_stricmp( pszStat, "humanity" ) ) m_iHumanity = clamp( iValue, 0, 100 );
	else if ( !Q_stricmp( pszStat, "resonance" ) ) m_iResonance = clamp( iValue, 0, 100 );
	else if ( !Q_stricmp( pszStat, "influence" ) ) m_iInfluence = clamp( iValue, 0, 100 );
	else if ( !Q_stricmp( pszStat, "chargen" ) ) m_bChargenDone = iValue != 0;
	else if ( !Q_stricmp( pszStat, "suspicion" ) ) m_flSuspicion = clamp( (float)iValue / 100.0f, 0.0f, 100.0f );
	else if ( !Q_stricmp( pszStat, "zone" ) ) m_iZone = clamp( iValue, 0, 2 );
	else if ( !Q_stricmp( pszStat, "alarm" ) ) m_bAlarm = iValue != 0;
	else return false;
	return true;
}

// ---------------------------------------------------------------------------
// Записи
// ---------------------------------------------------------------------------
void CRPGCharacterData::BuildRecords( CUtlVector<RPGRecord_t> &out ) const
{
	const CRPGData &data = RPGData();

	out.AddToTail( RPGRecord_t( RPG_REC_NAME, "", 0, m_Name.Get() ) );
	for ( int i = 0; g_pszRPGStatNames[i]; ++i )
	{
		int v = 0;
		GetStat( g_pszRPGStatNames[i], v );
		out.AddToTail( RPGRecord_t( RPG_REC_STAT, g_pszRPGStatNames[i], v ) );
	}
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
		out.AddToTail( RPGRecord_t( RPG_REC_ATTR, g_pszRPGAttrIds[i], m_Attr[i] ) );
	for ( int i = 0; i < data.GetSkillCount() && i < RPG_MAX_SKILLS; ++i )
	{
		if ( m_SkillInvested[i] )
			out.AddToTail( RPGRecord_t( RPG_REC_SKILL, data.GetSkill( i ).id.Get(), m_SkillInvested[i] ) );
		if ( m_SkillTagged[i] )
			out.AddToTail( RPGRecord_t( RPG_REC_TAG, data.GetSkill( i ).id.Get(), 1 ) );
	}
	for ( int i = 0; i < m_Traits.Count(); ++i )
		out.AddToTail( RPGRecord_t( RPG_REC_TRAIT, m_Traits[i].Get(), 1 ) );
	for ( int i = m_Perks.First(); i != m_Perks.InvalidIndex(); i = m_Perks.Next( i ) )
		out.AddToTail( RPGRecord_t( RPG_REC_PERK, m_Perks.GetElementName( i ), m_Perks[i] ) );
	for ( int i = m_Factions.First(); i != m_Factions.InvalidIndex(); i = m_Factions.Next( i ) )
		out.AddToTail( RPGRecord_t( RPG_REC_FACTION, m_Factions.GetElementName( i ), m_Factions[i] ) );
	for ( int i = m_Flags.First(); i != m_Flags.InvalidIndex(); i = m_Flags.Next( i ) )
	{
		const RPGFlagValue_t &f = m_Flags[i];
		if ( f.IsString() )
			out.AddToTail( RPGRecord_t( RPG_REC_FLAG_STR, m_Flags.GetElementName( i ), 0, f.sValue.Get() ) );
		else
			out.AddToTail( RPGRecord_t( RPG_REC_FLAG_INT, m_Flags.GetElementName( i ), f.iValue ) );
	}
	for ( int i = m_Dispositions.First(); i != m_Dispositions.InvalidIndex(); i = m_Dispositions.Next( i ) )
		out.AddToTail( RPGRecord_t( RPG_REC_DISPOSITION, m_Dispositions.GetElementName( i ), m_Dispositions[i] ) );
	for ( int i = 0; i < m_Notes.Count(); ++i )
		out.AddToTail( RPGRecord_t( RPG_REC_NOTE, m_Notes[i].Get(), 1 ) );
	for ( int i = 0; i < m_Locations.Count(); ++i )
		out.AddToTail( RPGRecord_t( RPG_REC_LOCATION, m_Locations[i].Get(), 1 ) );
	out.AddToTail( RPGRecord_t( RPG_REC_ARMOR, "", 0, m_ArmorClass.Get() ) );
}

static void ListSet( CUtlVector<CUtlString> &list, const char *pszId, bool bOn )
{
	for ( int i = 0; i < list.Count(); ++i )
	{
		if ( !Q_stricmp( list[i].Get(), pszId ) )
		{
			if ( !bOn )
				list.Remove( i );
			return;
		}
	}
	if ( bOn )
		list.AddToTail( CUtlString( pszId ) );
}

bool CRPGCharacterData::ApplyRecord( const RPGRecord_t &rec )
{
	const char *pszKey = rec.key.Get();
	switch ( rec.type )
	{
	case RPG_REC_STAT:
		return SetStatRaw( pszKey, rec.iValue );
	case RPG_REC_NAME:
		m_Name = rec.sValue;
		return true;
	case RPG_REC_ATTR:
	{
		int a = RPGFormulas::AttrFromName( pszKey );
		if ( a >= 0 )
			m_Attr[a] = clamp( rec.iValue, RPG_ATTR_MIN, RPG_ATTR_MAX_EFFECTIVE );
		return true;
	}
	case RPG_REC_SKILL:
	case RPG_REC_TAG:
	{
		int s = RPGData().FindSkill( pszKey );
		if ( s >= 0 && s < RPG_MAX_SKILLS )
		{
			if ( rec.type == RPG_REC_SKILL )
				m_SkillInvested[s] = Max( 0, rec.iValue );
			else
				m_SkillTagged[s] = rec.iValue != 0;
		}
		return true;
	}
	case RPG_REC_TRAIT:
		ListSet( m_Traits, pszKey, rec.iValue != 0 );
		return true;
	case RPG_REC_PERK:
		if ( rec.iValue > 0 )
		{
			int i = m_Perks.Find( pszKey );
			if ( i == m_Perks.InvalidIndex() )
				m_Perks.Insert( pszKey, rec.iValue );
			else
				m_Perks[i] = rec.iValue;
		}
		else
			m_Perks.Remove( pszKey );
		return true;
	case RPG_REC_FACTION:
	{
		int i = m_Factions.Find( pszKey );
		int v = clamp( rec.iValue, -100, 100 );
		if ( i == m_Factions.InvalidIndex() )
			m_Factions.Insert( pszKey, v );
		else
			m_Factions[i] = v;
		return true;
	}
	case RPG_REC_FLAG_INT:
	case RPG_REC_FLAG_STR:
	{
		RPGFlagValue_t f;
		if ( rec.type == RPG_REC_FLAG_STR )
			f.sValue = rec.sValue;
		else
			f.iValue = rec.iValue;
		int i = m_Flags.Find( pszKey );
		if ( i == m_Flags.InvalidIndex() )
			m_Flags.Insert( pszKey, f );
		else
			m_Flags[i] = f;
		return true;
	}
	case RPG_REC_FLAG_CLEAR:
		m_Flags.Remove( pszKey );
		return true;
	case RPG_REC_DISPOSITION:
	{
		int i = m_Dispositions.Find( pszKey );
		int v = clamp( rec.iValue, -100, 100 );
		if ( i == m_Dispositions.InvalidIndex() )
			m_Dispositions.Insert( pszKey, v );
		else
			m_Dispositions[i] = v;
		return true;
	}
	case RPG_REC_NOTE:
		ListSet( m_Notes, pszKey, rec.iValue != 0 );
		return true;
	case RPG_REC_LOCATION:
		ListSet( m_Locations, pszKey, rec.iValue != 0 );
		return true;
	case RPG_REC_ARMOR:
		m_ArmorClass = rec.sValue;
		return true;
	}
	return false;
}

void CRPGCharacterData::Print( const char *pszPrefix ) const
{
	const CRPGData &data = RPGData();
	Msg( "%s name \"%s\"  level %d  xp %d/%d  skill points %d  perk picks %d  chargen %s\n", pszPrefix, m_Name.Get(),
		m_iLevel, m_iXP, GetXPForNextLevel(), m_iSkillPoints, m_iPerkPicks, m_bChargenDone ? "done" : "no" );
	Msg( "%s HP max %d  carry %.1f kg  armor %d (%s)  credits %d\n", pszPrefix, GetMaxHealth(), GetCarryWeight(), GetArmor(),
		m_ArmorClass.Length() ? m_ArmorClass.Get() : "robe", m_iCredits );
	Msg( "%s civil %d (%s%s)  suspicion %.1f (%s)  zone %d  alarm %d  humanity %d (%s)  resonance %d  influence %d  act %d\n", pszPrefix,
		m_iCivil, RPGFormulas::CivilTierKey( GetCivilTier() ), HasTempPass() ? ", temppass" : "", m_flSuspicion,
		RPGFormulas::SuspicionTierKey( GetSuspicionTier() ), m_iZone, m_bAlarm ? 1 : 0, m_iHumanity,
		RPGFormulas::HumanityTierKey( GetHumanityTier() ), m_iResonance, m_iInfluence, GetAct() );
	CUtlString line;
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
		line.Append( CFmtStr( "%s %d(%d)  ", g_pszRPGAttrAbbr[i], GetAttr( i ), m_Attr[i] ) );
	Msg( "%s attributes: %s\n", pszPrefix, line.Get() );
	line = "";
	for ( int i = 0; i < data.GetSkillCount() && i < RPG_MAX_SKILLS; ++i )
	{
		line.Append( CFmtStr( "%s%s %d", i ? ", " : "", data.GetSkill( i ).id.Get(), GetSkill( i ) ) );
		if ( m_SkillTagged[i] )
			line.Append( "*" );
		if ( m_SkillInvested[i] )
			line.Append( CFmtStr( "(+%d)", m_SkillInvested[i] ) );
	}
	Msg( "%s skills: %s\n", pszPrefix, line.Get() );
	line = "";
	for ( int i = 0; i < m_Traits.Count(); ++i )
		line.Append( CFmtStr( "%s%s", i ? ", " : "", m_Traits[i].Get() ) );
	for ( int i = m_Perks.First(); i != m_Perks.InvalidIndex(); i = m_Perks.Next( i ) )
		line.Append( CFmtStr( "%s%s x%d", line.Length() ? ", " : "", m_Perks.GetElementName( i ), m_Perks[i] ) );
	Msg( "%s traits/perks: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
	line = "";
	for ( int i = m_Factions.First(); i != m_Factions.InvalidIndex(); i = m_Factions.Next( i ) )
		line.Append( CFmtStr( "%s%s %d (%s)", line.Length() ? ", " : "", m_Factions.GetElementName( i ), m_Factions[i],
			RPGFormulas::FactionTierKey( RPGFormulas::FactionTier( m_Factions[i] ) ) ) );
	Msg( "%s factions: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
	line = "";
	for ( int i = m_Flags.First(); i != m_Flags.InvalidIndex(); i = m_Flags.Next( i ) )
	{
		const RPGFlagValue_t &f = m_Flags[i];
		if ( f.IsString() )
			line.Append( CFmtStr( "%s%s=\"%s\"", line.Length() ? ", " : "", m_Flags.GetElementName( i ), f.sValue.Get() ) );
		else
			line.Append( CFmtStr( "%s%s=%d", line.Length() ? ", " : "", m_Flags.GetElementName( i ), f.iValue ) );
	}
	Msg( "%s flags: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
	line = "";
	for ( int i = m_Dispositions.First(); i != m_Dispositions.InvalidIndex(); i = m_Dispositions.Next( i ) )
		line.Append( CFmtStr( "%s%s %d", line.Length() ? ", " : "", m_Dispositions.GetElementName( i ), m_Dispositions[i] ) );
	Msg( "%s dispositions: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
	line = "";
	for ( int i = 0; i < m_Notes.Count(); ++i )
		line.Append( CFmtStr( "%s%s", i ? ", " : "", m_Notes[i].Get() ) );
	Msg( "%s notes: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
	line = "";
	for ( int i = 0; i < m_Locations.Count(); ++i )
		line.Append( CFmtStr( "%s%s", i ? ", " : "", m_Locations[i].Get() ) );
	Msg( "%s locations: %s\n", pszPrefix, line.Length() ? line.Get() : "-" );
}

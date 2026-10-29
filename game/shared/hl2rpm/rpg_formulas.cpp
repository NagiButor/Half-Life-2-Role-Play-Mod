//========= HL2RPM ============//
//
// RPG-система: формулы правил (см. rpg_formulas.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_formulas.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const char *g_pszRPGAttrIds[RPG_ATTR_COUNT] = { "agility", "analytics", "might", "vigilance", "spirit", "authority" };
const char *g_pszRPGAttrAbbr[RPG_ATTR_COUNT] = { "LOV", "ANA", "MIG", "BDI", "DUH", "AVT" };
// кириллические сокращения из дизайн-документа (UTF-8)
static const char *s_pszRPGAttrAbbrRu[RPG_ATTR_COUNT] = { "\xD0\x9B\xD0\x9E\xD0\x92", "\xD0\x90\xD0\x9D\xD0\x90", "\xD0\x9C\xD0\x9E\xD0\xA9", "\xD0\x91\xD0\x94\xD0\x98", "\xD0\x94\xD0\xA3\xD0\xA5", "\xD0\x90\xD0\x92\xD0\xA2" };

const char *g_pszRPGCivilTierKeys[RPG_CIVIL_COUNT] = { "anticitizen", "malicious", "watched", "citizen", "reliable", "loyalist" };
const char *g_pszRPGFactionTierKeys[RPG_FTIER_COUNT] = { "enemy", "dislike", "neutral", "recognized", "ally" };
const char *g_pszRPGHumanityTierKeys[RPG_HTIER_COUNT] = { "empty", "callous", "human", "compassionate", "righteous" };
const char *g_pszRPGSuspicionTierKeys[RPG_STIER_COUNT] = { "calm", "attention", "search", "detain", "alarm" };
const char *g_pszRPGSuitTypeKeys[RPG_SUIT_COUNT] = { "citizen", "worker", "cp", "overwatch", "rebel", "hev" };

int RPG_SuitTypeFromKey( const char *pszKey )
{
	if ( !pszKey || !pszKey[0] )
		return -1;
	for ( int i = 0; i < RPG_SUIT_COUNT; ++i )
	{
		if ( !Q_stricmp( pszKey, g_pszRPGSuitTypeKeys[i] ) )
			return i;
	}
	return -1;
}

namespace RPGFormulas
{

int SkillBase( int primary, int secondary )
{
	return 2 + 2 * primary + ( secondary + 1 ) / 2;
}

int XPForLevel( int level )
{
	if ( level <= 1 )
		return 0;
	return 80 * ( level - 1 ) * ( level + 3 );
}

int LevelForXP( int xp )
{
	int level = 1;
	while ( level < RPG_MAX_LEVEL && xp >= XPForLevel( level + 1 ) )
		++level;
	return level;
}

int SkillPointsPerLevel( int analytics )
{
	return 10 + Max( 0, analytics ) / 2;
}

bool LevelGivesPerk( int level )
{
	return level >= 2 && ( level % 2 ) == 0;
}

int MaxHealth( int spirit, int level, int bonus )
{
	return Max( 1, 50 + 10 * spirit + 3 * ( Max( 1, level ) - 1 ) + bonus );
}

float CarryWeight( int might, float bonus )
{
	return Max( 1.0f, 25.0f + 3.0f * (float)might + bonus );
}

int CheckXPSkill( int threshold, bool bInQuest )
{
	int xp = Max( 0, threshold ) / 2;
	return bInQuest ? xp / 2 : xp;
}

int CheckXPAttr( int value, bool bInQuest )
{
	int xp = 5 * Max( 0, value );
	return bInQuest ? xp / 2 : xp;
}

int BribeCost( int base, int authority, int barter, float flMult )
{
	float f = (float)base * ( 1.3f - 0.06f * (float)authority ) * ( 1.2f - 0.005f * (float)barter ) * flMult;
	int cost = (int)ceilf( f - 0.001f );
	cost = ( ( cost + 4 ) / 5 ) * 5;
	return Max( 0, cost );
}

float BuyPriceMult( int barter, float flFactionBonus )
{
	return Max( 1.0f, 1.6f - 0.006f * (float)barter - flFactionBonus );
}

float SellPriceMult( int barter, float flFactionBonus )
{
	return Min( 0.9f, 0.4f + 0.004f * (float)barter + flFactionBonus );
}

float FactionPriceBonus( int factionTier )
{
	switch ( factionTier )
	{
	case RPG_FTIER_RECOGNIZED:	return 0.1f;
	case RPG_FTIER_ALLY:		return 0.2f;
	case RPG_FTIER_DISLIKE:		return -0.25f;
	default:					return 0.0f;
	}
}

int CivilTier( int civil )
{
	if ( civil <= -75 ) return RPG_CIVIL_ANTICITIZEN;
	if ( civil <= -40 ) return RPG_CIVIL_MALICIOUS;
	if ( civil <= -15 ) return RPG_CIVIL_WATCHED;
	if ( civil <= 19 ) return RPG_CIVIL_CITIZEN;
	if ( civil <= 49 ) return RPG_CIVIL_RELIABLE;
	return RPG_CIVIL_LOYALIST;
}

int CivilTierMin( int tier )
{
	static const int s_Min[RPG_CIVIL_COUNT] = { -100, -74, -39, -14, 20, 50 };
	return s_Min[clamp( tier, 0, RPG_CIVIL_COUNT - 1 )];
}

int CivilTierMax( int tier )
{
	static const int s_Max[RPG_CIVIL_COUNT] = { -75, -40, -15, 19, 49, 100 };
	return s_Max[clamp( tier, 0, RPG_CIVIL_COUNT - 1 )];
}

int FactionTier( int value )
{
	if ( value <= -60 ) return RPG_FTIER_ENEMY;
	if ( value <= -25 ) return RPG_FTIER_DISLIKE;
	if ( value <= 24 ) return RPG_FTIER_NEUTRAL;
	if ( value <= 59 ) return RPG_FTIER_RECOGNIZED;
	return RPG_FTIER_ALLY;
}

int FactionTierMin( int tier )
{
	static const int s_Min[RPG_FTIER_COUNT] = { -100, -59, -24, 25, 60 };
	return s_Min[clamp( tier, 0, RPG_FTIER_COUNT - 1 )];
}

int FactionTierMax( int tier )
{
	static const int s_Max[RPG_FTIER_COUNT] = { -60, -25, 24, 59, 100 };
	return s_Max[clamp( tier, 0, RPG_FTIER_COUNT - 1 )];
}

int HumanityTier( int humanity )
{
	if ( humanity <= 19 ) return RPG_HTIER_EMPTY;
	if ( humanity <= 39 ) return RPG_HTIER_CALLOUS;
	if ( humanity <= 60 ) return RPG_HTIER_HUMAN;
	if ( humanity <= 80 ) return RPG_HTIER_COMPASSIONATE;
	return RPG_HTIER_RIGHTEOUS;
}

int SuspicionTier( float suspicion )
{
	if ( suspicion >= 100.0f ) return RPG_STIER_ALARM;
	if ( suspicion >= 75.0f ) return RPG_STIER_DETAIN;
	if ( suspicion >= 50.0f ) return RPG_STIER_SEARCH;
	if ( suspicion >= 25.0f ) return RPG_STIER_ATTENTION;
	return RPG_STIER_CALM;
}

float SuspicionDecayCivilMult( int civilTier )
{
	switch ( civilTier )
	{
	case RPG_CIVIL_RELIABLE:	return 1.5f;
	case RPG_CIVIL_LOYALIST:	return 2.0f;
	case RPG_CIVIL_WATCHED:
	case RPG_CIVIL_MALICIOUS:
	case RPG_CIVIL_ANTICITIZEN:	return 0.5f;
	default:					return 1.0f;
	}
}

static const char *KeyFromTable( const char **ppTable, int count, int index )
{
	if ( index < 0 || index >= count )
		return "";
	return ppTable[index];
}

static int TableFromKey( const char **ppTable, int count, const char *pszKey )
{
	if ( !pszKey || !pszKey[0] )
		return -1;
	for ( int i = 0; i < count; ++i )
	{
		if ( !Q_stricmp( ppTable[i], pszKey ) )
			return i;
	}
	return -1;
}

const char *CivilTierKey( int tier ) { return KeyFromTable( g_pszRPGCivilTierKeys, RPG_CIVIL_COUNT, tier ); }
const char *FactionTierKey( int tier ) { return KeyFromTable( g_pszRPGFactionTierKeys, RPG_FTIER_COUNT, tier ); }
const char *HumanityTierKey( int tier ) { return KeyFromTable( g_pszRPGHumanityTierKeys, RPG_HTIER_COUNT, tier ); }
const char *SuspicionTierKey( int tier ) { return KeyFromTable( g_pszRPGSuspicionTierKeys, RPG_STIER_COUNT, tier ); }
int CivilTierFromKey( const char *pszKey ) { return TableFromKey( g_pszRPGCivilTierKeys, RPG_CIVIL_COUNT, pszKey ); }
int FactionTierFromKey( const char *pszKey ) { return TableFromKey( g_pszRPGFactionTierKeys, RPG_FTIER_COUNT, pszKey ); }
int HumanityTierFromKey( const char *pszKey ) { return TableFromKey( g_pszRPGHumanityTierKeys, RPG_HTIER_COUNT, pszKey ); }
int SuspicionTierFromKey( const char *pszKey ) { return TableFromKey( g_pszRPGSuspicionTierKeys, RPG_STIER_COUNT, pszKey ); }

int AttrFromName( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return -1;
	for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
	{
		if ( !Q_stricmp( pszName, g_pszRPGAttrIds[i] ) || !Q_stricmp( pszName, g_pszRPGAttrAbbr[i] ) || !Q_strcmp( pszName, s_pszRPGAttrAbbrRu[i] ) )
			return i;
	}
	return -1;
}

} // namespace RPGFormulas

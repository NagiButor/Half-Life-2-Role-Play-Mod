//========= HL2RPM ============//
//
// RPG-система: разбор и проверка условий, разбор эффектов и проверок (см. rpg_conditions.h).
// Применение эффектов - только на сервере (server/hl2rpm/rpg_effects.cpp).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_conditions.h"
#include "hl2rpm/rpg_character_data.h"
#include "hl2rpm/rpg_formulas.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static IRPGWorldQuery *s_pWorldQuery = NULL;

void RPG_SetWorldQuery( IRPGWorldQuery *pQuery )
{
	s_pWorldQuery = pQuery;
}

IRPGWorldQuery *RPG_GetWorldQuery()
{
	return s_pWorldQuery;
}

// ---------------------------------------------------------------------------
// Разбор строк
// ---------------------------------------------------------------------------
static const char *SkipSpaces( const char *p )
{
	while ( p && ( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) )
		++p;
	return p;
}

static CUtlString Trimmed( const char *pszBegin, const char *pszEnd = NULL )
{
	if ( !pszBegin )
		return CUtlString();
	if ( !pszEnd )
		pszEnd = pszBegin + Q_strlen( pszBegin );
	while ( pszBegin < pszEnd && ( *pszBegin == ' ' || *pszBegin == '\t' || *pszBegin == '\r' || *pszBegin == '\n' ) )
		++pszBegin;
	while ( pszEnd > pszBegin && ( pszEnd[-1] == ' ' || pszEnd[-1] == '\t' || pszEnd[-1] == '\r' || pszEnd[-1] == '\n' ) )
		--pszEnd;
	CUtlString out;
	out.SetDirect( pszBegin, (int)( pszEnd - pszBegin ) );
	return out;
}

// разбить по разделителю (вне кавычек), элементы обрезаны, пустые выкинуты
static void SplitList( const char *pszText, char cSep, CUtlVector<CUtlString> &out, bool bKeepEmpty = false )
{
	if ( !pszText )
		return;
	const char *pStart = pszText;
	for ( const char *p = pszText; ; ++p )
	{
		if ( *p == cSep || *p == '\0' )
		{
			CUtlString item = Trimmed( pStart, p );
			if ( bKeepEmpty || !item.IsEmpty() )
				out.AddToTail( item );
			if ( *p == '\0' )
				break;
			pStart = p + 1;
		}
	}
}

static bool IsOpChar( char c )
{
	return c == '>' || c == '<' || c == '=' || c == '!';
}

// идентификатор до пробела/оператора/двоеточия
static const char *SplitIdent( const char *p, CUtlString &ident )
{
	p = SkipSpaces( p );
	const char *pStart = p;
	while ( *p && *p != ' ' && *p != '\t' && *p != ':' && !IsOpChar( *p ) )
		++p;
	ident.SetDirect( pStart, (int)( p - pStart ) );
	return p;
}

static bool ParseOp( const char *&p, int &op )
{
	p = SkipSpaces( p );
	if ( p[0] == '>' && p[1] == '=' ) { op = RPG_OP_GE; p += 2; return true; }
	if ( p[0] == '<' && p[1] == '=' ) { op = RPG_OP_LE; p += 2; return true; }
	if ( p[0] == '!' && p[1] == '=' ) { op = RPG_OP_NE; p += 2; return true; }
	if ( p[0] == '=' && p[1] == '=' ) { op = RPG_OP_EQ; p += 2; return true; }
	if ( p[0] == '>' ) { op = RPG_OP_GT; p += 1; return true; }
	if ( p[0] == '<' ) { op = RPG_OP_LT; p += 1; return true; }
	if ( p[0] == '=' ) { op = RPG_OP_EQ; p += 1; return true; }
	return false;
}

static bool IsInteger( const char *p )
{
	p = SkipSpaces( p );
	if ( *p == '+' || *p == '-' )
		++p;
	if ( !*p )
		return false;
	while ( *p )
	{
		if ( *p < '0' || *p > '9' )
			return ( *SkipSpaces( p ) == '\0' );
		++p;
	}
	return true;
}

static int ToInt( const char *p )
{
	p = SkipSpaces( p );
	if ( *p == '+' )
		++p;
	return atoi( p );
}

static bool Compare( int cur, int op, int val )
{
	switch ( op )
	{
	case RPG_OP_GE: return cur >= val;
	case RPG_OP_LE: return cur <= val;
	case RPG_OP_GT: return cur > val;
	case RPG_OP_LT: return cur < val;
	case RPG_OP_EQ: return cur == val;
	case RPG_OP_NE: return cur != val;
	}
	return false;
}

static const char *OpString( int op )
{
	static const char *s_Ops[] = { ">=", "<=", ">", "<", "=", "!=" };
	return ( op >= 0 && op < 6 ) ? s_Ops[op] : "?";
}

// уровень без явного оператора: выше «середины» - «не ниже», ниже - «не выше», середина - ровно
static bool CompareTier( int curTier, const RPGCondition_t &c, int midTier )
{
	if ( c.bHasOp )
		return Compare( curTier, c.op, c.tier );
	if ( c.tier > midTier )
		return curTier >= c.tier;
	if ( c.tier < midTier )
		return curTier <= c.tier;
	return curTier == c.tier;
}

// ---------------------------------------------------------------------------
// Имена типов условий
// ---------------------------------------------------------------------------
static const char *s_pszCondTypeNames[RPG_COND_COUNT] =
{
	"", "attr", "skill", "faction", "civil", "item", "perk", "trait", "flag", "noflag", "humanity",
	"resonance", "influence", "suspicion", "credits", "level", "act", "time", "disposition",
	"quest_state", "quest_stage", "note", "location", "chargen", "zone", "suit", "npc_var", "law"
};

const char *RPG_ConditionTypeName( int type )
{
	return ( type > 0 && type < RPG_COND_COUNT ) ? s_pszCondTypeNames[type] : "";
}

int RPG_ConditionTypeFromName( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return RPG_COND_INVALID;
	for ( int i = 1; i < RPG_COND_COUNT; ++i )
	{
		if ( !Q_stricmp( pszName, s_pszCondTypeNames[i] ) )
			return i;
	}
	return RPG_COND_INVALID;
}

static int QuestStateFromName( const char *psz )
{
	if ( !Q_stricmp( psz, "inactive" ) || !Q_stricmp( psz, "none" ) ) return 0;
	if ( !Q_stricmp( psz, "active" ) ) return 1;
	if ( !Q_stricmp( psz, "completed" ) || !Q_stricmp( psz, "done" ) || !Q_stricmp( psz, "complete" ) ) return 2;
	if ( !Q_stricmp( psz, "failed" ) || !Q_stricmp( psz, "fail" ) ) return 3;
	return -1;
}

// ---------------------------------------------------------------------------
// RPGCondition_t::Parse
// ---------------------------------------------------------------------------
// "op N" / "N" / ":N" (без оператора - >=)
static bool ParseOpNumber( const char *p, RPGCondition_t &c, int iDefaultOp = RPG_OP_GE )
{
	p = SkipSpaces( p );
	if ( *p == ':' )
		p = SkipSpaces( p + 1 );
	c.bHasOp = ParseOp( p, c.op );
	if ( !c.bHasOp )
		c.op = iDefaultOp;
	if ( !IsInteger( p ) )
		return false;
	c.value = ToInt( p );
	return true;
}

// "op N" или "op <ключ уровня>"
static bool ParseOpNumberOrTier( const char *p, RPGCondition_t &c, int ( *pfnTierFromKey )( const char * ) )
{
	p = SkipSpaces( p );
	if ( *p == ':' )
		p = SkipSpaces( p + 1 );
	c.bHasOp = ParseOp( p, c.op );
	if ( !c.bHasOp )
		c.op = RPG_OP_GE;
	p = SkipSpaces( p );
	if ( IsInteger( p ) )
	{
		c.value = ToInt( p );
		c.tier = -1;
		return true;
	}
	if ( pfnTierFromKey )
	{
		CUtlString key = Trimmed( p );
		int t = pfnTierFromKey( key.Get() );
		if ( t >= 0 )
		{
			c.tier = t;
			return true;
		}
	}
	return false;
}

bool RPGCondition_t::Parse( int iType, const char *pszBody, CUtlString *pError )
{
	RPGData().EnsureLoaded();
	type = iType;
	op = RPG_OP_GE;
	id = "";
	str = "";
	value = value2 = 0;
	tier = -1;
	bString = false;
	bHasOp = false;

	const char *p = SkipSpaces( pszBody ? pszBody : "" );
	if ( *p == '!' && p[1] != '=' )
	{
		bNegate = !bNegate;
		p = SkipSpaces( p + 1 );
	}

	bool bOk = true;
	CUtlString ident;
	const char *pRest = NULL;

	switch ( type )
	{
	case RPG_COND_ATTR:
	{
		pRest = SplitIdent( p, ident );
		int a = RPGData().FindAttr( ident.Get() );
		if ( a < 0 )
		{
			bOk = false;
			break;
		}
		id = g_pszRPGAttrIds[a];
		bOk = ParseOpNumber( pRest, *this );
		break;
	}
	case RPG_COND_SKILL:
	{
		pRest = SplitIdent( p, ident );
		if ( RPGData().FindSkill( ident.Get() ) < 0 )
		{
			bOk = false;
			break;
		}
		id = ident;
		bOk = ParseOpNumber( pRest, *this );
		break;
	}
	case RPG_COND_FACTION:
	{
		pRest = SplitIdent( p, ident );
		id = ident;
		if ( ident.IsEmpty() )
		{
			bOk = false;
			break;
		}
		bOk = ParseOpNumberOrTier( pRest, *this, RPGFormulas::FactionTierFromKey );
		break;
	}
	case RPG_COND_CIVIL:
	{
		const char *q = p;
		int tmpOp;
		bool bOp = ParseOp( q, tmpOp );
		CUtlString key = Trimmed( q );
		if ( !Q_stricmp( key.Get(), RPG_CIVIL_TEMPPASS_KEY ) )
		{
			id = RPG_CIVIL_TEMPPASS_KEY;
			if ( bOp && tmpOp == RPG_OP_NE )
				bNegate = !bNegate;
			break;
		}
		bOk = ParseOpNumberOrTier( p, *this, RPGFormulas::CivilTierFromKey );
		break;
	}
	case RPG_COND_HUMANITY:
		bOk = ParseOpNumberOrTier( p, *this, RPGFormulas::HumanityTierFromKey );
		break;
	case RPG_COND_SUSPICION:
		bOk = ParseOpNumberOrTier( p, *this, RPGFormulas::SuspicionTierFromKey );
		break;
	case RPG_COND_RESONANCE:
	case RPG_COND_INFLUENCE:
	case RPG_COND_CREDITS:
	case RPG_COND_LEVEL:
	case RPG_COND_ZONE:
		bOk = ParseOpNumber( p, *this );
		break;
	case RPG_COND_CHARGEN:
		if ( !*p )
			value = 1;
		else
			bOk = ParseOpNumber( p, *this, RPG_OP_EQ );
		if ( !bHasOp )
			op = RPG_OP_EQ;
		break;
	case RPG_COND_ITEM:
	{
		pRest = SplitIdent( p, ident );
		id = ident;
		value = 1;
		if ( ident.IsEmpty() )
			bOk = false;
		else if ( *SkipSpaces( pRest ) )
			bOk = ParseOpNumber( pRest, *this );
		break;
	}
	case RPG_COND_PERK:
	case RPG_COND_TRAIT:
	{
		pRest = SplitIdent( p, ident );
		id = ident;
		value = 1;
		if ( ident.IsEmpty() )
			bOk = false;
		else if ( type == RPG_COND_PERK && *SkipSpaces( pRest ) )
			bOk = ParseOpNumber( pRest, *this );
		if ( bOk && type == RPG_COND_PERK && !RPGData().FindPerk( id.Get() ) )
			DevWarning( "[rpg] condition: unknown perk '%s'\n", id.Get() );
		if ( bOk && type == RPG_COND_TRAIT && !RPGData().FindTrait( id.Get() ) )
			DevWarning( "[rpg] condition: unknown trait '%s'\n", id.Get() );
		break;
	}
	case RPG_COND_FLAG:
	case RPG_COND_NOFLAG:
	{
		pRest = SplitIdent( p, ident );
		id = ident;
		if ( ident.IsEmpty() )
		{
			bOk = false;
			break;
		}
		const char *q = SkipSpaces( pRest );
		if ( !*q )
		{
			tier = -2;	// просто «установлен»
			break;
		}
		if ( *q == ':' )
		{
			// name:value = name=value
			op = RPG_OP_EQ;
			bHasOp = true;
			++q;
		}
		else if ( !( bHasOp = ParseOp( q, op ) ) )
		{
			bOk = false;
			break;
		}
		CUtlString v = Trimmed( q );
		if ( IsInteger( v.Get() ) )
		{
			value = ToInt( v.Get() );
			str = v;
		}
		else
		{
			bString = true;
			str = v;
			if ( op != RPG_OP_EQ && op != RPG_OP_NE )
				bOk = false;
		}
		break;
	}
	case RPG_COND_ACT:
	{
		const char *pDash = Q_strstr( p, "-" );
		if ( pDash && pDash != p )
		{
			value = ToInt( p );
			value2 = ToInt( pDash + 1 );
			tier = -3;	// диапазон
		}
		else
		{
			bOk = ParseOpNumber( p, *this, RPG_OP_EQ );
		}
		break;
	}
	case RPG_COND_TIME:
	{
		CUtlString key = Trimmed( p );
		if ( !Q_stricmp( key.Get(), "day" ) || !Q_stricmp( key.Get(), "night" ) || !Q_stricmp( key.Get(), "curfew" ) )
		{
			id = key;
			break;
		}
		const char *pDash = Q_strstr( p, "-" );
		if ( !pDash || !IsInteger( CUtlString( Trimmed( p, pDash ) ).Get() ) || !IsInteger( pDash + 1 ) )
		{
			bOk = false;
			break;
		}
		value = ToInt( p );
		value2 = ToInt( pDash + 1 );
		id = "range";
		break;
	}
	case RPG_COND_DISPOSITION:
	{
		const char *q = p;
		int tmpOp;
		if ( ParseOp( q, tmpOp ) || IsInteger( p ) )
		{
			bOk = ParseOpNumber( p, *this );	// собеседник
		}
		else
		{
			pRest = SplitIdent( p, ident );
			id = ident;
			bOk = ParseOpNumber( pRest, *this );
		}
		break;
	}
	case RPG_COND_QUEST_STATE:
	case RPG_COND_QUEST_STAGE:
	{
		pRest = SplitIdent( p, ident );
		id = ident;
		const char *q = SkipSpaces( pRest );
		if ( *q == ':' )
			q = SkipSpaces( q + 1 );
		if ( ident.IsEmpty() )
		{
			bOk = false;
			break;
		}
		if ( type == RPG_COND_QUEST_STATE )
		{
			CUtlString st = Trimmed( q );
			value = QuestStateFromName( st.Get() );
			bOk = value >= 0;
		}
		else
		{
			bOk = IsInteger( q );
			value = ToInt( q );
		}
		break;
	}
	case RPG_COND_NOTE:
	case RPG_COND_LOCATION:
	case RPG_COND_SUIT:
	case RPG_COND_LAW:
		id = Trimmed( p );
		bOk = !id.IsEmpty();
		if ( bOk && type == RPG_COND_SUIT && RPG_SuitTypeFromKey( id.Get() ) < 0 )
			bOk = false;
		break;
	case RPG_COND_NPCVAR:
	{
		// "i/n": вариант i (0..n-1) из n
		const char *pSlash = Q_strstr( p, "/" );
		bOk = pSlash != NULL;
		if ( bOk )
		{
			value = atoi( p );
			value2 = atoi( pSlash + 1 );
			bOk = value2 > 0 && value >= 0 && value < value2;
		}
		break;
	}
	default:
		bOk = false;
		break;
	}

	if ( !bOk )
	{
		if ( pError )
			pError->Format( "bad %s condition '%s'", RPG_ConditionTypeName( iType ), pszBody ? pszBody : "" );
		type = RPG_COND_INVALID;
	}
	return bOk;
}

// ---------------------------------------------------------------------------
// RPGCondition_t::Test
// ---------------------------------------------------------------------------
static bool IsCurfewHour( float h, int act )
{
	int start;
	switch ( act )
	{
	case 1: start = 22; break;
	case 2: start = 20; break;
	case 3: start = 18; break;
	default: return false;	// акт IV - комендантского часа нет
	}
	return h >= (float)start || h < 6.0f;
}

static bool InHourRange( float h, int a, int b )
{
	if ( a == b )
		return true;
	if ( a < b )
		return h >= (float)a && h < (float)b;
	return h >= (float)a || h < (float)b;	// через полночь (22-6)
}

int RPGCondition_t::GetCurrentValue( const RPGEvalContext_t &ctx ) const
{
	const CRPGCharacterData *c = ctx.pChar;
	if ( !c )
		return 0;
	switch ( type )
	{
	case RPG_COND_ATTR: return c->GetAttr( RPGFormulas::AttrFromName( id.Get() ) );
	case RPG_COND_SKILL: return c->GetSkill( id.Get(), true );
	case RPG_COND_FACTION: return c->GetFaction( id.Get() );
	case RPG_COND_CIVIL: return c->m_iCivil;
	case RPG_COND_HUMANITY: return c->m_iHumanity;
	case RPG_COND_RESONANCE: return c->m_iResonance;
	case RPG_COND_INFLUENCE: return c->m_iInfluence;
	case RPG_COND_SUSPICION: return (int)c->m_flSuspicion;
	case RPG_COND_CREDITS: return c->m_iCredits;
	case RPG_COND_LEVEL: return c->m_iLevel;
	case RPG_COND_ZONE: return c->m_iZone;
	case RPG_COND_ACT: return c->GetAct();
	case RPG_COND_PERK: return c->GetPerkRank( id.Get() );
	case RPG_COND_FLAG:
	case RPG_COND_NOFLAG: return c->GetFlagInt( id.Get() );
	case RPG_COND_ITEM:
		if ( s_pWorldQuery )
			return Max( 0, s_pWorldQuery->CountItem( ctx.pPlayer, id.Get() ) );
		return 0;
	case RPG_COND_DISPOSITION:
	{
		const char *pszNPC = id.Get();
		int iStart = 0;
		if ( id.IsEmpty() && s_pWorldQuery && ctx.pNPC )
		{
			pszNPC = s_pWorldQuery->GetNPCName( ctx.pNPC );
			iStart = s_pWorldQuery->GetNPCStartDisposition( ctx.pNPC );
		}
		return c->GetDisposition( pszNPC, iStart );
	}
	}
	return 0;
}

bool RPGCondition_t::Test( const RPGEvalContext_t &ctx, bool *pUnknown ) const
{
	const CRPGCharacterData *c = ctx.pChar;
	bool bUnknown = false;
	bool bResult = false;

	if ( !c || type == RPG_COND_INVALID )
	{
		if ( pUnknown )
			*pUnknown = ( c == NULL );
		return c == NULL;	// нет персонажа - не блокировать; ошибка разбора - не выполнено
	}

	switch ( type )
	{
	case RPG_COND_ATTR:
	case RPG_COND_SKILL:
	case RPG_COND_RESONANCE:
	case RPG_COND_INFLUENCE:
	case RPG_COND_CREDITS:
	case RPG_COND_LEVEL:
	case RPG_COND_ZONE:
		bResult = Compare( GetCurrentValue( ctx ), op, value );
		break;
	case RPG_COND_CHARGEN:
		bResult = Compare( c->m_bChargenDone ? 1 : 0, op, value );
		break;
	case RPG_COND_FACTION:
		if ( tier >= 0 )
			bResult = CompareTier( c->GetFactionTier( id.Get() ), *this, RPG_FTIER_NEUTRAL );
		else
			bResult = Compare( c->GetFaction( id.Get() ), op, value );
		break;
	case RPG_COND_CIVIL:
		if ( !Q_stricmp( id.Get(), RPG_CIVIL_TEMPPASS_KEY ) )
			bResult = c->HasTempPass();
		else if ( tier >= 0 )
			bResult = CompareTier( c->GetCivilTier(), *this, RPG_CIVIL_CITIZEN );
		else
			bResult = Compare( c->m_iCivil, op, value );
		break;
	case RPG_COND_HUMANITY:
		if ( tier >= 0 )
			bResult = CompareTier( c->GetHumanityTier(), *this, RPG_HTIER_HUMAN );
		else
			bResult = Compare( c->m_iHumanity, op, value );
		break;
	case RPG_COND_SUSPICION:
		if ( tier >= 0 )
			bResult = CompareTier( c->GetSuspicionTier(), *this, RPG_STIER_CALM );
		else
			bResult = Compare( (int)c->m_flSuspicion, op, value );
		break;
	case RPG_COND_ITEM:
	{
		int n = s_pWorldQuery ? s_pWorldQuery->CountItem( ctx.pPlayer, id.Get() ) : -1;
		if ( n < 0 )
		{
			bUnknown = true;
			bResult = true;
		}
		else
			bResult = Compare( n, op, value );
		break;
	}
	case RPG_COND_PERK:
		bResult = Compare( c->GetPerkRank( id.Get() ), op, value );
		break;
	case RPG_COND_TRAIT:
		bResult = c->HasTrait( id.Get() );
		break;
	case RPG_COND_FLAG:
	case RPG_COND_NOFLAG:
	{
		const RPGFlagValue_t *pFlag = c->FindFlag( id.Get() );
		bool bMatch;
		if ( tier == -2 )
			bMatch = pFlag && pFlag->IsSet();
		else if ( bString || ( pFlag && pFlag->IsString() ) )
		{
			const char *pszCur = pFlag ? c->GetFlagString( id.Get() ) : "";
			bool bEq = !Q_stricmp( pszCur, str.Get() );
			bMatch = ( op == RPG_OP_NE ) ? !bEq : ( op == RPG_OP_EQ ? bEq : false );
		}
		else
			bMatch = Compare( pFlag ? pFlag->iValue : 0, op, value );
		bResult = ( type == RPG_COND_FLAG ) ? bMatch : !bMatch;
		break;
	}
	case RPG_COND_ACT:
		if ( tier == -3 )
			bResult = c->GetAct() >= value && c->GetAct() <= value2;
		else
			bResult = Compare( c->GetAct(), op, value );
		break;
	case RPG_COND_TIME:
	{
		float h = 12.0f, flRise = 6.0f, flSet = 18.0f;
		if ( !s_pWorldQuery || !s_pWorldQuery->GetTimeOfDay( h, flRise, flSet ) )
		{
			bUnknown = true;
			bResult = true;
			break;
		}
		if ( !Q_stricmp( id.Get(), "day" ) )
			bResult = h >= flRise && h < flSet;
		else if ( !Q_stricmp( id.Get(), "night" ) )
			bResult = !( h >= flRise && h < flSet );
		else if ( !Q_stricmp( id.Get(), "curfew" ) )
			bResult = IsCurfewHour( h, c->GetAct() );
		else
			bResult = InHourRange( h, value, value2 );
		break;
	}
	case RPG_COND_DISPOSITION:
		if ( id.IsEmpty() && !ctx.pNPC )
		{
			bUnknown = true;	// нет собеседника
			bResult = true;
		}
		else
			bResult = Compare( GetCurrentValue( ctx ), op, value );
		break;
	case RPG_COND_QUEST_STATE:
	case RPG_COND_QUEST_STAGE:
	{
		int iStage = 0;
		int iState = s_pWorldQuery ? s_pWorldQuery->GetQuestState( ctx.pPlayer, id.Get(), iStage ) : -1;
		if ( iState < 0 )
		{
			bUnknown = true;
			bResult = true;
		}
		else if ( type == RPG_COND_QUEST_STATE )
			bResult = ( iState == value );
		else
			bResult = ( iState == 1 && iStage >= value );
		break;
	}
	case RPG_COND_NOTE:
		bResult = c->HasNote( id.Get() );
		break;
	case RPG_COND_LOCATION:
		bResult = c->HasLocation( id.Get() );
		break;
	case RPG_COND_SUIT:
	{
		int iSuit = s_pWorldQuery ? s_pWorldQuery->GetSuitType( ctx.pPlayer ) : -1;
		if ( iSuit < 0 )
		{
			bUnknown = true;
			bResult = true;
		}
		else
			bResult = ( iSuit == RPG_SuitTypeFromKey( id.Get() ) );
		break;
	}
	case RPG_COND_NPCVAR:
	{
		// по имени собеседника (у генериков разные имена - разный «характер»), без имени - по индексу
		const char *pszName = ( s_pWorldQuery && ctx.pNPC ) ? s_pWorldQuery->GetNPCName( ctx.pNPC ) : NULL;
		if ( !ctx.pNPC )
		{
			bUnknown = true;
			bResult = true;
			break;
		}
		unsigned int h = 2166136261u;
		if ( pszName && pszName[0] )
		{
			for ( const char *q = pszName; *q; ++q )
				h = ( h ^ (unsigned char)( *q | 0x20 ) ) * 16777619u;
		}
		else
			h = (unsigned int)ctx.pNPC->entindex() * 2654435761u;
		bResult = (int)( ( h >> 4 ) % (unsigned int)value2 ) == value;
		break;
	}
	case RPG_COND_LAW:
	{
		const int v = s_pWorldQuery ? s_pWorldQuery->LawQuery( ctx.pPlayer, ctx.pNPC, id.Get() ) : -1;
		if ( v < 0 )
		{
			bUnknown = true;
			bResult = true;
		}
		else
			bResult = v > 0;
		break;
	}
	default:
		bResult = false;
		break;
	}

	if ( pUnknown )
		*pUnknown = bUnknown;
	if ( bUnknown )
		return true;
	return bNegate ? !bResult : bResult;
}

void RPGCondition_t::Describe( CUtlString &out ) const
{
	out = bNegate ? "!" : "";
	out.Append( RPG_ConditionTypeName( type ) );
	out.Append( " " );
	switch ( type )
	{
	case RPG_COND_FLAG:
	case RPG_COND_NOFLAG:
		out.Append( id.Get() );
		if ( tier != -2 )
		{
			out.Append( OpString( op ) );
			out.Append( str.Get() );
		}
		return;
	case RPG_COND_ITEM:
	case RPG_COND_PERK:
		out.Append( CFmtStr( "%s%s%d", id.Get(), OpString( op ), value ) );
		return;
	case RPG_COND_TRAIT:
	case RPG_COND_NOTE:
	case RPG_COND_LOCATION:
	case RPG_COND_SUIT:
	case RPG_COND_LAW:
		out.Append( id.Get() );
		return;
	case RPG_COND_NPCVAR:
		out.Append( CFmtStr( "%d/%d", value, value2 ) );
		return;
	case RPG_COND_ACT:
		if ( tier == -3 )
			out.Append( CFmtStr( "%d-%d", value, value2 ) );
		else
			out.Append( CFmtStr( "%s%d", OpString( op ), value ) );
		return;
	case RPG_COND_TIME:
		if ( !Q_stricmp( id.Get(), "range" ) )
			out.Append( CFmtStr( "%d-%d", value, value2 ) );
		else
			out.Append( id.Get() );
		return;
	case RPG_COND_QUEST_STATE:
	case RPG_COND_QUEST_STAGE:
		out.Append( CFmtStr( "%s:%d", id.Get(), value ) );
		return;
	case RPG_COND_CIVIL:
		if ( !Q_stricmp( id.Get(), RPG_CIVIL_TEMPPASS_KEY ) )
		{
			out.Append( RPG_CIVIL_TEMPPASS_KEY );
			return;
		}
		break;
	}
	if ( !id.IsEmpty() )
		out.Append( id.Get() );
	if ( tier >= 0 )
	{
		const char *pszTier = "";
		if ( type == RPG_COND_FACTION ) pszTier = RPGFormulas::FactionTierKey( tier );
		else if ( type == RPG_COND_CIVIL ) pszTier = RPGFormulas::CivilTierKey( tier );
		else if ( type == RPG_COND_HUMANITY ) pszTier = RPGFormulas::HumanityTierKey( tier );
		else if ( type == RPG_COND_SUSPICION ) pszTier = RPGFormulas::SuspicionTierKey( tier );
		out.Append( CFmtStr( "%s%s", bHasOp ? OpString( op ) : ":", pszTier ) );
	}
	else
		out.Append( CFmtStr( "%s%d", OpString( op ), value ) );
}

// ---------------------------------------------------------------------------
// RPGConditionList_t
// ---------------------------------------------------------------------------
RPGConditionList_t::RPGConditionList_t()
{
	m_bHideIfUnmet = false;
}

void RPGConditionList_t::Clear()
{
	m_Terms.RemoveAll();
	m_bHideIfUnmet = false;
}

bool RPGConditionList_t::IsConditionKey( const char *pszKey )
{
	if ( !pszKey )
		return false;
	if ( !Q_stricmp( pszKey, "require" ) || !Q_stricmp( pszKey, "hide_if_unmet" ) )
		return true;
	if ( Q_strnicmp( pszKey, "require_", 8 ) )
		return false;
	return RPG_ConditionTypeFromName( pszKey + 8 ) != RPG_COND_INVALID;
}

// определить тип у элемента выражения без явного вида ("ANA>=5", "mq00_done", "lambda:recognized")
static int GuessConditionType( const char *pszAlt, const char *&pszBody )
{
	pszBody = pszAlt;
	const char *p = SkipSpaces( pszAlt );

	// "вид тело" / "вид:тело"
	CUtlString word;
	const char *pAfter = SplitIdent( p, word );
	int t = RPG_ConditionTypeFromName( word.Get() );
	if ( t != RPG_COND_INVALID && ( *pAfter == ' ' || *pAfter == '\t' || *pAfter == ':' ) )
	{
		pszBody = SkipSpaces( *pAfter == ':' ? pAfter + 1 : pAfter );
		return t;
	}
	// числовые поля персонажа: "level>=3", "credits >= 50", "civil:reliable"
	if ( t != RPG_COND_INVALID && t != RPG_COND_FLAG && t != RPG_COND_NOFLAG && t != RPG_COND_ITEM )
	{
		pszBody = pAfter;
		return t;
	}

	const CRPGData &data = RPGData();
	if ( data.FindAttr( word.Get() ) >= 0 )
		return RPG_COND_ATTR;
	if ( data.FindSkill( word.Get() ) >= 0 )
		return RPG_COND_SKILL;
	if ( data.FindFaction( word.Get() ) )
		return RPG_COND_FACTION;
	if ( RPGFormulas::CivilTierFromKey( word.Get() ) >= 0 || !Q_stricmp( word.Get(), RPG_CIVIL_TEMPPASS_KEY ) )
		return RPG_COND_CIVIL;
	if ( !Q_stricmp( word.Get(), "day" ) || !Q_stricmp( word.Get(), "night" ) || !Q_stricmp( word.Get(), "curfew" ) )
		return RPG_COND_TIME;
	if ( !Q_strnicmp( word.Get(), "item_", 5 ) || !Q_strnicmp( word.Get(), "weapon_", 7 ) || data.FindItem( word.Get() ) )
		return RPG_COND_ITEM;
	if ( !Q_strnicmp( word.Get(), "PK-", 3 ) || data.FindPerk( word.Get() ) )
		return RPG_COND_PERK;
	if ( !Q_strnicmp( word.Get(), "TR-", 3 ) || data.FindTrait( word.Get() ) )
		return RPG_COND_TRAIT;
	if ( data.FindLocation( word.Get() ) )
		return RPG_COND_LOCATION;
	return RPG_COND_FLAG;
}

static bool AddAlternatives( RPGConditionList_t &list, int iType, const char *pszText )
{
	bool bOk = true;
	CUtlVector<CUtlString> terms;
	SplitList( pszText, ',', terms );
	for ( int t = 0; t < terms.Count(); ++t )
	{
		CUtlVector<CUtlString> alts;
		SplitList( terms[t].Get(), '|', alts );
		if ( alts.Count() == 0 )
			continue;
		RPGConditionTerm_t &term = list.m_Terms[list.m_Terms.AddToTail()];
		for ( int a = 0; a < alts.Count(); ++a )
		{
			const char *pszAlt = alts[a].Get();
			bool bNeg = false;
			const char *p = SkipSpaces( pszAlt );
			// "!" перед видом/флагом ("!flag x", "!mq02_done"), но не "!="
			if ( *p == '!' && p[1] != '=' )
			{
				bNeg = true;
				p = SkipSpaces( p + 1 );
			}
			int iCondType = iType;
			const char *pszBody = p;
			if ( iCondType == RPG_COND_INVALID )
				iCondType = GuessConditionType( p, pszBody );

			RPGCondition_t &cond = term.alternatives[term.alternatives.AddToTail()];
			cond.bNegate = bNeg;
			CUtlString err;
			if ( !cond.Parse( iCondType, pszBody, &err ) )
			{
				Warning( "[rpg] condition: %s\n", err.Get() );
				bOk = false;
			}
		}
	}
	return bOk;
}

bool RPGConditionList_t::ParseExpression( const char *pszExpr )
{
	if ( !pszExpr || !pszExpr[0] )
		return true;
	RPGData().EnsureLoaded();
	return AddAlternatives( *this, RPG_COND_INVALID, pszExpr );
}

bool RPGConditionList_t::AddKey( const char *pszKey, const char *pszValue )
{
	if ( !pszKey )
		return false;
	if ( !Q_stricmp( pszKey, "hide_if_unmet" ) )
	{
		m_bHideIfUnmet = atoi( pszValue ? pszValue : "0" ) != 0;
		return true;
	}
	if ( !Q_stricmp( pszKey, "require" ) )
		return ParseExpression( pszValue );
	if ( Q_strnicmp( pszKey, "require_", 8 ) )
		return false;
	int iType = RPG_ConditionTypeFromName( pszKey + 8 );
	if ( iType == RPG_COND_INVALID )
		return false;
	RPGData().EnsureLoaded();
	return AddAlternatives( *this, iType, pszValue ? pszValue : "" );
}

bool RPGConditionList_t::ParseKV( KeyValues *pKV, const char *pszPrefix )
{
	if ( !pKV )
		return true;
	bool bOk = true;
	const int nPrefix = pszPrefix ? Q_strlen( pszPrefix ) : 0;
	for ( KeyValues *pKey = pKV->GetFirstValue(); pKey; pKey = pKey->GetNextValue() )
	{
		const char *pszName = pKey->GetName();
		if ( nPrefix )
		{
			if ( Q_strnicmp( pszName, pszPrefix, nPrefix ) )
				continue;
			pszName += nPrefix;
		}
		if ( !IsConditionKey( pszName ) )
			continue;
		if ( !AddKey( pszName, pKey->GetString() ) )
			bOk = false;
	}
	return bOk;
}

RPGEvalResult_t RPGConditionList_t::Evaluate( const RPGEvalContext_t &ctx ) const
{
	RPGEvalResult_t res;
	for ( int t = 0; t < m_Terms.Count(); ++t )
	{
		const RPGConditionTerm_t &term = m_Terms[t];
		bool bAny = false;
		for ( int a = 0; a < term.alternatives.Count(); ++a )
		{
			bool bUnknown = false;
			if ( term.alternatives[a].Test( ctx, &bUnknown ) )
			{
				bAny = true;
				if ( bUnknown )
					res.unknown = true;
				break;
			}
		}
		if ( !bAny && term.alternatives.Count() > 0 )
		{
			res.met = false;
			if ( res.failTerm < 0 )
			{
				res.failTerm = t;
				const RPGCondition_t &c = term.alternatives[0];
				c.Describe( res.reason );
				if ( c.type != RPG_COND_TRAIT && c.type != RPG_COND_NOTE && c.type != RPG_COND_LOCATION && c.type != RPG_COND_TIME && c.type != RPG_COND_SUIT &&
					c.type != RPG_COND_NPCVAR && c.type != RPG_COND_LAW )
					res.reason.Append( CFmtStr( " (%d)", c.GetCurrentValue( ctx ) ) );
			}
		}
	}
	res.visible = res.met || !m_bHideIfUnmet;
	return res;
}

bool RPG_TestCondition( const char *pszExpr, const RPGEvalContext_t &ctx, CUtlString *pReason )
{
	RPGConditionList_t list;
	list.ParseExpression( pszExpr );
	RPGEvalResult_t res = list.Evaluate( ctx );
	if ( pReason )
		*pReason = res.reason;
	return res.met;
}

// ---------------------------------------------------------------------------
// Эффекты: разбор
// ---------------------------------------------------------------------------
static const char *s_pszEffectNames[RPG_EFF_COUNT] =
{
	"", "xp", "rep", "civil", "suspicion", "humanity", "resonance", "influence", "credits",
	"give_item", "take_item", "set_flag", "clear_flag", "add_flag", "disposition", "disposition_npc",
	"grant_quest", "complete_quest", "fail_quest", "set_stage", "quest_event", "discover", "damage",
	"heal", "start_combat", "open_barter", "open_chargen", "open_levelup", "fire", "notify", "sound",
	"act", "note", "wait", "haggle", "law"
};

const char *RPG_EffectTypeName( int type )
{
	return ( type > 0 && type < RPG_EFF_COUNT ) ? s_pszEffectNames[type] : "";
}

static int EffectTypeFromName( const char *pszName )
{
	if ( !pszName || !pszName[0] )
		return RPG_EFF_INVALID;
	for ( int i = 1; i < RPG_EFF_COUNT; ++i )
	{
		if ( !Q_stricmp( pszName, s_pszEffectNames[i] ) )
			return i;
	}
	return RPG_EFF_INVALID;
}

bool RPGEffectList_t::IsEffectKey( const char *pszKey )
{
	return EffectTypeFromName( pszKey ) != RPG_EFF_INVALID;
}

// "id<sep>N": первый из ':' '=' ' '
static void SplitIdAmount( const char *pszItem, CUtlString &id, int &amount, int iDefault )
{
	const char *p = pszItem;
	const char *pSep = NULL;
	for ( const char *q = p; *q; ++q )
	{
		if ( *q == ':' || *q == '=' || *q == ' ' || *q == '\t' )
		{
			pSep = q;
			break;
		}
	}
	if ( !pSep )
	{
		id = Trimmed( p );
		amount = iDefault;
		return;
	}
	id = Trimmed( p, pSep );
	CUtlString rest = Trimmed( pSep + 1 );
	amount = rest.IsEmpty() ? iDefault : ToInt( rest.Get() );
}

bool RPGEffectList_t::AddKey( const char *pszKey, const char *pszValue )
{
	int iType = EffectTypeFromName( pszKey );
	if ( iType == RPG_EFF_INVALID )
		return false;
	if ( !pszValue )
		pszValue = "";
	RPGData().EnsureLoaded();

	CUtlVector<CUtlString> items;
	if ( iType == RPG_EFF_FIRE )
	{
		// несколько fire - через ';'; поля - через ',' (или ESC, как в выходах Hammer)
		CUtlString v = pszValue;
		char *pBuf = v.Get() ? const_cast<char *>( v.Get() ) : NULL;
		for ( char *q = pBuf; q && *q; ++q )
		{
			if ( *q == 0x1b )
				*q = ',';
		}
		SplitList( v.Get(), ';', items );
	}
	else
		SplitList( pszValue, ',', items );

	if ( items.Count() == 0 )
	{
		// ключ без значения: open_chargen, open_levelup, start_combat ...
		if ( iType == RPG_EFF_OPEN_CHARGEN || iType == RPG_EFF_OPEN_LEVELUP || iType == RPG_EFF_OPEN_BARTER || iType == RPG_EFF_START_COMBAT )
			items.AddToTail( CUtlString( "1" ) );
		else
			return true;
	}

	bool bOk = true;
	for ( int i = 0; i < items.Count(); ++i )
	{
		const char *pszItem = items[i].Get();
		RPGEffect_t e;
		e.type = iType;
		switch ( iType )
		{
		case RPG_EFF_XP:
		case RPG_EFF_CIVIL:
		case RPG_EFF_SUSPICION:
		case RPG_EFF_HUMANITY:
		case RPG_EFF_RESONANCE:
		case RPG_EFF_INFLUENCE:
		case RPG_EFF_CREDITS:
		case RPG_EFF_DAMAGE:
		case RPG_EFF_HEAL:
		case RPG_EFF_ACT:
		case RPG_EFF_DISPOSITION:
		case RPG_EFF_START_COMBAT:
		case RPG_EFF_OPEN_CHARGEN:
		case RPG_EFF_OPEN_LEVELUP:
			if ( !IsInteger( pszItem ) )
			{
				Warning( "[rpg] effect %s: '%s' is not a number\n", pszKey, pszItem );
				bOk = false;
				continue;
			}
			e.amount = ToInt( pszItem );
			if ( ( iType == RPG_EFF_START_COMBAT || iType == RPG_EFF_OPEN_CHARGEN || iType == RPG_EFF_OPEN_LEVELUP ) && e.amount == 0 )
				continue;
			break;
		case RPG_EFF_REP:
		case RPG_EFF_DISPOSITION_NPC:
			SplitIdAmount( pszItem, e.id, e.amount, 0 );
			if ( iType == RPG_EFF_REP && !RPGData().FindFaction( e.id.Get() ) )
				DevWarning( "[rpg] effect rep: unknown faction '%s'\n", e.id.Get() );
			break;
		case RPG_EFF_GIVE_ITEM:
		case RPG_EFF_TAKE_ITEM:
			SplitIdAmount( pszItem, e.id, e.amount, 1 );
			if ( e.amount <= 0 )
				continue;
			break;
		case RPG_EFF_SET_FLAG:
		{
			const char *pEq = Q_strstr( pszItem, "=" );
			if ( pEq )
			{
				e.id = Trimmed( pszItem, pEq );
				CUtlString v = Trimmed( pEq + 1 );
				if ( IsInteger( v.Get() ) )
					e.amount = ToInt( v.Get() );
				else
				{
					e.bString = true;
					e.str = v;
				}
			}
			else
			{
				e.id = pszItem;
				e.amount = 1;
			}
			break;
		}
		case RPG_EFF_ADD_FLAG:
			SplitIdAmount( pszItem, e.id, e.amount, 1 );
			break;
		case RPG_EFF_CLEAR_FLAG:
		case RPG_EFF_GRANT_QUEST:
		case RPG_EFF_COMPLETE_QUEST:
		case RPG_EFF_FAIL_QUEST:
		case RPG_EFF_DISCOVER:
		case RPG_EFF_NOTE:
		case RPG_EFF_SOUND:
		case RPG_EFF_LAW:
			e.id = pszItem;
			break;
		case RPG_EFF_NOTIFY:
		{
			const char *pColon = Q_strstr( pszItem, ":" );
			if ( pColon )
			{
				e.id = Trimmed( pszItem, pColon );
				e.param = Trimmed( pColon + 1 );
			}
			else
				e.id = pszItem;
			break;
		}
		case RPG_EFF_SET_STAGE:
			SplitIdAmount( pszItem, e.id, e.amount, 0 );
			break;
		case RPG_EFF_QUEST_EVENT:
		{
			const char *pColon = Q_strstr( pszItem, ":" );
			if ( pColon )
			{
				e.str = Trimmed( pszItem, pColon );		// тип: talk/reach/event/...
				e.id = Trimmed( pColon + 1 );
			}
			else
			{
				e.str = "event";
				e.id = pszItem;
			}
			break;
		}
		case RPG_EFF_HAGGLE:
			// "+10" - торговец собеседника, "kadar:+10" - указанный
			if ( IsInteger( pszItem ) )
			{
				e.id = "";
				e.amount = ToInt( pszItem );
			}
			else
				SplitIdAmount( pszItem, e.id, e.amount, 0 );
			break;
		case RPG_EFF_OPEN_BARTER:
			// "1" - торговец собеседника (rpg_trader), иначе id торговца
			if ( IsInteger( pszItem ) )
			{
				if ( ToInt( pszItem ) == 0 )
					continue;
				e.id = "";
			}
			else
				e.id = pszItem;
			e.amount = 1;
			break;
		case RPG_EFF_FIRE:
		{
			CUtlVector<CUtlString> f;
			SplitList( pszItem, ',', f, true );
			if ( f.Count() < 2 )
			{
				Warning( "[rpg] effect fire: '%s' needs target,Input[,param,delay]\n", pszItem );
				bOk = false;
				continue;
			}
			e.id = f[0];
			e.str = f[1];
			if ( f.Count() > 2 )
				e.param = f[2];
			if ( f.Count() > 3 )
				e.delay = (float)atof( f[3].Get() );
			break;
		}
		}
		m_Effects.AddToTail( e );
	}
	return bOk;
}

bool RPGEffectList_t::ParseString( const char *pszEffects )
{
	if ( !pszEffects || !pszEffects[0] )
		return true;

	// группируем: элемент "ключ=..." начинает новый ключ, остальные продолжают предыдущий
	CUtlVector<CUtlString> items;
	SplitList( pszEffects, ',', items, true );
	CUtlString curKey, curValue;
	bool bOk = true;
	bool bHaveKey = false;
	for ( int i = 0; i < items.Count(); ++i )
	{
		const char *pszItem = items[i].Get();
		const char *pEq = Q_strstr( pszItem, "=" );
		CUtlString candidate = pEq ? Trimmed( pszItem, pEq ) : Trimmed( pszItem );
		if ( IsEffectKey( candidate.Get() ) )
		{
			if ( bHaveKey && !AddKey( curKey.Get(), curValue.Get() ) )
				bOk = false;
			curKey = candidate;
			curValue = pEq ? Trimmed( pEq + 1 ) : CUtlString();
			bHaveKey = true;
		}
		else if ( bHaveKey )
		{
			curValue.Append( "," );
			curValue.Append( pszItem );
		}
		else if ( !items[i].IsEmpty() )
		{
			Warning( "[rpg] effects: '%s' is not an effect key\n", pszItem );
			bOk = false;
		}
	}
	if ( bHaveKey && !AddKey( curKey.Get(), curValue.Get() ) )
		bOk = false;
	return bOk;
}

bool RPGEffectList_t::ParseKV( KeyValues *pKV, const char *pszPrefix )
{
	if ( !pKV )
		return true;
	bool bOk = true;
	const int nPrefix = pszPrefix ? Q_strlen( pszPrefix ) : 0;
	for ( KeyValues *pKey = pKV->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey() )
	{
		const char *pszName = pKey->GetName();
		if ( nPrefix )
		{
			if ( Q_strnicmp( pszName, pszPrefix, nPrefix ) )
				continue;
			pszName += nPrefix;
		}
		if ( !Q_stricmp( pszName, "effects" ) )
		{
			// блок { "xp" "25" ... } или строка "xp=25, ..."
			if ( pKey->GetFirstSubKey() )
				bOk &= ParseKV( pKey, "" );
			else
				bOk &= ParseString( pKey->GetString() );
			continue;
		}
		if ( pKey->GetFirstSubKey() || !IsEffectKey( pszName ) )
			continue;
		if ( !AddKey( pszName, pKey->GetString() ) )
			bOk = false;
	}
	return bOk;
}

void RPGEffect_t::Describe( CUtlString &out ) const
{
	out = RPG_EffectTypeName( type );
	out.Append( " " );
	switch ( type )
	{
	case RPG_EFF_REP:
	case RPG_EFF_GIVE_ITEM:
	case RPG_EFF_TAKE_ITEM:
	case RPG_EFF_ADD_FLAG:
	case RPG_EFF_DISPOSITION_NPC:
	case RPG_EFF_SET_STAGE:
	case RPG_EFF_HAGGLE:
		out.Append( CFmtStr( "%s:%d", id.Get(), amount ) );
		break;
	case RPG_EFF_SET_FLAG:
		if ( bString )
			out.Append( CFmtStr( "%s=%s", id.Get(), str.Get() ) );
		else
			out.Append( CFmtStr( "%s=%d", id.Get(), amount ) );
		break;
	case RPG_EFF_QUEST_EVENT:
		out.Append( CFmtStr( "%s:%s", str.Get(), id.Get() ) );
		break;
	case RPG_EFF_FIRE:
		out.Append( CFmtStr( "%s,%s,%s,%.2f", id.Get(), str.Get(), param.Get(), delay ) );
		break;
	case RPG_EFF_CLEAR_FLAG:
	case RPG_EFF_GRANT_QUEST:
	case RPG_EFF_COMPLETE_QUEST:
	case RPG_EFF_FAIL_QUEST:
	case RPG_EFF_DISCOVER:
	case RPG_EFF_NOTE:
	case RPG_EFF_SOUND:
	case RPG_EFF_NOTIFY:
	case RPG_EFF_OPEN_BARTER:
	case RPG_EFF_LAW:
		out.Append( id.Get() );
		break;
	default:
		out.Append( CFmtStr( "%+d", amount ) );
		break;
	}
}

// ---------------------------------------------------------------------------
// Проверки
// ---------------------------------------------------------------------------
const char *RPGCheck_t::KindName( int kind )
{
	switch ( kind )
	{
	case RPG_CHECK_SKILL: return "skill";
	case RPG_CHECK_ATTR: return "attr";
	case RPG_CHECK_LIE: return "lie";
	case RPG_CHECK_INTIMIDATE: return "intimidate";
	case RPG_CHECK_BRIBE: return "bribe";
	case RPG_CHECK_BARTER: return "barter";
	}
	return "";
}

// "<id>:N" -> часть (атрибут или навык)
static bool ParseCheckPart( const char *pszPart, RPGCheckPart_t &part, int iForce )	// -1 любой, 0 навык, 1 атрибут
{
	CUtlVector<CUtlString> f;
	SplitList( pszPart, ':', f );
	if ( f.Count() != 2 || !IsInteger( f[1].Get() ) )
		return false;
	part.threshold = ToInt( f[1].Get() );
	int a = RPGData().FindAttr( f[0].Get() );
	int s = RPGData().FindSkill( f[0].Get() );
	if ( iForce == 1 || ( iForce < 0 && a >= 0 ) )
	{
		part.bAttr = true;
		part.index = a;
		return a >= 0;
	}
	part.bAttr = false;
	part.index = s;
	return s >= 0;
}

bool RPGCheck_t::Parse( const char *pszSpec, CUtlString *pError )
{
	RPGData().EnsureLoaded();
	kind = RPG_CHECK_NONE;
	numParts = 0;
	base = 0;
	if ( !pszSpec || !pszSpec[0] )
		return false;

	CUtlString spec = Trimmed( pszSpec );
	const char *p = spec.Get();
	const char *pColon = Q_strstr( p, ":" );
	if ( !pColon )
	{
		if ( pError )
			pError->Format( "bad check '%s'", pszSpec );
		return false;
	}
	CUtlString k = Trimmed( p, pColon );
	const char *pRest = pColon + 1;
	bool bOk = false;

	if ( !Q_stricmp( k.Get(), "skill" ) || !Q_stricmp( k.Get(), "attr" ) )
	{
		kind = !Q_stricmp( k.Get(), "skill" ) ? RPG_CHECK_SKILL : RPG_CHECK_ATTR;
		bOk = ParseCheckPart( pRest, parts[0], kind == RPG_CHECK_ATTR ? 1 : 0 );
		numParts = 1;
	}
	else if ( !Q_stricmp( k.Get(), "lie" ) || !Q_stricmp( k.Get(), "barter" ) )
	{
		kind = !Q_stricmp( k.Get(), "lie" ) ? RPG_CHECK_LIE : RPG_CHECK_BARTER;
		parts[0].bAttr = false;
		parts[0].index = RPGData().FindSkill( kind == RPG_CHECK_LIE ? "speech" : "barter" );
		parts[0].threshold = ToInt( pRest );
		numParts = 1;
		bOk = parts[0].index >= 0 && IsInteger( pRest );
	}
	else if ( !Q_stricmp( k.Get(), "intimidate" ) )
	{
		kind = RPG_CHECK_INTIMIDATE;
		CUtlVector<CUtlString> alts;
		SplitList( pRest, '/', alts );
		bOk = alts.Count() > 0;
		for ( int i = 0; i < alts.Count() && numParts < RPG_CHECK_MAX_PARTS; ++i )
		{
			if ( !ParseCheckPart( alts[i].Get(), parts[numParts], -1 ) )
				bOk = false;
			else
				++numParts;
		}
	}
	else if ( !Q_stricmp( k.Get(), "bribe" ) )
	{
		kind = RPG_CHECK_BRIBE;
		base = ToInt( pRest );
		bOk = IsInteger( pRest ) && base >= 0;
	}

	if ( !bOk )
	{
		if ( pError )
			pError->Format( "bad check '%s'", pszSpec );
		kind = RPG_CHECK_NONE;
		numParts = 0;
	}
	return bOk;
}

void RPGCheck_t::Describe( CUtlString &out ) const
{
	out = KindName( kind );
	if ( kind == RPG_CHECK_BRIBE )
	{
		out.Append( CFmtStr( " %d", base ) );
		return;
	}
	for ( int i = 0; i < numParts; ++i )
	{
		const RPGCheckPart_t &pt = parts[i];
		const char *pszId = pt.bAttr ? ( pt.index >= 0 ? g_pszRPGAttrAbbr[pt.index] : "?" )
			: ( pt.index >= 0 && pt.index < RPGData().GetSkillCount() ? RPGData().GetSkill( pt.index ).id.Get() : "?" );
		out.Append( CFmtStr( "%s%s %d", i ? " /" : " ", pszId, pt.threshold ) );
	}
}

RPGCheckResult_t RPG_EvaluateCheck( const RPGCheck_t &check, const RPGEvalContext_t &ctx, bool bInQuest )
{
	RPGCheckResult_t res;
	const CRPGCharacterData *c = ctx.pChar;
	if ( !c || !check.IsValid() )
		return res;

	if ( check.kind == RPG_CHECK_BRIBE )
	{
		res.cost = RPGFormulas::BribeCost( check.base, c->GetAttr( RPG_ATTR_AUTHORITY ),
			c->GetSkill( "barter", true ), c->GetModMult( "bribe_cost" ) );
		res.value = c->m_iCredits;
		res.threshold = res.cost;
		res.pass = c->m_iCredits >= res.cost;
		return res;
	}

	// лучшая часть: проходящая с наибольшим запасом, иначе ближайшая к порогу
	int bestMargin = INT_MIN;
	for ( int i = 0; i < check.numParts; ++i )
	{
		const RPGCheckPart_t &pt = check.parts[i];
		int v = pt.bAttr ? c->GetAttr( pt.index ) : c->GetSkill( pt.index, true );
		int margin = v - pt.threshold;
		if ( margin > bestMargin )
		{
			bestMargin = margin;
			res.part = i;
			res.value = v;
			res.threshold = pt.threshold;
			res.pass = margin >= 0;
			res.xp = pt.bAttr ? RPGFormulas::CheckXPAttr( pt.threshold, bInQuest ) : RPGFormulas::CheckXPSkill( pt.threshold, bInQuest );
		}
	}
	return res;
}

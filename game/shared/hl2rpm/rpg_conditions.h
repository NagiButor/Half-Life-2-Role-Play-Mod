//========= HL2RPM ============//
//
// RPG-система: условия (RPG_SPEC §3), эффекты (§4) и проверки диалогов (§5 «check»).
// Один движок для диалогов, квестов, logic_rpg, trigger_rpg, prop_rpg_usable.
//
// Условия:
//   RPGConditionList_t cond;
//   cond.ParseKV( pOptionKV );                 // все require_*, require "<выр>", hide_if_unmet
//   cond.ParseExpression( "mq00_done, !mq02_x, ANA>=6 | speech>=40" );
//   RPGEvalResult_t r = cond.Evaluate( RPG_MakeEvalContext( pPlayer, pNPC ) );   // r.met / r.visible / r.reason
// Эффекты:
//   RPGEffectList_t eff;
//   eff.ParseKV( pOptionKV );                  // ключи §4 (xp, rep, give_item...) и блок/строка "effects"
//   eff.ParseKV( pOptionKV, "on_success_" );   // только on_success_<ключ>
//   eff.ParseString( "xp=25, rep=lambda:+10, reapers:-5, set_flag=mq02_path=lambda" );
//   eff.Apply( pPlayer, pNPC, pCaller );       // только сервер
// Проверки:
//   RPGCheck_t chk; chk.Parse( "skill:speech:50" );   // attr:MIG:7, lie:35, intimidate:MIG:6/speech:40, bribe:50, barter:35
//   RPGCheckResult_t res = chk.Evaluate( ctx, bInQuest );   // res.pass, res.value, res.threshold, res.xp, res.cost
//   RPG_ApplyCheckOutcome( pPlayer, pNPC, chk, res );         // сервер: XP, кредиты подкупа, подозрение за провал
//
//=============================//

#ifndef RPG_CONDITIONS_H
#define RPG_CONDITIONS_H
#ifdef _WIN32
#pragma once
#endif

#include "hl2rpm/rpg_shared.h"

class CRPGCharacterData;
class KeyValues;
// на клиенте cbase.h уже делает CBasePlayer = C_BasePlayer (shared_classnames.h)
class CBasePlayer;
class CBaseEntity;

// ---------------------------------------------------------------------------
// Контекст: чей персонаж, игрок, собеседник
// ---------------------------------------------------------------------------
struct RPGEvalContext_t
{
	const CRPGCharacterData *pChar;
	CBasePlayer *pPlayer;
	CBaseEntity *pNPC;		// собеседник (require_disposition, start_combat, open_barter 1)

	RPGEvalContext_t() : pChar( NULL ), pPlayer( NULL ), pNPC( NULL ) {}
};

// Контекст для персонажа этой стороны (сервер: персонаж игрока; клиент: зеркало)
RPGEvalContext_t RPG_MakeEvalContext( CBasePlayer *pPlayer, CBaseEntity *pNPC = NULL );

// То, чего нет в данных персонажа: инвентарь, квесты, время, NPC. Сервер и клиент
// ставят свою реализацию (клиент знает не всё - «неизвестно» считается выполненным).
class IRPGWorldQuery
{
public:
	// -1 - неизвестно
	virtual int CountItem( CBasePlayer *pPlayer, const char *pszClassname ) = 0;
	// QuestState (0 inactive, 1 active, 2 completed, 3 failed), stage; -1 - неизвестно
	virtual int GetQuestState( CBasePlayer *pPlayer, const char *pszQuest, int &iStage ) = 0;
	// targetname NPC (ключ словаря отношений) и его rpg_disposition
	virtual const char *GetNPCName( CBaseEntity *pNPC ) = 0;
	virtual int GetNPCStartDisposition( CBaseEntity *pNPC ) = 0;
	// время суток; false - неизвестно
	virtual bool GetTimeOfDay( float &flHours, float &flSunrise, float &flSunset ) = 0;
	// RPGSuitType_t надетого костюма
	virtual int GetSuitType( CBasePlayer *pPlayer ) = 0;
};
void RPG_SetWorldQuery( IRPGWorldQuery *pQuery );
IRPGWorldQuery *RPG_GetWorldQuery();

// ---------------------------------------------------------------------------
// Условия
// ---------------------------------------------------------------------------
enum RPGCondType_t
{
	RPG_COND_INVALID = 0,
	RPG_COND_ATTR, RPG_COND_SKILL, RPG_COND_FACTION, RPG_COND_CIVIL, RPG_COND_ITEM,
	RPG_COND_PERK, RPG_COND_TRAIT, RPG_COND_FLAG, RPG_COND_NOFLAG, RPG_COND_HUMANITY,
	RPG_COND_RESONANCE, RPG_COND_INFLUENCE, RPG_COND_SUSPICION, RPG_COND_CREDITS, RPG_COND_LEVEL,
	RPG_COND_ACT, RPG_COND_TIME, RPG_COND_DISPOSITION, RPG_COND_QUEST_STATE, RPG_COND_QUEST_STAGE,
	RPG_COND_NOTE, RPG_COND_LOCATION, RPG_COND_CHARGEN, RPG_COND_ZONE, RPG_COND_SUIT,
	RPG_COND_COUNT
};

enum RPGCompareOp_t
{
	RPG_OP_GE = 0, RPG_OP_LE, RPG_OP_GT, RPG_OP_LT, RPG_OP_EQ, RPG_OP_NE
};

struct RPGCondition_t
{
	int type;			// RPGCondType_t
	int op;				// RPGCompareOp_t
	CUtlString id;		// атрибут/навык/фракция/предмет/перк/черта/флаг/квест/NPC/ключ времени
	CUtlString str;		// строковое значение (флаг=значение)
	int value, value2;	// число; value2 - верх диапазона (act 1-2, time 22-6)
	int tier;			// уровень фракции/ГС/человечности/подозрения, -1 - число
	bool bString;		// сравнение строк
	bool bNegate;		// "!" перед условием
	bool bHasOp;		// оператор задан явно (для уровней: без него «не хуже»)

	RPGCondition_t() : type( RPG_COND_INVALID ), op( RPG_OP_GE ), value( 0 ), value2( 0 ), tier( -1 ), bString( false ), bNegate( false ), bHasOp( false ) {}

	// тело условия для типа (то, что после require_<тип>)
	bool Parse( int type, const char *pszBody, CUtlString *pError = NULL );
	// bUnknown - сторона не может это проверить (тогда результат true)
	bool Test( const RPGEvalContext_t &ctx, bool *pUnknown = NULL ) const;
	// текущее значение для подписи «есть N» (0, если неприменимо)
	int GetCurrentValue( const RPGEvalContext_t &ctx ) const;
	// человекочитаемо (для логов и rpg_test): "skill speech>=50"
	void Describe( CUtlString &out ) const;
};

// альтернативы через "|" - достаточно одной
struct RPGConditionTerm_t
{
	CUtlVector<RPGCondition_t> alternatives;
};

struct RPGEvalResult_t
{
	bool met;				// все условия выполнены
	bool visible;			// показывать опцию (false, если hide_if_unmet и не выполнено)
	bool unknown;			// что-то не удалось проверить на этой стороне
	int failTerm;			// индекс первого невыполненного условия, -1
	CUtlString reason;		// "skill speech>=50 (30)"

	RPGEvalResult_t() : met( true ), visible( true ), unknown( false ), failTerm( -1 ) {}
};

class RPGConditionList_t
{
public:
	RPGConditionList_t();

	void Clear();
	bool IsEmpty() const { return m_Terms.Count() == 0; }

	// все ключи require_<тип>, общий require "<выр>, <выр>", hide_if_unmet; prefix - напр. "on_" (обычно "")
	// возвращает false, если что-то не разобралось (подробности - Warning в консоль)
	bool ParseKV( KeyValues *pKV, const char *pszPrefix = "" );
	// одно выражение (ключ condition у энтити, rpg_test)
	bool ParseExpression( const char *pszExpr );
	// один ключ: "require_skill" "speech>=50"; false - ключ не условие или ошибка
	bool AddKey( const char *pszKey, const char *pszValue );
	static bool IsConditionKey( const char *pszKey );

	RPGEvalResult_t Evaluate( const RPGEvalContext_t &ctx ) const;

	bool m_bHideIfUnmet;
	CUtlVector<RPGConditionTerm_t> m_Terms;		// все должны выполняться
};

// разовая проверка выражения
bool RPG_TestCondition( const char *pszExpr, const RPGEvalContext_t &ctx, CUtlString *pReason = NULL );

// имена типов условий (attr, skill, ...) и разбор
const char *RPG_ConditionTypeName( int type );
int RPG_ConditionTypeFromName( const char *pszName );

// ---------------------------------------------------------------------------
// Эффекты
// ---------------------------------------------------------------------------
enum RPGEffectType_t
{
	RPG_EFF_INVALID = 0,
	RPG_EFF_XP, RPG_EFF_REP, RPG_EFF_CIVIL, RPG_EFF_SUSPICION, RPG_EFF_HUMANITY, RPG_EFF_RESONANCE,
	RPG_EFF_INFLUENCE, RPG_EFF_CREDITS, RPG_EFF_GIVE_ITEM, RPG_EFF_TAKE_ITEM, RPG_EFF_SET_FLAG,
	RPG_EFF_CLEAR_FLAG, RPG_EFF_ADD_FLAG, RPG_EFF_DISPOSITION, RPG_EFF_DISPOSITION_NPC,
	RPG_EFF_GRANT_QUEST, RPG_EFF_COMPLETE_QUEST, RPG_EFF_FAIL_QUEST, RPG_EFF_SET_STAGE, RPG_EFF_QUEST_EVENT,
	RPG_EFF_DISCOVER, RPG_EFF_DAMAGE, RPG_EFF_HEAL, RPG_EFF_START_COMBAT, RPG_EFF_OPEN_BARTER,
	RPG_EFF_OPEN_CHARGEN, RPG_EFF_OPEN_LEVELUP, RPG_EFF_FIRE, RPG_EFF_NOTIFY, RPG_EFF_SOUND,
	RPG_EFF_ACT, RPG_EFF_NOTE,
	RPG_EFF_WAIT,		// wait N: пропустить N игровых часов (смена, ожидание) с затемнением
	RPG_EFF_HAGGLE,		// haggle [trader:]N: цены торговца лучше на N % до конца игровых суток (N < 0 - хуже)
	RPG_EFF_COUNT
};

struct RPGEffect_t
{
	int type;			// RPGEffectType_t
	CUtlString id;		// фракция/предмет/флаг/квест/NPC/локация/цель fire/токен/звук
	CUtlString str;		// строковое значение флага; Input для fire; тип события квеста
	CUtlString param;	// параметр fire
	int amount;			// число/количество/изменение
	float delay;		// задержка fire
	bool bString;		// set_flag со строковым значением

	RPGEffect_t() : type( RPG_EFF_INVALID ), amount( 0 ), delay( 0.0f ), bString( false ) {}
	void Describe( CUtlString &out ) const;
};

class RPGEffectList_t
{
public:
	void Clear() { m_Effects.RemoveAll(); }
	bool IsEmpty() const { return m_Effects.Count() == 0; }

	// ключи §4 с префиксом ("" / "on_success_" / "on_fail_"), а также блок или строка "<prefix>effects"
	bool ParseKV( KeyValues *pKV, const char *pszPrefix = "" );
	// "ключ=значение, ключ=значение" (элемент без '=' продолжает список предыдущего ключа)
	bool ParseString( const char *pszEffects );
	// один ключ; false - не эффект или ошибка
	bool AddKey( const char *pszKey, const char *pszValue );
	static bool IsEffectKey( const char *pszKey );

#ifndef CLIENT_DLL
	// применить всё по порядку (сервер). pCaller - для fire (энтити-источник)
	void Apply( CBasePlayer *pPlayer, CBaseEntity *pNPC = NULL, CBaseEntity *pCaller = NULL ) const;
#endif

	CUtlVector<RPGEffect_t> m_Effects;
};

const char *RPG_EffectTypeName( int type );

// ---------------------------------------------------------------------------
// Проверки диалогов (check)
// ---------------------------------------------------------------------------
enum RPGCheckKind_t
{
	RPG_CHECK_NONE = 0,
	RPG_CHECK_SKILL,		// skill:<id>:N
	RPG_CHECK_ATTR,			// attr:<id>:N
	RPG_CHECK_LIE,			// lie:N (speech)
	RPG_CHECK_INTIMIDATE,	// intimidate:MIG:6/speech:40 - лучшая из частей
	RPG_CHECK_BRIBE,		// bribe:N - база цены
	RPG_CHECK_BARTER,		// barter:N
};

#define RPG_CHECK_MAX_PARTS 2

struct RPGCheckPart_t
{
	bool bAttr;			// характеристика или навык
	int index;			// RPGAttribute_t / индекс навыка
	int threshold;
};

struct RPGCheck_t
{
	int kind;			// RPGCheckKind_t
	int numParts;
	RPGCheckPart_t parts[RPG_CHECK_MAX_PARTS];
	int base;			// подкуп: база

	RPGCheck_t() : kind( RPG_CHECK_NONE ), numParts( 0 ), base( 0 ) {}
	bool Parse( const char *pszSpec, CUtlString *pError = NULL );
	bool IsValid() const { return kind != RPG_CHECK_NONE; }
	// подпись для логов: "speech 50"
	void Describe( CUtlString &out ) const;
	static const char *KindName( int kind );	// "skill", "attr", "lie", ... (для токенов #RPG_Check_<kind>)
};

struct RPGCheckResult_t
{
	bool pass;
	int part;			// какая часть решила (интимидация)
	int value;			// значение персонажа (навык/характеристика; у подкупа - кредиты)
	int threshold;		// порог (у подкупа - цена)
	int xp;				// XP за успех
	int cost;			// подкуп: цена

	RPGCheckResult_t() : pass( false ), part( 0 ), value( 0 ), threshold( 0 ), xp( 0 ), cost( 0 ) {}
};

RPGCheckResult_t RPG_EvaluateCheck( const RPGCheck_t &check, const RPGEvalContext_t &ctx, bool bInQuest );

#ifndef CLIENT_DLL
// сервер: XP за успех, списание подкупа, подозрение за проваленную ложь/подкуп перед ГО, уведомление
void RPG_ApplyCheckOutcome( CBasePlayer *pPlayer, CBaseEntity *pNPC, const RPGCheck_t &check, const RPGCheckResult_t &result );
#endif

#endif // RPG_CONDITIONS_H

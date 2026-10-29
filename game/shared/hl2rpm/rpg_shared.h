//========= HL2RPM ============//
//
// RPG-система: общие для клиента и сервера определения (перечисления, сетевые записи).
// Правила - Дизайн-документ, Part I; форматы и имена - hl2rpm_extras/docs/RPG_SPEC.md.
//
//=============================//

#ifndef RPG_SHARED_H
#define RPG_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "utlstring.h"
#include "utlvector.h"

// ---------------------------------------------------------------------------
// Характеристики Λ.А.М.Б.Д.А. (порядок фиксирован: сохранения и сеть идут по id)
// ---------------------------------------------------------------------------
enum RPGAttribute_t
{
	RPG_ATTR_AGILITY = 0,	// LOV - Ловкость
	RPG_ATTR_ANALYTICS,		// ANA - Аналитика
	RPG_ATTR_MIGHT,			// MIG - Мощь
	RPG_ATTR_VIGILANCE,		// BDI - Бдительность
	RPG_ATTR_SPIRIT,		// DUH - Дух
	RPG_ATTR_AUTHORITY,		// AVT - Авторитет
	RPG_ATTR_COUNT
};

#define RPG_MAX_SKILLS			24		// в данных 13; запас под будущие
#define RPG_ATTR_MIN			1
#define RPG_ATTR_MAX			10		// при создании
#define RPG_ATTR_MAX_EFFECTIVE	12		// с имплантами/бонусами (показывается «10+»)
#define RPG_ATTR_START			5
#define RPG_CHARGEN_POINTS		34		// сумма характеристик на создании
#define RPG_TAG_COUNT			3
#define RPG_TAG_BONUS			15
#define RPG_MAX_TRAITS			2
#define RPG_SKILL_CAP			100
#define RPG_SKILL_CAP_CHECK		110		// предел для проверок с временными бонусами
#define RPG_MAX_LEVEL			30
#define RPG_NAME_MAX			48		// байт UTF-8

// ---------------------------------------------------------------------------
// Тип костюма (CPlayerLocalData::m_iSuitType) - тема HUD (RPG_SPEC §8)
// ---------------------------------------------------------------------------
enum RPGSuitType_t
{
	RPG_SUIT_CITIZEN = 0,	// роба (без предмета): «Браслет»
	RPG_SUIT_WORKER,		// item_suit_worker
	RPG_SUIT_CP,			// item_suit_cp - маска ГО
	RPG_SUIT_OVERWATCH,		// item_combine_soldier_suit
	RPG_SUIT_REBEL,			// item_suit_rebel
	RPG_SUIT_HEV,			// item_suit
	RPG_SUIT_COUNT
};

// ---------------------------------------------------------------------------
// Уровни шкал (ключи - в токенах #RPG_Civil_<key>, #RPG_FactionTier_<key>)
// ---------------------------------------------------------------------------
enum RPGCivilTier_t
{
	RPG_CIVIL_ANTICITIZEN = 0,	// <= -75
	RPG_CIVIL_MALICIOUS,		// -74..-40
	RPG_CIVIL_WATCHED,			// -39..-15
	RPG_CIVIL_CITIZEN,			// -14..19
	RPG_CIVIL_RELIABLE,			// 20..49
	RPG_CIVIL_LOYALIST,			// >= 50
	RPG_CIVIL_COUNT
};

enum RPGFactionTier_t
{
	RPG_FTIER_ENEMY = 0,		// <= -60
	RPG_FTIER_DISLIKE,			// -59..-25
	RPG_FTIER_NEUTRAL,			// -24..24
	RPG_FTIER_RECOGNIZED,		// 25..59
	RPG_FTIER_ALLY,				// >= 60
	RPG_FTIER_COUNT
};

enum RPGHumanityTier_t
{
	RPG_HTIER_EMPTY = 0,		// 0-19 Пустой
	RPG_HTIER_CALLOUS,			// 20-39 Чёрствый
	RPG_HTIER_HUMAN,			// 40-60 Человек
	RPG_HTIER_COMPASSIONATE,	// 61-80 Сострадательный
	RPG_HTIER_RIGHTEOUS,		// 81-100 Праведный
	RPG_HTIER_COUNT
};

enum RPGSuspicionTier_t
{
	RPG_STIER_CALM = 0,			// 0-24
	RPG_STIER_ATTENTION,		// 25-49 проверка документов
	RPG_STIER_SEARCH,			// 50-74 досмотр
	RPG_STIER_DETAIN,			// 75-99 задержание
	RPG_STIER_ALARM,			// 100 тревога
	RPG_STIER_COUNT
};

// зона контроля Альянса (trigger_rpg_zone)
enum RPGZone_t
{
	RPG_ZONE_NONE = 0,
	RPG_ZONE_CONTROL = 1,		// обычная: подозрение растёт только здесь
	RPG_ZONE_RESTRICTED = 2,	// запретная: +5/с
};

// ---------------------------------------------------------------------------
// User message RPG_State: последовательность записей
//   byte type, string key, затем long value (или string - см. RPGRecordHasString)
// Полная синхронизация: FULL_BEGIN ... FULL_END. Одно сообщение <= ~220 байт.
// Те же записи пишет блок сохранения HL2RPMCharacter (неизвестные типы пропускаются).
// ---------------------------------------------------------------------------
enum RPGRecordType_t
{
	RPG_REC_NONE = 0,
	RPG_REC_FULL_BEGIN = 1,		// клиент сбрасывает зеркало
	RPG_REC_FULL_END = 2,		// клиент оповещает слушателей о полном обновлении
	RPG_REC_STAT = 3,			// key: level xp skillpoints perkpicks credits civil humanity resonance
								//      influence chargen suspicion(x100) zone alarm
	RPG_REC_NAME = 4,			// string
	RPG_REC_ATTR = 5,			// key attr id, базовое значение
	RPG_REC_SKILL = 6,			// key skill id, вложено очков
	RPG_REC_TAG = 7,			// key skill id, 1/0
	RPG_REC_TRAIT = 8,			// key TR-xx, 1/0
	RPG_REC_PERK = 9,			// key PK-xx, ранг (0 - снять)
	RPG_REC_FACTION = 10,		// key faction id, значение
	RPG_REC_FLAG_INT = 11,		// key имя флага, число
	RPG_REC_FLAG_STR = 12,		// key имя флага, string
	RPG_REC_FLAG_CLEAR = 13,	// key имя флага
	RPG_REC_DISPOSITION = 14,	// key targetname NPC, значение
	RPG_REC_NOTE = 15,			// key note id, 1/0
	RPG_REC_LOCATION = 16,		// key location id, 1/0
	RPG_REC_ARMOR = 17,			// string: classname надетого костюма ("" - роба)
	RPG_REC_SERVER_ONLY = 100,	// >= 100 - только в сохранении (счётчики убийств и т.п.)
	RPG_REC_KILLCOUNT = 100,	// key classname, убито за текущий игровой день
	RPG_REC_SERVER_STAT = 101,	// key имя, число (clock_prev x100, kill_day)
};

inline bool RPGRecordHasString( int type )
{
	return type == RPG_REC_NAME || type == RPG_REC_FLAG_STR || type == RPG_REC_ARMOR;
}

struct RPGRecord_t
{
	int type;
	CUtlString key;
	int iValue;
	CUtlString sValue;

	RPGRecord_t() : type( RPG_REC_NONE ), iValue( 0 ) {}
	RPGRecord_t( int t, const char *k, int v, const char *s = NULL ) : type( t ), key( k ? k : "" ), iValue( v ), sValue( s ? s : "" ) {}
};

#define RPG_STATE_MSG_MAX_BYTES		220
#define RPG_STRING_VALUE_MAX		160		// длиннее строковые значения обрезаются

// ---------------------------------------------------------------------------
// User message RPG_Notify: byte kind, string token, long value, string extra
// ---------------------------------------------------------------------------
enum RPGNotifyKind_t
{
	RPG_NOTIFY_GENERIC = 0,		// token = токен локализации, extra = параметр %s1
	RPG_NOTIFY_XP = 1,			// value = +XP, extra = источник (kill/check/quest/location/note/...)
	RPG_NOTIFY_LEVELUP = 2,		// value = новый уровень, extra = "skillpoints perkpicks"
	RPG_NOTIFY_REP = 3,			// token = faction id, value = изменение, extra = ключ уровня
	RPG_NOTIFY_CIVIL = 4,		// value = изменение, token = ключ уровня ГС
	RPG_NOTIFY_SUSPICION = 5,	// смена уровня подозрения: token = ключ уровня, value = подозрение
	RPG_NOTIFY_LOCATION = 6,	// token = location id, value = XP
	RPG_NOTIFY_ITEM = 7,		// token = classname, value = количество (<0 - забрали)
	RPG_NOTIFY_QUEST = 8,		// token = quest id, extra = started/completed/failed/stage, value = стадия
	RPG_NOTIFY_NOTE = 9,		// token = note id (новая запись в «Заметках»)
	RPG_NOTIFY_CREDITS = 10,	// value = изменение
	RPG_NOTIFY_HUMANITY = 11,	// value = изменение (показывать ли - решает UI)
	RPG_NOTIFY_PERK = 12,		// token = perk id, value = ранг
	RPG_NOTIFY_CHECK = 13,		// результат проверки: token = подпись, value = 1/0, extra = вид
	RPG_NOTIFY_ZONE = 14,		// value = новая зона (0/1/2)
	RPG_NOTIFY_FAIL = 15,		// отказ действия: token = токен причины, extra = параметр
	RPG_NOTIFY_OPEN_UI = 20,	// token = экран (chargen/levelup/barter/note/terminal/read/sleep),
								// value = entindex источника, extra = параметр (trader id, note id, часы)
};

// ---------------------------------------------------------------------------
// Ключи уровней (для токенов) - индексы = перечисления выше
// ---------------------------------------------------------------------------
extern const char *g_pszRPGAttrIds[RPG_ATTR_COUNT];
extern const char *g_pszRPGAttrAbbr[RPG_ATTR_COUNT];
extern const char *g_pszRPGCivilTierKeys[RPG_CIVIL_COUNT];
extern const char *g_pszRPGFactionTierKeys[RPG_FTIER_COUNT];
extern const char *g_pszRPGHumanityTierKeys[RPG_HTIER_COUNT];
extern const char *g_pszRPGSuspicionTierKeys[RPG_STIER_COUNT];
extern const char *g_pszRPGSuitTypeKeys[RPG_SUIT_COUNT];

#define RPG_CIVIL_TEMPPASS_KEY	"temppass"
#define RPG_FLAG_TEMPPASS		"civil_temp_pass"
#define RPG_FLAG_ACT			"world_act"
#define RPG_FLAG_DAY			"world_day"

int RPG_SuitTypeFromKey( const char *pszKey );	// "cp" -> RPG_SUIT_CP, -1 если неизвестно

#endif // RPG_SHARED_H

//========= HL2RPM ============================================================//
//
// Character rules of the RPG (see hl2rpm_rpgdefs.h).
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "KeyValues.h"
#include "filesystem.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define RPM_RPG_FILE "scripts/rpg/rpg.txt"

namespace RPMRPG
{
	struct Builtin_t { const char *id, *stat, *en, *ru, *desc_en, *desc_ru; };

	static const Builtin_t s_Stats[RPM_STAT_COUNT] =
	{
		{ "str", "", "Strength", "Сила", "Raw physical power: melee damage and how much you can carry.", "Физическая мощь: урон в ближнем бою и сколько можно унести." },
		{ "per", "", "Perception", "Восприятие", "Senses and awareness: energy weapons, explosives, lockpicking.", "Чутьё и внимательность: оружие Альянса, взрывчатка, взлом." },
		{ "end", "", "Endurance", "Выносливость", "Stamina and toughness: health and survival.", "Стойкость и здоровье: запас здоровья и выживание." },
		{ "cha", "", "Charisma", "Обаяние", "Charm and presence: speech and barter.", "Умение располагать к себе: красноречие и бартер." },
		{ "int", "", "Intelligence", "Интеллект", "Knowledge and wit: science, repair, medicine, skill points per level.", "Знания и сообразительность: наука, ремонт, медицина, очки навыков за уровень." },
		{ "agi", "", "Agility", "Ловкость", "Coordination and speed: guns and sneak.", "Координация и скорость: огнестрел и скрытность." },
		{ "lck", "", "Luck", "Удача", "Fortune: critical hits and a little of every skill.", "Везение: критические попадания и понемногу каждый навык." },
	};

	static const Builtin_t s_Skills[RPM_SKILL_COUNT] =
	{
		{ "guns", "agi", "Guns", "Огнестрел", "Pistols, SMGs, shotguns, rifles.", "Пистолеты, пистолеты-пулемёты, дробовики, винтовки." },
		{ "energy", "per", "Combine Weapons", "Оружие Альянса", "Pulse rifles and other Combine technology.", "Импульсные винтовки и прочее оружие Альянса." },
		{ "melee", "str", "Melee", "Ближний бой", "Crowbars, batons, knives and fists.", "Монтировки, дубинки, ножи и кулаки." },
		{ "explosives", "per", "Explosives", "Взрывчатка", "Grenades, rockets, mines.", "Гранаты, ракеты, мины." },
		{ "lockpick", "per", "Lockpick", "Взлом", "Opening locked doors and containers.", "Вскрытие запертых дверей и контейнеров." },
		{ "science", "int", "Science", "Наука", "Hacking terminals and Combine machines.", "Взлом терминалов и машин Альянса." },
		{ "repair", "int", "Repair", "Ремонт", "Fixing gear and machines.", "Починка снаряжения и механизмов." },
		{ "medicine", "int", "Medicine", "Медицина", "How much medkits and food heal.", "Сколько лечат аптечки и еда." },
		{ "sneak", "agi", "Sneak", "Скрытность", "Staying unseen, stealing.", "Оставаться незамеченным, воровать." },
		{ "speech", "cha", "Speech", "Красноречие", "Persuading people in dialogs.", "Убеждать людей в разговоре." },
		{ "barter", "cha", "Barter", "Бартер", "Better prices when trading.", "Выгодные цены в торговле." },
		{ "survival", "end", "Survival", "Выживание", "Living off the land of the occupied City.", "Выживать в оккупированном Городе." },
	};

	static const struct { const char *key; float value; } s_Rules[] =
	{
		{ "stat_total", 40 }, { "stat_min", 1 }, { "stat_max", 10 }, { "stat_start", 5 },
		{ "skill_base", 2 }, { "skill_per_stat", 2 }, { "skill_luck_div", 2 }, { "tag_bonus", 15 }, { "tags", 3 }, { "skill_max", 100 },
		{ "points_base", 10 }, { "points_per_int", 0.5f },
		{ "level_max", 30 }, { "perk_every", 2 }, { "traits_max", 2 }, { "xp_scale", 1 },
		{ "hp_base", 50 }, { "hp_per_end", 10 }, { "hp_per_level", 3 },
		{ "carry_base", 25 }, { "carry_per_str", 3 },
		{ "dmg_min", 0.75f }, { "dmg_max", 1.25f }, { "skill_req_penalty", 0.25f },
		{ "spread_min", 1.5f }, { "spread_max", 0.6f },
		{ "crit_per_luck", 1 }, { "crit_mult", 1.5f },
		{ "heal_per_medicine", 0.005f }, { "barter_per_point", 0.002f },
		{ "xp_lockpick", 10 }, { "xp_hack", 10 }, { "xp_check", 10 }, { "xp_kill", 10 },
		{ "rep_kill", 25 }, { "rep_hostile", -50 }, { "rep_theft", 5 },
		// behaviour of NPCs (hl2rpm_world.cpp, hl2rpm_weaponlaw.cpp)
		{ "npc_yield", 0 }, { "npc_pickup_weapons", 0 }, { "npc_talk_gestures", 1 },
		{ "weapon_law", 1 }, { "weapon_law_grace", 5 }, { "weapon_law_melee", 0 }, { "weapon_law_dist", 1000 }, { "weapon_law_disguise", 1 },
	};

	static KeyValues *g_pRoot = NULL;
	static bool g_bLoaded = false;
	static long g_FileTime = 0;

	void Load( bool bForce )
	{
		static double s_flNextCheck = 0;
		double now = Plat_FloatTime();
		if ( g_bLoaded && !bForce && now < s_flNextCheck )
			return;
		s_flNextCheck = now + 1.0;
		long t = filesystem->GetFileTime( RPM_RPG_FILE, "GAME" );
		if ( g_bLoaded && !bForce && t == g_FileTime )
			return;
		g_bLoaded = true;
		g_FileTime = t;
		if ( g_pRoot )
			g_pRoot->deleteThis();
		g_pRoot = new KeyValues( "RPG" );
		if ( !g_pRoot->LoadFromFile( filesystem, RPM_RPG_FILE, "GAME" ) )
			g_pRoot->Clear();
	}

	static KeyValues *Root()
	{
		Load();
		return g_pRoot;
	}

	float Rule( const char *key )
	{
		KeyValues *r = Root()->FindKey( "rules" );
		if ( r && r->FindKey( key ) )
			return r->GetFloat( key );
		for ( int i = 0; i < ARRAYSIZE( s_Rules ); i++ )
			if ( !Q_stricmp( s_Rules[i].key, key ) )
				return s_Rules[i].value;
		return 0.0f;
	}

	int StatCount() { return RPM_STAT_COUNT; }
	const char *StatId( int i ) { return ( i >= 0 && i < RPM_STAT_COUNT ) ? s_Stats[i].id : ""; }
	int SkillCount() { return RPM_SKILL_COUNT; }
	const char *SkillId( int i ) { return ( i >= 0 && i < RPM_SKILL_COUNT ) ? s_Skills[i].id : ""; }

	int StatIndex( const char *id )
	{
		for ( int i = 0; id && i < RPM_STAT_COUNT; i++ )
			if ( !Q_stricmp( s_Stats[i].id, id ) )
				return i;
		return -1;
	}

	int SkillIndex( const char *id )
	{
		for ( int i = 0; id && i < RPM_SKILL_COUNT; i++ )
			if ( !Q_stricmp( s_Skills[i].id, id ) )
				return i;
		return -1;
	}

	static KeyValues *Block( const char *section, const char *id )
	{
		KeyValues *s = Root()->FindKey( section );
		return ( s && id && id[0] ) ? s->FindKey( id ) : NULL;
	}

	const char *SkillStat( const char *skill )
	{
		KeyValues *k = Block( "skills", skill );
		if ( k && k->GetString( "stat" )[0] )
			return k->GetString( "stat" );
		int i = SkillIndex( skill );
		return i >= 0 ? s_Skills[i].stat : "";
	}

	const char *LocString( KeyValues *pKV, const char *key )
	{
		if ( !pKV )
			return "";
		CFmtStr k( "%s_%s", key, RPMItems::Language() );
		if ( pKV->FindKey( k ) )
			return pKV->GetString( k, "" );
		return pKV->GetString( key, "" );
	}

	static bool IsRussian()
	{
		return !Q_stricmp( RPMItems::Language(), "russian" );
	}

	static const char *Text( const char *id, bool bDesc )
	{
		const char *key = bDesc ? "desc" : "name";
		static const char *sections[] = { "stats", "skills", "perks", "traits" };
		for ( int s = 0; s < ARRAYSIZE( sections ); s++ )
		{
			const char *t = LocString( Block( sections[s], id ), key );
			if ( t[0] )
				return t;
		}
		int i = StatIndex( id );
		if ( i >= 0 )
			return bDesc ? ( IsRussian() ? s_Stats[i].desc_ru : s_Stats[i].desc_en ) : ( IsRussian() ? s_Stats[i].ru : s_Stats[i].en );
		i = SkillIndex( id );
		if ( i >= 0 )
			return bDesc ? ( IsRussian() ? s_Skills[i].desc_ru : s_Skills[i].desc_en ) : ( IsRussian() ? s_Skills[i].ru : s_Skills[i].en );
		return bDesc ? "" : id;
	}

	const char *Name( const char *id ) { return Text( id, false ); }
	const char *Desc( const char *id ) { return Text( id, true ); }

	KeyValues *Perks() { return Root()->FindKey( "perks", true ); }
	KeyValues *Traits() { return Root()->FindKey( "traits", true ); }
	KeyValues *Perk( const char *id ) { return Block( "perks", id ); }
	KeyValues *Trait( const char *id ) { return Block( "traits", id ); }

	int XPForLevel( int level )
	{
		if ( level <= 1 )
			return 0;
		return (int)( 25.0f * ( 3 * level + 2 ) * ( level - 1 ) * Max( 0.05f, Rule( "xp_scale" ) ) );
	}
}

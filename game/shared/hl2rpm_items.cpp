//========= HL2RPM ============================================================//
//
// Item database of the RPG (see hl2rpm_items.h).
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_items.h"
#include "KeyValues.h"
#include "filesystem.h"
#include "utldict.h"
#include "steam/steam_api.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define RPM_ITEMS_FILE "scripts/rpg/items.txt"

namespace RPMItems
{
	static CUtlDict<RPMItemDef_t *, int> g_Defs;
	static CUtlVector<RPMItemDef_t *> g_List;
	static KeyValues *g_pRoot = NULL;
	static bool g_bLoaded = false;
	static long g_FileTime = 0;

	const char *Language()
	{
		// singleplayer: the server's dialog_language lives in the same process as the client
		static ConVarRef dialog_language( "dialog_language" );
		static ConVarRef cl_language( "cl_language" );
		const char *lang = dialog_language.IsValid() ? dialog_language.GetString() : "";
		if ( !lang[0] && cl_language.IsValid() )
			lang = cl_language.GetString();
#ifndef NO_STEAM
		if ( !lang[0] && steamapicontext && steamapicontext->SteamApps() )
			lang = steamapicontext->SteamApps()->GetCurrentGameLanguage();
#endif
		return ( lang && lang[0] ) ? lang : "english";
	}

	static void Clear()
	{
		g_Defs.RemoveAll();
		g_List.PurgeAndDeleteElements();
		if ( g_pRoot )
		{
			g_pRoot->deleteThis();
			g_pRoot = NULL;
		}
	}

	void Load( bool bForce )
	{
		// the Creation Kit rewrites the file while the game runs (F8): look at its time,
		// but not on every lookup
		static double s_flNextCheck = 0;
		double now = Plat_FloatTime();
		if ( g_bLoaded && !bForce && now < s_flNextCheck )
			return;
		s_flNextCheck = now + 1.0;
		long t = filesystem->GetFileTime( RPM_ITEMS_FILE, "GAME" );
		if ( g_bLoaded && !bForce && t == g_FileTime )
			return;
		Clear();
		g_bLoaded = true;
		g_FileTime = t;
		g_pRoot = new KeyValues( "Items" );
		if ( !g_pRoot->LoadFromFile( filesystem, RPM_ITEMS_FILE, "GAME" ) )
			return;
		for ( KeyValues *k = g_pRoot->GetFirstTrueSubKey(); k; k = k->GetNextTrueSubKey() )
		{
			RPMItemDef_t *d = new RPMItemDef_t;
			V_memset( d, 0, sizeof( *d ) );
			Q_strncpy( d->id, k->GetName(), sizeof( d->id ) );
			Q_strlower( d->id );
			Q_strncpy( d->category, k->GetString( "category", "misc" ), sizeof( d->category ) );
			Q_strncpy( d->base, k->GetString( "base", "" ), sizeof( d->base ) );
			Q_strncpy( d->model, k->GetString( "model", "" ), sizeof( d->model ) );
			d->weight = k->GetFloat( "weight", -1.0f );
			d->value = k->GetInt( "value", 0 );
			d->heal = k->GetInt( "heal", 0 );
			d->stackable = k->GetBool( "stackable", !Q_stricmp( d->base, "currency" ) );
			d->quest = k->GetBool( "quest", false );
			d->damage = k->GetFloat( "damage", 0.0f );
			d->fireRate = k->GetFloat( "fire_rate", 0.0f );
			d->clip = k->GetInt( "clip", 0 );
			Q_strncpy( d->skill, k->GetString( "skill", "" ), sizeof( d->skill ) );
			d->skillReq = k->GetInt( "skill_req", 0 );
			d->pKV = k;
			g_List.AddToTail( d );
			if ( g_Defs.Find( d->id ) == g_Defs.InvalidIndex() )
				g_Defs.Insert( d->id, d );
		}
		DevMsg( "RPMItems: %d items from %s\n", g_List.Count(), RPM_ITEMS_FILE );
	}

	const RPMItemDef_t *Find( const char *id )
	{
		if ( !id || !id[0] )
			return NULL;
		Load();
		char low[64];
		Q_strncpy( low, id, sizeof( low ) );
		Q_strlower( low );
		int i = g_Defs.Find( low );
		return i == g_Defs.InvalidIndex() ? NULL : g_Defs[i];
	}

	int Count()
	{
		Load();
		return g_List.Count();
	}

	const RPMItemDef_t *Get( int i )
	{
		return ( i >= 0 && i < g_List.Count() ) ? g_List[i] : NULL;
	}

	const char *Text( const RPMItemDef_t *pDef, const char *key )
	{
		if ( !pDef || !pDef->pKV )
			return "";
		char k[96];
		Q_snprintf( k, sizeof( k ), "%s_%s", key, Language() );
		const char *v = pDef->pKV->GetString( k, NULL );
		if ( v && v[0] )
			return v;
		return pDef->pKV->GetString( key, "" );
	}

	// names and prices of the stock HL2 items for a project whose item database does not have them
	// (price of ammo - per bullet; 0 - not sold: the gravity gun, the suit)
	struct StockName_t { const char *id; const char *en; const char *ru; int value; const char *model; };
	static const StockName_t s_StockNames[] =
	{
		{ "weapon_crowbar", "Crowbar", "Монтировка", 15, "models/weapons/w_crowbar.mdl" },
		{ "weapon_stunstick", "Stunstick", "Электродубинка", 40, "models/weapons/w_stunbaton.mdl" },
		// the melee weapons of the HL2 Melee Pack (hl2rpm_melee.cpp)
		{ "weapon_rpm_axe", "Axe", "Топор", 30, "models/weapons/hl2rpm/w_axe.mdl" },
		{ "weapon_rpm_bottle", "Bottle", "Бутылка", 2, "models/weapons/hl2rpm/w_bottle.mdl" },
		{ "weapon_rpm_brokenbottle", "Broken bottle", "Розочка", 3, "models/weapons/hl2rpm/w_brokenbottle.mdl" },
		{ "weapon_rpm_hook", "Hook", "Крюк", 25, "models/weapons/hl2rpm/w_hook.mdl" },
		{ "weapon_rpm_pan", "Frying pan", "Сковорода", 8, "models/weapons/hl2rpm/w_pan.mdl" },
		{ "weapon_rpm_pickaxe", "Pickaxe", "Кирка", 28, "models/weapons/hl2rpm/w_pickaxe.mdl" },
		{ "weapon_rpm_pipe", "Pipe", "Труба", 10, "models/weapons/hl2rpm/w_pipe.mdl" },
		{ "weapon_rpm_pot", "Pot", "Кастрюля", 6, "models/weapons/hl2rpm/w_pot.mdl" },
		{ "weapon_rpm_shovel", "Shovel", "Лопата", 18, "models/weapons/hl2rpm/w_shovel.mdl" },
		{ "weapon_pistol", "USP Match pistol", "Пистолет USP Match", 60, "models/weapons/w_pistol.mdl" },
		// the Beretta 92 (hl2rpm_beretta.cpp, make_beretta.py)
		{ "weapon_rpm_beretta", "Beretta 92", "Беретта 92", 90, "models/weapons/hl2rpm/w_beretta.mdl" },
		{ "weapon_357", ".357 Magnum", "Револьвер .357", 150, "models/weapons/w_357.mdl" },
		{ "weapon_smg1", "MP7 SMG", "Пистолет-пулемёт MP7", 120, "models/weapons/w_smg1.mdl" },
		{ "weapon_ar2", "Pulse rifle", "Импульсная винтовка", 250, "models/weapons/w_irifle.mdl" },
		{ "weapon_shotgun", "Shotgun", "Дробовик", 140, "models/weapons/w_shotgun.mdl" },
		{ "weapon_crossbow", "Crossbow", "Арбалет", 200, "models/weapons/w_crossbow.mdl" },
		{ "weapon_frag", "Grenade", "Граната", 20, "models/weapons/w_grenade.mdl" },
		{ "weapon_rpg", "Rocket launcher", "Ракетомёт", 350, "models/weapons/w_rocket_launcher.mdl" },
		{ "weapon_physcannon", "Gravity gun", "Гравипушка", 0, "models/weapons/w_physics.mdl" },
		{ "weapon_bugbait", "Bugbait", "Феропод", 0, "models/weapons/w_bugbait.mdl" },
		{ "item_healthkit", "Health kit", "Аптечка", 25, "models/items/healthkit.mdl" },
		{ "item_healthvial", "Health vial", "Флакон здоровья", 10, "models/healthvial.mdl" },
		{ "item_battery", "Suit battery", "Батарея костюма", 20, "models/items/battery.mdl" },
		{ "item_suit", "H.E.V. suit", "Костюм H.E.V.", 0, "models/hl2rpm/items/suit_hev.mdl" },
		{ "item_suit_pcv", "P.C.V. suit", "Костюм P.C.V.", 0, "models/hl2rpm/items/suit_pcv.mdl" },
		{ "item_suit_cp", "Civil Protection uniform", "Форма Гражданской обороны", 0, "models/hl2rpm/items/suit_cp.mdl" },
		{ "item_combine_soldier_suit", "Overwatch soldier armor", "Броня солдата Надзора", 0, "models/hl2rpm/items/suit_soldier.mdl" },
		{ "item_suit_prison", "Nova Prospekt guard armor", "Броня охраны Нова Проспект", 0, "models/hl2rpm/items/suit_prison.mdl" },
		{ "item_suit_elite", "Overwatch elite armor", "Броня элиты Надзора", 0, "models/hl2rpm/items/suit_elite.mdl" },
		{ "item_suit_worker", "Combine worker's coverall", "Роба рабочего Альянса", 0, "models/hl2rpm/items/suit_worker.mdl" },
		{ "item_suit_hazmat", "Hazmat suit", "Костюм химзащиты", 0, "models/hl2rpm/items/suit_hazmat.mdl" },
		{ "item_ammo_pistol", "9mm rounds", "Патроны 9 мм", 1, "models/items/boxsrounds.mdl" },
		{ "item_box_srounds", "9mm rounds", "Патроны 9 мм", 1, "models/items/boxsrounds.mdl" },
		{ "item_large_box_srounds", "9mm rounds", "Патроны 9 мм", 1, "models/items/boxsrounds.mdl" },
		{ "item_ammo_smg1", "4.6mm rounds", "Патроны 4,6 мм", 1, "models/items/boxmrounds.mdl" },
		{ "item_box_mrounds", "4.6mm rounds", "Патроны 4,6 мм", 1, "models/items/boxmrounds.mdl" },
		{ "item_large_box_mrounds", "4.6mm rounds", "Патроны 4,6 мм", 1, "models/items/boxmrounds.mdl" },
		{ "item_ammo_ar2", "Pulse cells", "Энергоячейки AR2", 3, "models/items/combine_rifle_cartridge01.mdl" },
		{ "item_box_lrounds", "Pulse cells", "Энергоячейки AR2", 3, "models/items/combine_rifle_cartridge01.mdl" },
		{ "item_large_box_lrounds", "Pulse cells", "Энергоячейки AR2", 3, "models/items/combine_rifle_cartridge01.mdl" },
		{ "item_ammo_357", ".357 rounds", "Патроны .357", 4, "models/items/357ammo.mdl" },
		{ "item_box_buckshot", "Buckshot", "Дробь 12 калибра", 2, "models/items/boxbuckshot.mdl" },
		{ "item_ammo_crossbow", "Crossbow bolts", "Арбалетные болты", 8, "models/items/crossbowrounds.mdl" },
		{ "item_rpg_round", "Rocket", "Ракета", 60, "models/weapons/w_missile_closed.mdl" },
		{ "item_ammo_smg1_grenade", "SMG grenade", "Подствольная граната", 30, "models/items/ar2_grenade.mdl" },
		{ "item_ammo_ar2_altfire", "Energy ball", "Энергошар", 40, "models/items/combine_rifle_ammo01.mdl" },
	};

	// the suits: what stops a hit (percent, as the "dr" effect of a perk) and what the player looks like in them
	struct SuitInfo_t { const char *id; int defense; const char *body; int skin; const char *en; const char *ru; };
	static const SuitInfo_t s_Suits[] =
	{
		{ "item_suit", 15, "models/hl2rpm/characters/hev_mk5.mdl", 0, "Hazardous Environment Suit, Mark V. Health and armor on the display, a flashlight, an air supply.",
			"Защитный костюм H.E.V., пятая модель. Показывает здоровье и броню, питает фонарь, держит запас воздуха." },
		{ "item_suit_pcv", 20, "models/hl2rpm/characters/hev_mk5.mdl", 1, "Powered Combat Vest, a prototype for the military. Heavier plates than the H.E.V., fewer systems.",
			"Боевой жилет P.C.V., опытный образец для армии. Пластины тяжелее, чем у H.E.V., систем меньше." },
		{ "item_suit_cp", 10, "models/police.mdl", 0, "The uniform and the mask of a Civil Protection unit. Light, and it opens doors that a citizen wouldn't go near.",
			"Форма и маска юнита Гражданской обороны. Лёгкая, и перед ней открываются двери, к которым гражданин не подойдёт." },
		{ "item_combine_soldier_suit", 20, "models/combine_soldier.mdl", 0, "Armor of an Overwatch soldier, wired to the network of the Combine.",
			"Броня солдата Надзора, подключённая к сети Альянса." },
		{ "item_suit_prison", 20, "models/combine_soldier_prisonguard.mdl", 0, "Armor of the Nova Prospekt guard: the soldier's armor, cut for the cell blocks.",
			"Броня охраны Нова Проспект: солдатская броня, подогнанная для тюремных блоков." },
		{ "item_suit_elite", 30, "models/combine_super_soldier.mdl", 0, "White armor of the Overwatch elite. What was in it is better left unknown.",
			"Белая броня элиты Надзора. Что было внутри, лучше не знать." },
		{ "item_suit_worker", 5, "models/puntleycmb/workernpc.mdl", 0, "The coverall, the hard hat and the respirator of a Combine worker. Not armor - a pass to the work zones and air you can breathe there.",
			"Роба, каска и респиратор рабочего Альянса. Не броня - пропуск в рабочие зоны и воздух, которым там можно дышать." },
		{ "item_suit_hazmat", 8, "models/hl2_combine_hazmat_cit.mdl", 0, "A sealed suit for the toxic zones: a mask, a hood, an air tank and a dosimeter on the display.",
			"Герметичный костюм для заражённых зон: маска, капюшон, баллон с воздухом и дозиметр на дисплее." },
	};

	static const SuitInfo_t *FindSuit( const char *id )
	{
		for ( int i = 0; id && i < ARRAYSIZE( s_Suits ); i++ )
			if ( !Q_stricmp( id, s_Suits[i].id ) )
				return &s_Suits[i];
		return NULL;
	}

	int SuitDefense( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		if ( d && d->pKV && d->pKV->FindKey( "defense" ) )
			return d->pKV->GetInt( "defense" );
		const SuitInfo_t *s = FindSuit( id );
		return s ? s->defense : 0;
	}

	const char *SuitBody( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		const char *m = d && d->pKV ? d->pKV->GetString( "suit_body" ) : "";
		if ( m[0] )
			return m;
		const SuitInfo_t *s = FindSuit( id );
		return s ? s->body : "";
	}

	// the skin of that body (the H.E.V. and the P.C.V. are one model: hl2rpm/characters/hev_mk5)
	int SuitBodySkin( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		if ( d && d->pKV && d->pKV->FindKey( "suit_body_skin" ) )
			return d->pKV->GetInt( "suit_body_skin" );
		if ( d && d->pKV && d->pKV->GetString( "suit_body" )[0] )
			return 0;
		const SuitInfo_t *s = FindSuit( id );
		return s ? s->skin : 0;
	}

	// the first-person hands of a suit: the model (the weapons with "uses_hands") and the painting
	// of the stock view models' arms (v_hand_sheet_<vm>, make_vm_hands.py; client/hl2rpm_vmhands.cpp)
	static const struct { const char *suit; const char *hands; int skin; const char *vm; } s_Hands[] =
	{
		{ "", "models/hl2rpm/hands/citizen.mdl", 0, "citizen" },
		{ "item_suit", "models/hl2rpm/hands/hev.mdl", 0, "hev" },
		{ "item_suit_pcv", "models/hl2rpm/hands/hev.mdl", 1, "pcv" },
		{ "item_suit_cp", "models/hl2rpm/hands/cp.mdl", 0, "cp" },
		{ "item_combine_soldier_suit", "models/hl2rpm/hands/soldier.mdl", 0, "soldier" },
		{ "item_suit_prison", "models/hl2rpm/hands/prison.mdl", 0, "prison" },
		{ "item_suit_elite", "models/hl2rpm/hands/elite.mdl", 0, "elite" },
		{ "item_suit_worker", "models/hl2rpm/hands/worker.mdl", 0, "worker" },
		{ "item_suit_hazmat", "models/hl2rpm/hands/hazmat.mdl", 0, "hazmat" },
	};

	const char *SuitViewHands( const char *id, int theme )
	{
		if ( !id )
			id = "";
		const RPMItemDef_t *d = id[0] ? Find( id ) : NULL;
		if ( d && d->pKV && d->pKV->GetString( "suit_vmhands" )[0] )
			return d->pKV->GetString( "suit_vmhands" );
		for ( int i = 0; i < ARRAYSIZE( s_Hands ); i++ )
			if ( !Q_stricmp( s_Hands[i].suit, id ) )
				return s_Hands[i].vm;
		switch ( theme )		// hl2rpm_ui.h: 2 PCV, 3 Combine, 4 elite, 5 worker, 6 hazmat
		{
		case 2: return "pcv";
		case 3: return "soldier";
		case 4: return "elite";
		case 5: return "worker";
		case 6: return "hazmat";
		}
		return id[0] ? "hev" : "citizen";
	}

	const char *SuitHands( const char *id, int theme, int &skin )
	{
		skin = 0;
		if ( !id )
			id = "";
		const RPMItemDef_t *d = id[0] ? Find( id ) : NULL;
		if ( d && d->pKV && d->pKV->GetString( "suit_hands" )[0] )
		{
			skin = d->pKV->GetInt( "suit_hands_skin" );
			return d->pKV->GetString( "suit_hands" );
		}
		for ( int i = 0; i < ARRAYSIZE( s_Hands ); i++ )
			if ( !Q_stricmp( s_Hands[i].suit, id ) )
			{
				skin = s_Hands[i].skin;
				return s_Hands[i].hands;
			}
		switch ( theme )		// hl2rpm_ui.h: 2 PCV, 3 Combine, 4 elite
		{
		case 2: skin = 1; return "models/hl2rpm/hands/hev.mdl";
		case 3: return "models/hl2rpm/hands/soldier.mdl";
		case 4: return "models/hl2rpm/hands/elite.mdl";
		case 5: return "models/hl2rpm/hands/worker.mdl";
		case 6: return "models/hl2rpm/hands/hazmat.mdl";
		}
		return "models/hl2rpm/hands/hev.mdl";
	}

	void HandsModels( CUtlVector<const char *> &models )
	{
		for ( int i = 0; i < ARRAYSIZE( s_Hands ); i++ )
			if ( models.Find( s_Hands[i].hands ) < 0 )
				models.AddToTail( s_Hands[i].hands );
	}

	void SuitBodies( CUtlVector<const char *> &models )
	{
		for ( int i = 0; i < ARRAYSIZE( s_Suits ); i++ )
			if ( s_Suits[i].body[0] )
				models.AddToTail( s_Suits[i].body );
	}

	static const StockName_t *FindStock( const char *id )
	{
		if ( !id )
			return NULL;
		// "_large" boxes are like the small ones
		char base[64];
		Q_strncpy( base, id, sizeof( base ) );
		int len = Q_strlen( base );
		if ( len > 6 && !Q_stricmp( base + len - 6, "_large" ) )
			base[len - 6] = 0;
		for ( int i = 0; i < ARRAYSIZE( s_StockNames ); i++ )
			if ( !Q_stricmp( base, s_StockNames[i].id ) )
				return &s_StockNames[i];
		return NULL;
	}

	int Value( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		if ( d && d->value > 0 )
			return d->value;
		if ( id && !Q_stricmp( id, RPM_ITEM_BOBBYPIN ) )
			return 2;
		const StockName_t *s = d ? NULL : FindStock( id );
		return s ? s->value : 0;
	}

	const char *Model( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		if ( d && d->model[0] )
			return d->model;
		if ( id && !Q_stricmp( id, RPM_ITEM_BOBBYPIN ) )
			return "models/hl2rpm/items/bobby_pin.mdl";
		if ( const StockName_t *s = FindStock( id ) )
			return s->model;
		// an item of the database without a model
		if ( d && !Q_stricmp( d->base, "key" ) )
			return "models/hl2rpm/items/key_flat.mdl";
		return d && d->base[0] && Q_stricmp( d->base, "currency" ) ? "models/props_lab/clipboard.mdl" : "";
	}

	const char *Name( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		const char *n = d ? Text( d, "name" ) : "";
		if ( n[0] )
			return n;
		// the default currency and bobby pins are built in (a project without an item database still has them)
		if ( id && !Q_stricmp( id, "rpm_credits" ) )
			return !Q_stricmp( Language(), "russian" ) ? "Жетоны" : "Tokens";
		if ( id && !Q_stricmp( id, RPM_ITEM_BOBBYPIN ) )
			return !Q_stricmp( Language(), "russian" ) ? "Шпилька" : "Bobby pin";
		// stock items
		if ( const StockName_t *st = FindStock( id ) )
			return !Q_stricmp( Language(), "russian" ) ? st->ru : st->en;
		// weapon_pistol -> Pistol, item_ammo_smg1 -> Ammo smg1
		static char buf[96];
		const char *s = id ? id : "";
		if ( !Q_strnicmp( s, "weapon_", 7 ) )
			s += 7;
		else if ( !Q_strnicmp( s, "item_", 5 ) )
			s += 5;
		Q_strncpy( buf, s, sizeof( buf ) );
		for ( char *p = buf; *p; ++p )
			if ( *p == '_' )
				*p = ' ';
		if ( buf[0] >= 'a' && buf[0] <= 'z' )
			buf[0] -= 32;
		return buf;
	}

	bool IsBuiltin( const char *id )
	{
		return id && ( !Q_stricmp( id, "rpm_credits" ) || !Q_stricmp( id, RPM_ITEM_BOBBYPIN ) );
	}

	bool IsGeneric( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		return d ? d->base[0] != 0 : IsBuiltin( id );
	}

	bool IsStackable( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		return d ? d->stackable : IsBuiltin( id );
	}

	const char *Desc( const char *id )
	{
		const RPMItemDef_t *d = Find( id );
		const char *s = d ? Text( d, "desc" ) : "";
		if ( s[0] || !id )
			return s;
		bool ru = !Q_stricmp( Language(), "russian" );
		if ( const SuitInfo_t *pSuit = FindSuit( id ) )
			return ru ? pSuit->ru : pSuit->en;
		if ( !Q_stricmp( id, RPM_ITEM_BOBBYPIN ) )
			return ru ? "Отмычка для замков: шпилька подбирает положение, замок поворачивается в «сладкой точке». Ломается, если давить мимо."
				: "Picks locks: the pin finds the spot, the lock turns there. Breaks if you force it elsewhere.";
		return s;
	}
}

#ifdef GAME_DLL
CON_COMMAND( rpm_items_reload, "HL2RPM: reload scripts/rpg/items.txt" )
{
	RPMItems::Load( true );
	Msg( "RPMItems: %d items\n", RPMItems::Count() );
}
#endif

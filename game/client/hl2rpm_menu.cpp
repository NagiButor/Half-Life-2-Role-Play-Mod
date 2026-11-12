//========= HL2RPM ============================================================//
//
// The character menu (M6) - the Pip-Boy of the mod, in the look of hl2rpm_ui.h:
// ПЕРСОНАЖ (attributes, skills, perks and traits, reputation), ИНВЕНТАРЬ (categories,
// the card of an item, use / drop / take the suit off), ЗАДАНИЯ (active, finished),
// ЗАПИСКИ (notes of the item database). A status line at the bottom: name, level and
// experience, health, weight, money.
//
// Data: the character state (hl2rpm_charpanel.cpp, user message RPMChar) and what the
// inventory window already receives (items, weight, quests - clientmode_shared.cpp);
// actions are the commands of the inventory window (inventory_use / _drop / _unequip_suit).
//
//   Tab / I (inventory_request_local), rpm_journal, rpm_character -> this menu
//   rpm_menu [char|inv|quests|notes]     cl_rpm_menu 0 = the old inventory window
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_rpgdefs.h"
#include "hl2rpm_items.h"
#include "hl2rpm_itempreview.h"
#include "vgui_inventorypanel.h"
#include "vgui_lootpanel.h"
#include "iclientmode.h"
#include "timecycle/env_timecycle.h"
#include "weather/weather_shared.h"
#include "weather/c_weather_system.h"
#include "quest_netmessages.h"
#include "filesystem.h"
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/ILocalize.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Panel.h>
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
void InventoryClient_PlayPanelSound( bool bOpen );

using namespace RPMUI;

extern KeyValues *RPMChar_ClientState();
extern bool HL2RPM_IsDialogOpen();

ConVar cl_rpm_menu( "cl_rpm_menu", "1", FCVAR_ARCHIVE, "HL2RPM: the windows of the new look - Tab opens the character menu, loot and trade (0 = the old inventory and loot windows)" );

static ConVar cl_rpm_item_model_spin( "cl_rpm_item_model_spin", "30", 0, "HL2RPM: the model on the card of an item turns by this many degrees a second" );

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }

// the tracked quest (its markers on the HUD), told by the server: quest_system.cpp SendMarkers
static CUtlString s_TrackedQuest;
CON_COMMAND( rpm_quest_tracked, "HL2RPM (server -> client): rpm_quest_tracked \"<quest id>\" - the tracked quest (\"\" - none)" )
{
	s_TrackedQuest = args.ArgC() > 1 ? args[1] : "";
}
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

enum MenuTab_t { TAB_CHAR, TAB_INV, TAB_QUESTS, TAB_NOTES, TAB_COUNT };
enum CharSub_t { SUB_STATS, SUB_SKILLS, SUB_PERKS, SUB_REP, SUB_CHAR_COUNT };
enum InvSub_t { INV_ALL, INV_WEAPONS, INV_APPAREL, INV_AID, INV_AMMO, INV_MISC, SUB_INV_COUNT };
enum QuestSub_t { Q_ACTIVE, Q_DONE, SUB_QUEST_COUNT };

// what is worn (the server says: hl2rpm_char.cpp)
static char s_szWornSuit[64] = "item_suit";
const char *RPMMenu_WornSuit() { return s_szWornSuit; }		// (hl2rpm_hands.cpp: the hands of the suit)
CON_COMMAND( rpm_suit_worn, "HL2RPM (server -> client): the suit worn" )
{
	Q_strncpy( s_szWornSuit, args.ArgC() > 1 && args.Arg( 1 )[0] ? args.Arg( 1 ) : "item_suit", sizeof( s_szWornSuit ) );
}

enum MenuHot_t { H_TAB, H_SUB, H_ROW, H_USE, H_DROP, H_UNEQUIP, H_LEVELUP, H_CLOSE, H_TRACK, H_HOLSTER };

static int SubCount( int tab )
{
	switch ( tab )
	{
	case TAB_CHAR: return SUB_CHAR_COUNT;
	case TAB_INV: return SUB_INV_COUNT;
	case TAB_QUESTS: return SUB_QUEST_COUNT;
	}
	return 1;
}

static const char *TabName( int tab )
{
	switch ( tab )
	{
	case TAB_CHAR: return TR( "Character", "Персонаж" );
	case TAB_INV: return TR( "Inventory", "Инвентарь" );
	case TAB_QUESTS: return TR( "Quests", "Задания" );
	}
	return TR( "Notes", "Записки" );
}

static const char *SubName( int tab, int sub )
{
	if ( tab == TAB_CHAR )
	{
		static const char *en[] = { "Attributes", "Skills", "Perks", "Reputation" };
		static const char *ru[] = { "Характеристики", "Навыки", "Перки", "Репутация" };
		return IsRussian() ? ru[sub] : en[sub];
	}
	if ( tab == TAB_INV )
	{
		static const char *en[] = { "All", "Weapons", "Apparel", "Aid", "Ammo", "Misc" };
		static const char *ru[] = { "Всё", "Оружие", "Одежда", "Помощь", "Патроны", "Прочее" };
		return IsRussian() ? ru[sub] : en[sub];
	}
	if ( tab == TAB_QUESTS )
		return sub == Q_ACTIVE ? TR( "Active", "Текущие" ) : TR( "Finished", "Завершённые" );
	return "";
}

static bool ListHas( const char *pszList, const char *pszItem )
{
	CUtlStringList parts;
	V_SplitString( pszList ? pszList : "", ";", parts );
	for ( int i = 0; i < parts.Count(); i++ )
		if ( !Q_stricmp( parts[i], pszItem ) )
			return true;
	return false;
}

// sum of an effect of a perk / trait definition (rank times)
static float DefEffect( KeyValues *pDef, int times, const char *type, const char *id )
{
	float sum = 0;
	KeyValues *e = pDef ? pDef->FindKey( "effects" ) : NULL;
	for ( KeyValues *x = e ? e->GetFirstTrueSubKey() : NULL; x; x = x->GetNextTrueSubKey() )
		if ( !Q_stricmp( x->GetString( "type" ), type ) && ( !id || !Q_stricmp( x->GetString( "id" ), id ) ) )
			sum += x->GetFloat( "value" ) * times;
	return sum;
}

static int ItemCategory( const char *cls )
{
	const RPMItemDef_t *d = RPMItems::Find( cls );
	const char *cat = d ? d->category : "";
	if ( !Q_stricmp( cat, "weapon" ) || ( !cat[0] && !Q_strnicmp( cls, "weapon_", 7 ) ) )
		return INV_WEAPONS;
	if ( !Q_stricmp( cat, "apparel" ) || !Q_strnicmp( cls, "item_suit", 9 ) )
		return INV_APPAREL;
	if ( !Q_stricmp( cat, "aid" ) || !Q_stricmp( cat, "food" ) || ( !cat[0] && ( Q_stristr( cls, "health" ) || Q_stristr( cls, "battery" ) ) ) )
		return INV_AID;
	if ( !Q_stricmp( cat, "ammo" ) || InventoryClient_IsAmmo( cls ) )
		return INV_AMMO;
	return INV_MISC;
}

static bool IsNoteItem( const char *cls )
{
	const RPMItemDef_t *d = RPMItems::Find( cls );
	return d && ( !Q_stricmp( d->base, "note" ) || !Q_stricmp( d->category, "note" ) );
}

// display names of the factions: scripts/world/factions.txt ("name", "name_<language>")
static KeyValues *Factions()
{
	static KeyValues *s_pFactions = NULL;
	static float s_flLoaded = -100.0f;
	if ( !s_pFactions || gpGlobals->realtime - s_flLoaded > 30.0f )
	{
		if ( s_pFactions )
			s_pFactions->deleteThis();
		s_pFactions = new KeyValues( "Factions" );
		if ( !s_pFactions->LoadFromFile( g_pFullFileSystem, "scripts/world/factions.txt", "GAME" ) )
			s_pFactions->Clear();
		s_flLoaded = gpGlobals->realtime;
	}
	return s_pFactions;
}

static const char *FactionName( const char *id )
{
	KeyValues *f = Factions()->FindKey( id );
	const char *n = f ? RPMRPG::LocString( f, "name" ) : "";
	if ( n[0] )
		return n;
	static char buf[64];
	Q_strncpy( buf, id, sizeof( buf ) );
	for ( char *c = buf; *c; c++ )
		if ( *c == '_' )
			*c = ' ';
	if ( buf[0] >= 'a' && buf[0] <= 'z' )
		buf[0] -= 32;
	return buf;
}

static const char *RepWord( int v )
{
	if ( v <= (int)RPMRPG::Rule( "rep_hostile" ) )
		return TR( "Enemy", "Враг" );
	if ( v < -10 )
		return TR( "Disliked", "Неприязнь" );
	if ( v <= 10 )
		return TR( "Neutral", "Нейтрально" );
	if ( v < 50 )
		return TR( "Liked", "Уважение" );
	return TR( "Idolized", "Почитание" );
}

//=============================================================================
struct MenuRow_t
{
	CUtlString id;
	CUtlString label;
	CUtlString value;
	int count;
	int kind;			// inventory: 0 item, 1 the worn suit; quests: index in the list
	bool marker, markerOn, dim;
};

class CRPMMenu : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMMenu, Panel );
public:
	CRPMMenu( Panel *parent ) : BaseClass( parent, "RPMMenu" )
	{
		SetPaintBackgroundEnabled( false );
		SetVisible( false );
		m_iTab = TAB_INV;
		for ( int i = 0; i < TAB_COUNT; i++ )
		{
			m_iSub[i] = 0;
			m_iSel[i] = 0;
			m_iScroll[i] = 0;
		}
		m_flOpenTime = m_flSelTime = m_flTabTime = 0.0f;
		m_iCardScroll = 0;
		m_bEscapeBlocked = false;
		m_flModelYaw = RPMItemPreview::YAW_START;
		m_flModelPitch = RPMItemPreview::PITCH_START;
		m_flModelZoom = 1.0f;
		m_flModelTime = 0.0f;
		m_bModelDrag = false;
		m_iDragX = m_iDragY = 0;
		m_iModelX = m_iModelY = m_iModelW = m_iModelH = 0;
	}

	virtual void ApplySchemeSettings( IScheme *s )
	{
		BaseClass::ApplySchemeSettings( s );
		LoadFonts( s );
	}

	void Open( int tab )
	{
		if ( tab >= 0 && tab < TAB_COUNT )
			m_iTab = tab;
		if ( g_pClientMode && g_pClientMode->GetViewport() && GetParent() != g_pClientMode->GetViewport() )
			SetParent( g_pClientMode->GetViewport() );
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		SetBounds( 0, 0, sw, sh );
		SetVisible( true );
		MakePopup();
		SetZPos( 1500 );
		SetKeyBoardInputEnabled( true );
		SetMouseInputEnabled( true );
		MoveToFront();
		RequestFocus();
		surface()->SetCursor( dc_arrow );
		m_flOpenTime = m_flTabTime = gpGlobals->realtime;
		m_iCardScroll = 0;
		engine->ClientCmd( "inventory_request\n" );
		engine->ClientCmd( "quest_request\n" );
		engine->ClientCmd( "inventory_ui_open 1\n" );
		if ( !m_bEscapeBlocked )
		{
			engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
			m_bEscapeBlocked = true;
		}
		InventoryClient_PlayPanelSound( true );		// the sounds of the old inventory (the user's)
	}

	void Close()
	{
		if ( !IsVisible() )
			return;
		SetVisible( false );
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( false );
		engine->ClientCmd( "inventory_ui_open 0\n" );
		if ( m_bEscapeBlocked )
		{
			engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
			m_bEscapeBlocked = false;
		}
		InventoryClient_PlayPanelSound( false );
	}

	int Tab() const { return m_iTab; }

	//---------------------------------------------------------------------
	// layout (sizes of a 900 pixel tall screen)
	//---------------------------------------------------------------------
	void Layout( int &x, int &y, int &w, int &h )
	{
		int sw, sh;
		GetSize( sw, sh );
		w = Min( Sc( 1300 ), sw - Sc( 100 ) );
		h = Min( Sc( 760 ), sh - Sc( 80 ) );
		x = ( sw - w ) / 2;
		y = ( sh - h ) / 2;
	}

	void ListRect( int &lx, int &ly, int &lw, int &lh )
	{
		int x, y, w, h;
		Layout( x, y, w, h );
		lx = x + Sc( 36 );
		ly = y + Sc( 122 );
		lw = ( w - Sc( 110 ) ) * 2 / 5;
		lh = h - Sc( 122 ) - Sc( 94 );
	}

	int RowH() { return Sc( 34 ); }
	int VisibleRows() { int lx, ly, lw, lh; ListRect( lx, ly, lw, lh ); return Max( 1, lh / RowH() ); }

	//---------------------------------------------------------------------
	// rows of the current tab
	//---------------------------------------------------------------------
	void BuildRows()
	{
		m_Rows.RemoveAll();
		KeyValues *st = RPMChar_ClientState();
		int sub = m_iSub[m_iTab];
		if ( m_iTab == TAB_CHAR )
		{
			if ( sub == SUB_STATS )
			{
				for ( int i = 0; i < RPMRPG::StatCount(); i++ )
				{
					const char *id = RPMRPG::StatId( i );
					MenuRow_t &r = AddRow( id, RPMRPG::Name( id ) );
					r.value.Format( "%d", st->FindKey( "stats", true )->GetInt( id, 5 ) );
				}
			}
			else if ( sub == SUB_SKILLS )
			{
				for ( int i = 0; i < RPMRPG::SkillCount(); i++ )
				{
					const char *id = RPMRPG::SkillId( i );
					MenuRow_t &r = AddRow( id, RPMRPG::Name( id ) );
					r.value.Format( "%d", st->FindKey( "skills", true )->GetInt( id ) );
					r.marker = true;
					r.markerOn = ListHas( st->GetString( "tags" ), id );
				}
			}
			else if ( sub == SUB_PERKS )
			{
				KeyValues *perks = st->FindKey( "perks", true );
				for ( KeyValues *p = perks->GetFirstValue(); p; p = p->GetNextValue() )
				{
					MenuRow_t &r = AddRow( p->GetName(), RPMRPG::Name( p->GetName() ) );
					KeyValues *d = RPMRPG::Perk( p->GetName() );
					if ( d && d->GetInt( "ranks", 1 ) > 1 )
						r.value.Format( "%d/%d", p->GetInt(), d->GetInt( "ranks", 1 ) );
				}
				CUtlStringList traits;
				V_SplitString( st->GetString( "traits" ), ";", traits );
				for ( int i = 0; i < traits.Count(); i++ )
					if ( traits[i][0] )
					{
						MenuRow_t &r = AddRow( traits[i], RPMRPG::Name( traits[i] ) );
						r.value = TR( "trait", "черта" );
						r.dim = true;
						r.kind = 1;
					}
			}
			else
			{
				KeyValues *rep = st->FindKey( "rep", true );
				for ( KeyValues *f = Factions()->GetFirstTrueSubKey(); f; f = f->GetNextTrueSubKey() )
				{
					MenuRow_t &r = AddRow( f->GetName(), FactionName( f->GetName() ) );
					r.value.Format( "%+d", rep->GetInt( f->GetName() ) );
				}
				for ( KeyValues *v = rep->GetFirstValue(); v; v = v->GetNextValue() )
					if ( !Factions()->FindKey( v->GetName() ) )
					{
						MenuRow_t &r = AddRow( v->GetName(), FactionName( v->GetName() ) );
						r.value.Format( "%+d", v->GetInt() );
					}
			}
		}
		else if ( m_iTab == TAB_INV || m_iTab == TAB_NOTES )
		{
			C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
			if ( m_iTab == TAB_INV && pPlayer && pPlayer->IsSuitEquipped() && ( sub == INV_ALL || sub == INV_APPAREL ) )
			{
				MenuRow_t &r = AddRow( s_szWornSuit, RPMItems::Name( s_szWornSuit ) );
				r.kind = 1;
				r.marker = r.markerOn = true;
				r.value = TR( "worn", "надет" );
			}
			// the equipped weapon (one: hl2rpm_weapons.cpp), out in the hands or put away
			const char *active = pPlayer && pPlayer->GetActiveWeapon() ? pPlayer->GetActiveWeapon()->GetName() : "";
			if ( m_iTab == TAB_INV && pPlayer && ( sub == INV_ALL || sub == INV_WEAPONS ) )
			{
				for ( int i = 0; i < MAX_WEAPONS; i++ )
				{
					C_BaseCombatWeapon *pWeapon = pPlayer->GetWeapon( i );
					if ( !pWeapon )
						continue;
					MenuRow_t &r = AddRow( pWeapon->GetName(), RPMItems::Name( pWeapon->GetName() ) );
					r.kind = 2;
					r.marker = true;
					r.markerOn = true;
					r.value = !Q_stricmp( active, pWeapon->GetName() ) ? TR( "in hands", "в руках" ) : TR( "put away", "убрано" );
				}
			}
			CVGuiInventoryPanel *inv = GetGlobalInventoryPanel();
			int first = m_Rows.Count();
			for ( int i = 0; inv && i < inv->GetItemList().Count(); i++ )
			{
				const char *raw = inv->GetItemList()[i].Get();
				char cls[128];
				int count = 1;
				bool bExplicit = false;
				InventoryClient_ParseEntry( raw, cls, sizeof( cls ), count, bExplicit );
				if ( !cls[0] )
					continue;
				bool note = IsNoteItem( cls );
				if ( m_iTab == TAB_NOTES ? !note : ( sub != INV_ALL && ItemCategory( cls ) != sub ) )
					continue;
				bool stack = InventoryClient_IsAmmo( cls ) || RPMItems::IsStackable( cls );
				int n = stack && bExplicit ? Max( 1, count ) : 1;
				MenuRow_t *r = NULL;
				for ( int j = first; j < m_Rows.Count(); j++ )
					if ( !Q_stricmp( m_Rows[j].id, cls ) )
						r = &m_Rows[j];
				if ( r )
				{
					r->count += n;
					r->value.Format( "%.1f", atof( r->value ) + InventoryClient_EntryWeight( raw ) );
					continue;
				}
				r = &AddRow( cls, RPMItems::Name( cls ) );
				r->count = n;
				r->value.Format( "%.1f", InventoryClient_EntryWeight( raw ) );
				if ( ItemCategory( cls ) == INV_WEAPONS )
				{
					r->marker = true;
					r->markerOn = !Q_stricmp( active, cls );
				}
			}
			// by name inside the list (the worn suit stays first)
			for ( int i = first; i < m_Rows.Count(); i++ )
				for ( int j = i + 1; j < m_Rows.Count(); j++ )
					if ( Q_stricmp( m_Rows[j].label, m_Rows[i].label ) < 0 )
						V_swap( m_Rows[i], m_Rows[j] );
			if ( m_iTab == TAB_NOTES )
				for ( int i = 0; i < m_Rows.Count(); i++ )
					m_Rows[i].value.Clear();
		}
		else if ( m_iTab == TAB_QUESTS )
		{
			CVGuiInventoryPanel *inv = GetGlobalInventoryPanel();
			for ( int i = 0; inv && i < inv->GetQuestList().Count(); i++ )
			{
				const QuestClientEntry &q = inv->GetQuestList()[i];
				bool active = q.state == kQuestState_Active;
				bool done = q.state == kQuestState_Completed || q.state == kQuestState_Failed;
				if ( sub == Q_ACTIVE ? !active : !done )
					continue;
				MenuRow_t &r = AddRow( q.id, q.baseTitle.Length() ? q.baseTitle.Get() : q.id.Get() );
				r.kind = i;
				// active quests: the square - filled at the tracked one
				r.marker = active;
				r.markerOn = active && !Q_stricmp( q.id, s_TrackedQuest );
				if ( q.state == kQuestState_Failed )
				{
					r.value = TR( "failed", "провалено" );
					r.dim = true;
				}
			}
		}
		m_iSel[m_iTab] = clamp( m_iSel[m_iTab], 0, Max( 0, m_Rows.Count() - 1 ) );
		int maxScroll = Max( 0, m_Rows.Count() - VisibleRows() );
		m_iScroll[m_iTab] = clamp( m_iScroll[m_iTab], 0, maxScroll );
	}

	MenuRow_t &AddRow( const char *id, const char *label )
	{
		MenuRow_t &r = m_Rows[m_Rows.AddToTail()];
		r.id = id;
		r.label = label;
		r.count = 1;
		r.kind = 0;
		r.marker = r.markerOn = r.dim = false;
		return r;
	}

	const MenuRow_t *Selected() { int s = m_iSel[m_iTab]; return s >= 0 && s < m_Rows.Count() ? &m_Rows[s] : NULL; }

	//---------------------------------------------------------------------
	// painting
	//---------------------------------------------------------------------
	virtual void Paint()
	{
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		m_Hot.Begin( mx, my );
		BuildRows();
		const Palette_t &p = Pal();
		int sw, sh;
		GetSize( sw, sh );
		float open = Ramp( m_flOpenTime, 0.22f );
		Rect( 0, 0, sw, sh, Color( 0, 0, 0, (int)( 170 * open ) ) );
		int x, y, w, h;
		Layout( x, y, w, h );
		// the window unfolds from its middle
		int ph = Max( Sc( 8 ), (int)( h * open ) );
		Plate( x, y + ( h - ph ) / 2, w, ph, 247, Sc( 10 ) );
		FrameBox( x, y + ( h - ph ) / 2, w, ph, Sc( 18 ) );
		if ( open < 0.6f )
			return;
		float a = Ramp( m_flOpenTime, 0.2f, 0.15f );

		// main tabs
		int tx = x + Sc( 36 );
		for ( int i = 0; i < TAB_COUNT; i++ )
		{
			bool on = i == m_iTab;
			int tw = SpacedWidth( F_TAB, TabName( i ), Sc( 4 ) );
			bool hover = m_Hot.Over( tx - Sc( 8 ), y + Sc( 14 ), tw + Sc( 16 ), Sc( 44 ) );
			Spaced( F_TAB, tx, y + Sc( 22 ), Fade( on || hover ? p.accent : p.dim, a ), TabName( i ), Sc( 4 ), on );
			if ( on )
				Rect( tx, y + Sc( 52 ), tw, Max( 2, Sc( 2 ) ), Fade( p.accent, a ) );
			m_Hot.Add( tx - Sc( 8 ), y + Sc( 14 ), tw + Sc( 16 ), Sc( 44 ), H_TAB, i );
			tx += tw + Sc( 46 );
		}
		PaintClock( x + w - Sc( 36 ), y + Sc( 26 ), a );
		Line( x + Sc( 30 ), y + Sc( 64 ), w - Sc( 60 ), Fade( p.faint, a ) );

		// sub tabs
		tx = x + Sc( 40 );
		for ( int i = 0; i < SubCount( m_iTab ) && m_iTab != TAB_NOTES; i++ )
		{
			bool on = i == m_iSub[m_iTab];
			const char *name = SubName( m_iTab, i );
			int tw = SpacedWidth( F_SMALL, name, Sc( 3 ) );
			bool hover = m_Hot.Over( tx - Sc( 8 ), y + Sc( 74 ), tw + Sc( 16 ), Sc( 28 ) );
			Spaced( F_SMALL, tx, y + Sc( 80 ), Fade( on ? p.accent : hover ? p.dim : p.grey, a ), name, Sc( 3 ) );
			if ( on )
			{
				surface()->DrawSetColor( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 120 * a ) );
				surface()->DrawOutlinedRect( tx - Sc( 8 ), y + Sc( 74 ), tx + tw + Sc( 8 ), y + Sc( 102 ) );
			}
			m_Hot.Add( tx - Sc( 8 ), y + Sc( 74 ), tw + Sc( 16 ), Sc( 28 ), H_SUB, i );
			tx += tw + Sc( 34 );
		}

		PaintList( a );
		PaintCard( a );
		PaintStatus( x, y, w, h, a );

		// the hovered row is the selected one (like the Pip-Boy); a rollover sound on change
		const Hot_t *hot = m_Hot.Hovered();
		static CHoverSound s_Hover;
		s_Hover.Update( hot );
	}

	void PaintClock( int rx, int y, float a )
	{
		const Palette_t &p = Pal();
		CUtlString s;
		CEnvTimecycle *tc = GetTimecycle();
		if ( tc )
		{
			float hours = tc->GetTimeOfDayHours();
			s.Format( "%02d:%02d", (int)floorf( hours ) % 24, (int)floorf( ( hours - floorf( hours ) ) * 60.0f ) % 60 );
		}
		C_WeatherSystem *ws = GetWeatherSystem();
		if ( ws && ws->GetTargetPreset() >= 0 )
		{
			const wchar_t *wname = g_pVGuiLocalize->Find( GetWeatherPresetInfo( ws->GetTargetPreset() ).pszDisplayName );
			if ( wname )
			{
				char name[128];
				g_pVGuiLocalize->ConvertUnicodeToANSI( wname, name, sizeof( name ) );
				s.Append( CFmtStr( "%s%s", s.Length() ? "  \xc2\xb7  " : "", name ) );
			}
		}
		Spaced( F_SMALL, rx, y, Fade( p.dim, a ), s.Get(), Sc( 1 ), false, 2 );
	}

	void PaintList( float a )
	{
		const Palette_t &p = Pal();
		int lx, ly, lw, lh;
		ListRect( lx, ly, lw, lh );
		int rows = VisibleRows();
		int first = m_iScroll[m_iTab];
		if ( !m_Rows.Count() )
		{
			const char *empty = m_iTab == TAB_NOTES ? TR( "No notes.", "Записок нет." ) : m_iTab == TAB_QUESTS ? TR( "No quests here.", "Здесь пока пусто." )
				: m_iTab == TAB_INV ? TR( "Nothing here.", "Здесь ничего нет." ) : TR( "None yet.", "Пока нет." );
			Text( F_TEXT, lx + Sc( 12 ), ly, Fade( p.grey, a ), empty );
			return;
		}
		for ( int i = first; i < m_Rows.Count() && i < first + rows; i++ )
		{
			const MenuRow_t &r = m_Rows[i];
			int ry = ly + ( i - first ) * RowH();
			bool sel = i == m_iSel[m_iTab];
			float ra = a * Ramp( m_flTabTime, 0.15f, ( i - first ) * 0.02f );
			if ( sel )
				Highlight( lx - Sc( 8 ), ry - Sc( 4 ), lw, RowH() - Sc( 4 ), 1.0f - ( gpGlobals->realtime - m_flSelTime ) / 0.35f, (int)( 255 * ra ) );
			Color c = sel ? p.text : r.dim ? p.dim : p.accent;
			int th = surface()->GetFontTall( Font( F_TEXT ) );
			int ty = ry + ( RowH() - Sc( 8 ) - th ) / 2;
			if ( r.marker )
				Marker( lx + Sc( 2 ), ty + th / 2 - Sc( 5 ), Sc( 10 ), r.markerOn, Fade( c, ra ) );
			CUtlString label = r.label;
			if ( r.count > 1 )
				label.Append( CFmtStr( " (%d)", r.count ) );
			int valueW = r.value.Length() ? TextWidth( F_TEXT, r.value ) + Sc( 16 ) : 0;
			Text( F_TEXT, lx + Sc( 20 ), ty, Fade( c, ra ), label, sel, 0, lw - Sc( 50 ) - valueW );
			if ( r.value.Length() )
				Text( F_TEXT, lx + lw - Sc( 30 ), ty, Fade( c, ra ), r.value, false, 2 );
			m_Hot.Add( lx - Sc( 8 ), ry - Sc( 4 ), lw, RowH(), H_ROW, i );
			if ( r.marker && m_iTab == TAB_QUESTS )
				m_Hot.Add( lx - Sc( 8 ), ry - Sc( 4 ), Sc( 30 ), RowH(), H_TRACK, i );
		}
		// more rows than fit
		if ( first > 0 )
			Arrow( lx + lw / 2, ly - Sc( 14 ), Sc( 5 ), true, Fade( p.dim, a ) );
		if ( first + rows < m_Rows.Count() )
			Arrow( lx + lw / 2, ly + rows * RowH(), Sc( 5 ), false, Fade( p.dim, a ) );
	}

	// one "name ... value" line of a card
	int CardLine( int x, int y, int w, const char *name, const char *value, Color vc, float a )
	{
		const Palette_t &p = Pal();
		Text( F_SMALL, x, y, Fade( p.dim, a ), name );
		Text( F_SMALL, x + w, y, Fade( vc, a ), value, false, 2 );
		Line( x - Sc( 2 ), y + Sc( 26 ), w + Sc( 4 ), Fade( Color( p.accent.r(), p.accent.g(), p.accent.b(), 30 ), a ) );
		return Sc( 34 );
	}

	void PaintCard( float a )
	{
		const Palette_t &p = Pal();
		int x, y, w, h, lx, ly, lw, lh;
		Layout( x, y, w, h );
		ListRect( lx, ly, lw, lh );
		const int cx = lx + lw + Sc( 40 ), cy = ly, cw = x + w - Sc( 40 ) - cx, ch = lh - Sc( 6 );
		Plate( cx, cy, cw, ch, (int)( 230 * a ), Sc( 8 ) );
		FrameBox( cx, cy, cw, ch, Sc( 12 ), (int)( 255 * a ) );
		m_iModelW = 0;
		const MenuRow_t *r = Selected();
		if ( !r )
			return;
		float ca = a * Ramp( m_flSelTime, 0.18f );
		const int ix = cx + Sc( 30 ), iw = cw - Sc( 60 );
		KeyValues *st = RPMChar_ClientState();
		int sub = m_iSub[m_iTab];
		int ty = cy + Sc( 24 );
		// title and a big number on the right
		CUtlString big;
		if ( m_iTab == TAB_CHAR && sub != SUB_PERKS )
			big = r->value;
		Spaced( F_TITLE, ix, ty, Fade( p.accent, ca ), r->label, Sc( 4 ), true );
		if ( big.Length() )
		{
			const char *digits = big.Get();
			char sign[2] = { 0, 0 };
			if ( digits[0] == '-' || digits[0] == '+' )
				sign[0] = *digits++;
			int nw = Text( F_NUM, cx + cw - Sc( 30 ), cy + Sc( 10 ), Fade( sign[0] == '-' ? p.bad : p.accent, ca ), digits, true, 2 );
			if ( sign[0] )
				Text( F_TITLE, cx + cw - Sc( 36 ) - nw, cy + Sc( 26 ), Fade( sign[0] == '-' ? p.bad : p.accent, ca ), sign, true, 2 );
		}
		int yy = cy + Sc( 76 );
		Line( cx + Sc( 28 ), yy, cw - Sc( 56 ), Fade( p.faint, ca ) );
		yy += Sc( 16 );

		if ( m_iTab == TAB_CHAR )
		{
			if ( sub == SUB_REP )
			{
				int v = st->FindKey( "rep", true )->GetInt( r->id );
				Text( F_TEXT, ix, yy, Fade( v < 0 ? p.bad : v > 10 ? p.good : p.text, ca ), RepWord( v ) );
				yy += Sc( 40 );
				// a bar from -100 to +100 with the middle marked
				Bar( ix, yy, iw, Sc( 8 ), ( v + 100 ) / 200.0f, Fade( v < 0 ? p.bad : p.accent, ca ) );
				Rect( ix + iw / 2, yy - Sc( 4 ), 1, Sc( 16 ), Fade( p.text, ca ) );
				yy += Sc( 30 );
				Wrap( F_SMALL, ix, yy, iw, Fade( p.dim, ca ), TR( "Killing members of a faction lowers it; at the bottom the whole faction attacks.",
					"Убийство членов фракции снижает репутацию; на самом дне на вас нападает вся фракция." ) );
				return;
			}
			yy += Wrap( F_TEXT, ix, yy, iw, Fade( p.text, ca ), RPMRPG::Desc( r->id ) ) + Sc( 20 );
			if ( sub == SUB_STATS )
			{
				int base = st->FindKey( "stats_base", true )->GetInt( r->id, 5 ), cur = st->FindKey( "stats", true )->GetInt( r->id, 5 );
				yy += CardLine( ix, yy, iw, TR( "Base", "Выбрано" ), CFmtStr( "%d", base ), p.accent, ca );
				if ( cur != base )
					yy += CardLine( ix, yy, iw, TR( "Traits and perks", "Черты и перки" ), CFmtStr( "%+d", cur - base ), cur > base ? p.good : p.bad, ca );
				CUtlString governs;
				for ( int i = 0; i < RPMRPG::SkillCount(); i++ )
					if ( !Q_stricmp( RPMRPG::SkillStat( RPMRPG::SkillId( i ) ), r->id ) )
						governs.Append( CFmtStr( "%s%s", governs.Length() ? ", " : "", RPMRPG::Name( RPMRPG::SkillId( i ) ) ) );
				if ( governs.Length() )
				{
					Text( F_SMALL, ix, yy, Fade( p.dim, ca ), TR( "Raises skills:", "Повышает навыки:" ) );
					Wrap( F_SMALL, ix, yy + Sc( 22 ), iw, Fade( p.accent, ca ), governs );
				}
			}
			else if ( sub == SUB_SKILLS )
			{
				const char *gov = RPMRPG::SkillStat( r->id );
				if ( gov && gov[0] )
					yy += CardLine( ix, yy, iw, TR( "Depends on", "Зависит от" ), CFmtStr( "%s %d", RPMRPG::Name( gov ), st->FindKey( "stats", true )->GetInt( gov, 5 ) ), p.accent, ca );
				if ( r->markerOn )
					yy += CardLine( ix, yy, iw, TR( "Tagged skill", "Отмеченный навык" ), CFmtStr( "+%d", (int)RPMRPG::Rule( "tag_bonus" ) ), p.good, ca );
				// perks and traits that change it
				KeyValues *perks = st->FindKey( "perks", true );
				for ( KeyValues *pk = perks->GetFirstValue(); pk && yy < cy + ch - Sc( 40 ); pk = pk->GetNextValue() )
				{
					KeyValues *d = RPMRPG::Perk( pk->GetName() );
					int e = (int)( DefEffect( d, pk->GetInt(), "skill", r->id ) + DefEffect( d, pk->GetInt(), "skills", NULL ) );
					if ( e )
						yy += CardLine( ix, yy, iw, CFmtStr( TR( "Perk \"%s\"", "Перк «%s»" ), RPMRPG::Name( pk->GetName() ) ), CFmtStr( "%+d", e ), e > 0 ? p.good : p.bad, ca );
				}
				CUtlStringList traits;
				V_SplitString( st->GetString( "traits" ), ";", traits );
				for ( int i = 0; i < traits.Count() && yy < cy + ch - Sc( 40 ); i++ )
				{
					KeyValues *d = RPMRPG::Trait( traits[i] );
					int e = (int)( DefEffect( d, 1, "skill", r->id ) + DefEffect( d, 1, "skills", NULL ) );
					if ( e )
						yy += CardLine( ix, yy, iw, CFmtStr( TR( "Trait \"%s\"", "Черта «%s»" ), RPMRPG::Name( traits[i] ) ), CFmtStr( "%+d", e ), e > 0 ? p.good : p.bad, ca );
				}
			}
			else if ( sub == SUB_PERKS )
			{
				KeyValues *d = r->kind ? RPMRPG::Trait( r->id ) : RPMRPG::Perk( r->id );
				if ( d && !r->kind && d->GetInt( "ranks", 1 ) > 1 )
					yy += CardLine( ix, yy, iw, TR( "Rank", "Ранг" ), CFmtStr( TR( "%d of %d", "%d из %d" ), st->FindKey( "perks", true )->GetInt( r->id ), d->GetInt( "ranks", 1 ) ), p.accent, ca );
				if ( d && !r->kind )
					yy += CardLine( ix, yy, iw, TR( "From level", "С уровня" ), CFmtStr( "%d", d->GetInt( "level", 1 ) ), p.accent, ca );
				if ( r->kind )
					Text( F_SMALL, ix, yy, Fade( p.dim, ca ), TR( "A trait is chosen once, when the character is created.", "Черта выбирается один раз — при создании персонажа." ) );
			}
			return;
		}

		if ( m_iTab == TAB_QUESTS )
		{
			CVGuiInventoryPanel *inv = GetGlobalInventoryPanel();
			if ( !inv || r->kind < 0 || r->kind >= inv->GetQuestList().Count() )
				return;
			const QuestClientEntry &q = inv->GetQuestList()[r->kind];
			const char *state = q.state == kQuestState_Completed ? TR( "Completed", "Выполнено" ) : q.state == kQuestState_Failed ? TR( "Failed", "Провалено" ) : "";
			if ( state[0] )
			{
				Text( F_TEXT, ix, yy, Fade( q.state == kQuestState_Failed ? p.bad : p.good, ca ), state );
				yy += Sc( 34 );
			}
			if ( q.state == kQuestState_Active )
			{
				const bool bTracked = !Q_stricmp( q.id, s_TrackedQuest );
				Text( F_SMALL, ix, yy, Fade( bTracked ? p.good : p.dim, ca ), bTracked ? TR( "Tracked: its markers are on the screen", "Отслеживается: его метки на экране" )
					: TR( "Enter - track (show its markers)", "Enter — отслеживать (метки на экране)" ) );
				yy += Sc( 26 );
			}
			if ( q.stageTitle.Length() && q.state == kQuestState_Active )
			{
				Spaced( F_SMALL, ix, yy, Fade( p.accent, ca ), q.stageTitle, Sc( 2 ) );
				yy += Sc( 30 );
			}
			CUtlStringList lines;
			V_SplitString( q.stageDescription.Get(), "\n", lines );
			for ( int i = 0; i < lines.Count() && yy < cy + ch - Sc( 40 ); i++ )
			{
				const char *s = lines[i];
				// "[x] text" / "[  ] text" (quest_system.cpp)
				const char *close = s[0] == '[' ? strchr( s, ']' ) : NULL;
				bool obj = close && close - s <= 3;
				if ( !obj )
				{
					yy += Wrap( F_TEXT, ix, yy, iw, Fade( p.text, ca ), s ) + Sc( 4 );
					continue;
				}
				if ( yy > cy + Sc( 100 ) && i && !( lines[i - 1][0] == '[' ) )
					yy += Sc( 10 );
				bool done = s[1] == 'x' || s[1] == 'X';
				const char *t = close + 1;
				while ( *t == ' ' )
					t++;
				int th = surface()->GetFontTall( Font( F_TEXT ) );
				Marker( ix + Sc( 2 ), yy + th / 2 - Sc( 6 ), Sc( 12 ), done, Fade( done ? p.dim : p.accent, ca ) );
				yy += Wrap( F_TEXT, ix + Sc( 26 ), yy, iw - Sc( 26 ), Fade( done ? p.dim : p.accent, ca ), t ) + Sc( 8 );
			}
			return;
		}

		if ( m_iTab == TAB_NOTES )
		{
			const RPMItemDef_t *d = RPMItems::Find( r->id );
			PaintScrollText( ix, yy, iw, cy + ch - Sc( 24 ) - yy, d ? RPMItems::Text( d, "text" ) : "", ca );
			return;
		}

		// an item: its model on the right, turning; the text takes the rest of the card
		const RPMItemDef_t *d = RPMItems::Find( r->id );
		const char *desc = RPMItems::Desc( r->id );
		int tw = iw;
		if ( m_iTab == TAB_INV )
		{
			if ( Q_stricmp( m_strModelItem, r->id ) )
			{
				m_strModelItem = r->id;
				m_flModelYaw = RPMItemPreview::YAW_START;
				m_flModelPitch = RPMItemPreview::PITCH_START;
				m_flModelZoom = 1.0f;
			}
			const float flNow = Plat_FloatTime();
			if ( !m_bModelDrag )
				m_flModelYaw = fmodf( m_flModelYaw + Min( flNow - m_flModelTime, 0.1f ) * cl_rpm_item_model_spin.GetFloat(), 360.0f );
			m_flModelTime = flNow;
			const int mw = iw / 2, my = yy - Sc( 8 ), mh = cy + ch - Sc( 60 ) - my, mx = ix + iw - mw;
			if ( RPMItemPreview::Paint( RPMItems::Model( r->id ), mx, my, mw, mh, m_flModelYaw, m_flModelPitch, m_flModelZoom, (int)( 255 * ca ), p.accent ) )
			{
				m_iModelX = mx;
				m_iModelY = my;
				m_iModelW = mw;
				m_iModelH = mh;
				tw = iw - mw - Sc( 24 );
			}
		}
		if ( desc[0] )
			yy += Wrap( F_TEXT, ix, yy, tw, Fade( p.text, ca ), desc, tw < iw ? 6 : 5 ) + Sc( 16 );
		if ( r->kind == 0 )
		{
			if ( r->count > 1 )
				yy += CardLine( ix, yy, tw, TR( "Count", "Количество" ), CFmtStr( "%d", r->count ), p.accent, ca );
			yy += CardLine( ix, yy, tw, TR( "Weight", "Вес" ), CFmtStr( TR( "%s kg", "%s кг" ), r->value.Get() ), p.accent, ca );
		}
		if ( RPMItems::Value( r->id ) > 0 )
			yy += CardLine( ix, yy, tw, TR( "Value", "Цена" ), CFmtStr( "%d", RPMItems::Value( r->id ) ), p.accent, ca );
		// (the suits of HL2 are not in the database)
		if ( RPMItems::SuitDefense( r->id ) > 0 )
			yy += CardLine( ix, yy, tw, TR( "Protection", "Защита" ), CFmtStr( "%d %%", RPMItems::SuitDefense( r->id ) ), p.good, ca );
		if ( d )
		{
			if ( d->damage > 0 )
				yy += CardLine( ix, yy, tw, TR( "Damage", "Урон" ), CFmtStr( "%.0f", d->damage ), p.accent, ca );
			if ( d->fireRate > 0 )
				yy += CardLine( ix, yy, tw, TR( "Rate of fire", "Скорострельность" ), CFmtStr( TR( "%.1f / s", "%.1f / с" ), d->fireRate ), p.accent, ca );
			if ( d->clip > 0 )
				yy += CardLine( ix, yy, tw, TR( "Magazine", "Магазин" ), CFmtStr( "%d", d->clip ), p.accent, ca );
			if ( d->skill[0] && yy < cy + ch - Sc( 90 ) )
			{
				int have = st->FindKey( "skills", true )->GetInt( d->skill );
				CUtlString v = d->skillReq > 0 ? CFmtStr( "%s %d", RPMRPG::Name( d->skill ), d->skillReq ).Access() : RPMRPG::Name( d->skill );
				yy += CardLine( ix, yy, tw, TR( "Skill", "Навык" ), v, d->skillReq > have ? p.bad : p.accent, ca );
			}
			if ( d->heal > 0 && yy < cy + ch - Sc( 90 ) )
				yy += CardLine( ix, yy, tw, TR( "Heals", "Лечит" ), CFmtStr( "+%d", d->heal ), p.good, ca );
			if ( d->quest && yy < cy + ch - Sc( 90 ) )
				Wrap( F_SMALL, ix, yy, tw, Fade( p.dim, ca ), TR( "Quest item: can't be dropped.", "Предмет задания: выбросить нельзя." ) );
		}
		// actions
		int by = cy + ch - Sc( 52 ), bw = ( iw - Sc( 20 ) ) / 2;
		if ( r->kind == 1 )
			m_Hot.Button( ix, by, bw, Sc( 32 ), TR( "Take off", "Снять" ), H_UNEQUIP );
		else if ( r->kind == 2 )
		{
			C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
			const bool bOut = pPlayer && pPlayer->GetActiveWeapon();
			m_Hot.Button( ix, by, bw, Sc( 32 ), TR( "Take off [E]", "Снять [E]" ), H_UNEQUIP, 1 );
			m_Hot.Button( ix + bw + Sc( 20 ), by, bw, Sc( 32 ), TR( "Drop [R]", "Выбросить [R]" ), H_DROP );
			Text( F_SMALL, ix, by - Sc( 20 ), Fade( p.dim, ca ), bOut ? TR( "In your hands. H - put away.", "В руках. H - убрать." ) : TR( "Put away. H - take out.", "Убрано. H - достать." ) );
		}
		else
		{
			bool weapon = ItemCategory( r->id ) == INV_WEAPONS;
			bool ammo = ItemCategory( r->id ) == INV_AMMO;
			m_Hot.Button( ix, by, bw, Sc( 32 ), weapon ? TR( "Equip [E]", "Экипировать [E]" ) : TR( "Use [E]", "Использовать [E]" ), H_USE, 0, !ammo );
			m_Hot.Button( ix + bw + Sc( 20 ), by, bw, Sc( 32 ), TR( "Drop [R]", "Выбросить [R]" ), H_DROP, 0, !( d && d->quest ) );
		}
	}

	// a long text in the card that scrolls with the wheel
	void PaintScrollText( int x, int y, int w, int h, const char *text, float a )
	{
		const Palette_t &p = Pal();
		int lineH = LineHeight( F_TEXT );
		int lines = Max( 1, h / lineH );
		int total = WrapHeight( F_TEXT, w, text ) / lineH;
		m_iCardScroll = clamp( m_iCardScroll, 0, Max( 0, total - lines ) );
		surface()->DrawSetTextFont( Font( F_TEXT ) );
		int drawn = 0;
		{
			// the lines from m_iCardScroll that fit
			static wchar_t wtext[4096];
			g_pVGuiLocalize->ConvertANSIToUnicode( text ? text : "", wtext, sizeof( wtext ) );
			int len = V_wcslen( wtext );
			int start = 0, line = 0;
			HFont f = Font( F_TEXT );
			surface()->DrawSetTextColor( Fade( p.text, a ) );
			while ( start <= len && drawn < lines )
			{
				int lastSpace = -1, i = start, wsum = 0;
				for ( ; i < len && wtext[i] != L'\n'; i++ )
				{
					wsum += surface()->GetCharacterWidth( f, wtext[i] );
					if ( wtext[i] == L' ' )
						lastSpace = i;
					if ( wsum > w && lastSpace > start )
					{
						i = lastSpace;
						break;
					}
				}
				if ( line >= m_iCardScroll )
				{
					surface()->DrawSetTextPos( x, y + drawn * lineH );
					surface()->DrawPrintText( wtext + start, i - start );
					drawn++;
				}
				line++;
				start = i + 1;
				if ( i >= len )
					break;
			}
		}
		if ( m_iCardScroll > 0 )
			Arrow( x + w, y - Sc( 10 ), Sc( 5 ), true, Fade( p.dim, a ) );
		if ( m_iCardScroll + lines < total )
			Arrow( x + w, y + h, Sc( 5 ), false, Fade( p.dim, a ) );
	}

	void PaintStatus( int x, int y, int w, int h, float a )
	{
		const Palette_t &p = Pal();
		KeyValues *st = RPMChar_ClientState();
		const int by = y + h - Sc( 70 );
		Line( x + Sc( 30 ), by - Sc( 10 ), w - Sc( 60 ), Fade( p.faint, a ) );
		const char *name = st->GetString( "name" );
		int nx = x + Sc( 36 );
		nx += Spaced( F_TAB, nx, by + Sc( 6 ), Fade( p.accent, a ), name[0] ? name : TR( "Citizen", "Гражданин" ), Sc( 2 ), true ) + Sc( 30 );
		int level = st->GetInt( "level", 1 ), xp = st->GetInt( "xp" ), prev = st->GetInt( "xp_prev" ), next = st->GetInt( "xp_next" );
		nx += Text( F_TEXT, nx, by + Sc( 8 ), Fade( p.dim, a ), CFmtStr( TR( "LVL %d", "УР. %d" ), level ) ) + Sc( 16 );
		float frac = next > prev ? clamp( float( xp - prev ) / float( next - prev ), 0.0f, 1.0f ) : 1.0f;
		Bar( nx, by + Sc( 16 ), Sc( 180 ), Sc( 8 ), frac, Fade( p.accent, a ) );
		nx += Sc( 194 );
		nx += Text( F_SMALL, nx, by + Sc( 10 ), Fade( p.dim, a ), next ? CFmtStr( "%d / %d", xp, next ).Access() : CFmtStr( "%d", xp ).Access() ) + Sc( 20 );
		// points to spend: the button to the level up window
		if ( st->GetInt( "points" ) > 0 || st->GetInt( "perks_pending" ) > 0 )
			m_Hot.Button( nx, by + Sc( 4 ), Sc( 190 ), Sc( 30 ), TR( "Level up", "Повысить уровень" ), H_LEVELUP, 0, true, F_SMALL );
		// health, weight, money on the right
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		CVGuiInventoryPanel *inv = GetGlobalInventoryPanel();
		float cur = 0, max = 0;
		bool over = false;
		if ( inv )
			inv->GetWeightInfo( cur, max, over );
		int money = 0;
		for ( int i = 0; inv && i < inv->GetItemList().Count(); i++ )
		{
			char cls[128];
			int count = 1;
			bool bExplicit = false;
			InventoryClient_ParseEntry( inv->GetItemList()[i].Get(), cls, sizeof( cls ), count, bExplicit );
			if ( !Q_stricmp( cls, "rpm_credits" ) )
				money += bExplicit ? count : 1;
		}
		int rx = x + w - Sc( 36 );
		rx -= Text( F_TEXT, rx, by + Sc( 8 ), Fade( p.dim, a ), CFmtStr( "%s %d", RPMItems::Name( "rpm_credits" ), money ), false, 2 ) + Sc( 26 );
		rx -= Text( F_TEXT, rx, by + Sc( 8 ), Fade( over ? p.bad : p.dim, a ), CFmtStr( TR( "WG %.1f / %.0f kg", "ВЕС %.1f / %.0f кг" ), cur, max ), false, 2 ) + Sc( 26 );
		if ( pPlayer )
			Text( F_TEXT, rx, by + Sc( 8 ), Fade( p.dim, a ), CFmtStr( TR( "HP %d / %d", "ЗДОРОВЬЕ %d / %d" ), pPlayer->GetHealth(), Max( 1, pPlayer->GetMaxHealth() ) ), false, 2 );
		// keys
		const char *keys = m_iTab == TAB_QUESTS ? TR( "Q / E - tabs    \xc2\xb7    \xe2\x86\x90 \xe2\x86\x92 - sections    \xc2\xb7    \xe2\x86\x91 \xe2\x86\x93 - list    \xc2\xb7    Enter - track    \xc2\xb7    Tab - close",
			"Q / E — вкладки    ·    ← → — разделы    ·    ↑ ↓ — список    ·    Enter — отслеживать    ·    Tab — закрыть" )
			: m_iTab == TAB_INV ? TR( "Q / E - tabs    \xc2\xb7    \xe2\x86\x90 \xe2\x86\x92 - sections    \xc2\xb7    E - use    \xc2\xb7    R - drop    \xc2\xb7    X - split    \xc2\xb7    Tab - close",
			"Q / E — вкладки    ·    ← → — разделы    ·    E — использовать    ·    R — выбросить    ·    X — разделить    ·    Tab — закрыть" )
			: TR( "Q / E - tabs    \xc2\xb7    \xe2\x86\x90 \xe2\x86\x92 - sections    \xc2\xb7    \xe2\x86\x91 \xe2\x86\x93 - list    \xc2\xb7    Tab - close",
				"Q / E — вкладки    ·    ← → — разделы    ·    ↑ ↓ — список    ·    Tab — закрыть" );
		Text( F_SMALL, x + w / 2, y + h - Sc( 28 ), Fade( p.grey, a ), keys, false, 1 );
	}

	//---------------------------------------------------------------------
	// input
	//---------------------------------------------------------------------
	void SetTab( int tab )
	{
		tab = ( tab + TAB_COUNT ) % TAB_COUNT;
		if ( tab != m_iTab )
		{
			m_iTab = tab;
			m_flTabTime = m_flSelTime = gpGlobals->realtime;
			m_iCardScroll = 0;
			Sound( SND_TAB );
		}
	}

	void SetSub( int sub )
	{
		int n = SubCount( m_iTab );
		sub = ( sub + n ) % n;
		if ( sub != m_iSub[m_iTab] )
		{
			m_iSub[m_iTab] = sub;
			m_iSel[m_iTab] = 0;
			m_iScroll[m_iTab] = 0;
			m_iCardScroll = 0;
			m_flTabTime = m_flSelTime = gpGlobals->realtime;
			Sound( SND_TAB );
		}
	}

	void Select( int i )
	{
		if ( i < 0 || i >= m_Rows.Count() || i == m_iSel[m_iTab] )
			return;
		m_iSel[m_iTab] = i;
		m_flSelTime = gpGlobals->realtime;
		m_iCardScroll = 0;
		int rows = VisibleRows();
		if ( i < m_iScroll[m_iTab] )
			m_iScroll[m_iTab] = i;
		else if ( i >= m_iScroll[m_iTab] + rows )
			m_iScroll[m_iTab] = i - rows + 1;
	}

	// Quests: track the selected active quest (again - none)
	void TrackSelected()
	{
		const MenuRow_t *r = Selected();
		if ( !r || m_iTab != TAB_QUESTS || !r->marker )
			return;
		engine->ClientCmd( CFmtStr( "rpm_quest_track \"%s\"\n", r->id.Get() ) );
		// at once in the menu, the server confirms with rpm_quest_tracked
		s_TrackedQuest = !Q_stricmp( s_TrackedQuest, r->id ) ? "" : r->id.Get();
		Sound( SND_CLICK );
		BuildRows();
	}

	// Inventory: the selected stack (ammo, tokens, bobby pins...) into two halves - R then drops one of them
	void SplitSelected()
	{
		const MenuRow_t *r = Selected();
		CVGuiInventoryPanel *inv = GetGlobalInventoryPanel();
		if ( !r || !inv || m_iTab != TAB_INV || r->kind != 0 )
			return;
		for ( int i = 0; i < inv->GetItemList().Count(); i++ )
		{
			char cls[128];
			int count = 1;
			bool bExplicit = false;
			InventoryClient_ParseEntry( inv->GetItemList()[i].Get(), cls, sizeof( cls ), count, bExplicit );
			if ( Q_stricmp( cls, r->id ) || !bExplicit || count < 2 )
				continue;
			engine->ClientCmd( CFmtStr( "inventory_split %d %d\n", i, count / 2 ) );
			Sound( SND_CLICK );
			return;
		}
	}

	void ItemCommand( const char *cmd )
	{
		const MenuRow_t *r = Selected();
		if ( !r || m_iTab != TAB_INV )
			return;
		// the equipped weapon: E takes it off, R puts it back first and drops it from the inventory
		if ( r->kind == 2 )
		{
			if ( !Q_stricmp( cmd, "inventory_use" ) )
				engine->ClientCmd( "inventory_unequip_weapon\n" );
			else
				engine->ClientCmd( CFmtStr( "inventory_unequip_weapon; %s \"%s\"\n", cmd, r->id.Get() ) );
			Sound( SND_CLICK );
			return;
		}
		if ( r->kind != 0 )
			return;
		engine->ClientCmd( CFmtStr( "%s \"%s\"\n", cmd, r->id.Get() ) );
		Sound( SND_CLICK );
	}

	void Act( int action, int arg )
	{
		switch ( action )
		{
		case H_TAB: SetTab( arg ); break;
		case H_SUB: SetSub( arg ); break;
		case H_ROW: Select( arg ); Sound( SND_CLICK ); break;
		case H_USE: ItemCommand( "inventory_use" ); break;
		case H_DROP: ItemCommand( "inventory_drop" ); break;
		case H_UNEQUIP: engine->ClientCmd( arg ? "inventory_unequip_weapon\n" : "inventory_unequip_suit\n" ); Sound( SND_CLICK ); break;
		case H_HOLSTER: engine->ClientCmd( "rpm_holster\n" ); Sound( SND_CLICK ); break;
		case H_LEVELUP: Close(); engine->ClientCmd( "rpm_char_open levelup\n" ); break;
		case H_CLOSE: Close(); break;
		case H_TRACK: Select( arg ); TrackSelected(); break;
		}
	}

	virtual void OnMousePressed( MouseCode code )
	{
		if ( code != MOUSE_LEFT )
			return;
		int mx, my;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		const Hot_t *hot = m_Hot.Hit( mx, my );
		if ( hot )
			Act( hot->action, hot->arg );
		else if ( OverModel( mx, my ) )
		{
			// the model turns with the mouse
			m_bModelDrag = true;
			m_iDragX = mx;
			m_iDragY = my;
			input()->SetMouseCapture( GetVPanel() );
		}
	}

	virtual void OnMouseReleased( MouseCode code )
	{
		if ( code == MOUSE_LEFT && m_bModelDrag )
		{
			m_bModelDrag = false;
			input()->SetMouseCapture( NULL );
		}
	}

	bool OverModel( int x, int y )
	{
		return m_iModelW > 0 && x >= m_iModelX && x < m_iModelX + m_iModelW && y >= m_iModelY && y < m_iModelY + m_iModelH;
	}

	virtual void OnCursorMoved( int x, int y )
	{
		if ( m_bModelDrag )
		{
			m_flModelYaw = fmodf( m_flModelYaw + ( x - m_iDragX ) * 0.6f, 360.0f );
			m_flModelPitch = clamp( m_flModelPitch + ( y - m_iDragY ) * 0.4f, -60.0f, 60.0f );
			m_iDragX = x;
			m_iDragY = y;
			return;
		}
		// hovering a row selects it, like the Pip-Boy
		const Hot_t *hot = m_Hot.Hit( x, y );
		if ( hot && hot->action == H_ROW )
			Select( hot->arg );
	}

	virtual void OnMouseWheeled( int delta )
	{
		int mx, my, lx, ly, lw, lh;
		input()->GetCursorPos( mx, my );
		ScreenToLocal( mx, my );
		ListRect( lx, ly, lw, lh );
		if ( OverModel( mx, my ) )
		{
			m_flModelZoom = clamp( m_flModelZoom * ( delta > 0 ? 1.12f : 1.0f / 1.12f ), 0.8f, 3.0f );
			return;
		}
		if ( mx > lx + lw + Sc( 20 ) && ( m_iTab == TAB_QUESTS || m_iTab == TAB_NOTES ) )
		{
			m_iCardScroll = Max( 0, m_iCardScroll - delta * 2 );
			return;
		}
		int maxScroll = Max( 0, m_Rows.Count() - VisibleRows() );
		m_iScroll[m_iTab] = clamp( m_iScroll[m_iTab] - delta, 0, maxScroll );
	}

	virtual void OnKeyCodePressed( KeyCode code )
	{
		// the key that opened it closes it
		if ( code == KEY_TAB || code == KEY_I || ( code == KEY_J && m_iTab == TAB_QUESTS ) )
		{
			Close();
			return;
		}
		BaseClass::OnKeyCodePressed( code );
	}

	virtual void OnKeyCodeTyped( KeyCode code )
	{
		switch ( code )
		{
		case KEY_ESCAPE:
			Close();
			engine->ClientCmd_Unrestricted( "gameui_hide\n" );
			engine->ClientCmd_Unrestricted( "wait;gameui_hide\n" );
			return;
		case KEY_TAB: case KEY_I: return;
		case KEY_Q: SetTab( m_iTab - 1 ); return;
		case KEY_E:
			if ( m_iTab == TAB_INV )
				ItemCommand( "inventory_use" );
			else
				SetTab( m_iTab + 1 );
			return;
		case KEY_R: if ( m_iTab == TAB_INV ) ItemCommand( "inventory_drop" ); return;
		case KEY_X: if ( m_iTab == TAB_INV ) SplitSelected(); return;
		case KEY_H: engine->ClientCmd( "rpm_holster\n" ); Sound( SND_CLICK ); return;
		case KEY_LEFT: case KEY_A: SetSub( m_iSub[m_iTab] - 1 ); return;
		case KEY_RIGHT: case KEY_D: SetSub( m_iSub[m_iTab] + 1 ); return;
		case KEY_UP: case KEY_W: Select( Max( 0, m_iSel[m_iTab] - 1 ) ); return;
		case KEY_DOWN: case KEY_S: Select( Min( m_Rows.Count() - 1, m_iSel[m_iTab] + 1 ) ); return;
		case KEY_ENTER: case KEY_PAD_ENTER:
			if ( m_iTab == TAB_INV )
				ItemCommand( "inventory_use" );
			else if ( m_iTab == TAB_QUESTS )
				TrackSelected();
			return;
		case KEY_T: if ( m_iTab == TAB_QUESTS ) TrackSelected(); return;
		default: break;
		}
		BaseClass::OnKeyCodeTyped( code );
	}

	virtual void OnThink()
	{
		BaseClass::OnThink();
		// a dialog or the loot window takes over
		if ( IsVisible() && ( HL2RPM_IsDialogOpen() || ( GetGlobalLootPanel() && GetGlobalLootPanel()->IsVisible() ) ) )
			Close();
	}

	// tests: rpm_menu_key
	void SimulateKey( KeyCode code ) { BuildRows(); OnKeyCodeTyped( code ); }

	// the model on the card of an item
	CUtlString m_strModelItem;
	float m_flModelYaw, m_flModelPitch, m_flModelZoom, m_flModelTime;
	bool m_bModelDrag;
	int m_iDragX, m_iDragY;
	int m_iModelX, m_iModelY, m_iModelW, m_iModelH;

	int m_iTab;
	int m_iSub[TAB_COUNT], m_iSel[TAB_COUNT], m_iScroll[TAB_COUNT];
	int m_iCardScroll;
	float m_flOpenTime, m_flSelTime, m_flTabTime;
	bool m_bEscapeBlocked;
	CUtlVector<MenuRow_t> m_Rows;
	CHotList m_Hot;
};

static CRPMMenu *s_pMenu = NULL;

static CRPMMenu *GetMenu()
{
	if ( !s_pMenu )
		s_pMenu = new CRPMMenu( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
	return s_pMenu;
}

// Tab / I / the journal: open on the tab or close; false = the old inventory window does it
bool RPMMenu_Toggle( int tab )
{
	if ( !cl_rpm_menu.GetBool() )
		return false;
	if ( HL2RPM_IsDialogOpen() )
		return true;
	CRPMMenu *m = GetMenu();
	if ( m->IsVisible() )
		m->Close();
	else
		m->Open( tab );
	return true;
}

bool RPMMenu_IsOpen()
{
	return s_pMenu && s_pMenu->IsVisible();
}

class CRPMMenuSystem : public CAutoGameSystem
{
public:
	CRPMMenuSystem() : CAutoGameSystem( "CRPMMenuSystem" ) {}
	virtual void LevelShutdownPreEntity()
	{
		if ( s_pMenu )
			s_pMenu->Close();
	}
};
static CRPMMenuSystem g_RPMMenuSystem;

static int TabFromName( const char *s )
{
	if ( !Q_stricmp( s, "char" ) || !Q_stricmp( s, "character" ) ) return TAB_CHAR;
	if ( !Q_stricmp( s, "quests" ) || !Q_stricmp( s, "journal" ) ) return TAB_QUESTS;
	if ( !Q_stricmp( s, "notes" ) ) return TAB_NOTES;
	return TAB_INV;
}

CON_COMMAND( rpm_menu, "HL2RPM: the character menu: rpm_menu [char|inv|quests|notes] [sub] (toggles)" )
{
	CRPMMenu *m = GetMenu();
	int tab = args.ArgC() > 1 ? TabFromName( args[1] ) : m->Tab();
	if ( m->IsVisible() && args.ArgC() <= 1 )
	{
		m->Close();
		return;
	}
	if ( !m->IsVisible() )
		m->Open( tab );
	else
		m->SetTab( tab );
	if ( args.ArgC() > 2 )
		m->SetSub( atoi( args[2] ) );
}

// tests: the keys of the menu from the console (up down left right q e r enter esc)
CON_COMMAND( rpm_menu_key, "HL2RPM: press a key of the character menu (tests)" )
{
	if ( !s_pMenu || !s_pMenu->IsVisible() || args.ArgC() < 2 )
		return;
	static const struct { const char *name; KeyCode code; } keys[] = { { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT },
		{ "right", KEY_RIGHT }, { "q", KEY_Q }, { "e", KEY_E }, { "r", KEY_R }, { "enter", KEY_ENTER }, { "t", KEY_T }, { "x", KEY_X }, { "esc", KEY_ESCAPE } };
	for ( int i = 0; i < ARRAYSIZE( keys ); i++ )
		if ( !Q_stricmp( args[1], keys[i].name ) )
			s_pMenu->SimulateKey( keys[i].code );
}

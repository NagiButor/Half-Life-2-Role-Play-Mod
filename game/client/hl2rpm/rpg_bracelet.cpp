//========= HL2RPM ============//
//
// RPG-интерфейс: учётный браслет (Дизайн-документ Part I/14 §2) - главное меню героя.
// Вкладки: «Гражданин» (Λ.А.М.Б.Д.А., навыки, перки, черты; распределение очков и выбор перка -
// это и есть экран «Новый уровень»), «Имущество» (инвентарь inventory_system), «Данные» (задания,
// заметки, отношения), «Пайки» (статус и нормы), «Радио» («Голос Города» - Брин-касты HL2).
// Открывается клавишей инвентаря (TAB / I -> inventory_request_local) и командой rpg_bracelet <вкладка>.
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_formulas.h"
#include "hl2rpm/rpg_quest_data.h"
#include "hl2rpm/rpg_conditions.h"
#include "c_baseplayer.h"
#include "engine/IEngineSound.h"
#include "filesystem.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"
#include "tier1/KeyValues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

static ConVar cl_rpg_bracelet( "cl_rpg_bracelet", "1", FCVAR_CLIENTDLL, "RPG: the inventory key opens the registry bracelet (0 - the old inventory panel)" );
static ConVar cl_rpg_radio( "cl_rpg_radio", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "RPG: bracelet radio station (0 off, 1 Voice of the City)" );
static ConVar cl_rpg_radio_volume( "cl_rpg_radio_volume", "0.45", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "RPG: bracelet radio volume" );

enum BraceletTab_t
{
	TAB_CITIZEN = 0, TAB_PROPERTY, TAB_DATA, TAB_RATIONS, TAB_RADIO, TAB_COUNT
};
static const char *s_pszTabTokens[TAB_COUNT] = { "#HL2RPM_UI_BR_TabCitizen", "#HL2RPM_UI_BR_TabProperty", "#HL2RPM_UI_BR_TabData", "#HL2RPM_UI_BR_TabRations", "#HL2RPM_UI_BR_TabRadio" };
static const char *s_pszTabIds[TAB_COUNT] = { "citizen", "property", "data", "rations", "radio" };

enum DataTab_t { DATA_QUESTS = 0, DATA_NOTES, DATA_RELATIONS, DATA_COUNT };
static const char *s_pszDataTokens[DATA_COUNT] = { "#HL2RPM_UI_BR_Quests", "#HL2RPM_UI_BR_Notes", "#HL2RPM_UI_BR_Relations" };

// категории имущества
enum ItemCat_t { CAT_ALL = 0, CAT_WEAPON, CAT_ARMOR, CAT_MEDICINE, CAT_FOOD, CAT_DOCUMENT, CAT_MISC, CAT_COUNT };
static const char *s_pszCatTokens[CAT_COUNT] = { "#HL2RPM_UI_BR_CatAll", "#HL2RPM_UI_BR_CatWeapon", "#HL2RPM_UI_BR_CatArmor", "#HL2RPM_UI_BR_CatMedicine",
	"#HL2RPM_UI_BR_CatFood", "#HL2RPM_UI_BR_CatDocument", "#HL2RPM_UI_BR_CatMisc" };

// кнопки
enum
{
	BTN_TAB = 1000,				// + вкладка
	BTN_DATATAB = 1010,			// + подвкладка «Данных»
	BTN_CAT = 1020,				// + категория
	BTN_CLOSE = 1040,
	BTN_SKILL_ADD = 1100,		// + навык
	BTN_PERK_OPEN = 1150,
	BTN_PERK_CLOSE = 1151,
	BTN_PERK = 1200,			// + перк
	BTN_PERK_TAKE = 1190,
	BTN_ITEM = 2000,			// + строка имущества
	BTN_ITEM_USE = 1900,
	BTN_ITEM_DROP = 1901,
	BTN_UNEQUIP = 1902,
	BTN_QUEST = 3000,			// + строка задания
	BTN_QUEST_TRACK = 2900,
	BTN_NOTE = 4000,			// + заметка
	BTN_RADIO = 5000,			// + станция
	BTN_HOVER_ATTR = 6000,		// наведение: + характеристика
	BTN_HOVER_TRAIT = 6100,		// + черта
	BTN_HOVER_PERK = 6200,		// + взятый перк
	BTN_HOVER_SKILL = 6300,		// + навык
};

// ---------------------------------------------------------------------------
// Вес и имя предметов (как в инвентаре: items.txt -> scripts/inventory_weights.txt -> по умолчанию)
// ---------------------------------------------------------------------------
static CUtlDict<float, int> s_Weights;
static bool s_bWeightsLoaded = false;

static bool IsAmmoClass( const char *pszClass )
{
	return !Q_strnicmp( pszClass, "item_ammo_", 10 ) || !Q_strnicmp( pszClass, "item_box_", 9 ) || !Q_stricmp( pszClass, "item_rpg_round" );
}

static float ItemUnitWeight( const char *pszClass )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
	if ( pDef && pDef->kg >= 0.0f )
		return pDef->kg;
	if ( !s_bWeightsLoaded )
	{
		s_bWeightsLoaded = true;
		KeyValues *pKV = new KeyValues( "InventoryWeights" );
		if ( pKV->LoadFromFile( g_pFullFileSystem, "scripts/inventory_weights.txt", "GAME" ) )
		{
			for ( KeyValues *pKey = pKV->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey() )
				s_Weights.Insert( pKey->GetName(), pKey->GetFloat( "kg", 0.0f ) );
		}
		pKV->deleteThis();
	}
	const int i = s_Weights.Find( pszClass );
	if ( i != s_Weights.InvalidIndex() )
		return s_Weights[i];
	if ( IsAmmoClass( pszClass ) )
		return 0.01f;
	return !Q_strnicmp( pszClass, "weapon_", 7 ) ? 3.0f : 1.0f;
}

static void ItemDisplayName( const char *pszClass, wchar_t *pOut, int nBytes )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
	const char *pszToken = pDef ? pDef->name.Get() : CFmtStr( "#RPG_Item_%s", pszClass ).Access();
	if ( pszToken[0] == '#' && g_pVGuiLocalize->Find( pszToken + 1 ) )
	{
		Localize( pszToken, pOut, nBytes );
		return;
	}
	// без токена: «item_box_buckshot» -> «Box buckshot»
	char sz[128];
	const char *p = pszClass;
	if ( !Q_strnicmp( p, "item_", 5 ) ) p += 5;
	else if ( !Q_strnicmp( p, "weapon_", 7 ) ) p += 7;
	Q_strncpy( sz, p, sizeof( sz ) );
	for ( char *c = sz; *c; ++c )
	{
		if ( *c == '_' )
			*c = ' ';
	}
	if ( sz[0] )
		sz[0] = (char)toupper( sz[0] );
	g_pVGuiLocalize->ConvertANSIToUnicode( sz, pOut, nBytes );
}

static int ItemCategory( const char *pszClass )
{
	const RPGItemDef_t *pDef = RPGData().FindItem( pszClass );
	const char *pszCat = pDef ? pDef->category.Get() : "";
	if ( !Q_strnicmp( pszClass, "weapon_", 7 ) || !Q_stricmp( pszCat, "weapon" ) || IsAmmoClass( pszClass ) || !Q_stricmp( pszCat, "ammo" ) )
		return CAT_WEAPON;
	if ( !Q_stricmp( pszCat, "armor" ) || !Q_strnicmp( pszClass, "item_suit", 9 ) || !Q_stricmp( pszClass, "item_combine_soldier_suit" ) )
		return CAT_ARMOR;
	if ( !Q_stricmp( pszCat, "medicine" ) || !Q_stricmp( pszClass, "item_healthkit" ) )
		return CAT_MEDICINE;
	if ( !Q_stricmp( pszCat, "food" ) )
		return CAT_FOOD;
	if ( !Q_stricmp( pszCat, "document" ) || !Q_stricmp( pszCat, "note" ) || !Q_stricmp( pszCat, "key" ) )
		return CAT_DOCUMENT;
	return CAT_MISC;
}

// ---------------------------------------------------------------------------
// Радио «Голос Города»: Брин-касты HL2 по программам, с паузами
// ---------------------------------------------------------------------------
struct RadioProgram_t { const char *pszPrefix; int count; const char *pszToken; };
static const RadioProgram_t s_RadioPrograms[] =
{
	{ "vo/breencast/br_welcome%02d.wav", 7, "#HL2RPM_UI_BR_RadioWelcome" },
	{ "vo/breencast/br_instinct%02d.wav", 25, "#HL2RPM_UI_BR_RadioInstinct" },
	{ "vo/breencast/br_collaboration%02d.wav", 11, "#HL2RPM_UI_BR_RadioCollaboration" },
};

class CRPGRadio : public CAutoGameSystemPerFrame
{
public:
	CRPGRadio() : CAutoGameSystemPerFrame( "CRPGRadio" ) { Reset(); }
	void Reset() { m_iGuid = -1; m_iProgram = 0; m_iLine = 0; m_flNextAt = 0.0f; m_bPlaying = false; }
	virtual void LevelShutdownPreEntity() { Stop(); Reset(); }
	virtual void Update( float frametime )
	{
		const bool bOn = cl_rpg_radio.GetInt() == 1 && C_BasePlayer::GetLocalPlayer() && !engine->IsPaused();
		if ( !bOn )
		{
			Stop();
			return;
		}
		if ( m_bPlaying && m_iGuid >= 0 && enginesound->IsSoundStillPlaying( m_iGuid ) )
			return;
		m_bPlaying = false;
		if ( gpGlobals->realtime < m_flNextAt )
			return;
		const RadioProgram_t &prog = s_RadioPrograms[m_iProgram];
		char szFile[128];
		Q_snprintf( szFile, sizeof( szFile ), prog.pszPrefix, m_iLine + 1 );
		enginesound->EmitAmbientSound( szFile, cl_rpg_radio_volume.GetFloat() );
		m_iGuid = enginesound->GetGuidForLastSoundEmitted();
		m_bPlaying = true;
		// следующая фраза через короткую паузу; между программами - длинная
		if ( ++m_iLine >= prog.count )
		{
			m_iLine = 0;
			m_iProgram = ( m_iProgram + 1 ) % ARRAYSIZE( s_RadioPrograms );
			m_flNextAt = gpGlobals->realtime + enginesound->GetSoundDuration( szFile ) + 14.0f;
		}
		else
			m_flNextAt = gpGlobals->realtime + enginesound->GetSoundDuration( szFile ) + 0.6f;
	}
	void Stop()
	{
		if ( m_bPlaying && m_iGuid >= 0 )
			enginesound->StopSoundByGuid( m_iGuid );
		m_bPlaying = false;
		m_iGuid = -1;
	}
	const char *ProgramToken() const { return s_RadioPrograms[m_iProgram].pszToken; }
	bool IsPlaying() const { return m_bPlaying; }
private:
	int m_iGuid, m_iProgram, m_iLine;
	float m_flNextAt;
	bool m_bPlaying;
};
static CRPGRadio g_RPGRadio;

// ---------------------------------------------------------------------------
// Браслет
// ---------------------------------------------------------------------------
class CRPGBracelet : public CRPGScreen, public IRPGClientListener
{
	DECLARE_CLASS_SIMPLE( CRPGBracelet, CRPGScreen );
public:
	CRPGBracelet() : BaseClass( "RPGBracelet" )
	{
		m_iTab = TAB_CITIZEN;
		m_iDataTab = DATA_QUESTS;
		m_iCat = CAT_ALL;
		m_iSelItem = -1;
		m_SelQuest[0] = 0;
		m_SelNote[0] = 0;
		m_iScroll = m_iScrollQuests = m_iScrollNotes = 0;
		m_bPerkPick = false;
		m_SelPerk[0] = 0;
		m_iHoverKind = 0;
		m_iHoverIndex = -1;
		m_flWeight = 0.0f;
		m_flMaxWeight = 0.0f;
		m_bOver = false;
		RPGClient().AddListener( this );
	}
	~CRPGBracelet() { RPGClient().RemoveListener( this ); }

	void OpenTab( int iTab )
	{
		if ( iTab >= 0 && iTab < TAB_COUNT )
			m_iTab = iTab;
		if ( !IsOpen() )
			Open();
		RequestInventory();
	}
	void Toggle()
	{
		if ( IsOpen() )
			Close();
		else
			OpenTab( m_iTab );
	}
	void RequestInventory() { engine->ClientCmd( "inventory_request\n" ); }

	// ---- данные инвентаря (clientmode_shared.cpp перенаправляет сюда, пока браслет открыт) ----
	void SetInventory( const CUtlVector<CUtlString> &items )
	{
		m_Items.RemoveAll();
		for ( int i = 0; i < items.Count(); ++i )
		{
			// "class:bullets[:clip1:clip2:ammoIdx:ammoCount]"
			Item_t it;
			it.raw = items[i];
			CUtlVector<char *> parts;
			V_SplitString( items[i].Get(), ":", parts );
			it.classname = parts.Count() ? parts[0] : "";
			it.bullets = parts.Count() > 1 ? atoi( parts[1] ) : 0;
			it.clip = parts.Count() > 2 ? atoi( parts[2] ) : -1;
			it.reserve = parts.Count() > 5 ? atoi( parts[5] ) : 0;
			it.isWeapon = parts.Count() > 2;
			parts.PurgeAndDeleteElements();
			if ( it.classname.IsEmpty() )
				continue;
			it.count = 1;
			// одинаковые неоружейные предметы - одной строкой
			bool bMerged = false;
			if ( !it.isWeapon && !IsAmmoClass( it.classname.Get() ) )
			{
				for ( int j = 0; j < m_Items.Count(); ++j )
				{
					if ( !m_Items[j].isWeapon && !Q_stricmp( m_Items[j].classname.Get(), it.classname.Get() ) )
					{
						++m_Items[j].count;
						bMerged = true;
						break;
					}
				}
			}
			if ( !bMerged )
				m_Items.AddToTail( it );
		}
		if ( m_iSelItem >= m_Items.Count() )
			m_iSelItem = m_Items.Count() - 1;
	}
	void SetWeight( float flCur, float flMax, bool bOver )
	{
		m_flWeight = flCur;
		m_flMaxWeight = flMax;
		m_bOver = bOver;
	}

	// ---- IRPGClientListener ----
	virtual bool OnRPGOpenUI( const char *pszScreen, const char *pszParam, int iSource )
	{
		if ( !Q_stricmp( pszScreen, "levelup" ) )
		{
			m_iTab = TAB_CITIZEN;
			m_bPerkPick = RPGClient().GetPerkPicks() > 0 && RPGClient().GetSkillPoints() <= 0;
			OpenTab( TAB_CITIZEN );
			return true;
		}
		return false;
	}

protected:
	virtual void OnOpened()
	{
		engine->ClientCmd( "inventory_ui_open 1\n" );
		PlayUISound( "buttons/combine_button1.wav" );
	}
	virtual void OnClosed()
	{
		engine->ClientCmd( "inventory_ui_open 0\n" );
		m_bPerkPick = false;
	}

	virtual void OnKey( KeyCode code )
	{
		if ( code == KEY_TAB || code == KEY_I )
		{
			Close();
			return;
		}
		if ( code == KEY_Q || code == KEY_LEFT )
			SwitchTab( ( m_iTab + TAB_COUNT - 1 ) % TAB_COUNT );
		else if ( code == KEY_E || code == KEY_RIGHT )
			SwitchTab( ( m_iTab + 1 ) % TAB_COUNT );
		else if ( code >= KEY_1 && code <= KEY_5 )
			SwitchTab( code - KEY_1 );
	}
	void SwitchTab( int iTab )
	{
		if ( iTab == m_iTab )
			return;
		m_iTab = iTab;
		m_bPerkPick = false;
		m_iHoverKind = 0;
		PlayUISound( "ui/buttonrollover.wav" );
		if ( m_iTab == TAB_PROPERTY )
			RequestInventory();
	}

	virtual void OnWheel( int delta )
	{
		if ( m_iTab == TAB_PROPERTY )
			m_iScroll = Max( 0, m_iScroll - delta );
		else if ( m_iTab == TAB_DATA && m_iDataTab == DATA_QUESTS )
			m_iScrollQuests = Max( 0, m_iScrollQuests - delta );
		else if ( m_iTab == TAB_DATA && m_iDataTab == DATA_NOTES )
			m_iScrollNotes = Max( 0, m_iScrollNotes - delta );
	}

	virtual void OnButton( int id, bool bRight );
	virtual void PaintScreen();

private:
	struct Item_t
	{
		CUtlString raw, classname;
		int bullets, clip, reserve, count;
		bool isWeapon;
	};

	void PaintHeader( int x0, int y0, int x1 );
	void PaintCitizen( int x0, int y0, int x1, int y1 );
	void PaintPerkPick( int x0, int y0, int x1, int y1 );
	void PaintProperty( int x0, int y0, int x1, int y1 );
	void PaintData( int x0, int y0, int x1, int y1 );
	void PaintQuests( int x0, int y0, int x1, int y1 );
	void PaintNotes( int x0, int y0, int x1, int y1 );
	void PaintRelations( int x0, int y0, int x1, int y1 );
	void PaintRations( int x0, int y0, int x1, int y1 );
	void PaintRadio( int x0, int y0, int x1, int y1 );
	void PaintInfoBox( int x0, int y0, int x1, int y1, const wchar_t *pTitle, const wchar_t *pText, const wchar_t *pFlavor = NULL );
	int PaintWrapped( HFont font, int x, int y, int w, const wchar_t *pText, const Color &clr, int maxLines = 99 );
	bool IsPerkAvailable( const RPGPerkDef_t &def, CUtlString *pReason );
	void SortedQuests( CUtlVector<int> &out );

	int m_iTab, m_iDataTab, m_iCat;
	CUtlVector<Item_t> m_Items;
	CUtlVector<int> m_VisibleItems;
	int m_iSelItem;
	char m_SelQuest[64];
	char m_SelNote[64];
	int m_iScroll, m_iScrollQuests, m_iScrollNotes;
	bool m_bPerkPick;
	char m_SelPerk[16];
	int m_iHoverKind, m_iHoverIndex;		// подсказка внизу «Гражданина»
	float m_flWeight, m_flMaxWeight;
	bool m_bOver;
};

static CRPGBracelet *s_pBracelet = NULL;

static CRPGBracelet *Bracelet()
{
	if ( !s_pBracelet )
		s_pBracelet = new CRPGBracelet();
	return s_pBracelet;
}

// ---------------------------------------------------------------------------
int CRPGBracelet::PaintWrapped( HFont font, int x, int y, int w, const wchar_t *pText, const Color &clr, int maxLines )
{
	if ( !pText || !pText[0] )
		return y;
	CUtlVector<Span_t> lines;
	WrapText( font, pText, w, lines );
	for ( int i = 0; i < lines.Count() && i < maxLines; ++i )
	{
		DrawText( font, x, y, clr, pText + lines[i].start, lines[i].end - lines[i].start );
		y += FontTall( font ) + S( 2 );
	}
	return y;
}

void CRPGBracelet::PaintInfoBox( int x0, int y0, int x1, int y1, const wchar_t *pTitle, const wchar_t *pText, const wchar_t *pFlavor )
{
	DrawRect( x0, y0, x1, y1, Color( 255, 255, 255, 6 ) );
	DrawFrame( x0, y0, x1, y1, clrLine );
	int y = y0 + S( 12 );
	if ( pTitle && pTitle[0] )
	{
		DrawText( GetFont( FONT_HEADER ), x0 + S( 14 ), y, clrAmber, pTitle );
		y += FontTall( GetFont( FONT_HEADER ) ) + S( 6 );
	}
	const int maxLines = Max( 1, ( y1 - y - S( 8 ) ) / ( FontTall( GetFont( FONT_BODY ) ) + S( 2 ) ) );
	y = PaintWrapped( GetFont( FONT_BODY ), x0 + S( 14 ), y, x1 - x0 - S( 28 ), pText, clrText, maxLines );
	if ( pFlavor && pFlavor[0] )
		PaintWrapped( GetFont( FONT_BODY_SMALL ), x0 + S( 14 ), y + S( 6 ), x1 - x0 - S( 28 ), pFlavor, clrAccentDim, 3 );
}

// ---------------------------------------------------------------------------
void CRPGBracelet::PaintScreen()
{
	DrawDimmer( 200 );
	const int sw = ScreenW(), sh = ScreenH();
	const int W = Min( S( 1420 ), sw - S( 32 ) ), H = Min( S( 800 ), sh - S( 32 ) );
	const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
	DrawWindow( x0, y0, x1, y1 );
	const int pad = S( 26 );
	PaintHeader( x0 + pad, y0 + pad - S( 4 ), x1 - pad );

	// вкладки
	const int ty = y0 + S( 84 );
	const int tabW = ( W - pad * 2 ) / TAB_COUNT;
	HFont fTab = GetFont( FONT_HEADER );
	wchar_t w[256];
	for ( int t = 0; t < TAB_COUNT; ++t )
	{
		const int tx0 = x0 + pad + t * tabW, tx1 = tx0 + tabW - S( 6 );
		const bool bHover = Button( BTN_TAB + t, tx0, ty, tx1, ty + S( 40 ) );
		const bool bActive = t == m_iTab;
		DrawRect( tx0, ty, tx1, ty + S( 40 ), bActive ? Color( 110, 190, 255, 34 ) : ( bHover ? Color( 110, 190, 255, 18 ) : Color( 255, 255, 255, 5 ) ) );
		DrawRect( tx0, ty + S( 38 ), tx1, ty + S( 40 ), bActive ? clrAmber : clrLine );
		Localize( s_pszTabTokens[t], w, sizeof( w ) );
		wchar_t wNum[8];
		V_snwprintf( wNum, ARRAYSIZE( wNum ), L"%d", t + 1 );
		DrawText( GetFont( FONT_MONO_SMALL ), tx0 + S( 10 ), ty + S( 12 ), clrTextFaint, wNum );
		DrawText( fTab, tx0 + ( tx1 - tx0 - TextWidth( fTab, w ) ) / 2, ty + ( S( 40 ) - FontTall( fTab ) ) / 2, bActive ? clrAmber : ( bHover ? clrText : clrTextDim ), w );
		// значок «есть очки»: на «Гражданине» при непотраченных очках/перках
		if ( t == TAB_CITIZEN && ( RPGClient().GetSkillPoints() > 0 || RPGClient().GetPerkPicks() > 0 ) )
			DrawDiamond( (float)( tx1 - S( 16 ) ), (float)( ty + S( 20 ) ), (float)S( 5 ), Fade( clrAmber, 0.6f + 0.4f * sinf( gpGlobals->realtime * 4.0f ) ) );
	}

	const int cy0 = ty + S( 56 ), cy1 = y1 - S( 54 );
	switch ( m_iTab )
	{
	case TAB_CITIZEN:
		if ( m_bPerkPick )
			PaintPerkPick( x0 + pad, cy0, x1 - pad, cy1 );
		else
			PaintCitizen( x0 + pad, cy0, x1 - pad, cy1 );
		break;
	case TAB_PROPERTY: PaintProperty( x0 + pad, cy0, x1 - pad, cy1 ); break;
	case TAB_DATA: PaintData( x0 + pad, cy0, x1 - pad, cy1 ); break;
	case TAB_RATIONS: PaintRations( x0 + pad, cy0, x1 - pad, cy1 ); break;
	case TAB_RADIO: PaintRadio( x0 + pad, cy0, x1 - pad, cy1 ); break;
	}

	// низ: подсказки и «закрыть»
	DrawRect( x0 + pad, y1 - S( 46 ), x1 - pad, y1 - S( 45 ), clrLine );
	Localize( "#HL2RPM_UI_BR_Hints", w, sizeof( w ) );
	DrawText( GetFont( FONT_MONO_SMALL ), x0 + pad, y1 - S( 32 ), clrTextDim, w );
	Localize( "#HL2RPM_UI_BR_Close", w, sizeof( w ) );
	TextButton( BTN_CLOSE, x1 - pad - S( 150 ), y1 - S( 40 ), x1 - pad, y1 - S( 10 ), w );
}

void CRPGBracelet::PaintHeader( int x0, int y0, int x1 )
{
	const C_RPGCharacter &c = RPGClient();
	wchar_t w[256], w2[256];
	HFont fTitle = GetFont( FONT_TITLE ), fSmall = GetFont( FONT_MONO_SMALL ), fBody = GetFont( FONT_BODY );
	Localize( "#HL2RPM_UI_BR_Title", w, sizeof( w ) );
	DrawText( fTitle, x0, y0, clrAmber, w );
	// имя и уровень
	Localize( c.GetName(), w2, sizeof( w2 ) );
	wchar_t wLvl[16];
	V_snwprintf( wLvl, ARRAYSIZE( wLvl ), L"%d", c.GetLevel() );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_NameLevel", "%s1  ·  Level %s2", w2, wLvl );
	DrawText( fBody, x0, y0 + FontTall( fTitle ) + S( 2 ), clrText, w );

	// справа: время, день, место, кредиты
	float flHours = 12.0f, flRise, flSet;
	const bool bTime = RPG_GetWorldQuery() && RPG_GetWorldQuery()->GetTimeOfDay( flHours, flRise, flSet );
	const int iH = (int)flHours % 24, iM = (int)( ( flHours - floorf( flHours ) ) * 60.0f );
	wchar_t wTime[32], wDay[16];
	if ( bTime )
		V_snwprintf( wTime, ARRAYSIZE( wTime ), L"%02d:%02d", iH, iM );
	else
		V_wcsncpy( wTime, L"--:--", sizeof( wTime ) );
	V_snwprintf( wDay, ARRAYSIZE( wDay ), L"%d", Max( 1, c.GetFlagInt( RPG_FLAG_DAY, 1 ) ) );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_TimeDay", "%s1  ·  DAY %s2", wTime, wDay );
	DrawTextRight( GetFont( FONT_HEADER ), x1, y0 + S( 2 ), clrText, w );

	// место: последняя открытая локация этой карты (на вокзале - «Вокзал», а не «Поезд»), иначе первая
	char szMap[MAX_PATH];
	V_FileBase( engine->GetLevelName(), szMap, sizeof( szMap ) );
	w2[0] = 0;
	const CUtlVector<CUtlString> &found = c.Data().m_Locations;
	for ( int i = found.Count() - 1; i >= 0 && !w2[0]; --i )
	{
		const RPGLocationDef_t *pLoc = RPGData().FindLocation( found[i].Get() );
		if ( pLoc && !Q_stricmp( pLoc->map.Get(), szMap ) )
			Localize( pLoc->name.Get(), w2, sizeof( w2 ) );
	}
	for ( int i = 0; i < RPGData().GetLocationCount() && !w2[0]; ++i )
	{
		const RPGLocationDef_t &loc = RPGData().GetLocation( i );
		if ( !Q_stricmp( loc.map.Get(), szMap ) )
			Localize( loc.name.Get(), w2, sizeof( w2 ) );
	}
	wchar_t wCr[16];
	V_snwprintf( wCr, ARRAYSIZE( wCr ), L"%d", c.GetCredits() );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_HUD_Credits", "%s1 cr.", wCr );
	if ( w2[0] )
	{
		wchar_t wLine[320];
		V_snwprintf( wLine, ARRAYSIZE( wLine ), L"%ls  \x00B7  %ls", w2, w );
		DrawTextRight( fSmall, x1, y0 + S( 6 ) + FontTall( GetFont( FONT_HEADER ) ), clrTextDim, wLine );
	}
	else
		DrawTextRight( fSmall, x1, y0 + S( 6 ) + FontTall( GetFont( FONT_HEADER ) ), clrTextDim, w );
}

// ---------------------------------------------------------------------------
// «Гражданин»
// ---------------------------------------------------------------------------
void CRPGBracelet::PaintCitizen( int x0, int y0, int x1, int y1 )
{
	const C_RPGCharacter &c = RPGClient();
	const CRPGCharacterData &d = c.Data();
	RPGData().EnsureLoaded();
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fMono = GetFont( FONT_MONO ), fSmall = GetFont( FONT_BODY_SMALL ),
		fMonoS = GetFont( FONT_MONO_SMALL ), fBig = GetFont( FONT_TITLE );
	wchar_t w[512], w2[128], w3[128];
	const int infoH = S( 120 );
	const int listY1 = y1 - infoH - S( 12 );
	int hoverKind = 0, hoverIndex = -1;

	// ---- колонка 1: карточка и Λ.А.М.Б.Д.А. ----
	const int c1x = x0, c1w = S( 400 );
	int y = y0;
	// опыт
	const int lvl = c.GetLevel();
	const int xpLo = RPGFormulas::XPForLevel( lvl ), xpHi = c.GetXPForNextLevel();
	V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", c.GetXP() );
	V_snwprintf( w3, ARRAYSIZE( w3 ), L"%d", xpHi );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_XP", "XP %s1 / %s2", w2, w3 );
	DrawText( fMonoS, c1x, y, clrTextDim, w );
	y += FontTall( fMonoS ) + S( 4 );
	DrawBar( c1x, y, c1x + c1w, y + S( 5 ), xpHi > xpLo ? ( c.GetXP() - xpLo ) / (float)( xpHi - xpLo ) : 1.0f, clrAccent, Color( 255, 255, 255, 24 ) );
	y += S( 14 );
	// статус, здоровье, вес
	if ( !c.IsChargenDone() )
		Localize( "#HL2RPM_UI_HUD_Unregistered", w2, sizeof( w2 ) );
	else if ( c.HasTempPass() )
		Localize( "#RPG_Civil_temppass", w2, sizeof( w2 ) );
	else
		Localize( CFmtStr( "#RPG_Civil_%s", RPGFormulas::CivilTierKey( c.GetCivilTier() ) ), w2, sizeof( w2 ) );
	Localize( "#RPG_Civil_Title", w3, sizeof( w3 ) );
	V_snwprintf( w, ARRAYSIZE( w ), L"%ls: %ls", w3, w2 );
	DrawText( fBody, c1x, y, clrText, w );
	y += FontTall( fBody ) + S( 2 );
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	wchar_t wHp[32], wKg[32];
	V_snwprintf( wHp, ARRAYSIZE( wHp ), L"%d / %d", pPlayer ? pPlayer->GetHealth() : 0, c.GetMaxHealth() );
	V_snwprintf( wKg, ARRAYSIZE( wKg ), L"%.1f / %.0f", m_flWeight, m_flMaxWeight > 0.0f ? m_flMaxWeight : c.GetCarryWeight() );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_HpCarry", "Health %s1   Carry %s2 kg", wHp, wKg );
	DrawText( fSmall, c1x, y, clrTextDim, w );
	y += FontTall( fSmall ) + S( 16 );

	Localize( "#RPG_Lambda_Title", w, sizeof( w ) );
	DrawText( fHead, c1x, y, clrAccent, w );
	y += FontTall( fHead ) + S( 8 );
	const int rowH = Min( S( 46 ), ( listY1 - y ) / RPG_ATTR_COUNT );
	for ( int a = 0; a < RPG_ATTR_COUNT; ++a )
	{
		const int ry = y + a * rowH;
		const bool bHover = Button( BTN_HOVER_ATTR + a, c1x, ry, c1x + c1w, ry + rowH - S( 6 ) );
		if ( bHover ) { hoverKind = 1; hoverIndex = a; }
		DrawRect( c1x, ry, c1x + c1w, ry + rowH - S( 6 ), bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 6 ) );
		Localize( RPGData().GetAttr( a ).name.Get(), w, sizeof( w ) );
		Localize( CFmtStr( "#RPG_Attr_%s_Abbr", g_pszRPGAttrIds[a] ), w2, sizeof( w2 ) );
		DrawText( fMonoS, c1x + S( 10 ), ry + ( rowH - S( 6 ) - FontTall( fMonoS ) ) / 2, clrAccentDim, w2 );
		DrawText( fBody, c1x + S( 56 ), ry + ( rowH - S( 6 ) - FontTall( fBody ) ) / 2, clrText, w );
		const int v = c.GetAttr( a ), vb = c.GetAttrBase( a );
		V_snwprintf( w, ARRAYSIZE( w ), L"%d", v );
		DrawTextRight( fBig, c1x + c1w - S( 12 ), ry + ( rowH - S( 6 ) - FontTall( fBig ) ) / 2, v != vb ? clrGood : clrAmber, w );
		// 10 делений
		const int bx = c1x + S( 200 ), bw = S( 130 );
		for ( int k = 0; k < 10; ++k )
		{
			const int sx = bx + k * bw / 10;
			DrawRect( sx, ry + ( rowH - S( 6 ) ) / 2 - S( 3 ), sx + bw / 10 - S( 3 ), ry + ( rowH - S( 6 ) ) / 2 + S( 3 ), k < v ? clrAccent : Color( 255, 255, 255, 22 ) );
		}
	}

	// ---- колонка 2: навыки ----
	const int c2x = c1x + c1w + S( 30 ), c2w = S( 420 );
	y = y0;
	const int sp = c.GetSkillPoints();
	Localize( "#HL2RPM_UI_BR_Skills", w, sizeof( w ) );
	DrawText( fHead, c2x, y, clrAccent, w );
	if ( sp > 0 )
	{
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", sp );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_SkillPoints", "Points: %s1", w2 );
		DrawTextRight( fMono, c2x + c2w, y + S( 2 ), Fade( clrAmber, 0.7f + 0.3f * sinf( gpGlobals->realtime * 4.0f ) ), w );
	}
	y += FontTall( fHead ) + S( 8 );
	const int nSkills = Min( RPGData().GetSkillCount(), (int)RPG_MAX_SKILLS );
	const int sRow = nSkills ? Min( S( 34 ), ( listY1 - y ) / nSkills ) : S( 34 );
	for ( int s = 0; s < nSkills; ++s )
	{
		const int ry = y + s * sRow;
		const RPGSkillDef_t &def = RPGData().GetSkill( s );
		const bool bTag = c.IsSkillTagged( s );
		const bool bHover = Button( BTN_HOVER_SKILL + s, c2x, ry, c2x + c2w - ( sp > 0 ? S( 40 ) : 0 ), ry + sRow - S( 4 ) );
		if ( bHover ) { hoverKind = 4; hoverIndex = s; }
		DrawRect( c2x, ry, c2x + c2w, ry + sRow - S( 4 ), bHover ? Color( 110, 190, 255, 24 ) : ( bTag ? Color( 255, 186, 82, 16 ) : Color( 255, 255, 255, 5 ) ) );
		if ( bTag )
			DrawDiamond( (float)( c2x + S( 12 ) ), (float)( ry + ( sRow - S( 4 ) ) / 2 ), (float)S( 4 ), clrAmber );
		Localize( def.name.Get(), w, sizeof( w ) );
		DrawText( fBody, c2x + S( 26 ), ry + ( sRow - S( 4 ) - FontTall( fBody ) ) / 2, bTag ? clrAmber : clrText, w );
		const int val = c.GetSkill( s );
		V_snwprintf( w, ARRAYSIZE( w ), L"%d", val );
		DrawTextRight( fMono, c2x + c2w - ( sp > 0 ? S( 46 ) : S( 10 ) ), ry + ( sRow - S( 4 ) - FontTall( fMono ) ) / 2, bTag ? clrAmber : clrText, w );
		if ( sp > 0 )
			TextButton( BTN_SKILL_ADD + s, c2x + c2w - S( 36 ), ry + S( 2 ), c2x + c2w - S( 4 ), ry + sRow - S( 6 ), L"+", val < RPG_SKILL_CAP );
	}

	// ---- колонка 3: перки и черты ----
	const int c3x = c2x + c2w + S( 30 ), c3w = x1 - c3x;
	y = y0;
	Localize( "#HL2RPM_UI_BR_Perks", w, sizeof( w ) );
	DrawText( fHead, c3x, y, clrAccent, w );
	y += FontTall( fHead ) + S( 8 );
	if ( c.GetPerkPicks() > 0 )
	{
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", c.GetPerkPicks() );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_PickPerk", "Choose a perk (%s1)", w2 );
		TextButton( BTN_PERK_OPEN, c3x, y, c3x + c3w, y + S( 38 ), w, true, true );
		y += S( 46 );
	}
	int nPerks = 0;
	for ( int i = d.m_Perks.First(); i != d.m_Perks.InvalidIndex(); i = d.m_Perks.Next( i ) )
	{
		const RPGPerkDef_t *pDef = RPGData().FindPerk( d.m_Perks.GetElementName( i ) );
		if ( !pDef || d.m_Perks[i] <= 0 || y + S( 30 ) > listY1 - S( 120 ) )
			continue;
		const bool bHover = Button( BTN_HOVER_PERK + nPerks, c3x, y, c3x + c3w, y + S( 28 ) );
		if ( bHover ) { hoverKind = 3; hoverIndex = (int)( pDef - &RPGData().GetPerk( 0 ) ); }
		DrawRect( c3x, y, c3x + c3w, y + S( 28 ), bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) );
		Localize( pDef->name.Get(), w, sizeof( w ) );
		if ( pDef->ranks > 1 )
		{
			V_snwprintf( w2, ARRAYSIZE( w2 ), L"  %d/%d", d.m_Perks[i], pDef->ranks );
			V_wcsncat( w, w2, ARRAYSIZE( w ) );
		}
		DrawText( fBody, c3x + S( 10 ), y + ( S( 28 ) - FontTall( fBody ) ) / 2, clrText, w );
		y += S( 32 );
		++nPerks;
	}
	if ( !nPerks && c.GetPerkPicks() <= 0 )
	{
		Localize( "#HL2RPM_UI_BR_NoPerks", w, sizeof( w ) );
		DrawText( fSmall, c3x, y, clrTextFaint, w );
		y += FontTall( fSmall ) + S( 8 );
	}
	y += S( 14 );
	Localize( "#HL2RPM_UI_BR_Traits", w, sizeof( w ) );
	DrawText( fHead, c3x, y, clrAccent, w );
	y += FontTall( fHead ) + S( 8 );
	for ( int i = 0; i < d.m_Traits.Count(); ++i )
	{
		const RPGTraitDef_t *pDef = RPGData().FindTrait( d.m_Traits[i].Get() );
		if ( !pDef )
			continue;
		const bool bHover = Button( BTN_HOVER_TRAIT + i, c3x, y, c3x + c3w, y + S( 28 ) );
		if ( bHover ) { hoverKind = 2; hoverIndex = i; }
		DrawRect( c3x, y, c3x + c3w, y + S( 28 ), bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 186, 82, 14 ) );
		Localize( pDef->name.Get(), w, sizeof( w ) );
		DrawText( fBody, c3x + S( 10 ), y + ( S( 28 ) - FontTall( fBody ) ) / 2, clrAmber, w );
		y += S( 32 );
	}
	if ( !d.m_Traits.Count() )
	{
		Localize( "#HL2RPM_UI_BR_NoTraits", w, sizeof( w ) );
		DrawText( fSmall, c3x, y, clrTextFaint, w );
	}

	// ---- описание под курсором ----
	wchar_t wTitle[128] = L"", wText[1024] = L"", wFlavor[512] = L"";
	if ( hoverKind == 1 )
	{
		Localize( RPGData().GetAttr( hoverIndex ).name.Get(), wTitle, sizeof( wTitle ) );
		Localize( RPGData().GetAttr( hoverIndex ).desc.Get(), wText, sizeof( wText ) );
	}
	else if ( hoverKind == 2 )
	{
		const RPGTraitDef_t *pDef = RPGData().FindTrait( d.m_Traits[hoverIndex].Get() );
		if ( pDef )
		{
			Localize( pDef->name.Get(), wTitle, sizeof( wTitle ) );
			Localize( pDef->desc.Get(), wText, sizeof( wText ) );
		}
	}
	else if ( hoverKind == 3 && hoverIndex >= 0 && hoverIndex < RPGData().GetPerkCount() )
	{
		const RPGPerkDef_t &def = RPGData().GetPerk( hoverIndex );
		Localize( def.name.Get(), wTitle, sizeof( wTitle ) );
		Localize( def.desc.Get(), wText, sizeof( wText ) );
		if ( !def.flavor.IsEmpty() )
			Localize( def.flavor.Get(), wFlavor, sizeof( wFlavor ) );
	}
	else if ( hoverKind == 4 )
	{
		const RPGSkillDef_t &def = RPGData().GetSkill( hoverIndex );
		Localize( def.name.Get(), wTitle, sizeof( wTitle ) );
		Localize( def.desc.Get(), wText, sizeof( wText ) );
		if ( sp > 0 )
			Localize( "#HL2RPM_UI_BR_SkillAddHint", wFlavor, sizeof( wFlavor ) );
	}
	else
		Localize( sp > 0 ? "#HL2RPM_UI_BR_LevelHint" : "#HL2RPM_UI_BR_CitizenHint", wText, sizeof( wText ) );
	PaintInfoBox( x0, y1 - infoH, x1, y1, wTitle, wText, wFlavor );
}

bool CRPGBracelet::IsPerkAvailable( const RPGPerkDef_t &def, CUtlString *pReason )
{
	const C_RPGCharacter &c = RPGClient();
	if ( c.GetPerkRank( def.id.Get() ) >= Max( def.ranks, 1 ) )
	{
		if ( pReason ) *pReason = "taken";
		return false;
	}
	if ( c.GetLevel() < def.level )
	{
		if ( pReason ) pReason->Format( "level:%d", def.level );
		return false;
	}
	if ( !def.require.IsEmpty() )
	{
		RPGConditionList_t cond;
		cond.ParseExpression( def.require.Get() );
		RPGEvalResult_t r = cond.Evaluate( RPG_MakeEvalContext( C_BasePlayer::GetLocalPlayer() ) );
		if ( !r.met )
		{
			if ( pReason ) pReason->Format( "req:%s", r.reason.Get() );
			return false;
		}
	}
	return true;
}

void CRPGBracelet::PaintPerkPick( int x0, int y0, int x1, int y1 )
{
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL );
	wchar_t w[512], w2[64];
	V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", RPGClient().GetPerkPicks() );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_PickPerkTitle", "Choose a perk - %s1 left", w2 );
	DrawText( fHead, x0, y0, clrAmber, w );
	Localize( "#HL2RPM_UI_BR_Back", w, sizeof( w ) );
	TextButton( BTN_PERK_CLOSE, x1 - S( 160 ), y0 - S( 6 ), x1, y0 + S( 30 ), w );

	const int listX1 = x0 + S( 560 );
	int y = y0 + FontTall( fHead ) + S( 14 );
	// доступные - сверху
	CUtlVector<int> order;
	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( int i = 0; i < RPGData().GetPerkCount(); ++i )
		{
			const bool bAvail = IsPerkAvailable( RPGData().GetPerk( i ), NULL );
			if ( ( pass == 0 ) == bAvail )
				order.AddToTail( i );
		}
	}
	const int rowH = S( 30 );
	int iSel = -1;
	for ( int k = 0; k < order.Count(); ++k )
	{
		if ( y + rowH > y1 )
			break;
		const RPGPerkDef_t &def = RPGData().GetPerk( order[k] );
		CUtlString reason;
		const bool bAvail = IsPerkAvailable( def, &reason );
		const bool bSel = !Q_stricmp( m_SelPerk, def.id.Get() );
		if ( bSel )
			iSel = order[k];
		const bool bHover = Button( BTN_PERK + order[k], x0, y, listX1, y + rowH - S( 4 ) );
		DrawRect( x0, y, listX1, y + rowH - S( 4 ), bSel ? Color( 255, 186, 82, 40 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) ) );
		Localize( def.name.Get(), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), y + ( rowH - S( 4 ) - FontTall( fBody ) ) / 2, bAvail ? ( bSel ? clrAmber : clrText ) : clrTextFaint, w );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", def.level );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_PerkLevel", "lvl %s1", w2 );
		DrawTextRight( fSmall, listX1 - S( 10 ), y + ( rowH - S( 4 ) - FontTall( fSmall ) ) / 2, bAvail ? clrTextDim : clrTextFaint, w );
		y += rowH;
	}
	// описание выбранного
	if ( iSel >= 0 )
	{
		const RPGPerkDef_t &def = RPGData().GetPerk( iSel );
		CUtlString reason;
		const bool bAvail = IsPerkAvailable( def, &reason );
		wchar_t wTitle[128], wText[1024], wFlavor[512] = L"";
		Localize( def.name.Get(), wTitle, sizeof( wTitle ) );
		Localize( def.desc.Get(), wText, sizeof( wText ) );
		if ( !def.flavor.IsEmpty() )
			Localize( def.flavor.Get(), wFlavor, sizeof( wFlavor ) );
		const int bx0 = listX1 + S( 24 );
		PaintInfoBox( bx0, y0 + FontTall( fHead ) + S( 14 ), x1, y1 - S( 60 ), wTitle, wText, wFlavor );
		if ( !bAvail )
		{
			// почему нельзя: уровень или условие
			if ( !Q_strnicmp( reason.Get(), "level:", 6 ) )
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( reason.Get() + 6, w2, sizeof( w2 ) );
				LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_PerkNeedLevel", "Requires level %s1", w2 );
			}
			else if ( !Q_stricmp( reason.Get(), "taken" ) )
				Localize( "#HL2RPM_UI_BR_PerkTaken", w, sizeof( w ) );
			else
			{
				wchar_t wReq[256];
				g_pVGuiLocalize->ConvertANSIToUnicode( reason.Get() + 4, wReq, sizeof( wReq ) );
				LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_PerkNeed", "Requires: %s1", wReq );
			}
			DrawText( fSmall, bx0, y1 - S( 44 ), clrBad, w );
		}
		Localize( "#HL2RPM_UI_BR_PerkTake", w, sizeof( w ) );
		TextButton( BTN_PERK_TAKE, x1 - S( 220 ), y1 - S( 48 ), x1, y1, w, bAvail && RPGClient().GetPerkPicks() > 0, true );
	}
	else
	{
		Localize( "#HL2RPM_UI_BR_PerkPickHint", w, sizeof( w ) );
		PaintWrapped( fBody, listX1 + S( 24 ), y0 + FontTall( fHead ) + S( 20 ), x1 - listX1 - S( 24 ), w, clrTextDim );
	}
}

// ---------------------------------------------------------------------------
// «Имущество»
// ---------------------------------------------------------------------------
void CRPGBracelet::PaintProperty( int x0, int y0, int x1, int y1 )
{
	const C_RPGCharacter &c = RPGClient();
	HFont fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL ), fMono = GetFont( FONT_MONO ), fMonoS = GetFont( FONT_MONO_SMALL );
	wchar_t w[512], w2[128], w3[128];

	// категории
	int cx = x0;
	for ( int k = 0; k < CAT_COUNT; ++k )
	{
		Localize( s_pszCatTokens[k], w, sizeof( w ) );
		const int bw = TextWidth( fSmall, w ) + S( 24 );
		const bool bHover = Button( BTN_CAT + k, cx, y0, cx + bw, y0 + S( 28 ) );
		const bool bOn = k == m_iCat;
		DrawRect( cx, y0, cx + bw, y0 + S( 28 ), bOn ? Color( 255, 186, 82, 40 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 6 ) ) );
		DrawFrame( cx, y0, cx + bw, y0 + S( 28 ), bOn ? clrAmber : clrLine );
		DrawText( fSmall, cx + S( 12 ), y0 + ( S( 28 ) - FontTall( fSmall ) ) / 2, bOn ? clrAmber : clrText, w );
		cx += bw + S( 6 );
	}
	// вес
	const float flMax = m_flMaxWeight > 0.0f ? m_flMaxWeight : c.GetCarryWeight();
	V_snwprintf( w2, ARRAYSIZE( w2 ), L"%.1f", m_flWeight );
	V_snwprintf( w3, ARRAYSIZE( w3 ), L"%.0f", flMax );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_Weight", "Weight %s1 / %s2 kg", w2, w3 );
	DrawTextRight( fMono, x1, y0 + S( 2 ), m_bOver ? clrBad : clrText, w );
	DrawBar( x1 - S( 260 ), y0 + S( 24 ), x1, y0 + S( 28 ), flMax > 0 ? m_flWeight / flMax : 0.0f, m_bOver ? clrBad : clrAccent, Color( 255, 255, 255, 24 ) );

	// надетый костюм
	int y = y0 + S( 44 );
	const char *pszArmor = c.GetArmorClass();
	const int listX1 = x0 + S( 820 );
	if ( pszArmor && pszArmor[0] )
	{
		ItemDisplayName( pszArmor, w2, sizeof( w2 ) );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_Wearing", "Wearing: %s1", w2 );
		DrawText( fBody, x0, y + S( 4 ), clrAmber, w );
		Localize( "#HL2RPM_UI_BR_Unequip", w, sizeof( w ) );
		TextButton( BTN_UNEQUIP, listX1 - S( 150 ), y, listX1, y + S( 30 ), w );
		y += S( 40 );
	}

	// список
	m_VisibleItems.RemoveAll();
	for ( int i = 0; i < m_Items.Count(); ++i )
	{
		if ( m_iCat == CAT_ALL || ItemCategory( m_Items[i].classname.Get() ) == m_iCat )
			m_VisibleItems.AddToTail( i );
	}
	// шапка таблицы
	Localize( "#HL2RPM_UI_BR_ColItem", w, sizeof( w ) );
	DrawText( fMonoS, x0 + S( 12 ), y, clrTextFaint, w );
	Localize( "#HL2RPM_UI_BR_ColCount", w, sizeof( w ) );
	DrawTextRight( fMonoS, listX1 - S( 200 ), y, clrTextFaint, w );
	Localize( "#HL2RPM_UI_BR_ColWeight", w, sizeof( w ) );
	DrawTextRight( fMonoS, listX1 - S( 100 ), y, clrTextFaint, w );
	Localize( "#HL2RPM_UI_BR_ColValue", w, sizeof( w ) );
	DrawTextRight( fMonoS, listX1 - S( 12 ), y, clrTextFaint, w );
	y += FontTall( fMonoS ) + S( 6 );
	const int rowH = S( 34 );
	const int nRows = Max( 1, ( y1 - y ) / rowH );
	m_iScroll = clamp( m_iScroll, 0, Max( 0, m_VisibleItems.Count() - nRows ) );
	if ( !m_VisibleItems.Count() )
	{
		Localize( m_Items.Count() ? "#HL2RPM_UI_BR_CatEmpty" : "#HL2RPM_UI_BR_InvEmpty", w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), y + S( 8 ), clrTextFaint, w );
	}
	for ( int r = 0; r < nRows && m_iScroll + r < m_VisibleItems.Count(); ++r )
	{
		const int idx = m_VisibleItems[m_iScroll + r];
		const Item_t &it = m_Items[idx];
		const int ry = y + r * rowH;
		const RPGItemDef_t *pDef = RPGData().FindItem( it.classname.Get() );
		const bool bSel = idx == m_iSelItem;
		const bool bHover = Button( BTN_ITEM + idx, x0, ry, listX1, ry + rowH - S( 4 ) );
		DrawRect( x0, ry, listX1, ry + rowH - S( 4 ), bSel ? Color( 255, 186, 82, 36 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) ) );
		if ( pDef && pDef->contraband > 0 )
			DrawRect( x0, ry, x0 + S( 3 ), ry + rowH - S( 4 ), clrBad );	// контрабанда - красная метка
		ItemDisplayName( it.classname.Get(), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), ry + ( rowH - S( 4 ) - FontTall( fBody ) ) / 2, bSel ? clrAmber : clrText, w );
		// количество: патроны, магазин оружия, штуки
		if ( IsAmmoClass( it.classname.Get() ) )
			V_snwprintf( w, ARRAYSIZE( w ), L"%d", it.bullets );
		else if ( it.isWeapon && it.clip >= 0 )
			V_snwprintf( w, ARRAYSIZE( w ), L"%d", it.clip );
		else
			V_snwprintf( w, ARRAYSIZE( w ), L"%d", it.count );
		DrawTextRight( fMono, listX1 - S( 200 ), ry + ( rowH - S( 4 ) - FontTall( fMono ) ) / 2, clrTextDim, w );
		const float kg = IsAmmoClass( it.classname.Get() ) ? ItemUnitWeight( it.classname.Get() ) * it.bullets : ItemUnitWeight( it.classname.Get() ) * it.count;
		V_snwprintf( w, ARRAYSIZE( w ), L"%.2f", kg );
		DrawTextRight( fMono, listX1 - S( 100 ), ry + ( rowH - S( 4 ) - FontTall( fMono ) ) / 2, clrTextDim, w );
		V_snwprintf( w, ARRAYSIZE( w ), L"%d", pDef ? pDef->value * Max( it.count, 1 ) : 0 );
		DrawTextRight( fMono, listX1 - S( 12 ), ry + ( rowH - S( 4 ) - FontTall( fMono ) ) / 2, clrTextDim, w );
	}
	if ( m_VisibleItems.Count() > nRows )
	{
		// полоса прокрутки
		const int sbx = listX1 + S( 6 );
		DrawRect( sbx, y, sbx + S( 3 ), y + nRows * rowH, Color( 255, 255, 255, 18 ) );
		const int th = Max( S( 20 ), nRows * rowH * nRows / m_VisibleItems.Count() );
		const int ty = y + ( nRows * rowH - th ) * m_iScroll / Max( 1, m_VisibleItems.Count() - nRows );
		DrawRect( sbx, ty, sbx + S( 3 ), ty + th, clrAccentDim );
	}

	// подробно о выбранном
	const int dx0 = listX1 + S( 26 );
	if ( m_iSelItem >= 0 && m_iSelItem < m_Items.Count() )
	{
		const Item_t &it = m_Items[m_iSelItem];
		const RPGItemDef_t *pDef = RPGData().FindItem( it.classname.Get() );
		wchar_t wTitle[128], wText[1024] = L"";
		ItemDisplayName( it.classname.Get(), wTitle, sizeof( wTitle ) );
		if ( pDef )
			Localize( pDef->desc.Get(), wText, sizeof( wText ) );
		else
			Localize( CFmtStr( "#RPG_Item_%s_Desc", it.classname.Get() ), wText, sizeof( wText ) );
		if ( wText[0] == L'R' && wText[1] == L'P' && wText[2] == L'G' )
			wText[0] = 0;
		const int by1 = y1 - S( 52 );
		PaintInfoBox( dx0, y0 + S( 44 ), x1, by1 - S( 60 ), wTitle, wText );
		// характеристики
		int iy = by1 - S( 52 );
		const int cat = ItemCategory( it.classname.Get() );
		Localize( s_pszCatTokens[cat], w2, sizeof( w2 ) );
		DrawText( fSmall, dx0, iy, clrTextDim, w2 );
		if ( pDef && pDef->contraband > 0 )
		{
			const wchar_t *pRoman = pDef->contraband >= 3 ? L"III" : ( pDef->contraband == 2 ? L"II" : L"I" );
			LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_Contraband", "Contraband (%s1)", pRoman );
			DrawTextRight( fSmall, x1, iy, clrBad, w );
		}
		// действия
		const char *pszUse = pDef ? pDef->use.Get() : "";
		const char *pszUseToken = "#HL2RPM_UI_BR_Use";
		if ( cat == CAT_ARMOR )
			pszUseToken = "#HL2RPM_UI_BR_Wear";
		else if ( cat == CAT_WEAPON && !IsAmmoClass( it.classname.Get() ) )
			pszUseToken = "#HL2RPM_UI_BR_Equip";
		else if ( !Q_strnicmp( pszUse, "readable", 8 ) || !Q_strnicmp( pszUse, "note", 4 ) )
			pszUseToken = "#HL2RPM_UI_BR_Read";
		Localize( pszUseToken, w, sizeof( w ) );
		TextButton( BTN_ITEM_USE, dx0, by1, dx0 + ( x1 - dx0 ) / 2 - S( 6 ), y1, w, !IsAmmoClass( it.classname.Get() ), true );
		Localize( "#HL2RPM_UI_BR_Drop", w, sizeof( w ) );
		TextButton( BTN_ITEM_DROP, dx0 + ( x1 - dx0 ) / 2 + S( 6 ), by1, x1, y1, w );
	}
	else
	{
		Localize( "#HL2RPM_UI_BR_PropertyHint", w, sizeof( w ) );
		PaintWrapped( fBody, dx0, y0 + S( 50 ), x1 - dx0, w, clrTextDim );
	}
}

// ---------------------------------------------------------------------------
// «Данные»
// ---------------------------------------------------------------------------
void CRPGBracelet::PaintData( int x0, int y0, int x1, int y1 )
{
	HFont fSmall = GetFont( FONT_BODY_SMALL );
	wchar_t w[128];
	int cx = x0;
	for ( int k = 0; k < DATA_COUNT; ++k )
	{
		Localize( s_pszDataTokens[k], w, sizeof( w ) );
		const int bw = TextWidth( fSmall, w ) + S( 30 );
		const bool bHover = Button( BTN_DATATAB + k, cx, y0, cx + bw, y0 + S( 28 ) );
		const bool bOn = k == m_iDataTab;
		DrawRect( cx, y0, cx + bw, y0 + S( 28 ), bOn ? Color( 255, 186, 82, 40 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 6 ) ) );
		DrawFrame( cx, y0, cx + bw, y0 + S( 28 ), bOn ? clrAmber : clrLine );
		DrawText( fSmall, cx + S( 15 ), y0 + ( S( 28 ) - FontTall( fSmall ) ) / 2, bOn ? clrAmber : clrText, w );
		cx += bw + S( 6 );
	}
	const int cy0 = y0 + S( 44 );
	if ( m_iDataTab == DATA_QUESTS )
		PaintQuests( x0, cy0, x1, y1 );
	else if ( m_iDataTab == DATA_NOTES )
		PaintNotes( x0, cy0, x1, y1 );
	else
		PaintRelations( x0, cy0, x1, y1 );
}

void CRPGBracelet::SortedQuests( CUtlVector<int> &out )
{
	out.RemoveAll();
	RPGQuestData().EnsureLoaded();
	const CRPGCharacterData &d = RPGClient().Data();
	// активные (главные, фракции, побочные), затем выполненные, затем проваленные
	static const int s_States[3] = { RPG_QUEST_ACTIVE, RPG_QUEST_COMPLETED, RPG_QUEST_FAILED };
	for ( int s = 0; s < 3; ++s )
	{
		for ( int cat = 0; cat < RPG_QCAT_COUNT; ++cat )
		{
			for ( int i = 0; i < RPGQuestData().Count(); ++i )
			{
				const RPGQuestDef_t &q = RPGQuestData().Get( i );
				if ( q.category == cat && RPGQuest_GetState( d, q.id.Get() ) == s_States[s] )
					out.AddToTail( i );
			}
		}
	}
}

void CRPGBracelet::PaintQuests( int x0, int y0, int x1, int y1 )
{
	const CRPGCharacterData &d = RPGClient().Data();
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL ), fMonoS = GetFont( FONT_MONO_SMALL ), fTitle = GetFont( FONT_TITLE );
	wchar_t w[1024], w2[256];
	CUtlVector<int> order;
	SortedQuests( order );
	const char *pszTracked = RPGQuest_GetTracked( d );
	const int listX1 = x0 + S( 470 );
	const int rowH = S( 34 );
	int y = y0;
	if ( !order.Count() )
	{
		Localize( "#HL2RPM_UI_BR_NoQuests", w, sizeof( w ) );
		DrawText( fBody, x0, y, clrTextFaint, w );
		return;
	}
	if ( !m_SelQuest[0] )
		Q_strncpy( m_SelQuest, ( pszTracked && pszTracked[0] ) ? pszTracked : RPGQuestData().Get( order[0] ).id.Get(), sizeof( m_SelQuest ) );
	const int nRows = Max( 1, ( y1 - y ) / rowH );
	m_iScrollQuests = clamp( m_iScrollQuests, 0, Max( 0, order.Count() - nRows ) );
	int lastState = -1;
	for ( int r = 0; r < nRows && m_iScrollQuests + r < order.Count(); ++r )
	{
		const RPGQuestDef_t &q = RPGQuestData().Get( order[m_iScrollQuests + r] );
		const int state = RPGQuest_GetState( d, q.id.Get() );
		const int ry = y + r * rowH;
		const bool bSel = !Q_stricmp( m_SelQuest, q.id.Get() );
		const bool bHover = Button( BTN_QUEST + order[m_iScrollQuests + r], x0, ry, listX1, ry + rowH - S( 4 ) );
		DrawRect( x0, ry, listX1, ry + rowH - S( 4 ), bSel ? Color( 255, 186, 82, 36 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) ) );
		if ( state != lastState )
			lastState = state;
		const Color clrState = state == RPG_QUEST_ACTIVE ? clrAmber : ( state == RPG_QUEST_COMPLETED ? clrGood : clrBad );
		DrawRect( x0, ry, x0 + S( 3 ), ry + rowH - S( 4 ), Fade( clrState, state == RPG_QUEST_ACTIVE ? 1.0f : 0.6f ) );
		if ( pszTracked && !Q_stricmp( pszTracked, q.id.Get() ) )
			DrawDiamond( (float)( x0 + S( 16 ) ), (float)( ry + ( rowH - S( 4 ) ) / 2 ), (float)S( 5 ), clrAmber );
		Localize( q.title.Get(), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 30 ), ry + ( rowH - S( 4 ) - FontTall( fBody ) ) / 2, state == RPG_QUEST_ACTIVE ? ( bSel ? clrAmber : clrText ) : clrTextDim, w );
		Localize( state == RPG_QUEST_ACTIVE ? "#HL2RPM_UI_BR_QActive" : ( state == RPG_QUEST_COMPLETED ? "#HL2RPM_UI_BR_QDone" : "#HL2RPM_UI_BR_QFailed" ), w, sizeof( w ) );
		DrawTextRight( fMonoS, listX1 - S( 10 ), ry + ( rowH - S( 4 ) - FontTall( fMonoS ) ) / 2, Fade( clrState, 0.8f ), w );
	}

	// подробно
	const RPGQuestDef_t *pQ = RPGQuestData().Find( m_SelQuest );
	if ( !pQ )
		return;
	const int dx0 = listX1 + S( 26 );
	int dy = y0;
	Localize( pQ->title.Get(), w, sizeof( w ) );
	DrawText( fTitle, dx0, dy, clrAmber, w );
	dy += FontTall( fTitle ) + S( 2 );
	Localize( CFmtStr( "#HL2RPM_UI_BR_QCat_%s", CRPGQuestData::CategoryKey( pQ->category ) ), w, sizeof( w ) );
	DrawText( fMonoS, dx0, dy, clrTextDim, w );
	dy += FontTall( fMonoS ) + S( 10 );
	Localize( pQ->desc.Get(), w, sizeof( w ) );
	dy = PaintWrapped( fBody, dx0, dy, x1 - dx0, w, clrText, 5 ) + S( 10 );
	const int state = RPGQuest_GetState( d, pQ->id.Get() );
	const int iStage = RPGQuest_GetStage( d, pQ->id.Get() );
	// пройденные стадии - кратко, текущая - с целями
	for ( int s = 0; s < pQ->stages.Count() && s <= iStage; ++s )
	{
		const RPGQuestStage_t &st = pQ->stages[s];
		const bool bCurrent = s == iStage && state == RPG_QUEST_ACTIVE;
		Localize( !st.title.IsEmpty() ? st.title.Get() : st.desc.Get(), w2, sizeof( w2 ) );
		DrawDiamond( (float)( dx0 + S( 6 ) ), (float)( dy + FontTall( fHead ) / 2 ), (float)S( 4 ), bCurrent ? clrAmber : clrTextFaint );
		DrawText( bCurrent ? fHead : fSmall, dx0 + S( 20 ), dy, bCurrent ? clrText : clrTextFaint, w2 );
		dy += FontTall( bCurrent ? fHead : fSmall ) + S( 4 );
		if ( bCurrent )
		{
			if ( !st.title.IsEmpty() && !st.desc.IsEmpty() )
			{
				Localize( st.desc.Get(), w, sizeof( w ) );
				dy = PaintWrapped( fSmall, dx0 + S( 20 ), dy, x1 - dx0 - S( 20 ), w, clrTextDim, 4 ) + S( 4 );
			}
			for ( int o = 0; o < st.objectives.Count(); ++o )
			{
				const RPGQuestObjective_t &obj = st.objectives[o];
				if ( obj.title.IsEmpty() )
					continue;
				const int cur = RPGQuest_GetProgress( d, pQ->id.Get(), o );
				const bool bDone = cur >= Max( obj.count, 1 );
				Localize( obj.title.Get(), w2, sizeof( w2 ) );
				if ( obj.count > 1 )
					V_snwprintf( w, ARRAYSIZE( w ), L"%ls  (%d/%d)", w2, Min( cur, obj.count ), obj.count );
				else
					V_wcsncpy( w, w2, sizeof( w ) );
				DrawFrame( dx0 + S( 22 ), dy + S( 3 ), dx0 + S( 34 ), dy + S( 15 ), bDone ? clrGood : clrAccentDim );
				if ( bDone )
					DrawRect( dx0 + S( 25 ), dy + S( 6 ), dx0 + S( 31 ), dy + S( 12 ), clrGood );
				DrawText( fSmall, dx0 + S( 42 ), dy, bDone ? clrTextFaint : ( obj.optional ? clrTextDim : clrText ), w );
				dy += FontTall( fSmall ) + S( 4 );
			}
		}
		if ( dy > y1 - S( 60 ) )
			break;
	}
	if ( state == RPG_QUEST_ACTIVE )
	{
		const bool bTracked = pszTracked && !Q_stricmp( pszTracked, pQ->id.Get() );
		Localize( bTracked ? "#HL2RPM_UI_BR_Untrack" : "#HL2RPM_UI_BR_Track", w, sizeof( w ) );
		TextButton( BTN_QUEST_TRACK, x1 - S( 240 ), y1 - S( 40 ), x1, y1, w, true, !bTracked );
	}
}

void CRPGBracelet::PaintNotes( int x0, int y0, int x1, int y1 )
{
	const CRPGCharacterData &d = RPGClient().Data();
	HFont fBody = GetFont( FONT_BODY ), fTitle = GetFont( FONT_TITLE );
	wchar_t w[4096];
	if ( !d.m_Notes.Count() )
	{
		Localize( "#HL2RPM_UI_BR_NoNotes", w, sizeof( w ) );
		DrawText( fBody, x0, y0, clrTextFaint, w );
		return;
	}
	const int listX1 = x0 + S( 420 ), rowH = S( 34 );
	if ( !m_SelNote[0] )
		Q_strncpy( m_SelNote, d.m_Notes.Tail().Get(), sizeof( m_SelNote ) );
	const int nRows = Max( 1, ( y1 - y0 ) / rowH );
	m_iScrollNotes = clamp( m_iScrollNotes, 0, Max( 0, d.m_Notes.Count() - nRows ) );
	for ( int r = 0; r < nRows && m_iScrollNotes + r < d.m_Notes.Count(); ++r )
	{
		// новые сверху
		const int idx = d.m_Notes.Count() - 1 - ( m_iScrollNotes + r );
		const char *pszId = d.m_Notes[idx].Get();
		const int ry = y0 + r * rowH;
		const bool bSel = !Q_stricmp( m_SelNote, pszId );
		const bool bHover = Button( BTN_NOTE + idx, x0, ry, listX1, ry + rowH - S( 4 ) );
		DrawRect( x0, ry, listX1, ry + rowH - S( 4 ), bSel ? Color( 255, 186, 82, 36 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) ) );
		Localize( CFmtStr( "#NOTE_%s_TITLE", pszId ), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), ry + ( rowH - S( 4 ) - FontTall( fBody ) ) / 2, bSel ? clrAmber : clrText, w );
	}
	const int dx0 = listX1 + S( 26 );
	Localize( CFmtStr( "#NOTE_%s_TITLE", m_SelNote ), w, sizeof( w ) );
	DrawText( fTitle, dx0, y0, clrAmber, w );
	Localize( CFmtStr( "#NOTE_%s_TEXT", m_SelNote ), w, sizeof( w ) );
	if ( w[0] == L'N' && w[1] == L'O' && w[2] == L'T' && w[3] == L'E' )
		w[0] = 0;
	DrawRect( dx0, y0 + FontTall( fTitle ) + S( 8 ), x1, y1, Color( 236, 226, 200, 10 ) );
	PaintWrapped( fBody, dx0 + S( 16 ), y0 + FontTall( fTitle ) + S( 22 ), x1 - dx0 - S( 32 ), w, Color( 230, 224, 208, 255 ) );
}

void CRPGBracelet::PaintRelations( int x0, int y0, int x1, int y1 )
{
	const C_RPGCharacter &c = RPGClient();
	const CRPGCharacterData &d = c.Data();
	HFont fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL ), fMonoS = GetFont( FONT_MONO_SMALL );
	wchar_t w[256];
	int y = y0;
	const int rowH = S( 44 );
	int shown = 0;
	for ( int i = 0; i < RPGData().GetFactionCount(); ++i )
	{
		const RPGFactionDef_t &f = RPGData().GetFaction( i );
		if ( f.service )
			continue;
		const bool bKnown = d.m_Factions.Find( f.id.Get() ) != d.m_Factions.InvalidIndex();
		if ( f.hidden && !bKnown )
			continue;
		if ( y + rowH > y1 )
			break;
		const int v = c.GetFaction( f.id.Get() );
		const int tier = c.GetFactionTier( f.id.Get() );
		DrawRect( x0, y, x1, y + rowH - S( 6 ), Color( 255, 255, 255, 5 ) );
		Localize( f.name.Get(), w, sizeof( w ) );
		DrawText( fBody, x0 + S( 12 ), y + ( rowH - S( 6 ) - FontTall( fBody ) ) / 2, clrText, w );
		// шкала -100..100 с нулём посередине
		const int bx0 = x0 + S( 420 ), bx1 = x1 - S( 240 ), bmid = ( bx0 + bx1 ) / 2, by = y + ( rowH - S( 6 ) ) / 2;
		DrawRect( bx0, by - S( 2 ), bx1, by + S( 2 ), Color( 255, 255, 255, 22 ) );
		DrawRect( bmid, by - S( 6 ), bmid + 1, by + S( 6 ), clrTextFaint );
		const int vx = bmid + ( bx1 - bmid ) * v / 100;
		const Color clrV = tier >= RPG_FTIER_RECOGNIZED ? clrGood : ( tier <= RPG_FTIER_DISLIKE ? clrBad : clrAccent );
		DrawRect( Min( bmid, vx ), by - S( 2 ), Max( bmid, vx ), by + S( 2 ), clrV );
		DrawDiamond( (float)vx, (float)by, (float)S( 5 ), clrV );
		Localize( CFmtStr( "#RPG_FactionTier_%s", RPGFormulas::FactionTierKey( tier ) ), w, sizeof( w ) );
		DrawText( fSmall, bx1 + S( 20 ), by - FontTall( fSmall ) / 2, clrV, w );
		V_snwprintf( w, ARRAYSIZE( w ), L"%+d", v );
		DrawTextRight( fMonoS, x1 - S( 12 ), by - FontTall( fMonoS ) / 2, clrTextDim, w );
		y += rowH;
		++shown;
	}
	if ( !shown )
	{
		Localize( "#HL2RPM_UI_BR_NoRelations", w, sizeof( w ) );
		DrawText( fBody, x0, y0, clrTextFaint, w );
	}
}

// ---------------------------------------------------------------------------
// «Пайки»
// ---------------------------------------------------------------------------
void CRPGBracelet::PaintRations( int x0, int y0, int x1, int y1 )
{
	const C_RPGCharacter &c = RPGClient();
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fHuge = GetFont( FONT_HUGE ), fMonoS = GetFont( FONT_MONO_SMALL );
	wchar_t w[1024], w2[256];
	// карточка гражданина
	const int cx1 = x0 + S( 560 );
	DrawRect( x0, y0, cx1, y0 + S( 250 ), Color( 110, 190, 255, 12 ) );
	DrawFrame( x0, y0, cx1, y0 + S( 250 ), clrLine );
	DrawBrackets( x0, y0, cx1, y0 + S( 250 ), S( 12 ), clrAccent );
	Localize( "#HL2RPM_UI_BR_CardTitle", w, sizeof( w ) );
	DrawText( fMonoS, x0 + S( 18 ), y0 + S( 14 ), clrAccentDim, w );
	Localize( c.GetName(), w, sizeof( w ) );
	DrawText( fHead, x0 + S( 18 ), y0 + S( 36 ), clrText, w );
	const char *pszTierKey = !c.IsChargenDone() ? NULL : ( c.HasTempPass() ? RPG_CIVIL_TEMPPASS_KEY : RPGFormulas::CivilTierKey( c.GetCivilTier() ) );
	if ( pszTierKey )
		Localize( CFmtStr( "#RPG_Civil_%s", pszTierKey ), w, sizeof( w ) );
	else
		Localize( "#HL2RPM_UI_HUD_Unregistered", w, sizeof( w ) );
	const int tier = c.GetCivilTier();
	DrawText( fHuge, x0 + S( 18 ), y0 + S( 70 ), !pszTierKey || c.HasTempPass() ? clrAmber : ( tier <= RPG_CIVIL_MALICIOUS ? clrBad : ( tier >= RPG_CIVIL_RELIABLE ? clrGood : clrText ) ), w );
	V_snwprintf( w2, ARRAYSIZE( w2 ), L"%+d", c.GetCivil() );
	LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_CivilValue", "Civil status index %s1", w2 );
	DrawText( fMonoS, x0 + S( 18 ), y0 + S( 70 ) + FontTall( fHuge ) + S( 6 ), clrTextDim, w );
	// нормы по статусу
	Localize( CFmtStr( "#HL2RPM_UI_BR_Norm_%s", pszTierKey ? pszTierKey : "none" ), w, sizeof( w ) );
	PaintWrapped( fBody, x0 + S( 18 ), y0 + S( 160 ), cx1 - x0 - S( 36 ), w, clrText, 4 );

	// справа: раздача, правила
	const int rx0 = cx1 + S( 30 );
	Localize( "#HL2RPM_UI_BR_RationsWhere", w, sizeof( w ) );
	DrawText( fHead, rx0, y0, clrAccent, w );
	Localize( "#HL2RPM_UI_BR_RationsText", w, sizeof( w ) );
	PaintWrapped( fBody, rx0, y0 + FontTall( fHead ) + S( 10 ), x1 - rx0, w, clrText, 12 );
	if ( c.HasTempPass() )
	{
		Localize( "#HL2RPM_UI_Bracelet_PassExpires", w, sizeof( w ) );
		PaintWrapped( fBody, x0, y0 + S( 280 ), cx1 - x0, w, clrAmber, 4 );
	}
}

// ---------------------------------------------------------------------------
// «Радио»
// ---------------------------------------------------------------------------
void CRPGBracelet::PaintRadio( int x0, int y0, int x1, int y1 )
{
	const C_RPGCharacter &c = RPGClient();
	HFont fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ), fSmall = GetFont( FONT_BODY_SMALL ), fMono = GetFont( FONT_MONO );
	wchar_t w[512];
	struct Station_t { const char *pszName, *pszDesc, *pszFlag; const wchar_t *pszFreq; };
	static const Station_t s_Stations[] =
	{
		{ "#HL2RPM_UI_BR_StVoice", "#HL2RPM_UI_BR_StVoiceDesc", NULL, L"101.7" },
		{ "#HL2RPM_UI_BR_StLambda", "#HL2RPM_UI_BR_StLambdaDesc", "radio_lambda", L"\x03BB" },
		{ "#HL2RPM_UI_BR_StRemnant", "#HL2RPM_UI_BR_StRemnantDesc", "radio_remnant", L"88.4" },
		{ "#HL2RPM_UI_BR_StCP", "#HL2RPM_UI_BR_StCPDesc", "radio_cp", L"CP-17" },
	};
	int y = y0;
	const int rowH = S( 78 );
	for ( int i = 0; i < (int)ARRAYSIZE( s_Stations ); ++i )
	{
		const Station_t &st = s_Stations[i];
		const bool bLocked = st.pszFlag && !c.IsFlagSet( st.pszFlag );
		const bool bOn = i == 0 && cl_rpg_radio.GetInt() == 1;
		const bool bHover = Button( BTN_RADIO + i, x0, y, x0 + S( 760 ), y + rowH - S( 8 ), !bLocked );
		DrawRect( x0, y, x0 + S( 760 ), y + rowH - S( 8 ), bOn ? Color( 255, 186, 82, 30 ) : ( bHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 5 ) ) );
		DrawFrame( x0, y, x0 + S( 760 ), y + rowH - S( 8 ), bOn ? clrAmber : clrLine );
		DrawText( fMono, x0 + S( 16 ), y + S( 12 ), bLocked ? clrTextFaint : clrAccent, st.pszFreq );
		Localize( bLocked ? "#HL2RPM_UI_BR_StLocked" : st.pszName, w, sizeof( w ) );
		DrawText( fHead, x0 + S( 120 ), y + S( 10 ), bLocked ? clrTextFaint : ( bOn ? clrAmber : clrText ), w );
		if ( !bLocked )
		{
			Localize( st.pszDesc, w, sizeof( w ) );
			DrawText( fSmall, x0 + S( 120 ), y + S( 14 ) + FontTall( fHead ), clrTextDim, w );
		}
		if ( bOn )
		{
			// «эфир»: бегущие полоски
			for ( int k = 0; k < 12; ++k )
			{
				const float hgt = 0.3f + 0.7f * fabsf( sinf( gpGlobals->realtime * ( 3.0f + k * 0.7f ) + k ) );
				const int bx = x0 + S( 620 ) + k * S( 10 );
				const int bh = (int)( S( 34 ) * hgt );
				DrawRect( bx, y + rowH / 2 - bh / 2 - S( 4 ), bx + S( 6 ), y + rowH / 2 + bh / 2 - S( 4 ), Fade( clrAmber, 0.7f ) );
			}
		}
		y += rowH;
	}
	// сейчас в эфире
	if ( cl_rpg_radio.GetInt() == 1 )
	{
		wchar_t wProg[128];
		Localize( g_RPGRadio.ProgramToken(), wProg, sizeof( wProg ) );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_BR_OnAir", "On air: %s1", wProg );
		DrawText( fBody, x0, y + S( 10 ), clrAmber, w );
	}
	Localize( "#HL2RPM_UI_BR_RadioHint", w, sizeof( w ) );
	PaintWrapped( fSmall, x0 + S( 800 ), y0, x1 - x0 - S( 800 ), w, clrTextDim );
}

// ---------------------------------------------------------------------------
void CRPGBracelet::OnButton( int id, bool bRight )
{
	if ( id == BTN_CLOSE )
	{
		Close();
		return;
	}
	if ( id >= BTN_TAB && id < BTN_TAB + TAB_COUNT )
	{
		SwitchTab( id - BTN_TAB );
		return;
	}
	if ( id >= BTN_DATATAB && id < BTN_DATATAB + DATA_COUNT )
	{
		m_iDataTab = id - BTN_DATATAB;
		return;
	}
	if ( id >= BTN_CAT && id < BTN_CAT + CAT_COUNT )
	{
		m_iCat = id - BTN_CAT;
		m_iScroll = 0;
		return;
	}
	if ( id >= BTN_SKILL_ADD && id < BTN_SKILL_ADD + RPG_MAX_SKILLS )
	{
		const int s = id - BTN_SKILL_ADD;
		const int n = Min( bRight ? 5 : 1, RPGClient().GetSkillPoints() );
		if ( n > 0 && s < RPGData().GetSkillCount() )
		{
			engine->ClientCmd( CFmtStr( "rpg_skill_add %s %d\n", RPGData().GetSkill( s ).id.Get(), n ) );
			PlayUISound( "buttons/combine_button3.wav" );
		}
		return;
	}
	if ( id == BTN_PERK_OPEN )
	{
		m_bPerkPick = true;
		m_SelPerk[0] = 0;
		return;
	}
	if ( id == BTN_PERK_CLOSE )
	{
		m_bPerkPick = false;
		return;
	}
	if ( id >= BTN_PERK && id < BTN_PERK + 200 )
	{
		const int i = id - BTN_PERK;
		if ( i < RPGData().GetPerkCount() )
			Q_strncpy( m_SelPerk, RPGData().GetPerk( i ).id.Get(), sizeof( m_SelPerk ) );
		return;
	}
	if ( id == BTN_PERK_TAKE )
	{
		const RPGPerkDef_t *pDef = RPGData().FindPerk( m_SelPerk );
		if ( pDef && IsPerkAvailable( *pDef, NULL ) && RPGClient().GetPerkPicks() > 0 )
		{
			engine->ClientCmd( CFmtStr( "rpg_perk_pick %s\n", pDef->id.Get() ) );
			PlayUISound( "friends/friend_join.wav" );
			// последний перк выбран - обратно к карточке
			if ( RPGClient().GetPerkPicks() <= 1 )
				m_bPerkPick = false;
		}
		return;
	}
	if ( id >= BTN_ITEM && id < BTN_ITEM + 900 )
	{
		m_iSelItem = id - BTN_ITEM;
		return;
	}
	if ( ( id == BTN_ITEM_USE || id == BTN_ITEM_DROP ) && m_iSelItem >= 0 && m_iSelItem < m_Items.Count() )
	{
		const char *pszClass = m_Items[m_iSelItem].classname.Get();
		engine->ClientCmd( CFmtStr( "%s %s\n", id == BTN_ITEM_USE ? "inventory_use" : "inventory_drop", pszClass ) );
		PlayUISound( id == BTN_ITEM_USE ? "buttons/combine_button1.wav" : "physics/cardboard/cardboard_box_impact_soft1.wav" );
		return;
	}
	if ( id == BTN_UNEQUIP )
	{
		engine->ClientCmd( "inventory_unequip_suit\n" );
		return;
	}
	if ( id >= BTN_QUEST && id < BTN_QUEST + 900 )
	{
		const int i = id - BTN_QUEST;
		if ( i < RPGQuestData().Count() )
			Q_strncpy( m_SelQuest, RPGQuestData().Get( i ).id.Get(), sizeof( m_SelQuest ) );
		return;
	}
	if ( id == BTN_QUEST_TRACK )
	{
		const char *pszTracked = RPGQuest_GetTracked( RPGClient().Data() );
		const bool bTracked = pszTracked && !Q_stricmp( pszTracked, m_SelQuest );
		engine->ClientCmd( CFmtStr( "rpg_quest_track \"%s\"\n", bTracked ? "" : m_SelQuest ) );
		return;
	}
	if ( id >= BTN_NOTE && id < BTN_NOTE + 900 )
	{
		const int i = id - BTN_NOTE;
		if ( i < RPGClient().Data().m_Notes.Count() )
			Q_strncpy( m_SelNote, RPGClient().Data().m_Notes[i].Get(), sizeof( m_SelNote ) );
		return;
	}
	if ( id == BTN_RADIO )
	{
		cl_rpg_radio.SetValue( cl_rpg_radio.GetInt() == 1 ? 0 : 1 );
		if ( cl_rpg_radio.GetInt() == 0 )
			g_RPGRadio.Stop();
		return;
	}
}

// ---------------------------------------------------------------------------
// Связь с инвентарём и командами
// ---------------------------------------------------------------------------
// rpg_barter.cpp: экран торговли тоже показывает инвентарь
bool RPGBarter_WantsInventory();
void RPGBarter_OnInventory( const CUtlVector<CUtlString> &items );

bool RPGBracelet_WantsInventory()
{
	return ( cl_rpg_bracelet.GetBool() && s_pBracelet && s_pBracelet->IsOpen() ) || RPGBarter_WantsInventory();
}

void RPGBracelet_OnInventory( const CUtlVector<CUtlString> &items )
{
	if ( RPGBarter_WantsInventory() )
		RPGBarter_OnInventory( items );
	Bracelet()->SetInventory( items );
}

void RPGBracelet_ItemName( const char *pszClass, wchar_t *pOut, int nBytes )
{
	ItemDisplayName( pszClass, pOut, nBytes );
}

void RPGBracelet_OnInventoryWeight( float flCur, float flMax, bool bOver )
{
	Bracelet()->SetWeight( flCur, flMax, bOver );
}

// inventory_request_local (TAB / I): true - браслет взял на себя
bool RPGBracelet_HandleInventoryKey()
{
	if ( !cl_rpg_bracelet.GetBool() || !C_BasePlayer::GetLocalPlayer() )
		return false;
	Bracelet()->Toggle();
	return true;
}

class CRPGBraceletSystem : public CAutoGameSystem
{
public:
	CRPGBraceletSystem() : CAutoGameSystem( "CRPGBraceletSystem" ) {}
	virtual void LevelInitPostEntity() { Bracelet(); }
	virtual void LevelShutdownPreEntity()
	{
		if ( s_pBracelet && s_pBracelet->IsOpen() )
			s_pBracelet->Close();
	}
};
static CRPGBraceletSystem g_RPGBraceletSystem;

CON_COMMAND_F( rpg_bracelet, "RPG: open the registry bracelet: rpg_bracelet [citizen|property|data|rations|radio]", FCVAR_CLIENTDLL )
{
	int iTab = -1;
	if ( args.ArgC() > 1 )
	{
		for ( int i = 0; i < TAB_COUNT; ++i )
		{
			if ( !Q_stricmp( args[1], s_pszTabIds[i] ) )
				iTab = i;
		}
	}
	if ( iTab < 0 && Bracelet()->IsOpen() )
	{
		Bracelet()->Close();
		return;
	}
	Bracelet()->OpenTab( iTab );
}

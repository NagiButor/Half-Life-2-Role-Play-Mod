//========= HL2RPM ============//
//
// RPG-интерфейс: HUD (Дизайн-документ Part I/14 §1). Один элемент рисует
//   ленту уведомлений (слева вверху, RPG_Notify) и баннер по центру (новый уровень, локация, задание),
//   компас со сторонами света и меткой задания (вверху, m_vecLocatorOrigin игрока),
//   «глаз» подозрения под компасом (в зонах контроля), отслеживаемое задание (справа вверху),
//   браслет учёта без костюма (здоровье, статус, кредиты; патроны справа внизу),
//   рамку маски ГО / визора Надзора.
// В костюме здоровье, броню и патроны рисует штатный HUD HL2 (у ГО и Надзора - синий).
//
//=============================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "view.h"
#include "c_basehlplayer.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_ui_common.h"
#include "hl2rpm/rpg_quest_data.h"
#include "hl2rpm/rpg_dialog_panel.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/rpg_formulas.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Panel.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"
#include "usermessages.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

ConVar cl_rpg_hud( "cl_rpg_hud", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "RPG HUD: notifications, compass, suspicion, tracked task, bracelet" );
static ConVar cl_rpg_hud_compass( "cl_rpg_hud_compass", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "RPG HUD: compass with the task marker" );
static ConVar cl_rpg_hud_tracker( "cl_rpg_hud_tracker", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "RPG HUD: tracked task and its objectives" );
static ConVar cl_rpg_hud_feed_time( "cl_rpg_hud_feed_time", "5", FCVAR_CLIENTDLL, "RPG HUD: seconds a notification stays on screen" );
static ConVar cl_rpg_hud_debug( "cl_rpg_hud_debug", "0", FCVAR_CLIENTDLL | FCVAR_CHEAT, "RPG HUD: 1 - draw every part (suspicion 40, test marker in front)" );

static const Color clrHudBack( 6, 10, 14, 255 );
static const Color clrHudNumber( 196, 228, 255, 255 );
static const Color clrSearch( 255, 140, 60, 255 );

// стороны света: пеленг 0 = север = +Y карты (как вид сверху Hammer), по часовой
static const char *s_pszCompassTokens[8] =
{
	"#HL2RPM_UI_Compass_N", "#HL2RPM_UI_Compass_NE", "#HL2RPM_UI_Compass_E", "#HL2RPM_UI_Compass_SE",
	"#HL2RPM_UI_Compass_S", "#HL2RPM_UI_Compass_SW", "#HL2RPM_UI_Compass_W", "#HL2RPM_UI_Compass_NW"
};
static const char *s_pszCompassFallback[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };

// подсказка у прицела (сервер: hl2rpm/rpg_usehint.cpp): вид и подпись (токен или текст)
static int s_iUseHintKind = 0;
static char s_szUseHint[160] = "";
static float s_flUseHintChanged = 0.0f;
static const char *s_pszUseVerbs[] = { "", "#HL2RPM_UI_Use_Talk", "#HL2RPM_UI_Use_Take", "#HL2RPM_UI_Use_Sleep",
	"#HL2RPM_UI_Use_Read", "#HL2RPM_UI_Use_Use" };

static void __MsgFunc_RPG_UseHint( bf_read &msg )
{
	s_iUseHintKind = msg.ReadByte();
	msg.ReadString( s_szUseHint, sizeof( s_szUseHint ) );
	s_flUseHintChanged = gpGlobals->realtime;
}

class CHudRPG : public CHudElement, public vgui::Panel, public IRPGClientListener
{
	DECLARE_CLASS_SIMPLE( CHudRPG, vgui::Panel );
public:
	CHudRPG( const char *pElementName );
	~CHudRPG();

	virtual void Init();
	virtual void LevelInit();
	virtual bool ShouldDraw();
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void OnThink();
	virtual void Paint();

	// IRPGClientListener
	virtual void OnRPGNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra );

private:
	struct FeedEntry_t
	{
		wchar_t text[256];
		Color accent;
		float start;
		int kind;
		int value;
	};
	struct Banner_t
	{
		wchar_t kicker[96];
		wchar_t title[160];
		wchar_t sub[256];
		Color clr;
	};

	void AddFeed( int kind, const wchar_t *pText, const Color &accent, int value );
	void AddBanner( const char *pszKicker, const wchar_t *pTitle, const wchar_t *pSub, const Color &clr );

	void PaintFeed();
	void PaintBanner();
	void PaintCompass( C_BaseHLPlayer *pPlayer );
	void PaintSuspicion();
	void PaintTracker();
	void PaintBracelet( C_BasePlayer *pPlayer );
	void PaintAmmo( C_BasePlayer *pPlayer );
	void PaintSuitFrame();
	void PaintUseHint();
	void DrawEye( float cx, float cy, float w, float openness, const Color &clr );

	CUtlVector<FeedEntry_t> m_Feed;
	CUtlVector<Banner_t> m_Banners;
	float m_flBannerStart;
	float m_flSuspAlpha;
	float m_flLastThink;
	float m_flShownHealth;
};

DECLARE_HUDELEMENT( CHudRPG );

CHudRPG::CHudRPG( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPG" )
{
	Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );
	SetProportional( false );
	SetPaintBackgroundEnabled( false );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
	SetHiddenBits( HIDEHUD_ALL );
	m_flBannerStart = -1.0f;
	m_flSuspAlpha = 0.0f;
	m_flLastThink = 0.0f;
	m_flShownHealth = -1.0f;
	RPGClient().AddListener( this );
}

CHudRPG::~CHudRPG()
{
	RPGClient().RemoveListener( this );
}

void CHudRPG::Init()
{
	usermessages->HookMessage( "RPG_UseHint", __MsgFunc_RPG_UseHint );
	LevelInit();
}

void CHudRPG::LevelInit()
{
	s_iUseHintKind = 0;
	s_szUseHint[0] = 0;
	m_Feed.RemoveAll();
	m_Banners.RemoveAll();
	m_flBannerStart = -1.0f;
	m_flSuspAlpha = 0.0f;
	m_flShownHealth = -1.0f;
}

bool CHudRPG::ShouldDraw()
{
	if ( !cl_rpg_hud.GetBool() || engine->IsLevelMainMenuBackground() )
		return false;
	return CHudElement::ShouldDraw();
}

void CHudRPG::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	LoadFonts( pScheme );
	SetPaintBackgroundEnabled( false );
	int w, h;
	surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
}

void CHudRPG::OnThink()
{
	int w, h;
	surface()->GetScreenSize( w, h );
	if ( w != GetWide() || h != GetTall() )
		SetBounds( 0, 0, w, h );

	const float now = gpGlobals->realtime;
	const float dt = clamp( now - m_flLastThink, 0.0f, 0.1f );
	m_flLastThink = now;

	// лента: истёкшие строки
	const float life = cl_rpg_hud_feed_time.GetFloat() + 0.6f;
	for ( int i = m_Feed.Count() - 1; i >= 0; --i )
	{
		if ( now - m_Feed[i].start > life )
			m_Feed.Remove( i );
	}
	// баннер: следующий из очереди
	if ( m_Banners.Count() )
	{
		if ( m_flBannerStart < 0.0f )
			m_flBannerStart = now;
		else if ( now - m_flBannerStart > 4.6f )
		{
			m_Banners.Remove( 0 );
			m_flBannerStart = m_Banners.Count() ? now : -1.0f;
		}
	}
	// «глаз»: плавно появляется в зонах контроля
	const C_RPGCharacter &c = RPGClient();
	const bool bShow = cl_rpg_hud_debug.GetBool() || c.IsAlarm() ||
		( c.GetZone() != RPG_ZONE_NONE && ( c.GetSuspicion() > 0.5f || c.GetZone() == RPG_ZONE_RESTRICTED ) );
	m_flSuspAlpha = Approach( bShow ? 1.0f : 0.0f, m_flSuspAlpha, dt * ( bShow ? 4.0f : 1.5f ) );
}

// ---------------------------------------------------------------------------
// Уведомления
// ---------------------------------------------------------------------------
void CHudRPG::AddFeed( int kind, const wchar_t *pText, const Color &accent, int value )
{
	if ( !pText || !pText[0] )
		return;
	const float now = gpGlobals->realtime;
	// опыт подряд складывается в одну строку
	if ( kind == RPG_NOTIFY_XP && m_Feed.Count() )
	{
		FeedEntry_t &last = m_Feed.Tail();
		if ( last.kind == RPG_NOTIFY_XP && now - last.start < 2.5f )
		{
			last.value += value;
			wchar_t wN[16];
			V_snwprintf( wN, ARRAYSIZE( wN ), L"%d", last.value );
			LocalizeFormat( last.text, sizeof( last.text ), "#HL2RPM_UI_Notify_XP", "+%s1 XP", wN );
			last.start = Max( last.start, now - 0.3f );
			return;
		}
	}
	while ( m_Feed.Count() >= 6 )
		m_Feed.Remove( 0 );
	FeedEntry_t &e = m_Feed[m_Feed.AddToTail()];
	V_wcsncpy( e.text, pText, sizeof( e.text ) );
	e.accent = accent;
	e.start = now;
	e.kind = kind;
	e.value = value;
}

void CHudRPG::AddBanner( const char *pszKicker, const wchar_t *pTitle, const wchar_t *pSub, const Color &clr )
{
	if ( m_Banners.Count() >= 4 )
		return;
	Banner_t &b = m_Banners[m_Banners.AddToTail()];
	Localize( pszKicker, b.kicker, sizeof( b.kicker ) );
	V_wcsncpy( b.title, pTitle ? pTitle : L"", sizeof( b.title ) );
	V_wcsncpy( b.sub, pSub ? pSub : L"", sizeof( b.sub ) );
	b.clr = clr;
}

void CHudRPG::OnRPGNotify( int iKind, const char *pszToken, int iValue, const char *pszExtra )
{
	char szText[512];
	if ( !RPG_FormatNotify( iKind, pszToken, iValue, pszExtra, szText, sizeof( szText ) ) )
		return;
	wchar_t wText[256];
	g_pVGuiLocalize->ConvertANSIToUnicode( szText, wText, sizeof( wText ) );

	Color accent = clrAccent;
	const char *pszSound = NULL;
	switch ( iKind )
	{
	case RPG_NOTIFY_XP:
		accent = clrAccent;
		break;
	case RPG_NOTIFY_LEVELUP:
	{
		accent = clrAmber;
		pszSound = "friends/friend_join.wav";
		wchar_t wLvl[16], wTitle[96], wSub[256];
		V_snwprintf( wLvl, ARRAYSIZE( wLvl ), L"%d", iValue );
		LocalizeFormat( wTitle, sizeof( wTitle ), "#HL2RPM_UI_Banner_Level", "Level %s1", wLvl );
		Localize( "#HL2RPM_UI_Notify_LevelUpHint", wSub, sizeof( wSub ) );
		AddBanner( "#HL2RPM_UI_Banner_LevelUp", wTitle, wSub, clrAmber );
		break;
	}
	case RPG_NOTIFY_REP:
	case RPG_NOTIFY_CIVIL:
	case RPG_NOTIFY_CREDITS:
		accent = iValue >= 0 ? clrGood : clrBad;
		break;
	case RPG_NOTIFY_SUSPICION:
	{
		const int tier = RPGFormulas::SuspicionTierFromKey( pszToken );
		accent = tier >= RPG_STIER_DETAIN ? clrBad : ( tier >= RPG_STIER_SEARCH ? clrSearch : ( tier >= RPG_STIER_ATTENTION ? clrAmber : clrAccentDim ) );
		if ( tier >= RPG_STIER_SEARCH )
			pszSound = "buttons/combine_button_locked.wav";
		break;
	}
	case RPG_NOTIFY_LOCATION:
	{
		accent = clrAmber;
		pszSound = "buttons/blip1.wav";
		char szName[192];
		const RPGLocationDef_t *pDef = RPGData().FindLocation( pszToken );
		RPG_LocalizeUTF8( pDef ? pDef->name.Get() : CFmtStr( "#RPG_Loc_%s", pszToken ).Access(), szName, sizeof( szName ), pszToken );
		wchar_t wName[160], wSub[64] = L"", wN[16];
		g_pVGuiLocalize->ConvertANSIToUnicode( szName, wName, sizeof( wName ) );
		if ( iValue > 0 )
		{
			V_snwprintf( wN, ARRAYSIZE( wN ), L"%d", iValue );
			LocalizeFormat( wSub, sizeof( wSub ), "#HL2RPM_UI_Notify_XP", "+%s1 XP", wN );
		}
		AddBanner( "#HL2RPM_UI_Banner_Location", wName, wSub, clrAmber );
		return;		// в ленту не дублировать: баннер и есть уведомление
	}
	case RPG_NOTIFY_QUEST:
	{
		const char *pszWhat = pszExtra ? pszExtra : "";
		accent = !Q_stricmp( pszWhat, "completed" ) ? clrGood : ( !Q_stricmp( pszWhat, "failed" ) ? clrBad : clrAmber );
		pszSound = "buttons/blip1.wav";
		// главные задания - ещё и баннером
		RPGQuestData().EnsureLoaded();
		const RPGQuestDef_t *pDef = RPGQuestData().Find( pszToken );
		if ( pDef && pDef->category == RPG_QCAT_MAIN && ( !Q_stricmp( pszWhat, "started" ) || !Q_stricmp( pszWhat, "completed" ) ) )
		{
			wchar_t wTitle[160];
			Localize( pDef->title.Get(), wTitle, sizeof( wTitle ) );
			AddBanner( !Q_stricmp( pszWhat, "started" ) ? "#HL2RPM_UI_Banner_QuestNew" : "#HL2RPM_UI_Banner_QuestDone", wTitle, L"",
				!Q_stricmp( pszWhat, "started" ) ? clrAmber : clrGood );
		}
		break;
	}
	case RPG_NOTIFY_NOTE:
	case RPG_NOTIFY_PERK:
		accent = clrAmber;
		pszSound = "buttons/blip1.wav";
		break;
	case RPG_NOTIFY_CHECK:
		accent = iValue ? clrGood : clrBad;
		break;
	case RPG_NOTIFY_ZONE:
		accent = iValue == RPG_ZONE_RESTRICTED ? clrBad : ( iValue ? clrAccent : clrAccentDim );
		pszSound = iValue ? "buttons/combine_button1.wav" : NULL;
		break;
	case RPG_NOTIFY_FAIL:
		accent = clrBad;
		pszSound = "buttons/button10.wav";
		break;
	case RPG_NOTIFY_ITEM:
		accent = iValue >= 0 ? clrText : clrTextDim;
		break;
	default:
		accent = clrTextDim;
		break;
	}
	AddFeed( iKind, wText, accent, iValue );
	if ( pszSound )
		PlayUISound( pszSound );
}

void CHudRPG::PaintFeed()
{
	const float now = gpGlobals->realtime;
	const float hold = cl_rpg_hud_feed_time.GetFloat();
	HFont font = GetFont( FONT_HUD_MEDIUM );
	const int h = FontTall( font ) + S( 12 );
	int y = S( 24 );
	for ( int i = 0; i < m_Feed.Count(); ++i )
	{
		const FeedEntry_t &e = m_Feed[i];
		const float t = now - e.start;
		float a = 1.0f;
		if ( t < 0.2f )
			a = t / 0.2f;
		else if ( t > hold )
			a = 1.0f - ( t - hold ) / 0.6f;
		a = clamp( a, 0.0f, 1.0f );
		if ( a <= 0.0f )
			continue;
		const float s = t < 0.25f ? 1.0f - t / 0.25f : 0.0f;
		const int x = S( 24 ) - (int)( s * s * S( 40 ) );
		const int tw = TextWidth( font, e.text );
		DrawFade( x, y, x + tw + S( 70 ), y + h, clrHudBack, (int)( 190 * a ), 0, true );
		DrawRect( x, y, x + S( 3 ), y + h, Fade( e.accent, a ) );
		DrawDiamond( (float)( x + S( 15 ) ), y + h * 0.5f, (float)S( 4 ), Fade( e.accent, a ) );
		DrawText( font, x + S( 26 ), y + ( h - FontTall( font ) ) / 2, Fade( clrText, a ), e.text );
		y += h + S( 4 );
	}
}

void CHudRPG::PaintBanner()
{
	if ( !m_Banners.Count() || m_flBannerStart < 0.0f )
		return;
	const Banner_t &b = m_Banners[0];
	const float t = gpGlobals->realtime - m_flBannerStart;
	float a = 1.0f;
	if ( t < 0.4f )
		a = t / 0.4f;
	else if ( t > 3.6f )
		a = 1.0f - ( t - 3.6f ) / 1.0f;
	a = clamp( a, 0.0f, 1.0f );
	if ( a <= 0.0f )
		return;
	const int cx = GetWide() / 2;
	int y = (int)( GetTall() * 0.2f );
	HFont fKick = GetFont( FONT_HUD_MEDIUM ), fTitle = GetFont( FONT_HUGE ), fSub = GetFont( FONT_HUD_SMALL );
	const int wTitle = TextWidth( fTitle, b.title );
	const int half = Max( wTitle / 2 + S( 60 ), S( 220 ) );
	// подложка
	DrawFade( cx - half - S( 120 ), y - S( 10 ), cx, y + S( 104 ), clrHudBack, 0, (int)( 170 * a ), true );
	DrawFade( cx, y - S( 10 ), cx + half + S( 120 ), y + S( 104 ), clrHudBack, (int)( 170 * a ), 0, true );
	// надзаголовок между линиями
	const int wk = TextWidth( fKick, b.kicker );
	DrawText( fKick, cx - wk / 2, y, Fade( clrAccent, a ), b.kicker );
	const int ly = y + FontTall( fKick ) / 2;
	DrawFade( cx - half, ly, cx - wk / 2 - S( 14 ), ly + 1, clrAccent, 0, (int)( 200 * a ), true );
	DrawFade( cx + wk / 2 + S( 14 ), ly, cx + half, ly + 1, clrAccent, (int)( 200 * a ), 0, true );
	y += FontTall( fKick ) + S( 4 );
	DrawText( fTitle, cx - wTitle / 2, y, Fade( b.clr, a ), b.title );
	y += FontTall( fTitle ) + S( 2 );
	if ( b.sub[0] )
	{
		const int ws = TextWidth( fSub, b.sub );
		DrawText( fSub, cx - ws / 2, y, Fade( clrTextDim, a ), b.sub );
	}
}

// ---------------------------------------------------------------------------
// Компас
// ---------------------------------------------------------------------------
void CHudRPG::PaintCompass( C_BaseHLPlayer *pPlayer )
{
	const int cw = S( 640 ), ch = S( 26 );
	const int cx = GetWide() / 2, x0 = cx - cw / 2, x1 = cx + cw / 2, y0 = S( 12 ), y1 = y0 + ch;
	DrawFade( x0, y0, cx, y1, clrHudBack, 0, 150, true );
	DrawFade( cx, y0, x1, y1, clrHudBack, 150, 0, true );
	DrawFade( x0, y1, cx, y1 + 1, clrAccent, 0, 120, true );
	DrawFade( cx, y1, x1, y1 + 1, clrAccent, 120, 0, true );

	const float yaw = MainViewAngles()[YAW];
	const float pxPerDeg = ( cw * 0.5f ) / 90.0f;
	HFont fBig = GetFont( FONT_HUD_MEDIUM ), fSmall = GetFont( FONT_HUD_SMALL );
	wchar_t w[32];
	for ( int b = 0; b < 360; b += 15 )
	{
		const float d = AngleNormalize( ( 90.0f - b ) - yaw );	// > 0 - левее центра
		if ( fabsf( d ) > 90.0f )
			continue;
		const int x = cx - (int)( d * pxPerDeg );
		const float edge = 1.0f - powf( fabsf( d ) / 90.0f, 3.0f );
		if ( b % 45 == 0 )
		{
			const int k = b / 45;
			Localize( s_pszCompassTokens[k], w, sizeof( w ) );
			if ( w[0] == L'H' && w[1] == L'L' )
				g_pVGuiLocalize->ConvertANSIToUnicode( s_pszCompassFallback[k], w, sizeof( w ) );
			HFont f = ( b % 90 == 0 ) ? fBig : fSmall;
			const Color c = ( b == 0 ) ? clrAmber : ( ( b % 90 == 0 ) ? clrText : clrTextDim );
			DrawText( f, x - TextWidth( f, w ) / 2, y0 + ( ch - FontTall( f ) ) / 2, Fade( c, edge ), w );
		}
		else
			DrawRect( x, y1 - S( 8 ), x + 1, y1 - S( 3 ), Fade( clrTextDim, edge * 0.8f ) );
	}
	// центр
	const float tx[3] = { (float)cx - S( 5 ), (float)cx + S( 5 ), (float)cx };
	const float ty[3] = { (float)y1 + S( 7 ), (float)y1 + S( 7 ), (float)y1 + S( 2 ) };
	DrawPolygon( 3, tx, ty, Fade( clrAccent, 0.8f ) );

	// метка задания (компасная метка игрока: квесты ставят её через SetLocatorTargetEntity)
	Vector target = pPlayer->m_HL2Local.m_vecLocatorOrigin;
	if ( cl_rpg_hud_debug.GetBool() && target == vec3_invalid )
	{
		Vector fwd;
		AngleVectors( MainViewAngles(), &fwd );
		target = MainViewOrigin() + fwd * 900.0f + Vector( 0, 300, 0 );
	}
	if ( target == vec3_invalid || target == vec3_origin )
		return;
	const Vector delta = target - MainViewOrigin();
	float d = AngleNormalize( RAD2DEG( atan2f( delta.y, delta.x ) ) - yaw );
	const bool bOff = fabsf( d ) > 86.0f;
	d = clamp( d, -86.0f, 86.0f );
	const float mx = (float)( cx - (int)( d * pxPerDeg ) );
	const float my = y0 + ch * 0.5f;
	const float pulse = 0.75f + 0.25f * sinf( gpGlobals->realtime * 4.0f );
	DrawDiamond( mx, my, (float)S( 9 ), Color( 0, 0, 0, 170 ) );
	DrawDiamond( mx, my, (float)S( 7 ), Fade( clrAmber, pulse ) );
	DrawDiamond( mx, my, (float)S( 3 ), Color( 40, 24, 6, 255 ) );
	if ( bOff )
	{
		// задание за спиной: стрелка в сторону поворота
		const float dir = d > 0 ? -1.0f : 1.0f;
		const float ax[3] = { mx + dir * S( 12 ), mx + dir * S( 20 ), mx + dir * S( 12 ) };
		const float ay[3] = { my - S( 5 ), my, my + S( 5 ) };
		DrawPolygon( 3, ax, ay, Fade( clrAmber, pulse ) );
	}
	// расстояние (1 ед. = 1 дюйм) и выше/ниже
	wchar_t wN[16], wDist[32];
	V_snwprintf( wN, ARRAYSIZE( wN ), L"%d", (int)( delta.Length() * 0.0254f + 0.5f ) );
	LocalizeFormat( wDist, sizeof( wDist ), "#HL2RPM_UI_Compass_Meters", "%s1 m", wN );
	const int dw = TextWidth( fSmall, wDist );
	DrawText( fSmall, (int)mx - dw / 2, y1 + S( 9 ), clrAmber, wDist );
	if ( fabsf( delta.z ) > 120.0f )
	{
		const float up = delta.z > 0 ? -1.0f : 1.0f;
		const float hx = mx + dw / 2 + S( 8 );
		const float hy = (float)( y1 + S( 9 ) + FontTall( fSmall ) / 2 );
		const float px[3] = { hx - S( 4 ), hx + S( 4 ), hx };
		const float py[3] = { hy - up * S( 3 ), hy - up * S( 3 ), hy + up * S( 4 ) };
		DrawPolygon( 3, px, py, clrAmber );
	}
}

// ---------------------------------------------------------------------------
// Подозрение
// ---------------------------------------------------------------------------
void CHudRPG::DrawEye( float cx, float cy, float w, float openness, const Color &clr )
{
	const int N = 12;
	float px[2 * N + 2], py[2 * N + 2];
	const float h = w * 0.5f * clamp( openness, 0.08f, 1.0f );
	for ( int i = 0; i <= N; ++i )
	{
		const float t = (float)i / N;
		px[i] = cx + ( t * 2.0f - 1.0f ) * w * 0.5f;
		py[i] = cy - h * 0.5f * sinf( M_PI_F * t );
		px[2 * N + 1 - i] = px[i];
		py[2 * N + 1 - i] = cy + h * 0.5f * sinf( M_PI_F * t );
	}
	DrawPolygon( 2 * N + 2, px, py, clr );
	// внутри - тёмное, радужка, зрачок
	for ( int i = 0; i < 2 * N + 2; ++i )
	{
		px[i] = cx + ( px[i] - cx ) * 0.82f;
		py[i] = cy + ( py[i] - cy ) * 0.7f;
	}
	DrawPolygon( 2 * N + 2, px, py, Color( 6, 10, 14, clr.a() ) );
	const float r = Min( h * 0.42f, w * 0.17f );
	DrawDisc( cx, cy, r, clr, 16 );
	DrawDisc( cx, cy, r * 0.45f, Color( 6, 10, 14, clr.a() ), 12 );
}

void CHudRPG::PaintSuspicion()
{
	if ( m_flSuspAlpha <= 0.01f )
		return;
	const C_RPGCharacter &c = RPGClient();
	float susp = c.GetSuspicion();
	int tier = c.GetSuspicionTier();
	if ( cl_rpg_hud_debug.GetBool() && susp <= 0.0f )
	{
		susp = 40.0f;
		tier = RPGFormulas::SuspicionTier( susp );
	}
	const float a = m_flSuspAlpha;
	Color clr = tier >= RPG_STIER_DETAIN ? clrBad : ( tier >= RPG_STIER_SEARCH ? clrSearch : ( tier >= RPG_STIER_ATTENTION ? clrAmber : clrAccent ) );
	if ( c.IsAlarm() || tier >= RPG_STIER_ALARM )
		clr = Fade( clrBad, 0.55f + 0.45f * fabsf( sinf( gpGlobals->realtime * 6.0f ) ) );
	const int cx = GetWide() / 2;
	const int y = S( 64 );
	const int ew = S( 34 );
	const int barW = S( 210 );
	const int totalW = ew + S( 10 ) + barW + S( 34 );
	const int x0 = cx - totalW / 2;
	DrawFade( x0 - S( 60 ), y - S( 6 ), cx, y + S( 34 ), clrHudBack, 0, (int)( 140 * a ), true );
	DrawFade( cx, y - S( 6 ), x0 + totalW + S( 60 ), y + S( 34 ), clrHudBack, (int)( 140 * a ), 0, true );
	DrawEye( (float)( x0 + ew / 2 ), (float)( y + S( 14 ) ), (float)ew, 0.45f + 0.55f * clamp( susp / 100.0f, 0.0f, 1.0f ), Fade( clr, a ) );

	HFont f = GetFont( FONT_HUD_SMALL );
	wchar_t wLabel[64], wTier[64], w[160], wN[16];
	Localize( "#HL2RPM_UI_HUD_Suspicion", wLabel, sizeof( wLabel ) );
	Localize( CFmtStr( "#HL2RPM_UI_Suspicion_%s", RPGFormulas::SuspicionTierKey( tier ) ), wTier, sizeof( wTier ) );
	V_snwprintf( w, ARRAYSIZE( w ), L"%ls  \x00B7  %ls", wLabel, wTier );
	const int bx = x0 + ew + S( 10 );
	DrawText( f, bx, y, Fade( clr, a ), w );
	const int barY = y + FontTall( f ) + S( 4 );
	DrawBar( bx, barY, bx + barW, barY + S( 4 ), susp / 100.0f, Fade( clr, a ), Color( 255, 255, 255, (int)( 30 * a ) ) );
	V_snwprintf( wN, ARRAYSIZE( wN ), L"%d", (int)( susp + 0.5f ) );
	HFont fNum = GetFont( FONT_HUD_MEDIUM );
	DrawText( fNum, bx + barW + S( 8 ), barY + S( 2 ) - FontTall( fNum ) / 2, Fade( clrText, a ), wN );
}

// ---------------------------------------------------------------------------
// Отслеживаемое задание
// ---------------------------------------------------------------------------
void CHudRPG::PaintTracker()
{
	const CRPGCharacterData &d = RPGClient().Data();
	const char *pszId = RPGQuest_GetTracked( d );
	if ( !pszId || !pszId[0] || RPGQuest_GetState( d, pszId ) != RPG_QUEST_ACTIVE )
		return;
	RPGQuestData().EnsureLoaded();
	const RPGQuestDef_t *pDef = RPGQuestData().Find( pszId );
	if ( !pDef )
		return;
	const int iStage = RPGQuest_GetStage( d, pszId );
	const RPGQuestStage_t *pStage = ( iStage >= 0 && iStage < pDef->stages.Count() ) ? &pDef->stages[iStage] : NULL;

	HFont fTitle = GetFont( FONT_HUD_MEDIUM ), fLine = GetFont( FONT_HUD_SMALL );
	const int xr = GetWide() - S( 28 );
	const int maxW = S( 340 );
	int y = S( 20 );
	wchar_t w[512];

	// строки считаются заранее, чтобы подложка была по высоте
	struct Line_t { wchar_t text[256]; Color clr; bool bTitle; bool bDone; };
	CUtlVector<Line_t> lines;
	{
		Line_t &l = lines[lines.AddToTail()];
		Localize( pDef->title.Get(), l.text, sizeof( l.text ) );
		l.clr = clrAmber;
		l.bTitle = true;
		l.bDone = false;
	}
	if ( pStage )
	{
		bool bAnyObjective = false;
		for ( int i = 0; i < pStage->objectives.Count(); ++i )
		{
			const RPGQuestObjective_t &o = pStage->objectives[i];
			if ( o.title.IsEmpty() )
				continue;
			bAnyObjective = true;
			Line_t &l = lines[lines.AddToTail()];
			Localize( o.title.Get(), w, sizeof( w ) );
			const int cur = RPGQuest_GetProgress( d, pszId, i );
			if ( o.count > 1 )
				V_snwprintf( l.text, ARRAYSIZE( l.text ), L"%ls  %d/%d", w, Min( cur, o.count ), o.count );
			else
				V_wcsncpy( l.text, w, sizeof( l.text ) );
			l.bDone = cur >= Max( o.count, 1 );
			l.clr = l.bDone ? clrTextFaint : ( o.optional ? clrTextDim : clrText );
			l.bTitle = false;
		}
		if ( !bAnyObjective )
		{
			// без подписей целей - заголовок стадии или начало описания
			Localize( !pStage->title.IsEmpty() ? pStage->title.Get() : pStage->desc.Get(), w, sizeof( w ) );
			CUtlVector<Span_t> wrap;
			WrapText( fLine, w, maxW - S( 16 ), wrap );
			for ( int i = 0; i < wrap.Count() && i < 3; ++i )
			{
				Line_t &l = lines[lines.AddToTail()];
				const int n = Min( wrap[i].end - wrap[i].start, (int)ARRAYSIZE( l.text ) - 1 );
				V_wcsncpy( l.text, w + wrap[i].start, ( n + 1 ) * sizeof( wchar_t ) );
				l.text[n] = 0;
				l.clr = clrText;
				l.bTitle = false;
				l.bDone = false;
			}
		}
	}
	int totalH = 0;
	for ( int i = 0; i < lines.Count(); ++i )
		totalH += FontTall( lines[i].bTitle ? fTitle : fLine ) + S( 4 );
	DrawFade( xr - maxW - S( 40 ), y - S( 8 ), xr + S( 28 ), y + totalH + S( 6 ), clrHudBack, 0, 150, true );
	DrawRect( xr + S( 8 ), y, xr + S( 10 ), y + totalH - S( 4 ), Fade( clrAmber, 0.8f ) );
	for ( int i = 0; i < lines.Count(); ++i )
	{
		const Line_t &l = lines[i];
		HFont f = l.bTitle ? fTitle : fLine;
		DrawTextRight( f, xr, y, l.clr, l.text );
		if ( !l.bTitle )
		{
			const int tw = TextWidth( f, l.text );
			const float dy = y + FontTall( f ) * 0.5f;
			if ( l.bDone )
				DrawRect( xr - tw, (int)dy, xr, (int)dy + 1, clrTextFaint );
			DrawDiamond( (float)( xr - tw - S( 9 ) ), dy, (float)S( 3 ), l.bDone ? clrTextFaint : clrAmber );
		}
		y += FontTall( f ) + S( 4 );
	}
}

// ---------------------------------------------------------------------------
// Браслет учёта (без костюма)
// ---------------------------------------------------------------------------
void CHudRPG::PaintBracelet( C_BasePlayer *pPlayer )
{
	if ( pPlayer->IsSuitEquipped() )
		return;
	const C_RPGCharacter &c = RPGClient();
	const int x0 = S( 28 ), y1 = GetTall() - S( 26 ), w = S( 290 ), h = S( 66 ), y0 = y1 - h;
	DrawRect( x0, y0, x0 + w, y1, Color( 6, 12, 18, 175 ) );
	DrawFrame( x0, y0, x0 + w, y1, Color( 110, 190, 255, 50 ) );
	DrawBrackets( x0, y0, x0 + w, y1, S( 8 ), Fade( clrAccent, 0.8f ) );

	HFont fLabel = GetFont( FONT_HUD_SMALL ), fNum = GetFont( FONT_HUD_NUMBERS ), fMed = GetFont( FONT_HUD_MEDIUM );
	wchar_t w1[96], w2[96];

	// здоровье: число догоняет плавно, при малом - красное мерцание
	const int hp = Max( pPlayer->GetHealth(), 0 );
	const int maxHp = Max( c.GetMaxHealth(), 1 );
	if ( m_flShownHealth < 0.0f )
		m_flShownHealth = (float)hp;
	m_flShownHealth = Approach( (float)hp, m_flShownHealth, Max( 1.0f, fabsf( hp - m_flShownHealth ) ) * 6.0f * gpGlobals->absoluteframetime );
	const float frac = clamp( hp / (float)maxHp, 0.0f, 1.0f );
	Color clrHp = clrHudNumber;
	if ( frac <= 0.25f )
		clrHp = Fade( clrBad, 0.6f + 0.4f * fabsf( sinf( gpGlobals->realtime * 5.0f ) ) );
	Localize( "#HL2RPM_UI_HUD_Health", w1, sizeof( w1 ) );
	DrawText( fLabel, x0 + S( 12 ), y0 + S( 7 ), clrAccentDim, w1 );
	V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", (int)( m_flShownHealth + 0.5f ) );
	DrawText( fNum, x0 + S( 12 ), y0 + S( 7 ) + FontTall( fLabel ), clrHp, w2 );
	DrawBar( x0 + S( 12 ), y1 - S( 9 ), x0 + S( 112 ), y1 - S( 6 ), frac, frac <= 0.25f ? clrBad : clrAccent, Color( 255, 255, 255, 28 ) );

	// разделитель и статус
	DrawRect( x0 + S( 126 ), y0 + S( 10 ), x0 + S( 127 ), y1 - S( 10 ), Color( 110, 190, 255, 60 ) );
	const int rx = x0 + S( 138 );
	Localize( "#HL2RPM_UI_HUD_Bracelet", w1, sizeof( w1 ) );
	DrawText( fLabel, rx, y0 + S( 7 ), clrAccentDim, w1 );
	Color clrStatus = clrText;
	if ( !c.IsChargenDone() )
	{
		Localize( "#HL2RPM_UI_HUD_Unregistered", w2, sizeof( w2 ) );
		clrStatus = clrAmber;
	}
	else if ( c.HasTempPass() )
	{
		Localize( "#RPG_Civil_temppass", w2, sizeof( w2 ) );
		clrStatus = clrAmber;
	}
	else
	{
		const int tier = c.GetCivilTier();
		Localize( CFmtStr( "#RPG_Civil_%s", RPGFormulas::CivilTierKey( tier ) ), w2, sizeof( w2 ) );
		clrStatus = tier <= RPG_CIVIL_MALICIOUS ? clrBad : ( tier == RPG_CIVIL_WATCHED ? clrAmber : ( tier >= RPG_CIVIL_RELIABLE ? clrGood : clrText ) );
	}
	DrawText( fMed, rx, y0 + S( 7 ) + FontTall( fLabel ) + S( 2 ), clrStatus, w2 );
	V_snwprintf( w1, ARRAYSIZE( w1 ), L"%d", c.GetCredits() );
	LocalizeFormat( w2, sizeof( w2 ), "#HL2RPM_UI_HUD_Credits", "%s1 cr.", w1 );
	DrawText( fLabel, rx, y1 - S( 6 ) - FontTall( fLabel ), clrTextDim, w2 );
}

void CHudRPG::PaintAmmo( C_BasePlayer *pPlayer )
{
	if ( pPlayer->IsSuitEquipped() )
		return;
	C_BaseCombatWeapon *pWpn = pPlayer->GetActiveWeapon();
	if ( !pWpn || !pWpn->UsesPrimaryAmmo() )
		return;
	const int clip = pWpn->UsesClipsForAmmo1() ? pWpn->Clip1() : -1;
	const int reserve = pPlayer->GetAmmoCount( pWpn->GetPrimaryAmmoType() );
	const int x1 = GetWide() - S( 28 ), y1 = GetTall() - S( 26 ), x0 = x1 - S( 170 ), y0 = y1 - S( 66 );
	DrawRect( x0, y0, x1, y1, Color( 6, 12, 18, 175 ) );
	DrawFrame( x0, y0, x1, y1, Color( 110, 190, 255, 50 ) );
	DrawBrackets( x0, y0, x1, y1, S( 8 ), Fade( clrAccent, 0.8f ) );
	HFont fLabel = GetFont( FONT_HUD_SMALL ), fNum = GetFont( FONT_HUD_NUMBERS ), fMed = GetFont( FONT_HUD_MEDIUM );
	wchar_t w[64];
	Localize( "#HL2RPM_UI_HUD_Ammo", w, sizeof( w ) );
	DrawText( fLabel, x0 + S( 12 ), y0 + S( 7 ), clrAccentDim, w );
	const int ny = y0 + S( 7 ) + FontTall( fLabel );
	if ( clip >= 0 )
	{
		V_snwprintf( w, ARRAYSIZE( w ), L"%d", clip );
		DrawText( fNum, x0 + S( 12 ), ny, clip == 0 ? clrBad : clrHudNumber, w );
		const int cw = TextWidth( fNum, w );
		V_snwprintf( w, ARRAYSIZE( w ), L"/ %d", reserve );
		DrawText( fMed, x0 + S( 20 ) + cw, ny + FontTall( fNum ) - FontTall( fMed ) - S( 4 ), clrTextDim, w );
	}
	else
	{
		V_snwprintf( w, ARRAYSIZE( w ), L"%d", reserve );
		DrawText( fNum, x0 + S( 12 ), ny, reserve == 0 ? clrBad : clrHudNumber, w );
	}
}

// ---------------------------------------------------------------------------
// Маска ГО / визор Надзора
// ---------------------------------------------------------------------------
void CHudRPG::PaintSuitFrame()
{
	const int suit = RPGClient().GetSuitType();
	if ( suit != RPG_SUIT_CP && suit != RPG_SUIT_OVERWATCH )
		return;
	const int sw = GetWide(), sh = GetTall();
	const bool bCP = suit == RPG_SUIT_CP;
	const Color clrMask = bCP ? Color( 0, 12, 30, 255 ) : Color( 0, 20, 34, 255 );
	// края маски темнее
	const int vw = S( 150 ), vh = S( 90 );
	DrawFade( 0, 0, vw, sh, clrMask, bCP ? 150 : 110, 0, true );
	DrawFade( sw - vw, 0, sw, sh, clrMask, 0, bCP ? 150 : 110, true );
	DrawFade( 0, 0, sw, vh, clrMask, bCP ? 120 : 80, 0, false );
	DrawFade( 0, sh - vh, sw, sh, clrMask, 0, bCP ? 120 : 80, false );
	// угловые скобки визора и подпись
	const Color clrLineMask = bCP ? Color( 120, 190, 255, 110 ) : Color( 150, 225, 255, 130 );
	const int m = S( 12 );
	DrawBrackets( m, m, sw - m, sh - m, S( 70 ), clrLineMask, 2 );
	HFont f = GetFont( FONT_HUD_SMALL );
	wchar_t w[128];
	Localize( bCP ? "#HL2RPM_UI_HUD_MaskCP" : "#HL2RPM_UI_HUD_MaskOW", w, sizeof( w ) );
	const int tw = TextWidth( f, w );
	DrawText( f, ( sw - tw ) / 2, sh - m - FontTall( f ) - S( 4 ), clrLineMask, w );
	if ( !bCP )
	{
		// визор: тонкие деления по центру
		const int cx = sw / 2, cy = sh / 2;
		DrawRect( cx - S( 60 ), cy, cx - S( 36 ), cy + 1, clrLineMask );
		DrawRect( cx + S( 36 ), cy, cx + S( 60 ), cy + 1, clrLineMask );
	}
}

// ---------------------------------------------------------------------------
// «[E] Говорить: Тамара Ильинична» под прицелом
// ---------------------------------------------------------------------------
void CHudRPG::PaintUseHint()
{
	if ( s_iUseHintKind <= 0 || s_iUseHintKind >= (int)ARRAYSIZE( s_pszUseVerbs ) )
		return;
	const float a = clamp( ( gpGlobals->realtime - s_flUseHintChanged ) / 0.15f, 0.0f, 1.0f );
	wchar_t wVerb[64], wName[160], wKey[16], wText[256];
	Localize( s_pszUseVerbs[s_iUseHintKind], wVerb, sizeof( wVerb ) );
	Localize( s_szUseHint, wName, sizeof( wName ) );
	const char *pszKey = engine->Key_LookupBinding( "+use" );
	char szKey[16];
	Q_strncpy( szKey, ( pszKey && pszKey[0] ) ? pszKey : "E", sizeof( szKey ) );
	Q_strupr( szKey );
	g_pVGuiLocalize->ConvertANSIToUnicode( szKey, wKey, sizeof( wKey ) );
	if ( wName[0] )
		V_snwprintf( wText, ARRAYSIZE( wText ), L"%ls: %ls", wVerb, wName );
	else
		V_wcsncpy( wText, wVerb, sizeof( wText ) );
	HFont fKey = GetFont( FONT_HUD_GLYPH ), fText = GetFont( FONT_HUD_MEDIUM );
	const int kw = Max( TextWidth( fKey, wKey ) + S( 12 ), S( 24 ) );
	const int tw = TextWidth( fText, wText );
	const int h = Max( FontTall( fText ), FontTall( fKey ) ) + S( 8 );
	const int total = kw + S( 10 ) + tw;
	const int x0 = ( GetWide() - total ) / 2, y0 = GetTall() / 2 + S( 46 );
	DrawFade( x0 - S( 40 ), y0 - S( 4 ), x0 + total / 2, y0 + h + S( 4 ), clrHudBack, 0, (int)( 150 * a ), true );
	DrawFade( x0 + total / 2, y0 - S( 4 ), x0 + total + S( 40 ), y0 + h + S( 4 ), clrHudBack, (int)( 150 * a ), 0, true );
	DrawRect( x0, y0, x0 + kw, y0 + h, Fade( clrAccent, 0.18f * a ) );
	DrawFrame( x0, y0, x0 + kw, y0 + h, Fade( clrAccent, a ) );
	DrawText( fKey, x0 + ( kw - TextWidth( fKey, wKey ) ) / 2, y0 + ( h - FontTall( fKey ) ) / 2, Fade( clrAccent, a ), wKey );
	DrawText( fText, x0 + kw + S( 10 ), y0 + ( h - FontTall( fText ) ) / 2, Fade( s_iUseHintKind == 1 ? clrAmber : clrText, a ), wText );
}

// ---------------------------------------------------------------------------
void CHudRPG::Paint()
{
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( !pLocal )
		return;
	C_BaseHLPlayer *pPlayer = dynamic_cast<C_BaseHLPlayer *>( pLocal );
	const bool bBusy = RPGDialogPanel_IsOpen() || RPGScreen_AnyOpen();
	// сценарная камера (point_viewcontrol: сон, катсцена) - только лента и баннеры
	const bool bViewControl = render->GetViewEntity() != 0 && render->GetViewEntity() != pLocal->entindex();
	if ( pLocal->IsAlive() && !bBusy && !bViewControl )
	{
		PaintSuitFrame();
		if ( cl_rpg_hud_compass.GetBool() && pPlayer && !pLocal->IsInAVehicle() )
			PaintCompass( pPlayer );
		PaintSuspicion();
		if ( cl_rpg_hud_tracker.GetBool() )
			PaintTracker();
		PaintBracelet( pLocal );
		PaintAmmo( pLocal );
		PaintUseHint();
	}
	// во сне и катсценах лента ждёт: строки покажутся, когда камера вернётся к игроку
	if ( bViewControl )
	{
		const float now = gpGlobals->realtime;
		for ( int i = 0; i < m_Feed.Count(); ++i )
			m_Feed[i].start = now;
		if ( m_flBannerStart >= 0.0f )
			m_flBannerStart = now;
		return;
	}
	PaintFeed();
	if ( !bBusy )
		PaintBanner();
}

// тест ленты и баннеров без игровых событий
CON_COMMAND_F( cl_rpg_hud_test, "RPG HUD: show test notifications (xp, level, location, quest, rep, fail)", FCVAR_CLIENTDLL | FCVAR_CHEAT )
{
	CHudRPG *pHud = GET_HUDELEMENT( CHudRPG );
	if ( !pHud )
		return;
	pHud->OnRPGNotify( RPG_NOTIFY_XP, "", 25, "check" );
	pHud->OnRPGNotify( RPG_NOTIFY_XP, "", 15, "check" );
	pHud->OnRPGNotify( RPG_NOTIFY_REP, "lambda", 5, "neutral" );
	pHud->OnRPGNotify( RPG_NOTIFY_CIVIL, "watched", -10, "" );
	pHud->OnRPGNotify( RPG_NOTIFY_ITEM, "item_temp_pass", 1, "" );
	pHud->OnRPGNotify( RPG_NOTIFY_FAIL, "#HL2RPM_UI_Fail_Condition", 0, "" );
	if ( args.ArgC() > 1 )
	{
		pHud->OnRPGNotify( RPG_NOTIFY_LEVELUP, "", 2, "13 1" );
		pHud->OnRPGNotify( RPG_NOTIFY_LOCATION, "c17_trainstation", 50, "" );
	}
}

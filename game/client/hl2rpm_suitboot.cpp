//========= HL2RPM ============================================================//
//
// Suits (M6): the colors of the HUD by the suit worn and the boot screen when a
// suit is put on.
//
// The server networks the theme of the suit (m_Local.m_iSuitTheme: 0 none, 1 HEV,
// 2 PCV, 3 Combine, 4 elite - hl2rpm_char.cpp). The HUD numbers of HL2 take the
// theme color for PCV / Combine / elite (gHUD.m_bCombineSuitActive) and the default
// HL2 colors for HEV; because the theme is networked state, a loaded game gets the
// right colors (the old "CombineSuit" message was lost on load - the blue HUD of the
// soldier suit stayed blue).
//
// The boot screen: a faint hexagon grid sweeps down, the name of the suit, the
// systems come up line by line ("... OK"), the user - the name of the character
// ("PETROV, IVAN" instead of "FREEMAN, GORDON"), a progress bar; 5 seconds.
//
// Work suits (worker, hazmat - 2026-11-02) boot as the cheap device they are: no visor grid and no
// hologram, a self-test on a small LCD with a hazard-striped progress bar, where their HUD will be.
//
//   rpm_suitboot [hev|pcv|combine|elite|worker|hazmat]    show it (tests)    cl_rpm_suitboot 0 - off
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_crosshair.h"
#include "iclientmode.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include "KeyValues.h"
#include "tier1/fmtstr.h"
#include <vgui/ILocalize.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

extern int RPMUI_SuitTheme();
extern KeyValues *RPMChar_ClientState();

static ConVar cl_rpm_suitboot( "cl_rpm_suitboot", "1", FCVAR_ARCHIVE, "HL2RPM: the boot screen of a suit when it is put on" );

static const float BOOT_TIME = 5.5f;
static int s_iBootTheme = 0;
static float s_flBootStart = -100.0f;

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

bool RPMSuitBoot_IsShowing() { return gpGlobals->realtime - s_flBootStart < BOOT_TIME; }

static void StartBoot( int theme, bool bForce = false )
{
	if ( ( !cl_rpm_suitboot.GetBool() && !bForce ) || theme <= THEME_NONE )
		return;
	s_iBootTheme = theme;
	s_flBootStart = gpGlobals->realtime;
	surface()->PlaySound( IsWorkTheme( theme ) ? "buttons/button3.wav" : theme >= THEME_COMBINE ? "buttons/combine_button1.wav" : "items/suitchargeok1.wav" );
}

// the user: "PETROV, IVAN" from "Ivan Petrov" (the HEV said "FREEMAN, GORDON")
static CUtlString UserName()
{
	KeyValues *st = RPMChar_ClientState();
	const char *name = st ? st->GetString( "name" ) : "";
	if ( !name[0] )
		return CUtlString( TR( "UNKNOWN", "НЕИЗВЕСТЕН" ) );
	CUtlStringList parts;
	V_SplitString( name, " ", parts );
	if ( parts.Count() < 2 )
		return CUtlString( name );
	CUtlString s = parts[parts.Count() - 1];
	s.Append( "," );
	for ( int i = 0; i < parts.Count() - 1; i++ )
		s.Append( CFmtStr( " %s", parts[i] ) );
	return s;
}

//=============================================================================
// the colors of the HUD by the theme (applied whenever it changes or something else changed them)
//=============================================================================
class CRPMSuitThemeSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMSuitThemeSystem() : CAutoGameSystemPerFrame( "CRPMSuitThemeSystem" ) { m_iApplied = -1; m_iLast = -1; m_flSettle = 0.0f; }

	virtual void LevelInitPostEntity()
	{
		m_iApplied = -1;
		m_iLast = -1;
		m_flSettle = gpGlobals->realtime + 2.0f;	// the state of a loaded game arrives: no boot screen for it
	}

	static Color ThemeColor( int theme )
	{
		switch ( theme )
		{
		case THEME_PCV: return Color( 120, 235, 100, 255 );
		case THEME_COMBINE: return Color( 90, 205, 230, 255 );
		case THEME_ELITE: return Color( 235, 80, 64, 255 );
		case THEME_WORKER: return Color( 242, 188, 44, 255 );
		case THEME_HAZMAT: return Color( 196, 232, 70, 255 );
		}
		return Color( 255, 255, 255, 255 );
	}

	virtual void Update( float frametime )
	{
		if ( !engine->IsInGame() || !C_BasePlayer::GetLocalPlayer() )
			return;
		int theme = RPMUI_SuitTheme();
		bool colored = theme == THEME_PCV || theme == THEME_COMBINE || theme == THEME_ELITE || IsWorkTheme( theme );
		Color want = colored ? ThemeColor( theme ) : gHUD.m_clrDefaultNormal;
		if ( theme != m_iApplied || gHUD.m_bCombineSuitActive != colored || gHUD.m_clrNormal != want )
		{
			gHUD.m_bCombineSuitActive = colored;
			gHUD.m_clrNormal = want;
			gHUD.m_clrYellowish = colored ? want : gHUD.m_clrDefaultYellowish;
			CHudCrosshair *pCross = GET_HUDELEMENT( CHudCrosshair );
			if ( pCross )
			{
				if ( colored && gHUD.GetIcon( "crosshair_default" ) )
					pCross->SetCrosshair( gHUD.GetIcon( "crosshair_default" ), want );
				else
					pCross->ResetCrosshair();
			}
			m_iApplied = theme;
		}
		// a suit put on during the game: its boot screen
		if ( theme != m_iLast )
		{
			if ( m_iLast >= 0 && theme > THEME_NONE && gpGlobals->realtime > m_flSettle )
				StartBoot( theme );
			m_iLast = theme;
		}
	}

	int m_iApplied, m_iLast;
	float m_flSettle;
};
static CRPMSuitThemeSystem g_RPMSuitThemeSystem;

//=============================================================================
// the boot screen
//=============================================================================
struct BootLine_t { const char *en, *ru; int status; };	// status: 0 none, 1 OK, 2 warning, 3 the user

static const BootLine_t s_HEVLines[] =
{
	{ "HEADS UP DISPLAY: ACTIVATED", "ИНДИКАЦИЯ НА ШЛЕМЕ: АКТИВИРОВАНА", 0 },
	{ "ATMOSPHERIC CONTAMINANT SENSORS", "ДАТЧИКИ ЗАГРЯЗНЕНИЯ АТМОСФЕРЫ", 1 },
	{ "VITAL SIGN MONITORING", "МОНИТОРИНГ ЖИЗНЕННЫХ ПОКАЗАТЕЛЕЙ", 1 },
	{ "AUTOMATIC MEDICAL SYSTEMS", "АВТОМАТИЧЕСКАЯ МЕДИЦИНСКАЯ СИСТЕМА", 1 },
	{ "DEFENSIVE WEAPON SELECTION SYSTEM", "СИСТЕМА ВЫБОРА ОРУЖИЯ", 1 },
	{ "MUNITION LEVEL MONITORING", "КОНТРОЛЬ БОЕЗАПАСА", 1 },
	{ "COMMUNICATIONS INTERFACE", "ИНТЕРФЕЙС СВЯЗИ", 1 },
	{ "USER", "ПОЛЬЗОВАТЕЛЬ", 3 },
};
static const BootLine_t s_PCVLines[] =
{
	{ "MAIN SYSTEMS REBOOT INITIATED", "ПЕРЕЗАПУСК ОСНОВНЫХ СИСТЕМ", 0 },
	{ "ARMOR PLATING", "БРОНЕПЛАСТИНЫ", 1 },
	{ "AIR FILTRATION", "ФИЛЬТРАЦИЯ ВОЗДУХА", 1 },
	{ "TARGETING RETICLE", "ПРИЦЕЛЬНАЯ РАМКА", 1 },
	{ "LONG RANGE COMMUNICATION", "ДАЛЬНЯЯ СВЯЗЬ", 2 },
	{ "USER", "ПОЛЬЗОВАТЕЛЬ", 3 },
};
static const BootLine_t s_EliteLines[] =
{
	{ "OVERWATCH TRANSHUMAN ARM: LINKED", "ТРАНСЧЕЛОВЕЧЕСКОЕ ПОДРАЗДЕЛЕНИЕ: СВЯЗЬ", 0 },
	{ "AUGMENTATION INTEGRITY", "ЦЕЛОСТНОСТЬ АУГМЕНТАЦИИ", 1 },
	{ "PAIN SUPPRESSION", "ПОДАВЛЕНИЕ БОЛИ", 1 },
	{ "THREAT ASSESSMENT", "ОЦЕНКА УГРОЗ", 1 },
	{ "PULSE WEAPON SYNC", "СИНХРОНИЗАЦИЯ ИМПУЛЬСНОГО ОРУЖИЯ", 1 },
	{ "MEMORY REPLACEMENT", "ЗАМЕЩЕНИЕ ПАМЯТИ", 2 },
	{ "UNIT", "ЮНИТ", 3 },
};
static const BootLine_t s_CombineLines[] =
{
	{ "OVERWATCH NETWORK: CONNECTING", "СЕТЬ НАДЗОРА: ПОДКЛЮЧЕНИЕ", 0 },
	{ "BIOSIGNAL", "БИОСИГНАЛ", 1 },
	{ "NEURAL INTERFACE", "НЕЙРОИНТЕРФЕЙС", 1 },
	{ "COMPLIANCE MONITOR", "КОНТРОЛЬ ПОДЧИНЕНИЯ", 1 },
	{ "SECTOR MAP", "КАРТА СЕКТОРА", 1 },
	{ "UNIT", "ЮНИТ", 3 },
};


// the self-test of a work suit
static const BootLine_t s_WorkerLines[] =
{
	{ "SELF-TEST", "САМОДИАГНОСТИКА", 0 },
	{ "SEALS", "ГЕРМЕТИЧНОСТЬ", 1 },
	{ "RESPIRATOR", "РЕСПИРАТОР", 1 },
	{ "LAMP", "ФОНАРЬ", 1 },
	{ "RADIO", "РАЦИЯ", 2 },
	{ "TOKEN", "ЖЕТОН", 3 },
};
static const BootLine_t s_HazmatLines[] =
{
	{ "SELF-TEST", "САМОДИАГНОСТИКА", 0 },
	{ "SEALS", "ГЕРМЕТИЧНОСТЬ", 1 },
	{ "AIR TANK", "БАЛЛОН", 1 },
	{ "FILTERS", "ФИЛЬТРЫ", 1 },
	{ "DOSIMETER", "ДОЗИМЕТР", 1 },
	{ "OPERATOR", "ОПЕРАТОР", 3 },
};

static void BootContent( int theme, const char *&title, const char *&subtitle, const BootLine_t *&lines, int &count )
{
	switch ( theme )
	{
	case THEME_PCV:
		title = "P.C.V. PROTOTYPE XI";
		subtitle = TR( "POWERED COMBAT VEST", "БОЕВОЙ ЖИЛЕТ С ПИТАНИЕМ" );
		lines = s_PCVLines; count = ARRAYSIZE( s_PCVLines );
		return;
	case THEME_COMBINE:
		title = "OVERWATCH // CIVIL PROTECTION";
		subtitle = TR( "PROTECTION TEAM UNIT", "ЮНИТ ГРАЖДАНСКОЙ ОБОРОНЫ" );
		lines = s_CombineLines; count = ARRAYSIZE( s_CombineLines );
		return;
	case THEME_WORKER:
		title = TR( "CWU COVERALL R-7", "РОБА Р-7  ГТС" );
		subtitle = TR( "CIVIL WORKERS' UNION", "ГРАЖДАНСКИЙ ТРУДОВОЙ СОЮЗ" );
		lines = s_WorkerLines; count = ARRAYSIZE( s_WorkerLines );
		return;
	case THEME_HAZMAT:
		title = TR( "HAZMAT SUIT KZ-4", "КОСТЮМ КЗ-4" );
		subtitle = TR( "CHEMICAL AND RADIATION PROTECTION", "ХИМИЧЕСКАЯ И РАДИАЦИОННАЯ ЗАЩИТА" );
		lines = s_HazmatLines; count = ARRAYSIZE( s_HazmatLines );
		return;
	case THEME_ELITE:
		title = "OVERWATCH // ELITE";
		subtitle = TR( "TRANSHUMAN ARM", "ТРАНСЧЕЛОВЕЧЕСКОЕ ПОДРАЗДЕЛЕНИЕ" );
		lines = s_EliteLines; count = ARRAYSIZE( s_EliteLines );
		return;
	}
	title = "H.E.V. MARK V";
	subtitle = TR( "HAZARDOUS ENVIRONMENT SUIT", "КОСТЮМ ДЛЯ ОПАСНЫХ СРЕД" );
	lines = s_HEVLines; count = ARRAYSIZE( s_HEVLines );
}

class CHudRPMSuitBoot : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMSuitBoot, Panel );
public:
	CHudRPMSuitBoot( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMSuitBoot" )
	{
		SetParent( g_pClientMode->GetViewport() );
		SetHiddenBits( HIDEHUD_PLAYERDEAD );
		m_iLinesSounded = 0;
		m_hMono = m_hMonoBig = INVALID_FONT;
	}

	virtual void LevelInit() { s_flBootStart = -100.0f; }

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		LoadFonts( pScheme );
		m_hMono = pScheme->GetFont( "RPMUI_Mono", false );
		m_hMonoBig = pScheme->GetFont( "RPMUI_MonoBig", false );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
		int w, h;
		surface()->GetScreenSize( w, h );
		SetBounds( 0, 0, w, h );
	}

	virtual bool ShouldDraw()
	{
		return CHudElement::ShouldDraw() && gpGlobals->realtime - s_flBootStart < BOOT_TIME;
	}

	// a faint hexagon grid, revealed from the top down to row "reveal" (0..1)
	void HexGrid( int sw, int sh, float reveal, Color c )
	{
		const int r = Sc( 22 );
		const float hw = r * 0.866f;
		surface()->DrawSetColor( c );
		int rows = sh / ( r * 3 / 2 ) + 2;
		for ( int row = 0; row < rows; row++ )
		{
			float y = row * r * 1.5f;
			if ( y > sh * reveal )
				break;
			for ( float x = ( row & 1 ) ? hw : 0.0f; x < sw + hw; x += hw * 2 )
			{
				// three edges per hexagon draw the whole grid
				int x0 = (int)x, y0 = (int)y;
				surface()->DrawLine( x0, y0 - r, x0 + (int)hw, y0 - r / 2 );
				surface()->DrawLine( x0 + (int)hw, y0 - r / 2, x0 + (int)hw, y0 + r / 2 );
				surface()->DrawLine( x0, y0 - r, x0 - (int)hw, y0 - r / 2 );
			}
		}
	}

	virtual void Paint()
	{
		const Palette_t &p = PalOf( s_iBootTheme );
		int sw, sh;
		GetSize( sw, sh );
		const float t = gpGlobals->realtime - s_flBootStart;
		const float fade = clamp( ( BOOT_TIME - t ) / 0.6f, 0.0f, 1.0f ) * clamp( t / 0.15f, 0.0f, 1.0f );
		if ( IsWorkTheme( s_iBootTheme ) )
		{
			PaintWork( sw, sh, t, fade );
			return;
		}

		// the visor powering on: a dark wash, a scan line, the grid sweeping down, frame ticks from the middle
		Rect( 0, 0, sw, sh, Color( 0, 0, 0, (int)( 110 * fade * clamp( 1.6f - t, 0.0f, 1.0f ) ) ) );
		float sweep = clamp( t / 1.2f, 0.0f, 1.0f );
		HexGrid( sw, sh, sweep, Color( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 26 * fade ) ) );
		if ( sweep < 1.0f )
			Rect( 0, (int)( sh * sweep ), sw, Max( 2, Sc( 2 ) ), Color( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 160 * fade ) ) );
		float open = Ramp( s_flBootStart, 0.5f );
		int fw = (int)( ( sw - Sc( 80 ) ) * ( 0.3f + 0.7f * open ) ), fh = (int)( ( sh - Sc( 80 ) ) * ( 0.3f + 0.7f * open ) );
		FrameBox( ( sw - fw ) / 2, ( sh - fh ) / 2, fw, fh, Sc( 30 ), (int)( 200 * fade ) );

		// the title and the systems
		const char *title, *subtitle;
		const BootLine_t *lines;
		int count;
		BootContent( s_iBootTheme, title, subtitle, lines, count );
		// below the notifications (top left), on a dark plate: the sky must not wash the text out
		int x = Sc( 90 ), y = Sc( 270 );
		float ta = fade * Ramp( s_flBootStart, 0.3f, 0.3f );
		{
			int blockH = surface()->GetFontTall( Font( F_TITLE ) ) + Sc( 2 ) + surface()->GetFontTall( Font( F_SMALL ) ) + Sc( 34 )
				+ count * ( surface()->GetFontTall( Font( F_TEXT ) ) + Sc( 8 ) );
			Plate( x - Sc( 26 ), y - Sc( 22 ), Sc( 700 ), blockH + Sc( 40 ), (int)( 175 * ta ), Sc( 8 ) );
		}
		Spaced( F_TITLE, x, y, Fade( p.accent, ta ), title, Sc( 5 ), true );
		y += surface()->GetFontTall( Font( F_TITLE ) ) + Sc( 2 );
		Spaced( F_SMALL, x, y, Fade( p.dim, ta ), subtitle, Sc( 3 ) );
		y += Sc( 34 );
		const float lineStart = 0.7f, lineStep = 0.42f;
		const int lineH = surface()->GetFontTall( Font( F_TEXT ) ) + Sc( 8 );
		const int dotsX = x + Sc( 520 );
		int shown = 0;
		for ( int i = 0; i < count; i++ )
		{
			float lt = t - ( lineStart + i * lineStep );
			if ( lt < 0.0f )
				break;
			shown++;
			const BootLine_t &l = lines[i];
			const char *text = IsRussian() ? l.ru : l.en;
			// typed on: ~70 characters a second
			int chars = (int)( lt * 70.0f );
			CUtlString full = text;
			if ( l.status == 3 )
				full.Append( CFmtStr( ": %s", UserName().Get() ) );
			CUtlString typed;
			int n = 0;
			for ( const char *c = full.Get(); *c && n <= chars; c++ )
			{
				if ( ( *c & 0xC0 ) != 0x80 )
					n++;
				if ( n > chars )
					break;
				char ch[2] = { *c, 0 };
				typed.Append( ch );
			}
			bool cursor = n <= chars ? false : ( (int)( t * 6 ) & 1 );
			Text( F_TEXT, x, y, Fade( l.status == 3 ? p.text : p.accent, fade ), typed, l.status == 3 );
			if ( cursor )
				Text( F_TEXT, x + TextWidth( F_TEXT, typed ), y, Fade( p.accent, fade ), "_" );
			// "...... OK" after the line is typed
			if ( ( l.status == 1 || l.status == 2 ) && lt > 0.3f )
			{
				int tx = x + TextWidth( F_TEXT, typed ) + Sc( 10 );
				for ( int dx = tx; dx < dotsX - Sc( 8 ); dx += Sc( 9 ) )
					Rect( dx, y + surface()->GetFontTall( Font( F_TEXT ) ) - Sc( 6 ), Max( 2, Sc( 2 ) ), Max( 2, Sc( 2 ) ), Fade( p.dim, fade ) );
				if ( l.status == 1 )
					Text( F_TEXT, dotsX, y, Fade( p.good, fade ), TR( "OK", "ОК" ), true );
				else if ( s_iBootTheme == THEME_ELITE )
					Text( F_TEXT, dotsX, y, Fade( p.bad, fade ), TR( "IN PROGRESS", "ИДЁТ" ), true );
				else
					Text( F_TEXT, dotsX, y, Fade( p.bad, fade ), TR( "NO SIGNAL", "НЕТ СИГНАЛА" ), true );
			}
			y += lineH;
		}
		if ( shown > m_iLinesSounded )
		{
			surface()->PlaySound( s_iBootTheme >= THEME_COMBINE ? "buttons/combine_button7.wav" : "buttons/blip1.wav" );
			m_iLinesSounded = shown;
		}
		if ( t < 0.1f )
			m_iLinesSounded = 0;

		// the progress of the boot at the bottom
		float prog = clamp( ( t - 0.4f ) / ( lineStart + count * lineStep ), 0.0f, 1.0f );

		// the hologram of the suit on the right (materials/vgui/hl2rpm/suit_holo_<theme>, make_suit_holograms.py),
		// built up from the feet as the systems come online
		{
			static int s_iHoloTex[THEME_COUNT];
			static bool s_bHoloInit = false;
			if ( !s_bHoloInit )
			{
				for ( int i = 0; i < THEME_COUNT; i++ )
					s_iHoloTex[i] = -1;
				s_bHoloInit = true;
			}
			const int th = clamp( s_iBootTheme, 0, THEME_COUNT - 1 );
			if ( s_iHoloTex[th] < 0 )
			{
				s_iHoloTex[th] = surface()->CreateNewTextureID();
				surface()->DrawSetTextureFile( s_iHoloTex[th], CFmtStr( "vgui/hl2rpm/suit_holo_%s", ThemeName( th ) ), true, false );
			}
			const int hh = (int)( sh * 0.62f ), hw = hh / 2;
			const int hx = sw - Sc( 120 ) - hw, hy = ( sh - hh ) / 2 - Sc( 20 );
			const float build = clamp( ( t - 0.5f ) / 2.6f, 0.0f, 1.0f );
			const int cut = hy + hh - (int)( hh * build );
			float flick = 0.82f + 0.18f * sinf( t * 37.0f ) * sinf( t * 11.0f );
			if ( ( (int)( t * 13.0f ) % 17 ) == 0 )
				flick *= 0.55f;		// a glitch now and then
			surface()->DrawSetTexture( s_iHoloTex[th] );
			surface()->DrawSetColor( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 235 * fade * flick ) );
			if ( build > 0.0f )
				surface()->DrawTexturedSubRect( hx, cut, hx + hw, hy + hh, 0.0f, (float)( cut - hy ) / hh, 1.0f, 1.0f );
			if ( build > 0.0f && build < 1.0f )
				Rect( hx - Sc( 20 ), cut, hw + Sc( 40 ), Max( 2, Sc( 2 ) ), Color( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 200 * fade ) ) );
			// a base ring under the feet
			Rect( hx - Sc( 10 ), hy + hh + Sc( 6 ), hw + Sc( 20 ), Max( 1, Sc( 1 ) ), Color( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 120 * fade ) ) );
		}
		int bw = Sc( 420 ), by = sh - Sc( 190 );	// above the HUD numbers
		Spaced( F_SMALL, x, by - Sc( 26 ), Fade( p.dim, fade ), prog < 1.0f ? TR( "INITIALIZING", "ИНИЦИАЛИЗАЦИЯ" ) : TR( "ONLINE", "В СЕТИ" ), Sc( 3 ) );
		Bar( x, by, bw, Sc( 8 ), prog, Fade( p.accent, fade ) );
		Text( F_SMALL, x + bw + Sc( 14 ), by - Sc( 6 ), Fade( p.accent, fade ), CFmtStr( "%d%%", (int)( prog * 100 ) ) );
		// a warning stripe of the prototype
		if ( s_iBootTheme == THEME_PCV && t > 2.5f )
		{
			int wy = by + Sc( 26 );
			float blink = ( (int)( t * 3 ) & 1 ) ? 1.0f : 0.55f;
			Rect( x, wy, bw, Sc( 26 ), Color( 200, 160, 20, (int)( 170 * fade * blink ) ) );
			Text( F_SMALL, x + bw / 2, wy + Sc( 4 ), Color( 0, 0, 0, (int)( 255 * fade ) ), TR( "PROTOTYPE - FAULTS POSSIBLE", "ПРОТОТИП — ВОЗМОЖНЫ СБОИ" ), false, 1 );
		}
	}


	// the work suits: a small LCD box where their HUD will be, a self-test, a striped progress bar
	void PaintWork( int sw, int sh, float t, float fade )
	{
		const Palette_t &p = PalOf( s_iBootTheme );
		HFont mono = m_hMono != INVALID_FONT ? m_hMono : Font( F_TEXT );
		const char *title, *subtitle;
		const BootLine_t *lines;
		int count;
		BootContent( s_iBootTheme, title, subtitle, lines, count );
		const int lineH = surface()->GetFontTall( mono ) + Sc( 6 );
		const int w = Sc( 360 ), h = Sc( 70 ) + count * lineH + Sc( 34 );
		const int x = Sc( 24 ), y = sh - Sc( 24 ) - Sc( 134 ) - Sc( 14 ) - h;
		const float open = clamp( t / 0.25f, 0.0f, 1.0f );
		const int hh = (int)( h * open );
		// the case, opening downwards
		Rect( x, y, w, hh, Color( p.plate.r() + 14, p.plate.g() + 14, p.plate.b() + 10, (int)( 222 * fade ) ) );
		Color rim( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 70 * fade ) );
		Rect( x, y, w, Max( 1, Sc( 1 ) ), rim );
		Rect( x, y + hh - Max( 1, Sc( 1 ) ), w, Max( 1, Sc( 1 ) ), rim );
		if ( open < 1.0f )
			return;
		const int ix = x + Sc( 14 ), iw = w - Sc( 28 );
		int cy = y + Sc( 12 );
		WText( m_hMonoBig != INVALID_FONT ? m_hMonoBig : mono, ix, cy, Fade( p.accent, fade ), title );
		cy += Sc( 34 );
		WText( mono, ix, cy, Fade( p.dim, fade ), subtitle );
		cy += lineH + Sc( 6 );
		const float lineStart = 0.5f, lineStep = 0.45f;
		int shown = 0;
		for ( int i = 0; i < count; i++ )
		{
			const float lt = t - ( lineStart + i * lineStep );
			if ( lt < 0.0f )
				break;
			shown++;
			const BootLine_t &l = lines[i];
			CUtlString text = IsRussian() ? l.ru : l.en;
			if ( l.status == 3 )
				text.Append( CFmtStr( ": %s", UserName().Get() ) );
			WText( mono, ix, cy, Fade( l.status == 3 ? p.text : p.accent, fade ), text );
			if ( l.status == 1 || l.status == 2 )
			{
				// the check runs as a row of dots, then the verdict
				const float k = clamp( lt / 0.3f, 0.0f, 1.0f );
				const int tw = WTextW( mono, text );
				const int dx0 = ix + tw + Sc( 8 ), dx1 = ix + iw - Sc( 78 );
				for ( int dx = dx0; dx < dx0 + ( dx1 - dx0 ) * k; dx += Sc( 8 ) )
					Rect( dx, cy + surface()->GetFontTall( mono ) - Sc( 5 ), Max( 2, Sc( 2 ) ), Max( 2, Sc( 2 ) ), Fade( p.dim, fade ) );
				if ( k >= 1.0f )
				{
					if ( l.status == 1 )
						WText( mono, ix + iw, cy, Fade( p.good, fade ), TR( "OK", "НОРМА" ), 2 );
					else
						WText( mono, ix + iw, cy, Fade( p.bad, fade ), TR( "NO SIGNAL", "НЕТ СВЯЗИ" ), 2 );
				}
			}
			cy += lineH;
		}
		if ( shown > m_iLinesSounded )
		{
			surface()->PlaySound( "buttons/blip1.wav" );
			m_iLinesSounded = shown;
		}
		if ( t < 0.1f )
			m_iLinesSounded = 0;
		// the progress: a hazard-striped bar filling up
		const float prog = clamp( ( t - 0.3f ) / ( lineStart + count * lineStep ), 0.0f, 1.0f );
		const int by = y + h - Sc( 22 ), bh = Sc( 10 );
		Rect( ix, by, iw, bh, Color( 10, 9, 6, (int)( 230 * fade ) ) );
		const int fillW = (int)( iw * prog );
		Color sc( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 220 * fade ) );
		const float step = bh * 2.0f;
		for ( float sx = (float)ix - bh; sx < ix + fillW; sx += step )
		{
			const float L = (float)ix, R = (float)( ix + fillW );
			const float t0 = clamp( sx + bh, L, R ), t1 = clamp( sx + bh + step * 0.5f, L, R );
			const float b0 = clamp( sx, L, R ), b1 = clamp( sx + step * 0.5f, L, R );
			if ( t1 - t0 < 0.5f && b1 - b0 < 0.5f )
				continue;
			Quad( t0, (float)by, t1, (float)by, b1, (float)( by + bh ), b0, (float)( by + bh ), sc );
		}
	}

	int WText( HFont f, int x, int y, Color c, const char *utf8, int align = 0 )
	{
		wchar_t w[256];
		V_UTF8ToUnicode( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( f, w, tw, th );
		if ( align == 2 )
			x -= tw;
		surface()->DrawSetTextFont( f );
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( x, y );
		surface()->DrawPrintText( w, wcslen( w ) );
		return tw;
	}

	int WTextW( HFont f, const char *utf8 )
	{
		wchar_t w[256];
		V_UTF8ToUnicode( utf8, w, sizeof( w ) );
		int tw, th;
		surface()->GetTextSize( f, w, tw, th );
		return tw;
	}

	HFont m_hMono, m_hMonoBig;

	int m_iLinesSounded;
};

DECLARE_HUDELEMENT( CHudRPMSuitBoot );

CON_COMMAND( rpm_suitboot, "HL2RPM: show the boot screen of a suit: rpm_suitboot [hev|pcv|combine|elite|worker|hazmat]" )
{
	int theme = THEME_HEV;
	if ( args.ArgC() > 1 )
	{
		for ( int i = 1; i < THEME_COUNT; i++ )
			if ( !Q_stricmp( args[1], ThemeName( i ) ) )
				theme = i;
	}
	StartBoot( theme, true );
}

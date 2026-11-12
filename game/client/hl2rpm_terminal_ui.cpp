//========= HL2RPM ============================================================//
//
// Terminals on the client (M6.5; server: hl2rpm_terminal.cpp).
//
// C_RPMTerminal - the networked rpm_terminal: knows where its screen is (model space,
// sent by the server). C_RPMTerminalScreen - a client-only translucent renderable at
// the screen that draws the interface into the world with DrawPanelIn3DSpace: an idle
// screen (title, cursor, "locked") for every terminal near the camera, the full
// interface for the one in use. Styles: "pc" (green CRT of the city network) and
// "combine" (teal Overwatch console).
//
// Using one (Fallout 4): the camera glides to the front of the screen, an invisible
// popup takes the keyboard and the mouse (the cursor is traced onto the screen), Tab /
// Esc leave and the camera glides back. Pages: boot, menu (submenus), a record (scroll),
// the word game of Fallout (hacking), "locked out", "skill too low".
//
//=============================================================================//
#include "cbase.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include "iclientmode.h"
#include "view_shared.h"
#include "view.h"
#include "cdll_client_int.h"
#include "engine/IEngineSound.h"
#include "engine/ivdebugoverlay.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "mathlib/vmatrix.h"
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Panel.h>
#include <vgui_controls/Controls.h>
#include "usermessages.h"
#include "KeyValues.h"
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static ConVar cl_rpm_terminal_dist( "cl_rpm_terminal_dist", "700", FCVAR_ARCHIVE, "HL2RPM: terminal screens farther than this show nothing" );
static ConVar cl_rpm_terminal_debug( "cl_rpm_terminal_debug", "0", FCVAR_NONE, "HL2RPM: 1 - outline the screens of terminals" );
static ConVar cl_rpm_terminal_fill( "cl_rpm_terminal_fill", "0.82", FCVAR_ARCHIVE, "HL2RPM: how much of the view the screen of a terminal in use fills" );

enum { TSTYLE_PC = 0, TSTYLE_COMBINE = 1 };
enum { TLOCK_OPEN = 0, TLOCK_HACK, TLOCK_LOCKOUT, TLOCK_SKILL };

class C_RPMTerminal;
class C_RPMTerminalScreen;

static bool TermRussian( const char *lang = NULL )
{
	return !Q_stricmp( lang && lang[0] ? lang : RPMItems::Language(), "russian" );
}
#define TTR( en, ru ) ( TermRussian() ? ( ru ) : ( en ) )

static void TermSound( const char *s, float vol = 0.5f, int pitch = 100 )
{
	enginesound->EmitAmbientSound( s, vol, pitch );
}

//=============================================================================
// the entity
//=============================================================================
class C_RPMTerminal : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_RPMTerminal, C_BaseAnimating );
	DECLARE_CLIENTCLASS();

	C_RPMTerminal() : m_pScreen( NULL ) { m_szTitle[0] = 0; }

	virtual void OnDataChanged( DataUpdateType_t type );
	virtual void UpdateOnRemove();

	// the screen in the world: center, normal (towards the viewer), right and up as the viewer sees them
	void ScreenFrame( Vector &center, Vector &normal, Vector &right, Vector &up )
	{
		matrix3x4_t local, world;
		AngleMatrix( m_angScreen, m_vecScreenPos, local );
		ConcatTransforms( EntityToWorldTransform(), local, world );
		Vector left;
		MatrixGetColumn( world, 0, normal );
		MatrixGetColumn( world, 1, left );
		MatrixGetColumn( world, 2, up );
		MatrixGetColumn( world, 3, center );
		// the viewer faces -normal: their right is the screen's +y (left of the screen's own frame)
		right = left;
	}

	int m_iStyle, m_iLockState;
	Vector m_vecScreenPos;
	QAngle m_angScreen;
	float m_flScreenW, m_flScreenH;
	char m_szTitle[64];
	C_RPMTerminalScreen *m_pScreen;
};

IMPLEMENT_CLIENTCLASS_DT( C_RPMTerminal, DT_RPMTerminal, CRPMTerminal )
	RecvPropInt( RECVINFO( m_iStyle ) ),
	RecvPropInt( RECVINFO( m_iLockState ) ),
	RecvPropVector( RECVINFO( m_vecScreenPos ) ),
	RecvPropQAngles( RECVINFO( m_angScreen ) ),
	RecvPropFloat( RECVINFO( m_flScreenW ) ),
	RecvPropFloat( RECVINFO( m_flScreenH ) ),
	RecvPropString( RECVINFO( m_szTitle ) ),
END_RECV_TABLE()

//=============================================================================
// the session (one terminal at a time)
//=============================================================================
enum Page_t { PG_NONE, PG_BOOT, PG_MENU, PG_TEXT, PG_HACK, PG_LOCKOUT, PG_SKILL };

struct TermHot_t { int x, y, w, h, action, arg; };
enum { TH_ENTRY, TH_BACK, TH_CHAR, TH_EXIT };

#define HACK_COLS 12
#define HACK_ROWS 16
#define HACK_N ( HACK_COLS * HACK_ROWS * 2 )
#define HACK_MAXWORDS 20
#define HACK_LOG 16

struct HackGame_t
{
	bool on;
	int len, nWords, pass;
	wchar_t words[HACK_MAXWORDS][16];
	bool removed[HACK_MAXWORDS];
	wchar_t s[HACK_N];
	signed char wordAt[HACK_N];
	bool used[HACK_N];
	int tries, maxTries;
	int cursor;
	CUtlString log[HACK_LOG];
	int nLog;
	int addr0;
	float endTime;		// success / lockout: when to tell the server
	bool won;
};

struct TermSession_t
{
	CHandle<C_RPMTerminal> hTerm;
	KeyValues *pContent;
	int token;
	bool active;			// using it (else the camera is gliding back or nothing)
	float camT;				// 0..1 camera progress
	float lastTime;
	int page;
	float pageTime;
	CUtlVector<CUtlString> path;	// submenus by entry id
	CUtlString textId;
	int sel, scroll, maxScroll;
	CUtlString message;
	float messageTime;
	// the camera as last set (for the mouse)
	Vector viewOrigin;
	QAngle viewAngles;
	float viewFov;
	bool viewValid;
	// hovered region (screen pixels of the panel)
	CUtlVector<TermHot_t> hot;
	int hoverHot;
	int panelW, panelH;
	HackGame_t hack;
};

static TermSession_t s_T;

static KeyValues *Content() { return s_T.pContent; }
static C_RPMTerminal *ActiveTerm() { return s_T.hTerm.Get(); }

bool RPMTerminal_IsOpen() { return s_T.hTerm.Get() && ( s_T.active || s_T.camT > 0.0f ); }
bool RPMTerminal_HidesHud() { return RPMTerminal_IsOpen() && s_T.camT > 0.05f; }

static void SetPage( int page )
{
	s_T.page = page;
	s_T.pageTime = gpGlobals->realtime;
	s_T.sel = 0;
	s_T.scroll = 0;
	s_T.hoverHot = -1;
}

// the entry list currently shown (root or a submenu)
static KeyValues *CurrentList()
{
	KeyValues *c = Content();
	if ( !c )
		return NULL;
	KeyValues *list = c->FindKey( "entries" );
	for ( int i = 0; i < s_T.path.Count(); i++ )
	{
		KeyValues *found = NULL;
		for ( KeyValues *e = list ? list->GetFirstTrueSubKey() : NULL; e && !found; e = e->GetNextTrueSubKey() )
			if ( !Q_strcmp( e->GetString( "id" ), s_T.path[i] ) )
				found = e;
		KeyValues *sub = found ? found->FindKey( "entries" ) : NULL;
		if ( !sub )
		{
			// the submenu is gone (its conditions changed): stay where it was
			s_T.path.RemoveMultipleFromTail( s_T.path.Count() - i );
			break;
		}
		list = sub;
	}
	return list;
}

static KeyValues *FindEntryById( KeyValues *list, const char *id )
{
	if ( !list )
		return NULL;
	for ( KeyValues *e = list->GetFirstTrueSubKey(); e; e = e->GetNextTrueSubKey() )
	{
		if ( !Q_strcmp( e->GetString( "id" ), id ) )
			return e;
		KeyValues *r = FindEntryById( e->FindKey( "entries" ), id );
		if ( r )
			return r;
	}
	return NULL;
}

static int ListCount( KeyValues *list )
{
	int n = 0;
	for ( KeyValues *e = list ? list->GetFirstTrueSubKey() : NULL; e; e = e->GetNextTrueSubKey() )
		n++;
	return n;
}

static KeyValues *ListItem( KeyValues *list, int idx )
{
	int n = 0;
	for ( KeyValues *e = list ? list->GetFirstTrueSubKey() : NULL; e; e = e->GetNextTrueSubKey(), n++ )
		if ( n == idx )
			return e;
	return NULL;
}

static void SendCmd( const char *cmd, const char *arg = NULL )
{
	if ( !s_T.token )
		return;
	if ( arg )
		engine->ClientCmd_Unrestricted( CFmtStr( "rpm_terminal_cmd %d %s \"%s\"\n", s_T.token, cmd, arg ) );
	else
		engine->ClientCmd_Unrestricted( CFmtStr( "rpm_terminal_cmd %d %s\n", s_T.token, cmd ) );
}

static int Style()
{
	C_RPMTerminal *t = ActiveTerm();
	if ( Content() )
		return !Q_stricmp( Content()->GetString( "style" ), "combine" ) ? TSTYLE_COMBINE : TSTYLE_PC;
	return t ? t->m_iStyle : TSTYLE_PC;
}

static void SndMove()
{
	if ( Style() == TSTYLE_COMBINE )
		TermSound( "npc/scanner/scanner_blip1.wav", 0.25f, 150 );
	else
		TermSound( CFmtStr( "ambient/machines/keyboard%d_clicks.wav", RandomInt( 1, 6 ) ), 0.3f );
}

static void SndSelect()
{
	if ( Style() == TSTYLE_COMBINE )
		TermSound( "buttons/combine_button1.wav", 0.4f, 120 );
	else
		TermSound( "ambient/machines/keyboard7_clicks_enter.wav", 0.5f );
}

static void SndDeny()
{
	TermSound( Style() == TSTYLE_COMBINE ? "buttons/combine_button_locked.wav" : "buttons/button10.wav", 0.5f );
}

//=============================================================================
// the word game
//=============================================================================
static const char *s_pszWordsRU =
	"ДВОР ЗОНА ЛИФТ ПОСТ БЛОК ВОДА ХЛЕБ КЛЮЧ СЕТЬ СВЕТ ТЕНЬ ЛУЧИ БАЗА КОДЫ ЯЩИК КРАН ПУЛЯ ЩИТЫ ЦЕХА ТОКИ ЗНАК ДОЗА ПЛАН РОТА ЗАЛП "
	"ГРАД ПОЛЕ ГОРА ЛЕСА ЯДРО ГРУЗ ПАЁК ПОЕЗД ЛАМПА ЛЕНТА ШАХТА ОТЧЁТ ОТРЯД ДОЖДЬ ГОРОД СКЛАД ЖЕТОН КАНАЛ МАСКА БАШНЯ ТРУБА РАДИО "
	"ЭКРАН ЛОГИН ПУЛЬТ ЗАМОК ДВЕРЬ СТЕНА КРЫША АРХИВ ГАРАЖ ВОКЗАЛ ПРИКАЗ СИГНАЛ ВОРОТА ДАТЧИК ФОНАРЬ КАМЕРА ПОРТАЛ ПАТРОН ДОПРОС "
	"ЗАСЛОН КОНВОЙ ЖИТЕЛЬ РЕГИОН ОТСЕКИ БАРЬЕР ЗАЩИТА ДОСТУП ЗАПРОС ЖУРНАЛ СЕКТОР ПАРОЛЬ ОБЪЕКТ ТУННЕЛЬ ПАТРУЛЬ РЕАКТОР ТРЕВОГА "
	"КВАРТАЛ МОНИТОР СТАНЦИЯ ОБЛАСТЬ ПРОПУСК ИНЖЕНЕР СИСТЕМА ВЕНТИЛЬ БЕЖЕНЕЦ ПОДВАЛЫ ЧЕРДАКИ АППАРАТ ТРАНЗИТ КОНСОЛЬ РЕСУРСЫ "
	"ЛОКАТОР РАЦИОНЫ ЦИТАДЕЛЬ ОПЕРАТОР КОНТРОЛЬ ЛЕСТНИЦА ЗАГРУЗКА ТЕРМИНАЛ ПРОТОКОЛ КОМПЛЕКС КАРАНТИН ЗАЧИСТКА ГАРНИЗОН ДИРЕКТОР "
	"ТЕЛЕПОРТ ЛАБОРАНТ ГРАЖДАНЕ ПРОВЕРКА РЕГИСТРЫ КОНСЕРВЫ ИЗОЛЯЦИЯ ПЕРЕПИСЬ ГЕНЕРАТОР ИНСПЕКТОР НАРУШЕНИЕ АВАРИЙНЫЙ ОТКЛЮЧИТЬ "
	"ДИСПЕТЧЕР КОМЕНДАНТ КОНТЕЙНЕР ПРОЦЕССОР ОХРАННИКИ ПОСЕЛЕНИЕ ВЕРТОЛЁТЫ ДЕЖУРСТВО ЭВАКУАЦИЯ ДИРЕКТИВА СВЕТОФОРЫ ОТОПЛЕНИЕ "
	"ИНЦИДЕНТЫ ПОДСТАНЦИЯ ТЕХНОЛОГИЯ ИНСТРУКЦИЯ МАГИСТРАЛЬ РАСПИСАНИЕ ДЕПОРТАЦИЯ ПЕРЕДАТЧИК ВЫЧИСЛЕНИЕ КООРДИНАТЫ ПРОПУСКНОЙ "
	"ЛИКВИДАЦИЯ ИНФОРМАЦИЯ МЕДИКАМЕНТ ПОДЧИНЕНИЕ НАЗНАЧЕНИЕ РАЗРЕШЕНИЕ ВЕНТИЛЯЦИЯ ВЗРЫВЧАТКА ЗАДЕРЖАНИЕ ПРЕСТУПНИК НАРУШИТЕЛЬ "
	"ЛАБОРАТОРИЯ КАНАЛИЗАЦИЯ РЕГИСТРАЦИЯ НАДЗИРАТЕЛЬ ЭКСПЕРИМЕНТ ДЕЗИНФЕКЦИЯ ТРАНСПОРТЁР ИНФОРМАТОРЫ НАБЛЮДАТЕЛИ ПРЕДПИСАНИЕ "
	"КОНФИСКАЦИЯ ДЕАКТИВАЦИЯ ПЕРЕСЕЛЕНЕЦ ЭЛЕКТРОСЕТЬ СЛЕДОВАТЕЛЬ ПРОИЗВОДСТВО ОБСЕРВАТОРИЯ КОММУНИКАЦИЯ ОБОРУДОВАНИЕ "
	"КВАЛИФИКАЦИЯ БЕЗОПАСНОСТЬ ОСВЕДОМИТЕЛЬ ДЕЗЕРТИРСТВО ДОКУМЕНТАЦИЯ СТЕРИЛИЗАЦИЯ СУБОРДИНАЦИЯ";

static const char *s_pszWordsEN =
	"GATE LOCK CODE ZONE UNIT CELL DOOR GRID HALL PIPE RAIL WARD BOLT DATA LINK FILE SCAN WIRE TANK DOCK "
	"TOWER POWER GUARD ALARM VAULT RADIO SQUAD CRATE TRAIN FENCE CABLE PANEL RELAY ORDER CLERK FLOOD LEVER "
	"PATROL SECTOR SIGNAL ACCESS WARDEN CANALS RATION TUNNEL PORTAL CAMERA SENSOR BUNKER CONVOY BORDER "
	"CITIZEN BARRIER CONSOLE REACTOR OFFICER CONTROL STATION LOCKOUT PROCESS DISPATCH MONITOR CURFEW "
	"OVERSEER CITADEL TERMINAL PROTOCOL TRANSFER SECURITY DETAINEE CHECKLOG HARDWARE GARRISON PASSWORD "
	"OVERWATCH DETENTION GENERATOR INSPECTOR TRANSPORT CHECKPOINT AUTHORITY DIRECTIVE COMPLIANCE "
	"QUARANTINE EVACUATION SUBSTATION DISPATCHER PROCESSOR RELOCATION EXPERIMENT INCIDENTS SUPPRESSOR "
	"DEPORTATION CONTAINMENT LABORATORY COORDINATES RESISTANCE INFORMATION TRANSMITTER MAINTENANCE "
	"REGISTRATION CONFISCATION SURVEILLANCE DISTRIBUTION INSTRUCTIONS VENTILATIONS PRESCRIPTION";

static int WLen( const wchar_t *w ) { return (int)wcslen( w ); }

static int Likeness( const wchar_t *a, const wchar_t *b )
{
	int n = 0;
	for ( int i = 0; a[i] && b[i]; i++ )
		if ( a[i] == b[i] )
			n++;
	return n;
}

static void HackLog( const char *line )
{
	HackGame_t &h = s_T.hack;
	if ( h.nLog >= HACK_LOG )
	{
		for ( int i = 1; i < HACK_LOG; i++ )
			h.log[i - 1] = h.log[i];
		h.nLog = HACK_LOG - 1;
	}
	h.log[h.nLog++] = line;
}

static void HackStart( int level, int skill )
{
	HackGame_t &h = s_T.hack;
	V_memset( h.removed, 0, sizeof( h.removed ) );
	V_memset( h.used, 0, sizeof( h.used ) );
	h.nLog = 0;
	h.endTime = 0;
	h.won = false;
	// words by length from the pool of the language
	const char *pool = TermRussian( Content() ? Content()->GetString( "language" ) : NULL ) ? s_pszWordsRU : s_pszWordsEN;
	static wchar_t s_Pool[4096];
	V_UTF8ToUnicode( pool, s_Pool, sizeof( s_Pool ) );
	CUtlVector<const wchar_t *> byLen[16];
	CUtlVector<wchar_t *> storage;
	wchar_t *p = s_Pool;
	while ( *p )
	{
		while ( *p == L' ' )
			*p++ = 0;
		if ( !*p )
			break;
		wchar_t *start = p;
		while ( *p && *p != L' ' )
			p++;
		bool bEnd = !*p;
		*p = 0;
		int len = WLen( start );
		if ( len >= 4 && len <= 12 )
			byLen[len].AddToTail( start );
		if ( bEnd )
			break;
		p++;
	}
	int want = clamp( 4 + level * 8 / 100, 4, 12 );
	int count = clamp( 8 + level / 20, 8, 13 );
	int len = want;
	for ( int d = 0; d < 9; d++ )
	{
		int a = want - d, b = want + d;
		if ( a >= 4 && byLen[a].Count() >= count ) { len = a; break; }
		if ( b <= 12 && byLen[b].Count() >= count ) { len = b; break; }
	}
	CUtlVector<const wchar_t *> &bucket = byLen[len];
	count = Min( count, bucket.Count() );
	// shuffle
	for ( int i = bucket.Count() - 1; i > 0; i-- )
	{
		int j = RandomInt( 0, i );
		const wchar_t *t = bucket[i];
		bucket[i] = bucket[j];
		bucket[j] = t;
	}
	const wchar_t *pass = bucket[0];
	// a third of the rest close to the password, the others random
	CUtlVector<const wchar_t *> rest;
	for ( int i = 1; i < bucket.Count(); i++ )
		rest.AddToTail( bucket[i] );
	CUtlVector<const wchar_t *> chosen;
	chosen.AddToTail( pass );
	int nClose = ( count - 1 ) / 3;
	for ( int k = 0; k < nClose && rest.Count(); k++ )
	{
		int best = 0;
		for ( int i = 1; i < rest.Count(); i++ )
			if ( Likeness( rest[i], pass ) > Likeness( rest[best], pass ) )
				best = i;
		chosen.AddToTail( rest[best] );
		rest.Remove( best );
	}
	while ( chosen.Count() < count && rest.Count() )
	{
		int i = RandomInt( 0, rest.Count() - 1 );
		chosen.AddToTail( rest[i] );
		rest.Remove( i );
	}
	h.len = len;
	h.nWords = Min( chosen.Count(), HACK_MAXWORDS );
	// the password at a random place in the list
	h.pass = RandomInt( 0, h.nWords - 1 );
	for ( int i = 0; i < h.nWords; i++ )
	{
		const wchar_t *w = i == h.pass ? chosen[0] : chosen[i == 0 ? h.pass : i];
		V_wcsncpy( h.words[i], w, sizeof( h.words[i] ) );
	}
	// place them in the stream with a gap of at least one character
	for ( int i = 0; i < HACK_N; i++ )
		h.wordAt[i] = -1;
	for ( int w = 0; w < h.nWords; w++ )
	{
		for ( int tries = 0; tries < 400; tries++ )
		{
			int at = RandomInt( 0, HACK_N - len );
			bool ok = true;
			for ( int c = at - 1; c <= at + len && ok; c++ )
				if ( c >= 0 && c < HACK_N && h.wordAt[c] >= 0 )
					ok = false;
			if ( !ok )
				continue;
			for ( int c = 0; c < len; c++ )
			{
				h.wordAt[at + c] = (signed char)w;
				h.s[at + c] = h.words[w][c];
			}
			break;
		}
	}
	static const wchar_t s_Junk[] = L"!@#$%^&*()[]{}<>/\\|;:'\",.?-_+=~";
	int nJunk = WLen( s_Junk );
	for ( int i = 0; i < HACK_N; i++ )
		if ( h.wordAt[i] < 0 )
			h.s[i] = s_Junk[RandomInt( 0, nJunk - 1 )];
	KeyValues *c = Content();
	int skillV = c ? c->GetInt( "skill" ) : skill;
	int lvl = c ? c->GetInt( "level" ) : level;
	h.maxTries = clamp( 4 + ( skillV >= lvl + 25 ? 1 : 0 ) - ( skillV < lvl ? 1 : 0 ), 3, 5 );
	h.tries = h.maxTries;
	h.cursor = 0;
	h.addr0 = 0xF000 + RandomInt( 0, 0x7F ) * 0x10;
	h.on = true;
}

static bool IsLetter( int i ) { return s_T.hack.wordAt[i] >= 0; }

// a pair of brackets on one row without letters between: the end index, else -1
static int BracketEnd( int i )
{
	HackGame_t &h = s_T.hack;
	if ( i < 0 || i >= HACK_N || h.used[i] )
		return -1;
	wchar_t open = h.s[i], close = 0;
	switch ( open )
	{
	case L'(': close = L')'; break;
	case L'[': close = L']'; break;
	case L'{': close = L'}'; break;
	case L'<': close = L'>'; break;
	default: return -1;
	}
	int rowEnd = ( i / HACK_COLS ) * HACK_COLS + HACK_COLS - 1;
	for ( int j = i + 1; j <= rowEnd; j++ )
	{
		if ( IsLetter( j ) )
			return -1;
		if ( h.s[j] == close )
			return j;
	}
	return -1;
}

// what selecting at i covers: a word, a bracket pair or one character
static void HackSpan( int i, int &a, int &b )
{
	HackGame_t &h = s_T.hack;
	a = b = i;
	if ( i < 0 || i >= HACK_N )
		return;
	int w = h.wordAt[i];
	if ( w >= 0 && !h.removed[w] )
	{
		while ( a > 0 && h.wordAt[a - 1] == w )
			a--;
		while ( b < HACK_N - 1 && h.wordAt[b + 1] == w )
			b++;
		return;
	}
	int e = BracketEnd( i );
	if ( e >= 0 )
		b = e;
}

static void HackSelect( int i )
{
	HackGame_t &h = s_T.hack;
	if ( !h.on || h.endTime > 0 || i < 0 || i >= HACK_N )
		return;
	bool ru = TermRussian( Content() ? Content()->GetString( "language" ) : NULL );
	int a, b;
	HackSpan( i, a, b );
	char text[64];
	wchar_t wt[32];
	int n = Min( b - a + 1, 31 );
	V_wcsncpy( wt, &h.s[a], ( n + 1 ) * sizeof( wchar_t ) );
	wt[n] = 0;
	V_UnicodeToUTF8( wt, text, sizeof( text ) );
	HackLog( CFmtStr( ">%s", text ) );
	int w = h.wordAt[i];
	if ( w >= 0 && !h.removed[w] )
	{
		if ( w == h.pass )
		{
			HackLog( ru ? ">Точное совпадение!" : ">Exact match!" );
			HackLog( ru ? ">Вход в систему..." : ">Logging in..." );
			h.won = true;
			h.endTime = gpGlobals->realtime + 1.3f;
			TermSound( Style() == TSTYLE_COMBINE ? "buttons/combine_button5.wav" : "buttons/button14.wav", 0.5f );
			return;
		}
		h.tries--;
		HackLog( ru ? ">Вход запрещён." : ">Entry denied." );
		HackLog( CFmtStr( ru ? ">Совпадение=%d" : ">Likeness=%d", Likeness( h.words[w], h.words[h.pass] ) ) );
		SndDeny();
		if ( h.tries <= 0 )
		{
			HackLog( ru ? ">Блокировка." : ">Lockout initiated." );
			h.won = false;
			h.endTime = gpGlobals->realtime + 1.3f;
		}
		return;
	}
	if ( b > a )
	{
		// a bracket pair: once
		h.used[a] = true;
		bool bDud = true;
		if ( h.tries < h.maxTries && RandomFloat( 0, 1 ) < 0.3f )
			bDud = false;
		CUtlVector<int> duds;
		for ( int k = 0; k < h.nWords; k++ )
			if ( k != h.pass && !h.removed[k] )
				duds.AddToTail( k );
		if ( bDud && duds.Count() )
		{
			int k = duds[RandomInt( 0, duds.Count() - 1 )];
			h.removed[k] = true;
			for ( int c = 0; c < HACK_N; c++ )
				if ( h.wordAt[c] == k )
					h.s[c] = L'.';
			HackLog( ru ? ">Слово удалено." : ">Dud removed." );
		}
		else
		{
			h.tries = h.maxTries;
			HackLog( ru ? ">Попытки сброшены." : ">Tries reset." );
		}
		SndSelect();
		return;
	}
	HackLog( ru ? ">Ошибка" : ">Error" );
	SndDeny();
}

//=============================================================================
// painting (panel pixels; a character grid of a monospace font)
//=============================================================================
static HFont s_hMono = 0, s_hMonoGlow = 0, s_hMonoBig = 0, s_hMonoBigGlow = 0;
static int s_iCW = 12, s_iCH = 22;

static void LoadTermFonts()
{
	if ( s_hMono )
		return;
	IScheme *pScheme = scheme()->GetIScheme( scheme()->GetScheme( "ClientScheme" ) );
	if ( !pScheme )
		return;
	s_hMono = pScheme->GetFont( "RPMUI_Mono", false );
	s_hMonoGlow = pScheme->GetFont( "RPMUI_MonoGlow", false );
	s_hMonoBig = pScheme->GetFont( "RPMUI_MonoBig", false );
	s_hMonoBigGlow = pScheme->GetFont( "RPMUI_MonoBigGlow", false );
	if ( !s_hMono )
		s_hMono = pScheme->GetFont( "DefaultFixed", false );
	if ( !s_hMonoBig )
		s_hMonoBig = s_hMono;
	s_iCW = Max( 6, surface()->GetCharacterWidth( s_hMono, 'M' ) );
	s_iCH = surface()->GetFontTall( s_hMono ) + 2;
}

struct TermColors_t { Color bg, bg2, text, dim, faint, hi, hiText, bad; };

static TermColors_t ColorsOf( int style, float flicker )
{
	TermColors_t c;
	if ( style == TSTYLE_COMBINE )
	{
		c.bg = Color( 2, 10, 16, 255 );
		c.bg2 = Color( 8, 30, 44, 255 );
		c.text = Color( (int)( 120 * flicker ), (int)( 225 * flicker ), (int)( 240 * flicker ), 255 );
		c.dim = Color( 60, 140, 160, 255 );
		c.faint = Color( 40, 110, 130, 90 );
		c.hi = Color( 110, 220, 235, 255 );
		c.hiText = Color( 2, 14, 20, 255 );
		c.bad = Color( 240, 110, 80, 255 );
	}
	else
	{
		c.bg = Color( 4, 14, 6, 255 );
		c.bg2 = Color( 10, 30, 14, 255 );
		c.text = Color( (int)( 110 * flicker ), (int)( 250 * flicker ), (int)( 130 * flicker ), 255 );
		c.dim = Color( 60, 160, 80, 255 );
		c.faint = Color( 60, 200, 90, 70 );
		c.hi = Color( 110, 250, 130, 255 );
		c.hiText = Color( 4, 18, 8, 255 );
		c.bad = Color( 250, 120, 80, 255 );
	}
	return c;
}

// the typing effect: characters left to show this paint
static int s_iReveal = 1 << 30;

static int DrawWide( int x, int y, const wchar_t *s, int n, Color c, HFont font = 0, HFont glow = 0 )
{
	if ( !font )
		font = s_hMono, glow = s_hMonoGlow;
	n = Min( n, s_iReveal );
	if ( n <= 0 )
		return 0;
	s_iReveal -= n;
	int cw = font == s_hMono ? s_iCW : surface()->GetCharacterWidth( font, 'M' );
	surface()->DrawSetTextFont( font );
	surface()->DrawSetTextColor( c );
	for ( int i = 0; i < n; i++ )
	{
		surface()->DrawSetTextPos( x + i * cw, y );
		surface()->DrawUnicodeChar( s[i] );
	}
	if ( glow )
	{
		surface()->DrawSetTextFont( glow );
		surface()->DrawSetTextColor( Color( c.r(), c.g(), c.b(), c.a() / 2 ) );
		for ( int i = 0; i < n; i++ )
		{
			surface()->DrawSetTextPos( x + i * cw, y );
			surface()->DrawUnicodeChar( s[i] );
		}
	}
	return n * cw;
}

static int DrawU( int x, int y, const char *utf8, Color c, int maxChars = 999, HFont font = 0, HFont glow = 0 )
{
	wchar_t w[512];
	V_UTF8ToUnicode( utf8, w, sizeof( w ) );
	int n = Min( WLen( w ), maxChars );
	return DrawWide( x, y, w, n, c, font, glow );
}

static int ULen( const char *utf8 )
{
	wchar_t w[512];
	V_UTF8ToUnicode( utf8, w, sizeof( w ) );
	return WLen( w );
}

// word wrap to cols characters: lines of wide text
static void WrapText( const char *utf8, int cols, CUtlVector<CUtlString> &lines )
{
	static wchar_t w[8192];
	V_UTF8ToUnicode( utf8, w, sizeof( w ) );
	// "\n" written as two characters (KeyValues without escapes) is a line break too
	int len = 0;
	for ( int r = 0; w[r]; r++ )
	{
		if ( w[r] == L'\\' && w[r + 1] == L'n' )
		{
			w[len++] = L'\n';
			r++;
		}
		else
			w[len++] = w[r];
	}
	w[len] = 0;
	int i = 0;
	while ( i <= len )
	{
		int end = i;
		while ( end < len && w[end] != L'\n' && end - i < cols )
			end++;
		int cut = end;
		if ( end < len && w[end] != L'\n' && end - i >= cols )
		{
			int sp = end;
			while ( sp > i && w[sp] != L' ' )
				sp--;
			if ( sp > i )
				cut = sp;
		}
		wchar_t tmp[256];
		int n = Min( cut - i, 255 );
		V_wcsncpy( tmp, &w[i], ( n + 1 ) * sizeof( wchar_t ) );
		tmp[n] = 0;
		char out[1024];
		V_UnicodeToUTF8( tmp, out, sizeof( out ) );
		lines.AddToTail( out );
		i = cut;
		if ( i < len && ( w[i] == L' ' || w[i] == L'\n' ) )
			i++;
		if ( i >= len )
			break;
	}
}

static void Fill( int x, int y, int w, int h, Color c )
{
	surface()->DrawSetColor( c );
	surface()->DrawFilledRect( x, y, x + w, y + h );
}

static void AddHot( int x, int y, int w, int h, int action, int arg )
{
	TermHot_t t = { x, y, w, h, action, arg };
	s_T.hot.AddToTail( t );
}

// background, scan lines, frame and the banner of the style
static void PaintFrame( int W, int H, int style, const TermColors_t &c, float now, const char *title, bool bActive )
{
	Fill( 0, 0, W, H, c.bg );
	if ( style == TSTYLE_COMBINE )
	{
		// a faint grid and a header bar
		surface()->DrawSetColor( c.faint.r(), c.faint.g(), c.faint.b(), 22 );
		for ( int x = 0; x < W; x += 24 )
			surface()->DrawFilledRect( x, 0, x + 1, H );
		for ( int y = 0; y < H; y += 24 )
			surface()->DrawFilledRect( 0, y, W, y + 1 );
		Fill( 0, 0, W, s_iCH + 10, c.bg2 );
		Fill( 0, s_iCH + 10, W, 2, c.dim );
		// moving scan bar
		int sy = (int)( fmodf( now * 60.0f, (float)H + 80 ) ) - 40;
		surface()->DrawSetColor( c.hi.r(), c.hi.g(), c.hi.b(), 14 );
		surface()->DrawFilledRect( 0, sy, W, sy + 30 );
		// corner brackets
		surface()->DrawSetColor( c.dim );
		int t = 3, L = 22;
		surface()->DrawFilledRect( 4, H - 4 - t, 4 + L, H - 4 );
		surface()->DrawFilledRect( 4, H - 4 - L, 4 + t, H - 4 );
		surface()->DrawFilledRect( W - 4 - L, H - 4 - t, W - 4, H - 4 );
		surface()->DrawFilledRect( W - 4 - t, H - 4 - L, W - 4, H - 4 );
	}
	// scan lines
	surface()->DrawSetColor( 0, 0, 0, style == TSTYLE_COMBINE ? 60 : 80 );
	for ( int y = 0; y < H; y += 3 )
		surface()->DrawFilledRect( 0, y, W, y + 1 );
	// darker corners (a CRT bulge)
	if ( style == TSTYLE_PC )
	{
		int e = W / 7;
		surface()->DrawSetColor( 0, 0, 0, 120 );
		surface()->DrawFilledRectFade( 0, 0, e, H, 120, 0, true );
		surface()->DrawFilledRectFade( W - e, 0, W, H, 0, 120, true );
		surface()->DrawFilledRectFade( 0, 0, W, H / 6, 110, 0, false );
		surface()->DrawFilledRectFade( 0, H - H / 6, W, H, 0, 110, false );
	}
}

// the banner lines at the top; returns the next text row y
static int PaintBanner( int W, int style, const TermColors_t &c, const char *title, const char *lang )
{
	int x = s_iCW, y = 6;
	bool ru = TermRussian( lang );
	if ( style == TSTYLE_COMBINE )
	{
		DrawU( x, y + 2, CFmtStr( "%s // %s", ru ? "СЕТЬ НАДЗОРА" : "OVERWATCH NET", title ), c.text );
		const char *right = ru ? "ДОСТУП: ГРАЖДАНСКИЙ" : "ACCESS: CIVIL";
		if ( W / s_iCW >= 50 )
			DrawU( W - s_iCW * ( ULen( right ) + 1 ), y + 2, right, c.dim );
		return s_iCH + 22;
	}
	DrawU( x, y, ru ? "ТЕРМИНАЛ ГОРСЕТИ · ВЕРСИЯ 2.11" : "CITYNET TERMINAL - VERSION 2.11", c.text );
	DrawU( x, y + s_iCH, title, c.text );
	return y + s_iCH * 2 + 8;
}

static void PaintIdle( C_RPMTerminal *t, int W, int H, float now )
{
	int style = t->m_iStyle;
	float flicker = 0.55f + 0.03f * sinf( now * 9.0f );
	TermColors_t c = ColorsOf( style, flicker );
	s_iReveal = 1 << 30;
	PaintFrame( W, H, style, c, now, t->m_szTitle, false );
	bool ru = TermRussian();
	int cy = H / 2 - s_iCH;
	int tl = ULen( t->m_szTitle );
	DrawU( ( W - tl * s_iCW ) / 2, cy, t->m_szTitle, c.text );
	const char *st = t->m_iLockState == TLOCK_LOCKOUT ? ( ru ? "ЗАБЛОКИРОВАНО" : "LOCKED OUT" )
		: t->m_iLockState == TLOCK_HACK ? ( ru ? "ТРЕБУЕТСЯ ПАРОЛЬ" : "PASSWORD REQUIRED" ) : ( ru ? "ГОТОВ" : "READY" );
	int sl = ULen( st );
	DrawU( ( W - sl * s_iCW ) / 2, cy + s_iCH + 6, st, t->m_iLockState == TLOCK_LOCKOUT ? c.bad : c.dim );
	if ( fmodf( now, 1.0f ) < 0.5f )
		Fill( ( W - s_iCW ) / 2, cy + s_iCH * 2 + 14, s_iCW, s_iCH - 4, c.text );
}

static void PaintMenu( int W, int H, int y0, const TermColors_t &c, float now )
{
	KeyValues *list = CurrentList();
	int x = s_iCW;
	int y = y0;
	int n = ListCount( list );
	bool ru = TermRussian( Content()->GetString( "language" ) );
	int total = n + ( s_T.path.Count() ? 1 : 0 );
	s_T.sel = clamp( s_T.sel, 0, Max( 0, total - 1 ) );
	int rowsFree = Max( 3, ( H - y - s_iCH * 3 ) / ( s_iCH + 2 ) );
	int first = clamp( s_T.sel - rowsFree + 1, 0, Max( 0, total - rowsFree ) );
	for ( int i = first; i < total && i < first + rowsFree; i++ )
	{
		KeyValues *e = i < n ? ListItem( list, i ) : NULL;
		CUtlString label;
		if ( e )
			label.Format( "[%s]", e->GetString( "title" ) );
		else
			label = ru ? "[< Назад]" : "[< Back]";
		int len = ULen( label );
		bool bSel = i == s_T.sel;
		if ( bSel )
		{
			Fill( x - 4, y - 1, len * s_iCW + 8, s_iCH, c.hi );
			DrawU( x, y, label, c.hiText, 999, s_hMono, 0 );
		}
		else
			DrawU( x, y, label, c.text );
		AddHot( x - 4, y - 1, Max( len * s_iCW + 8, W / 2 ), s_iCH, e ? TH_ENTRY : TH_BACK, i );
		y += s_iCH + 2;
	}
	if ( !s_T.message.IsEmpty() )
		DrawU( x, H - s_iCH * 2 - 4, s_T.message, c.dim );
	if ( fmodf( now, 1.0f ) < 0.5f )
		DrawU( x, H - s_iCH - 4, ">_", c.text );
}

static void PaintText( int W, int H, int y0, const TermColors_t &c, float now )
{
	KeyValues *e = FindEntryById( Content()->FindKey( "entries" ), s_T.textId );
	int x = s_iCW;
	int cols = Max( 10, W / s_iCW - 2 );
	bool ru = TermRussian( Content()->GetString( "language" ) );
	if ( !e )
	{
		SetPage( PG_MENU );
		return;
	}
	DrawU( x, y0, e->GetString( "title" ), c.text );
	y0 += s_iCH + 6;
	Fill( x, y0 - 3, W - 2 * x, 1, c.dim );
	CUtlVector<CUtlString> lines;
	WrapText( e->GetString( "text" ), cols, lines );
	int rows = Max( 3, ( H - y0 - s_iCH * 2 - 8 ) / s_iCH );
	s_T.maxScroll = Max( 0, lines.Count() - rows );
	s_T.scroll = clamp( s_T.scroll, 0, s_T.maxScroll );
	for ( int i = 0; i < rows && s_T.scroll + i < lines.Count(); i++ )
		DrawU( x, y0 + i * s_iCH, lines[s_T.scroll + i], c.text );
	int by = H - s_iCH - 6;
	const char *back = ru ? "[< Назад]" : "[< Back]";
	Fill( x - 4, by - 1, ULen( back ) * s_iCW + 8, s_iCH, c.hi );
	DrawU( x, by, back, c.hiText, 999, s_hMono, 0 );
	AddHot( x - 4, by - 1, ULen( back ) * s_iCW + 8, s_iCH, TH_BACK, 0 );
	if ( s_T.maxScroll > 0 )
	{
		CFmtStr more( "%d/%d", s_T.scroll + rows < lines.Count() ? s_T.scroll + rows : lines.Count(), lines.Count() );
		DrawU( W - s_iCW * ( ULen( more ) + 1 ), by, more, c.dim );
	}
}

static void PaintHack( int W, int H, int y0, const TermColors_t &c, float now )
{
	HackGame_t &h = s_T.hack;
	KeyValues *k = Content();
	bool ru = TermRussian( k->GetString( "language" ) );
	int x = s_iCW;
	int cols = W / s_iCW;
	bool wide = cols >= 58;
	DrawU( x, y0, ru ? "ВВЕДИТЕ ПАРОЛЬ" : "ENTER PASSWORD NOW", c.text );
	y0 += s_iCH;
	if ( h.tries == 1 && !h.won )
	{
		if ( fmodf( now, 0.8f ) < 0.55f )
			DrawU( x, y0, ru ? "!!! ВНИМАНИЕ: БЛОКИРОВКА БЛИЗКА !!!" : "!!! WARNING: LOCKOUT IMMINENT !!!", c.bad );
	}
	y0 += s_iCH;
	int tx = x + DrawU( x, y0, CFmtStr( "%s:", ru ? "Попыток осталось" : "Attempts remaining" ), c.text ) + s_iCW;
	for ( int i = 0; i < h.tries; i++ )
		Fill( tx + i * s_iCW * 2, y0 + 3, s_iCW, s_iCH - 8, c.text );
	y0 += s_iCH + 8;

	// the hovered span
	int ha = -1, hb = -1;
	HackSpan( h.cursor, ha, hb );
	int colW = ( wide ? 7 : 0 ) + HACK_COLS;
	for ( int col = 0; col < 2; col++ )
	{
		int cx = x + col * ( colW + 2 ) * s_iCW;
		for ( int row = 0; row < HACK_ROWS; row++ )
		{
			int ry = y0 + row * s_iCH;
			int base = ( col * HACK_ROWS + row ) * HACK_COLS;
			int px = cx;
			if ( wide )
			{
				px += DrawU( cx, ry, CFmtStr( "0x%04X", h.addr0 + base ), c.dim ) + s_iCW;
			}
			for ( int k2 = 0; k2 < HACK_COLS; k2++ )
			{
				int i = base + k2;
				bool bHi = i >= ha && i <= hb;
				if ( bHi )
					Fill( px + k2 * s_iCW, ry - 1, s_iCW, s_iCH, c.hi );
				DrawWide( px + k2 * s_iCW, ry, &h.s[i], 1, bHi ? c.hiText : c.text, s_hMono, bHi ? 0 : s_hMonoGlow );
				AddHot( px + k2 * s_iCW, ry - 1, s_iCW, s_iCH, TH_CHAR, i );
			}
		}
	}
	// the log: right of the columns, or below them on a narrow screen
	int lx = wide ? x + 2 * ( colW + 2 ) * s_iCW : x;
	int ly = wide ? y0 : y0 + HACK_ROWS * s_iCH + 6;
	int logRows = wide ? HACK_ROWS - 1 : Max( 2, ( H - ly ) / s_iCH - 1 );
	int start = Max( 0, h.nLog - logRows );
	for ( int i = start; i < h.nLog; i++ )
		DrawU( lx, ly + ( i - start ) * s_iCH, h.log[i], c.text, wide ? Max( 8, cols - ( lx / s_iCW ) - 1 ) : cols - 2 );
	// the hovered text with a cursor
	int cy = ly + ( Min( h.nLog, logRows ) ) * s_iCH;
	if ( wide )
		cy = y0 + ( HACK_ROWS - 1 ) * s_iCH;
	wchar_t cur[32];
	int n = clamp( hb - ha + 1, 0, 31 );
	if ( ha >= 0 )
	{
		V_wcsncpy( cur, &h.s[ha], ( n + 1 ) * sizeof( wchar_t ) );
		cur[n] = 0;
		int w = DrawU( lx, cy, ">", c.text );
		w += DrawWide( lx + w, cy, cur, n, c.text );
		if ( fmodf( now, 1.0f ) < 0.5f )
			Fill( lx + w, cy, s_iCW, s_iCH - 4, c.text );
	}
}

static void PaintActive( int W, int H, float now )
{
	KeyValues *k = Content();
	C_RPMTerminal *t = ActiveTerm();
	if ( !k || !t )
		return;
	int style = Style();
	float flicker = 0.93f + 0.07f * sinf( now * 13.0f ) * sinf( now * 3.1f );
	TermColors_t c = ColorsOf( style, flicker );
	s_T.hot.RemoveAll();
	s_iReveal = 1 << 30;
	PaintFrame( W, H, style, c, now, k->GetString( "title" ), true );
	int y = PaintBanner( W, style, c, k->GetString( "title" ), k->GetString( "language" ) );
	int x = s_iCW;
	// the welcome text of the terminal (not over the word game)
	const char *header = k->GetString( "header" );
	if ( header[0] && s_T.page != PG_HACK && s_T.page != PG_TEXT )
	{
		CUtlVector<CUtlString> lines;
		WrapText( header, Max( 10, W / s_iCW - 2 ), lines );
		for ( int i = 0; i < lines.Count() && i < 4; i++, y += s_iCH )
			DrawU( x, y, lines[i], c.text );
	}
	y += s_iCH / 2;
	bool ru = TermRussian( k->GetString( "language" ) );
	// the page types itself out (the banner above stays)
	float rate = s_T.page == PG_TEXT ? 260.0f : s_T.page == PG_HACK ? 1400.0f : 900.0f;
	s_iReveal = (int)( ( now - s_T.pageTime ) * rate ) + 8;
	switch ( s_T.page )
	{
	case PG_BOOT:
		if ( fmodf( now, 0.5f ) < 0.3f )
			DrawU( x, y, ">_", c.text );
		break;
	case PG_MENU:
		PaintMenu( W, H, y, c, now );
		break;
	case PG_TEXT:
		PaintText( W, H, y, c, now );
		break;
	case PG_HACK:
		PaintHack( W, H, y, c, now );
		break;
	case PG_LOCKOUT:
	{
		const char *l1 = ru ? "ТЕРМИНАЛ ЗАБЛОКИРОВАН" : "TERMINAL LOCKED";
		const char *l2 = ru ? "ОБРАТИТЕСЬ К АДМИНИСТРАТОРУ" : "PLEASE CONTACT AN ADMINISTRATOR";
		DrawU( ( W - ULen( l1 ) * s_iCW ) / 2, H / 2 - s_iCH, l1, c.bad );
		DrawU( ( W - ULen( l2 ) * s_iCW ) / 2, H / 2 + 4, l2, c.text );
		break;
	}
	case PG_SKILL:
	{
		const char *l1 = ru ? "ТРЕБУЕТСЯ ПАРОЛЬ" : "PASSWORD REQUIRED";
		DrawU( x, y, l1, c.text );
		CFmtStr l2( ru ? "Не хватает навыка «%s»: нужно %d (у вас %d)" : "Not enough %s: %d needed (you have %d)",
			k->GetString( "skill_name" ), k->GetInt( "need" ), k->GetInt( "skill" ) );
		CUtlVector<CUtlString> lines;
		WrapText( l2, Max( 10, W / s_iCW - 2 ), lines );
		for ( int i = 0; i < lines.Count(); i++ )
			DrawU( x, y + ( i + 1 ) * s_iCH + 6, lines[i], c.bad );
		break;
	}
	}
	if ( s_T.page != PG_HACK && s_T.page != PG_TEXT )
	{
		const char *hint = ru ? "Tab — выйти" : "Tab - leave";
		DrawU( W - s_iCW * ( ULen( hint ) + 1 ), H - s_iCH - 4, hint, c.dim );
	}
}

//=============================================================================
// the 3D panel and the screen renderable
//=============================================================================
class CRPMTermScreenPanel : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMTermScreenPanel, Panel );
public:
	CRPMTermScreenPanel() : BaseClass( NULL, "RPMTermScreen" ), m_pTerm( NULL ), m_bActive( false )
	{
		SetPaintBackgroundEnabled( false );
		SetVisible( true );
	}

	virtual void Paint()
	{
		if ( !m_pTerm )
			return;
		float now = gpGlobals->realtime;
		if ( m_bActive )
			PaintActive( GetWide(), GetTall(), now );
		else
			PaintIdle( m_pTerm, GetWide(), GetTall(), now );
	}

	C_RPMTerminal *m_pTerm;
	bool m_bActive;
};

static CRPMTermScreenPanel *s_pScreenPanel = NULL;

// pixel size of the panel for a screen: a grid of 24 text rows (32 on a narrow one)
static void PanelSize( float w, float h, int &pw, int &ph )
{
	LoadTermFonts();
	float aspect = w / Max( 0.1f, h );
	int rows = aspect < 1.0f ? 34 : 24;
	ph = rows * s_iCH + 16;
	pw = (int)( ph * aspect );
}

class C_RPMTerminalScreen : public C_BaseEntity
{
	DECLARE_CLASS( C_RPMTerminalScreen, C_BaseEntity );
public:
	C_RPMTerminalScreen( C_RPMTerminal *pTerm ) : m_pTerm( pTerm ) {}

	// not by the view: the leaf system asks this only when the visibility is updated
	virtual bool ShouldDraw() { return m_pTerm && !m_pTerm->IsDormant(); }
	bool InViewRange()
	{
		Vector c, n, r, u;
		m_pTerm->ScreenFrame( c, n, r, u );
		Vector d = CurrentViewOrigin() - c;
		return DotProduct( d, n ) > 0.0f && d.LengthSqr() < Square( cl_rpm_terminal_dist.GetFloat() );
	}
	virtual bool IsTransparent() { return true; }
	virtual RenderGroup_t GetRenderGroup() { return RENDER_GROUP_TRANSLUCENT_ENTITY; }
	virtual ShadowType_t ShadowCastType() { return SHADOWS_NONE; }
	virtual bool ShouldReceiveProjectedTextures( int flags ) { return false; }
	virtual void GetRenderBounds( Vector &mins, Vector &maxs )
	{
		float w = m_pTerm ? m_pTerm->m_flScreenW * 0.5f : 8.0f, h = m_pTerm ? m_pTerm->m_flScreenH * 0.5f : 8.0f;
		mins.Init( -2.0f, -w, -h );
		maxs.Init( 2.0f, w, h );
	}
	void Place()
	{
		if ( !m_pTerm )
			return;
		Vector c, n, r, u;
		m_pTerm->ScreenFrame( c, n, r, u );
		QAngle ang;
		VectorAngles( n, u, ang );
		SetAbsOrigin( c );
		SetAbsAngles( ang );
		UpdateVisibility();
		if ( RenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
			ClientLeafSystem()->RenderableChanged( RenderHandle() );
	}
	virtual int DrawModel( int flags )
	{
		if ( !m_pTerm || !InViewRange() )
			return 0;
		if ( !s_pScreenPanel )
			s_pScreenPanel = new CRPMTermScreenPanel();
		Vector c, n, r, u;
		m_pTerm->ScreenFrame( c, n, r, u );
		float w = m_pTerm->m_flScreenW, h = m_pTerm->m_flScreenH;
		int pw, ph;
		PanelSize( w, h, pw, ph );
		bool bActive = s_T.hTerm.Get() == m_pTerm && Content() != NULL;
		if ( bActive )
		{
			s_T.panelW = pw;
			s_T.panelH = ph;
		}
		s_pScreenPanel->m_pTerm = m_pTerm;
		s_pScreenPanel->m_bActive = bActive;
		s_pScreenPanel->SetBounds( 0, 0, pw, ph );
		// panel space: x right, y up, z out of the screen; the origin is the upper left corner
		Vector ul = c - r * ( w * 0.5f ) + u * ( h * 0.5f ) + n * 0.25f;
		VMatrix m;
		m.SetBasisVectors( r, u, n );
		m.SetTranslation( ul );
		g_pMatSystemSurface->DrawPanelIn3DSpace( s_pScreenPanel->GetVPanel(), m, pw, ph, w, h );
		if ( cl_rpm_terminal_debug.GetBool() )
		{
			debugoverlay->AddBoxOverlay( c, Vector( -0.2f, -w * 0.5f, -h * 0.5f ), Vector( 0.2f, w * 0.5f, h * 0.5f ), GetAbsAngles(), 255, 0, 255, 20, 0.0f );
		}
		return 1;
	}

	C_RPMTerminal *m_pTerm;
};

void C_RPMTerminal::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( !m_pScreen )
	{
		m_pScreen = new C_RPMTerminalScreen( this );
		if ( !m_pScreen->InitializeAsClientEntity( NULL, RENDER_GROUP_TRANSLUCENT_ENTITY ) )
		{
			m_pScreen->Release();
			m_pScreen = NULL;
			return;
		}
	}
	m_pScreen->Place();
}

void C_RPMTerminal::UpdateOnRemove()
{
	if ( m_pScreen )
	{
		m_pScreen->m_pTerm = NULL;
		m_pScreen->Release();
		m_pScreen = NULL;
	}
	BaseClass::UpdateOnRemove();
}

//=============================================================================
// input: an invisible popup; the mouse is traced onto the screen
//=============================================================================
static bool CursorToPanel( int mx, int my, int &px, int &py )
{
	C_RPMTerminal *t = ActiveTerm();
	if ( !t || !s_T.viewValid || s_T.panelW <= 0 )
		return false;
	int sw, sh;
	engine->GetScreenSize( sw, sh );
	float tanV = tanf( DEG2RAD( s_T.viewFov * 0.5f ) ) * 0.75f;
	float tanH = tanV * sw / (float)Max( 1, sh );
	float nx = ( 2.0f * mx / sw - 1.0f ) * tanH, ny = ( 1.0f - 2.0f * my / sh ) * tanV;
	Vector f, r, u;
	AngleVectors( s_T.viewAngles, &f, &r, &u );
	Vector dir = f + r * nx + u * ny;
	Vector c, n, sr, su;
	t->ScreenFrame( c, n, sr, su );
	float denom = DotProduct( dir, n );
	if ( fabsf( denom ) < 1e-4f )
		return false;
	float d = DotProduct( c - s_T.viewOrigin, n ) / denom;
	if ( d <= 0.0f )
		return false;
	Vector hit = s_T.viewOrigin + dir * d - c;
	float fx = DotProduct( hit, sr ) / t->m_flScreenW + 0.5f;
	float fy = 0.5f - DotProduct( hit, su ) / t->m_flScreenH;
	px = (int)( fx * s_T.panelW );
	py = (int)( fy * s_T.panelH );
	return fx >= 0.0f && fx <= 1.0f && fy >= 0.0f && fy <= 1.0f;
}

static int HotAt( int px, int py )
{
	for ( int i = 0; i < s_T.hot.Count(); i++ )
	{
		const TermHot_t &h = s_T.hot[i];
		if ( px >= h.x && px < h.x + h.w && py >= h.y && py < h.y + h.h )
			return i;
	}
	return -1;
}

static void TermLeave();
static void TermBack();

static void OpenEntry( KeyValues *e )
{
	if ( !e )
		return;
	const char *kind = e->GetString( "kind" );
	SndSelect();
	if ( !Q_stricmp( kind, "menu" ) )
	{
		s_T.path.AddToTail( e->GetString( "id" ) );
		SetPage( PG_MENU );
		s_T.message.Clear();
	}
	else if ( !Q_stricmp( kind, "text" ) )
	{
		s_T.textId = e->GetString( "id" );
		SetPage( PG_TEXT );
		SendCmd( "read", e->GetString( "id" ) );
		TermSound( Style() == TSTYLE_COMBINE ? "npc/scanner/scanner_scan2.wav" : "ambient/machines/keyboard_fast1_1second.wav", 0.3f );
	}
	else
		SendCmd( "run", e->GetString( "id" ) );
}

static void TermActivate()
{
	switch ( s_T.page )
	{
	case PG_MENU:
	{
		KeyValues *list = CurrentList();
		int n = ListCount( list );
		if ( s_T.sel < n )
			OpenEntry( ListItem( list, s_T.sel ) );
		else
			TermBack();
		break;
	}
	case PG_TEXT:
		TermBack();
		break;
	case PG_HACK:
		HackSelect( s_T.hack.cursor );
		break;
	default:
		break;
	}
}

static void TermBack()
{
	if ( s_T.page == PG_TEXT )
	{
		SetPage( PG_MENU );
		SndMove();
		return;
	}
	if ( s_T.page == PG_MENU && s_T.path.Count() )
	{
		s_T.path.RemoveMultipleFromTail( 1 );
		SetPage( PG_MENU );
		SndMove();
		return;
	}
	TermLeave();
}

static void MoveSel( int d )
{
	if ( s_T.page == PG_MENU )
	{
		int total = ListCount( CurrentList() ) + ( s_T.path.Count() ? 1 : 0 );
		if ( total <= 0 )
			return;
		s_T.sel = ( s_T.sel + d + total ) % total;
		SndMove();
	}
	else if ( s_T.page == PG_TEXT )
	{
		s_T.scroll = clamp( s_T.scroll + d, 0, s_T.maxScroll );
	}
}

static void HackMove( int dx, int dy )
{
	HackGame_t &h = s_T.hack;
	int i = h.cursor;
	int col = i / ( HACK_COLS * HACK_ROWS ), rest = i % ( HACK_COLS * HACK_ROWS );
	int row = rest / HACK_COLS, ch = rest % HACK_COLS;
	if ( dx )
	{
		// skip over the rest of a word
		int a, b;
		HackSpan( i, a, b );
		int next = dx > 0 ? b + 1 : a - 1;
		int nrest = 0;
		if ( next >= 0 && next < HACK_N && ( next / ( HACK_COLS * HACK_ROWS ) ) == col && ( next % ( HACK_COLS * HACK_ROWS ) ) / HACK_COLS == row )
			h.cursor = next;
		else
		{
			// to the other column on the same row
			col = clamp( col + dx, 0, 1 );
			nrest = row * HACK_COLS + ( dx > 0 ? 0 : HACK_COLS - 1 );
			h.cursor = col * HACK_COLS * HACK_ROWS + nrest;
		}
	}
	if ( dy )
	{
		row = clamp( row + dy, 0, HACK_ROWS - 1 );
		h.cursor = col * HACK_COLS * HACK_ROWS + row * HACK_COLS + ch;
	}
	SndMove();
}

class CRPMTermInput : public Panel
{
	DECLARE_CLASS_SIMPLE( CRPMTermInput, Panel );
public:
	CRPMTermInput( Panel *parent ) : BaseClass( parent, "RPMTermInput" )
	{
		SetPaintBackgroundEnabled( false );
		SetVisible( false );
	}

	void Show()
	{
		if ( IsVisible() )
			return;
		if ( g_pClientMode && g_pClientMode->GetViewport() && GetParent() != g_pClientMode->GetViewport() )
			SetParent( g_pClientMode->GetViewport() );
		int sw, sh;
		surface()->GetScreenSize( sw, sh );
		SetBounds( 0, 0, sw, sh );
		SetVisible( true );
		MakePopup();
		SetZPos( 1400 );
		SetKeyBoardInputEnabled( true );
		SetMouseInputEnabled( true );
		MoveToFront();
		RequestFocus();
		SetCursor( dc_none );
		engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
	}

	void HideIt()
	{
		if ( !IsVisible() )
			return;
		SetVisible( false );
		SetKeyBoardInputEnabled( false );
		SetMouseInputEnabled( false );
		engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
	}

	virtual void OnCursorMoved( int x, int y )
	{
		int px, py;
		if ( !s_T.active || !CursorToPanel( x, y, px, py ) )
			return;
		int hi = HotAt( px, py );
		if ( hi < 0 )
			return;
		const TermHot_t &h = s_T.hot[hi];
		if ( h.action == TH_CHAR && s_T.page == PG_HACK )
		{
			int a0, b0, a1, b1;
			HackSpan( s_T.hack.cursor, a0, b0 );
			HackSpan( h.arg, a1, b1 );
			if ( a0 != a1 )
				SndMove();
			s_T.hack.cursor = h.arg;
		}
		else if ( ( h.action == TH_ENTRY || h.action == TH_BACK ) && s_T.page == PG_MENU && s_T.sel != h.arg )
		{
			s_T.sel = h.arg;
			SndMove();
		}
	}

	virtual void OnMousePressed( MouseCode code )
	{
		if ( !s_T.active )
			return;
		if ( code == MOUSE_RIGHT )
		{
			TermBack();
			return;
		}
		if ( code != MOUSE_LEFT )
			return;
		int mx, my, px, py;
		input()->GetCursorPos( mx, my );
		if ( !CursorToPanel( mx, my, px, py ) )
			return;
		int hi = HotAt( px, py );
		if ( hi < 0 )
			return;
		const TermHot_t &h = s_T.hot[hi];
		if ( h.action == TH_BACK && s_T.page == PG_TEXT )
			TermBack();
		else if ( s_T.page == PG_MENU )
		{
			s_T.sel = h.arg;
			TermActivate();
		}
		else if ( s_T.page == PG_HACK && h.action == TH_CHAR )
		{
			s_T.hack.cursor = h.arg;
			TermActivate();
		}
	}

	virtual void OnMouseWheeled( int delta )
	{
		if ( s_T.page == PG_TEXT )
			s_T.scroll = clamp( s_T.scroll - delta * 3, 0, s_T.maxScroll );
		else
			MoveSel( -delta );
	}

	virtual void OnKeyCodeTyped( KeyCode code )
	{
		if ( !s_T.active )
			return;
		switch ( code )
		{
		case KEY_ESCAPE:
			TermLeave();
			engine->ClientCmd_Unrestricted( "gameui_hide\n" );
			engine->ClientCmd_Unrestricted( "wait;gameui_hide\n" );
			return;
		case KEY_TAB:
			TermLeave();
			return;
		case KEY_BACKSPACE:
			TermBack();
			return;
		case KEY_UP: case KEY_W:
			if ( s_T.page == PG_HACK ) HackMove( 0, -1 ); else MoveSel( -1 );
			return;
		case KEY_DOWN: case KEY_S:
			if ( s_T.page == PG_HACK ) HackMove( 0, 1 ); else MoveSel( 1 );
			return;
		case KEY_LEFT: case KEY_A:
			if ( s_T.page == PG_HACK ) HackMove( -1, 0 );
			return;
		case KEY_RIGHT: case KEY_D:
			if ( s_T.page == PG_HACK ) HackMove( 1, 0 );
			return;
		case KEY_PAGEUP:
			if ( s_T.page == PG_TEXT ) s_T.scroll = clamp( s_T.scroll - 10, 0, s_T.maxScroll );
			return;
		case KEY_PAGEDOWN:
			if ( s_T.page == PG_TEXT ) s_T.scroll = clamp( s_T.scroll + 10, 0, s_T.maxScroll );
			return;
		case KEY_ENTER: case KEY_PAD_ENTER: case KEY_SPACE: case KEY_E:
			TermActivate();
			return;
		default:
			break;
		}
		BaseClass::OnKeyCodeTyped( code );
	}
};

static CRPMTermInput *s_pTermInput = NULL;

static CRPMTermInput *TermInput()
{
	if ( !s_pTermInput )
		s_pTermInput = new CRPMTermInput( g_pClientMode ? g_pClientMode->GetViewport() : NULL );
	return s_pTermInput;
}

static void TermLeave()
{
	if ( !s_T.active )
		return;
	SendCmd( "close" );
	s_T.active = false;
	s_T.token = 0;
	TermSound( Style() == TSTYLE_COMBINE ? "buttons/combine_button2.wav" : "buttons/button16.wav", 0.35f );
}

static void TermEndAll()
{
	if ( s_pTermInput )
		s_pTermInput->HideIt();
	if ( s_T.pContent )
		s_T.pContent->deleteThis();
	s_T.pContent = NULL;
	s_T.hTerm = NULL;
	s_T.active = false;
	s_T.camT = 0.0f;
	s_T.token = 0;
	s_T.viewValid = false;
	s_T.hack.on = false;
	s_T.page = PG_NONE;
}

//=============================================================================
// the messages of the server
//=============================================================================
static void OnContent( KeyValues *k )
{
	bool bOpen = k->GetBool( "open" );
	C_RPMTerminal *t = dynamic_cast<C_RPMTerminal *>( ClientEntityList().GetBaseEntity( k->GetInt( "ent" ) ) );
	if ( !t )
	{
		k->deleteThis();
		return;
	}
	int oldLock = s_T.pContent ? s_T.pContent->GetInt( "lock", -1 ) : -1;
	if ( s_T.pContent )
		s_T.pContent->deleteThis();
	s_T.pContent = k;
	s_T.token = k->GetInt( "token" );
	int lock = k->GetInt( "lock" );
	const char *msg = k->GetString( "message" );
	if ( bOpen || s_T.hTerm.Get() != t || !s_T.active )
	{
		s_T.hTerm = t;
		s_T.active = true;
		s_T.path.RemoveAll();
		s_T.message = msg;
		s_T.hack.on = false;
		SetPage( PG_BOOT );
		TermInput()->Show();
		TermSound( t->m_iStyle == TSTYLE_COMBINE ? "ambient/machines/combine_terminal_idle2.wav" : "buttons/button17.wav", 0.4f );
		return;
	}
	if ( msg[0] )
	{
		s_T.message = msg;
		s_T.messageTime = gpGlobals->realtime;
	}
	if ( lock != oldLock )
	{
		s_T.path.RemoveAll();
		s_T.hack.on = false;
		SetPage( lock == TLOCK_OPEN ? PG_MENU : lock == TLOCK_HACK ? PG_HACK : lock == TLOCK_LOCKOUT ? PG_LOCKOUT : PG_SKILL );
		if ( lock == TLOCK_HACK )
			HackStart( k->GetInt( "level" ), k->GetInt( "skill" ) );
		if ( lock == TLOCK_LOCKOUT )
			SndDeny();
		if ( lock == TLOCK_OPEN )
			TermSound( Style() == TSTYLE_COMBINE ? "buttons/combine_button7.wav" : "buttons/button14.wav", 0.4f );
	}
}

static CUtlString s_TermAccum;

static void __MsgFunc_RPMTerm( bf_read &msg )
{
	int flags = msg.ReadByte();
	char piece[256];
	msg.ReadString( piece, sizeof( piece ) );
	if ( flags & 1 )
		s_TermAccum.Clear();
	s_TermAccum.Append( piece );
	if ( !( flags & 2 ) )
		return;
	KeyValues *k = new KeyValues( "Term" );
	if ( k->LoadFromBuffer( "Term", s_TermAccum.Get() ) )
		OnContent( k );
	else
		k->deleteThis();
	s_TermAccum.Clear();
}

//=============================================================================
// per frame: the camera progress, the boot page, the end of the word game
//=============================================================================
class CRPMTerminalSystem : public CAutoGameSystemPerFrame
{
public:
	CRPMTerminalSystem() : CAutoGameSystemPerFrame( "CRPMTerminalSystem" ) {}

	virtual bool Init()
	{
		usermessages->HookMessage( "RPMTerm", __MsgFunc_RPMTerm );
		return true;
	}

	virtual void LevelShutdownPreEntity()
	{
		TermEndAll();
	}

	virtual void Update( float frametime )
	{
		float now = gpGlobals->realtime;
		float dt = clamp( now - s_T.lastTime, 0.0f, 0.1f );
		s_T.lastTime = now;
		if ( !s_T.hTerm.Get() )
		{
			if ( s_T.active || s_T.camT > 0.0f )
				TermEndAll();
			return;
		}
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( s_T.active && ( !pPlayer || !pPlayer->IsAlive() ) )
			TermLeave();
		if ( s_T.active )
			s_T.camT = Min( 1.0f, s_T.camT + dt / 0.6f );
		else
		{
			s_T.camT = Max( 0.0f, s_T.camT - dt / 0.45f );
			if ( s_T.camT <= 0.0f )
				TermEndAll();
			return;
		}
		KeyValues *k = Content();
		if ( s_T.page == PG_BOOT && k && now - s_T.pageTime > 0.75f )
		{
			int lock = k->GetInt( "lock" );
			SetPage( lock == TLOCK_OPEN ? PG_MENU : lock == TLOCK_HACK ? PG_HACK : lock == TLOCK_LOCKOUT ? PG_LOCKOUT : PG_SKILL );
			if ( lock == TLOCK_HACK )
				HackStart( k->GetInt( "level" ), k->GetInt( "skill" ) );
		}
		HackGame_t &h = s_T.hack;
		if ( s_T.page == PG_HACK && h.on && h.endTime > 0 && now >= h.endTime )
		{
			h.endTime = 0;
			h.on = false;
			SendCmd( h.won ? "hacked" : "lockout" );
		}
	}
};

static CRPMTerminalSystem s_TerminalSystem;

//=============================================================================
// the camera (ClientModeShared::OverrideView)
//=============================================================================
void RPMTerminal_OverrideView( CViewSetup *pSetup )
{
	C_RPMTerminal *t = ActiveTerm();
	if ( !t || s_T.camT <= 0.0f )
	{
		s_T.viewValid = false;
		return;
	}
	Vector c, n, r, u;
	t->ScreenFrame( c, n, r, u );
	// far enough that the screen fills cl_rpm_terminal_fill of the view
	int sw, sh;
	engine->GetScreenSize( sw, sh );
	float tanV = tanf( DEG2RAD( pSetup->fov * 0.5f ) ) * 0.75f;
	float tanH = tanV * sw / (float)Max( 1, sh );
	float fill = clamp( cl_rpm_terminal_fill.GetFloat(), 0.3f, 1.0f );
	float d = Max( t->m_flScreenH * 0.5f / ( tanV * fill ), t->m_flScreenW * 0.5f / ( tanH * fill ) );
	d = Max( d, 9.0f );
	Vector camPos = c + n * d;
	QAngle camAng;
	VectorAngles( -n, u, camAng );
	float e = s_T.camT * s_T.camT * ( 3.0f - 2.0f * s_T.camT );
	Quaternion q0, q1, q;
	AngleQuaternion( pSetup->angles, q0 );
	AngleQuaternion( camAng, q1 );
	QuaternionSlerp( q0, q1, e, q );
	pSetup->origin = Lerp( e, pSetup->origin, camPos );
	QuaternionAngles( q, pSetup->angles );
	s_T.viewOrigin = pSetup->origin;
	s_T.viewAngles = pSetup->angles;
	s_T.viewFov = pSetup->fov;
	s_T.viewValid = true;
}

//=============================================================================
// console
//=============================================================================
CON_COMMAND_F( rpm_terminal_key, "HL2RPM: drive the terminal from the console (tests): up | down | left | right | enter | back | leave | win | fail", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 || !s_T.active || !s_pTermInput )
		return;
	const char *k = args[1];
	if ( !Q_stricmp( k, "up" ) ) s_pTermInput->OnKeyCodeTyped( KEY_UP );
	else if ( !Q_stricmp( k, "down" ) ) s_pTermInput->OnKeyCodeTyped( KEY_DOWN );
	else if ( !Q_stricmp( k, "left" ) ) s_pTermInput->OnKeyCodeTyped( KEY_LEFT );
	else if ( !Q_stricmp( k, "right" ) ) s_pTermInput->OnKeyCodeTyped( KEY_RIGHT );
	else if ( !Q_stricmp( k, "enter" ) ) s_pTermInput->OnKeyCodeTyped( KEY_ENTER );
	else if ( !Q_stricmp( k, "back" ) ) s_pTermInput->OnKeyCodeTyped( KEY_BACKSPACE );
	else if ( !Q_stricmp( k, "leave" ) ) s_pTermInput->OnKeyCodeTyped( KEY_TAB );
	else if ( ( !Q_stricmp( k, "win" ) || !Q_stricmp( k, "fail" ) ) && s_T.page == PG_HACK && s_T.hack.on )
	{
		// select the password / a wrong word (its first letter)
		HackGame_t &h = s_T.hack;
		bool win = !Q_stricmp( k, "win" );
		for ( int i = 0; i < HACK_N; i++ )
		{
			int w = h.wordAt[i];
			if ( w >= 0 && !h.removed[w] && ( win ? w == h.pass : w != h.pass ) )
			{
				h.cursor = i;
				if ( args.ArgC() < 3 || atoi( args[2] ) )
					HackSelect( i );
				break;
			}
		}
	}
}

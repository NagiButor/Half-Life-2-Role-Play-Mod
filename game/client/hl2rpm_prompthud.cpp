//========= HL2RPM ============================================================//
//
// The use prompt under the crosshair (M6), in the look of hl2rpm_ui.h:
//
//      [E] ГОВОРИТЬ            [E] ОТКРЫТЬ             [E] ВЗЯТЬ
//        Болтун                  Шкафчик                 Аптечка   0,5 кг
//                                Заперто · Взлом 50
//
// The server says what USE would do (user message "RPMPrompt", hl2rpm_prompt.cpp);
// the key is the one bound to +use.
//
//=============================================================================//
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "usermessages.h"
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPMUI;

extern bool HL2RPM_IsDialogOpen();
extern bool RPMMenu_IsOpen();
extern bool RPMLoot_IsOpen();
extern bool RPMLockpick_IsOpen();

static ConVar cl_rpm_prompt( "cl_rpm_prompt", "1", FCVAR_ARCHIVE, "HL2RPM: the use prompt under the crosshair" );

enum { PV_NONE, PV_TALK, PV_SEARCH, PV_OPEN, PV_OPEN_DOOR, PV_CLOSE_DOOR, PV_TAKE, PV_USE, PV_SIT, PV_LIE, PV_LEAN, PV_STAND };

struct PromptState_t
{
	int verb, flags, level;
	char name[128], key[128];
	float weight;
	float changed;
};
static PromptState_t s_Prompt, s_Shown;		// the last message / what is on screen (it fades out)

static void __MsgFunc_RPMPrompt( bf_read &msg )
{
	PromptState_t p;
	p.verb = msg.ReadByte();
	p.flags = msg.ReadByte();
	p.level = msg.ReadShort();
	msg.ReadString( p.name, sizeof( p.name ) );
	msg.ReadString( p.key, sizeof( p.key ) );
	p.weight = msg.ReadFloat();
	p.changed = gpGlobals->realtime;
	s_Prompt = p;
	if ( p.verb != PV_NONE )
		s_Shown = p;
}

static bool IsRussian() { return !Q_stricmp( RPMItems::Language(), "russian" ); }
#define TR( en, ru ) ( IsRussian() ? ( ru ) : ( en ) )

static const char *VerbName( int verb )
{
	switch ( verb )
	{
	case PV_TALK: return TR( "Talk", "Говорить" );
	case PV_SEARCH: return TR( "Search", "Обыскать" );
	case PV_OPEN: return TR( "Open", "Открыть" );
	case PV_OPEN_DOOR: return TR( "Open", "Открыть" );
	case PV_CLOSE_DOOR: return TR( "Close", "Закрыть" );
	case PV_TAKE: return TR( "Take", "Взять" );
	case PV_USE: return TR( "Use", "Использовать" );
	case PV_SIT: return TR( "Sit", "Сесть" );
	case PV_LIE: return TR( "Lie down", "Лечь" );
	case PV_LEAN: return TR( "Lean", "Прислониться" );
	case PV_STAND: return TR( "Stand up", "Встать" );
	}
	return "";
}

class CHudRPMPrompt : public CHudElement, public Panel
{
	DECLARE_CLASS_SIMPLE( CHudRPMPrompt, Panel );
public:
	CHudRPMPrompt( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudRPMPrompt" )
	{
		SetParent( g_pClientMode->GetViewport() );
		SetHiddenBits( HIDEHUD_PLAYERDEAD );
		s_Prompt.verb = s_Shown.verb = PV_NONE;
		s_Prompt.changed = s_Shown.changed = 0.0f;
	}

	virtual void Init()
	{
		usermessages->HookMessage( "RPMPrompt", __MsgFunc_RPMPrompt );
	}

	virtual void LevelInit()
	{
		s_Prompt.verb = s_Shown.verb = PV_NONE;
	}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		LoadFonts( pScheme );
		SetPaintBackgroundEnabled( false );
		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );
		int w, h;
		surface()->GetScreenSize( w, h );
		SetBounds( 0, 0, w, h );
	}

	// 0..1: in while there is a target, out after it is gone
	float Alpha()
	{
		float t = gpGlobals->realtime - s_Prompt.changed;
		if ( s_Prompt.verb != PV_NONE )
			return Ramp( s_Prompt.changed, 0.12f );
		return 1.0f - clamp( t / 0.2f, 0.0f, 1.0f );
	}

	virtual bool ShouldDraw()
	{
		if ( !cl_rpm_prompt.GetBool() || !CHudElement::ShouldDraw() || s_Shown.verb == PV_NONE )
			return false;
		if ( HL2RPM_IsDialogOpen() || RPMMenu_IsOpen() || RPMLoot_IsOpen() || RPMLockpick_IsOpen() )
			return false;
		return Alpha() > 0.0f;
	}

	virtual void Paint()
	{
		const Palette_t &p = Pal();
		const PromptState_t &s = s_Shown;
		float a = Alpha();
		int sw, sh;
		GetSize( sw, sh );
		const int cx = sw / 2, top = sh / 2 + Sc( 46 );

		// the key bound to +use
		const char *bind = engine->Key_LookupBinding( "+use" );
		char key[16];
		Q_strncpy( key, bind && bind[0] ? bind : "e", sizeof( key ) );
		Q_strupr( key );
		// somebody's container (flag 8, the owner in the key field): stealing, in red
		const bool steal = ( s.flags & 8 ) != 0;
		const char *verb = steal ? TR( "Steal", "Украсть" ) : VerbName( s.verb );

		// lines: verb / name (+ weight) / lock or empty
		CUtlString line2 = s.name;
		CUtlString weight;
		if ( s.verb == PV_TAKE && s.weight > 0.0f )
			weight.Format( TR( "%.1f kg", "%.1f кг" ), s.weight );
		CUtlString line3;
		bool bad = false;
		if ( s.flags & 1 )
		{
			bad = true;
			if ( s.flags & 4 )
				line3 = s.key[0] ? CUtlString( CFmtStr( TR( "Jammed \xc2\xb7 needs %s", "Заклинило · нужен «%s»" ), s.key ) ) : CUtlString( TR( "Jammed", "Заклинило" ) );
			else if ( s.level > 0 && s.key[0] )
				line3.Format( TR( "Locked \xc2\xb7 Lockpick %d or %s", "Заперто · Взлом %d или «%s»" ), s.level, s.key );
			else if ( s.level > 0 )
				line3.Format( TR( "Locked \xc2\xb7 Lockpick %d", "Заперто · Взлом %d" ), s.level );
			else if ( s.key[0] )
				line3.Format( TR( "Locked \xc2\xb7 needs %s", "Заперто · нужен «%s»" ), s.key );
			else
				line3 = TR( "Locked", "Заперто" );
		}
		else if ( s.flags & 2 )
			line3 = TR( "Empty", "Пусто" );
		if ( steal )
		{
			CUtlString owner;
			owner.Format( TR( "Owner: %s", "Владелец: %s" ), s.key );
			line3 = line3.Length() ? CUtlString( CFmtStr( "%s \xc2\xb7 %s", line3.Get(), owner.Get() ) ) : owner;
		}

		const int track = Sc( 2 );
		int keyW = TextWidth( F_SMALL, key ) + Sc( 12 );
		int verbW = SpacedWidth( F_TAB, verb, track );
		int w1 = keyW + Sc( 10 ) + verbW;
		int w2 = line2.Length() ? TextWidth( F_TEXT, line2 ) + ( weight.Length() ? TextWidth( F_SMALL, weight ) + Sc( 14 ) : 0 ) : 0;
		int w3 = line3.Length() ? TextWidth( F_SMALL, line3 ) : 0;
		int boxW = Max( w1, Max( w2, w3 ) ) + Sc( 36 );
		int hTab = surface()->GetFontTall( Font( F_TAB ) ), hText = surface()->GetFontTall( Font( F_TEXT ) ), hSmall = surface()->GetFontTall( Font( F_SMALL ) );
		int boxH = Sc( 12 ) + hTab + ( line2.Length() ? Sc( 4 ) + hText : 0 ) + ( line3.Length() ? Sc( 4 ) + hSmall : 0 ) + Sc( 12 );
		int bx = cx - boxW / 2;
		Plate( bx, top, boxW, boxH, (int)( 190 * a ), Sc( 6 ) );

		int y = top + Sc( 12 );
		int x = cx - w1 / 2;
		// [E]: a framed key
		surface()->DrawSetColor( p.accent.r(), p.accent.g(), p.accent.b(), (int)( 200 * a ) );
		surface()->DrawOutlinedRect( x, y + ( hTab - hSmall ) / 2 - Sc( 2 ), x + keyW, y + ( hTab + hSmall ) / 2 + Sc( 2 ) );
		Text( F_SMALL, x + keyW / 2, y + ( hTab - hSmall ) / 2, Fade( p.accent, a ), key, false, 1 );
		Spaced( F_TAB, x + keyW + Sc( 10 ), y, Fade( bad ? p.dim : steal ? p.bad : p.accent, a ), verb, track, !bad );
		y += hTab + Sc( 4 );
		if ( line2.Length() )
		{
			int nx = cx - w2 / 2;
			nx += Text( F_TEXT, nx, y, Fade( p.text, a ), line2 );
			if ( weight.Length() )
				Text( F_SMALL, nx + Sc( 14 ), y + ( hText - hSmall ), Fade( p.dim, a ), weight );
			y += hText + Sc( 4 );
		}
		if ( line3.Length() )
			Text( F_SMALL, cx, y, Fade( bad ? p.bad : p.grey, a ), line3, false, 1 );
	}
};

DECLARE_HUDELEMENT( CHudRPMPrompt );

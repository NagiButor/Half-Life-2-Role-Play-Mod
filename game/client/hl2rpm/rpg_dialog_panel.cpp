//========= HL2RPM ============//
//
// RPG-интерфейс: окно диалога v2. Внизу экрана, как в Fallout: New Vegas, в эстетике
// интерфейсов Альянса: имя собеседника, реплика, нумерованные ответы с цветными проверками.
// Сообщения - "RPG_Dialog" (hl2rpm/rpg_dialog_shared.h).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_dialog_panel.h"
#include "hl2rpm/rpg_dialog_shared.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/rpg_ui_common.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_data.h"
#include "c_baseplayer.h"
#include "iclientmode.h"
#include "usermessages.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui/IInput.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

static ConVar cl_rpg_dialog_autoface( "cl_rpg_dialog_autoface", "1", FCVAR_ARCHIVE, "RPG dialog: turn the camera to the NPC when a conversation starts" );
static ConVar cl_rpg_dialog_autocontinue( "cl_rpg_dialog_autocontinue", "1", FCVAR_ARCHIVE, "RPG dialog: lines without answers continue by themselves after a pause" );
static ConVar cl_rpg_dialog_readspeed( "cl_rpg_dialog_readspeed", "16", FCVAR_ARCHIVE, "RPG dialog: reading speed (characters per second) for lines without a voice" );

static float AngleDiffDeg( float a, float b )
{
	float d = fmodf( a - b, 360.0f );
	if ( d > 180.0f ) d -= 360.0f;
	if ( d < -180.0f ) d += 360.0f;
	return d;
}

class CRPGDialogPanel : public EditablePanel
{
	DECLARE_CLASS_SIMPLE( CRPGDialogPanel, EditablePanel );
public:
	CRPGDialogPanel( VPANEL parent );

	void OnDialogMessage( bf_read &msg );
	bool IsOpen() const { return m_bOpen; }
	int GetNPCIndex() const { return m_iNPC; }
	void ForceClose() { Close( false ); }

	virtual void ApplySchemeSettings( IScheme *pScheme );
	virtual void Paint();
	virtual void PaintBackground() {}
	virtual void OnMousePressed( MouseCode code );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMouseWheeled( int delta );
	virtual void OnKeyCodePressed( KeyCode code );
	virtual void OnTick();

private:
	struct Option_t
	{
		int idx;
		int state;
		wchar_t text[512];
		wchar_t prefix[160];	// [Красноречие 50] [Лямбда]
		Color prefixColor;
		CUtlVector<Span_t> lines;
		int y0, y1;				// в координатах окна (без прокрутки)
	};

	void Open();
	void Close( bool bTellServer );
	void Relayout();
	void Choose( int optionPos );
	void ContinueNode();
	void BuildPrefix( Option_t &o, const char *pszLabel, const char *pszTag );
	void StartAutoFace();
	void UpdateAutoFace();

	bool m_bOpen;
	bool m_bShown;			// узел собран (SHOW)
	int m_iNPC;
	int m_iActor;
	char m_szEntity[128];
	wchar_t m_wszDialogSpeaker[128];
	wchar_t m_wszSpeaker[128];
	wchar_t m_wszLine[2048];
	CUtlString m_LineAccum;		// LINE-куски длинной реплики
	int m_iNode;
	int m_iAutoNext;
	bool m_bEnd;
	float m_flSoundDur;
	float m_flShownAt;
	float m_flContinueAt;
	bool m_bAutoFace;
	CUtlVector<Option_t> m_Options;
	CUtlVector<Span_t> m_LineSpans;

	// вёрстка
	int m_x0, m_y0, m_x1, m_y1;
	int m_iLineTop, m_iOptionsTop, m_iOptionsBottom, m_iScroll, m_iMaxScroll;
	int m_iHover;
	bool m_bLayoutDirty;

	// поворот камеры к собеседнику
	bool m_bFacing;
	float m_flFaceStart, m_flFaceDuration;
	QAngle m_angFaceFrom;
};

static CRPGDialogPanel *s_pRPGDialog = NULL;

bool RPGDialogPanel_IsOpen()
{
	return s_pRPGDialog && s_pRPGDialog->IsOpen();
}

int RPGDialogPanel_GetNPCIndex()
{
	return s_pRPGDialog && s_pRPGDialog->IsOpen() ? s_pRPGDialog->GetNPCIndex() : -1;
}

CRPGDialogPanel::CRPGDialogPanel( VPANEL parent ) : BaseClass( NULL, "RPGDialogPanel" )
{
	SetParent( parent );
	SetProportional( false );
	SetVisible( false );
	SetPaintBackgroundEnabled( false );
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );
	m_bOpen = false;
	m_bShown = false;
	m_iNPC = m_iActor = -1;
	m_szEntity[0] = 0;
	m_wszDialogSpeaker[0] = m_wszSpeaker[0] = m_wszLine[0] = 0;
	m_iNode = -1;
	m_iAutoNext = -1;
	m_bEnd = false;
	m_flSoundDur = -1.0f;
	m_flShownAt = m_flContinueAt = 0.0f;
	m_bAutoFace = false;
	m_iHover = -1;
	m_iScroll = m_iMaxScroll = 0;
	m_bLayoutDirty = true;
	m_bFacing = false;
	ivgui()->AddTickSignal( GetVPanel(), 0 );
}

void CRPGDialogPanel::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	LoadFonts( pScheme );
	int w, h;
	surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	m_bLayoutDirty = true;
}

void CRPGDialogPanel::Open()
{
	if ( m_bOpen )
		return;
	m_bOpen = true;
	int w, h;
	surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	SetVisible( true );
	MakePopup( false, false );
	SetZPos( 900 );
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );
	RequestFocus();
	MoveToFront();
	engine->ClientCmd_Unrestricted( "gameui_preventescapetoshow\n" );
	PlayUISound( "ui/buttonrollover.wav" );
}

void CRPGDialogPanel::Close( bool bTellServer )
{
	if ( !m_bOpen )
		return;
	m_bOpen = false;
	m_bShown = false;
	m_bFacing = false;
	SetVisible( false );
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( false );
	// экран интерфейса, открытый из разговора (анкета, торговля), держит Esc за собой
	if ( !RPGScreen_AnyOpen() )
		engine->ClientCmd_Unrestricted( "gameui_allowescapetoshow\n" );
	if ( bTellServer )
		engine->ClientCmd( "rpg_dialog_close\n" );
	m_Options.RemoveAll();
}

void CRPGDialogPanel::StartAutoFace()
{
	m_bFacing = false;
	if ( !m_bAutoFace || !cl_rpg_dialog_autoface.GetBool() || m_iNPC <= 0 )
		return;
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	C_BaseEntity *pEnt = cl_entitylist->GetEnt( m_iNPC );
	if ( !pLocal || !pEnt )
		return;
	engine->GetViewAngles( m_angFaceFrom );
	Vector dir = ( pEnt->WorldSpaceCenter() + Vector( 0, 0, 21 ) ) - pLocal->EyePosition();
	QAngle target;
	VectorAngles( dir, target );
	const float total = Max( fabsf( AngleDiffDeg( target.y, m_angFaceFrom.y ) ), fabsf( AngleDiffDeg( target.x, m_angFaceFrom.x ) ) );
	m_flFaceDuration = clamp( 0.35f + total / 180.0f * 0.55f, 0.35f, 0.9f );
	m_flFaceStart = gpGlobals->curtime;
	m_bFacing = true;
}

void CRPGDialogPanel::UpdateAutoFace()
{
	if ( !m_bFacing )
		return;
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	C_BaseEntity *pEnt = cl_entitylist->GetEnt( m_iNPC );
	if ( !pLocal || !pEnt )
	{
		m_bFacing = false;
		return;
	}
	Vector dir = ( pEnt->WorldSpaceCenter() + Vector( 0, 0, 21 ) ) - pLocal->EyePosition();
	QAngle target;
	VectorAngles( dir, target );
	float t = ( gpGlobals->curtime - m_flFaceStart ) / m_flFaceDuration;
	if ( t >= 1.0f )
	{
		t = 1.0f;
		m_bFacing = false;
	}
	const float s = t * t * ( 3.0f - 2.0f * t );
	QAngle ang;
	ang.x = AngleNormalize( m_angFaceFrom.x + AngleDiffDeg( target.x, m_angFaceFrom.x ) * s );
	ang.y = AngleNormalize( m_angFaceFrom.y + AngleDiffDeg( target.y, m_angFaceFrom.y ) * s );
	ang.z = 0.0f;
	engine->SetViewAngles( ang );
}

// [Красноречие 50], [Мощь 7], [Ложь 35], [Подкуп 50 кр.] + метки [Лямбда] [ГС] [Перк]
void CRPGDialogPanel::BuildPrefix( Option_t &o, const char *pszLabel, const char *pszTag )
{
	o.prefix[0] = 0;
	o.prefixColor = clrTextDim;
	wchar_t wPart[160];
	wPart[0] = 0;
	if ( pszLabel && pszLabel[0] )
	{
		char szKind[32] = { 0 }, szId[64] = { 0 };
		int value = 0;
		const char *p1 = strchr( pszLabel, ':' );
		const char *p2 = p1 ? strchr( p1 + 1, ':' ) : NULL;
		if ( p1 && p2 )
		{
			Q_strncpy( szKind, pszLabel, Min( (int)sizeof( szKind ), (int)( p1 - pszLabel ) + 1 ) );
			Q_strncpy( szId, p1 + 1, Min( (int)sizeof( szId ), (int)( p2 - p1 ) ) );
			value = atoi( p2 + 1 );
		}
		wchar_t wName[96], wValue[16];
		V_snwprintf( wValue, ARRAYSIZE( wValue ), L"%d", value );
		wName[0] = 0;
		if ( !Q_stricmp( szKind, "skill" ) )
			Localize( CFmtStr( "#RPG_Skill_%s", szId ), wName, sizeof( wName ) );
		else if ( !Q_stricmp( szKind, "attr" ) )
			Localize( CFmtStr( "#RPG_Attr_%s", szId ), wName, sizeof( wName ) );
		else if ( !Q_stricmp( szKind, "custom" ) )
			Localize( szId, wName, sizeof( wName ) );

		if ( wName[0] )
			V_snwprintf( wPart, ARRAYSIZE( wPart ), L"[%ls %ls]", wName, wValue );
		else
		{
			wchar_t wFmt[96];
			LocalizeFormat( wFmt, sizeof( wFmt ), CFmtStr( "#RPG_Check_%s", szKind ), CFmtStr( "%s %%s2", szKind ), L"", wValue );
			V_snwprintf( wPart, ARRAYSIZE( wPart ), L"[%ls]", wFmt );
		}
		V_wcsncpy( o.prefix, wPart, sizeof( o.prefix ) );
		o.prefixColor = o.state == RPG_DLGOPT_CHECK_PASS ? clrGood : ( o.state == RPG_DLGOPT_CHECK_FAIL ? clrBad : clrTextFaint );
	}
	if ( pszTag && pszTag[0] )
	{
		wchar_t wName[96];
		wName[0] = 0;
		const char *pColon = strchr( pszTag, ':' );
		const char *pszArg = pColon ? pColon + 1 : "";
		if ( !Q_strnicmp( pszTag, "faction:", 8 ) )
			Localize( CFmtStr( "#RPG_Faction_%s", pszArg ), wName, sizeof( wName ) );
		else if ( !Q_strnicmp( pszTag, "perk:", 5 ) )
			Localize( CFmtStr( "#RPG_Perk_%s", pszArg ), wName, sizeof( wName ) );
		else if ( !Q_strnicmp( pszTag, "trait:", 6 ) )
			Localize( CFmtStr( "#RPG_Trait_%s", pszArg ), wName, sizeof( wName ) );
		else if ( !Q_strnicmp( pszTag, "item:", 5 ) )
			Localize( CFmtStr( "#RPG_Item_%s", pszArg ), wName, sizeof( wName ) );
		else if ( !Q_stricmp( pszTag, "civil" ) )
			Localize( "#HL2RPM_UI_Dlg_TagCivil", wName, sizeof( wName ) );
		else if ( !Q_stricmp( pszTag, "resonance" ) )
			Localize( "#RPG_Resonance_Title", wName, sizeof( wName ) );
		else if ( !Q_stricmp( pszTag, "compassion" ) )
			Localize( "#HL2RPM_UI_Dlg_TagCompassion", wName, sizeof( wName ) );
		else if ( !Q_stricmp( pszTag, "cruelty" ) )
			Localize( "#HL2RPM_UI_Dlg_TagCruelty", wName, sizeof( wName ) );
		else
			Localize( pszTag, wName, sizeof( wName ) );
		if ( wName[0] )
		{
			wchar_t wTag[128];
			V_snwprintf( wTag, ARRAYSIZE( wTag ), L"[%ls]", wName );
			if ( o.prefix[0] )
			{
				V_wcsncat( o.prefix, L" ", sizeof( o.prefix ) );
				V_wcsncat( o.prefix, wTag, sizeof( o.prefix ) );
			}
			else
			{
				V_wcsncpy( o.prefix, wTag, sizeof( o.prefix ) );
				o.prefixColor = o.state == RPG_DLGOPT_UNMET ? clrTextFaint : clrAmber;
			}
		}
	}
}

void CRPGDialogPanel::OnDialogMessage( bf_read &msg )
{
	const int type = msg.ReadByte();
	char sz[512];
	switch ( type )
	{
	case RPG_DLGMSG_BEGIN:
	{
		m_iNPC = msg.ReadShort();
		msg.ReadString( m_szEntity, sizeof( m_szEntity ) );
		msg.ReadString( sz, sizeof( sz ) );
		Localize( sz, m_wszDialogSpeaker, sizeof( m_wszDialogSpeaker ) );
		m_bAutoFace = ( msg.ReadByte() & RPG_DLGF_AUTOFACE ) != 0;
		m_LineAccum.Clear();
		Open();
		StartAutoFace();
		break;
	}
	case RPG_DLGMSG_LINE:
		msg.ReadString( sz, sizeof( sz ) );
		m_LineAccum.Append( sz );
		break;
	case RPG_DLGMSG_NODE:
	{
		m_iNode = msg.ReadShort();
		msg.ReadString( sz, sizeof( sz ) );
		if ( sz[0] )
			Localize( sz, m_wszLine, sizeof( m_wszLine ) );
		else
			Localize( m_LineAccum.Get(), m_wszLine, sizeof( m_wszLine ) );
		m_LineAccum.Clear();
		msg.ReadString( sz, sizeof( sz ) );
		if ( sz[0] )
			Localize( sz, m_wszSpeaker, sizeof( m_wszSpeaker ) );
		else
			V_wcsncpy( m_wszSpeaker, m_wszDialogSpeaker, sizeof( m_wszSpeaker ) );
		m_iAutoNext = msg.ReadShort();
		m_bEnd = msg.ReadByte() != 0;
		m_flSoundDur = msg.ReadFloat();
		m_iActor = msg.ReadShort();
		m_Options.RemoveAll();
		m_bShown = false;
		m_iHover = -1;
		m_iScroll = 0;
		break;
	}
	case RPG_DLGMSG_OPTION:
	{
		Option_t &o = m_Options[m_Options.AddToTail()];
		o.idx = msg.ReadByte();
		o.state = msg.ReadByte();
		msg.ReadString( sz, sizeof( sz ) );
		Localize( sz, o.text, sizeof( o.text ) );
		char szLabel[128], szTag[128];
		msg.ReadString( szLabel, sizeof( szLabel ) );
		msg.ReadString( szTag, sizeof( szTag ) );
		BuildPrefix( o, szLabel, szTag );
		break;
	}
	case RPG_DLGMSG_SHOW:
	{
		m_bShown = true;
		m_flShownAt = gpGlobals->curtime;
		// реплика без ответов: дальше - по звуку или по времени чтения
		const int chars = V_wcslen( m_wszLine );
		const float flRead = clamp( 1.2f + chars / Max( 4.0f, cl_rpg_dialog_readspeed.GetFloat() ), 2.0f, 12.0f );
		m_flContinueAt = m_flShownAt + ( m_flSoundDur > 0.0f ? Max( m_flSoundDur + 0.35f, 1.0f ) : flRead );
		m_bLayoutDirty = true;
		break;
	}
	case RPG_DLGMSG_END:
		Close( false );
		break;
	}
}

void CRPGDialogPanel::Relayout()
{
	m_bLayoutDirty = false;
	int sw, sh;
	surface()->GetScreenSize( sw, sh );
	const int w = Min( (int)( sw * 0.64f ), S( 1120 ) );
	const int pad = S( 26 );
	const int textW = w - pad * 2;
	HFont fLine = GetFont( FONT_BODY );
	HFont fOpt = GetFont( FONT_BODY );
	HFont fHeader = GetFont( FONT_HEADER );
	const int lineH = FontTall( fLine ) + S( 3 );
	const int optH = FontTall( fOpt ) + S( 2 );

	WrapText( fLine, m_wszLine, textW - S( 14 ), m_LineSpans );
	const int headerH = FontTall( fHeader ) + S( 14 );
	const int lineBlock = Max( 1, m_LineSpans.Count() ) * lineH;

	// варианты: номер + префикс проверки + текст, перенос по ширине
	const int numW = TextWidth( GetFont( FONT_MONO ), L"9. " ) + S( 4 );
	int y = 0;
	for ( int i = 0; i < m_Options.Count(); ++i )
	{
		Option_t &o = m_Options[i];
		wchar_t wFull[700];
		if ( o.prefix[0] )
			V_snwprintf( wFull, ARRAYSIZE( wFull ), L"%ls %ls", o.prefix, o.text );
		else
			V_wcsncpy( wFull, o.text, sizeof( wFull ) );
		WrapText( fOpt, wFull, textW - numW - S( 10 ), o.lines );
		o.y0 = y;
		y += Max( 1, o.lines.Count() ) * optH + S( 8 );
		o.y1 = y;
	}
	const int optionsH = y;
	const int hintH = m_Options.Count() == 0 ? FontTall( GetFont( FONT_BODY_SMALL ) ) + S( 8 ) : 0;
	const int maxH = (int)( sh * 0.52f );
	const int fixedH = pad + headerH + lineBlock + S( 18 ) + hintH + pad;
	const int optionsVisible = Min( optionsH, Max( optH * 2, maxH - fixedH ) );
	const int h = fixedH + optionsVisible;

	m_x0 = ( sw - w ) / 2;
	m_x1 = m_x0 + w;
	m_y1 = sh - S( 44 );
	m_y0 = m_y1 - h;
	m_iLineTop = m_y0 + pad + headerH;
	m_iOptionsTop = m_iLineTop + lineBlock + S( 18 );
	m_iOptionsBottom = m_iOptionsTop + optionsVisible;
	m_iMaxScroll = Max( 0, optionsH - optionsVisible );
	m_iScroll = clamp( m_iScroll, 0, m_iMaxScroll );
}

void CRPGDialogPanel::Paint()
{
	if ( !m_bOpen )
		return;
	if ( m_bLayoutDirty )
		Relayout();

	const int pad = S( 26 );
	DrawWindow( m_x0, m_y0, m_x1, m_y1 );

	// имя собеседника - янтарём, слева голубая метка
	HFont fHeader = GetFont( FONT_HEADER );
	const int hx = m_x0 + pad;
	const int hy = m_y0 + pad - S( 4 );
	DrawRect( hx, hy + S( 4 ), hx + S( 4 ), hy + FontTall( fHeader ) - S( 2 ), clrAccent );
	DrawText( fHeader, hx + S( 14 ), hy, clrAmber, m_wszSpeaker[0] ? m_wszSpeaker : L"..." );
	// тонкая линия под заголовком
	DrawRect( hx, hy + FontTall( fHeader ) + S( 6 ), m_x1 - pad, hy + FontTall( fHeader ) + S( 7 ), clrLine );

	// реплика
	HFont fLine = GetFont( FONT_BODY );
	const int lineH = FontTall( fLine ) + S( 3 );
	int y = m_iLineTop;
	for ( int i = 0; i < m_LineSpans.Count(); ++i )
	{
		const Span_t &s = m_LineSpans[i];
		DrawText( fLine, hx + S( 14 ), y, clrText, m_wszLine + s.start, s.end - s.start );
		y += lineH;
	}

	if ( !m_bShown )
		return;

	// варианты ответа
	HFont fOpt = GetFont( FONT_BODY );
	HFont fNum = GetFont( FONT_MONO );
	const int optH = FontTall( fOpt ) + S( 2 );
	const int numW = TextWidth( fNum, L"9. " ) + S( 4 );
	for ( int i = 0; i < m_Options.Count(); ++i )
	{
		Option_t &o = m_Options[i];
		const int oy0 = m_iOptionsTop + o.y0 - m_iScroll;
		const int oy1 = m_iOptionsTop + o.y1 - m_iScroll - S( 8 );
		// без отсечения в ISurface: рисуем только целиком видимые варианты
		if ( oy0 < m_iOptionsTop - S( 2 ) || oy1 > m_iOptionsBottom + S( 2 ) )
			continue;
		const bool bDisabled = o.state == RPG_DLGOPT_UNMET || o.state == RPG_DLGOPT_DONE;
		const bool bHover = i == m_iHover && !bDisabled;
		if ( bHover )
		{
			DrawRect( hx - S( 6 ), oy0 - S( 3 ), m_x1 - pad + S( 6 ), oy1 + S( 3 ), clrHover );
			DrawRect( hx - S( 6 ), oy0 - S( 3 ), hx - S( 3 ), oy1 + S( 3 ), clrAccent );
		}
		wchar_t wNum[8];
		V_snwprintf( wNum, ARRAYSIZE( wNum ), L"%d.", i + 1 < 10 ? i + 1 : 0 );
		DrawText( fNum, hx + S( 2 ), oy0 + S( 1 ), bDisabled ? clrTextFaint : ( bHover ? clrAccent : clrAccentDim ), wNum );

		const Color clrBody = bDisabled ? clrTextFaint : ( o.state == RPG_DLGOPT_EXIT ? clrTextDim : ( bHover ? Color( 255, 255, 255, 255 ) : clrText ) );
		// префикс проверки другим цветом: он всегда в начале первой строки
		const int prefixLen = o.prefix[0] ? V_wcslen( o.prefix ) + 1 : 0;
		wchar_t wFull[700];
		if ( o.prefix[0] )
			V_snwprintf( wFull, ARRAYSIZE( wFull ), L"%ls %ls", o.prefix, o.text );
		else
			V_wcsncpy( wFull, o.text, sizeof( wFull ) );
		int ly = oy0;
		for ( int l = 0; l < o.lines.Count(); ++l )
		{
			const Span_t &s = o.lines[l];
			int x = hx + numW;
			int start = s.start;
			if ( start < prefixLen )
			{
				const int end = Min( s.end, prefixLen );
				DrawText( fOpt, x, ly, bDisabled ? clrTextFaint : o.prefixColor, wFull + start, end - start );
				x += TextWidth( fOpt, wFull + start, end - start );
				start = end;
			}
			if ( start < s.end )
				DrawText( fOpt, x, ly, clrBody, wFull + start, s.end - start );
			ly += optH;
		}
	}

	if ( m_iMaxScroll > 0 )
	{
		// полоса прокрутки
		const int bx = m_x1 - S( 10 );
		DrawRect( bx, m_iOptionsTop, bx + S( 3 ), m_iOptionsBottom, Color( 110, 190, 255, 40 ) );
		const float frac = (float)( m_iOptionsBottom - m_iOptionsTop ) / ( m_iOptionsBottom - m_iOptionsTop + m_iMaxScroll );
		const int barH = Max( S( 16 ), (int)( ( m_iOptionsBottom - m_iOptionsTop ) * frac ) );
		const int barY = m_iOptionsTop + (int)( ( m_iOptionsBottom - m_iOptionsTop - barH ) * ( (float)m_iScroll / m_iMaxScroll ) );
		DrawRect( bx, barY, bx + S( 3 ), barY + barH, clrAccent );
	}

	if ( m_Options.Count() == 0 )
	{
		// «дальше»: мигает, когда реплику можно пропустить
		HFont fSmall = GetFont( FONT_BODY_SMALL );
		wchar_t wHint[128];
		Localize( "#HL2RPM_UI_Dlg_Continue", wHint, sizeof( wHint ) );
		if ( !wHint[0] || wHint[0] == L'H' )
			V_wcsncpy( wHint, L"\x25B8 ...", sizeof( wHint ) );
		const float a = 0.55f + 0.45f * sinf( gpGlobals->curtime * 4.0f );
		Color c = clrAccent;
		c[3] = (int)( 255 * a );
		DrawTextRight( fSmall, m_x1 - pad, m_y1 - pad - FontTall( fSmall ) + S( 6 ), c, wHint );
	}
}

void CRPGDialogPanel::OnCursorMoved( int x, int y )
{
	m_iHover = -1;
	if ( !m_bShown || y < m_iOptionsTop || y > m_iOptionsBottom || x < m_x0 || x > m_x1 )
		return;
	for ( int i = 0; i < m_Options.Count(); ++i )
	{
		const int oy0 = m_iOptionsTop + m_Options[i].y0 - m_iScroll - S( 3 );
		const int oy1 = m_iOptionsTop + m_Options[i].y1 - m_iScroll - S( 4 );
		if ( y >= oy0 && y < oy1 )
		{
			if ( m_iHover != i && m_Options[i].state != RPG_DLGOPT_UNMET && m_Options[i].state != RPG_DLGOPT_DONE )
				PlayUISound( "ui/buttonrollover.wav" );
			m_iHover = i;
			return;
		}
	}
}

void CRPGDialogPanel::OnMouseWheeled( int delta )
{
	m_iScroll = clamp( m_iScroll - delta * S( 36 ), 0, m_iMaxScroll );
}

void CRPGDialogPanel::Choose( int pos )
{
	if ( !m_bShown || pos < 0 || pos >= m_Options.Count() )
		return;
	const Option_t &o = m_Options[pos];
	if ( o.state == RPG_DLGOPT_UNMET || o.state == RPG_DLGOPT_DONE )
	{
		PlayUISound( "buttons/button10.wav" );
		return;
	}
	PlayUISound( "ui/buttonclick.wav" );
	m_bShown = false;		// до ответа сервера ничего не нажимается дважды
	engine->ClientCmd( CFmtStr( "rpg_dialog_choose %d %d\n", m_iNode, o.idx ) );
}

void CRPGDialogPanel::ContinueNode()
{
	if ( !m_bShown || m_Options.Count() )
		return;
	m_bShown = false;
	engine->ClientCmd( CFmtStr( "rpg_dialog_continue %d\n", m_iNode ) );
}

void CRPGDialogPanel::OnMousePressed( MouseCode code )
{
	if ( code != MOUSE_LEFT || !m_bShown )
		return;
	if ( m_Options.Count() == 0 )
	{
		ContinueNode();
		return;
	}
	int x, y;
	input()->GetCursorPos( x, y );
	OnCursorMoved( x, y );
	if ( m_iHover >= 0 )
		Choose( m_iHover );
}

void CRPGDialogPanel::OnKeyCodePressed( KeyCode code )
{
	if ( !m_bOpen )
		return;
	if ( code >= KEY_1 && code <= KEY_9 )
	{
		Choose( code - KEY_1 );
		return;
	}
	if ( code >= KEY_PAD_1 && code <= KEY_PAD_9 )
	{
		Choose( code - KEY_PAD_1 );
		return;
	}
	if ( code == KEY_ESCAPE || code == KEY_0 || code == KEY_PAD_0 )
	{
		// выход: опция-выход, иначе закрыть
		for ( int i = 0; i < m_Options.Count(); ++i )
		{
			if ( m_Options[i].state == RPG_DLGOPT_EXIT )
			{
				Choose( i );
				return;
			}
		}
		if ( m_Options.Count() == 0 && code != KEY_ESCAPE )
		{
			ContinueNode();
			return;
		}
		Close( true );
		return;
	}
	if ( code == KEY_SPACE || code == KEY_ENTER || code == KEY_PAD_ENTER || code == KEY_E )
	{
		if ( m_Options.Count() == 0 )
			ContinueNode();
		else if ( m_iHover >= 0 )
			Choose( m_iHover );
		return;
	}
	if ( code == KEY_UP || code == KEY_W )
	{
		m_iHover = m_iHover <= 0 ? m_Options.Count() - 1 : m_iHover - 1;
		return;
	}
	if ( code == KEY_DOWN || code == KEY_S )
	{
		m_iHover = ( m_iHover + 1 ) % Max( 1, m_Options.Count() );
		return;
	}
	BaseClass::OnKeyCodePressed( code );
}

void CRPGDialogPanel::OnTick()
{
	if ( !m_bOpen )
		return;
	UpdateAutoFace();
	if ( m_bShown && m_Options.Count() == 0 && cl_rpg_dialog_autocontinue.GetBool() && gpGlobals->curtime >= m_flContinueAt )
		ContinueNode();
	// окно потеряло фокус (консоль, меню): закрыть разговор не надо, но клавиши должны работать снова
	if ( IsVisible() && !HasFocus() && !engine->Con_IsVisible() && !engine->IsPaused() && !RPGScreen_AnyOpen() )
		RequestFocus();
}

// ---------------------------------------------------------------------------
static void __MsgFunc_RPG_Dialog( bf_read &msg )
{
	if ( !s_pRPGDialog )
		return;
	s_pRPGDialog->OnDialogMessage( msg );
}

class CRPGDialogPanelSystem : public CAutoGameSystem
{
public:
	CRPGDialogPanelSystem() : CAutoGameSystem( "CRPGDialogPanelSystem" ) {}
	virtual bool Init()
	{
		usermessages->HookMessage( "RPG_Dialog", __MsgFunc_RPG_Dialog );
		return true;
	}
	virtual void LevelInitPostEntity()
	{
		if ( !s_pRPGDialog && g_pClientMode && g_pClientMode->GetViewport() )
			s_pRPGDialog = new CRPGDialogPanel( g_pClientMode->GetViewport()->GetVPanel() );
	}
	virtual void LevelShutdownPreEntity()
	{
		if ( s_pRPGDialog && s_pRPGDialog->IsOpen() )
			s_pRPGDialog->ForceClose();
	}
	virtual void Shutdown()
	{
		s_pRPGDialog = NULL;	// панель удаляет vgui вместе с вьюпортом
	}
};
static CRPGDialogPanelSystem g_RPGDialogPanelSystem;

//========= HL2RPM ============//
//
// RPG-интерфейс: «Анкета гражданина» - создание персонажа (Part I/02 §2, Part V/01 MQ-00 сцена 5).
// Открывается эффектом open_chargen (окно 4, Тамара). Раскладка теста профпригодности приходит
// флагами cg_<характеристика> (±) и cg_tags (специализации через «+»); всё можно перераспределить:
// сумма Λ.А.М.Б.Д.А. = 34 (1..10), ровно 3 специализации, до 2 черт.
// Подтверждение - rpg_chargen_commit; сервер продолжает разговор с Тамарой (rpg_dialog).
//
//=============================//

#include "cbase.h"
#include "hl2rpm/rpg_screen.h"
#include "hl2rpm/c_rpg_character.h"
#include "hl2rpm/rpg_data.h"
#include "hl2rpm/rpg_formulas.h"
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui/IInput.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/Controls.h>
#include "tier1/fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace RPGUI;

extern ConVar cl_rpg_ui_debug;

// ---------------------------------------------------------------------------
// Поле имени в стиле анкеты
// ---------------------------------------------------------------------------
class CRPGNameEntry : public TextEntry
{
	DECLARE_CLASS_SIMPLE( CRPGNameEntry, TextEntry );
public:
	CRPGNameEntry( Panel *pParent ) : BaseClass( pParent, "NameEntry" ) {}
	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		SetFont( GetFont( FONT_HEADER ) );
		SetFgColor( clrText );
		SetBgColor( Color( 20, 28, 36, 240 ) );
		SetBorder( NULL );
		SetSelectionBgColor( Color( 110, 190, 255, 120 ) );
		SetSelectionUnfocusedBgColor( Color( 110, 190, 255, 60 ) );
	}
	// TextEntry рисует фон и сам текст в PaintBackground - поверх только линия поля
	virtual void PaintBackground()
	{
		BaseClass::PaintBackground();
		int w, h;
		GetSize( w, h );
		DrawRect( 0, h - 2, w, h, HasFocus() ? clrAccent : clrAccentDim );
	}
};

enum
{
	BTN_ATTR_MINUS = 100,		// + индекс характеристики
	BTN_ATTR_PLUS = 120,
	BTN_SKILL = 200,			// + индекс навыка
	BTN_TRAIT = 300,			// + индекс черты
	BTN_CONFIRM = 10,
	BTN_RESET = 11,
	BTN_INFO_ATTR = 400,		// наведение на строку характеристики
};

class CRPGCharGenScreen : public CRPGScreen, public IRPGClientListener
{
	DECLARE_CLASS_SIMPLE( CRPGCharGenScreen, CRPGScreen );
public:
	CRPGCharGenScreen() : BaseClass( "RPGCharGen" )
	{
		m_pName = new CRPGNameEntry( this );
		m_pName->SetMaximumCharCount( 32 );
		m_pName->SetAllowNonAsciiCharacters( true );
		m_pName->SetVisible( false );
		RPGClient().AddListener( this );
		ResetToHints();
	}
	~CRPGCharGenScreen() { RPGClient().RemoveListener( this ); }

	virtual bool OnRPGOpenUI( const char *pszScreen, const char *pszParam, int iSource )
	{
		if ( Q_stricmp( pszScreen, "chargen" ) )
			return false;
		ResetToHints();
		Open();
		return true;
	}

protected:
	virtual bool CloseOnEscape() { return false; }	// анкету нельзя бросить: разговор ждёт её

	virtual void OnOpened()
	{
		m_pName->SetVisible( true );
		m_pName->RequestFocus();
	}
	virtual void OnClosed()
	{
		m_pName->SetVisible( false );
	}

	void ResetToHints()
	{
		RPGData().EnsureLoaded();
		const CRPGCharacterData &d = RPGClient().Data();
		for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
			m_iAttr[i] = clamp( RPG_ATTR_START + d.GetFlagInt( CFmtStr( "cg_%s", g_pszRPGAttrIds[i] ), 0 ), RPG_ATTR_MIN, RPG_ATTR_MAX );
		// подсказки теста могут дать сумму не 34: свободные очки (или долг) видны игроку
		m_Tags.RemoveAll();
		const char *pszTags = d.GetFlagString( "cg_tags" );
		if ( pszTags && pszTags[0] )
		{
			CUtlVector<char *> parts;
			V_SplitString( pszTags, "+", parts );
			for ( int i = 0; i < parts.Count() && m_Tags.Count() < RPG_TAG_COUNT; ++i )
			{
				const int s = RPGData().FindSkill( parts[i] );
				if ( s >= 0 )
					m_Tags.AddToTail( s );
			}
			parts.PurgeAndDeleteElements();
		}
		m_Traits.RemoveAll();
		m_iInfoKind = 0;
		m_iInfoIndex = -1;
	}

	int Sum() const
	{
		int s = 0;
		for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
			s += m_iAttr[i];
		return s;
	}

	// эффективная характеристика с учётом черт (TR-04: АВТ −1, TR-07: ЛОВ +1)
	int EffAttr( int a ) const
	{
		int v = m_iAttr[a];
		for ( int t = 0; t < m_Traits.Count(); ++t )
		{
			const RPGTraitDef_t &def = RPGData().GetTrait( m_Traits[t] );
			for ( int m = 0; m < def.mods.Count(); ++m )
			{
				if ( !def.mods[m].bMul && !Q_stricmp( def.mods[m].key.Get(), CFmtStr( "attr:%s", g_pszRPGAttrIds[a] ) ) )
					v += (int)def.mods[m].value;
			}
		}
		return clamp( v, RPG_ATTR_MIN, RPG_ATTR_MAX_EFFECTIVE );
	}

	int SkillValue( int s ) const
	{
		const RPGSkillDef_t &def = RPGData().GetSkill( s );
		int v = RPGFormulas::SkillBase( EffAttr( def.primary ), EffAttr( def.secondary ) );
		if ( m_Tags.Find( s ) != m_Tags.InvalidIndex() )
			v += RPG_TAG_BONUS;
		for ( int t = 0; t < m_Traits.Count(); ++t )
		{
			const RPGTraitDef_t &tr = RPGData().GetTrait( m_Traits[t] );
			for ( int m = 0; m < tr.mods.Count(); ++m )
			{
				if ( !tr.mods[m].bMul && !Q_stricmp( tr.mods[m].key.Get(), CFmtStr( "skill:%s", def.id.Get() ) ) )
					v += (int)tr.mods[m].value;
			}
		}
		return clamp( v, 0, RPG_SKILL_CAP );
	}

	bool GetNameUTF8( char *pszOut, int nOut )
	{
		wchar_t w[64];
		m_pName->GetText( w, sizeof( w ) );
		g_pVGuiLocalize->ConvertUnicodeToANSI( w, pszOut, nOut );
		// кавычки, ; и управляющие символы ломают командную строку
		int j = 0;
		for ( int i = 0; pszOut[i]; ++i )
		{
			const unsigned char c = (unsigned char)pszOut[i];
			if ( c == '"' || c == ';' || c == '\\' || c < 32 )
				continue;
			pszOut[j++] = pszOut[i];
		}
		pszOut[j] = '\0';
		// обрезать пробелы
		while ( j > 0 && pszOut[j - 1] == ' ' )
			pszOut[--j] = '\0';
		const char *p = pszOut;
		while ( *p == ' ' )
			++p;
		if ( p != pszOut )
			memmove( pszOut, p, strlen( p ) + 1 );
		return pszOut[0] != '\0';
	}

	bool IsValid( wchar_t *pReason, int nBytes )
	{
		char szName[128];
		if ( !GetNameUTF8( szName, sizeof( szName ) ) )
		{
			Localize( "#HL2RPM_UI_CG_NeedName", pReason, nBytes );
			return false;
		}
		const int free = RPG_CHARGEN_POINTS - Sum();
		if ( free != 0 )
		{
			wchar_t wN[16];
			V_snwprintf( wN, ARRAYSIZE( wN ), L"%d", free );
			LocalizeFormat( pReason, nBytes, free > 0 ? "#HL2RPM_UI_CG_NeedSpend" : "#HL2RPM_UI_CG_NeedRemove", "%s1", wN );
			return false;
		}
		if ( m_Tags.Count() != RPG_TAG_COUNT )
		{
			Localize( "#HL2RPM_UI_CG_NeedTags", pReason, nBytes );
			return false;
		}
		pReason[0] = 0;
		return true;
	}

	virtual void OnButton( int id, bool bRight )
	{
		if ( id >= BTN_ATTR_MINUS && id < BTN_ATTR_MINUS + RPG_ATTR_COUNT )
		{
			int &v = m_iAttr[id - BTN_ATTR_MINUS];
			if ( v > RPG_ATTR_MIN )
				--v;
			return;
		}
		if ( id >= BTN_ATTR_PLUS && id < BTN_ATTR_PLUS + RPG_ATTR_COUNT )
		{
			int &v = m_iAttr[id - BTN_ATTR_PLUS];
			if ( v < RPG_ATTR_MAX && Sum() < RPG_CHARGEN_POINTS )
				++v;
			return;
		}
		if ( id >= BTN_SKILL && id < BTN_SKILL + RPG_MAX_SKILLS )
		{
			const int s = id - BTN_SKILL;
			const int i = m_Tags.Find( s );
			if ( i != m_Tags.InvalidIndex() )
				m_Tags.Remove( i );
			else if ( m_Tags.Count() < RPG_TAG_COUNT )
				m_Tags.AddToTail( s );
			else
				PlayUISound( "buttons/button10.wav" );
			return;
		}
		if ( id >= BTN_TRAIT && id < BTN_TRAIT + 64 )
		{
			const int t = id - BTN_TRAIT;
			const int i = m_Traits.Find( t );
			if ( i != m_Traits.InvalidIndex() )
				m_Traits.Remove( i );
			else if ( m_Traits.Count() < RPG_MAX_TRAITS )
				m_Traits.AddToTail( t );
			else
				PlayUISound( "buttons/button10.wav" );
			return;
		}
		if ( id == BTN_RESET )
		{
			ResetToHints();
			return;
		}
		if ( id == BTN_CONFIRM )
		{
			wchar_t wReason[256];
			if ( !IsValid( wReason, sizeof( wReason ) ) )
			{
				if ( cl_rpg_ui_debug.GetBool() )
				{
					char szReason[256];
					g_pVGuiLocalize->ConvertUnicodeToANSI( wReason, szReason, sizeof( szReason ) );
					Msg( "[rpg ui] chargen: not valid: %s\n", szReason );
				}
				PlayUISound( "buttons/button10.wav" );
				return;
			}
			char szName[128];
			GetNameUTF8( szName, sizeof( szName ) );
			CUtlString cmd;
			cmd.Format( "rpg_chargen_commit \"%s\"", szName );
			for ( int i = 0; i < RPG_ATTR_COUNT; ++i )
				cmd.Append( CFmtStr( " %d", m_iAttr[i] ) );
			for ( int i = 0; i < m_Tags.Count(); ++i )
				cmd.Append( CFmtStr( " %s", RPGData().GetSkill( m_Tags[i] ).id.Get() ) );
			for ( int i = 0; i < m_Traits.Count(); ++i )
				cmd.Append( CFmtStr( " %s", RPGData().GetTrait( m_Traits[i] ).id.Get() ) );
			cmd.Append( "\n" );
			if ( cl_rpg_ui_debug.GetBool() )
				Msg( "[rpg ui] chargen: %s", cmd.Get() );
			engine->ClientCmd( cmd.Get() );
			PlayUISound( "buttons/button14.wav" );
			Close();
		}
	}

	virtual void PaintScreen()
	{
		DrawDimmer( 215 );
		const int sw = ScreenW(), sh = ScreenH();
		const int W = Min( S( 1300 ), sw - S( 40 ) );
		const int H = Min( S( 800 ), sh - S( 40 ) );
		const int x0 = ( sw - W ) / 2, y0 = ( sh - H ) / 2, x1 = x0 + W, y1 = y0 + H;
		DrawWindow( x0, y0, x1, y1 );
		const int pad = S( 28 );

		HFont fTitle = GetFont( FONT_TITLE ), fHead = GetFont( FONT_HEADER ), fBody = GetFont( FONT_BODY ),
			fSmall = GetFont( FONT_BODY_SMALL ), fMono = GetFont( FONT_MONO ), fMonoS = GetFont( FONT_MONO_SMALL );
		wchar_t w[512], w2[256];

		// ---- шапка ----
		Localize( "#HL2RPM_UI_CG_Title", w, sizeof( w ) );
		DrawText( fTitle, x0 + pad, y0 + pad - S( 6 ), clrAmber, w );
		Localize( "#HL2RPM_UI_CG_Subtitle", w, sizeof( w ) );
		DrawTextRight( fMonoS, x1 - pad, y0 + pad, clrTextDim, w );
		const int headLine = y0 + pad + FontTall( fTitle ) + S( 4 );
		DrawRect( x0 + pad, headLine, x1 - pad, headLine + 1, clrLine );

		// ---- имя ----
		const int nameY = headLine + S( 16 );
		Localize( "#HL2RPM_UI_CG_Name", w, sizeof( w ) );
		DrawText( fSmall, x0 + pad, nameY + S( 6 ), clrTextDim, w );
		const int nameX = x0 + pad + S( 170 );
		m_pName->SetBounds( nameX, nameY, S( 460 ), FontTall( fHead ) + S( 12 ) );
		Localize( "#HL2RPM_UI_CG_PassNo", w, sizeof( w ) );
		DrawTextRight( fMono, x1 - pad, nameY + S( 6 ), clrAccentDim, w );

		const int colTop = nameY + FontTall( fHead ) + S( 34 );
		const int infoTop = y1 - S( 190 );

		// ================= колонка 1: Λ.А.М.Б.Д.А. =================
		const int c1x = x0 + pad, c1w = S( 390 );
		Localize( "#RPG_Lambda_Title", w, sizeof( w ) );
		DrawText( fHead, c1x, colTop, clrAccent, w );
		const int free = RPG_CHARGEN_POINTS - Sum();
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", free );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_CG_Free", "Free: %s1", w2 );
		DrawTextRight( fMono, c1x + c1w, colTop + S( 2 ), free == 0 ? clrGood : ( free > 0 ? clrAmber : clrBad ), w );
		const int rowH = S( 60 );
		for ( int a = 0; a < RPG_ATTR_COUNT; ++a )
		{
			const int ry = colTop + S( 40 ) + a * rowH;
			const bool bRowHover = Button( BTN_INFO_ATTR + a, c1x, ry, c1x + c1w - S( 110 ), ry + rowH - S( 8 ) );
			if ( bRowHover )
			{
				m_iInfoKind = 1;
				m_iInfoIndex = a;
			}
			DrawRect( c1x, ry, c1x + c1w, ry + rowH - S( 8 ), bRowHover ? Color( 110, 190, 255, 24 ) : Color( 255, 255, 255, 6 ) );
			// буква в рамке
			Localize( CFmtStr( "#RPG_Attr_%s_Abbr", g_pszRPGAttrIds[a] ), w, sizeof( w ) );
			DrawFrame( c1x + S( 8 ), ry + S( 8 ), c1x + S( 58 ), ry + rowH - S( 16 ), clrAccentDim );
			DrawText( fMono, c1x + S( 8 ) + ( S( 50 ) - TextWidth( fMono, w ) ) / 2, ry + ( rowH - S( 8 ) - FontTall( fMono ) ) / 2, clrAccent, w );
			Localize( CFmtStr( "#RPG_Attr_%s", g_pszRPGAttrIds[a] ), w, sizeof( w ) );
			DrawText( fHead, c1x + S( 72 ), ry + ( rowH - S( 8 ) - FontTall( fHead ) ) / 2, clrText, w );
			// значение и кнопки
			const int vx = c1x + c1w - S( 104 );
			const int by = ry + ( rowH - S( 8 ) - S( 30 ) ) / 2;
			TextButton( BTN_ATTR_MINUS + a, vx, by, vx + S( 30 ), by + S( 30 ), L"\x2212", m_iAttr[a] > RPG_ATTR_MIN );
			V_snwprintf( w, ARRAYSIZE( w ), L"%d", m_iAttr[a] );
			HFont fBig = GetFont( FONT_TITLE );
			DrawText( fBig, vx + S( 52 ) - TextWidth( fBig, w ) / 2, ry + ( rowH - S( 8 ) - FontTall( fBig ) ) / 2, clrAmber, w );
			TextButton( BTN_ATTR_PLUS + a, vx + S( 74 ), by, vx + S( 104 ), by + S( 30 ), L"+", m_iAttr[a] < RPG_ATTR_MAX && free > 0 );
			if ( EffAttr( a ) != m_iAttr[a] )
			{
				V_snwprintf( w, ARRAYSIZE( w ), L"(%d)", EffAttr( a ) );
				DrawTextRight( fMonoS, vx - S( 8 ), ry + S( 6 ), clrTextDim, w );
			}
		}

		// ================= колонка 2: навыки и специализации =================
		const int c2x = c1x + c1w + S( 36 ), c2w = S( 400 );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", m_Tags.Count() );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_CG_Tags", "Specializations: %s1 / 3", w2 );
		DrawText( fHead, c2x, colTop, clrAccent, w );
		const int skillH = S( 33 );
		for ( int s = 0; s < RPGData().GetSkillCount() && s < RPG_MAX_SKILLS; ++s )
		{
			const int ry = colTop + S( 40 ) + s * skillH;
			if ( ry + skillH > infoTop - S( 8 ) )
				break;
			const bool bTag = m_Tags.Find( s ) != m_Tags.InvalidIndex();
			const bool bHover = Button( BTN_SKILL + s, c2x, ry, c2x + c2w, ry + skillH - S( 4 ) );
			if ( bHover )
			{
				m_iInfoKind = 2;
				m_iInfoIndex = s;
			}
			DrawRect( c2x, ry, c2x + c2w, ry + skillH - S( 4 ), bHover ? Color( 110, 190, 255, 30 ) : ( bTag ? Color( 255, 186, 82, 22 ) : Color( 255, 255, 255, 5 ) ) );
			// флажок
			const int cb = S( 16 );
			const int cy = ry + ( skillH - S( 4 ) - cb ) / 2;
			DrawFrame( c2x + S( 8 ), cy, c2x + S( 8 ) + cb, cy + cb, bTag ? clrAmber : clrAccentDim );
			if ( bTag )
				DrawRect( c2x + S( 11 ), cy + S( 3 ), c2x + S( 5 ) + cb, cy + cb - S( 3 ), clrAmber );
			const RPGSkillDef_t &def = RPGData().GetSkill( s );
			Localize( def.name.Get(), w, sizeof( w ) );
			DrawText( fBody, c2x + S( 34 ), ry + ( skillH - S( 4 ) - FontTall( fBody ) ) / 2, bTag ? clrAmber : clrText, w );
			V_snwprintf( w, ARRAYSIZE( w ), L"%d", SkillValue( s ) );
			DrawTextRight( fMono, c2x + c2w - S( 10 ), ry + ( skillH - S( 4 ) - FontTall( fMono ) ) / 2, bTag ? clrAmber : clrTextDim, w );
		}

		// ================= колонка 3: особенности (черты) =================
		const int c3x = c2x + c2w + S( 36 ), c3w = x1 - pad - c3x;
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", m_Traits.Count() );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_CG_Traits", "Peculiarities: %s1 / 2", w2 );
		DrawText( fHead, c3x, colTop, clrAccent, w );
		for ( int t = 0; t < RPGData().GetTraitCount(); ++t )
		{
			const int ry = colTop + S( 40 ) + t * skillH;
			if ( ry + skillH > infoTop - S( 8 ) )
				break;
			const bool bOn = m_Traits.Find( t ) != m_Traits.InvalidIndex();
			const bool bHover = Button( BTN_TRAIT + t, c3x, ry, c3x + c3w, ry + skillH - S( 4 ) );
			if ( bHover )
			{
				m_iInfoKind = 3;
				m_iInfoIndex = t;
			}
			DrawRect( c3x, ry, c3x + c3w, ry + skillH - S( 4 ), bHover ? Color( 110, 190, 255, 30 ) : ( bOn ? Color( 255, 186, 82, 22 ) : Color( 255, 255, 255, 5 ) ) );
			const int cb = S( 16 );
			const int cy = ry + ( skillH - S( 4 ) - cb ) / 2;
			DrawFrame( c3x + S( 8 ), cy, c3x + S( 8 ) + cb, cy + cb, bOn ? clrAmber : clrAccentDim );
			if ( bOn )
				DrawRect( c3x + S( 11 ), cy + S( 3 ), c3x + S( 5 ) + cb, cy + cb - S( 3 ), clrAmber );
			Localize( RPGData().GetTrait( t ).name.Get(), w, sizeof( w ) );
			DrawText( fBody, c3x + S( 34 ), ry + ( skillH - S( 4 ) - FontTall( fBody ) ) / 2, bOn ? clrAmber : clrText, w );
		}

		// ================= описание под курсором =================
		DrawRect( x0 + pad, infoTop, x1 - pad, infoTop + 1, clrLine );
		const int ix = x0 + pad, iw = W - pad * 2 - S( 330 );
		int iy = infoTop + S( 14 );
		wchar_t wTitle[128] = L"", wDesc[1024] = L"", wFlavor[512] = L"";
		if ( m_iInfoKind == 1 && m_iInfoIndex >= 0 )
		{
			Localize( CFmtStr( "#RPG_Attr_%s", g_pszRPGAttrIds[m_iInfoIndex] ), wTitle, sizeof( wTitle ) );
			Localize( CFmtStr( "#RPG_Attr_%s_Desc", g_pszRPGAttrIds[m_iInfoIndex] ), wDesc, sizeof( wDesc ) );
		}
		else if ( m_iInfoKind == 2 && m_iInfoIndex >= 0 && m_iInfoIndex < RPGData().GetSkillCount() )
		{
			const RPGSkillDef_t &def = RPGData().GetSkill( m_iInfoIndex );
			Localize( def.name.Get(), wTitle, sizeof( wTitle ) );
			Localize( def.desc.Get(), wDesc, sizeof( wDesc ) );
		}
		else if ( m_iInfoKind == 3 && m_iInfoIndex >= 0 && m_iInfoIndex < RPGData().GetTraitCount() )
		{
			const RPGTraitDef_t &def = RPGData().GetTrait( m_iInfoIndex );
			Localize( def.name.Get(), wTitle, sizeof( wTitle ) );
			Localize( def.desc.Get(), wDesc, sizeof( wDesc ) );
			// реплика Тамары на эту черту (MQ-00, узел 7)
			const wchar_t *pwTamara = g_pVGuiLocalize->Find( CFmtStr( "RPG_Trait_%s_Tamara", def.id.Get() ) );
			if ( pwTamara )
				LocalizeFormat( wFlavor, sizeof( wFlavor ), "#HL2RPM_UI_CG_TamaraSays", "%s1", pwTamara );
		}
		else
		{
			Localize( "#HL2RPM_UI_CG_Hint", wDesc, sizeof( wDesc ) );
		}
		if ( wTitle[0] )
		{
			DrawText( fHead, ix, iy, clrAmber, wTitle );
			iy += FontTall( fHead ) + S( 4 );
		}
		CUtlVector<Span_t> lines;
		WrapText( fBody, wDesc, iw, lines );
		for ( int i = 0; i < lines.Count() && i < 3; ++i )
		{
			DrawText( fBody, ix, iy, clrText, wDesc + lines[i].start, lines[i].end - lines[i].start );
			iy += FontTall( fBody ) + S( 2 );
		}
		if ( wFlavor[0] )
		{
			WrapText( fSmall, wFlavor, iw, lines );
			for ( int i = 0; i < lines.Count() && i < 2; ++i )
			{
				DrawText( fSmall, ix, iy + S( 2 ), clrAccentDim, wFlavor + lines[i].start, lines[i].end - lines[i].start );
				iy += FontTall( fSmall ) + S( 2 );
			}
		}

		// ---- производные показатели и кнопки ----
		const int rx = x1 - pad - S( 300 );
		const int maxHp = RPGFormulas::MaxHealth( EffAttr( RPG_ATTR_SPIRIT ), 1, 0 );
		const float carry = RPGFormulas::CarryWeight( EffAttr( RPG_ATTR_MIGHT ), 0.0f );
		const int sp = RPGFormulas::SkillPointsPerLevel( EffAttr( RPG_ATTR_ANALYTICS ) );
		wchar_t wHp[16], wCarry[16];
		V_snwprintf( wHp, ARRAYSIZE( wHp ), L"%d", maxHp );
		V_snwprintf( wCarry, ARRAYSIZE( wCarry ), L"%.0f", carry );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_CG_Derived1", "Health %s1   Carry %s2 kg", wHp, wCarry );
		DrawText( fMonoS, rx, infoTop + S( 16 ), clrTextDim, w );
		V_snwprintf( w2, ARRAYSIZE( w2 ), L"%d", sp );
		LocalizeFormat( w, sizeof( w ), "#HL2RPM_UI_CG_Derived2", "Skill points per level %s1", w2 );
		DrawText( fMonoS, rx, infoTop + S( 16 ) + FontTall( fMonoS ) + S( 4 ), clrTextDim, w );

		wchar_t wReason[256];
		const bool bValid = IsValid( wReason, sizeof( wReason ) );
		const int byy = y1 - pad - S( 44 );
		Localize( "#HL2RPM_UI_CG_Reset", w, sizeof( w ) );
		TextButton( BTN_RESET, rx, byy, rx + S( 110 ), byy + S( 44 ), w );
		Localize( "#HL2RPM_UI_CG_Confirm", w, sizeof( w ) );
		TextButton( BTN_CONFIRM, rx + S( 122 ), byy, x1 - pad, byy + S( 44 ), w, bValid, true );
		if ( !bValid )
			DrawTextRight( fSmall, x1 - pad, byy - FontTall( fSmall ) - S( 6 ), clrBad, wReason );
	}

private:
	CRPGNameEntry *m_pName;
	int m_iAttr[RPG_ATTR_COUNT];
	CUtlVector<int> m_Tags;
	CUtlVector<int> m_Traits;
	int m_iInfoKind;		// 0 подсказка, 1 характеристика, 2 навык, 3 черта
	int m_iInfoIndex;
};

// создаётся вместе с клиентом
class CRPGCharGenSystem : public CAutoGameSystem
{
public:
	CRPGCharGenSystem() : CAutoGameSystem( "CRPGCharGenSystem" ) {}
	virtual void LevelInitPostEntity()
	{
		if ( !m_pScreen )
			m_pScreen = new CRPGCharGenScreen();
	}
	virtual void LevelShutdownPreEntity()
	{
		if ( m_pScreen && m_pScreen->IsOpen() )
			m_pScreen->Close();
	}
	CRPGCharGenScreen *m_pScreen = NULL;
};
static CRPGCharGenSystem g_RPGCharGenSystem;

CON_COMMAND_F( cl_rpg_chargen_open, "RPG: open the citizen card screen (test)", FCVAR_CLIENTDLL | FCVAR_CHEAT )
{
	if ( g_RPGCharGenSystem.m_pScreen )
		g_RPGCharGenSystem.m_pScreen->OnRPGOpenUI( "chargen", "", -1 );
}

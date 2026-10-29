//========= HL2RPM ============//
//
// RPG-интерфейс: основа полноэкранных экранов (Карточка гражданина, новый уровень, браслет,
// чтение записок, торговля). Всё рисуется вручную (rpg_ui_common), кнопки - прямоугольники
// с наведением; экран забирает мышь и клавиатуру, Esc закрывает его (а не меню игры).
//
//=============================//

#ifndef RPG_SCREEN_H
#define RPG_SCREEN_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/EditablePanel.h>
#include "hl2rpm/rpg_ui_common.h"

class CRPGScreen : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CRPGScreen, vgui::EditablePanel );
public:
	CRPGScreen( const char *pszName );

	virtual void Open();
	virtual void Close();
	bool IsOpen() const { return m_bOpen; }

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void Paint();
	virtual void PaintBackground() {}
	virtual void OnMousePressed( vgui::MouseCode code );
	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMouseWheeled( int delta );
	virtual void OnKeyCodePressed( vgui::KeyCode code );
	virtual void OnTick();

protected:
	// ---- для наследников ----
	virtual void PaintScreen() = 0;					// всё содержимое; кнопки регистрировать через Button()
	virtual void OnButton( int id, bool bRight ) {}
	virtual void OnWheel( int delta ) {}
	virtual void OnKey( vgui::KeyCode code ) {}
	virtual void OnOpened() {}
	virtual void OnClosed() {}
	virtual bool CloseOnEscape() { return true; }
	virtual void Think() {}

	// кнопка: зарегистрировать прямоугольник (в этом кадре); true - под курсором
	bool Button( int id, int x0, int y0, int x1, int y1, bool bEnabled = true );
	bool IsHover( int id ) const { return m_iHover == id; }
	int HoverId() const { return m_iHover; }
	// готовая кнопка с текстом: рамка, подсветка
	bool TextButton( int id, int x0, int y0, int x1, int y1, const wchar_t *pText, bool bEnabled = true, bool bPrimary = false );

	// затемнение мира под экраном
	void DrawDimmer( int alpha = 200 );

	int ScreenW() const { return m_iScreenW; }
	int ScreenH() const { return m_iScreenH; }
	int MouseX() const { return m_iMouseX; }
	int MouseY() const { return m_iMouseY; }
	bool MouseIn( int x0, int y0, int x1, int y1 ) const { return m_iMouseX >= x0 && m_iMouseX < x1 && m_iMouseY >= y0 && m_iMouseY < y1; }

private:
	struct Btn_t { int id, x0, y0, x1, y1; bool bEnabled; };
	CUtlVector<Btn_t> m_Buttons;
	int m_iHover;
	int m_iPressed;
	int m_iMouseX, m_iMouseY;
	bool m_bOpen;
	int m_iScreenW, m_iScreenH;
	static int s_nOpen;
	friend bool RPGScreen_AnyOpen();
};

// открыт ли хоть один полноэкранный RPG-экран (диалог не забирает у него фокус и Esc)
bool RPGScreen_AnyOpen();

#endif // RPG_SCREEN_H

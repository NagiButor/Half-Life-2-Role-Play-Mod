#ifndef DIALOG_UI_H
#define DIALOG_UI_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui/VGUI.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/RichText.h>
#include "dialog_data.h"

using namespace vgui;

#define MAX_RESPONSE_BUTTONS 6

// Кнопка для ответа
class CDialogResponseButton : public Button
{
public:
	DECLARE_CLASS_SIMPLE(CDialogResponseButton, Button);

	CDialogResponseButton(Panel *pParent, const char *pszPanelName, const DialogResponse_t *pResponse);
	virtual ~CDialogResponseButton();

	virtual void OnCommand(const char *pCommand);
	virtual void ApplySchemeSettings(IScheme *pScheme);
	virtual void Paint();

	DialogResponse_t *GetResponse() { return m_pResponse; }

private:
	DialogResponse_t *m_pResponse;
	Color m_BGColor;
	Color m_BGColorHover;
	Color m_TextColor;
};

// Главная панель диалога
class CDialogPanel : public Frame
{
public:
	DECLARE_CLASS_SIMPLE(CDialogPanel, Frame);

	CDialogPanel(Panel *pParent, const char *pszPanelName);
	virtual ~CDialogPanel();

	virtual void ApplySchemeSettings(IScheme *pScheme);
	virtual void OnCommand(const char *pCommand);
	virtual void PerformLayout();
	virtual void Paint();

	// Показать диалог
	void ShowDialog(int iNPCID, int iStartNodeID);

	// Скрыть диалог
	void HideDialog();

	// Проверить, активен ли диалог
	bool IsDialogActive() const { return m_bDialogActive; }

protected:
	virtual void OnClose();

private:
	// Обновить отображение текущего узла
	void UpdateDialog();

	// Очистить предыдущие кнопки ответов
	void ClearResponseButtons();

	// Создать кнопку ответа
	void CreateResponseButton(const DialogResponse_t *pResponse, int iIndex);

	// Обработать выбранный ответ
	void OnResponseSelected(const DialogResponse_t *pResponse);

	// Члены класса
	Label *m_pNPCNameLabel;
	RichText *m_pNPCText;
	CDialogResponseButton *m_pResponseButtons[MAX_RESPONSE_BUTTONS];
	RichText *m_pPlayerText;

	int m_iCurrentNPCID;
	int m_iCurrentNodeID;
	bool m_bDialogActive;

	Color m_BgColor;
	Color m_TextColor;
	Color m_NPCTextColor;
};

// Глобальный указатель на панель диалога
extern CDialogPanel *g_pDialogPanel;

#endif // DIALOG_UI_H

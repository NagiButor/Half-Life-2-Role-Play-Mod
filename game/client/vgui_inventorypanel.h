
#ifndef VGUI_INVENTORYPANEL_H
#define VGUI_INVENTORYPANEL_H

#include "utlvector.h"
#include "utlstring.h"
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Panel.h>
#include <vgui/IScheme.h>

namespace vgui { class Panel; class Label; class Button; class IScheme; class TextEntry; }

struct QuestClientEntry
{
	CUtlString id;
	CUtlString baseTitle;
	CUtlString stageTitle;
	CUtlString stageDescription;
	int state;
	int stage;
	int progress;
};

class CVGuiInventoryPanel : public vgui::Frame
{
public:
	typedef vgui::Frame BaseClass;
	CVGuiInventoryPanel(vgui::Panel *parent);
	virtual void ApplySchemeSettings(vgui::IScheme *scheme);

	void BeginInventory();
	void SetItems(const CUtlVector<CUtlString> &items);
	void SetWeight(float curWeight, float maxWeight, bool bOverencumbered);
	void SetQuests(const CUtlVector<QuestClientEntry> &quests);
	void EndInventory();

	// Drag/stack support
	void StartDrag(int index);
	void EndDrag();

	virtual void PaintBackground();
	virtual void Paint() OVERRIDE;
	virtual void OnCommand(const char *command);
	virtual void OnTick() OVERRIDE;
	virtual void OnMousePressed(vgui::MouseCode code) OVERRIDE;
	virtual void OnMouseWheeled(int delta) OVERRIDE;
	virtual void OnKeyCodePressed(vgui::KeyCode code) OVERRIDE;
	virtual void OnKeyCodeTyped(vgui::KeyCode code) OVERRIDE;

	bool IsItemSelected(int idx);
	bool IsUseActionSelected();
	bool IsDropActionSelected();
	bool AreActionsVisibleForItem(int idx);
	int GetActiveTab() const;
	bool IsQuestRowIndex(int rowIndex) const;
	const QuestClientEntry *GetQuestEntryForRowIndex(int rowIndex) const;
	void OpenInventoryTab();

private:
	void ShowActions(int idx);
	void HideActions();
	void ShowSplitPopup(int idx);
	void HideSplitPopup();
	void SelectItem(int idx);
	void SelectQuest(int idx);
	void AdjustScrollToSelection();
	void SetActionSelection(int action);
	void PerformSelectedAction();
	void ScheduleModalRelease(float delay);
	void UpdateArmedState();

	vgui::Label *m_pTitleLabel;
	vgui::Label *m_pTimeLabel;
	vgui::Button *m_pTabInventoryBtn;
	vgui::Button *m_pTabQuestsBtn;
	vgui::Button *m_pSortBtn;
	vgui::Button *m_pUnequipSuitBtn;
	vgui::Label *m_pWeightLabel;
	vgui::Button *m_pActionUseBtn;
	vgui::Button *m_pActionDropBtn;
	CUtlVector<vgui::Button*> m_Options;
	CUtlVector<CUtlString> m_Items;
	CUtlVector<QuestClientEntry> m_Quests;
	CUtlVector<int> m_QuestRowToQuestIndex;
	vgui::Panel *m_pListPanel;
	vgui::Panel *m_pQuestDetailPanel;
	vgui::Label *m_pQuestDescLabel;
	vgui::Button *m_pQuestShowOnMapBtn;
	vgui::Panel *m_pSplitPanel;
	vgui::Label *m_pSplitTitleLabel;
	vgui::TextEntry *m_pSplitAmountEntry;
	vgui::Button *m_pSplitConfirmBtn;
	vgui::Button *m_pSplitCancelBtn;
	int m_nSplitIndex;
	int m_nSplitMax;
	bool m_bSplitVisible;
	int m_nSelectedQuestIndex;
	int m_nActiveTab;
	int m_nActionIndex;
	int m_nDragIndex;
	int m_nSelectedIndex; // index selected for wheel/keyboard navigation
	int m_nContentOffset; // vertical offset (pixels) for scrolling content
	int m_nSelectedAction; // 0=use, 1=drop
	bool m_bSelectionVisible;
	bool m_bStackInProgress;
	bool m_bPendingModalRelease;
	bool m_bShowCompletedQuests;
	float m_flModalReleaseTime;
	float m_flCurWeight;
	float m_flMaxWeight;
	bool m_bOverencumbered;
};

CVGuiInventoryPanel *GetGlobalInventoryPanel();

#endif // VGUI_INVENTORYPANEL_H

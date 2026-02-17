#ifndef VGUI_LOOTPANEL_H
#define VGUI_LOOTPANEL_H

#include "utlvector.h"
#include "utlstring.h"
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Panel.h>

namespace vgui { class Panel; class Label; class Button; class IScheme; }

class CVGuiLootPanel : public vgui::Frame
{
public:
    typedef vgui::Frame BaseClass;
    CVGuiLootPanel(vgui::Panel *parent);
    virtual void ApplySchemeSettings(vgui::IScheme *scheme) OVERRIDE;

    void BeginLoot();
    void SetContext(const char *displayName, const char *soundOpen, const char *soundClose);
    void SetItems(const CUtlVector<CUtlString> &leftItems, const CUtlVector<CUtlString> &rightItems);
    void EndLoot();

    bool IsLeftItemSelected(int idx);
    bool IsRightItemSelected(int idx);
    bool IsLeftPrimaryActionSelected();
    bool IsLeftSecondaryActionSelected();
    bool IsRightPrimaryActionSelected();
    bool IsRightSecondaryActionSelected();
    bool IsPutAllSelected();
    bool IsTakeAllSelected();
    bool AreActionsVisibleForItem(int column, int idx);
    bool AreActionsVisibleForColumn(int column);
    bool AreAnyActionsVisible();
    void DebugDump();
    void DebugPaintDump(bool force);

    virtual void PaintBackground() OVERRIDE;
    virtual void OnCommand(const char *command) OVERRIDE;
    virtual void OnMousePressed(vgui::MouseCode code) OVERRIDE;
    virtual void OnMouseWheeled(int delta) OVERRIDE;
    virtual void OnKeyCodePressed(vgui::KeyCode code) OVERRIDE;
    virtual void OnKeyCodeTyped(vgui::KeyCode code) OVERRIDE;
    virtual void OnTick() OVERRIDE;

private:
    void DebugAuto(const char *tag);
    void SelectItem(int column, int idx);
    void SetActionSelection(int action);
    void ShowActions(int column, int idx);
    void PerformSelectedAction();
    void ShowLeftActions(int idx);
    void ShowRightActions(int idx);
    void HideLeftActions();
    void HideRightActions();
    void HideActionButtons();
    void ClampOffsets();
    void RepositionLeftColumn();
    void RepositionRightColumn();
    void EnsureLeftSelectionVisible();
    void EnsureRightSelectionVisible();
    void ScheduleModalRelease(float delay);
    void RepaintSelection();
    void UpdateArmedState();

    vgui::Label *m_pTitleLabel;
    vgui::Label *m_pLeftLabel;
    vgui::Label *m_pRightLabel;
    vgui::Panel *m_pLeftListPanel;
    vgui::Panel *m_pRightListPanel;

    vgui::Button *m_pLeftActionPrimary;
    vgui::Button *m_pLeftActionSecondary;
    vgui::Button *m_pRightActionPrimary;
    vgui::Button *m_pRightActionSecondary;
    vgui::Button *m_pPutAll;
    vgui::Button *m_pTakeAll;

    int m_nSelectedLeftIndex;
    int m_nSelectedRightIndex;
    int m_nFocusedColumn;
    int m_nActionColumn;
    int m_nActionIndex;
    int m_nSelectedAction;
    int m_nLeftContentOffset;
    int m_nRightContentOffset;
    int m_nFooterSelection;
    bool m_bSelectionVisible;
    bool m_bPendingModalRelease;
    float m_flModalReleaseTime;

    CUtlVector<vgui::Button*> m_LeftButtons;
    CUtlVector<vgui::Button*> m_RightButtons;
    CUtlVector<CUtlString> m_LeftItems;
    CUtlVector<CUtlString> m_RightItems;
    CUtlString m_DisplayName;
    CUtlString m_SoundOpen;
    CUtlString m_SoundClose;
};

CVGuiLootPanel *GetGlobalLootPanel();

#endif // VGUI_LOOTPANEL_H

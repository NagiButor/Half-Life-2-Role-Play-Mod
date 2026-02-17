#pragma once

#include "cbase.h"
#include <vgui/VGUI.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Label.h>
#include <vgui/ISurface.h>

namespace vgui { class Frame; }

class C_SceneEntity;
class C_BaseFlex;

class CVGuiDialogPanel : public vgui::Frame
{
    DECLARE_CLASS_SIMPLE(CVGuiDialogPanel, vgui::Frame);
public:
    CVGuiDialogPanel(vgui::Panel *parent);
    void BeginDialog(const char *entityName, int entIndex = -1);
    void SetCurrentNodeId(int nodeId);
    void SetAutoAdvance(int autoNextId, bool autoClose);
    void SetPayloadFlags(bool autoFaceNPC, bool pendingSpoil);
    void SetSpeakerName(const char *name);
    void SetLine(const char *line);
    // choreo: scene name (.vcd), extraSequence: optional sequence/gesture to play locally on actor
    void SetChoreo(const char *choreo, const char *extraSequence = NULL);
    void PlaySound(const char *soundName);
    void AddOption(int id, const char *text, int nextId, int flags);
    void EndDialog();
    // Set a delay (seconds) after which option buttons become active/visible
    void SetOptionDelay(float seconds);

protected:
    virtual void OnCommand(const char *command) OVERRIDE;
    virtual void OnMousePressed(vgui::MouseCode code) OVERRIDE;
    virtual void OnMouseWheeled(int delta) OVERRIDE;
    virtual void PaintBackground() OVERRIDE;
    virtual void Paint() OVERRIDE;
    virtual void OnTick() OVERRIDE;

private:
    vgui::Label *m_pSpeakerLabel;
    vgui::Label *m_pLineLabel;
    CUtlVector<vgui::Button*> m_Options;
    CUtlString m_entityName;
    int m_entityEntIndex;
    int m_currentNodeId;
    int m_autoNextId;
    bool m_autoClose;
    bool m_autoFaceNPC;
    bool m_pendingSpoil;
    bool m_autoFaceActive;
    float m_autoFaceEndsAt;
    float m_autoFaceLastFrameTime;
    QAngle m_autoFaceStartAngles;
    QAngle m_autoFaceTargetAngles;
    float m_autoFaceStartTime;
    float m_autoFaceDuration;
    bool m_autoFaceConsumed;
    bool m_bPaintCalled;
    CHandle<C_SceneEntity> m_hSceneEntity;
    // Time in seconds a delay requested by user/server before showing options
    float m_optionDelaySeconds;
    // Absolute time (Plat_FloatTime) when options should be shown
    double m_optionAvailableAt;
    // If true, wait for the client-side scene entity to finish before showing options
    bool m_waitForSceneEnd;
    // Internal tracking whether scene was observed playing (to detect end)
    bool m_sceneWasPlaying;
    int m_nOptionsOffset;
};

CVGuiDialogPanel *GetGlobalDialogPanel();

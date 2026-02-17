#include "cbase.h"
#include "clientmode.h"
#include "vgui_dialogpanel.h"
#include <vgui_controls/Panel.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui/IInput.h>
#include <vgui/IVGui.h>
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <KeyValues.h>
#include "c_sceneentity.h"
#include "cliententitylist.h"
#include "c_baseflex.h"

using namespace vgui;

static CVGuiDialogPanel *s_pDialogPanel = nullptr;

static float AngleNormalize180(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static float AngleDiffDeg(float dest, float src)
{
    return AngleNormalize180(dest - src);
}

static float ApproachAngleDeg(float target, float value, float speed)
{
    float delta = AngleDiffDeg(target, value);
    if (delta > speed) delta = speed;
    if (delta < -speed) delta = -speed;
    return AngleNormalize180(value + delta);
}

static float SmoothStep01(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Custom dialog button that draws a pale red background when armed/pressed
class CDialogOptionButton : public Button
{
public:
    CDialogOptionButton(Panel *parent, const char *panelName, const char *text)
        : Button(parent, panelName, text)
    {
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h;
        GetSize(w, h);
        // pale reddish highlight when pressed/armed (semi-transparent)
        if (IsDepressed() || IsArmed())
        {
            surface()->DrawSetColor(180, 50, 40, 200);
            surface()->DrawFilledRect(0, 0, w, h);
        }
        else
        {
            // keep transparent background otherwise
        }
        // Draw gold/yellow outline around option buttons
        surface()->DrawSetColor(255, 200, 30, 255);
        surface()->DrawOutlinedRect(0, 0, w-1, h-1);
    }

    // Ensure the button text uses the same warm gold color as the dialog text
    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255, 200, 30, 255));
        Button::Paint();
    }
};

// Custom label that draws a solid black background and gold text to match HL2 DM style
class CDialogLineLabel : public Label
{
public:
    CDialogLineLabel(Panel *parent, const char *panelName, const char *text)
        : Label(parent, panelName, text)
    {
        SetPaintBackgroundEnabled(false);
    }

    virtual void Paint() OVERRIDE
    {
        int w, h;
        GetSize(w, h);
        // draw solid black background for the text area
        surface()->DrawSetColor(0, 0, 0, 255);
        surface()->DrawFilledRect(0, 0, w, h);

        // ensure the label draws only the text (disable its own background drawing)
        SetPaintBackgroundEnabled(false);
        SetFgColor(Color(255, 196, 37, 255));
        Label::Paint();
    }
};

class CDialogSpeakerLabel : public Label
{
public:
    CDialogSpeakerLabel(Panel *parent, const char *panelName, const char *text)
        : Label(parent, panelName, text)
    {
        SetPaintBackgroundEnabled(false);
        SetContentAlignment(Label::a_center);
        SetFgColor(Color(255, 200, 30, 255));
    }

    virtual void Paint() OVERRIDE
    {
        wchar_t wtext[256];
        wtext[0] = L'\0';
        GetText(wtext, ARRAYSIZE(wtext));
        if (!wtext[0])
            return;

        HFont font = GetFont();
        int textW = 0, textH = 0;
        surface()->GetTextSize(font, wtext, textW, textH);

        int w, h;
        GetSize(w, h);

        surface()->DrawSetTextFont(font);
        int x = (w - textW) / 2;
        int y = (h - textH) / 2;

        surface()->DrawSetTextColor(0, 0, 0, 180);
        surface()->DrawSetTextPos(x - 1, y);
        surface()->DrawPrintText(wtext, wcslen(wtext));
        surface()->DrawSetTextPos(x + 1, y);
        surface()->DrawPrintText(wtext, wcslen(wtext));
        surface()->DrawSetTextPos(x, y - 1);
        surface()->DrawPrintText(wtext, wcslen(wtext));
        surface()->DrawSetTextPos(x, y + 1);
        surface()->DrawPrintText(wtext, wcslen(wtext));

        surface()->DrawSetTextColor(255, 200, 30, 255);
        surface()->DrawSetTextPos(x, y);
        surface()->DrawPrintText(wtext, wcslen(wtext));
    }
};

CVGuiDialogPanel::CVGuiDialogPanel(vgui::Panel *parent) : BaseClass(parent, "DialogPanel")
{
    SetVisible(false);
    SetProportional(false);
    m_bPaintCalled = false;
    m_entityEntIndex = -1;
    m_currentNodeId = -1;
    m_autoNextId = -1;
    m_autoClose = false;
    m_autoFaceNPC = false;
    m_pendingSpoil = false;
    m_autoFaceActive = false;
    m_autoFaceEndsAt = 0.0f;
    m_autoFaceLastFrameTime = -1.0f;
    m_autoFaceStartAngles.Init();
    m_autoFaceTargetAngles.Init();
    m_autoFaceStartTime = 0.0f;
    m_autoFaceDuration = 0.0f;
    m_autoFaceConsumed = false;

    // Make a modest sized centered panel
    int sx, sy;
    surface()->GetScreenSize(sx, sy);
    // Make dialog wider and positioned lower on screen (Fallout NV style)
    int w = Max(700, (sx * 7) / 10);
    int h = 260;
    SetSize(w, h);
    // place near bottom center with a modest margin
    SetPos((sx - w) / 2, sy - h - 80);

    // Frame provides its own background; ensure it's visible and has a distinct color/alpha
    SetTitle("Dialog", true);
    SetMenuButtonVisible(false);
    // Remove the window close button and prevent resizing so this panel acts like a fixed dialog
    SetCloseButtonVisible(false);
    SetMinimizeButtonVisible(false);
    SetMaximizeButtonVisible(false);
    SetSizeable(false);
    // Prevent the user from moving the window
    SetMoveable(false);
    SetAlpha(255);
    // Black translucent background like the screenshot
    SetBgColor(Color(0, 0, 0, 230));
    // Warm yellow/gold text color for dialog
    SetFgColor(Color(255, 200, 30, 255));
    SetPaintBackgroundEnabled(true);

    m_pSpeakerLabel = new CDialogSpeakerLabel(this, "SpeakerLabel", "");
    m_pSpeakerLabel->SetVisible(true);
    m_pSpeakerLabel->SetBounds(12, 8, w - 24, 20);

    m_pLineLabel = new CDialogLineLabel(this, "LineLabel", "");
    m_pLineLabel->SetVisible(true);
    m_pLineLabel->SetWrap(true);
    m_pLineLabel->SetBounds(12, 30, w - 24, 64);
    m_pLineLabel->SetContentAlignment(Label::a_west);

    SetKeyBoardInputEnabled(false);
    SetMouseInputEnabled(true);
    m_optionDelaySeconds = 0.0f;
    m_optionAvailableAt = 0.0;
    m_waitForSceneEnd = false;
    m_sceneWasPlaying = false;
    m_nOptionsOffset = 0;
}

void CVGuiDialogPanel::SetCurrentNodeId(int nodeId)
{
    m_currentNodeId = nodeId;
}

void CVGuiDialogPanel::SetAutoAdvance(int autoNextId, bool autoClose)
{
    m_autoNextId = autoNextId;
    m_autoClose = autoClose;
}

void CVGuiDialogPanel::SetPayloadFlags(bool autoFaceNPC, bool pendingSpoil)
{
    m_autoFaceNPC = autoFaceNPC;
    m_pendingSpoil = pendingSpoil;
    if (m_autoFaceNPC && !m_autoFaceConsumed && !m_autoFaceActive)
    {
        m_autoFaceConsumed = true;
        m_autoFaceActive = true;
        m_autoFaceLastFrameTime = gpGlobals ? gpGlobals->curtime : -1.0f;
        float now = gpGlobals ? gpGlobals->curtime : 0.0f;
        m_autoFaceStartTime = now;
        engine->GetViewAngles(m_autoFaceStartAngles);

        m_autoFaceDuration = 0.65f;
        m_autoFaceEndsAt = now + 1.25f;

        if (gpGlobals && m_entityEntIndex > 0)
        {
            C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
            C_BaseEntity *pEnt = cl_entitylist->GetEnt(m_entityEntIndex);
            if (pLocal && pEnt)
            {
                Vector targetPos = pEnt->WorldSpaceCenter() + Vector(0, 0, 21.0f);
                Vector dir = targetPos - pLocal->EyePosition();
                QAngle targetAng;
                VectorAngles(dir, targetAng);
                m_autoFaceTargetAngles = targetAng;

                float yawDiff = fabsf(AngleDiffDeg(targetAng.y, m_autoFaceStartAngles.y));
                float pitchDiff = fabsf(AngleDiffDeg(targetAng.x, m_autoFaceStartAngles.x));
                float total = yawDiff > pitchDiff ? yawDiff : pitchDiff;
                m_autoFaceDuration = 0.35f + (total / 180.0f) * 0.55f;
                if (m_autoFaceDuration < 0.35f) m_autoFaceDuration = 0.35f;
                if (m_autoFaceDuration > 0.90f) m_autoFaceDuration = 0.90f;
            }
        }
        vgui::ivgui()->AddTickSignal( GetVPanel(), 1 );
    }
}

void CVGuiDialogPanel::SetSpeakerName(const char *name)
{
    if (!m_pSpeakerLabel)
        return;
    m_pSpeakerLabel->SetText((name && name[0]) ? name : "");
    m_pSpeakerLabel->SetVisible(name && name[0]);
}

void CVGuiDialogPanel::PaintBackground()
{
    int w, h;
    GetSize(w, h);
    Color c = GetBgColor();
    surface()->DrawSetColor(c.r(), c.g(), c.b(), c.a());
    surface()->DrawFilledRect(0, 0, w, h);
    // draw an outline
    surface()->DrawSetColor(255, 255, 255, 32);
    surface()->DrawOutlinedRect(0, 0, w-1, h-1);
}

void CVGuiDialogPanel::Paint()
{
    // Draw background using configured background color (black translucent)
    int w, h;
    GetSize(w, h);
    Color c = GetBgColor();
    surface()->DrawSetColor(c.r(), c.g(), c.b(), c.a());
    surface()->DrawFilledRect(0, 0, w, h);
    // subtle border (slightly lighter)
    surface()->DrawSetColor(60, 60, 60, 200);
    surface()->DrawOutlinedRect(0, 0, w-1, h-1);

    if (!m_bPaintCalled)
    {
        Msg("CVGuiDialogPanel::Paint called (first time) GetVPanel()=%p parent=%p\n", (void*)GetVPanel(), (void*)GetParent());
        m_bPaintCalled = true;
    }

    BaseClass::Paint();
}

CVGuiDialogPanel *GetGlobalDialogPanel()
{
    if (!s_pDialogPanel)
    {
        Msg("GetGlobalDialogPanel: creating dialog panel\n");
        vgui::Panel *parent = nullptr;
        if ( g_pClientMode )
            parent = g_pClientMode->GetViewport();
        s_pDialogPanel = new CVGuiDialogPanel(parent);
    }
    return s_pDialogPanel;
}

void CVGuiDialogPanel::BeginDialog(const char *entityName, int entIndex)
{
    bool continuing = (IsVisible() && m_entityName.Length() > 0 && entityName && !Q_stricmp(m_entityName.Get(), entityName));

    m_entityName = entityName ? entityName : "";
    m_entityEntIndex = entIndex;
    m_currentNodeId = -1;
    m_autoNextId = -1;
    m_autoClose = false;
    m_autoFaceNPC = false;
    m_pendingSpoil = false;
    m_autoFaceActive = false;
    if (!continuing)
    {
        m_autoFaceActive = false;
        m_autoFaceEndsAt = 0.0f;
        m_autoFaceLastFrameTime = -1.0f;
        m_autoFaceStartAngles.Init();
        m_autoFaceTargetAngles.Init();
        m_autoFaceStartTime = 0.0f;
        m_autoFaceDuration = 0.0f;
        m_autoFaceConsumed = false;
    }
    for (int i = 0; i < m_Options.Count(); ++i)
    {
        if (m_Options[i])
            m_Options[i]->MarkForDeletion();
    }
    m_Options.RemoveAll();
    m_nOptionsOffset = 0;

    Msg("CVGuiDialogPanel::BeginDialog entity=%s\n", m_entityName.Get());
    m_pLineLabel->SetText("...");
    // Ensure we have a parent (viewport) so the panel is actually displayed and receives input
    if (!GetParent())
    {
        if ( g_pClientMode && g_pClientMode->GetViewport() )
        {
            Msg("CVGuiDialogPanel: setting parent to client viewport\n");
            SetParent( g_pClientMode->GetViewport() );
        }
        else
        {
            Msg("CVGuiDialogPanel: no client viewport available yet\n");
        }
    }

    // Re-position in case screen size / parent changed: keep near bottom center
    int sx, sy;
    surface()->GetScreenSize(sx, sy);
    int pw, ph;
    GetSize(pw, ph);
    Msg("CVGuiDialogPanel: screenSize=(%d,%d) panelSize=(%d,%d)\n", sx, sy, pw, ph);
    SetPos((sx - pw) / 2, sy - ph - 80);

    // Try to make this panel popup/modal so it reliably receives input and shows cursor
    MakePopup();
    // Ensure panel is visible and on top
    SetVisible(true);
    SetZPos(1000);
    MoveToFront();
    if ( GetParent() )
    {
        GetParent()->MoveToFront();
    }

    // Enable input and capture mouse so player can click options
    SetKeyBoardInputEnabled(true);
    SetMouseInputEnabled(true);

    if ( input() )
    {
        vgui::VPANEL vp = GetVPanel();
        Msg("CVGuiDialogPanel: GetVPanel()=%p, input()=%p\n", (void*)vp, (void*)input());
        // Do not forcibly set global mouse focus/capture to the panel here.
        // Forcing mouse focus/capture to the panel prevents child controls
        // (buttons) from receiving normal mouse press/release messages and
        // therefore they won't show pressed state. MakePopup() +
        // SetMouseInputEnabled(true) is sufficient for dialogs.
    }
    else
    {
        Msg("CVGuiDialogPanel: input() is NULL, cannot capture mouse\n");
    }

    // Make sure the arrow cursor is visible
    surface()->SetCursor( vgui::dc_arrow );
    RequestFocus();
    SetEnabled(true);
    // Force layout/update
    InvalidateLayout(true);
    Repaint();
    // Log final state including parent size
    int px, py;
    GetPos(px, py);
    GetSize(pw, ph);
    if ( GetParent() )
    {
        int parentW = 0, parentH = 0;
        GetParent()->GetSize(parentW, parentH);
        Msg("CVGuiDialogPanel: visible=%d parent=%p parentSize=(%d,%d) pos=(%d,%d) size=(%d,%d)\n", IsVisible(), (void*)GetParent(), parentW, parentH, px, py, pw, ph);
    }
    else
    {
        Msg("CVGuiDialogPanel: visible=%d parent=NULL pos=(%d,%d) size=(%d,%d)\n", IsVisible(), px, py, pw, ph);
    }
    // schedule option visibility based on configured delay (use same timebase as OnTick)
    m_optionAvailableAt = (gpGlobals ? (gpGlobals->curtime + m_optionDelaySeconds) : 0.0f);
    // start receiving ticks so we can enable options when time arrives
    vgui::ivgui()->AddTickSignal( GetVPanel(), m_autoFaceActive ? 1 : 100 );
}

void CVGuiDialogPanel::SetLine(const char *line)
{
    if (m_pLineLabel)
        m_pLineLabel->SetText(line ? line : "");
}

void CVGuiDialogPanel::SetChoreo(const char *choreo, const char *extraSequence)
{
    if (!choreo || !choreo[0])
        return;

    // cleanup any previous scene
    if (m_hSceneEntity.Get())
    {
        m_hSceneEntity->StopClientOnlyScene();
        m_hSceneEntity->Remove();
        m_hSceneEntity = NULL;
    }

    char loadfile[MAX_PATH];
    Q_strncpy(loadfile, choreo, sizeof(loadfile));
    Q_SetExtension(loadfile, ".vcd", sizeof(loadfile));
    Q_FixSlashes(loadfile);

    // Try to find the actor entity by the stored targetname
    C_BaseFlex *pFlexOwner = NULL;
    // First prefer lookup by explicit entindex if provided
    if (m_entityEntIndex > 0)
    {
        C_BaseEntity *pEnt = cl_entitylist->GetEnt(m_entityEntIndex);
        if (pEnt)
        {
            pFlexOwner = dynamic_cast<C_BaseFlex*>(pEnt);
        }
    }
    if (!pFlexOwner && m_entityName.Get()[0])
    {
        int last = cl_entitylist->GetHighestEntityIndex();
        for (int i = 1; i <= last; ++i)
        {
            C_BaseEntity *pEnt = cl_entitylist->GetEnt(i);
            if (!pEnt) continue;
            const char *ename = pEnt->GetEntityName();
            if (!ename || !ename[0]) continue;
            if (Q_stricmp(ename, m_entityName.Get()) == 0)
            {
                pFlexOwner = dynamic_cast<C_BaseFlex*>(pEnt);
                break;
            }
        }
    }

    if (pFlexOwner)
    {
        Msg("CVGuiDialogPanel::SetChoreo: found actor owner entindex=%d name=%s\n", pFlexOwner->entindex(), pFlexOwner->GetEntityName() ? pFlexOwner->GetEntityName() : "(null)");
    }
    else
    {
        Msg("CVGuiDialogPanel::SetChoreo: no client-side actor found for targetname '%s'\n", m_entityName.Get());
    }

    // Create a client-only scene entity and attach to the flex actor (if found)
    C_SceneEntity *pSceneEnt = new C_SceneEntity;
    if (!pSceneEnt)
        return;

    if (!pSceneEnt->InitializeAsClientEntity("", RENDER_GROUP_OTHER))
    {
        pSceneEnt->Remove();
        return;
    }

    m_hSceneEntity = pSceneEnt;
    // true -> multiplayer flag to allow gestures/sequences + speaking events
    Msg("CVGuiDialogPanel::SetChoreo: initializing client-only scene '%s' owner=%p\n", loadfile, pFlexOwner);
    pSceneEnt->SetupClientOnlyScene(loadfile, pFlexOwner, true);
    Msg("CVGuiDialogPanel::SetChoreo: SetupClientOnlyScene returned, scene entity=%p\n", m_hSceneEntity.Get());
    // Wait for the client-side scene to finish before showing options (preferred)
    m_waitForSceneEnd = true;
    m_sceneWasPlaying = false;
    // ensure we receive ticks to observe scene completion
    vgui::ivgui()->AddTickSignal( GetVPanel(), m_autoFaceActive ? 1 : 100 );
}

void CVGuiDialogPanel::PlaySound(const char *soundName)
{
    if (soundName && soundName[0])
    {
        char cmd[512];
        Q_snprintf(cmd, sizeof(cmd), "play %s\n", soundName);
        engine->ClientCmd(cmd);
    }
}

void CVGuiDialogPanel::AddOption(int id, const char *text, int nextId, int flags)
{
    int idx = m_Options.Count();
    char namebuf[64];
    Q_snprintf(namebuf, sizeof(namebuf), "OptionButton%d", idx);
    CDialogOptionButton *btn = new CDialogOptionButton(this, namebuf, text ? text : "");

    // Position buttons stacked under the line
    int pw, ph;
    GetSize(pw, ph);
    int btnW = pw - 24;
    int btnH = 28;
    int x = 12;
    int baseY = 30 + 64 + 12;
    int y = baseY + idx * (btnH + 6) - m_nOptionsOffset;
    btn->SetBounds(x, y, btnW, btnH);

    char cmd[256];
    // Include flags in the command so client and server know option attributes
    Q_snprintf(cmd, sizeof(cmd), "dialog_choose %d %d \"%s\" %d", m_currentNodeId, id, m_entityName.Get(), flags);
    // Use VGUI command so panel receives OnCommand, but also prepare to send to server from OnCommand
    btn->SetCommand(cmd);
    // start disabled/invisible until allowed (waiting for NPC/dialog to finish)
    btn->SetEnabled(false);
    btn->SetKeyBoardInputEnabled(true);
    btn->SetMouseInputEnabled(true);
    btn->SetVisible(false);
    // Style button: no border, gold text. Background painting is handled by CDialogOptionButton
    btn->SetPaintBorderEnabled(false);
    btn->SetFgColor(Color(255, 200, 30, 255));
    m_Options.AddToTail(btn);
    Msg("CVGuiDialogPanel: added option id=%d text='%s' bounds=(%d,%d,%d,%d)\n", id, text ? text : "", x, y, btnW, btnH);
}

void CVGuiDialogPanel::OnMouseWheeled(int delta)
{
    if (!IsVisible() || m_Options.Count() == 0)
        return;

    int mx, my;
    input()->GetCursorPos(mx, my);
    ScreenToLocal(mx, my);

    int pw, ph;
    GetSize(pw, ph);
    int baseY = 30 + 64 + 12;
    if (my < baseY || my > ph - 8)
        return;

    int btnH = 28;
    int gap = 6;
    int viewHeight = (ph - 8) - baseY;
    int contentHeight = m_Options.Count() * (btnH + gap);
    int maxOffset = contentHeight - viewHeight;
    if (maxOffset < 0)
        maxOffset = 0;

    int step = 60;
    int d = (delta > 0) ? -step : step;
    m_nOptionsOffset += d;
    if (m_nOptionsOffset < 0)
        m_nOptionsOffset = 0;
    if (m_nOptionsOffset > maxOffset)
        m_nOptionsOffset = maxOffset;

    for (int i = 0; i < m_Options.Count(); ++i)
    {
        int bx, by, bw, bh;
        m_Options[i]->GetBounds(bx, by, bw, bh);
        int y = baseY + i * (btnH + gap) - m_nOptionsOffset;
        m_Options[i]->SetBounds(bx, y, bw, bh);
        bool inView = (y >= baseY) && (y <= (ph - 8 - btnH));
        if (m_Options[i]->IsEnabled())
            m_Options[i]->SetVisible(inView);
    }

    InvalidateLayout();
    Repaint();
}

void CVGuiDialogPanel::EndDialog()
{
    // Hide and release input capture
    SetVisible(false);
    SetKeyBoardInputEnabled(false);
    SetMouseInputEnabled(false);
    if ( input() )
    {
        input()->SetMouseCapture( (vgui::VPANEL)NULL );
        input()->SetMouseFocus( (vgui::VPANEL)NULL );
    }
    surface()->SetCursor( vgui::dc_none );
    // stop ticking
    vgui::ivgui()->RemoveTickSignal( GetVPanel() );
    m_optionAvailableAt = 0.0;
}

void CVGuiDialogPanel::SetOptionDelay(float seconds)
{
    if (seconds < 0.0f) seconds = 0.0f;
    m_optionDelaySeconds = seconds;
}

void CVGuiDialogPanel::OnTick()
{
    float now = gpGlobals ? gpGlobals->curtime : 0.0f;

    if (m_autoFaceActive)
    {
        if (now >= m_autoFaceEndsAt)
        {
            m_autoFaceActive = false;
        }
        else
        {
            if (!gpGlobals || gpGlobals->curtime == m_autoFaceLastFrameTime)
                goto AutoAdvance;
            m_autoFaceLastFrameTime = gpGlobals->curtime;

            C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
            if (pLocal && m_entityEntIndex > 0)
            {
                C_BaseEntity *pEnt = cl_entitylist->GetEnt(m_entityEntIndex);
                if (pEnt)
                {
                    QAngle targetAng = m_autoFaceTargetAngles;
                    if (targetAng.x == 0.0f && targetAng.y == 0.0f && targetAng.z == 0.0f)
                    {
                        Vector targetPos = pEnt->WorldSpaceCenter() + Vector(0, 0, 21.0f);
                        Vector dir = targetPos - pLocal->EyePosition();
                        VectorAngles(dir, targetAng);
                    }

                    QAngle curAng;
                    engine->GetViewAngles(curAng);

                    float duration = m_autoFaceDuration > 0.01f ? m_autoFaceDuration : 0.65f;
                    float t = (gpGlobals->curtime - m_autoFaceStartTime) / duration;
                    float smooth = SmoothStep01(t);

                    QAngle newAng = m_autoFaceStartAngles;
                    newAng.y = AngleNormalize180(m_autoFaceStartAngles.y + AngleDiffDeg(targetAng.y, m_autoFaceStartAngles.y) * smooth);
                    newAng.x = AngleNormalize180(m_autoFaceStartAngles.x + AngleDiffDeg(targetAng.x, m_autoFaceStartAngles.x) * smooth);
                    newAng.z = 0.0f;
                    engine->SetViewAngles(newAng);

                    if (t >= 1.0f)
                    {
                        QAngle finalAng = targetAng;
                        finalAng.z = 0.0f;
                        engine->SetViewAngles(finalAng);
                        m_autoFaceActive = false;
                    }
                }
            }
        }
    }


AutoAdvance:
    if (m_Options.Count() == 0 && (m_autoNextId != -1 || m_autoClose))
    {
        bool ready = false;
        if (m_optionAvailableAt > 0.0 && now >= m_optionAvailableAt)
            ready = true;

        if (!ready && m_waitForSceneEnd && m_hSceneEntity.Get())
        {
            bool playing = m_hSceneEntity->IsPlayingBack();
            if (playing)
            {
                m_sceneWasPlaying = true;
            }
            else if (m_sceneWasPlaying && !playing)
            {
                ready = true;
            }
        }

        if (ready)
        {
            if (m_hSceneEntity.Get())
            {
                m_hSceneEntity->StopClientOnlyScene();
                m_hSceneEntity->Remove();
                m_hSceneEntity = NULL;
            }
            m_waitForSceneEnd = false;
            m_sceneWasPlaying = false;
            m_optionAvailableAt = 0.0;
            if (!m_autoFaceActive)
                vgui::ivgui()->RemoveTickSignal( GetVPanel() );

            if (m_entityName.Length() > 0 && m_currentNodeId != -1)
            {
                char buf[320];
                if (m_pendingSpoil)
                    Q_snprintf(buf, sizeof(buf), "dialog_finish_spoil %d \"%s\"\n", m_currentNodeId, m_entityName.Get());
                else
                    Q_snprintf(buf, sizeof(buf), "dialog_continue %d \"%s\" %d %d\n", m_currentNodeId, m_entityName.Get(), m_autoNextId, m_autoClose ? 1 : 0);
                engine->ClientCmd(buf);
            }
            m_autoNextId = -1;
            m_autoClose = false;
            m_pendingSpoil = false;
            return;
        }
        return;
    }

    // First: time-based availability (server-provided or fallback)
    if (m_optionAvailableAt > 0.0 && now >= m_optionAvailableAt)
    {
        for (int i = 0; i < m_Options.Count(); ++i)
        {
            vgui::Button *b = dynamic_cast<vgui::Button*>(m_Options[i]);
            if (b)
            {
                b->SetVisible(true);
                b->SetEnabled(true);
            }
        }
        if (!m_autoFaceActive)
            vgui::ivgui()->RemoveTickSignal( GetVPanel() );
        m_optionAvailableAt = 0.0;
        m_waitForSceneEnd = false;
        m_sceneWasPlaying = false;
        return;
    }

    // If we are waiting for the client-side scene to finish, also allow scene-end to show options
    if (m_waitForSceneEnd && m_hSceneEntity.Get())
    {
        bool playing = m_hSceneEntity->IsPlayingBack();
        if (playing)
        {
            m_sceneWasPlaying = true;
        }
        else if (m_sceneWasPlaying && !playing)
        {
            // Scene finished — show options immediately
            for (int i = 0; i < m_Options.Count(); ++i)
            {
                vgui::Button *b = dynamic_cast<vgui::Button*>(m_Options[i]);
                if (b)
                {
                    b->SetVisible(true);
                    b->SetEnabled(true);
                }
            }
            m_waitForSceneEnd = false;
            m_sceneWasPlaying = false;
            if (!m_autoFaceActive)
                vgui::ivgui()->RemoveTickSignal( GetVPanel() );
            m_optionAvailableAt = 0.0;
            return;
        }
    }
    // otherwise keep waiting
}

void CVGuiDialogPanel::OnMousePressed(vgui::MouseCode code)
{
    if (code == MOUSE_LEFT)
    {
        if (m_Options.Count() == 0 && (m_autoNextId != -1 || m_autoClose))
        {
            if (m_hSceneEntity.Get())
            {
                m_hSceneEntity->StopClientOnlyScene();
                m_hSceneEntity->Remove();
                m_hSceneEntity = NULL;
            }
            m_waitForSceneEnd = false;
            m_sceneWasPlaying = false;
            m_optionAvailableAt = 0.0;
            vgui::ivgui()->RemoveTickSignal( GetVPanel() );

            if (m_entityName.Length() > 0 && m_currentNodeId != -1)
            {
                char buf[320];
                if (m_pendingSpoil)
                    Q_snprintf(buf, sizeof(buf), "dialog_finish_spoil %d \"%s\"\n", m_currentNodeId, m_entityName.Get());
                else
                    Q_snprintf(buf, sizeof(buf), "dialog_continue %d \"%s\"\n", m_currentNodeId, m_entityName.Get());
                engine->ClientCmd(buf);
            }
            m_autoNextId = -1;
            m_autoClose = false;
            m_pendingSpoil = false;
            return;
        }

        bool hasHiddenOptions = false;
        for (int i = 0; i < m_Options.Count(); ++i)
        {
            vgui::Button *b = dynamic_cast<vgui::Button*>(m_Options[i]);
            if (b && (!b->IsVisible() || !b->IsEnabled()))
            {
                hasHiddenOptions = true;
                break;
            }
        }
        if (hasHiddenOptions && (m_waitForSceneEnd || m_optionAvailableAt > 0.0))
        {
            if (m_hSceneEntity.Get())
            {
                m_hSceneEntity->StopClientOnlyScene();
                m_hSceneEntity->Remove();
                m_hSceneEntity = NULL;
            }
            m_waitForSceneEnd = false;
            m_sceneWasPlaying = false;
            m_optionAvailableAt = 0.0;
            vgui::ivgui()->RemoveTickSignal( GetVPanel() );

            for (int i = 0; i < m_Options.Count(); ++i)
            {
                vgui::Button *b = dynamic_cast<vgui::Button*>(m_Options[i]);
                if (b)
                {
                    b->SetVisible(true);
                    b->SetEnabled(true);
                }
            }
            return;
        }
    }

    BaseClass::OnMousePressed(code);
}

void CVGuiDialogPanel::OnCommand(const char *command)
{
    // Intercept dialog_choose commands and forward to server explicitly
    if (command && Q_strnicmp(command, "dialog_choose", Q_strlen("dialog_choose")) == 0)
    {
        Msg("CVGuiDialogPanel: OnCommand -> forwarding dialog_choose to server: %s\n", command);
        int flags = 0;
        const char *closing = strrchr(command, '"');
        if (closing)
        {
            const char *after = closing + 1;
            while (*after && isspace(*after)) ++after;
            if (*after)
                flags = atoi(after);
        }

        // If this option requests closing the dialog locally, do it now (flag bit 1)
        if (flags & 1)
        {
            CVGuiDialogPanel *panel = GetGlobalDialogPanel();
            if (panel)
                panel->EndDialog();
        }

        // Forward the choice to the server by executing the server-accessible concommand.
        char buf[320];
        Q_snprintf(buf, sizeof(buf), "%s\n", command);
        engine->ClientCmd(buf);
        return;
    }

    BaseClass::OnCommand(command);
}

static void __Cmd_Dialog_Open_Begin(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *ename = args.Arg(1);
    Msg("client: dialog_open_begin %s\n", ename);
    CVGuiDialogPanel *panel = GetGlobalDialogPanel();
    if ( panel )
        panel->BeginDialog(ename);
}

static void HandleDialogPayloadString(const char *payloadStr)
{
    if (!payloadStr || !payloadStr[0])
        return;

    CUtlString payload(payloadStr);

    // Split by '|'
    CUtlVector<CUtlString> parts;
    int last = 0;
    const char *s = payload.Get();
    int len = Q_strlen(s);
    for (int i = 0; i <= len; ++i)
    {
        if (s[i] == '|' || s[i] == '\0')
        {
            int chunkLen = i - last;
            char tmp[2048];
            Q_memcpy(tmp, s + last, chunkLen);
            tmp[chunkLen] = '\0';
            parts.AddToTail(CUtlString(tmp));
            last = i + 1;
        }
    }

    const char *entity = parts.Count() > 0 ? parts[0].Get() : "";
    int entIndex = (parts.Count() > 1) ? atoi(parts[1].Get()) : -1;
    const char *displayName = "";
    int nodeId = -1;
    const char *line = "";
    const char *choreo = "";
    const char *sound = "";
    const char *sequenceName = "";
    float optionDelay = 0.0f;
    int autoNextId = -1;
    bool autoClose = false;
    bool autoFaceNPC = false;
    bool pendingSpoil = false;
    const char *opts = "";

    if (parts.Count() >= 14)
    {
        displayName = parts[2].Get();
        nodeId = atoi(parts[3].Get());
        line = parts[4].Get();
        choreo = parts[5].Get();
        sound = parts[6].Get();
        sequenceName = parts[7].Get();
        optionDelay = (float)atof(parts[8].Get());
        autoNextId = atoi(parts[9].Get());
        autoClose = atoi(parts[10].Get()) != 0;
        autoFaceNPC = atoi(parts[11].Get()) != 0;
        pendingSpoil = atoi(parts[12].Get()) != 0;
        opts = parts[13].Get();
    }
    else if (parts.Count() >= 12)
    {
        displayName = parts[2].Get();
        nodeId = atoi(parts[3].Get());
        line = parts[4].Get();
        choreo = parts[5].Get();
        sound = parts[6].Get();
        sequenceName = parts[7].Get();
        optionDelay = (float)atof(parts[8].Get());
        autoNextId = atoi(parts[9].Get());
        autoClose = atoi(parts[10].Get()) != 0;
        opts = parts[11].Get();
    }
    else if (parts.Count() >= 10)
    {
        displayName = parts[2].Get();
        nodeId = atoi(parts[3].Get());
        line = parts[4].Get();
        choreo = parts[5].Get();
        sound = parts[6].Get();
        sequenceName = parts[7].Get();
        optionDelay = (float)atof(parts[8].Get());
        opts = parts[9].Get();
    }
    else
    {
        line = parts.Count() > 2 ? parts[2].Get() : "";
        choreo = parts.Count() > 3 ? parts[3].Get() : "";
        sound = parts.Count() > 4 ? parts[4].Get() : "";
        sequenceName = parts.Count() > 5 ? parts[5].Get() : "";
        if (parts.Count() > 7)
        {
            optionDelay = (float)atof(parts[6].Get());
            opts = parts[7].Get();
        }
        else if (parts.Count() == 7)
        {
            opts = parts[6].Get();
        }
        else if (parts.Count() == 6)
        {
            opts = parts[5].Get();
        }
    }

    CVGuiDialogPanel *panel = GetGlobalDialogPanel();
    if (!panel) return;
    // Apply server-specified option delay (seconds) before beginning dialog
    panel->SetOptionDelay(optionDelay);
    panel->BeginDialog(entity, entIndex);
    panel->SetCurrentNodeId(nodeId);
    panel->SetAutoAdvance(autoNextId, autoClose);
    panel->SetPayloadFlags(autoFaceNPC, pendingSpoil);
    panel->SetSpeakerName(displayName);
    if (line && line[0]) panel->SetLine(line);
    bool usedChoreo = (choreo && choreo[0]);
    if (usedChoreo) panel->SetChoreo(choreo, sequenceName[0] ? sequenceName : NULL);
    // Prefer sound embedded in the choreo scene if a choreo was provided.
    // Only play the separate .wav when no choreo is supplied.
    else if (sound && sound[0]) panel->PlaySound(sound);

    // parse options separated by ';' and fields by '~'
    if (opts && opts[0])
    {
        CUtlVector<CUtlString> olist;
        int lastO = 0;
        int olen = Q_strlen(opts);
        for (int i = 0; i <= olen; ++i)
        {
            if (opts[i] == ';' || opts[i] == '\0')
            {
                int chunkLen = i - lastO;
                char tmp[1024];
                Q_memcpy(tmp, opts + lastO, chunkLen);
                tmp[chunkLen] = '\0';
                olist.AddToTail(CUtlString(tmp));
                lastO = i + 1;
            }
        }
        for (int oi = 0; oi < olist.Count(); ++oi)
        {
            const char *o = olist[oi].Get();
            // parse id~text~next~flags
            CUtlVector<CUtlString> fields;
            int lastF = 0;
            int flen = Q_strlen(o);
            for (int j = 0; j <= flen; ++j)
            {
                if (o[j] == '~' || o[j] == '\0')
                {
                    int chunkLen = j - lastF;
                    char tmpf[512];
                    Q_memcpy(tmpf, o + lastF, chunkLen);
                    tmpf[chunkLen] = '\0';
                    fields.AddToTail(CUtlString(tmpf));
                    lastF = j + 1;
                }
            }
            int id = fields.Count() > 0 ? atoi(fields[0].Get()) : -1;
            const char *text = fields.Count() > 1 ? fields[1].Get() : "";
            int nextId = fields.Count() > 2 ? atoi(fields[2].Get()) : -1;
            int flags = fields.Count() > 3 ? atoi(fields[3].Get()) : 0;
            panel->AddOption(id, text, nextId, flags);
        }
    }
}

// New payload-based opening (server sends full node in one argument)
static void __Cmd_Dialog_Open_Payload(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    CUtlString payload;
    for (int i = 1; i < args.ArgC(); ++i)
    {
        if (i > 1) payload.Append(" ");
        payload.Append(args.Arg(i));
    }
    HandleDialogPayloadString(payload.Get());
}

static CUtlString s_DialogPayloadAccum;

static void __Cmd_Dialog_Open_Payload_Begin(const CCommand &args)
{
    s_DialogPayloadAccum.Clear();
}

static void __Cmd_Dialog_Open_Payload_Chunk(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    for (int i = 1; i < args.ArgC(); ++i)
    {
        s_DialogPayloadAccum.Append(args.Arg(i));
        if (i + 1 < args.ArgC())
            s_DialogPayloadAccum.Append(" ");
    }
}

static void __Cmd_Dialog_Open_Payload_End(const CCommand &args)
{
    HandleDialogPayloadString(s_DialogPayloadAccum.Get());
    s_DialogPayloadAccum.Clear();
}

static void __Cmd_Dialog_Set_Line(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    // Join all remaining args into a single line (to preserve spaces)
    CUtlString buf;
    for (int i = 1; i < args.ArgC(); ++i)
    {
        if (i > 1) buf.Append(" ");
        buf.Append(args.Arg(i));
    }
    GetGlobalDialogPanel()->SetLine(buf.Get());
}

static void __Cmd_Dialog_Set_Choreo(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *choreo = args.Arg(1);
    GetGlobalDialogPanel()->SetChoreo(choreo);
}

static void __Cmd_Dialog_Play_Sound(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    const char *s = args.Arg(1);
    GetGlobalDialogPanel()->PlaySound(s);
}

static void __Cmd_Dialog_Add_Option(const CCommand &args)
{
    if (args.ArgC() < 3) return;
    int id = atoi(args.Arg(1));
    const char *text = args.Arg(2);
    int nextId = -1;
    int flags = 0;
    if (args.ArgC() >= 4)
        nextId = atoi(args.Arg(3));
    if (args.ArgC() >= 5)
        flags = atoi(args.Arg(4));
    GetGlobalDialogPanel()->AddOption(id, text, nextId, flags);
}

static void __Cmd_Dialog_Open_End(const CCommand &args)
{
    GetGlobalDialogPanel()->EndDialog();
}

static ConCommand dialog_open_begin("dialog_open_begin", __Cmd_Dialog_Open_Begin, "Begin dialog for entity (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_set_line("dialog_set_line", __Cmd_Dialog_Set_Line, "Set dialog line (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_set_choreo("dialog_set_choreo", __Cmd_Dialog_Set_Choreo, "Set choreo (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_play_sound("dialog_play_sound", __Cmd_Dialog_Play_Sound, "Play dialog sound (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_add_option("dialog_add_option", __Cmd_Dialog_Add_Option, "Add an option (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_open_end("dialog_open_end", __Cmd_Dialog_Open_End, "End dialog (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_open_payload("dialog_open_payload", __Cmd_Dialog_Open_Payload, "Open dialog with payload (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_open_payload_begin("dialog_open_payload_begin", __Cmd_Dialog_Open_Payload_Begin, "Begin chunked dialog payload (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_open_payload_chunk("dialog_open_payload_chunk", __Cmd_Dialog_Open_Payload_Chunk, "Append chunked dialog payload (internal)", FCVAR_CLIENTDLL);
static ConCommand dialog_open_payload_end("dialog_open_payload_end", __Cmd_Dialog_Open_Payload_End, "End chunked dialog payload (internal)", FCVAR_CLIENTDLL);

static void __Cmd_Dialog_Set_Option_Delay(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    float seconds = atof(args.Arg(1));
    CVGuiDialogPanel *panel = GetGlobalDialogPanel();
    if (panel)
    {
        panel->SetOptionDelay(seconds);
        DevMsg("dialog: set option delay to %f seconds\n", seconds);
    }
}
static ConCommand dialog_set_option_delay("dialog_set_option_delay", __Cmd_Dialog_Set_Option_Delay, "Set delay (seconds) before dialog options appear (client)", FCVAR_CLIENTDLL);

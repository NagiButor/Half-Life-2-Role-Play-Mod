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
#include "hl2rpm_ui.h"
#include "hl2rpm_items.h"
#include "tier1/fmtstr.h"

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

// HL2RPM (M6): an option of the dialog - a numbered row painted in the look of hl2rpm_ui.h;
// a check label at the start of the text ("[Speech 50] ", added by the server) is green when
// the player passes it and red when not (flags 4 / 8), greyed options (flag 2) can't be chosen
class CDialogOptionButton : public Button
{
public:
    CDialogOptionButton(CVGuiDialogPanel *parent, const char *panelName, const char *text, int index)
        : Button(parent, panelName, text), m_pDialog(parent), m_iIndex(index)
    {
        SetPaintBackgroundEnabled(false);
        SetPaintBorderEnabled(false);
        m_iRPMFlags = 0;
        m_Text = text ? text : "";
    }

    // flags from the server - 2 conditions unmet (shown greyed, can't be chosen),
    // 4 / 8 a skill check the player passes / fails
    int m_iRPMFlags;
    CUtlString m_Text;
    CVGuiDialogPanel *m_pDialog;
    int m_iIndex;
    bool IsGreyed() const { return ( m_iRPMFlags & 2 ) != 0; }

    virtual void PaintBackground() OVERRIDE {}

    virtual void Paint() OVERRIDE
    {
        using namespace RPMUI;
        const Palette_t &p = Pal();
        float a = m_pDialog->OptionAppear(m_iIndex);
        if (a <= 0.0f)
            return;
        int w, h;
        GetSize(w, h);
        bool hl = !IsGreyed() && m_pDialog->HighlightedOption() == m_iIndex;
        if (hl)
            Highlight(0, 0, w, h, 1.0f - (gpGlobals->realtime - m_pDialog->HighlightTime()) / 0.35f, (int)(255 * a));
        int ty = (h - surface()->GetFontTall(Font(F_TEXT))) / 2;
        Color base = IsGreyed() ? p.grey : (hl ? p.text : p.accent);
        Text(F_TEXT, Sc(14), ty, Fade(hl ? p.text : (IsGreyed() ? p.grey : p.dim), a), CFmtStr("%d.", m_iIndex + 1));
        int tx = Sc(46);
        const char *s = m_Text.Get();
        if (s[0] == '[')
        {
            const char *end = strchr(s, ']');
            if (end)
            {
                char label[128];
                int n = Min((int)(end - s + 1), (int)sizeof(label) - 1);
                Q_strncpy(label, s, n + 1);
                Color lc = IsGreyed() ? p.grey : (m_iRPMFlags & 8) ? p.bad : (m_iRPMFlags & 4) ? p.good : base;
                tx += Text(F_TEXT, tx, ty, Fade(lc, a), label) + Sc(8);
                s = end + 1;
                while (*s == ' ')
                    s++;
            }
        }
        Text(F_TEXT, tx, ty, Fade(base, a), s, hl, 0, w - tx - Sc(10));
    }
};

// HL2RPM: an option shown after the line ends - greyed ones stay disabled
static void ShowOptionButton(vgui::Button *b)
{
    CDialogOptionButton *ob = dynamic_cast<CDialogOptionButton *>(b);
    b->SetVisible(true);
    b->SetEnabled(!(ob && ob->IsGreyed()));
}

static bool IsOptionGreyed(vgui::Panel *p)
{
    CDialogOptionButton *ob = dynamic_cast<CDialogOptionButton *>(p);
    return ob && ob->IsGreyed();
}

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
    m_flOpenTime = m_flLineTime = m_flOptionsTime = m_flSelTime = 0.0f;
    m_iKeySel = -1;
    m_iLastHighlight = -1;
    m_iTabH = m_iBoxH = m_iOptTop = m_iRowH = m_iOptViewH = 0;
    m_bEscapeBlocked = false;
    m_bOptionsRevealed = false;

    // a fixed window without a title bar: everything is painted (hl2rpm_ui.h)
    SetTitle("", false);
    SetTitleBarVisible(false);
    SetMenuButtonVisible(false);
    SetCloseButtonVisible(false);
    SetMinimizeButtonVisible(false);
    SetMaximizeButtonVisible(false);
    SetSizeable(false);
    SetMoveable(false);
    SetAlpha(255);
    DisableFadeEffect();
    SetPaintBackgroundEnabled(true);

    // the old labels only keep the texts (the window paints them)
    m_pSpeakerLabel = new Label(this, "SpeakerLabel", "");
    m_pSpeakerLabel->SetVisible(false);
    m_pLineLabel = new Label(this, "LineLabel", "");
    m_pLineLabel->SetVisible(false);

    SetKeyBoardInputEnabled(false);
    SetMouseInputEnabled(true);
    m_optionDelaySeconds = 0.0f;
    m_optionAvailableAt = 0.0;
    m_waitForSceneEnd = false;
    m_sceneWasPlaying = false;
    m_nOptionsOffset = 0;
    LayoutDialog();
}

void CVGuiDialogPanel::ApplySchemeSettings(vgui::IScheme *pScheme)
{
    BaseClass::ApplySchemeSettings(pScheme);
    RPMUI::LoadFonts(pScheme);
    SetBgColor(Color(0, 0, 0, 0));
    SetBorder(NULL);
    LayoutDialog();
}

// the window: the name of the NPC in a tab on the top right, under it the box with the
// line and the numbered options; its height follows the text and the options
void CVGuiDialogPanel::LayoutDialog()
{
    using namespace RPMUI;
    int sw, sh;
    surface()->GetScreenSize(sw, sh);
    const int w = Min(Sc(1180), sw - Sc(120));
    m_iTabH = Sc(46);
    m_iRowH = Sc(36);
    int lineH = m_Line.Length() ? WrapHeight(F_TEXT, w - Sc(68), m_Line.Get()) : LineHeight(F_TEXT);
    m_iOptTop = Sc(24) + lineH + Sc(30);
    int n = m_Options.Count();
    int footer = Sc(44);
    int maxBox = sh * 55 / 100;
    m_iOptViewH = n * m_iRowH;
    m_iBoxH = n ? m_iOptTop + m_iOptViewH + footer : Sc(24) + lineH + Sc(20) + footer;
    if (m_iBoxH > maxBox)
    {
        m_iOptViewH = Max(m_iRowH * 2, m_iOptViewH - (m_iBoxH - maxBox));
        m_iBoxH = m_iOptTop + m_iOptViewH + footer;
    }
    m_iBoxH = Max(m_iBoxH, Sc(140));
    int totalH = m_iTabH + m_iBoxH;
    SetBounds((sw - w) / 2, sh - Sc(70) - totalH, w, totalH);
    PlaceOptions();
}

void CVGuiDialogPanel::PlaceOptions()
{
    using namespace RPMUI;
    int w, h;
    GetSize(w, h);
    int top = m_iTabH + m_iOptTop;
    for (int i = 0; i < m_Options.Count(); ++i)
    {
        int y = top + i * m_iRowH - m_nOptionsOffset;
        m_Options[i]->SetBounds(Sc(26), y - Sc(3), w - Sc(52), m_iRowH - Sc(4));
        bool inView = y >= top - 1 && y + m_iRowH <= top + m_iOptViewH + 1;
        if (m_bOptionsRevealed)
            m_Options[i]->SetVisible(inView);
    }
}

// the line ended (or the player clicked): the options appear, greyed ones can't be chosen
void CVGuiDialogPanel::RevealOptions()
{
    for (int i = 0; i < m_Options.Count(); ++i)
        ShowOptionButton(m_Options[i]);
    m_bOptionsRevealed = true;
    PlaceOptions();
}

int CVGuiDialogPanel::HighlightedOption() const
{
    for (int i = 0; i < m_Options.Count(); ++i)
        if (m_Options[i]->IsVisible() && m_Options[i]->IsEnabled() && m_Options[i]->IsArmed())
            return i;
    return m_iKeySel;
}

// options appear one after another once they are shown
float CVGuiDialogPanel::OptionAppear(int index) const
{
    if (m_flOptionsTime <= 0.0f)
        return 0.0f;
    return RPMUI::Ramp(m_flOptionsTime, 0.18f, index * 0.05f);
}

void CVGuiDialogPanel::OnThink()
{
    BaseClass::OnThink();
    if (!IsVisible())
        return;
    if (m_flOptionsTime <= 0.0f && m_bOptionsRevealed && m_Options.Count())
        m_flOptionsTime = gpGlobals->realtime;
    int hl = HighlightedOption();
    if (hl != m_iLastHighlight)
    {
        if (hl >= 0)
            RPMUI::Sound(RPMUI::SND_HOVER);
        m_iLastHighlight = hl;
        m_flSelTime = gpGlobals->realtime;
    }
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
    m_Speaker = (name && name[0]) ? name : "";
}

void CVGuiDialogPanel::PaintBackground()
{
    using namespace RPMUI;
    const Palette_t &p = Pal();
    int w, h;
    GetSize(w, h);
    // the box unfolds from its middle when the window opens
    float open = Ramp(m_flOpenTime, 0.2f);
    int boxH = Max(Sc(6), (int)(m_iBoxH * open));
    int boxY = m_iTabH + (m_iBoxH - boxH) / 2;
    Plate(0, boxY, w, boxH, 246);
    FrameBox(0, boxY, w, boxH);
    // the name of the NPC: a tab on the top right
    if (m_Speaker.Length())
    {
        float a = Ramp(m_flOpenTime, 0.2f, 0.1f);
        int nw = Max(Sc(220), SpacedWidth(F_TAB, m_Speaker.Get(), Sc(3)) + Sc(40));
        Plate(w - nw, 0, nw, m_iTabH - Sc(6), (int)(248 * a));
        int th = surface()->GetFontTall(Font(F_TAB));
        Spaced(F_TAB, w - nw / 2, (m_iTabH - Sc(6) - th) / 2, Fade(p.accent, a), m_Speaker.Get(), Sc(3), true, 1);
    }
}

void CVGuiDialogPanel::Paint()
{
    using namespace RPMUI;
    const Palette_t &p = Pal();
    int w, h;
    GetSize(w, h);
    float a = Ramp(m_flOpenTime, 0.2f, 0.12f) * Ramp(m_flLineTime, 0.25f);
    Wrap(F_TEXT, Sc(34), m_iTabH + Sc(24), w - Sc(68), Fade(p.text, a), m_Line.Get());
    if (m_Options.Count())
        Line(Sc(30), m_iTabH + m_iOptTop - Sc(16), w - Sc(60), Fade(p.faint, a));
    // more options than fit: a hint at the edge that the wheel scrolls
    int top = m_iTabH + m_iOptTop;
    if (m_nOptionsOffset > 0)
        Text(F_SMALL, w - Sc(40), top - Sc(14), p.dim, "\xe2\x96\xb2", false, 1);
    if (m_Options.Count() * m_iRowH - m_nOptionsOffset > m_iOptViewH + 1)
        Text(F_SMALL, w - Sc(40), top + m_iOptViewH, p.dim, "\xe2\x96\xbc", false, 1);
    // the hint at the bottom
    const bool ru = !Q_stricmp(RPMItems::Language(), "russian");
    CFmtStr hint;
    if (m_Options.Count() == 1 && m_flOptionsTime > 0.0f)
        hint.sprintf("%s", ru ? "1 — ответить    ·    Esc — закрыть" : "1 - answer    ·    Esc - close");
    else if (m_Options.Count() && m_flOptionsTime > 0.0f)
        hint.sprintf(ru ? "1–%d — ответить    ·    Esc — закрыть" : "1-%d - answer    ·    Esc - close", Min(9, m_Options.Count()));
    else
        hint.sprintf("%s", ru ? "Щелчок или пробел — дальше    ·    Esc — закрыть" : "Click or space - continue    ·    Esc - close");
    Text(F_SMALL, w / 2, h - Sc(30), Fade(p.grey, a), hint, false, 1);

    if (!m_bPaintCalled)
    {
        Msg("CVGuiDialogPanel::Paint called (first time) GetVPanel()=%p parent=%p\n", (void*)GetVPanel(), (void*)GetParent());
        m_bPaintCalled = true;
    }
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
    m_flOptionsTime = 0.0f;
    m_bOptionsRevealed = false;
    m_iKeySel = -1;
    m_iLastHighlight = -1;
    if (!continuing || !IsVisible())
        m_flOpenTime = gpGlobals->realtime;
    m_flLineTime = gpGlobals->realtime;
    m_Line.Clear();
    // Esc closes the window instead of opening the game menu
    if (!m_bEscapeBlocked)
    {
        engine->ClientCmd_Unrestricted("gameui_preventescapetoshow\n");
        m_bEscapeBlocked = true;
    }

    Msg("CVGuiDialogPanel::BeginDialog entity=%s\n", m_entityName.Get());
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
    LayoutDialog();
    int pw, ph;

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
    m_Line = line ? line : "";
    m_flLineTime = gpGlobals->realtime;
    LayoutDialog();
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
    CDialogOptionButton *btn = new CDialogOptionButton(this, namebuf, text ? text : "", idx);

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
    btn->m_iRPMFlags = flags & ~1;
    m_Options.AddToTail(btn);
    LayoutDialog();
    Msg("CVGuiDialogPanel: added option id=%d text='%s'\n", id, text ? text : "");
}

void CVGuiDialogPanel::OnMouseWheeled(int delta)
{
    if (!IsVisible() || m_Options.Count() == 0)
        return;

    int maxOffset = Max(0, m_Options.Count() * m_iRowH - m_iOptViewH);
    m_nOptionsOffset += (delta > 0) ? -m_iRowH : m_iRowH;
    m_nOptionsOffset = clamp(m_nOptionsOffset, 0, maxOffset);
    PlaceOptions();
    Repaint();
}

void CVGuiDialogPanel::EndDialog()
{
    // HL2RPM: tell the server - actions that waited for the window to close run now (the NPC attacks...)
    if (IsVisible())
        engine->ClientCmd("dialog_closed\n");
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
    if (m_bEscapeBlocked)
    {
        engine->ClientCmd_Unrestricted("gameui_allowescapetoshow\n");
        m_bEscapeBlocked = false;
    }
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
        RevealOptions();
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
            RevealOptions();
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
    if (code == MOUSE_LEFT && ContinueOrReveal())
        return;
    BaseClass::OnMousePressed(code);
}

// a click / space: a line without options continues, options not shown yet appear
bool CVGuiDialogPanel::ContinueOrReveal()
{
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
                else  // the same transition as when the line ends by itself (OnTick)
                    Q_snprintf(buf, sizeof(buf), "dialog_continue %d \"%s\" %d %d\n", m_currentNodeId, m_entityName.Get(), m_autoNextId, m_autoClose ? 1 : 0);
                engine->ClientCmd(buf);
            }
            m_autoNextId = -1;
            m_autoClose = false;
            m_pendingSpoil = false;
            RPMUI::Sound(RPMUI::SND_CLICK);
            return true;
        }

        bool hasHiddenOptions = false;
        for (int i = 0; i < m_Options.Count(); ++i)
        {
            vgui::Button *b = dynamic_cast<vgui::Button*>(m_Options[i]);
            if (b && (!b->IsVisible() || (!b->IsEnabled() && !IsOptionGreyed(b))))
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

            RevealOptions();
            return true;
        }
    }
    return false;
}

void CVGuiDialogPanel::ChooseOption(int index)
{
    if (index < 0 || index >= m_Options.Count())
        return;
    vgui::Button *b = m_Options[index];
    if (!b->IsVisible() || !b->IsEnabled())
    {
        RPMUI::Sound(RPMUI::SND_DENY);
        return;
    }
    RPMUI::Sound(RPMUI::SND_CLICK);
    b->DoClick();
}

// keys: 1-9 answer, up / down + enter, space / enter continue, Esc closes
void CVGuiDialogPanel::OnKeyCodeTyped(vgui::KeyCode code)
{
    if (code == KEY_ESCAPE)
    {
        EndDialog();
        engine->ClientCmd_Unrestricted("gameui_hide\n");
        engine->ClientCmd_Unrestricted("wait;gameui_hide\n");
        return;
    }
    bool optionsShown = m_flOptionsTime > 0.0f;
    if (code >= KEY_1 && code <= KEY_9)
    {
        if (optionsShown)
            ChooseOption(code - KEY_1);
        else
            ContinueOrReveal();
        return;
    }
    if ((code == KEY_UP || code == KEY_DOWN) && optionsShown && m_Options.Count())
    {
        int n = m_Options.Count();
        int sel = HighlightedOption();
        for (int step = 0; step < n; step++)
        {
            sel = (sel < 0) ? (code == KEY_DOWN ? 0 : n - 1) : (sel + (code == KEY_DOWN ? 1 : n - 1)) % n;
            if (m_Options[sel]->IsEnabled())
                break;
        }
        m_iKeySel = sel;
        // keep it in view
        int y = sel * m_iRowH;
        if (y < m_nOptionsOffset)
            m_nOptionsOffset = y;
        else if (y + m_iRowH > m_nOptionsOffset + m_iOptViewH)
            m_nOptionsOffset = y + m_iRowH - m_iOptViewH;
        PlaceOptions();
        return;
    }
    if (code == KEY_ENTER || code == KEY_PAD_ENTER || code == KEY_SPACE)
    {
        if (optionsShown && m_iKeySel >= 0)
            ChooseOption(m_iKeySel);
        else
            ContinueOrReveal();
        return;
    }
    BaseClass::OnKeyCodeTyped(code);
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

// HL2RPM: the keys of the dialog window from the console (tests): dialog_key 1..9 | up | down | enter | esc
static void __Cmd_Dialog_Key(const CCommand &args)
{
    if (args.ArgC() < 2 || !s_pDialogPanel || !s_pDialogPanel->IsVisible())
        return;
    const char *k = args.Arg(1);
    vgui::KeyCode code = KEY_NONE;
    if (k[0] >= '1' && k[0] <= '9' && !k[1])
        code = (vgui::KeyCode)(KEY_1 + (k[0] - '1'));
    else if (!Q_stricmp(k, "up")) code = KEY_UP;
    else if (!Q_stricmp(k, "down")) code = KEY_DOWN;
    else if (!Q_stricmp(k, "enter")) code = KEY_ENTER;
    else if (!Q_stricmp(k, "space")) code = KEY_SPACE;
    else if (!Q_stricmp(k, "esc")) code = KEY_ESCAPE;
    if (code != KEY_NONE)
        s_pDialogPanel->SimulateKey(code);
}
static ConCommand dialog_key("dialog_key", __Cmd_Dialog_Key, "HL2RPM: press a key of the dialog window (tests): 1-9 | up | down | enter | space | esc", FCVAR_CLIENTDLL);

// HL2RPM: whether a dialog with an NPC is on screen (the depth of field focuses on the NPC
// then: deferred_postfx.cpp)
bool HL2RPM_IsDialogOpen()
{
    return s_pDialogPanel != nullptr && s_pDialogPanel->IsVisible();
}

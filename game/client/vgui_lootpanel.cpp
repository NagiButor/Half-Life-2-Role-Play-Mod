#include "cbase.h"
#include "vgui_lootpanel.h"
#include "vgui_inventorypanel.h"
#include <vgui_controls/Panel.h>
#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/AnimationController.h>
#include "clientmode.h"
#include <stdarg.h>

using namespace vgui;

static const char *g_pszLootPanelBuild = __DATE__ " " __TIME__;
static ConVar cl_lootpanel_debug("cl_lootpanel_debug", "0", FCVAR_CLIENTDLL, "Enable loot panel DevMsg debugging");
static ConVar cl_lootpanel_debug_keys("cl_lootpanel_debug_keys", "1", FCVAR_CLIENTDLL, "Log loot panel keyboard input");
static ConVar cl_lootpanel_debug_mouse("cl_lootpanel_debug_mouse", "1", FCVAR_CLIENTDLL, "Log loot panel mouse hover and focus");
static ConVar cl_lootpanel_debug_paint("cl_lootpanel_debug_paint", "0", FCVAR_CLIENTDLL, "Log loot panel paint/highlight decisions");
static ConVar cl_lootpanel_debug_throttle("cl_lootpanel_debug_throttle", "0.10", FCVAR_CLIENTDLL, "Min seconds between periodic loot panel debug prints");

static vgui::KeyCode g_LootPanelLastPressedCode = KEY_NONE;
static float g_LootPanelLastPressedTime = -9999.0f;

static void LootDbgMsg(const char *fmt, ...)
{
    if (!cl_lootpanel_debug.GetBool())
        return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    V_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    DevMsg("%s", buf);
}

static void PlayLocalUISound(const char *soundName, int channel, float volume)
{
    if (!soundName || !soundName[0])
        return;

    C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
    if (!pPlayer)
    {
        vgui::surface()->PlaySound(soundName);
        return;
    }

    EmitSound_t params;
    params.m_pSoundName = soundName;
    params.m_flVolume = volume;
    params.m_nChannel = channel;
    params.m_SoundLevel = SNDLVL_NONE;

    CLocalPlayerFilter filter;
    C_BaseEntity::EmitSound(filter, -1, params);
}

enum
{
    CHAN_LOOT_PANEL = CHAN_USER_BASE + 110,
    CHAN_LOOT_CLICK_ITEM = CHAN_USER_BASE + 111,
    CHAN_LOOT_CLICK_ACTION = CHAN_USER_BASE + 112,
};

// Helper: map internal classnames to friendly labels and compute ammo bullet counts
static void GetFriendlyLabelAndCount(const char *classname, int pickupCount, bool hasExplicitBullets, char *out, int outSize)
{
    if (!classname || !classname[0])
    {
        Q_snprintf(out, outSize, "(unknown)");
        return;
    }

    if (!Q_stricmp(classname, "item_healthkit") || !Q_stricmp(classname, "item_healthvial"))
    {
        if (pickupCount > 1)
            Q_snprintf(out, outSize, "Healthkit x%d", pickupCount);
        else
            Q_snprintf(out, outSize, "Healthkit");
        return;
    }

    if (!Q_strnicmp(classname, "weapon_", 7))
    {
        const char *p = classname + 7;
        char buf[256];
        int bi = 0;
        bool cap = true;
        for (; *p && bi < (int)sizeof(buf)-1; ++p)
        {
            char c = *p;
            if (c == '_') { buf[bi++] = ' '; cap = true; }
            else
            {
                if (cap && c >= 'a' && c <= 'z') { buf[bi++] = c - ('a' - 'A'); cap = false; }
                else { buf[bi++] = c; cap = false; }
            }
        }
        buf[bi] = '\0';
        if (pickupCount > 1)
            Q_snprintf(out, outSize, "%s x%d", buf, pickupCount);
        else
            Q_snprintf(out, outSize, "%s", buf);
        return;
    }

    if (!Q_strnicmp(classname, "item_", 5))
    {
        const char *p = classname + 5;
        char buf[256];
        int bi = 0;
        bool cap = true;
        for (; *p && bi < (int)sizeof(buf)-1; ++p)
        {
            char c = *p;
            if (c == '_') { buf[bi++] = ' '; cap = true; }
            else
            {
                if (cap && c >= 'a' && c <= 'z') { buf[bi++] = c - ('a' - 'A'); cap = false; }
                else { buf[bi++] = c; cap = false; }
            }
        }
        buf[bi] = '\0';
        if (pickupCount > 1)
            Q_snprintf(out, outSize, "%s x%d", buf, pickupCount);
        else
            Q_snprintf(out, outSize, "%s", buf);
        return;
    }

    if (pickupCount > 1)
        Q_snprintf(out, outSize, "%s x%d", classname, pickupCount);
    else
        Q_snprintf(out, outSize, "%s", classname);
}

static void NormalizeClassnameClient(const char *raw, char *out, int outSize)
{
    if (!out || outSize <= 0)
        return;
    out[0] = '\0';
    if (!raw)
        return;
    int o = 0;
    for (const char *p = raw; *p && o < outSize - 1; ++p)
    {
        char c = *p;
        if (c == '\\' && p[1])
        {
            c = *++p;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
        {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

static void ExtractLootEntrySummary(const char *raw, char *cls, int clsSize, int &count, bool &hasExplicit)
{
    count = 1;
    hasExplicit = false;
    if (!cls || clsSize <= 0)
        return;
    cls[0] = '\0';
    if (!raw || !raw[0])
        return;
    char buf[512];
    Q_strncpy(buf, raw, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    char *tok[6];
    int ntok = 0;
    char *p = buf;
    tok[ntok++] = p;
    for (; *p && ntok < 6; ++p)
    {
        if (*p == ':')
        {
            *p = '\0';
            tok[ntok++] = p + 1;
        }
    }

    NormalizeClassnameClient(tok[0], cls, clsSize);
    if (ntok >= 2)
    {
        count = atoi(tok[1]);
        hasExplicit = true;
    }
}

static void ExtractClassnameFromRaw(const char *raw, char *cls, int clsSize)
{
    int count = 1;
    bool hasExplicit = false;
    ExtractLootEntrySummary(raw, cls, clsSize, count, hasExplicit);
}

static void EscapeClassname(const char *cls, CUtlString &out)
{
    out.Clear();
    for (const char *p = cls; p && *p; ++p)
    {
        if (*p == '"' || *p == '\\') out.Append("\\");
        char tmp[2] = { *p, '\0' };
        out.Append(tmp);
    }
}

static bool IsPointInsidePanel(Panel *p, int x, int y)
{
    if (!p || !p->IsVisible())
        return false;
    int sx = 0, sy = 0;
    p->LocalToScreen(sx, sy);
    int pw = 0, ph = 0;
    p->GetSize(pw, ph);
    return (x >= sx && x < sx + pw && y >= sy && y < sy + ph);
}

static CVGuiLootPanel *s_pLootPanel = nullptr;

class CInvOptionButton : public Button
{
public:
    CInvOptionButton(Panel *parent, const char *panelName, const char *text, int column = 0, int idx = -1, CVGuiLootPanel *owner = nullptr)
        : Button(parent, panelName, text), m_nColumn(column), m_nIndex(idx), m_pOwner(owner), m_bDbgInit(false), m_flDbgNext(0.0f)
    {
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h; GetSize(w, h);
        bool selected = false;
        bool suppressArmed = false;
        CVGuiLootPanel *pOwner = m_pOwner;
        if (pOwner)
        {
            if (m_nIndex >= 0)
            {
                selected = (m_nColumn == 0) ? pOwner->IsLeftItemSelected(m_nIndex) : pOwner->IsRightItemSelected(m_nIndex);
                suppressArmed = pOwner->AreActionsVisibleForItem(m_nColumn, m_nIndex);
                if (selected && pOwner->AreActionsVisibleForColumn(m_nColumn))
                    suppressArmed = true;
            }
            else if (m_nIndex == -100)
            {
                selected = (m_nColumn == 0) ? pOwner->IsLeftPrimaryActionSelected() : pOwner->IsRightPrimaryActionSelected();
            }
            else if (m_nIndex == -101)
            {
                selected = (m_nColumn == 0) ? pOwner->IsLeftSecondaryActionSelected() : pOwner->IsRightSecondaryActionSelected();
            }
            else if (m_nIndex == -1)
            {
                selected = (m_nColumn == 0) ? pOwner->IsPutAllSelected() : pOwner->IsTakeAllSelected();
            }
        }

        bool depressed = IsDepressed();
        bool armed = IsArmed();
        bool willFillRed = (m_nIndex >= 0)
            ? (!suppressArmed && (depressed || armed || selected))
            : (depressed || armed || selected);

        if (cl_lootpanel_debug.GetBool() && cl_lootpanel_debug_paint.GetBool())
        {
            float now = gpGlobals ? gpGlobals->curtime : 0.0f;
            float throttle = cl_lootpanel_debug_throttle.GetFloat();
            if (throttle < 0.0f)
                throttle = 0.0f;

            int mx = 0, my = 0;
            vgui::input()->GetCursorPos(mx, my);
            bool hovered = IsPointInsidePanel(this, mx, my);

            bool interesting = (m_nIndex < 0) || hovered || selected || suppressArmed;
            bool changed = !m_bDbgInit ||
                (m_bDbgLastHovered != hovered) ||
                (m_bDbgLastSelected != selected) ||
                (m_bDbgLastSuppress != suppressArmed) ||
                (m_bDbgLastArmed != armed) ||
                (m_bDbgLastDepressed != depressed) ||
                (m_bDbgLastFillRed != willFillRed);

            if (interesting && (changed || now >= m_flDbgNext))
            {
                vgui::VPANEL focus = vgui::input()->GetFocus();
                vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
                bool anyActions = pOwner ? pOwner->AreAnyActionsVisible() : false;
                LootDbgMsg("[LootPanel][PAINT] col=%d idx=%d hover=%d sel=%d sup=%d armed=%d dep=%d red=%d anyActions=%d focus=%p self=%p modal=%p\n",
                    m_nColumn, m_nIndex,
                    hovered ? 1 : 0, selected ? 1 : 0, suppressArmed ? 1 : 0,
                    armed ? 1 : 0, depressed ? 1 : 0, willFillRed ? 1 : 0,
                    anyActions ? 1 : 0,
                    (void*)focus, (void*)GetVPanel(), (void*)modal);
                m_bDbgInit = true;
                m_bDbgLastHovered = hovered;
                m_bDbgLastSelected = selected;
                m_bDbgLastSuppress = suppressArmed;
                m_bDbgLastArmed = armed;
                m_bDbgLastDepressed = depressed;
                m_bDbgLastFillRed = willFillRed;
                m_flDbgNext = now + throttle;
            }
        }

        if (willFillRed)
        {
            surface()->DrawSetColor(180, 50, 40, 200);
            surface()->DrawFilledRect(0, 0, w, h);
        }
        surface()->DrawSetColor(255, 200, 30, 255);
        surface()->DrawOutlinedRect(0, 0, w-1, h-1);
    }

    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255,200,30,255));
        Button::Paint();
    }

private:
    int m_nColumn;
    int m_nIndex;
    CVGuiLootPanel *m_pOwner;
    bool m_bDbgInit;
    bool m_bDbgLastHovered;
    bool m_bDbgLastSelected;
    bool m_bDbgLastSuppress;
    bool m_bDbgLastArmed;
    bool m_bDbgLastDepressed;
    bool m_bDbgLastFillRed;
    float m_flDbgNext;
};

class CConfirmCloseButton : public Button
{
public:
    CConfirmCloseButton(Panel *parent, const char *panelName, const char *text)
        : Button(parent, panelName, text)
    {
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
        SetFgColor(Color(255,255,255,255));
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h; GetSize(w, h);
        surface()->DrawSetColor(200, 40, 40, 255);
        surface()->DrawFilledRect(0, 0, w, h);
    }

    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255,255,255,255));
        Button::Paint();
    }
};

CVGuiLootPanel::CVGuiLootPanel(Panel *parent) : BaseClass(parent, "LootPanel")
{
    SetVisible(false);
    SetProportional(false);
    SetTitle("", false);
    SetMenuButtonVisible(false);
    SetCloseButtonVisible(false);
    SetTitleBarVisible(false);
    SetSizeable(false);
    SetMoveable(false);
    SetBgColor(Color(0,0,0,230));
    SetFgColor(Color(255,200,30,255));

    int sx, sy; surface()->GetScreenSize(sx, sy);
    int w = Max(700, (sx*8)/10);
    int h = Max(420, (sy*6)/10);
    SetSize(w, h);
    SetPos((sx - w)/2, (sy - h)/2);

    m_pTitleLabel = new Label(this, "Title", "");
    m_pTitleLabel->SetBounds(12, 8, w-24, 20);
    m_pTitleLabel->SetContentAlignment(Label::a_center);
    m_pTitleLabel->SetPaintBackgroundEnabled(false);
    m_pTitleLabel->SetFgColor(Color(255,200,30,255));

    m_pLeftLabel = new Label(this, "LeftLabel", "Your inventory");
    m_pLeftLabel->SetBounds(12, 32, (w/2)-24, 20);
    m_pLeftLabel->SetContentAlignment(Label::a_center);
    m_pLeftLabel->SetPaintBackgroundEnabled(false);
    m_pLeftLabel->SetFgColor(Color(255,200,30,255));

    m_pRightLabel = new Label(this, "RightLabel", "NPC's inventory");
    m_pRightLabel->SetBounds((w/2)+12, 32, (w/2)-24, 20);
    m_pRightLabel->SetContentAlignment(Label::a_center);
    m_pRightLabel->SetPaintBackgroundEnabled(false);
    m_pRightLabel->SetFgColor(Color(255,200,30,255));

    m_pLeftListPanel = nullptr;
    m_pRightListPanel = nullptr;

    m_pLeftActionPrimary = new CInvOptionButton(this, "LootLeftPrimary", "Put", 0, -100, this);
    m_pLeftActionSecondary = new CInvOptionButton(this, "LootLeftSecondary", "Drop", 0, -101, this);
    m_pRightActionPrimary = new CInvOptionButton(this, "LootRightPrimary", "Take", 1, -100, this);
    m_pRightActionSecondary = new CInvOptionButton(this, "LootRightSecondary", "Drop", 1, -101, this);

    m_pLeftActionPrimary->SetVisible(false);
    m_pLeftActionSecondary->SetVisible(false);
    m_pRightActionPrimary->SetVisible(false);
    m_pRightActionSecondary->SetVisible(false);

    m_pLeftActionPrimary->SetEnabled(false);
    m_pLeftActionSecondary->SetEnabled(false);
    m_pRightActionPrimary->SetEnabled(false);
    m_pRightActionSecondary->SetEnabled(false);

    m_pLeftActionPrimary->SetMouseInputEnabled(false);
    m_pLeftActionSecondary->SetMouseInputEnabled(false);
    m_pRightActionPrimary->SetMouseInputEnabled(false);
    m_pRightActionSecondary->SetMouseInputEnabled(false);
    m_pLeftActionPrimary->SetKeyBoardInputEnabled(false);
    m_pLeftActionSecondary->SetKeyBoardInputEnabled(false);
    m_pRightActionPrimary->SetKeyBoardInputEnabled(false);
    m_pRightActionSecondary->SetKeyBoardInputEnabled(false);

    m_pPutAll = new CInvOptionButton(this, "PutAll", "Put all", 0, -1, this);
    m_pPutAll->SetCommand("loot_put_all");
    m_pPutAll->SetKeyBoardInputEnabled(false);

    m_pTakeAll = new CInvOptionButton(this, "TakeAll", "Take all", 1, -1, this);
    m_pTakeAll->SetCommand("loot_take_all");
    m_pTakeAll->SetKeyBoardInputEnabled(false);

    m_nSelectedLeftIndex = -1;
    m_nSelectedRightIndex = -1;
    m_nFocusedColumn = 0;
    m_nActionColumn = -1;
    m_nActionIndex = -1;
    m_nSelectedAction = 0;
    m_nLeftContentOffset = 0;
    m_nRightContentOffset = 0;
    m_nFooterSelection = -1;
    m_bSelectionVisible = false;
    m_bPendingModalRelease = false;
    m_flModalReleaseTime = 0.0f;
    m_DisplayName = "";
    m_SoundOpen = "";
    m_SoundClose = "";

    // Add a small red close button top-right
    CConfirmCloseButton *pClose = new CConfirmCloseButton(this, "LootClose", "X");
    pClose->SetBounds(w - 32, 4, 28, 24);
    pClose->SetCommand("loot_close");
    pClose->SetKeyBoardInputEnabled(false);

}

void CVGuiLootPanel::ScheduleModalRelease(float delay)
{
    m_bPendingModalRelease = true;
    m_flModalReleaseTime = gpGlobals ? (gpGlobals->curtime + delay) : 0.0f;
    vgui::ivgui()->AddTickSignal(GetVPanel());
}

void CVGuiLootPanel::ApplySchemeSettings(vgui::IScheme *scheme)
{
    BaseClass::ApplySchemeSettings(scheme);
    if (m_pTitleLabel)
    {
        m_pTitleLabel->SetFgColor(Color(255,200,30,255));
        m_pTitleLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pLeftLabel)
    {
        m_pLeftLabel->SetFgColor(Color(255,200,30,255));
        m_pLeftLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pRightLabel)
    {
        m_pRightLabel->SetFgColor(Color(255,200,30,255));
        m_pRightLabel->SetContentAlignment(Label::a_center);
    }
}

CVGuiLootPanel *GetGlobalLootPanel()
{
    if (!s_pLootPanel)
    {
        DevMsg("[LootPanel] GetGlobalLootPanel create build=%s\n", g_pszLootPanelBuild);
        s_pLootPanel = new CVGuiLootPanel(NULL);
    }
    return s_pLootPanel;
}

void CVGuiLootPanel::BeginLoot()
{
    DevMsg("[LootPanel] BeginLoot build=%s\n", g_pszLootPanelBuild);
    bool wasVisible = IsVisible();
    for (int i = 0; i < m_LeftButtons.Count(); ++i) delete m_LeftButtons[i];
    for (int i = 0; i < m_RightButtons.Count(); ++i) delete m_RightButtons[i];
    m_LeftButtons.RemoveAll(); m_RightButtons.RemoveAll();

    HideActionButtons();
    m_nSelectedLeftIndex = -1;
    m_nSelectedRightIndex = -1;
    m_nFocusedColumn = 0;
    m_nActionColumn = -1;
    m_nActionIndex = -1;
    m_nSelectedAction = 0;
    m_nLeftContentOffset = 0;
    m_nRightContentOffset = 0;
    m_nFooterSelection = -1;
    m_bSelectionVisible = false;

    if (!GetParent())
    {
        Panel *parent = (Panel*)g_pClientMode->GetViewport();
        if (parent) SetParent(parent);
    }
    MakePopup(); SetVisible(true); SetZPos(1000); MoveToFront(); RequestFocus();
    engine->ClientCmd_Unrestricted("gameui_preventescapetoshow\n");
    vgui::input()->SetAppModalSurface(GetVPanel());
    SetKeyBoardInputEnabled(true); SetMouseInputEnabled(true);
    m_bPendingModalRelease = false;
    vgui::ivgui()->AddTickSignal(GetVPanel());
    InvalidateLayout(true); Repaint();
    DebugAuto("BeginLoot");
    if (!wasVisible)
    {
        const char *snd = m_SoundOpen.Get()[0] ? m_SoundOpen.Get() : "npc\\combine_soldier\\zipline_hitground2.wav";
        PlayLocalUISound(snd, CHAN_LOOT_PANEL, 0.25f);
    }

    int pw, ph; GetSize(pw, ph);
    int colW = (pw - 48) / 2; // left and right column widths
    int leftX = 12;
    int rightX = (pw/2) + 12;
    int y = 0;
    int btnH = 34;
    int btnW = colW - 12;

    int listTop = 64;
    int listBottomPad = 12;
    int bottomButtonsH = 34;
    int bottomButtonsGap = 12;
    int listHeight = ph - listTop - (bottomButtonsH + bottomButtonsGap + listBottomPad);
    if (listHeight < 64)
        listHeight = 64;

    if (!m_pLeftListPanel)
    {
        m_pLeftListPanel = new Panel(this, "LeftListPanel");
        m_pLeftListPanel->SetPaintBackgroundEnabled(false);
    }
    if (!m_pRightListPanel)
    {
        m_pRightListPanel = new Panel(this, "RightListPanel");
        m_pRightListPanel->SetPaintBackgroundEnabled(false);
    }
    m_pLeftListPanel->SetKeyBoardInputEnabled(false);
    m_pRightListPanel->SetKeyBoardInputEnabled(false);
    m_pLeftListPanel->SetBounds(leftX, listTop, btnW, listHeight);
    m_pRightListPanel->SetBounds(rightX, listTop, btnW, listHeight);

    if (m_pLeftActionPrimary && m_pLeftActionPrimary->GetParent() != m_pLeftListPanel)
        m_pLeftActionPrimary->SetParent(m_pLeftListPanel);
    if (m_pLeftActionSecondary && m_pLeftActionSecondary->GetParent() != m_pLeftListPanel)
        m_pLeftActionSecondary->SetParent(m_pLeftListPanel);
    if (m_pRightActionPrimary && m_pRightActionPrimary->GetParent() != m_pRightListPanel)
        m_pRightActionPrimary->SetParent(m_pRightListPanel);
    if (m_pRightActionSecondary && m_pRightActionSecondary->GetParent() != m_pRightListPanel)
        m_pRightActionSecondary->SetParent(m_pRightListPanel);
    if (m_pLeftActionPrimary)
        m_pLeftActionPrimary->AddActionSignalTarget(this);
    if (m_pLeftActionSecondary)
        m_pLeftActionSecondary->AddActionSignalTarget(this);
    if (m_pRightActionPrimary)
        m_pRightActionPrimary->AddActionSignalTarget(this);
    if (m_pRightActionSecondary)
        m_pRightActionSecondary->AddActionSignalTarget(this);

    // Left items (player inventory) - 'Put' action
    for (int i = 0; i < m_LeftItems.Count(); ++i)
    {
        const char *raw = m_LeftItems[i].Get();
        char disp[256]; char cls[201]; int cnt = 1; bool hasExplicit = false;
        ExtractLootEntrySummary(raw, cls, sizeof(cls), cnt, hasExplicit);
        GetFriendlyLabelAndCount(cls, cnt, hasExplicit, disp, sizeof(disp));

        char namebuf[64]; Q_snprintf(namebuf, sizeof(namebuf), "LootLeftItem%d", i);
        CInvOptionButton *btn = new CInvOptionButton(m_pLeftListPanel, namebuf, disp, 0, i, this);
        btn->SetKeyBoardInputEnabled(false);
        btn->SetBounds(0, y, btnW, btnH);
        char cmdbuf[64]; Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_select_left %d", i);
        btn->SetCommand(cmdbuf);
        btn->AddActionSignalTarget(this);
        m_LeftButtons.AddToTail(btn);
        y += btnH + 8;
    }

    // Right items (npc inventory) - 'Take' action
    y = 0;
    for (int i = 0; i < m_RightItems.Count(); ++i)
    {
        const char *raw = m_RightItems[i].Get();
        char disp[256]; char cls[201]; int cnt = 1; bool hasExplicit = false;
        ExtractLootEntrySummary(raw, cls, sizeof(cls), cnt, hasExplicit);
        GetFriendlyLabelAndCount(cls, cnt, hasExplicit, disp, sizeof(disp));

        char namebuf[64]; Q_snprintf(namebuf, sizeof(namebuf), "LootRightItem%d", i);
        CInvOptionButton *btn = new CInvOptionButton(m_pRightListPanel, namebuf, disp, 1, i, this);
        btn->SetKeyBoardInputEnabled(false);
        btn->SetBounds(0, y, btnW, btnH);
        char cmdbuf[64]; Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_select_right %d", i);
        btn->SetCommand(cmdbuf);
        btn->AddActionSignalTarget(this);
        m_RightButtons.AddToTail(btn);
        y += btnH + 8;
    }

    // Bottom action buttons
    int bw = 140, bh = 34;
    if (m_pPutAll)
    {
        bool enable = (m_LeftButtons.Count() > 0);
        m_pPutAll->SetBounds( (pw/4) - (bw/2), ph - bh - 12, bw, bh );
        m_pPutAll->SetVisible(enable);
        m_pPutAll->SetEnabled(enable);
        if (enable)
            m_pPutAll->AddActionSignalTarget(this);
    }

    if (m_pTakeAll)
    {
        bool enable = (m_RightButtons.Count() > 0);
        m_pTakeAll->SetBounds( (3*(pw/4)) - (bw/2), ph - bh - 12, bw, bh );
        m_pTakeAll->SetVisible(enable);
        m_pTakeAll->SetEnabled(enable);
        if (enable)
            m_pTakeAll->AddActionSignalTarget(this);
    }

    ClampOffsets();
    RepositionLeftColumn();
    RepositionRightColumn();

    if (m_LeftButtons.Count() > 0)
        m_nFocusedColumn = 0;
    else if (m_RightButtons.Count() > 0)
        m_nFocusedColumn = 1;
    m_nSelectedLeftIndex = -1;
    m_nSelectedRightIndex = -1;
    m_nFooterSelection = -1;
    UpdateArmedState();
    RepaintSelection();
}

void CVGuiLootPanel::SelectItem(int column, int idx)
{
    m_bSelectionVisible = true;
    HideActionButtons();
    m_nFooterSelection = -1;

    if (column == 0)
    {
        m_nFocusedColumn = 0;
        if (m_LeftButtons.Count() <= 0)
        {
            m_nSelectedLeftIndex = -1;
            return;
        }
        m_nSelectedLeftIndex = clamp(idx, 0, m_LeftButtons.Count() - 1);
        EnsureLeftSelectionVisible();
        RepaintSelection();
        return;
    }

    m_nFocusedColumn = 1;
    if (m_RightButtons.Count() <= 0)
    {
        m_nSelectedRightIndex = -1;
        return;
    }
    m_nSelectedRightIndex = clamp(idx, 0, m_RightButtons.Count() - 1);
    EnsureRightSelectionVisible();
    RepaintSelection();
}

void CVGuiLootPanel::SetActionSelection(int action)
{
    if (action < 0) action = 0;
    if (action > 1) action = 1;
    m_nSelectedAction = action;
    RepaintSelection();
}

void CVGuiLootPanel::ShowActions(int column, int idx)
{
    SetActionSelection(m_nSelectedAction);
    m_nFooterSelection = -1;
    if (column == 0)
    {
        if (idx < 0 || idx >= m_LeftButtons.Count())
            return;
        m_nFocusedColumn = 0;
        m_nSelectedLeftIndex = idx;
        EnsureLeftSelectionVisible();
        m_nActionColumn = 0;
        m_nActionIndex = idx;
        ShowLeftActions(idx);
        RepaintSelection();
        return;
    }

    if (idx < 0 || idx >= m_RightButtons.Count())
        return;
    m_nFocusedColumn = 1;
    m_nSelectedRightIndex = idx;
    EnsureRightSelectionVisible();
    m_nActionColumn = 1;
    m_nActionIndex = idx;
    ShowRightActions(idx);
    RepaintSelection();
}

void CVGuiLootPanel::PerformSelectedAction()
{
    if (m_nActionColumn < 0 || m_nActionIndex < 0)
        return;

    char cmdbuf[64];
    if (m_nActionColumn == 0)
    {
        if (m_nActionIndex >= m_LeftItems.Count())
            return;
        if (m_nSelectedAction == 1)
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_drop_left %d", m_nActionIndex);
        else
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_put %d", m_nActionIndex);
    }
    else
    {
        if (m_nActionIndex >= m_RightItems.Count())
            return;
        if (m_nSelectedAction == 1)
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_drop_right %d", m_nActionIndex);
        else
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_take %d", m_nActionIndex);
    }
    OnCommand(cmdbuf);
}

void CVGuiLootPanel::ShowLeftActions(int idx)
{
    if (!m_pLeftActionPrimary || !m_pLeftActionSecondary)
        return;
    if (idx < 0 || idx >= m_LeftButtons.Count())
        return;

    int x, y, w, h;
    m_LeftButtons[idx]->GetBounds(x, y, w, h);

    int actionW = 80;
    int actionH = 24;
    int gap = 6;
    int totalW = (actionW * 2) + gap;
    int actionX = x + w - totalW - 4;
    int actionY = y + (h - actionH) / 2;

    char cmdbuf[64];
    Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_put %d", idx);
    m_pLeftActionPrimary->SetCommand(cmdbuf);
    Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_drop_left %d", idx);
    m_pLeftActionSecondary->SetCommand(cmdbuf);

    m_pLeftActionPrimary->SetVisible(true);
    m_pLeftActionSecondary->SetVisible(true);
    m_pLeftActionPrimary->SetEnabled(true);
    m_pLeftActionSecondary->SetEnabled(true);
    m_pLeftActionPrimary->SetMouseInputEnabled(true);
    m_pLeftActionSecondary->SetMouseInputEnabled(true);
    m_pLeftActionPrimary->MoveToFront();
    m_pLeftActionSecondary->MoveToFront();

    int startX1 = actionX - 10;
    int startX2 = actionX + actionW + gap - 10;
    m_pLeftActionPrimary->SetBounds(startX1, actionY, actionW, actionH);
    m_pLeftActionSecondary->SetBounds(startX2, actionY, actionW, actionH);
    m_pLeftActionPrimary->SetAlpha(0);
    m_pLeftActionSecondary->SetAlpha(0);

    AnimationController *anim = g_pClientMode ? g_pClientMode->GetViewportAnimationController() : nullptr;
    if (anim)
    {
        anim->RunAnimationCommand(m_pLeftActionPrimary, "XPos", actionX, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pLeftActionSecondary, "XPos", actionX + actionW + gap, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pLeftActionPrimary, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pLeftActionSecondary, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
    }
    else
    {
        m_pLeftActionPrimary->SetBounds(actionX, actionY, actionW, actionH);
        m_pLeftActionSecondary->SetBounds(actionX + actionW + gap, actionY, actionW, actionH);
        m_pLeftActionPrimary->SetAlpha(255);
        m_pLeftActionSecondary->SetAlpha(255);
    }

    m_nSelectedLeftIndex = idx;
    m_nFocusedColumn = 0;
    m_nActionColumn = 0;
    m_nActionIndex = idx;
    if (m_nSelectedAction < 0 || m_nSelectedAction > 1)
        m_nSelectedAction = 0;
    m_bSelectionVisible = true;
}

void CVGuiLootPanel::ShowRightActions(int idx)
{
    if (!m_pRightActionPrimary || !m_pRightActionSecondary)
        return;
    if (idx < 0 || idx >= m_RightButtons.Count())
        return;

    int x, y, w, h;
    m_RightButtons[idx]->GetBounds(x, y, w, h);

    int actionW = 80;
    int actionH = 24;
    int gap = 6;
    int totalW = (actionW * 2) + gap;
    int actionX = x + w - totalW - 4;
    int actionY = y + (h - actionH) / 2;

    char cmdbuf[64];
    Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_take %d", idx);
    m_pRightActionPrimary->SetCommand(cmdbuf);
    Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_drop_right %d", idx);
    m_pRightActionSecondary->SetCommand(cmdbuf);

    m_pRightActionPrimary->SetVisible(true);
    m_pRightActionSecondary->SetVisible(true);
    m_pRightActionPrimary->SetEnabled(true);
    m_pRightActionSecondary->SetEnabled(true);
    m_pRightActionPrimary->SetMouseInputEnabled(true);
    m_pRightActionSecondary->SetMouseInputEnabled(true);
    m_pRightActionPrimary->MoveToFront();
    m_pRightActionSecondary->MoveToFront();

    int startX1 = actionX - 10;
    int startX2 = actionX + actionW + gap - 10;
    m_pRightActionPrimary->SetBounds(startX1, actionY, actionW, actionH);
    m_pRightActionSecondary->SetBounds(startX2, actionY, actionW, actionH);
    m_pRightActionPrimary->SetAlpha(0);
    m_pRightActionSecondary->SetAlpha(0);

    AnimationController *anim = g_pClientMode ? g_pClientMode->GetViewportAnimationController() : nullptr;
    if (anim)
    {
        anim->RunAnimationCommand(m_pRightActionPrimary, "XPos", actionX, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pRightActionSecondary, "XPos", actionX + actionW + gap, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pRightActionPrimary, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pRightActionSecondary, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
    }
    else
    {
        m_pRightActionPrimary->SetBounds(actionX, actionY, actionW, actionH);
        m_pRightActionSecondary->SetBounds(actionX + actionW + gap, actionY, actionW, actionH);
        m_pRightActionPrimary->SetAlpha(255);
        m_pRightActionSecondary->SetAlpha(255);
    }

    m_nSelectedRightIndex = idx;
    m_nFocusedColumn = 1;
    m_nActionColumn = 1;
    m_nActionIndex = idx;
    if (m_nSelectedAction < 0 || m_nSelectedAction > 1)
        m_nSelectedAction = 0;
    m_bSelectionVisible = true;
}

void CVGuiLootPanel::HideLeftActions()
{
    if (!m_pLeftActionPrimary || !m_pLeftActionSecondary)
        return;
    m_pLeftActionPrimary->SetVisible(false);
    m_pLeftActionSecondary->SetVisible(false);
    m_pLeftActionPrimary->SetEnabled(false);
    m_pLeftActionSecondary->SetEnabled(false);
    m_pLeftActionPrimary->SetMouseInputEnabled(false);
    m_pLeftActionSecondary->SetMouseInputEnabled(false);
    m_pLeftActionPrimary->SetAlpha(0);
    m_pLeftActionSecondary->SetAlpha(0);
    if (m_nActionColumn == 0)
    {
        m_nActionColumn = -1;
        m_nActionIndex = -1;
        m_nSelectedAction = 0;
    }
}

void CVGuiLootPanel::HideRightActions()
{
    if (!m_pRightActionPrimary || !m_pRightActionSecondary)
        return;
    m_pRightActionPrimary->SetVisible(false);
    m_pRightActionSecondary->SetVisible(false);
    m_pRightActionPrimary->SetEnabled(false);
    m_pRightActionSecondary->SetEnabled(false);
    m_pRightActionPrimary->SetMouseInputEnabled(false);
    m_pRightActionSecondary->SetMouseInputEnabled(false);
    m_pRightActionPrimary->SetAlpha(0);
    m_pRightActionSecondary->SetAlpha(0);
    if (m_nActionColumn == 1)
    {
        m_nActionColumn = -1;
        m_nActionIndex = -1;
        m_nSelectedAction = 0;
    }
}

void CVGuiLootPanel::HideActionButtons()
{
    bool wasAny = AreAnyActionsVisible();
    if (wasAny)
        DebugAuto("HideActionsPre");
    HideLeftActions();
    HideRightActions();
    m_nActionColumn = -1;
    m_nActionIndex = -1;
    m_nSelectedAction = 0;
    RepaintSelection();
    if (wasAny)
        DebugAuto("HideActionsPost");
}

bool CVGuiLootPanel::IsLeftItemSelected(int idx)
{
    return IsVisible() && (m_nFooterSelection < 0) && (m_nFocusedColumn == 0) && (idx >= 0) && (idx == m_nSelectedLeftIndex);
}

bool CVGuiLootPanel::IsRightItemSelected(int idx)
{
    return IsVisible() && (m_nFooterSelection < 0) && (m_nFocusedColumn == 1) && (idx >= 0) && (idx == m_nSelectedRightIndex);
}

bool CVGuiLootPanel::IsLeftPrimaryActionSelected()
{
    return IsVisible() && (m_nActionColumn == 0) && (m_nSelectedAction == 0);
}

bool CVGuiLootPanel::IsLeftSecondaryActionSelected()
{
    return IsVisible() && (m_nActionColumn == 0) && (m_nSelectedAction == 1);
}

bool CVGuiLootPanel::IsRightPrimaryActionSelected()
{
    return IsVisible() && (m_nActionColumn == 1) && (m_nSelectedAction == 0);
}

bool CVGuiLootPanel::IsRightSecondaryActionSelected()
{
    return IsVisible() && (m_nActionColumn == 1) && (m_nSelectedAction == 1);
}

bool CVGuiLootPanel::IsPutAllSelected()
{
    return IsVisible() && (m_nFooterSelection == 0);
}

bool CVGuiLootPanel::IsTakeAllSelected()
{
    return IsVisible() && (m_nFooterSelection == 1);
}

bool CVGuiLootPanel::AreActionsVisibleForItem(int column, int idx)
{
    if (!IsVisible())
        return false;
    if (idx < 0)
        return false;

    if (column == 0)
    {
        if (idx >= m_LeftItems.Count())
            return false;
        if (m_nActionColumn != 0 || m_nActionIndex != idx)
            return false;
        return (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) || (m_pLeftActionSecondary && m_pLeftActionSecondary->IsVisible());
    }

    if (column == 1)
    {
        if (idx >= m_RightItems.Count())
            return false;
        if (m_nActionColumn != 1 || m_nActionIndex != idx)
            return false;
        return (m_pRightActionPrimary && m_pRightActionPrimary->IsVisible()) || (m_pRightActionSecondary && m_pRightActionSecondary->IsVisible());
    }

    return false;
}

bool CVGuiLootPanel::AreAnyActionsVisible()
{
    if (!IsVisible())
        return false;
    return (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) ||
           (m_pLeftActionSecondary && m_pLeftActionSecondary->IsVisible()) ||
           (m_pRightActionPrimary && m_pRightActionPrimary->IsVisible()) ||
           (m_pRightActionSecondary && m_pRightActionSecondary->IsVisible());
}

bool CVGuiLootPanel::AreActionsVisibleForColumn(int column)
{
    if (!IsVisible())
        return false;
    if (column == 0)
        return (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) || (m_pLeftActionSecondary && m_pLeftActionSecondary->IsVisible());
    if (column == 1)
        return (m_pRightActionPrimary && m_pRightActionPrimary->IsVisible()) || (m_pRightActionSecondary && m_pRightActionSecondary->IsVisible());
    return false;
}

void CVGuiLootPanel::DebugAuto(const char *tag)
{
    if (!cl_lootpanel_debug.GetBool())
        return;

    float now = gpGlobals ? gpGlobals->curtime : 0.0f;
    vgui::VPANEL focus = vgui::input()->GetFocus();
    vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
    LootDbgMsg("[LootPanel][AUTO] t=%.3f tag=%s vis=%d kb=%d mouse=%d focus=%p self=%p modal=%p anyActions=%d focusCol=%d actionCol=%d selAct=%d selLIdx=%d selRIdx=%d\n",
        now, tag ? tag : "(null)",
        IsVisible() ? 1 : 0,
        IsKeyBoardInputEnabled() ? 1 : 0,
        IsMouseInputEnabled() ? 1 : 0,
        (void*)focus, (void*)GetVPanel(), (void*)modal,
        AreAnyActionsVisible() ? 1 : 0,
        m_nFocusedColumn, m_nActionColumn, m_nSelectedAction, m_nSelectedLeftIndex, m_nSelectedRightIndex);

    DebugPaintDump(true);
}

void CVGuiLootPanel::DebugDump()
{
    int mx = 0, my = 0;
    vgui::input()->GetCursorPos(mx, my);
    int lx = mx, ly = my;
    ScreenToLocal(lx, ly);

    vgui::VPANEL focus = vgui::input()->GetFocus();
    vgui::VPANEL modal = vgui::input()->GetAppModalSurface();

    int hoverCol = -1;
    int hoverIdx = -1;
    if (IsPointInsidePanel(m_pLeftListPanel, mx, my))
    {
        hoverCol = 0;
        for (int i = 0; i < m_LeftButtons.Count(); ++i)
        {
            if (IsPointInsidePanel(m_LeftButtons[i], mx, my))
            {
                hoverIdx = i;
                break;
            }
        }
    }
    else if (IsPointInsidePanel(m_pRightListPanel, mx, my))
    {
        hoverCol = 1;
        for (int i = 0; i < m_RightButtons.Count(); ++i)
        {
            if (IsPointInsidePanel(m_RightButtons[i], mx, my))
            {
                hoverIdx = i;
                break;
            }
        }
    }

    DevMsg("[LootPanel][DUMP] build=%s\n", g_pszLootPanelBuild);
    DevMsg("[LootPanel][DUMP] visible=%d kb=%d mouse=%d focus=%p self=%p modal=%p\n",
        IsVisible() ? 1 : 0,
        IsKeyBoardInputEnabled() ? 1 : 0,
        IsMouseInputEnabled() ? 1 : 0,
        (void*)focus, (void*)GetVPanel(), (void*)modal);
    DevMsg("[LootPanel][DUMP] cursor_screen=%d,%d cursor_local=%d,%d hoverCol=%d hoverIdx=%d\n",
        mx, my, lx, ly, hoverCol, hoverIdx);
    DevMsg("[LootPanel][DUMP] leftCount=%d rightCount=%d selVis=%d focusCol=%d actionCol=%d selAction=%d\n",
        m_LeftButtons.Count(), m_RightButtons.Count(),
        m_bSelectionVisible ? 1 : 0, m_nFocusedColumn, m_nActionColumn, m_nSelectedAction);
    DevMsg("[LootPanel][DUMP] actionIdx=%d selLeftIdx=%d selRightIdx=%d offL=%d offR=%d\n",
        m_nActionIndex, m_nSelectedLeftIndex, m_nSelectedRightIndex, m_nLeftContentOffset, m_nRightContentOffset);
    DevMsg("[LootPanel][DUMP] anyActions=%d leftActions(vis=%d,%d) rightActions(vis=%d,%d)\n",
        AreAnyActionsVisible() ? 1 : 0,
        (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) ? 1 : 0,
        (m_pLeftActionSecondary && m_pLeftActionSecondary->IsVisible()) ? 1 : 0,
        (m_pRightActionPrimary && m_pRightActionPrimary->IsVisible()) ? 1 : 0,
        (m_pRightActionSecondary && m_pRightActionSecondary->IsVisible()) ? 1 : 0);
}

void CVGuiLootPanel::DebugPaintDump(bool force)
{
    if (!cl_lootpanel_debug.GetBool() || !cl_lootpanel_debug_paint.GetBool() || !IsVisible())
        return;

    static float s_flNext = 0.0f;
    static int s_lastFocusCol = -999999;
    static int s_lastActionCol = -999999;
    static int s_lastSelLIdx = -999999;
    static int s_lastSelRIdx = -999999;
    static int s_lastSelAct = -999999;
    static int s_lastMainRed = -999999;
    static int s_lastMainSup = -999999;
    static int s_lastMainSel = -999999;
    static int s_lastMainArmed = -999999;
    static int s_lastMainDep = -999999;
    static vgui::VPANEL s_lastFocus = (vgui::VPANEL)0;
    static vgui::VPANEL s_lastModal = (vgui::VPANEL)0;

    float now = gpGlobals ? gpGlobals->curtime : 0.0f;
    float throttle = cl_lootpanel_debug_throttle.GetFloat();
    if (throttle < 0.0f)
        throttle = 0.0f;

    if (!force && now < s_flNext)
        return;

    int mx = 0, my = 0;
    vgui::input()->GetCursorPos(mx, my);
    int lx = mx, ly = my;
    ScreenToLocal(lx, ly);

    vgui::VPANEL focus = vgui::input()->GetFocus();
    vgui::VPANEL modal = vgui::input()->GetAppModalSurface();

    int hoverCol = -1;
    int hoverIdx = -1;
    if (IsPointInsidePanel(m_pLeftListPanel, mx, my))
    {
        hoverCol = 0;
        for (int i = 0; i < m_LeftButtons.Count(); ++i)
        {
            if (IsPointInsidePanel(m_LeftButtons[i], mx, my))
            {
                hoverIdx = i;
                break;
            }
        }
    }
    else if (IsPointInsidePanel(m_pRightListPanel, mx, my))
    {
        hoverCol = 1;
        for (int i = 0; i < m_RightButtons.Count(); ++i)
        {
            if (IsPointInsidePanel(m_RightButtons[i], mx, my))
            {
                hoverIdx = i;
                break;
            }
        }
    }

    int mainCol = m_nFocusedColumn;
    int mainIdx = (mainCol == 0) ? m_nSelectedLeftIndex : m_nSelectedRightIndex;

    vgui::Button *pMain = nullptr;
    if (mainCol == 0 && mainIdx >= 0 && mainIdx < m_LeftButtons.Count())
        pMain = m_LeftButtons[mainIdx];
    else if (mainCol == 1 && mainIdx >= 0 && mainIdx < m_RightButtons.Count())
        pMain = m_RightButtons[mainIdx];

    bool anyActions = AreAnyActionsVisible();
    bool mainSelected = false;
    bool mainSuppress = false;
    bool mainArmed = false;
    bool mainDep = false;
    bool mainRed = false;
    if (pMain)
    {
        mainSelected = (mainCol == 0) ? IsLeftItemSelected(mainIdx) : IsRightItemSelected(mainIdx);
        mainSuppress = AreActionsVisibleForItem(mainCol, mainIdx);
        if (!mainSuppress && mainSelected && anyActions)
            mainSuppress = true;
        mainArmed = pMain->IsArmed();
        mainDep = pMain->IsDepressed();
        mainRed = mainDep || (!mainSuppress && mainArmed) || (!mainSuppress && mainSelected);
    }

    bool changed =
        force ||
        s_lastFocusCol != m_nFocusedColumn ||
        s_lastActionCol != m_nActionColumn ||
        s_lastSelLIdx != m_nSelectedLeftIndex ||
        s_lastSelRIdx != m_nSelectedRightIndex ||
        s_lastSelAct != m_nSelectedAction ||
        s_lastMainRed != (mainRed ? 1 : 0) ||
        s_lastMainSup != (mainSuppress ? 1 : 0) ||
        s_lastMainSel != (mainSelected ? 1 : 0) ||
        s_lastMainArmed != (mainArmed ? 1 : 0) ||
        s_lastMainDep != (mainDep ? 1 : 0) ||
        s_lastFocus != focus ||
        s_lastModal != modal;

    if (!changed)
        return;

    LootDbgMsg("[LootPanel][PAINTTICK] t=%.3f screen=%d,%d local=%d,%d hoverCol=%d hoverIdx=%d focus=%p self=%p modal=%p\n",
        now, mx, my, lx, ly, hoverCol, hoverIdx, (void*)focus, (void*)GetVPanel(), (void*)modal);
    LootDbgMsg("[LootPanel][PAINTTICK] anyActions=%d focusCol=%d actionCol=%d selAct=%d selLIdx=%d selRIdx=%d\n",
        anyActions ? 1 : 0, m_nFocusedColumn, m_nActionColumn, m_nSelectedAction, m_nSelectedLeftIndex, m_nSelectedRightIndex);
    LootDbgMsg("[LootPanel][PAINTTICK] main col=%d idx=%d exists=%d sel=%d sup=%d armed=%d dep=%d red=%d\n",
        mainCol, mainIdx, pMain ? 1 : 0, mainSelected ? 1 : 0, mainSuppress ? 1 : 0, mainArmed ? 1 : 0, mainDep ? 1 : 0, mainRed ? 1 : 0);
    LootDbgMsg("[LootPanel][PAINTTICK] leftActions vis=%d,%d armed=%d,%d dep=%d,%d sel=%d,%d\n",
        (m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible()) ? 1 : 0,
        (m_pLeftActionSecondary && m_pLeftActionSecondary->IsVisible()) ? 1 : 0,
        (m_pLeftActionPrimary && m_pLeftActionPrimary->IsArmed()) ? 1 : 0,
        (m_pLeftActionSecondary && m_pLeftActionSecondary->IsArmed()) ? 1 : 0,
        (m_pLeftActionPrimary && m_pLeftActionPrimary->IsDepressed()) ? 1 : 0,
        (m_pLeftActionSecondary && m_pLeftActionSecondary->IsDepressed()) ? 1 : 0,
        IsLeftPrimaryActionSelected() ? 1 : 0, IsLeftSecondaryActionSelected() ? 1 : 0);
    LootDbgMsg("[LootPanel][PAINTTICK] rightActions vis=%d,%d armed=%d,%d dep=%d,%d sel=%d,%d\n",
        (m_pRightActionPrimary && m_pRightActionPrimary->IsVisible()) ? 1 : 0,
        (m_pRightActionSecondary && m_pRightActionSecondary->IsVisible()) ? 1 : 0,
        (m_pRightActionPrimary && m_pRightActionPrimary->IsArmed()) ? 1 : 0,
        (m_pRightActionSecondary && m_pRightActionSecondary->IsArmed()) ? 1 : 0,
        (m_pRightActionPrimary && m_pRightActionPrimary->IsDepressed()) ? 1 : 0,
        (m_pRightActionSecondary && m_pRightActionSecondary->IsDepressed()) ? 1 : 0,
        IsRightPrimaryActionSelected() ? 1 : 0, IsRightSecondaryActionSelected() ? 1 : 0);

    s_flNext = now + throttle;
    s_lastFocusCol = m_nFocusedColumn;
    s_lastActionCol = m_nActionColumn;
    s_lastSelLIdx = m_nSelectedLeftIndex;
    s_lastSelRIdx = m_nSelectedRightIndex;
    s_lastSelAct = m_nSelectedAction;
    s_lastMainRed = mainRed ? 1 : 0;
    s_lastMainSup = mainSuppress ? 1 : 0;
    s_lastMainSel = mainSelected ? 1 : 0;
    s_lastMainArmed = mainArmed ? 1 : 0;
    s_lastMainDep = mainDep ? 1 : 0;
    s_lastFocus = focus;
    s_lastModal = modal;
}

void CVGuiLootPanel::ClampOffsets()
{
    const int btnH = 34;
    const int gap = 8;

    if (m_pLeftListPanel)
    {
        int vw = 0, vh = 0;
        m_pLeftListPanel->GetSize(vw, vh);
        int contentH = m_LeftButtons.Count() > 0 ? (m_LeftButtons.Count() * (btnH + gap) - gap) : 0;
        int maxOff = Max(0, contentH - vh);
        m_nLeftContentOffset = clamp(m_nLeftContentOffset, 0, maxOff);
    }
    else
    {
        m_nLeftContentOffset = 0;
    }

    if (m_pRightListPanel)
    {
        int vw = 0, vh = 0;
        m_pRightListPanel->GetSize(vw, vh);
        int contentH = m_RightButtons.Count() > 0 ? (m_RightButtons.Count() * (btnH + gap) - gap) : 0;
        int maxOff = Max(0, contentH - vh);
        m_nRightContentOffset = clamp(m_nRightContentOffset, 0, maxOff);
    }
    else
    {
        m_nRightContentOffset = 0;
    }
}

void CVGuiLootPanel::RepositionLeftColumn()
{
    if (!m_pLeftListPanel)
        return;

    const int btnH = 34;
    const int gap = 8;

    int vw = 0, vh = 0;
    m_pLeftListPanel->GetSize(vw, vh);
    int y = -m_nLeftContentOffset;
    for (int i = 0; i < m_LeftButtons.Count(); ++i)
    {
        if (m_LeftButtons[i])
            m_LeftButtons[i]->SetBounds(0, y, vw, btnH);
        y += btnH + gap;
    }
}

void CVGuiLootPanel::RepositionRightColumn()
{
    if (!m_pRightListPanel)
        return;

    const int btnH = 34;
    const int gap = 8;

    int vw = 0, vh = 0;
    m_pRightListPanel->GetSize(vw, vh);
    int y = -m_nRightContentOffset;
    for (int i = 0; i < m_RightButtons.Count(); ++i)
    {
        if (m_RightButtons[i])
            m_RightButtons[i]->SetBounds(0, y, vw, btnH);
        y += btnH + gap;
    }
}

void CVGuiLootPanel::EnsureLeftSelectionVisible()
{
    if (!m_pLeftListPanel || m_nSelectedLeftIndex < 0 || m_nSelectedLeftIndex >= m_LeftButtons.Count())
        return;

    const int btnH = 34;
    const int gap = 8;
    int vw = 0, vh = 0;
    m_pLeftListPanel->GetSize(vw, vh);

    int top = m_nSelectedLeftIndex * (btnH + gap);
    int bottom = top + btnH;
    if (top < m_nLeftContentOffset)
        m_nLeftContentOffset = top;
    else if (bottom > (m_nLeftContentOffset + vh))
        m_nLeftContentOffset = bottom - vh;

    ClampOffsets();
    RepositionLeftColumn();
}

void CVGuiLootPanel::EnsureRightSelectionVisible()
{
    if (!m_pRightListPanel || m_nSelectedRightIndex < 0 || m_nSelectedRightIndex >= m_RightButtons.Count())
        return;

    const int btnH = 34;
    const int gap = 8;
    int vw = 0, vh = 0;
    m_pRightListPanel->GetSize(vw, vh);

    int top = m_nSelectedRightIndex * (btnH + gap);
    int bottom = top + btnH;
    if (top < m_nRightContentOffset)
        m_nRightContentOffset = top;
    else if (bottom > (m_nRightContentOffset + vh))
        m_nRightContentOffset = bottom - vh;

    ClampOffsets();
    RepositionRightColumn();
}

void CVGuiLootPanel::RepaintSelection()
{
    UpdateArmedState();
    for (int i = 0; i < m_LeftButtons.Count(); ++i)
    {
        if (m_LeftButtons[i])
            m_LeftButtons[i]->Repaint();
    }
    for (int i = 0; i < m_RightButtons.Count(); ++i)
    {
        if (m_RightButtons[i])
            m_RightButtons[i]->Repaint();
    }
    if (m_pLeftActionPrimary) m_pLeftActionPrimary->Repaint();
    if (m_pLeftActionSecondary) m_pLeftActionSecondary->Repaint();
    if (m_pRightActionPrimary) m_pRightActionPrimary->Repaint();
    if (m_pRightActionSecondary) m_pRightActionSecondary->Repaint();
    if (m_pPutAll) m_pPutAll->Repaint();
    if (m_pTakeAll) m_pTakeAll->Repaint();
    Repaint();
}

void CVGuiLootPanel::UpdateArmedState()
{
    for (int i = 0; i < m_LeftButtons.Count(); ++i)
    {
        if (m_LeftButtons[i])
        {
            bool suppress = (m_nFooterSelection >= 0) ||
                AreActionsVisibleForItem(0, i) ||
                (AreActionsVisibleForColumn(0) && (i == m_nSelectedLeftIndex));
            m_LeftButtons[i]->SetArmed(!suppress && (m_nFocusedColumn == 0) && (i == m_nSelectedLeftIndex));
        }
    }
    for (int i = 0; i < m_RightButtons.Count(); ++i)
    {
        if (m_RightButtons[i])
        {
            bool suppress = (m_nFooterSelection >= 0) ||
                AreActionsVisibleForItem(1, i) ||
                (AreActionsVisibleForColumn(1) && (i == m_nSelectedRightIndex));
            m_RightButtons[i]->SetArmed(!suppress && (m_nFocusedColumn == 1) && (i == m_nSelectedRightIndex));
        }
    }

    if (m_pLeftActionPrimary)
        m_pLeftActionPrimary->SetArmed((m_nActionColumn == 0) && (m_nSelectedAction == 0));
    if (m_pLeftActionSecondary)
        m_pLeftActionSecondary->SetArmed((m_nActionColumn == 0) && (m_nSelectedAction == 1));
    if (m_pRightActionPrimary)
        m_pRightActionPrimary->SetArmed((m_nActionColumn == 1) && (m_nSelectedAction == 0));
    if (m_pRightActionSecondary)
        m_pRightActionSecondary->SetArmed((m_nActionColumn == 1) && (m_nSelectedAction == 1));
    if (m_pPutAll)
        m_pPutAll->SetArmed(m_nFooterSelection == 0);
    if (m_pTakeAll)
        m_pTakeAll->SetArmed(m_nFooterSelection == 1);
}

void CVGuiLootPanel::SetItems(const CUtlVector<CUtlString> &leftItems, const CUtlVector<CUtlString> &rightItems)
{
    m_LeftItems.RemoveAll(); m_RightItems.RemoveAll();
    for (int i = 0; i < leftItems.Count(); ++i) m_LeftItems.AddToTail(leftItems[i]);
    for (int i = 0; i < rightItems.Count(); ++i) m_RightItems.AddToTail(rightItems[i]);
    BeginLoot();
}

void CVGuiLootPanel::EndLoot()
{
    bool wasVisible = IsVisible();
    HideActionButtons();
    if (m_pPutAll) m_pPutAll->SetVisible(false);
    if (m_pTakeAll) m_pTakeAll->SetVisible(false);
    SetVisible(false);
    if (!m_bPendingModalRelease)
        ScheduleModalRelease(0.0f);
    engine->ClientCmd_Unrestricted("gameui_allowescapetoshow\n");
    SetKeyBoardInputEnabled(false);
    SetMouseInputEnabled(false);
    if (wasVisible)
    {
        const char *snd = m_SoundClose.Get()[0] ? m_SoundClose.Get() : "npc\\combine_soldier\\zipline_hitground1.wav";
        PlayLocalUISound(snd, CHAN_LOOT_PANEL, 0.25f);
    }
}

void CVGuiLootPanel::OnTick()
{
    if (cl_lootpanel_debug.GetBool() && cl_lootpanel_debug_mouse.GetBool() && IsVisible())
    {
        static float s_flNext = 0.0f;
        static int s_lastMx = -999999;
        static int s_lastMy = -999999;
        static int s_lastHoverCol = -999999;
        static int s_lastHoverIdx = -999999;
        static vgui::VPANEL s_lastFocus = (vgui::VPANEL)0;
        static vgui::VPANEL s_lastModal = (vgui::VPANEL)0;

        float now = gpGlobals ? gpGlobals->curtime : 0.0f;
        float throttle = cl_lootpanel_debug_throttle.GetFloat();
        if (throttle < 0.0f)
            throttle = 0.0f;

        int mx = 0, my = 0;
        vgui::input()->GetCursorPos(mx, my);
        int lx = mx, ly = my;
        ScreenToLocal(lx, ly);

        vgui::VPANEL focus = vgui::input()->GetFocus();
        vgui::VPANEL modal = vgui::input()->GetAppModalSurface();

        int hoverCol = -1;
        int hoverIdx = -1;
        if (IsPointInsidePanel(m_pLeftListPanel, mx, my))
        {
            hoverCol = 0;
            for (int i = 0; i < m_LeftButtons.Count(); ++i)
            {
                if (IsPointInsidePanel(m_LeftButtons[i], mx, my))
                {
                    hoverIdx = i;
                    break;
                }
            }
        }
        else if (IsPointInsidePanel(m_pRightListPanel, mx, my))
        {
            hoverCol = 1;
            for (int i = 0; i < m_RightButtons.Count(); ++i)
            {
                if (IsPointInsidePanel(m_RightButtons[i], mx, my))
                {
                    hoverIdx = i;
                    break;
                }
            }
        }

        bool timeHit = (now >= s_flNext);
        bool posChanged = (mx != s_lastMx) || (my != s_lastMy);
        bool hoverChanged = (hoverCol != s_lastHoverCol) || (hoverIdx != s_lastHoverIdx);
        bool focusChanged = (focus != s_lastFocus) || (modal != s_lastModal);

        if (timeHit || posChanged || hoverChanged || focusChanged)
        {
            LootDbgMsg("[LootPanel][MOUSE] t=%.3f screen=%d,%d local=%d,%d hoverCol=%d hoverIdx=%d focus=%p self=%p modal=%p anyActions=%d actionCol=%d focusCol=%d selLIdx=%d selRIdx=%d\n",
                now, mx, my, lx, ly, hoverCol, hoverIdx,
                (void*)focus, (void*)GetVPanel(), (void*)modal,
                AreAnyActionsVisible() ? 1 : 0, m_nActionColumn, m_nFocusedColumn, m_nSelectedLeftIndex, m_nSelectedRightIndex);
            s_flNext = now + throttle;
            s_lastMx = mx;
            s_lastMy = my;
            s_lastHoverCol = hoverCol;
            s_lastHoverIdx = hoverIdx;
            s_lastFocus = focus;
            s_lastModal = modal;
        }
    }

    DebugPaintDump(false);

    if (m_bPendingModalRelease && gpGlobals && gpGlobals->curtime >= m_flModalReleaseTime)
    {
        vgui::input()->SetAppModalSurface(NULL);
        m_bPendingModalRelease = false;
    }

    if (!IsVisible() && !m_bPendingModalRelease)
        vgui::ivgui()->RemoveTickSignal(GetVPanel());
}

void CVGuiLootPanel::SetContext(const char *displayName, const char *soundOpen, const char *soundClose)
{
    m_DisplayName = (displayName && displayName[0]) ? displayName : "";
    m_SoundOpen = (soundOpen && soundOpen[0]) ? soundOpen : "";
    m_SoundClose = (soundClose && soundClose[0]) ? soundClose : "";

    if (m_pRightLabel)
    {
        if (m_DisplayName.Get()[0])
            m_pRightLabel->SetText(m_DisplayName.Get());
        else
            m_pRightLabel->SetText("NPC's inventory");
    }
}

void CVGuiLootPanel::PaintBackground()
{
    int w,h; GetSize(w,h);
    Color c = GetBgColor();
    surface()->DrawSetColor(c.r(), c.g(), c.b(), c.a());
    surface()->DrawFilledRect(0,0,w,h);
    surface()->DrawSetColor(60,60,60,200);
    surface()->DrawOutlinedRect(0,0,w-1,h-1);

    surface()->DrawSetColor(255,200,30,255);
    int mid = w / 2;
    surface()->DrawFilledRect(mid, 40, mid + 1, h - 40);
}

void CVGuiLootPanel::OnCommand(const char *command)
{
    RequestFocus();
    SetKeyBoardInputEnabled(true);

    if (cl_lootpanel_debug.GetBool())
    {
        int mx = 0, my = 0;
        vgui::input()->GetCursorPos(mx, my);
        vgui::VPANEL focus = vgui::input()->GetFocus();
        vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
        LootDbgMsg("[LootPanel][CMD][IN] cmd=\"%s\" mx=%d my=%d focus=%p self=%p modal=%p focusCol=%d actionCol=%d selAct=%d selLIdx=%d selRIdx=%d\n",
            command ? command : "(null)", mx, my, (void*)focus, (void*)GetVPanel(), (void*)modal,
            m_nFocusedColumn, m_nActionColumn, m_nSelectedAction, m_nSelectedLeftIndex, m_nSelectedRightIndex);
    }

    if (!Q_strnicmp(command, "loot_select_left ", 17))
    {
        int idx = atoi(command + 17);
        if (m_nActionColumn == 0 && m_nActionIndex == idx && m_pLeftActionPrimary && m_pLeftActionPrimary->IsVisible())
        {
            HideActionButtons();
            LootDbgMsg("[LootPanel][CMD] loot_select_left toggle_off idx=%d\n", idx);
            DebugAuto("SelectLeftToggleOff");
            return;
        }
        PlayLocalUISound("buttonclickrelease.wav", CHAN_LOOT_CLICK_ITEM, 0.16f);
        SelectItem(0, idx);
        SetActionSelection(0);
        ShowActions(0, m_nSelectedLeftIndex);
        LootDbgMsg("[LootPanel][CMD] loot_select_left idx=%d\n", idx);
        DebugAuto("SelectLeft");
        return;
    }

    if (!Q_strnicmp(command, "loot_select_right ", 18))
    {
        int idx = atoi(command + 18);
        if (m_nActionColumn == 1 && m_nActionIndex == idx && m_pRightActionPrimary && m_pRightActionPrimary->IsVisible())
        {
            HideActionButtons();
            LootDbgMsg("[LootPanel][CMD] loot_select_right toggle_off idx=%d\n", idx);
            DebugAuto("SelectRightToggleOff");
            return;
        }
        PlayLocalUISound("buttonclickrelease.wav", CHAN_LOOT_CLICK_ITEM, 0.16f);
        SelectItem(1, idx);
        SetActionSelection(0);
        ShowActions(1, m_nSelectedRightIndex);
        LootDbgMsg("[LootPanel][CMD] loot_select_right idx=%d\n", idx);
        DebugAuto("SelectRight");
        return;
    }

    if (!Q_strnicmp(command, "loot_put ", 9))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 9);
        LootDbgMsg("[LootPanel][CMD] loot_put idx=%d\n", idx);
        if (idx >= 0 && idx < m_LeftItems.Count())
        {
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_put_slot %d\n", idx);
            engine->ClientCmd(cmdbuf);
        }
        HideLeftActions();
        return;
    }

    if (!Q_strnicmp(command, "loot_take ", 10))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 10);
        LootDbgMsg("[LootPanel][CMD] loot_take idx=%d\n", idx);
        if (idx >= 0 && idx < m_RightItems.Count())
        {
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_take_slot %d\n", idx);
            engine->ClientCmd(cmdbuf);
        }
        HideRightActions();
        return;
    }

    if (!Q_strnicmp(command, "loot_drop_left ", 15))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 15);
        LootDbgMsg("[LootPanel][CMD] loot_drop_left idx=%d\n", idx);
        if (idx >= 0 && idx < m_LeftItems.Count())
        {
            const char *raw = m_LeftItems[idx].Get();
            char cls[256];
            ExtractClassnameFromRaw(raw, cls, sizeof(cls));
            CUtlString esc;
            EscapeClassname(cls, esc);
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_drop \"%s\"\n", esc.Get());
            engine->ClientCmd(cmdbuf);
        }
        HideLeftActions();
        return;
    }

    if (!Q_strnicmp(command, "loot_drop_right ", 16))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 16);
        LootDbgMsg("[LootPanel][CMD] loot_drop_right idx=%d\n", idx);
        if (idx >= 0 && idx < m_RightItems.Count())
        {
            const char *raw = m_RightItems[idx].Get();
            char cls[256];
            ExtractClassnameFromRaw(raw, cls, sizeof(cls));
            CUtlString esc;
            EscapeClassname(cls, esc);
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "loot_take_slot %d\n", idx);
            engine->ClientCmd(cmdbuf);
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_drop \"%s\"\n", esc.Get());
            engine->ClientCmd(cmdbuf);
        }
        HideRightActions();
        return;
    }

    if (!Q_stricmp(command, "loot_put_all"))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        LootDbgMsg("[LootPanel][CMD] loot_put_all\n");
        HideActionButtons();
        engine->ClientCmd("loot_put_all\n");
        return;
    }
    if (!Q_stricmp(command, "loot_take_all"))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_LOOT_CLICK_ACTION, 0.14f);
        LootDbgMsg("[LootPanel][CMD] loot_take_all\n");
        HideActionButtons();
        engine->ClientCmd("loot_take_all\n");
        return;
    }
    if (!Q_stricmp(command, "loot_close"))
    {
        LootDbgMsg("[LootPanel][CMD] loot_close\n");
        EndLoot();
        return;
    }

    LootDbgMsg("[LootPanel][CMD] pass_to_base cmd=\"%s\"\n", command ? command : "(null)");
    BaseClass::OnCommand(command);
}

void CVGuiLootPanel::OnMousePressed(MouseCode code)
{
    int mx, my;
    input()->GetCursorPos(mx, my);

    if (cl_lootpanel_debug.GetBool())
    {
        const char *hit = "none";
        if (IsPointInsidePanel(m_pLeftActionPrimary, mx, my) || IsPointInsidePanel(m_pLeftActionSecondary, mx, my) ||
            IsPointInsidePanel(m_pRightActionPrimary, mx, my) || IsPointInsidePanel(m_pRightActionSecondary, mx, my))
            hit = "action";
        else if (IsPointInsidePanel(m_pPutAll, mx, my) || IsPointInsidePanel(m_pTakeAll, mx, my))
            hit = "bottom";
        else if (IsPointInsidePanel(m_pLeftListPanel, mx, my))
            hit = "left_list";
        else if (IsPointInsidePanel(m_pRightListPanel, mx, my))
            hit = "right_list";
        vgui::VPANEL focus = vgui::input()->GetFocus();
        vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
        LootDbgMsg("[LootPanel][MOUSE][Pressed] code=%d mx=%d my=%d hit=%s focus=%p self=%p modal=%p focusCol=%d actionCol=%d selLIdx=%d selRIdx=%d\n",
            (int)code, mx, my, hit, (void*)focus, (void*)GetVPanel(), (void*)modal,
            m_nFocusedColumn, m_nActionColumn, m_nSelectedLeftIndex, m_nSelectedRightIndex);
    }

    if (IsPointInsidePanel(m_pLeftActionPrimary, mx, my) || IsPointInsidePanel(m_pLeftActionSecondary, mx, my) ||
        IsPointInsidePanel(m_pRightActionPrimary, mx, my) || IsPointInsidePanel(m_pRightActionSecondary, mx, my) ||
        IsPointInsidePanel(m_pPutAll, mx, my) || IsPointInsidePanel(m_pTakeAll, mx, my))
    {
        BaseClass::OnMousePressed(code);
        RequestFocus();
        return;
    }

    for (int i = 0; i < m_LeftButtons.Count(); ++i)
    {
        if (IsPointInsidePanel(m_LeftButtons[i], mx, my))
        {
            BaseClass::OnMousePressed(code);
            RequestFocus();
            return;
        }
    }

    for (int i = 0; i < m_RightButtons.Count(); ++i)
    {
        if (IsPointInsidePanel(m_RightButtons[i], mx, my))
        {
            BaseClass::OnMousePressed(code);
            RequestFocus();
            return;
        }
    }

    HideActionButtons();
    BaseClass::OnMousePressed(code);
    RequestFocus();
}

void CVGuiLootPanel::OnMouseWheeled(int delta)
{
    int mx, my;
    input()->GetCursorPos(mx, my);

    const int step = 60;
    int d = (delta > 0) ? -step : step;

    bool overLeft = IsPointInsidePanel(m_pLeftListPanel, mx, my);
    bool overRight = IsPointInsidePanel(m_pRightListPanel, mx, my);
    if (!overLeft && !overRight)
    {
        if (m_nFocusedColumn == 0)
            overLeft = true;
        else
            overRight = true;
    }

    HideActionButtons();
    m_bSelectionVisible = false;

    LootDbgMsg("[LootPanel][MOUSE][Wheel] delta=%d mx=%d my=%d overLeft=%d overRight=%d focusCol=%d offL=%d offR=%d\n",
        delta, mx, my, overLeft ? 1 : 0, overRight ? 1 : 0, m_nFocusedColumn, m_nLeftContentOffset, m_nRightContentOffset);

    if (overLeft)
    {
        m_nLeftContentOffset += d;
        ClampOffsets();
        RepositionLeftColumn();
        LootDbgMsg("[LootPanel][MOUSE][Wheel][After] side=left offL=%d\n", m_nLeftContentOffset);
    }
    else if (overRight)
    {
        m_nRightContentOffset += d;
        ClampOffsets();
        RepositionRightColumn();
        LootDbgMsg("[LootPanel][MOUSE][Wheel][After] side=right offR=%d\n", m_nRightContentOffset);
    }
}

void CVGuiLootPanel::OnKeyCodePressed(vgui::KeyCode code)
{
    g_LootPanelLastPressedCode = code;
    g_LootPanelLastPressedTime = gpGlobals ? gpGlobals->curtime : 0.0f;

    auto dump = [&](const char *stage)
    {
        if (!cl_lootpanel_debug.GetBool() || !cl_lootpanel_debug_keys.GetBool())
            return;
        vgui::VPANEL focus = vgui::input()->GetFocus();
        vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
        LootDbgMsg("[LootPanel][KEY]%s code=%d vis=%d kb=%d mouse=%d focus=%p self=%p modal=%p focusCol=%d actionCol=%d selAct=%d selVis=%d selLIdx=%d selRIdx=%d offL=%d offR=%d\n",
            stage, (int)code,
            IsVisible() ? 1 : 0, IsKeyBoardInputEnabled() ? 1 : 0, IsMouseInputEnabled() ? 1 : 0,
            (void*)focus, (void*)GetVPanel(), (void*)modal,
            m_nFocusedColumn, m_nActionColumn, m_nSelectedAction, m_bSelectionVisible ? 1 : 0,
            m_nSelectedLeftIndex, m_nSelectedRightIndex,
            m_nLeftContentOffset, m_nRightContentOffset);
    };

    dump("[Pressed][IN]");
    if (code == KEY_W || code == KEY_A || code == KEY_S || code == KEY_D || code == KEY_SPACE)
    {
        dump("[Pressed][IGNORED_WASD]");
        return;
    }

    if (m_nFooterSelection >= 0)
    {
        bool putAvail = (m_pPutAll && m_pPutAll->IsVisible() && m_pPutAll->IsEnabled());
        bool takeAvail = (m_pTakeAll && m_pTakeAll->IsVisible() && m_pTakeAll->IsEnabled());

        if (code == KEY_LEFT || code == KEY_RIGHT)
        {
            if (putAvail && takeAvail)
            {
                m_nFooterSelection = (m_nFooterSelection == 0) ? 1 : 0;
            }
            RepaintSelection();
            InvalidateLayout();
            return;
        }

        if (code == KEY_UP)
        {
            m_nFooterSelection = -1;
            if (m_nFocusedColumn == 0 && m_LeftButtons.Count() > 0)
                SelectItem(0, (m_nSelectedLeftIndex < 0) ? 0 : m_nSelectedLeftIndex);
            else if (m_nFocusedColumn == 1 && m_RightButtons.Count() > 0)
                SelectItem(1, (m_nSelectedRightIndex < 0) ? 0 : m_nSelectedRightIndex);
            else if (m_LeftButtons.Count() > 0)
                SelectItem(0, 0);
            else if (m_RightButtons.Count() > 0)
                SelectItem(1, 0);
            InvalidateLayout();
            Repaint();
            return;
        }

        if (code == KEY_ENTER || code == KEY_PAD_ENTER)
        {
            if (m_nFooterSelection == 0 && putAvail)
                OnCommand("loot_put_all");
            else if (m_nFooterSelection == 1 && takeAvail)
                OnCommand("loot_take_all");
            InvalidateLayout();
            Repaint();
            return;
        }

        if (code == KEY_DOWN)
            return;
    }

    if (code == KEY_UP || code == KEY_DOWN)
    {
        int dir = (code == KEY_UP) ? -1 : 1;
        bool putAvail = (m_pPutAll && m_pPutAll->IsVisible() && m_pPutAll->IsEnabled());
        bool takeAvail = (m_pTakeAll && m_pTakeAll->IsVisible() && m_pTakeAll->IsEnabled());

        if (code == KEY_DOWN)
        {
            if (m_nFocusedColumn == 0)
            {
                if (m_LeftButtons.Count() == 0 && (putAvail || takeAvail))
                {
                    m_nFooterSelection = putAvail ? 0 : 1;
                    HideActionButtons();
                    RepaintSelection();
                    InvalidateLayout();
                    return;
                }
                if (m_LeftButtons.Count() > 0 && m_nSelectedLeftIndex >= (m_LeftButtons.Count() - 1) && (putAvail || takeAvail))
                {
                    m_nFooterSelection = putAvail ? 0 : 1;
                    HideActionButtons();
                    RepaintSelection();
                    InvalidateLayout();
                    return;
                }
            }
            else
            {
                if (m_RightButtons.Count() == 0 && (putAvail || takeAvail))
                {
                    m_nFooterSelection = takeAvail ? 1 : 0;
                    HideActionButtons();
                    RepaintSelection();
                    InvalidateLayout();
                    return;
                }
                if (m_RightButtons.Count() > 0 && m_nSelectedRightIndex >= (m_RightButtons.Count() - 1) && (putAvail || takeAvail))
                {
                    m_nFooterSelection = takeAvail ? 1 : 0;
                    HideActionButtons();
                    RepaintSelection();
                    InvalidateLayout();
                    return;
                }
            }
        }

        if (m_nFocusedColumn == 0)
        {
            int idx = (m_nSelectedLeftIndex < 0) ? 0 : (m_nSelectedLeftIndex + dir);
            SelectItem(0, idx);
            dump("[Pressed][UPDOWN][LEFT]");
            DebugAuto("KeyUpDownLeft");
        }
        else
        {
            int idx = (m_nSelectedRightIndex < 0) ? 0 : (m_nSelectedRightIndex + dir);
            SelectItem(1, idx);
            dump("[Pressed][UPDOWN][RIGHT]");
            DebugAuto("KeyUpDownRight");
        }
        InvalidateLayout();
        Repaint();
        return;
    }

    if (code == KEY_LEFT || code == KEY_RIGHT)
    {
        m_bSelectionVisible = true;

        if (m_nActionColumn >= 0)
        {
            if (code == KEY_RIGHT)
            {
                if (m_nSelectedAction == 0)
                    SetActionSelection(1);
            }
            else
            {
                if (m_nSelectedAction == 1)
                    SetActionSelection(0);
                else
                {
                    HideActionButtons();
                }
            }
            if (m_nActionColumn == 0)
            {
                if (m_pLeftActionPrimary) m_pLeftActionPrimary->Repaint();
                if (m_pLeftActionSecondary) m_pLeftActionSecondary->Repaint();
            }
            else if (m_nActionColumn == 1)
            {
                if (m_pRightActionPrimary) m_pRightActionPrimary->Repaint();
                if (m_pRightActionSecondary) m_pRightActionSecondary->Repaint();
            }
            InvalidateLayout();
            Repaint();
            dump("[Pressed][LEFTRIGHT][ACTION]");
            return;
        }

        HideActionButtons();
        m_nFooterSelection = -1;
        if (code == KEY_LEFT && m_LeftButtons.Count() > 0)
            SelectItem(0, (m_nSelectedLeftIndex < 0) ? 0 : m_nSelectedLeftIndex);
        else if (code == KEY_RIGHT && m_RightButtons.Count() > 0)
            SelectItem(1, (m_nSelectedRightIndex < 0) ? 0 : m_nSelectedRightIndex);
        InvalidateLayout();
        Repaint();
        dump("[Pressed][LEFTRIGHT][SWITCHCOL]");
        DebugAuto("KeySwitchColumn");
        return;
    }

    if (code == KEY_ENTER || code == KEY_PAD_ENTER)
    {
        m_bSelectionVisible = true;
        m_nFooterSelection = -1;

        if (m_nActionColumn < 0)
        {
            if (m_nFocusedColumn == 0)
            {
                if (m_nSelectedLeftIndex < 0)
                    SelectItem(0, 0);
                SetActionSelection(0);
                ShowActions(0, m_nSelectedLeftIndex);
            }
            else
            {
                if (m_nSelectedRightIndex < 0)
                    SelectItem(1, 0);
                SetActionSelection(0);
                ShowActions(1, m_nSelectedRightIndex);
            }
            InvalidateLayout();
            Repaint();
            dump("[Pressed][ENTER][OPEN_ACTIONS]");
            DebugAuto("EnterOpenActions");
            return;
        }

        PerformSelectedAction();
        InvalidateLayout();
        Repaint();
        dump("[Pressed][ENTER][DO_ACTION]");
        DebugAuto("EnterDoAction");
        return;
    }

    dump("[Pressed][PASS_TO_BASE]");
    BaseClass::OnKeyCodePressed(code);
}

void CVGuiLootPanel::OnKeyCodeTyped(vgui::KeyCode code)
{
    if (cl_lootpanel_debug.GetBool() && cl_lootpanel_debug_keys.GetBool())
    {
        vgui::VPANEL focus = vgui::input()->GetFocus();
        vgui::VPANEL modal = vgui::input()->GetAppModalSurface();
        LootDbgMsg("[LootPanel][KEY][Typed] code=%d vis=%d kb=%d mouse=%d focus=%p self=%p modal=%p\n",
            (int)code,
            IsVisible() ? 1 : 0, IsKeyBoardInputEnabled() ? 1 : 0, IsMouseInputEnabled() ? 1 : 0,
            (void*)focus, (void*)GetVPanel(), (void*)modal);
    }

    if (code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT || code == KEY_ENTER || code == KEY_PAD_ENTER)
    {
        float now = gpGlobals ? gpGlobals->curtime : 0.0f;
        if (code == g_LootPanelLastPressedCode && (now - g_LootPanelLastPressedTime) < 0.05f)
            return;
        OnKeyCodePressed(code);
        return;
    }

    if (code == KEY_ESCAPE)
    {
        engine->ClientCmd_Unrestricted("gameui_hide\n");
        engine->ClientCmd_Unrestricted("wait;gameui_hide\n");
        ScheduleModalRelease(0.10f);
        EndLoot();
        return;
    }
    BaseClass::OnKeyCodeTyped(code);
}

// Client console callbacks to receive loot payload from server
// Expected payload format: "left1;left2;left3|right1;right2;right3"
static void __Cmd_Loot_Open_Payload(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    DevMsg("[LootPanel] loot_open_payload_local build=%s argc=%d\n", g_pszLootPanelBuild, args.ArgC());
    CVGuiInventoryPanel *invPanel = GetGlobalInventoryPanel();
    if (invPanel && invPanel->IsVisible())
        return;
    const char *payload = args.Arg(1);
    CUtlVector<CUtlString> leftItems; CUtlVector<CUtlString> rightItems;

    // find separator '|'
    const char *sep = strchr(payload, '|');
    if (sep)
    {
        // parse left
        CUtlString tmp;
        for (const char *p = payload; p < sep && *p; ++p)
        {
            if (*p == ';') { leftItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) leftItems.AddToTail(tmp);
        // parse right
        tmp.Clear();
        for (const char *p = sep + 1; *p; ++p)
        {
            if (*p == ';') { rightItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) rightItems.AddToTail(tmp);
    }
    else
    {
        // only left payload present
        CUtlString tmp;
        for (const char *p = payload; *p; ++p)
        {
            if (*p == ';') { leftItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) leftItems.AddToTail(tmp);
    }

    CVGuiLootPanel *panel = GetGlobalLootPanel();
    if (panel)
    {
        panel->SetContext("", "", "");
        panel->SetItems(leftItems, rightItems);
    }
}

static void __Cmd_Loot_Open_Payload2(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    DevMsg("[LootPanel] loot_open_payload2_local build=%s argc=%d\n", g_pszLootPanelBuild, args.ArgC());
    CVGuiInventoryPanel *invPanel = GetGlobalInventoryPanel();
    if (invPanel && invPanel->IsVisible())
        return;

    const char *payload = args.Arg(1);
    const char *displayName = (args.ArgC() >= 3) ? args.Arg(2) : "";
    const char *soundOpen = (args.ArgC() >= 4) ? args.Arg(3) : "";
    const char *soundClose = (args.ArgC() >= 5) ? args.Arg(4) : "";

    CUtlVector<CUtlString> leftItems; CUtlVector<CUtlString> rightItems;

    const char *sep = strchr(payload, '|');
    if (sep)
    {
        CUtlString tmp;
        for (const char *p = payload; p < sep && *p; ++p)
        {
            if (*p == ';') { leftItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) leftItems.AddToTail(tmp);
        tmp.Clear();
        for (const char *p = sep + 1; *p; ++p)
        {
            if (*p == ';') { rightItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) rightItems.AddToTail(tmp);
    }
    else
    {
        CUtlString tmp;
        for (const char *p = payload; *p; ++p)
        {
            if (*p == ';') { leftItems.AddToTail(tmp); tmp.Clear(); }
            else { char t[2] = {*p, '\0'}; tmp.Append(t); }
        }
        if (tmp.Get()[0]) leftItems.AddToTail(tmp);
    }

    CVGuiLootPanel *panel = GetGlobalLootPanel();
    if (panel)
    {
        panel->SetContext(displayName, soundOpen, soundClose);
        panel->SetItems(leftItems, rightItems);
    }
}

static void __Cmd_Loot_Open_End(const CCommand &args)
{
    CVGuiLootPanel *panel = GetGlobalLootPanel();
    if (panel) panel->EndLoot();
}

static void __Cmd_LootPanel_Debug_Dump(const CCommand &args)
{
    CVGuiLootPanel *panel = GetGlobalLootPanel();
    if (!panel)
        return;
    panel->DebugDump();
}

static void __Cmd_LootPanel_Debug_Paint_Once(const CCommand &args)
{
    CVGuiLootPanel *panel = GetGlobalLootPanel();
    if (!panel)
        return;
    panel->DebugPaintDump(true);
}

// Register commands
static ConCommand cl_loot_open_payload("loot_open_payload_local", __Cmd_Loot_Open_Payload);
static ConCommand cl_loot_open_payload2("loot_open_payload2_local", __Cmd_Loot_Open_Payload2);
static ConCommand cl_loot_open_end("loot_open_end_local", __Cmd_Loot_Open_End);
static ConCommand cl_lootpanel_debug_dump("lootpanel_debug_dump", __Cmd_LootPanel_Debug_Dump, "Dump loot panel debug state", FCVAR_CLIENTDLL);
static ConCommand cl_lootpanel_debug_paint_once("lootpanel_debug_paint_once", __Cmd_LootPanel_Debug_Paint_Once, "Dump loot panel paint/highlight state once", FCVAR_CLIENTDLL);

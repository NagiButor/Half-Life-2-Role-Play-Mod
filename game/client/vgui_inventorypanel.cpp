#include "cbase.h"
#include "vgui_inventorypanel.h"
#include "vgui_lootpanel.h"
#include <vgui_controls/Panel.h>
#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IScheme.h>
#include <vgui_controls/AnimationController.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/TextEntry.h>
#include <math.h>
#include "cliententitylist.h"
#include "clientmode.h"
#include "quest_netmessages.h"
#include "filesystem.h"
#include <KeyValues.h>
#include "timecycle/env_timecycle.h"

using namespace vgui;

class CSplitAmountTextEntry : public TextEntry
{
public:
    CSplitAmountTextEntry(Panel *parent, const char *panelName) : TextEntry(parent, panelName) {}

    virtual void ApplySchemeSettings(IScheme *pScheme) OVERRIDE
    {
        TextEntry::ApplySchemeSettings(pScheme);
        SetPaintBackgroundEnabled(true);
        SetBgColor(Color(0, 0, 0, 200));
        SetFgColor(Color(255, 200, 30, 255));
        SetSelectionTextColor(Color(255, 200, 30, 255));
        SetSelectionBgColor(Color(180, 50, 40, 200));
        SetPaintBorderEnabled(false);
        SetBorder(NULL);
    }
};

class CSplitGoldLabel : public Label
{
public:
    CSplitGoldLabel(Panel *parent, const char *panelName, const char *text) : Label(parent, panelName, text) {}

    virtual void ApplySchemeSettings(IScheme *pScheme) OVERRIDE
    {
        Label::ApplySchemeSettings(pScheme);
        SetPaintBackgroundEnabled(false);
        SetFgColor(Color(255, 200, 30, 255));
    }
};

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
    CHAN_INVENTORY_PANEL_OPEN_A = CHAN_USER_BASE + 120,
    CHAN_INVENTORY_PANEL_OPEN_B = CHAN_USER_BASE + 121,
    CHAN_INVENTORY_PANEL_CLOSE_A = CHAN_USER_BASE + 122,
    CHAN_INVENTORY_PANEL_CLOSE_B = CHAN_USER_BASE + 123,
    CHAN_INVENTORY_CLICK_ITEM = CHAN_USER_BASE + 124,
    CHAN_INVENTORY_CLICK_ACTION = CHAN_USER_BASE + 125,
};
// Helper: map internal classnames to friendly labels and compute ammo bullet counts
static void GetFriendlyLabelAndCount(const char *classname, int pickupCount, bool hasExplicitBullets, char *out, int outSize)
{
    if (!classname || !classname[0])
    {
        Q_snprintf(out, outSize, "(unknown)");
        return;
    }

    // Ammo pickups: convert to bullets using the server-defined sizes
    // Normalize all ammo displays to show only the total bullets: "<Name> - <total> bullets".
    auto ammoFormat = [&](const char *name, int perAmmo)
    {
        int total = hasExplicitBullets ? pickupCount : perAmmo * pickupCount;
        Q_snprintf(out, outSize, "%s - %d bullets", name, total);
    };

    if (!Q_stricmp(classname, "item_ammo_pistol") || !Q_stricmp(classname, "item_box_srounds"))
    {
        ammoFormat("Pistol Ammo", 20); // SIZE_AMMO_PISTOL
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_pistol_large") || !Q_stricmp(classname, "item_large_box_srounds"))
    {
        ammoFormat("Pistol Ammo (Large)", 100); // SIZE_AMMO_PISTOL_LARGE
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_smg1") || !Q_stricmp(classname, "item_box_mrounds"))
    {
        ammoFormat("SMG Ammo", 45); // SIZE_AMMO_SMG1
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_smg1_large") || !Q_stricmp(classname, "item_large_box_mrounds"))
    {
        ammoFormat("SMG Ammo (Large)", 225); // SIZE_AMMO_SMG1_LARGE
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_ar2") || !Q_stricmp(classname, "item_box_lrounds"))
    {
        ammoFormat("AR2 Ammo", 20); // SIZE_AMMO_AR2
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_ar2_large") || !Q_stricmp(classname, "item_large_box_lrounds"))
    {
        ammoFormat("AR2 Ammo (Large)", 100); // SIZE_AMMO_AR2_LARGE
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_357"))
    {
        ammoFormat(".357 Ammo", 6); // SIZE_AMMO_357
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_357_large"))
    {
        ammoFormat(".357 Ammo (Large)", 20); // SIZE_AMMO_357_LARGE
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_crossbow") || !Q_stricmp(classname, "item_box_xbowrounds"))
    {
        ammoFormat("Crossbow Bolts", 6); // SIZE_AMMO_CROSSBOW
        return;
    }
    if (!Q_stricmp(classname, "item_box_buckshot"))
    {
        ammoFormat("Buckshot", 20); // SIZE_AMMO_BUCKSHOT
        return;
    }
    if (!Q_stricmp(classname, "item_rpg_round") || !Q_stricmp(classname, "item_ml_grenade"))
    {
        ammoFormat("RPG Round", 1); // SIZE_AMMO_RPG_ROUND
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_smg1_grenade") || !Q_stricmp(classname, "item_ar2_grenade"))
    {
        ammoFormat("SMG Grenade", 1); // SIZE_AMMO_SMG1_GRENADE
        return;
    }
    if (!Q_stricmp(classname, "item_ammo_ar2_altfire"))
    {
        ammoFormat("AR2 Alt Fire", 1); // SIZE_AMMO_AR2_ALTFIRE
        return;
    }

    // Healthkit / battery
    if (!Q_stricmp(classname, "item_healthkit") || !Q_stricmp(classname, "item_healthvial"))
    {
        if (pickupCount > 1)
            Q_snprintf(out, outSize, "Healthkit x%d", pickupCount);
        else
            Q_snprintf(out, outSize, "Healthkit");
        return;
    }
    if (!Q_stricmp(classname, "item_battery") || !Q_stricmp(classname, "item_suitcharger"))
    {
        if (pickupCount > 1)
            Q_snprintf(out, outSize, "Battery x%d", pickupCount);
        else
            Q_snprintf(out, outSize, "Battery");
        return;
    }

    // Weapons: strip "weapon_" and prettify
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

    // Generic item: strip leading "item_" if present and prettify
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

    // Fallback: show class name
    if (pickupCount > 1)
        Q_snprintf(out, outSize, "%s x%d", classname, pickupCount);
    else
        Q_snprintf(out, outSize, "%s", classname);
}

static CUtlDict<float, int> s_WeightKgByClass_Client;
static bool s_bWeightTableLoaded_Client = false;
static void LoadInventoryWeightTableClient()
{
    s_WeightKgByClass_Client.RemoveAll();
    s_bWeightTableLoaded_Client = true;

    KeyValues *pKV = new KeyValues("InventoryWeights");
    if (!pKV->LoadFromFile(filesystem, "scripts/inventory_weights.txt", "GAME"))
    {
        pKV->deleteThis();
        return;
    }

    for (KeyValues *pKey = pKV->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
    {
        const float kg = pKey->GetFloat("kg", 0.0f);
        if (kg <= 0.0f)
            continue;
        s_WeightKgByClass_Client.Insert(pKey->GetName(), kg);
    }

    pKV->deleteThis();
}

static float GetClassUnitWeightKgClient(const char *classname, bool bAmmoUnit)
{
    if (!classname || !classname[0])
        return 0.0f;
    if (!s_bWeightTableLoaded_Client)
        LoadInventoryWeightTableClient();

    int found = s_WeightKgByClass_Client.Find(classname);
    if (found != s_WeightKgByClass_Client.InvalidIndex())
        return s_WeightKgByClass_Client[found];

    if (bAmmoUnit)
        return 0.01f;
    if (!Q_strnicmp(classname, "weapon_", 7))
        return 3.0f;
    if (!Q_strnicmp(classname, "item_", 5))
        return 1.0f;
    return 0.5f;
}

static bool IsAmmoClassnameForInventory(const char *classname);

static float GetInventoryEntryWeightKgClient(const char *classname, int count, bool hasExplicitBullets)
{
    if (!classname || !classname[0])
        return 0.0f;

    if (IsAmmoClassnameForInventory(classname))
    {
        const int bullets = hasExplicitBullets ? Max(0, count) : 0;
        return GetClassUnitWeightKgClient(classname, true) * (float)bullets;
    }

    return GetClassUnitWeightKgClient(classname, false);
}

static bool IsAmmoClassnameForInventory(const char *classname)
{
    if (!classname || !classname[0])
        return false;
    if (!Q_stricmp(classname, "item_ammo_pistol") || !Q_stricmp(classname, "item_box_srounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_pistol_large") || !Q_stricmp(classname, "item_large_box_srounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_smg1") || !Q_stricmp(classname, "item_box_mrounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_smg1_large") || !Q_stricmp(classname, "item_large_box_mrounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_ar2") || !Q_stricmp(classname, "item_box_lrounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_ar2_large") || !Q_stricmp(classname, "item_large_box_lrounds"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_357"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_357_large"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_crossbow") || !Q_stricmp(classname, "item_box_xbowrounds"))
        return true;
    if (!Q_stricmp(classname, "item_box_buckshot"))
        return true;
    if (!Q_stricmp(classname, "item_rpg_round") || !Q_stricmp(classname, "item_ml_grenade"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_smg1_grenade") || !Q_stricmp(classname, "item_ar2_grenade"))
        return true;
    if (!Q_stricmp(classname, "item_ammo_ar2_altfire"))
        return true;
    return false;
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

static CVGuiInventoryPanel *s_pInventoryPanel = nullptr;

// Custom inventory option button that matches vgui_dialog styling
class CInvOptionButton : public Button
{
public:
    CInvOptionButton(Panel *parent, const char *panelName, const char *text, int idx = -1, CVGuiInventoryPanel *owner = nullptr)
        : Button(parent, panelName, text), m_nIndex(idx)
    {
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
        m_hOwner = owner;
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h;
        GetSize(w, h);
        bool selected = false;
        bool suppressArmed = false;
        CVGuiInventoryPanel *pOwner = m_hOwner.Get();
        if (pOwner)
        {
            if (m_nIndex >= 0)
            {
                selected = pOwner->IsItemSelected(m_nIndex);
                suppressArmed = pOwner->AreActionsVisibleForItem(m_nIndex);
            }
            else if (m_nIndex == -100)
                selected = pOwner->IsUseActionSelected();
            else if (m_nIndex == -101)
                selected = pOwner->IsDropActionSelected();
        }

        if (IsDepressed() || IsArmed() || (!suppressArmed && selected))
        {
            surface()->DrawSetColor(180, 50, 40, 200);
            surface()->DrawFilledRect(0, 0, w, h);
        }
        // gold outline
        surface()->DrawSetColor(255, 200, 30, 255);
        surface()->DrawOutlinedRect(0, 0, w-1, h-1);
    }

    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255, 200, 30, 255));
        Button::Paint();
    }

    virtual void OnMousePressed(MouseCode code) OVERRIDE
    {
        // Dragging temporarily disabled: behave like a normal click
        Button::OnMousePressed(code);
    }

    virtual void OnMouseReleased(MouseCode code) OVERRIDE
    {
        // Dragging temporarily disabled: behave like a normal release
        Button::OnMouseReleased(code);
    }

private:
    int m_nIndex;
    DHANDLE<CVGuiInventoryPanel> m_hOwner;
};

class CQuestOptionButton : public Button
{
public:
    CQuestOptionButton(Panel *parent, const char *panelName, const char *text, int rowIndex, CVGuiInventoryPanel *owner)
        : Button(parent, panelName, text), m_nIndex(rowIndex)
    {
        m_hOwner = owner;
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
        SetKeyBoardInputEnabled(false);
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h;
        GetSize(w, h);
        bool selected = false;
        CVGuiInventoryPanel *pOwner = m_hOwner.Get();
        if (pOwner)
            selected = pOwner->IsItemSelected(m_nIndex);

        if (IsDepressed() || IsArmed() || selected)
        {
            surface()->DrawSetColor(180, 50, 40, 200);
            surface()->DrawFilledRect(0, 0, w, h);
        }
        surface()->DrawSetColor(255, 200, 30, 255);
        surface()->DrawOutlinedRect(0, 0, w-1, h-1);
    }

    virtual void Paint() OVERRIDE
    {
        CVGuiInventoryPanel *pOwner = m_hOwner.Get();
        if (!pOwner)
        {
            Button::Paint();
            return;
        }
        if (!pOwner->IsQuestRowIndex(m_nIndex))
        {
            SetFgColor(Color(255, 200, 30, 255));
            Button::Paint();
            return;
        }

        const QuestClientEntry *q = pOwner->GetQuestEntryForRowIndex(m_nIndex);
        if (!q)
        {
            SetFgColor(Color(255, 200, 30, 255));
            Button::Paint();
            return;
        }

        vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(GetScheme());
        HFont hMain = pScheme->GetFont("Default", true);
        HFont hSub = pScheme->GetFont("DefaultSmall", true);

        int w, h;
        GetSize(w, h);

        wchar_t wMain[256];
        wchar_t wSub[256];
        g_pVGuiLocalize->ConvertANSIToUnicode(q->baseTitle.Get(), wMain, sizeof(wMain));

        surface()->DrawSetTextFont(hMain);
        surface()->DrawSetTextColor(255, 200, 30, 255);
        surface()->DrawSetTextPos(10, 6);
        surface()->DrawPrintText(wMain, wcslen(wMain), vgui::FONT_DRAW_NONADDITIVE);

        CUtlString sub = q->stageTitle;
        g_pVGuiLocalize->ConvertANSIToUnicode(sub.Get(), wSub, sizeof(wSub));
        surface()->DrawSetTextFont(hSub);
        surface()->DrawSetTextColor(255, 200, 30, 200);
        surface()->DrawSetTextPos(10, 22);
        surface()->DrawPrintText(wSub, wcslen(wSub), vgui::FONT_DRAW_NONADDITIVE);
    }

private:
    int m_nIndex;
    DHANDLE<CVGuiInventoryPanel> m_hOwner;
};

class CInvTabButton : public Button
{
public:
    CInvTabButton(Panel *parent, const char *panelName, const char *text, int tabIndex, CVGuiInventoryPanel *owner)
        : Button(parent, panelName, text), m_nTabIndex(tabIndex)
    {
        m_hOwner = owner;
        SetPaintBackgroundEnabled(false);
        SetPaintBorderEnabled(false);
        SetContentAlignment(Label::a_center);
    }

    virtual void PaintBackground() OVERRIDE
    {
    }

    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255, 200, 30, 255));
        Button::Paint();
    }

private:
    int m_nTabIndex;
    DHANDLE<CVGuiInventoryPanel> m_hOwner;
};

// Small red 'X' close button used on the confirm panel
class CConfirmCloseButton : public Button
{
public:
    CConfirmCloseButton(Panel *parent, const char *panelName, const char *text)
        : Button(parent, panelName, text)
    {
        SetPaintBackgroundEnabled(true);
        SetPaintBorderEnabled(false);
        SetFgColor(Color(255, 200, 30, 255));
    }

    virtual void PaintBackground() OVERRIDE
    {
        int w, h; GetSize(w, h);
        surface()->DrawSetColor(200, 40, 40, 255);
        surface()->DrawFilledRect(0, 0, w, h);
    }

    virtual void Paint() OVERRIDE
    {
        SetFgColor(Color(255, 200, 30, 255));
        Button::Paint();
    }
};

CVGuiInventoryPanel::CVGuiInventoryPanel(Panel *parent) : BaseClass(parent, "InventoryPanel")
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

    int sx, sy;
    surface()->GetScreenSize(sx, sy);
    int w = Max(600, (sx*7)/10);
    int h = Max(400, (sy*6)/10);
    SetSize(w, h);
    SetPos((sx - w)/2, (sy - h)/2);

    m_pTitleLabel = new Label(this, "Title", "Inventory");
    m_pTitleLabel->SetBounds(12, 8, w-24, 36);
    m_pTitleLabel->SetContentAlignment(Label::a_center);
    // Draw only the text (yellow/gold) like vgui_dialog; don't paint a white/opaque background
    m_pTitleLabel->SetPaintBackgroundEnabled(false);
    m_pTitleLabel->SetFgColor(Color(255,200,30,255));
    m_pTitleLabel->SetVisible(false);

    m_pTimeLabel = new Label(this, "TimeLabel", "");
    m_pTimeLabel->SetBounds(12, 8, w - 24, 24);
    m_pTimeLabel->SetContentAlignment(Label::a_east);
    m_pTimeLabel->SetPaintBackgroundEnabled(false);
    m_pTimeLabel->SetFgColor(Color(255,200,30,255));
    m_pTimeLabel->SetVisible(true);

    m_pTabInventoryBtn = new CInvTabButton(this, "TabInventory", "Inventory", 0, this);
    m_pTabInventoryBtn->SetCommand("tab_inventory");
    m_pTabInventoryBtn->AddActionSignalTarget(this);
    m_pTabInventoryBtn->SetContentAlignment(Label::a_center);

    m_pTabQuestsBtn = new CInvTabButton(this, "TabQuests", "Quests", 1, this);
    m_pTabQuestsBtn->SetCommand("tab_quests");
    m_pTabQuestsBtn->AddActionSignalTarget(this);
    m_pTabQuestsBtn->SetContentAlignment(Label::a_center);

    // Create client-side bind so pressing i requests inventory
    engine->ClientCmd("bind i inventory_request_local\n");

    m_pSortBtn = nullptr;
    m_pUnequipSuitBtn = nullptr;
    m_pWeightLabel = nullptr;
    m_pActionUseBtn = nullptr;
    m_pActionDropBtn = nullptr;
    m_pQuestDetailPanel = nullptr;
    m_pQuestDescLabel = nullptr;
    m_pQuestShowOnMapBtn = nullptr;
    m_pSplitPanel = nullptr;
    m_pSplitTitleLabel = nullptr;
    m_pSplitAmountEntry = nullptr;
    m_pSplitConfirmBtn = nullptr;
    m_pSplitCancelBtn = nullptr;
    m_nSplitIndex = -1;
    m_nSplitMax = 0;
    m_bSplitVisible = false;
    m_nActionIndex = -1;
    m_nDragIndex = -1;
    m_nSelectedIndex = -1;
    m_nSelectedQuestIndex = -1;
    m_nActiveTab = 0;
    m_nContentOffset = 0;
    m_pListPanel = nullptr;
    m_bStackInProgress = false;
    m_nSelectedAction = 0;
    m_bSelectionVisible = false;
    m_bPendingModalRelease = false;
    m_bShowCompletedQuests = false;
    m_flModalReleaseTime = 0.0f;
    m_flCurWeight = 0.0f;
    m_flMaxWeight = 0.0f;
    m_bOverencumbered = false;
}

void CVGuiInventoryPanel::ScheduleModalRelease(float delay)
{
    m_bPendingModalRelease = true;
    m_flModalReleaseTime = gpGlobals ? (gpGlobals->curtime + delay) : 0.0f;
    vgui::ivgui()->AddTickSignal(GetVPanel());
}

void CVGuiInventoryPanel::ApplySchemeSettings(vgui::IScheme *scheme)
{
    BaseClass::ApplySchemeSettings(scheme);
    if (m_pTitleLabel)
    {
        m_pTitleLabel->SetFgColor(Color(255,200,30,255));
        m_pTitleLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pTimeLabel)
    {
        m_pTimeLabel->SetFgColor(Color(255,200,30,255));
        m_pTimeLabel->SetContentAlignment(Label::a_east);
    }
    if (m_pTabInventoryBtn)
    {
        m_pTabInventoryBtn->SetFgColor(Color(255,200,30,255));
        m_pTabInventoryBtn->SetContentAlignment(Label::a_center);
    }
    if (m_pTabQuestsBtn)
    {
        m_pTabQuestsBtn->SetFgColor(Color(255,200,30,255));
        m_pTabQuestsBtn->SetContentAlignment(Label::a_center);
    }
    if (m_pQuestDescLabel)
    {
        m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));
    }
    if (m_pWeightLabel)
    {
        m_pWeightLabel->SetPaintBackgroundEnabled(false);
        m_pWeightLabel->SetFgColor(m_bOverencumbered ? Color(200, 40, 40, 255) : Color(255, 200, 30, 255));
        m_pWeightLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pSplitTitleLabel)
    {
        m_pSplitTitleLabel->SetFgColor(Color(255, 200, 30, 255));
        m_pSplitTitleLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pSplitAmountEntry)
    {
        m_pSplitAmountEntry->SetPaintBackgroundEnabled(true);
        m_pSplitAmountEntry->SetBgColor(Color(0, 0, 0, 200));
        m_pSplitAmountEntry->SetFgColor(Color(255, 200, 30, 255));
        m_pSplitAmountEntry->SetSelectionTextColor(Color(255, 200, 30, 255));
        m_pSplitAmountEntry->SetSelectionBgColor(Color(180, 50, 40, 200));
        m_pSplitAmountEntry->SetPaintBorderEnabled(false);
        m_pSplitAmountEntry->SetBorder(NULL);
    }
}

CVGuiInventoryPanel *GetGlobalInventoryPanel()
{
    if (!s_pInventoryPanel)
    {
        s_pInventoryPanel = new CVGuiInventoryPanel(NULL);
    }
    return s_pInventoryPanel;
}

bool CVGuiInventoryPanel::IsItemSelected(int idx)
{
    return IsVisible() && m_bSelectionVisible && (idx >= 0) && (idx == m_nSelectedIndex);
}

bool CVGuiInventoryPanel::IsUseActionSelected()
{
    return IsVisible() && (m_nActionIndex >= 0) && (m_nSelectedAction == 0);
}

bool CVGuiInventoryPanel::IsDropActionSelected()
{
    return IsVisible() && (m_nActionIndex >= 0) && (m_nSelectedAction == 1);
}

bool CVGuiInventoryPanel::AreActionsVisibleForItem(int idx)
{
    if (!IsVisible())
        return false;
    if (idx < 0 || idx >= m_Items.Count())
        return false;
    if (m_nActionIndex != idx)
        return false;
    if (!m_pActionUseBtn || !m_pActionDropBtn)
        return false;
    return m_pActionUseBtn->IsVisible() || m_pActionDropBtn->IsVisible();
}

int CVGuiInventoryPanel::GetActiveTab() const
{
    return m_nActiveTab;
}

void CVGuiInventoryPanel::OpenInventoryTab()
{
    m_nActiveTab = 0;
    BeginInventory();
}

bool CVGuiInventoryPanel::IsQuestRowIndex(int rowIndex) const
{
    if (m_nActiveTab != 1)
        return false;
    if (rowIndex < 0 || rowIndex >= m_QuestRowToQuestIndex.Count())
        return false;
    return m_QuestRowToQuestIndex[rowIndex] >= 0;
}

const QuestClientEntry *CVGuiInventoryPanel::GetQuestEntryForRowIndex(int rowIndex) const
{
    if (!IsQuestRowIndex(rowIndex))
        return NULL;
    int questIndex = m_QuestRowToQuestIndex[rowIndex];
    if (questIndex < 0 || questIndex >= m_Quests.Count())
        return NULL;
    return &m_Quests[questIndex];
}

void CVGuiInventoryPanel::BeginInventory()
{
    bool wasVisible = IsVisible();
    // cleanup existing
    for (int i = 0; i < m_Options.Count(); ++i)
        delete m_Options[i];
    m_Options.RemoveAll();
    m_QuestRowToQuestIndex.RemoveAll();
    HideSplitPopup();
    HideActions();
    m_bSelectionVisible = false;

    // ensure visible
    if (!GetParent())
    {
        Panel *parent = (Panel*)g_pClientMode->GetViewport();
        if (parent)
            SetParent(parent);
    }
    MakePopup();
    SetVisible(true);
    SetZPos(1000);
    MoveToFront();
    engine->ClientCmd_Unrestricted("gameui_preventescapetoshow\n");
    engine->ClientCmd("inventory_ui_open 1\n");
    // Re-apply title color/alignment after making popup (some schemes reset label color when parent changes)
    if (m_pTitleLabel)
    {
        m_pTitleLabel->SetFgColor(Color(255,200,30,255));
        m_pTitleLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pQuestDescLabel)
    {
        m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));
    }
    SetKeyBoardInputEnabled(true);
    SetMouseInputEnabled(true);
    RequestFocus();
    vgui::input()->SetAppModalSurface(GetVPanel());
    m_bPendingModalRelease = false;

    vgui::ivgui()->AddTickSignal( GetVPanel() );
    if ( m_pTimeLabel )
    {
        CEnvTimecycle *tc = GetTimecycle();
        if ( tc )
        {
            float hours = tc->GetTimeOfDayHours();
            int hh = (int)floorf( hours ) % 24;
            int mm = (int)floorf( ( hours - floorf( hours ) ) * 60.0f + 0.5f ) % 60;
            char buf[64];
            Q_snprintf( buf, sizeof( buf ), "%02d:%02d", hh, mm );
            m_pTimeLabel->SetText( buf );
        }
        else
        {
            m_pTimeLabel->SetText( "" );
        }
    }

    InvalidateLayout(true);
    Repaint();
    if (!wasVisible)
    {
        PlayLocalUISound("npc\\combine_soldier\\zipline_clothing1.wav", CHAN_INVENTORY_PANEL_OPEN_A, 0.22f);
        //PlayLocalUISound("npc\\combine_soldier\\zipline1.wav", CHAN_INVENTORY_PANEL_OPEN_B, 0.22f); ���� ������� �� ���������, ��������������
    }

    // layout items
    int pw, ph; GetSize(pw, ph);
    int btnW = pw - 24;
    int btnH = (m_nActiveTab == 1) ? 46 : 34;
    int tabY = 8;
    int tabH = 36;
    int tabDividerW = 1;
    int tabW = (btnW - tabDividerW) / 2;
    if (m_pTabInventoryBtn)
        m_pTabInventoryBtn->SetBounds(12, tabY, tabW, tabH);
    if (m_pTabQuestsBtn)
        m_pTabQuestsBtn->SetBounds(12 + tabW + tabDividerW, tabY, tabW, tabH);

    int viewTop = tabY + tabH + 14;
    int detailHeight = (m_nActiveTab == 1) ? 132 : 0;
    int footerHeight = (m_nActiveTab == 0) ? 42 : 0;
    int viewHeight = ph - viewTop - 12 - detailHeight - footerHeight;
    if (!m_pListPanel)
    {
        m_pListPanel = new Panel(this, "ListPanel");
        m_pListPanel->SetPaintBackgroundEnabled(false);
    }
    m_pListPanel->SetBounds(12, viewTop, btnW, viewHeight);

    if (m_nActiveTab == 0 && !m_pSortBtn)
    {
        m_pSortBtn = new CInvOptionButton(this, "SortButton", "Sort", -200, this);
        m_pSortBtn->SetCommand("inv_sort");
        m_pSortBtn->AddActionSignalTarget(this);
    }
    if (m_pSortBtn)
    {
        m_pSortBtn->SetVisible(m_nActiveTab == 0);
        m_pSortBtn->SetEnabled(m_nActiveTab == 0);
        if (m_nActiveTab == 0)
        {
            int sortW = 110;
            int sortH = 30;
            int sortX = 12 + btnW - sortW;
            int sortY = viewTop + viewHeight + 8;
            m_pSortBtn->SetBounds(sortX, sortY, sortW, sortH);
        }
    }
    if (m_nActiveTab == 0 && !m_pUnequipSuitBtn)
    {
        m_pUnequipSuitBtn = new CInvOptionButton(this, "UnequipSuitButton", "Unequip suit", -201, this);
        m_pUnequipSuitBtn->SetCommand("inv_unequip_suit");
        m_pUnequipSuitBtn->AddActionSignalTarget(this);
    }
    if (m_pUnequipSuitBtn)
    {
        C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
        const bool showUnequip = (m_nActiveTab == 0) && pLocal && pLocal->IsSuitEquipped();
        m_pUnequipSuitBtn->SetVisible(showUnequip);
        m_pUnequipSuitBtn->SetEnabled(showUnequip);
        if (showUnequip)
        {
            int unW = 140;
            int unH = 30;
            int unX = 12;
            int unY = viewTop + viewHeight + 8;
            m_pUnequipSuitBtn->SetBounds(unX, unY, unW, unH);
        }
    }
    if (m_nActiveTab == 0 && !m_pWeightLabel)
    {
        m_pWeightLabel = new Label(this, "WeightLabel", "");
        m_pWeightLabel->SetPaintBackgroundEnabled(false);
        m_pWeightLabel->SetContentAlignment(Label::a_center);
    }
    if (m_pWeightLabel)
    {
        const bool showWeight = (m_nActiveTab == 0);
        m_pWeightLabel->SetVisible(showWeight);
        if (showWeight)
        {
            int labelH = 30;
            int labelY = viewTop + viewHeight + 8;
            int labelW = btnW - 260;
            if (labelW < 160) labelW = 160;
            int labelX = 12 + (btnW - labelW) / 2;
            m_pWeightLabel->SetBounds(labelX, labelY, labelW, labelH);

            char buf[128];
            Q_snprintf(buf, sizeof(buf), "Weight: %.1f / %.1f kg", m_flCurWeight, m_flMaxWeight);
            m_pWeightLabel->SetText(buf);
            m_pWeightLabel->SetFgColor(m_bOverencumbered ? Color(200, 40, 40, 255) : Color(255, 200, 30, 255));
        }
    }

    if (m_nActiveTab == 0 && !m_pActionUseBtn)
    {
        m_pActionUseBtn = new CInvOptionButton(m_pListPanel, "ActionUse", "Use", -100, this);
        m_pActionUseBtn->SetVisible(false);
        m_pActionUseBtn->SetEnabled(false);
        m_pActionUseBtn->SetMouseInputEnabled(false);
        m_pActionUseBtn->SetKeyBoardInputEnabled(false);
        m_pActionUseBtn->AddActionSignalTarget(this);
    }
    if (m_nActiveTab == 0 && !m_pActionDropBtn)
    {
        m_pActionDropBtn = new CInvOptionButton(m_pListPanel, "ActionDrop", "Drop", -101, this);
        m_pActionDropBtn->SetVisible(false);
        m_pActionDropBtn->SetEnabled(false);
        m_pActionDropBtn->SetMouseInputEnabled(false);
        m_pActionDropBtn->SetKeyBoardInputEnabled(false);
        m_pActionDropBtn->AddActionSignalTarget(this);
    }
    if (m_nActiveTab != 0)
    {
        if (m_pActionUseBtn) m_pActionUseBtn->SetVisible(false);
        if (m_pActionDropBtn) m_pActionDropBtn->SetVisible(false);
    }

    if (m_nActiveTab == 1)
    {
        if (!m_pQuestDetailPanel)
        {
            m_pQuestDetailPanel = new Panel(this, "QuestDetailPanel");
            m_pQuestDetailPanel->SetPaintBackgroundEnabled(true);
            m_pQuestDetailPanel->SetBgColor(Color(0, 0, 0, 200));
            m_pQuestDescLabel = new Label(m_pQuestDetailPanel, "QuestDesc", "");
            m_pQuestDescLabel->SetPaintBackgroundEnabled(false);
            m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));
            m_pQuestDescLabel->SetWrap(true);
            m_pQuestShowOnMapBtn = new CInvOptionButton(m_pQuestDetailPanel, "QuestShowOnMap", "Show on map", -300, this);
            m_pQuestShowOnMapBtn->SetCommand("quest_show_on_map");
            m_pQuestShowOnMapBtn->AddActionSignalTarget(this);
        }
        m_pQuestDetailPanel->SetBounds(12, viewTop + viewHeight + 8, btnW, detailHeight);
        if (m_pQuestDescLabel)
            m_pQuestDescLabel->SetBounds(12, 10, btnW - 24, detailHeight - 54);
        if (m_pQuestShowOnMapBtn)
            m_pQuestShowOnMapBtn->SetBounds(btnW - 180, detailHeight - 40, 168, 30);
        m_pQuestDetailPanel->SetVisible(m_nSelectedQuestIndex >= 0 && m_nSelectedQuestIndex < m_Quests.Count());
        if (m_pQuestDetailPanel->IsVisible() && m_pQuestDescLabel)
        {
            m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));
            m_pQuestDescLabel->SetText(m_Quests[m_nSelectedQuestIndex].stageDescription.Get());
        }
    }
    else
    {
        if (m_pQuestDetailPanel)
            m_pQuestDetailPanel->SetVisible(false);
    }

    int completedCount = 0;
    if (m_nActiveTab == 1)
    {
        for (int i = 0; i < m_Quests.Count(); ++i)
        {
            if (m_Quests[i].state == kQuestState_Completed)
                ++completedCount;
        }
        for (int i = 0; i < m_Quests.Count(); ++i)
        {
            if (m_Quests[i].state != kQuestState_Completed)
                m_QuestRowToQuestIndex.AddToTail(i);
        }
        if (completedCount > 0)
        {
            m_QuestRowToQuestIndex.AddToTail(-1);
            if (m_bShowCompletedQuests)
            {
                for (int i = 0; i < m_Quests.Count(); ++i)
                {
                    if (m_Quests[i].state == kQuestState_Completed)
                        m_QuestRowToQuestIndex.AddToTail(i);
                }
            }
        }
    }

    int listCount = (m_nActiveTab == 0) ? m_Items.Count() : m_QuestRowToQuestIndex.Count();
    for (int i = 0; i < listCount; ++i)
    {
        char namebuf[64];
        if (m_nActiveTab == 0)
            Q_snprintf(namebuf, sizeof(namebuf), "InvItem%d", i);
        else
            Q_snprintf(namebuf, sizeof(namebuf), "Quest%d", i);

        char display[256];
        display[0] = '\0';
        if (m_nActiveTab == 0)
        {
            const char *raw = m_Items[i].Get();
            const char *sep = strchr(raw, ':');
            char clsbuf[201];
            int cnt = 1;
            bool hasExplicit = false;
            if (sep)
            {
                int clen = sep - raw;
                if (clen > 200) clen = 200;
                Q_strncpy(clsbuf, raw, clen+1);
                clsbuf[clen] = '\0';
                cnt = atoi(sep + 1);
                hasExplicit = true;
            }
            else
            {
                Q_strncpy(clsbuf, raw, sizeof(clsbuf));
                clsbuf[sizeof(clsbuf)-1] = '\0';
                hasExplicit = false;
            }
            GetFriendlyLabelAndCount(clsbuf, cnt, hasExplicit, display, sizeof(display));

            float wkg = GetInventoryEntryWeightKgClient(clsbuf, cnt, hasExplicit);
            if (wkg > 0.0f)
            {
                char tmp[256];
                Q_snprintf(tmp, sizeof(tmp), "%s - %.2f kg", display, wkg);
                Q_strncpy(display, tmp, sizeof(display));
                display[sizeof(display) - 1] = '\0';
            }
        }
        else
        {
            int questIndex = m_QuestRowToQuestIndex[i];
            if (questIndex == -1)
            {
                Q_snprintf(display, sizeof(display), "Completed (%d) %s", completedCount, m_bShowCompletedQuests ? "[-]" : "[+]" );
            }
            else
            {
                const QuestClientEntry &q = m_Quests[questIndex];
                const char *prefix = "";
                if (q.state == kQuestState_Completed) prefix = "[Done] ";
                else if (q.state == kQuestState_Failed) prefix = "[Failed] ";
                else if (q.state == kQuestState_Active) prefix = "[Active] ";
                Q_snprintf(display, sizeof(display), "%s%s", prefix, q.baseTitle.Get());
            }
        }

        vgui::Button *btn = NULL;
        if (m_nActiveTab == 1)
            btn = new CQuestOptionButton(m_pListPanel, namebuf, display, i, this);
        else
            btn = new CInvOptionButton(m_pListPanel, namebuf, display, i, this);
        btn->SetKeyBoardInputEnabled(false);
        // apply vertical content offset so we can scroll long lists (local to list panel)
        int placedYLocal = (i * (btnH + 8)) - m_nContentOffset;
        btn->SetBounds(0, placedYLocal, btnW, btnH);
        char cmdbuf[64];
        if (m_nActiveTab == 0)
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "use_inv %d", i);
        else
        {
            int questIndex = m_QuestRowToQuestIndex[i];
            if (questIndex == -1)
                Q_snprintf(cmdbuf, sizeof(cmdbuf), "quest_toggle_completed");
            else
                Q_snprintf(cmdbuf, sizeof(cmdbuf), "quest_select_row %d", i);
        }
        btn->SetCommand(cmdbuf);
        // Ensure commands are routed to this panel even though btn is child of m_pListPanel
        btn->AddActionSignalTarget(this);
        m_Options.AddToTail((vgui::Button*)btn);
    }

    // compute content height and clamp offset
    int contentHeight = (btnH + 8) * listCount;
    if (m_nContentOffset < 0) m_nContentOffset = 0;
    int maxOffset = contentHeight - viewHeight;
    if (maxOffset < 0) maxOffset = 0;
    if (m_nContentOffset > maxOffset) m_nContentOffset = maxOffset;

    // Initialize selection for mouse-wheel navigation
    if (m_Options.Count() > 0)
    {
        m_nSelectedIndex = 0;
        AdjustScrollToSelection();
        UpdateArmedState();
        InvalidateLayout();
        Repaint();
    }
}

void CVGuiInventoryPanel::SelectItem(int idx)
{
    m_bSelectionVisible = true;
    if (m_Options.Count() <= 0)
    {
        m_nSelectedIndex = -1;
        HideActions();
        return;
    }

    if (idx < 0)
        idx = 0;
    if (idx >= m_Options.Count())
        idx = m_Options.Count() - 1;

    if (idx != m_nSelectedIndex)
    {
        m_nSelectedIndex = idx;
        HideActions();
    }

    AdjustScrollToSelection();
    UpdateArmedState();
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::SelectQuest(int idx)
{
    if (m_nActiveTab != 1)
        return;

    if (m_Quests.Count() <= 0)
    {
        m_nSelectedQuestIndex = -1;
    }
    else
    {
        if (idx < 0) idx = 0;
        if (idx >= m_Quests.Count()) idx = m_Quests.Count() - 1;
        m_nSelectedQuestIndex = idx;
    }

    if (m_pQuestDetailPanel)
    {
        bool vis = (m_nSelectedQuestIndex >= 0 && m_nSelectedQuestIndex < m_Quests.Count());
        m_pQuestDetailPanel->SetVisible(vis);
        if (vis && m_pQuestDescLabel)
        {
            m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));
            m_pQuestDescLabel->SetText(m_Quests[m_nSelectedQuestIndex].stageDescription.Get());
        }
    }

    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::AdjustScrollToSelection()
{
    if (m_nSelectedIndex < 0 || m_nSelectedIndex >= m_Options.Count())
        return;

    int pw, ph; GetSize(pw, ph);
    int btnH = (m_nActiveTab == 1) ? 46 : 34;
    int viewTop = 8 + 36 + 14;
    int detailHeight = (m_nActiveTab == 1) ? 132 : 0;
    int footerHeight = (m_nActiveTab == 0) ? 42 : 0;
    int viewHeight = ph - viewTop - 12 - detailHeight - footerHeight;
    int listCount = m_Options.Count();
    int contentHeight = (btnH + 8) * listCount;
    int maxOffset = contentHeight - viewHeight;
    if (maxOffset < 0) maxOffset = 0;

    int selectedTop = viewTop + m_nSelectedIndex * (btnH + 8);
    int selectedBottom = selectedTop + btnH;

    if (selectedTop - m_nContentOffset < viewTop)
        m_nContentOffset = selectedTop - viewTop;
    else if (selectedBottom - m_nContentOffset > viewTop + viewHeight)
        m_nContentOffset = selectedBottom - (viewTop + viewHeight);

    if (m_nContentOffset < 0) m_nContentOffset = 0;
    if (m_nContentOffset > maxOffset) m_nContentOffset = maxOffset;

    for (int i = 0; i < m_Options.Count(); ++i)
    {
        int placedYLocal = (i * (btnH + 8)) - m_nContentOffset;
        int bx, by, bw, bh;
        m_Options[i]->GetBounds(bx, by, bw, bh);
        m_Options[i]->SetBounds(0, placedYLocal, bw, bh);
    }

    if (m_nActionIndex >= 0 && m_pActionUseBtn && m_pActionUseBtn->IsVisible())
        ShowActions(m_nActionIndex);
}

void CVGuiInventoryPanel::SetActionSelection(int action)
{
    if (action < 0) action = 0;
    if (action > 1) action = 1;
    m_nSelectedAction = action;
    UpdateArmedState();
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::PerformSelectedAction()
{
    if (m_nActiveTab != 0)
        return;

    if (m_nSelectedIndex < 0 || m_nSelectedIndex >= m_Items.Count())
        return;

    char cmdbuf[64];
    if (m_nSelectedAction == 1)
        Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_drop %d", m_nSelectedIndex);
    else
    {
        const char *raw = m_Items[m_nSelectedIndex].Get();
        char clsbuf[201];
        int cnt = 1;
        bool hasExplicit = false;
        ExtractLootEntrySummary(raw, clsbuf, sizeof(clsbuf), cnt, hasExplicit);
        if (IsAmmoClassnameForInventory(clsbuf) && cnt > 1)
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_split %d", m_nSelectedIndex);
        else
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_use %d", m_nSelectedIndex);
    }

    OnCommand(cmdbuf);
}

static bool IsPointInsidePanelLocal(Panel *p, int x, int y)
{
    if (!p || !p->IsVisible())
        return false;
    int px, py, pw, ph;
    p->GetBounds(px, py, pw, ph);
    return (x >= px && x < px + pw && y >= py && y < py + ph);
}

void CVGuiInventoryPanel::HideActions()
{
    if (m_pActionUseBtn)
    {
        m_pActionUseBtn->SetVisible(false);
        m_pActionUseBtn->SetEnabled(false);
        m_pActionUseBtn->SetMouseInputEnabled(false);
    }
    if (m_pActionDropBtn)
    {
        m_pActionDropBtn->SetVisible(false);
        m_pActionDropBtn->SetEnabled(false);
        m_pActionDropBtn->SetMouseInputEnabled(false);
    }
    m_nActionIndex = -1;
    m_nSelectedAction = 0;
    UpdateArmedState();
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::ShowSplitPopup(int idx)
{
    if (idx < 0 || idx >= m_Items.Count())
        return;

    const char *raw = m_Items[idx].Get();
    char clsbuf[201];
    int cnt = 1;
    bool hasExplicit = false;
    ExtractLootEntrySummary(raw, clsbuf, sizeof(clsbuf), cnt, hasExplicit);
    if (!IsAmmoClassnameForInventory(clsbuf))
        return;
    if (cnt <= 1)
        return;

    if (!m_pSplitPanel)
    {
        m_pSplitPanel = new Panel(this, "SplitPanel");
        m_pSplitPanel->SetPaintBackgroundEnabled(true);
        m_pSplitPanel->SetBgColor(Color(0, 0, 0, 230));
        m_pSplitPanel->SetVisible(false);
        m_pSplitPanel->SetMouseInputEnabled(true);
        m_pSplitPanel->SetKeyBoardInputEnabled(true);

        m_pSplitTitleLabel = new CSplitGoldLabel(m_pSplitPanel, "SplitTitle", "");
        m_pSplitTitleLabel->SetContentAlignment(Label::a_center);

        Label *amountLabel = new CSplitGoldLabel(m_pSplitPanel, "SplitAmountLabel", "Amount:");
        amountLabel->SetContentAlignment(Label::a_west);

        m_pSplitAmountEntry = new CSplitAmountTextEntry(m_pSplitPanel, "SplitAmountEntry");

        m_pSplitConfirmBtn = new CInvOptionButton(m_pSplitPanel, "SplitConfirm", "Confirm", -400, this);
        m_pSplitConfirmBtn->SetCommand("split_confirm");
        m_pSplitConfirmBtn->AddActionSignalTarget(this);

        m_pSplitCancelBtn = new CInvOptionButton(m_pSplitPanel, "SplitCancel", "Cancel", -401, this);
        m_pSplitCancelBtn->SetCommand("split_cancel");
        m_pSplitCancelBtn->AddActionSignalTarget(this);
    }

    m_nSplitIndex = idx;
    m_nSplitMax = cnt - 1;
    m_bSplitVisible = true;

    char title[256];
    char display[256];
    GetFriendlyLabelAndCount(clsbuf, cnt, true, display, sizeof(display));
    Q_snprintf(title, sizeof(title), "Split: %s", display);
    if (m_pSplitTitleLabel)
        m_pSplitTitleLabel->SetText(title);

    if (m_pSplitAmountEntry)
        m_pSplitAmountEntry->SetText("1");

    int pw, ph;
    GetSize(pw, ph);
    int w = 320;
    int h = 140;
    int x = (pw - w) / 2;
    int y = (ph - h) / 2;
    m_pSplitPanel->SetBounds(x, y, w, h);

    if (m_pSplitTitleLabel)
        m_pSplitTitleLabel->SetBounds(12, 12, w - 24, 24);

    Panel *amountLabel = m_pSplitPanel->FindChildByName("SplitAmountLabel");
    if (amountLabel)
        amountLabel->SetBounds(16, 54, 90, 22);

    if (m_pSplitAmountEntry)
        m_pSplitAmountEntry->SetBounds(112, 52, w - 128, 26);

    if (m_pSplitConfirmBtn)
        m_pSplitConfirmBtn->SetBounds(w - 188, h - 40, 84, 28);
    if (m_pSplitCancelBtn)
        m_pSplitCancelBtn->SetBounds(w - 96, h - 40, 84, 28);

    m_pSplitPanel->SetVisible(true);
    m_pSplitPanel->MoveToFront();
    if (m_pSplitAmountEntry)
    {
        m_pSplitAmountEntry->RequestFocus();
        m_pSplitAmountEntry->SelectAllText(true);
    }
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::HideSplitPopup()
{
    m_bSplitVisible = false;
    m_nSplitIndex = -1;
    m_nSplitMax = 0;
    if (m_pSplitPanel)
        m_pSplitPanel->SetVisible(false);
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::ShowActions(int idx)
{
    if (!m_pActionUseBtn || !m_pActionDropBtn || !m_pListPanel)
        return;
    if (idx < 0 || idx >= m_Options.Count())
        return;

    bool isAmmo = false;
    int ammoCount = 1;
    if (idx >= 0 && idx < m_Items.Count())
    {
        const char *raw = m_Items[idx].Get();
        char clsbuf[201];
        int cnt = 1;
        bool hasExplicit = false;
        ExtractLootEntrySummary(raw, clsbuf, sizeof(clsbuf), cnt, hasExplicit);
        isAmmo = IsAmmoClassnameForInventory(clsbuf);
        ammoCount = cnt;
        if (isAmmo)
        {
            m_pActionUseBtn->SetText("Split");
            m_pActionUseBtn->SetEnabled(ammoCount > 1);
            m_pActionUseBtn->SetMouseInputEnabled(ammoCount > 1);
        }
        else
        {
            m_pActionUseBtn->SetText("Use");
        }
    }

    int x, y, w, h;
    m_Options[idx]->GetBounds(x, y, w, h);

    int actionW = 80;
    int actionH = 24;
    int gap = 6;
    int totalW = (actionW * 2) + gap;
    int actionX = x + w - totalW - 4;
    int actionY = y + (h - actionH) / 2;

    char cmdbuf[64];
    if (isAmmo)
        Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_split %d", idx);
    else
        Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_use %d", idx);
    m_pActionUseBtn->SetCommand(cmdbuf);
    Q_snprintf(cmdbuf, sizeof(cmdbuf), "inv_action_drop %d", idx);
    m_pActionDropBtn->SetCommand(cmdbuf);

    bool enableUse = !isAmmo || (ammoCount > 1);
    m_pActionUseBtn->SetVisible(true);
    m_pActionDropBtn->SetVisible(true);
    m_pActionUseBtn->SetEnabled(enableUse);
    m_pActionDropBtn->SetEnabled(true);
    m_pActionUseBtn->SetMouseInputEnabled(enableUse);
    m_pActionDropBtn->SetMouseInputEnabled(true);
    m_pActionUseBtn->MoveToFront();
    m_pActionDropBtn->MoveToFront();

    int startX1 = actionX - 10;
    int startX2 = actionX + actionW + gap - 10;
    m_pActionUseBtn->SetBounds(startX1, actionY, actionW, actionH);
    m_pActionDropBtn->SetBounds(startX2, actionY, actionW, actionH);
    m_pActionUseBtn->SetAlpha(0);
    m_pActionDropBtn->SetAlpha(0);

    AnimationController *anim = g_pClientMode ? g_pClientMode->GetViewportAnimationController() : nullptr;
    if (anim)
    {
        anim->RunAnimationCommand(m_pActionUseBtn, "XPos", actionX, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pActionDropBtn, "XPos", actionX + actionW + gap, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pActionUseBtn, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
        anim->RunAnimationCommand(m_pActionDropBtn, "Alpha", 255.0f, 0.0f, 0.15f, AnimationController::INTERPOLATOR_DEACCEL);
    }
    else
    {
        m_pActionUseBtn->SetBounds(actionX, actionY, actionW, actionH);
        m_pActionDropBtn->SetBounds(actionX + actionW + gap, actionY, actionW, actionH);
        m_pActionUseBtn->SetAlpha(255);
        m_pActionDropBtn->SetAlpha(255);
    }

    m_nActionIndex = idx;
    UpdateArmedState();
    InvalidateLayout();
    Repaint();
}

void CVGuiInventoryPanel::UpdateArmedState()
{
    for (int i = 0; i < m_Options.Count(); ++i)
    {
        if (m_Options[i])
        {
            bool suppress = AreActionsVisibleForItem(i);
            m_Options[i]->SetArmed(!suppress && m_bSelectionVisible && (i == m_nSelectedIndex));
        }
    }
    if (m_pActionUseBtn)
        m_pActionUseBtn->SetArmed((m_nActionIndex >= 0) && (m_nSelectedAction == 0));
    if (m_pActionDropBtn)
        m_pActionDropBtn->SetArmed((m_nActionIndex >= 0) && (m_nSelectedAction == 1));
}

void CVGuiInventoryPanel::StartDrag(int index)
{
    m_nDragIndex = index;
    // ensure we repaint while dragging
    vgui::ivgui()->AddTickSignal(GetVPanel());
}

void CVGuiInventoryPanel::EndDrag()
{
    if (m_nDragIndex < 0 || m_nDragIndex >= m_Items.Count())
    {
        m_nDragIndex = -1;
        return;
    }

    int sx = 0, sy = 0;
    vgui::input()->GetCursorPos(sx, sy);
    int lx = sx, ly = sy;
    ScreenToLocal(lx, ly);

    int target = -1;
    for (int i = 0; i < m_Options.Count(); ++i)
    {
        int bx, by, bw, bh;
        m_Options[i]->GetBounds(bx, by, bw, bh);
        if (lx >= bx && lx <= bx + bw && ly >= by && ly <= by + bh)
        {
            target = i;
            break;
        }
    }

        if (target >= 0 && target != m_nDragIndex)
        {
            // Only attempt stacking for same-class items (quick compare of classnames)
            const char *rawSrc = m_Items[m_nDragIndex].Get();
            const char *rawDst = m_Items[target].Get();
            const char *sep1 = strchr(rawSrc, ':');
            const char *sep2 = strchr(rawDst, ':');
            char srcCls[256]; char dstCls[256];
            if (sep1) { int len = sep1 - rawSrc; if (len > 255) len = 255; Q_strncpy(srcCls, rawSrc, len+1); srcCls[len]='\0'; }
            else { Q_strncpy(srcCls, rawSrc, sizeof(srcCls)); srcCls[255] = '\0'; }
            if (sep2) { int len = sep2 - rawDst; if (len > 255) len = 255; Q_strncpy(dstCls, rawDst, len+1); dstCls[len]='\0'; }
            else { Q_strncpy(dstCls, rawDst, sizeof(dstCls)); dstCls[255] = '\0'; }

            if (!Q_stricmp(srcCls, dstCls))
            {
                // avoid sending duplicate stack requests while one is in progress
                if (!m_bStackInProgress)
                {
                    // send indices so server stacks the exact entries the client dragged
                    CUtlString esc;
                    for (const char *p = srcCls; p && *p; ++p)
                    {
                        if (*p == '"' || *p == '\\') esc.Append("\\");
                        char tmp[2] = {*p, '\0'}; esc.Append(tmp);
                    }
                    char cmdbuf[256];
                    Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_stack \"%s\" %d %d\n", esc.Get(), m_nDragIndex, target);
                    engine->ClientCmd(cmdbuf);
                    m_bStackInProgress = true;
                }
            }
        }

    m_nDragIndex = -1;
}

void CVGuiInventoryPanel::OnTick()
{
    if (m_pTimeLabel)
    {
        CEnvTimecycle *tc = GetTimecycle();
        if (tc)
        {
            float hours = tc->GetTimeOfDayHours();
            int hh = (int)floorf(hours) % 24;
            int mm = (int)floorf((hours - floorf(hours)) * 60.0f + 0.5f) % 60;
            char buf[64];
            Q_snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
            m_pTimeLabel->SetText(buf);
        }
        else
        {
            m_pTimeLabel->SetText("");
        }
    }

    if (m_nDragIndex >= 0)
        Repaint();

    if (m_bPendingModalRelease && gpGlobals && gpGlobals->curtime >= m_flModalReleaseTime)
    {
        vgui::input()->SetAppModalSurface(NULL);
        m_bPendingModalRelease = false;
    }
}

void CVGuiInventoryPanel::OnMousePressed(MouseCode code)
{
    int mx, my;
    input()->GetCursorPos(mx, my);
    ScreenToLocal(mx, my);

    if (m_pListPanel)
    {
        int lx, ly, lw, lh;
        m_pListPanel->GetBounds(lx, ly, lw, lh);
        if (mx >= lx && mx < lx + lw && my >= ly && my < ly + lh)
        {
            int inX = mx - lx;
            int inY = my - ly;

            if (IsPointInsidePanelLocal(m_pActionUseBtn, inX, inY) || IsPointInsidePanelLocal(m_pActionDropBtn, inX, inY))
            {
                BaseClass::OnMousePressed(code);
                return;
            }

            for (int i = 0; i < m_Options.Count(); ++i)
            {
                if (IsPointInsidePanelLocal(m_Options[i], inX, inY))
                {
                    m_bSelectionVisible = true;
                    SelectItem(i);
                    BaseClass::OnMousePressed(code);
                    return;
                }
            }
        }
    }

    HideActions();
    BaseClass::OnMousePressed(code);
}

void CVGuiInventoryPanel::OnKeyCodePressed(vgui::KeyCode code)
{
    if (m_bSplitVisible)
    {
        if (code == KEY_ENTER || code == KEY_PAD_ENTER)
        {
            OnCommand("split_confirm");
            return;
        }
        BaseClass::OnKeyCodePressed(code);
        return;
    }
    // Toggle/close inventory with TAB or I (previously client bind relied on keyboard input disabled)
    if (code == KEY_TAB || code == KEY_I)
    {
        CVGuiInventoryPanel *panel = GetGlobalInventoryPanel();
        if (panel && panel->IsVisible())
        {
            engine->ClientCmd("inventory_open_end\n");
        }
        else
        {
            engine->ClientCmd("inventory_request\n");
        }
        return;
    }
    if (code == KEY_W || code == KEY_A || code == KEY_S || code == KEY_D || code == KEY_SPACE)
        return;

    if (m_nActiveTab == 1)
    {
        if (code == KEY_LEFT)
        {
            m_nActiveTab = 0;
            BeginInventory();
            return;
        }
        if (code == KEY_UP)
        {
            SelectQuest(m_nSelectedQuestIndex - 1);
            return;
        }
        if (code == KEY_DOWN)
        {
            SelectQuest(m_nSelectedQuestIndex + 1);
            return;
        }
        if (code == KEY_ENTER || code == KEY_PAD_ENTER)
        {
            if (m_nSelectedQuestIndex < 0)
                SelectQuest(0);
            return;
        }
        BaseClass::OnKeyCodePressed(code);
        return;
    }

    if (code == KEY_UP)
    {
        SelectItem(m_nSelectedIndex - 1);
        return;
    }
    if (code == KEY_DOWN)
    {
        SelectItem(m_nSelectedIndex + 1);
        return;
    }

    if (code == KEY_RIGHT)
    {
        m_bSelectionVisible = true;
        if (m_nSelectedIndex >= 0 && m_nSelectedIndex < m_Options.Count())
        {
            if (m_nActionIndex < 0)
            {
                ShowActions(m_nSelectedIndex);
                SetActionSelection(0);
            }
            else
            {
                SetActionSelection(1);
            }
        }
        return;
    }

    if (code == KEY_LEFT)
    {
        m_bSelectionVisible = true;
        if (m_nActionIndex >= 0)
        {
            if (m_nSelectedAction == 1)
                SetActionSelection(0);
            else
                HideActions();
        }
        return;
    }

    if (code == KEY_ENTER || code == KEY_PAD_ENTER)
    {
        m_bSelectionVisible = true;
        if (m_nSelectedIndex < 0 || m_nSelectedIndex >= m_Options.Count())
            return;

        if (m_nActionIndex < 0)
        {
            ShowActions(m_nSelectedIndex);
            SetActionSelection(0);
        }
        else
        {
            PerformSelectedAction();
        }
        return;
    }

    BaseClass::OnKeyCodePressed(code);
}

void CVGuiInventoryPanel::OnKeyCodeTyped(vgui::KeyCode code)
{
    if (code == KEY_ESCAPE)
    {
        if (m_bSplitVisible)
        {
            HideSplitPopup();
            return;
        }
        engine->ClientCmd_Unrestricted("gameui_hide\n");
        engine->ClientCmd_Unrestricted("wait;gameui_hide\n");
        ScheduleModalRelease(0.10f);
        engine->ClientCmd("inventory_open_end\n");
        return;
    }
    BaseClass::OnKeyCodeTyped(code);
}

void CVGuiInventoryPanel::OnMouseWheeled(int delta)
{
    if (!m_pListPanel || m_Options.Count() == 0)
        return;

    int mx, my;
    input()->GetCursorPos(mx, my);
    ScreenToLocal(mx, my);

    int lx, ly, lw, lh;
    m_pListPanel->GetBounds(lx, ly, lw, lh);
    bool overList = (mx >= lx && mx < lx + lw && my >= ly && my < ly + lh);
    if (overList)
    {
        const int step = 60;
        int d = (delta > 0) ? -step : step;
        HideActions();
        m_bSelectionVisible = false;

        m_nContentOffset += d;

        int pw, ph; GetSize(pw, ph);
        int viewTop = 8 + 36 + 14;
        int detailHeight = (m_nActiveTab == 1) ? 132 : 0;
        int footerHeight = (m_nActiveTab == 0) ? 42 : 0;
        int viewHeight = ph - viewTop - 12 - detailHeight - footerHeight;
        int btnH = (m_nActiveTab == 1) ? 46 : 34;
        int listCount = m_Options.Count();
        int contentHeight = (btnH + 8) * listCount;
        int maxOffset = contentHeight - viewHeight;
        if (maxOffset < 0) maxOffset = 0;
        if (m_nContentOffset < 0) m_nContentOffset = 0;
        if (m_nContentOffset > maxOffset) m_nContentOffset = maxOffset;

        for (int i = 0; i < m_Options.Count(); ++i)
        {
            int bx, by, bw, bh;
            m_Options[i]->GetBounds(bx, by, bw, bh);
            int placedYLocal = (i * (btnH + 8)) - m_nContentOffset;
            m_Options[i]->SetBounds(0, placedYLocal, bw, bh);
        }

        InvalidateLayout();
        Repaint();
        return;
    }

    int dir = (delta > 0) ? -1 : 1;
    int idx = m_nSelectedIndex;
    if (idx < 0 || idx >= m_Options.Count())
        idx = 0;
    SelectItem(idx + dir);
}

void CVGuiInventoryPanel::SetItems(const CUtlVector<CUtlString> &items)
{
    m_Items.RemoveAll();
    for (int i = 0; i < items.Count(); ++i)
        m_Items.AddToTail(items[i]);
    // New inventory arrived from server: clear any in-progress stack lock and rebuild UI
    m_bStackInProgress = false;
    if (IsVisible())
        BeginInventory();
    else
    {
        InvalidateLayout();
        Repaint();
    }
}

void CVGuiInventoryPanel::SetWeight(float curWeight, float maxWeight, bool bOverencumbered)
{
    m_flCurWeight = curWeight;
    m_flMaxWeight = maxWeight;
    m_bOverencumbered = bOverencumbered;
    if (IsVisible() && m_nActiveTab == 0)
        BeginInventory();
    else
    {
        InvalidateLayout();
        Repaint();
    }
}

void CVGuiInventoryPanel::SetQuests(const CUtlVector<QuestClientEntry> &quests)
{
    m_Quests.RemoveAll();
    for (int i = 0; i < quests.Count(); ++i)
        m_Quests.AddToTail(quests[i]);
    if (IsVisible() && m_nActiveTab == 1)
        BeginInventory();
    else
    {
        InvalidateLayout();
        Repaint();
    }
}

void CVGuiInventoryPanel::EndInventory()
{
    bool wasVisible = IsVisible();
    if (wasVisible)
    {
        PlayLocalUISound("npc\\combine_soldier\\zipline_clothing2.wav", CHAN_INVENTORY_PANEL_CLOSE_A, 0.22f);
        //PlayLocalUISound("npc\\combine_soldier\\zipline2.wav", CHAN_INVENTORY_PANEL_CLOSE_B, 0.22f); ���� ������� �� ���������, ��������������
    }
    HideSplitPopup();
    HideActions();
    SetVisible(false);
    vgui::ivgui()->RemoveTickSignal( GetVPanel() );
    if (!m_bPendingModalRelease)
        ScheduleModalRelease(0.0f);
    engine->ClientCmd("inventory_ui_open 0\n");
    engine->ClientCmd_Unrestricted("gameui_allowescapetoshow\n");
    SetKeyBoardInputEnabled(false);
    SetMouseInputEnabled(false);
}

void CVGuiInventoryPanel::PaintBackground()
{
    int w,h; GetSize(w,h);
    Color c = GetBgColor();
    surface()->DrawSetColor(c.r(), c.g(), c.b(), c.a());
    surface()->DrawFilledRect(0,0,w,h);
    surface()->DrawSetColor(60,60,60,200);
    surface()->DrawOutlinedRect(0,0,w-1,h-1);

    if (m_pTabInventoryBtn && m_pTabQuestsBtn)
    {
        int x0, y0, w0, h0;
        m_pTabInventoryBtn->GetBounds(x0, y0, w0, h0);

        surface()->DrawSetColor(255, 200, 30, 255);
        surface()->DrawFilledRect(x0 + w0, y0, x0 + w0 + 1, y0 + h0);

        vgui::Button *active = (m_nActiveTab == 0) ? m_pTabInventoryBtn : m_pTabQuestsBtn;
        int ax, ay, aw, ah;
        active->GetBounds(ax, ay, aw, ah);
        surface()->DrawFilledRect(ax, ay + ah - 2, ax + aw, ay + ah);

        vgui::Button *hover = NULL;
        if (m_nActiveTab != 0 && m_pTabInventoryBtn->IsArmed())
            hover = m_pTabInventoryBtn;
        if (m_nActiveTab != 1 && m_pTabQuestsBtn->IsArmed())
            hover = m_pTabQuestsBtn;
        if (hover)
        {
            int hx, hy, hw, hh;
            hover->GetBounds(hx, hy, hw, hh);
            surface()->DrawSetColor(255, 200, 30, 160);
            surface()->DrawFilledRect(hx, hy + hh - 2, hx + hw, hy + hh);
        }
    }
}

void CVGuiInventoryPanel::Paint()
{
    // Let base paint frame background and children
    BaseClass::Paint();

    if (m_pQuestDescLabel)
        m_pQuestDescLabel->SetFgColor(Color(255, 200, 30, 255));

    // If dragging an item, draw a ghost near the cursor showing the item's label
    if (m_nDragIndex >= 0 && m_nDragIndex < m_Items.Count())
    {
        int sx, sy; vgui::input()->GetCursorPos(sx, sy);
        int lx = sx, ly = sy;
        ScreenToLocal(lx, ly);

        // build display text for dragged item (reuse parsing logic)
        const char *raw = m_Items[m_nDragIndex].Get();
        const char *sep = strchr(raw, ':');
        char clsbuf[201];
        int cnt = 1; bool hasExplicit = false;
        if (sep)
        {
            int clen = sep - raw; if (clen > 200) clen = 200;
            Q_strncpy(clsbuf, raw, clen+1); clsbuf[clen] = '\0';
            cnt = atoi(sep + 1); hasExplicit = true;
        }
        else
        {
            Q_strncpy(clsbuf, raw, sizeof(clsbuf)); clsbuf[sizeof(clsbuf)-1] = '\0'; hasExplicit = false;
        }
        char display[256];
        GetFriendlyLabelAndCount(clsbuf, cnt, hasExplicit, display, sizeof(display));

        // Draw a semi-transparent full-size button ghost matching the source button size
        int srcX, srcY, srcW, srcH;
        m_Options[m_nDragIndex]->GetBounds(srcX, srcY, srcW, srcH);

        // Use the button size, scaled down slightly for visual offset
        int bw = srcW; int bh = srcH;
        int bx = lx - (bw/2); // center under cursor
        int by = ly - (bh/2);

        int pw, ph; GetSize(pw, ph);
        if (bx < 4) bx = 4;
        if (by < 4) by = 4;
        if (bx + bw > pw - 4) bx = pw - bw - 4;
        if (by + bh > ph - 4) by = ph - bh - 4;

        // semi-transparent background that matches button visuals
        surface()->DrawSetColor(28,28,28,180);
        surface()->DrawFilledRect(bx, by, bx + bw, by + bh);
        // gold outline
        surface()->DrawSetColor(255,200,30,200);
        surface()->DrawOutlinedRect(bx, by, bx + bw - 1, by + bh - 1);

        // draw label centered inside ghost
        vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(GetScheme());
        HFont hFont = pScheme->GetFont("DefaultLarge", true);
        wchar_t wMsg[256];
        g_pVGuiLocalize->ConvertANSIToUnicode(display, wMsg, sizeof(wMsg));
        int tw = 0, th = 0; surface()->GetTextSize(hFont, wMsg, tw, th);
        int tx = bx + (bw - tw) / 2;
        int ty = by + (bh - th) / 2;

        // shadow + main text
        surface()->DrawSetTextFont(hFont);
        surface()->DrawSetTextColor(10,10,10,200);
        surface()->DrawSetTextPos(tx + 1, ty + 1);
        surface()->DrawUnicodeString(wMsg, vgui::FONT_DRAW_NONADDITIVE);
        surface()->DrawSetTextColor(255,200,30,255);
        surface()->DrawSetTextPos(tx, ty);
        surface()->DrawUnicodeString(wMsg, vgui::FONT_DRAW_NONADDITIVE);
    }
}

void CVGuiInventoryPanel::OnCommand(const char *command)
{
    if (!Q_stricmp(command, "tab_inventory"))
    {
        m_nActiveTab = 0;
        BeginInventory();
        return;
    }
    if (!Q_stricmp(command, "tab_quests"))
    {
        m_nActiveTab = 1;
        engine->ClientCmd("quest_request\n");
        BeginInventory();
        return;
    }
    if (!Q_stricmp(command, "quest_toggle_completed"))
    {
        m_bShowCompletedQuests = !m_bShowCompletedQuests;
        BeginInventory();
        return;
    }
    if (!Q_strnicmp(command, "quest_select ", 13))
    {
        int idx = atoi(command + 13);
        SelectQuest(idx);
        return;
    }
    if (!Q_strnicmp(command, "quest_select_row ", 17))
    {
        int row = atoi(command + 17);
        if (row >= 0 && row < m_QuestRowToQuestIndex.Count())
        {
            int questIndex = m_QuestRowToQuestIndex[row];
            if (questIndex >= 0)
            {
                m_nSelectedIndex = row;
                m_bSelectionVisible = true;
                SelectQuest(questIndex);
                AdjustScrollToSelection();
                UpdateArmedState();
            }
        }
        return;
    }
    if (!Q_stricmp(command, "quest_show_on_map"))
    {
        engine->ClientCmd("ui_toast \"quest\" \"Show on map not implemented\" \"\"\n");
        return;
    }

    if (m_nActiveTab != 0)
    {
        BaseClass::OnCommand(command);
        return;
    }

    if (!Q_stricmp(command, "inv_sort"))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ACTION, 0.14f);
        HideActions();
        engine->ClientCmd("inventory_sort\n");
        return;
    }
    if (!Q_stricmp(command, "inv_unequip_suit"))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ACTION, 0.14f);
        HideActions();
        engine->ClientCmd("inventory_unequip_suit\n");
        return;
    }

    if (!Q_strnicmp(command, "use_inv ", 8))
    {
        if (m_nDragIndex >= 0)
            return;
        int idx = atoi(command + 8);
        if (idx >= 0 && idx < m_Items.Count())
        {
            if (idx == m_nActionIndex && m_pActionUseBtn && m_pActionUseBtn->IsVisible())
            {
                HideActions();
                return;
            }
            PlayLocalUISound("buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ITEM, 0.16f);
            m_nSelectedAction = -1;
            ShowActions(idx);
        }
        return;
    }

    if (!Q_strnicmp(command, "inv_action_split ", 17))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 17);
        HideActions();
        ShowSplitPopup(idx);
        return;
    }

    if (!Q_strnicmp(command, "inv_action_use ", 15))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 15);
        if (idx >= 0 && idx < m_Items.Count())
        {
            const char *raw = m_Items[idx].Get();
            char cls[256];
            ExtractClassnameFromRaw(raw, cls, sizeof(cls));

            CUtlString esc;
            for (const char *p = cls; p && *p; ++p)
            {
                if (*p == '"' || *p == '\\') esc.Append("\\");
                char tmp[2] = {*p, '\0'}; esc.Append(tmp);
            }
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_use \"%s\"\n", esc.Get());
            engine->ClientCmd(cmdbuf);
        }
        HideActions();
        return;
    }

    if (!Q_stricmp(command, "split_confirm"))
    {
        if (m_bSplitVisible && m_pSplitAmountEntry && m_nSplitIndex >= 0 && m_nSplitIndex < m_Items.Count())
        {
            char buf[64];
            m_pSplitAmountEntry->GetText(buf, sizeof(buf));
            int amount = atoi(buf);
            if (amount < 1) amount = 1;
            if (m_nSplitMax > 0 && amount > m_nSplitMax) amount = m_nSplitMax;
            if (amount > 0)
            {
                char cmdbuf[128];
                Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_split %d %d\n", m_nSplitIndex, amount);
                engine->ClientCmd(cmdbuf);
            }
        }
        HideSplitPopup();
        return;
    }

    if (!Q_stricmp(command, "split_cancel"))
    {
        HideSplitPopup();
        return;
    }

    if (!Q_strnicmp(command, "inv_action_drop ", 16))
    {
        PlayLocalUISound("ui/buttonclickrelease.wav", CHAN_INVENTORY_CLICK_ACTION, 0.14f);
        int idx = atoi(command + 16);
        if (idx >= 0 && idx < m_Items.Count())
        {
            const char *raw = m_Items[idx].Get();
            char cls[256];
            ExtractClassnameFromRaw(raw, cls, sizeof(cls));

            CUtlString esc;
            for (const char *p = cls; p && *p; ++p)
            {
                if (*p == '"' || *p == '\\') esc.Append("\\");
                char tmp[2] = {*p, '\0'}; esc.Append(tmp);
            }
            char cmdbuf[256];
            Q_snprintf(cmdbuf, sizeof(cmdbuf), "inventory_drop \"%s\"\n", esc.Get());
            engine->ClientCmd(cmdbuf);
        }
        HideActions();
        return;
    }

    BaseClass::OnCommand(command);
}

// Client console callbacks to receive inventory payload from server
static void __Cmd_Inventory_Open_Payload(const CCommand &args)
{
    if (args.ArgC() < 2) return;
    CVGuiLootPanel *lootPanel = GetGlobalLootPanel();
    if (lootPanel && lootPanel->IsVisible())
        return;
    const char *payload = args.Arg(1);
    CUtlVector<CUtlString> items;
    CUtlString tmp;
    for (const char *p = payload; *p; ++p)
    {
        if (*p == ';') { items.AddToTail(tmp); tmp.Clear(); }
        else { char t[2] = {*p, '\0'}; tmp.Append(t); }
    }
    if (tmp.Get()[0]) items.AddToTail(tmp);

    CVGuiInventoryPanel *panel = GetGlobalInventoryPanel();
    if (panel)
        panel->SetItems(items);
}

static void __Cmd_Inventory_Open_End(const CCommand &args)
{
    CVGuiInventoryPanel *panel = GetGlobalInventoryPanel();
    if (panel) panel->EndInventory();
}

// Local command that user keybind will call; forwards request to server
static void __Cmd_Inventory_Request_Local(const CCommand &args)
{
    // Toggle: if panel is visible, close it locally; otherwise request from server
    CVGuiLootPanel *lootPanel = GetGlobalLootPanel();
    if (lootPanel && lootPanel->IsVisible())
        return;
    CVGuiInventoryPanel *panel = GetGlobalInventoryPanel();
    if (panel && panel->IsVisible())
    {
        engine->ClientCmd("inventory_open_end\n");
    }
    else
    {
        if (panel)
            panel->OpenInventoryTab();
        engine->ClientCmd("inventory_request\n");
    }
}

class CToastNotificationPanel : public Panel
{
public:
    CToastNotificationPanel(Panel *parent) : Panel(parent, "ToastNotification")
    {
        SetSize(300, 28);
        SetProportional(false);
        SetVisible(false);
        m_flCornerRadius = 6.0f;

        SetZPos(10000);
        SetMouseInputEnabled(false);
        SetKeyBoardInputEnabled(false);
    }
    
    void ShowToastMessage(const char *message, const char *soundName)
    {
        ToastEntry e;
        e.showTime = gpGlobals->curtime;
        e.duration = 1.8f;
        e.isReward = false;
        e.count = 1;
        e.prefixW = 0;
        e.itemW = 0;
        e.spaceW = 0;

        const char *msgIn = message ? message : "";
        if (!Q_strnicmp(msgIn, "REWARD|", 7) || !Q_strnicmp(msgIn, "PICKUP|", 7))
        {
            const char *p = msgIn + 7;
            const char *bar = strchr(p, '|');
            if (bar)
            {
                e.isReward = true;
                char itemBuf[256];
                int n = (int)(bar - p);
                if (n < 0) n = 0;
                if (n > (int)(sizeof(itemBuf) - 1)) n = (int)(sizeof(itemBuf) - 1);
                Q_strncpy(itemBuf, p, n + 1);
                itemBuf[n] = '\0';
                e.item = itemBuf;
                e.count = atoi(bar + 1);
                if (e.count < 1) e.count = 1;
            }
        }

        if (e.isReward)
        {
            if (!Q_strnicmp(msgIn, "PICKUP|", 7))
                e.prefix = "You picked up";
            else
                e.prefix = "You received";
            e.itemText = e.item;
            if (e.count > 1)
            {
                char itemTextBuf[320];
                Q_snprintf(itemTextBuf, sizeof(itemTextBuf), "%s x%d", e.item.Get(), e.count);
                e.itemText = itemTextBuf;
            }
            e.message = "REWARD";
        }
        else
        {
            e.message = msgIn;
        }

        if (soundName && soundName[0])
            surface()->PlaySound(soundName);

        vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(GetScheme());
        HFont hFont = pScheme->GetFont("CenterPrintText", true);
        int textW = 0, textH = 0;
        if (e.isReward)
        {
            wchar_t wPrefix[256];
            wchar_t wItem[256];
            g_pVGuiLocalize->ConvertANSIToUnicode(e.prefix.Get(), wPrefix, sizeof(wPrefix));
            g_pVGuiLocalize->ConvertANSIToUnicode(e.itemText.Get(), wItem, sizeof(wItem));
            surface()->GetTextSize(hFont, wPrefix, e.prefixW, textH);
            surface()->GetTextSize(hFont, wItem, e.itemW, textH);
            wchar_t wSpace[8];
            g_pVGuiLocalize->ConvertANSIToUnicode(" ", wSpace, sizeof(wSpace));
            surface()->GetTextSize(hFont, wSpace, e.spaceW, textH);
            textW = e.prefixW + e.spaceW + e.itemW;
        }
        else
        {
            wchar_t wMsg[256];
            g_pVGuiLocalize->ConvertANSIToUnicode(e.message.Get(), wMsg, sizeof(wMsg));
            surface()->GetTextSize(hFont, wMsg, textW, textH);
        }

        int padH = 18;
        int padV = 10;
        e.width = textW + padH;
        e.height = textH + padV;
        if (e.width < 240) e.width = 240;

        m_Entries.AddToTail(e);
        while (m_Entries.Count() > 6)
            m_Entries.Remove(0);

        int sx, sy;
        surface()->GetScreenSize(sx, sy);
        SetPos(32, 32);

        SetVisible(true);
        SetZPos(10000);
        vgui::ivgui()->AddTickSignal(GetVPanel());
    }
    
    virtual void Paint() OVERRIDE
    {
        if (!IsVisible())
            return;
        for (int i = m_Entries.Count() - 1; i >= 0; --i)
        {
            float elapsed = gpGlobals->curtime - m_Entries[i].showTime;
            if (elapsed > m_Entries[i].duration)
                m_Entries.Remove(i);
        }

        if (m_Entries.Count() <= 0)
        {
            SetVisible(false);
            vgui::ivgui()->RemoveTickSignal(GetVPanel());
            return;
        }

        vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(GetScheme());
        HFont hFont = pScheme->GetFont("CenterPrintText", true);

        int yCursor = 0;
        for (int i = 0; i < m_Entries.Count(); ++i)
        {
            const ToastEntry &e = m_Entries[i];
            float elapsed = gpGlobals->curtime - e.showTime;
            float fadeTime = 0.28f;
            float alphaMul = 1.0f;
            if (elapsed > (e.duration - fadeTime))
            {
                float t = (elapsed - (e.duration - fadeTime)) / fadeTime;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                alphaMul = 1.0f - t;
            }

            float goldStart = 0.06f;
            float goldEnd = 0.28f;
            float goldDur = goldEnd - goldStart;
            float goldT = 0.0f;
            if (elapsed >= goldStart && elapsed <= goldEnd)
            {
                goldT = (elapsed - goldStart) / goldDur;
                goldT = sinf(goldT * M_PI);
            }

            int w = e.width;
            int h = e.height;
            int x0 = 0;
            int y0 = yCursor;
            int x1 = x0 + w;
            int y1 = y0 + h;
            yCursor += h + 6;

            surface()->DrawSetTexture(-1);
            const int NUM_CORNER_COORD = 10;
            const int NUM_BACKGROUND_COORD = NUM_CORNER_COORD * 4;
            vgui::Vertex_t vert[NUM_BACKGROUND_COORD];
            Vector2D corner[NUM_CORNER_COORD];
            for (int c = 0; c < NUM_CORNER_COORD; ++c)
            {
                corner[c].x = m_flCornerRadius * (1 - cos(((float)c / (float)(NUM_CORNER_COORD - 1)) * (M_PI / 2.0f)));
                corner[c].y = m_flCornerRadius * (1 - sin(((float)c / (float)(NUM_CORNER_COORD - 1)) * (M_PI / 2.0f)));
            }
            for (int c = 0; c < NUM_CORNER_COORD; c++)
            {
                int j = (NUM_CORNER_COORD - 1) - c;
                vert[c].Init(Vector2D(x0 + corner[c].x, y0 + corner[c].y));
                vert[c + NUM_CORNER_COORD].Init(Vector2D(x1 - corner[j].x, y0 + corner[j].y));
                vert[c + (NUM_CORNER_COORD * 2)].Init(Vector2D(x1 - corner[c].x, y1 - corner[c].y));
                vert[c + (NUM_CORNER_COORD * 3)].Init(Vector2D(x0 + corner[j].x, y1 - corner[j].y));
            }

            int bgAlpha = (int)(120 * alphaMul);
            surface()->DrawSetColor(28, 28, 28, bgAlpha);
            surface()->DrawTexturedPolygon(ARRAYSIZE(vert), vert);

            surface()->DrawSetColor(120, 120, 120, (int)(60 * alphaMul));
            surface()->DrawTexturedPolygon(ARRAYSIZE(vert), vert);

            int tx = x0 + 10;

            surface()->DrawSetTextFont(hFont);
            surface()->DrawSetTextColor(20, 20, 20, (int)(200 * alphaMul));

            if (e.isReward)
            {
                wchar_t wPrefix[256];
                wchar_t wItem[256];
                g_pVGuiLocalize->ConvertANSIToUnicode(e.prefix.Get(), wPrefix, sizeof(wPrefix));
                g_pVGuiLocalize->ConvertANSIToUnicode(e.itemText.Get(), wItem, sizeof(wItem));

                int prefixH = 0;
                int itemH = 0;
                int tmpW = 0;
                surface()->GetTextSize(hFont, wPrefix, tmpW, prefixH);
                surface()->GetTextSize(hFont, wItem, tmpW, itemH);
                int textH = (prefixH > itemH) ? prefixH : itemH;
                int yText = y0 + (h - textH) / 2;

                surface()->DrawSetTextPos(tx, yText + 1);
                surface()->DrawUnicodeString(wPrefix, vgui::FONT_DRAW_NONADDITIVE);
                surface()->DrawSetTextPos(tx + e.prefixW + e.spaceW, yText + 1);
                surface()->DrawUnicodeString(wItem, vgui::FONT_DRAW_NONADDITIVE);

                int rGray = 200, gGray = 200, bGray = 200;
                int rGold = 255, gGold = 200, bGold = 40, aGold = (int)(255 * alphaMul);
                int r = (int)(rGray + (rGold - rGray) * goldT);
                int g = (int)(gGray + (gGold - gGray) * goldT);
                int b = (int)(bGray + (bGold - bGray) * goldT);

                surface()->DrawSetTextColor(r, g, b, aGold);
                surface()->DrawSetTextPos(tx, yText);
                surface()->DrawUnicodeString(wPrefix, vgui::FONT_DRAW_NONADDITIVE);

                surface()->DrawSetTextColor(r, g, b, aGold);
                surface()->DrawSetTextPos(tx + e.prefixW + e.spaceW, yText);
                surface()->DrawUnicodeString(wItem, vgui::FONT_DRAW_NONADDITIVE);
            }
            else
            {
                wchar_t wMsg[256];
                g_pVGuiLocalize->ConvertANSIToUnicode(e.message.Get(), wMsg, sizeof(wMsg));
                int textW = 0, textH = 0;
                surface()->GetTextSize(hFont, wMsg, textW, textH);
                int yText = y0 + (h - textH) / 2;

                surface()->DrawSetTextPos(tx, yText + 1);
                surface()->DrawUnicodeString(wMsg, vgui::FONT_DRAW_NONADDITIVE);

                int rGray = 200, gGray = 200, bGray = 200, aGray = (int)(220 * alphaMul);
                int rGold = 255, gGold = 200, bGold = 40, aGold = (int)(255 * alphaMul);
                int r = (int)(rGray + (rGold - rGray) * goldT);
                int g = (int)(gGray + (gGold - gGray) * goldT);
                int b = (int)(bGray + (bGold - bGray) * goldT);
                int a = (int)(aGray + (aGold - aGray) * goldT);
                surface()->DrawSetTextColor(r, g, b, a);
                surface()->DrawSetTextPos(tx, yText);
                surface()->DrawUnicodeString(wMsg, vgui::FONT_DRAW_NONADDITIVE);
            }
        }

        int maxW = 0;
        int totalH = 0;
        for (int i = 0; i < m_Entries.Count(); ++i)
        {
            if (m_Entries[i].width > maxW) maxW = m_Entries[i].width;
            totalH += m_Entries[i].height;
            if (i < m_Entries.Count() - 1) totalH += 6;
        }
        SetSize(maxW, totalH);
    }
    
    virtual void OnTick() OVERRIDE
    {
        if (IsVisible())
            Repaint();
    }

private:
    struct ToastEntry
    {
        CUtlString message;
        float showTime;
        float duration;
        int width;
        int height;
        bool isReward;
        CUtlString prefix;
        CUtlString item;
        CUtlString itemText;
        int count;
        int prefixW;
        int itemW;
        int spaceW;
    };
    CUtlVector<ToastEntry> m_Entries;
    float m_flCornerRadius;
};

static CToastNotificationPanel *s_pToastNotification = nullptr;

CToastNotificationPanel *GetToastNotificationPanel()
{
    if (!s_pToastNotification && g_pClientMode)
    {
        // Create as child of client mode viewport
        s_pToastNotification = new CToastNotificationPanel(g_pClientMode->GetViewport());
    }
    return s_pToastNotification;
}

static void __Cmd_UI_Toast(const CCommand &args)
{
    if (args.ArgC() < 3)
        return;

    const char *message = args.Arg(2);
    const char *soundName = (args.ArgC() >= 4) ? args.Arg(3) : "";
    CToastNotificationPanel *pPanel = GetToastNotificationPanel();
    if (pPanel)
        pPanel->ShowToastMessage(message, soundName);
}

// Client console callback to display pickup notification from server
static void __Cmd_Inventory_Pickup_Notify(const CCommand &args)
{
    if (args.ArgC() < 2) 
    {
        Msg("Inventory: pickup_notify requires argument\n");
        return;
    }
    const char *itemName = args.Arg(1);
    char msg[512];
    Q_snprintf(msg, sizeof(msg), "PICKUP|%s|1", itemName ? itemName : "");
    CToastNotificationPanel *pPanel = GetToastNotificationPanel();
    if (pPanel)
    {
        pPanel->ShowToastMessage(msg, "friends/message.wav");
    }
}

static ConCommand inventory_open_payload("inventory_open_payload", __Cmd_Inventory_Open_Payload, "Open inventory with payload (internal)", FCVAR_CLIENTDLL);
static ConCommand inventory_open_end("inventory_open_end", __Cmd_Inventory_Open_End, "End inventory (internal)", FCVAR_CLIENTDLL);
static ConCommand inventory_request_local("inventory_request_local", __Cmd_Inventory_Request_Local, "Request inventory (local, bound to key)", FCVAR_CLIENTDLL);
static ConCommand inventory_pickup_notify("inventory_pickup_notify", __Cmd_Inventory_Pickup_Notify, "Pickup notification (internal)", FCVAR_CLIENTDLL);
static ConCommand ui_toast("ui_toast", __Cmd_UI_Toast, "UI toast notification (internal)", FCVAR_CLIENTDLL);

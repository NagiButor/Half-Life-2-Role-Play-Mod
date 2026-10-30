#include "cbase.h"
#include "dialogsystem_server.h"
#include "dialog_definitions.h"
#include "dialog_debug.h"
#include "player.h"
#include "entitylist.h"
#include "util.h"
#include "igameevents.h"
#include "quest_system.h"

namespace DialogSystemServer
{
    // Simple helper to get an entity name string; returns empty if none.
    static const char *GetEntityNameString(CBaseEntity *pEnt)
    {
        if (!pEnt) return "";
        return pEnt->GetEntityName().ToCStr();
    }

    // This function sends a client console command to the player to open the dialog
    // We keep the interface minimal: specify the NPC's entity_name, the dialog id (optional).
    void StartDialogForEntity(CBasePlayer *pPlayer, CBaseEntity *pNPC)
    {
        if (!pPlayer || !pNPC) return;

        DIALOG_DEVMSG("DialogSystem: StartDialogForEntity player=%s ent=%s entindex=%d\n", pPlayer->GetPlayerName(), pNPC->GetEntityName().ToCStr(), pNPC->entindex());

        const char *ename = GetEntityNameString(pNPC);
        if (!ename || !ename[0])
        {
            return;
        }

        // Find dialog for this entity and send the dialog payload to the player
        const DialogDefinitions::DSDialog *dlg = DialogDefinitions::FindDialogForEntity(ename);
        if (dlg)
        {
            DialogDefinitions::SendDialogToPlayer(pPlayer, dlg, pNPC->entindex());
        }
        else
        {
            // fallback: send begin/end so client shows empty dialog panel
            char cmd[256];
            Q_snprintf(cmd, sizeof(cmd), "dialog_open_begin \"%s\"", ename);
            engine->ClientCommand(pPlayer->edict(), cmd);
            engine->ClientCommand(pPlayer->edict(), "dialog_open_end");
        }
    }

    void HandleDialogChoice(CBasePlayer *pPlayer, int nodeId, int optionIndex, const char *entityName)
    {
        if (!pPlayer) return;
        DIALOG_DEVMSG("DialogSystem: HandleDialogChoice player=%s node=%d option=%d entity=%s\n", pPlayer->GetPlayerName(), nodeId, optionIndex, entityName ? entityName : "(null)");

        // Find dialog for this entity and locate the option to determine next actions
        if (!entityName || !entityName[0])
            return;

        const DialogDefinitions::DSDialog *dlg = DialogDefinitions::FindDialogForEntity(entityName);
        if (!dlg) return;

        const DialogDefinitions::DSDialogNode *pNode = NULL;
        for (int i = 0; i < dlg->nodes.Count(); ++i)
        {
            if (dlg->nodes[i] && dlg->nodes[i]->id == nodeId)
            {
                pNode = dlg->nodes[i];
                break;
            }
        }
        if (!pNode)
            return;
        if (optionIndex < 0 || optionIndex >= pNode->options.Count())
            return;

        const DialogDefinitions::DSDialogOption &opt = pNode->options[optionIndex];

        if (opt.requireReputation != INT_MIN)
        {
            if (QuestSystem().GetReputation(pPlayer) < opt.requireReputation)
                return;
        }
        if (opt.requireQuestId[0] && opt.requireQuestStage != INT_MIN)
        {
            const PlayerQuestState *st = QuestSystem().FindPlayerQuest(pPlayer, opt.requireQuestId);
            if (!st || st->state != kQuestState_Active || st->stage < opt.requireQuestStage)
                return;
        }
        if (opt.requireQuestStateId[0] && opt.requireQuestState != INT_MIN)
        {
            const PlayerQuestState *st = QuestSystem().FindPlayerQuest(pPlayer, opt.requireQuestStateId);
            const int state = st ? (int)st->state : (int)kQuestState_Inactive;
            if (state != opt.requireQuestState)
                return;
        }

        if (opt.grantQuestId[0])
        {
            if (!QuestSystem().GrantQuest(pPlayer, opt.grantQuestId))
                engine->ClientCommand(pPlayer->edict(), "ui_toast \"quest\" \"GrantQuest failed\" \"\"\n");
        }
        if (opt.completeQuestId[0])
        {
            if (!QuestSystem().CompleteQuest(pPlayer, opt.completeQuestId))
                engine->ClientCommand(pPlayer->edict(), "ui_toast \"quest\" \"CompleteQuest failed\" \"\"\n");
        }
        if (opt.failQuestId[0])
        {
            if (!QuestSystem().FailQuest(pPlayer, opt.failQuestId, 0))
                engine->ClientCommand(pPlayer->edict(), "ui_toast \"quest\" \"FailQuest failed\" \"\"\n");
        }
        if (opt.setQuestStageId[0] && opt.setQuestStage != INT_MIN)
        {
            if (!QuestSystem().SetQuestStage(pPlayer, opt.setQuestStageId, opt.setQuestStage))
                engine->ClientCommand(pPlayer->edict(), "ui_toast \"quest\" \"SetQuestStage failed\" \"\"\n");
        }

        if (opt.serverSequenceName[0])
        {
            DevMsg("DialogSystem: option requests server sequence '%s'\n", opt.serverSequenceName);
            CBaseEntity *pSeq = gEntList.FindEntityByName(NULL, opt.serverSequenceName);
            if (pSeq)
            {
                CBaseEntity *pOwner = gEntList.FindEntityByName(NULL, entityName);
                DevMsg("DialogSystem: starting option server sequence entity=%p owner=%p\n", pSeq, pOwner);
                pSeq->AcceptInput("BeginSequence", pOwner, pOwner, variant_t(), 0);
            }
            else
            {
                DevMsg("DialogSystem: option server sequence '%s' not found\n", opt.serverSequenceName);
            }
        }

        if (opt.nextId != -1)
        {
            int entIndex = -1;
            CBaseEntity *pFound = gEntList.FindEntityByName(NULL, entityName);
            if (pFound) entIndex = pFound->entindex();
            if (opt.spoils)
            {
                DialogDefinitions::SendDialogNodeToPlayerEx(pPlayer, dlg, opt.nextId, entIndex, NULL, true, true, -1, 1);
            }
            else
            {
                DialogDefinitions::SendDialogNodeToPlayer(pPlayer, dlg, opt.nextId, entIndex);
            }
            return;
        }

        if (opt.completeQuestId[0] && opt.afterNextId != -1)
        {
            int entIndex = -1;
            CBaseEntity *pFound = gEntList.FindEntityByName(NULL, entityName);
            if (pFound) entIndex = pFound->entindex();
            DialogDefinitions::SendDialogNodeToPlayerEx(pPlayer, dlg, opt.afterNextId, entIndex, NULL, NULL, NULL, NULL, true, false, -1, 1);
            return;
        }

        if (opt.completeQuestId[0] && opt.afterLine[0])
        {
            int entIndex = -1;
            CBaseEntity *pFound = gEntList.FindEntityByName(NULL, entityName);
            if (pFound) entIndex = pFound->entindex();
            DialogDefinitions::SendDialogNodeToPlayer(pPlayer, dlg, nodeId, entIndex, opt.afterLine, opt.afterChoreography, opt.afterSoundName, opt.afterSequenceName);
            return;
        }

        DialogDefinitions::MarkEntitySpoken(entityName);
        if (opt.spoils)
        {
            int entIndex = -1;
            CBaseEntity *pFound = gEntList.FindEntityByName(NULL, entityName);
            if (pFound) entIndex = pFound->entindex();
            DialogDefinitions::SendDialogNodeToPlayerEx(pPlayer, dlg, nodeId, entIndex, NULL, true, true, -1, 1);
            return;
        }
        if (opt.flags & 1)
            engine->ClientCommand(pPlayer->edict(), "dialog_open_end\n");
    }

    void ContinueDialog(CBasePlayer *pPlayer, int nodeId, const char *entityName, int overrideAutoNextId, int overrideAutoClose)
    {
        if (!pPlayer || !entityName || !entityName[0])
            return;

        const DialogDefinitions::DSDialog *dlg = DialogDefinitions::FindDialogForEntity(entityName);
        if (!dlg)
            return;

        const DialogDefinitions::DSDialogNode *pNode = NULL;
        for (int i = 0; i < dlg->nodes.Count(); ++i)
        {
            if (dlg->nodes[i] && dlg->nodes[i]->id == nodeId)
            {
                pNode = dlg->nodes[i];
                break;
            }
        }
        if (!pNode)
            return;

        int effectiveAutoNextId = (overrideAutoNextId != INT_MIN) ? overrideAutoNextId : pNode->autoNextId;
        bool effectiveAutoClose = (overrideAutoClose >= 0) ? (overrideAutoClose != 0) : pNode->autoClose;

        if (effectiveAutoNextId != -1)
        {
            int entIndex = -1;
            CBaseEntity *pFound = gEntList.FindEntityByName(NULL, entityName);
            if (pFound) entIndex = pFound->entindex();
            DialogDefinitions::SendDialogNodeToPlayer(pPlayer, dlg, effectiveAutoNextId, entIndex);
            return;
        }

        if (effectiveAutoClose)
        {
            DialogDefinitions::MarkEntitySpoken(entityName);
            engine->ClientCommand(pPlayer->edict(), "dialog_open_end\n");
        }
    }
}

// Server-side console command handler for dialog choices
static void CC_Dialog_Choose(const CCommand &args)
{
    DevMsg("CC_Dialog_Choose invoked. ArgC=%d\n", args.ArgC());
    for (int i = 0; i < args.ArgC(); ++i)
    {
        DevMsg("  Arg[%d] = '%s'\n", i, args.Arg(i));
    }

    if (args.ArgC() < 4)
    {
        DevMsg("CC_Dialog_Choose: not enough args\n");
        return;
    }
    int nodeId = atoi(args.Arg(1));
    int optionIndex = atoi(args.Arg(2));
    const char *entityName = args.Arg(3);

    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer)
    {
        DevMsg("CC_Dialog_Choose: UTIL_GetCommandClient() returned NULL\n");
        return;
    }

    DevMsg("Player %s chose node %d option %d for entity %s\n", pPlayer->GetPlayerName(), nodeId, optionIndex, entityName);

    // Forward to dialog system handler to process the choice.
    DialogSystemServer::HandleDialogChoice(pPlayer, nodeId, optionIndex, entityName);
}

// Allow clients to invoke this server command
static ConCommand dialog_choose_cc("dialog_choose", CC_Dialog_Choose, "Handle dialog choice (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Dialog_Continue(const CCommand &args)
{
    if (args.ArgC() < 3)
        return;
    int nodeId = atoi(args.Arg(1));
    const char *entityName = args.Arg(2);
    int overrideAutoNextId = INT_MIN;
    int overrideAutoClose = -1;
    if (args.ArgC() >= 5)
    {
        overrideAutoNextId = atoi(args.Arg(3));
        overrideAutoClose = atoi(args.Arg(4));
    }

    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer)
        return;

    DialogSystemServer::ContinueDialog(pPlayer, nodeId, entityName, overrideAutoNextId, overrideAutoClose);
}

static ConCommand dialog_continue_cc("dialog_continue", CC_Dialog_Continue, "Continue dialog (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

static void CC_Dialog_Finish_Spoil(const CCommand &args)
{
    if (args.ArgC() < 3)
        return;
    const char *entityName = args.Arg(2);
    CBasePlayer *pPlayer = UTIL_GetCommandClient();
    if (!pPlayer || !entityName || !entityName[0])
        return;

    DIALOG_DEVMSG("DialogSystem: dialog_finish_spoil player=%s entity=%s\n", pPlayer->GetPlayerName(), entityName);
    DialogDefinitions::MarkEntitySpoiled(entityName);
    DialogDefinitions::MarkEntitySpoken(entityName);
    engine->ClientCommand(pPlayer->edict(), "dialog_open_end\n");
}

static ConCommand dialog_finish_spoil_cc("dialog_finish_spoil", CC_Dialog_Finish_Spoil, "Finish dialog and mark entity spoiled (server)", FCVAR_GAMEDLL | FCVAR_CLIENTCMD_CAN_EXECUTE);

// Force-open a dialog for testing from server console: dialog_force <entity_name>
static void CC_Dialog_Force(const CCommand &args)
{
    if (args.ArgC() < 2)
    {
        DevMsg("Usage: dialog_force <entity_name>\n");
        return;
    }

    const char *entityName = args.Arg(1);
    CBaseEntity *pEnt = gEntList.FindEntityByName(NULL, entityName);
    if (!pEnt)
    {
        DevMsg("dialog_force: entity '%s' not found\n", entityName);
        return;
    }

    // Use first player (index 1) for testing
    CBasePlayer *pPlayer = UTIL_PlayerByIndex(1);
    if (!pPlayer)
    {
        DevMsg("dialog_force: no player at index 1\n");
        return;
    }

    DevMsg("dialog_force: forcing dialog '%s' for player %s\n", entityName, pPlayer->GetPlayerName());
    DialogSystemServer::StartDialogForEntity(pPlayer, pEnt);
}
static ConCommand dialog_force_cc("dialog_force", CC_Dialog_Force, "Force open dialog for entity (server)", FCVAR_GAMEDLL);

// cbase.h must be first for precompiled headers
#include "cbase.h"
#include "dialog_definitions.h"
#include "dialog_debug.h"
#include "player.h"
#include "util.h"
#include <limits.h>
#include <stdarg.h>
#include <KeyValues.h>
// Use shared choreo loader to measure .vcd duration
#include "choreoscene.h"

// BlockingLoadScene is defined in the shared SceneCache implementation; declare here.
extern CChoreoScene *BlockingLoadScene( const char *filename );

#if defined( GAME_DLL )
#include "baseflex.h"
#include "ai_basenpc.h"
// Scene helpers (InstancedScriptedScene, etc.)
#include "sceneentity.h"
#include "quest_system.h"
#endif

// Helper: escape backslashes and double-quotes for a quoted console argument
static CUtlString EscapeForQuotedArg(const char *s)
{
    CUtlString out;
    if (!s)
        return out;
    for (const char *p = s; *p; ++p)
    {
        if (*p == '\\' || *p == '"')
        {
            out.Append("\\");
        }
        char tmp[2] = { *p, '\0' };
        out.Append(tmp);
    }
    return out;
}

ConVar dialog_debug("dialog_debug", "0", FCVAR_GAMEDLL, "Enable dialog system debug prints");
extern ConVar quest_reward_debug;

namespace DialogDefinitions
{
    static CUtlVector<CUtlString> g_SpoiledEntities;
    static CUtlVector<CUtlString> g_SpokenEntities;

    static CUtlDict<DSDialog*, int> g_DialogByEntity;
    static CUtlVector<DSDialog*> g_LoadedDialogs;
    static bool g_DialogsLoaded = false;

    static void DeleteDialog(DSDialog *pDialog)
    {
        if (!pDialog)
            return;
        for (int i = 0; i < pDialog->nodes.Count(); ++i)
        {
            if (pDialog->nodes[i])
            {
                pDialog->nodes[i]->options.RemoveAll();
            }
            delete pDialog->nodes[i];
        }
        pDialog->nodes.RemoveAll();
        delete pDialog;
    }

    static CUtlString NormalizeEntityKey(const char *entityName)
    {
        CUtlString out;
        if (!entityName)
            return out;
        for (const char *p = entityName; *p; ++p)
        {
            char c = (char)tolower((unsigned char)*p);
            char tmp[2] = { c, '\0' };
            out.Append(tmp);
        }
        return out;
    }

    static void ClearLoadedDialogs()
    {
        for (int i = 0; i < g_LoadedDialogs.Count(); ++i)
        {
            DeleteDialog(g_LoadedDialogs[i]);
        }
        g_LoadedDialogs.RemoveAll();
        g_DialogByEntity.RemoveAll();
        g_DialogsLoaded = false;
    }

    static bool IsTrueString(const char *s)
    {
        if (!s || !s[0])
            return false;
        if (!Q_stricmp(s, "1") || !Q_stricmp(s, "true") || !Q_stricmp(s, "yes") || !Q_stricmp(s, "on"))
            return true;
        return atoi(s) != 0;
    }

    static int ParseQuestStateString( const char *s )
    {
        if (!s || !s[0])
            return INT_MIN;
        if (!Q_stricmp(s, "active"))
            return kQuestState_Active;
        if (!Q_stricmp(s, "completed") || !Q_stricmp(s, "done"))
            return kQuestState_Completed;
        if (!Q_stricmp(s, "failed"))
            return kQuestState_Failed;
        if (!Q_stricmp(s, "inactive"))
            return kQuestState_Inactive;
        return INT_MIN;
    }

    static void ParseOptionKV(KeyValues *pOptKV, DSDialogOption &outOpt)
    {
        const char *text = pOptKV->GetString("text", "");
        Q_strncpy(outOpt.text, text ? text : "", sizeof(outOpt.text));
        outOpt.nextId = pOptKV->GetInt("next", -1);
        outOpt.flags = pOptKV->GetInt("flags", 0);
        if (IsTrueString(pOptKV->GetString("close", "0")))
            outOpt.flags |= 1;
        const char *seq = pOptKV->GetString("server_sequence", "");
        Q_strncpy(outOpt.serverSequenceName, seq ? seq : "", sizeof(outOpt.serverSequenceName));
        outOpt.spoils = IsTrueString(pOptKV->GetString("spoils", "0"));

        const char *grantQuest = pOptKV->GetString("grant_quest", "");
        Q_strncpy(outOpt.grantQuestId, grantQuest ? grantQuest : "", sizeof(outOpt.grantQuestId));
        const char *completeQuest = pOptKV->GetString("complete_quest", "");
        Q_strncpy(outOpt.completeQuestId, completeQuest ? completeQuest : "", sizeof(outOpt.completeQuestId));
        const char *failQuest = pOptKV->GetString("fail_quest", "");
        Q_strncpy(outOpt.failQuestId, failQuest ? failQuest : "", sizeof(outOpt.failQuestId));

        outOpt.setQuestStage = INT_MIN;
        Q_strncpy(outOpt.setQuestStageId, "", sizeof(outOpt.setQuestStageId));
        const char *setStage = pOptKV->GetString("set_stage", "");
        if (setStage && setStage[0])
        {
            char buf[128];
            Q_strncpy(buf, setStage, sizeof(buf));
            char *colon = Q_strstr(buf, ":");
            if (colon)
            {
                *colon = '\0';
                ++colon;
                Q_strncpy(outOpt.setQuestStageId, buf, sizeof(outOpt.setQuestStageId));
                outOpt.setQuestStage = atoi(colon);
            }
        }

        outOpt.requireReputation = pOptKV->GetInt("require_rep", INT_MIN);
        outOpt.requireQuestStage = INT_MIN;
        Q_strncpy(outOpt.requireQuestId, pOptKV->GetString("require_quest", ""), sizeof(outOpt.requireQuestId));
        if (outOpt.requireQuestId[0])
            outOpt.requireQuestStage = pOptKV->GetInt("require_stage", 0);

        outOpt.requireQuestState = INT_MIN;
        Q_strncpy(outOpt.requireQuestStateId, "", sizeof(outOpt.requireQuestStateId));
        const char *reqQuestState = pOptKV->GetString("require_quest_state", "");
        if (reqQuestState && reqQuestState[0])
        {
            char buf[128];
            Q_strncpy(buf, reqQuestState, sizeof(buf));
            char *colon = Q_strstr(buf, ":");
            if (colon)
            {
                *colon = '\0';
                ++colon;
                Q_strncpy(outOpt.requireQuestStateId, buf, sizeof(outOpt.requireQuestStateId));
                outOpt.requireQuestState = ParseQuestStateString(colon);
            }
        }

        const char *reqQuestStage = pOptKV->GetString("require_quest_stage", "");
        if (reqQuestStage && reqQuestStage[0])
        {
            char buf[128];
            Q_strncpy(buf, reqQuestStage, sizeof(buf));
            char *colon = Q_strstr(buf, ":");
            if (colon)
            {
                *colon = '\0';
                ++colon;
                Q_strncpy(outOpt.requireQuestId, buf, sizeof(outOpt.requireQuestId));
                outOpt.requireQuestStage = atoi(colon);
            }
        }

        Q_strncpy(outOpt.afterLine, pOptKV->GetString("after_line", ""), sizeof(outOpt.afterLine));
        Q_strncpy(outOpt.afterChoreography, pOptKV->GetString("after_choreo", ""), sizeof(outOpt.afterChoreography));
        Q_strncpy(outOpt.afterSoundName, pOptKV->GetString("after_sound", ""), sizeof(outOpt.afterSoundName));
        Q_strncpy(outOpt.afterSequenceName, pOptKV->GetString("after_sequence", ""), sizeof(outOpt.afterSequenceName));
        outOpt.afterNextId = pOptKV->GetInt("after_next", -1);
    }

    static void TrimWhitespaceInPlace( char *s )
    {
        if ( !s )
            return;
        char *start = s;
        while ( *start && isspace( (unsigned char)*start ) )
            ++start;
        if ( start != s )
            memmove( s, start, Q_strlen( start ) + 1 );
        int len = Q_strlen( s );
        while ( len > 0 && isspace( (unsigned char)s[len-1] ) )
        {
            s[len-1] = '\0';
            --len;
        }
    }

    static void ParseQuestsKV(KeyValues *pQuestsKV)
    {
#if defined( GAME_DLL )
        if (!pQuestsKV)
            return;

        FOR_EACH_SUBKEY(pQuestsKV, pQuestKV)
        {
            QuestDef def;
            def.id = pQuestKV->GetName();
            def.title = pQuestKV->GetString("title", def.id.Get());
            def.description = pQuestKV->GetString("desc", "");
            def.minReputation = pQuestKV->GetInt("min_rep", 0);
            def.killTarget = pQuestKV->GetString("kill_target", "");
            def.killTargets.RemoveAll();
            def.killCount = pQuestKV->GetInt("kill_count", 0);
            def.failOnKillTarget = pQuestKV->GetString("fail_on_kill", "");
            def.failOnKillTargets.RemoveAll();
            def.failOnKillRepPenalty = pQuestKV->GetInt("fail_rep_penalty", 0);
            def.stages.RemoveAll();
            def.rewards.RemoveAll();

            const char *killTargets = pQuestKV->GetString("kill_targets", "");
            if ( killTargets && killTargets[0] )
            {
                char buf[512];
                Q_strncpy( buf, killTargets, sizeof( buf ) );
                char *ctx = NULL;
                char *tok = V_strtok_s( buf, ";", &ctx );
                while ( tok )
                {
                    TrimWhitespaceInPlace( tok );
                    if ( tok[0] )
                        def.killTargets.AddToTail( tok );
                    tok = V_strtok_s( NULL, ";", &ctx );
                }
            }

            const char *failTargets = pQuestKV->GetString("fail_on_kill_targets", "");
            if ( failTargets && failTargets[0] )
            {
                char buf[512];
                Q_strncpy( buf, failTargets, sizeof( buf ) );
                char *ctx = NULL;
                char *tok = V_strtok_s( buf, ";", &ctx );
                while ( tok )
                {
                    TrimWhitespaceInPlace( tok );
                    if ( tok[0] )
                        def.failOnKillTargets.AddToTail( tok );
                    tok = V_strtok_s( NULL, ";", &ctx );
                }
            }

            KeyValues *pStagesKV = pQuestKV->FindKey("stages");
            if ( pStagesKV )
            {
                FOR_EACH_SUBKEY( pStagesKV, pStageKV )
                {
                    int stageIndex = atoi( pStageKV->GetName() );
                    if ( stageIndex < 0 )
                        continue;
                    while ( def.stages.Count() <= stageIndex )
                    {
                        def.stages.AddToTail();
                        def.stages.Tail().killCount = 0;
                        def.stages.Tail().killTarget.Clear();
                        def.stages.Tail().killTargets.RemoveAll();
                    }

                    QuestStageDef &st = def.stages[stageIndex];
                    st.title = pStageKV->GetString("title", def.title.Get());
                    st.description = pStageKV->GetString("desc", def.description.Get());
                    st.killTarget = pStageKV->GetString("kill_target", "");
                    st.killCount = pStageKV->GetInt("kill_count", 0);
                    st.killTargets.RemoveAll();

                    const char *stageTargets = pStageKV->GetString("kill_targets", "");
                    if ( stageTargets && stageTargets[0] )
                    {
                        char buf[512];
                        Q_strncpy( buf, stageTargets, sizeof( buf ) );
                        char *ctx = NULL;
                        char *tok = V_strtok_s( buf, ";", &ctx );
                        while ( tok )
                        {
                            TrimWhitespaceInPlace( tok );
                            if ( tok[0] )
                                st.killTargets.AddToTail( tok );
                            tok = V_strtok_s( NULL, ";", &ctx );
                        }
                    }
                }
            }

            KeyValues *pRewardsKV = pQuestKV->FindKey("rewards");
            if ( pRewardsKV )
            {
                CUtlDict<int, int> seenRewards;
                FOR_EACH_VALUE( pRewardsKV, pRewardKV )
                {
                    const char *rewardName = pRewardKV->GetName();
                    const char *rewardAmountStr = pRewardsKV->GetString( rewardName, "0" );
                    const int rewardAmount = atoi( rewardAmountStr );
                    if ( !rewardName || !rewardName[0] || rewardAmount <= 0 )
                        continue;
                    if (seenRewards.Find(rewardName) != seenRewards.InvalidIndex())
                        continue;
                    seenRewards.Insert(rewardName, 1);
                    def.rewards.AddToTail();
                    def.rewards.Tail().itemName = rewardName;
                    def.rewards.Tail().amount = rewardAmount;
                }
                FOR_EACH_SUBKEY( pRewardsKV, pRewardKV2 )
                {
                    const char *rewardName = pRewardKV2->GetName();
                    const char *rewardAmountStr = pRewardsKV->GetString( rewardName, "0" );
                    const int rewardAmount = atoi( rewardAmountStr );
                    if ( !rewardName || !rewardName[0] || rewardAmount <= 0 )
                        continue;
                    if (seenRewards.Find(rewardName) != seenRewards.InvalidIndex())
                        continue;
                    seenRewards.Insert(rewardName, 1);
                    def.rewards.AddToTail();
                    def.rewards.Tail().itemName = rewardName;
                    def.rewards.Tail().amount = rewardAmount;
                }
            }
            if ( quest_reward_debug.GetBool() )
            {
                DevMsg( "DialogDefinitions: parsed quest '%s' rewards=%d\n", def.id.Get(), def.rewards.Count() );
                for ( int i = 0; i < def.rewards.Count(); ++i )
                {
                    DevMsg( "  reward[%d]: '%s' x%d\n", i, def.rewards[i].itemName.Get(), def.rewards[i].amount );
                }
                if ( pRewardsKV )
                {
                    FOR_EACH_VALUE( pRewardsKV, pDbg )
                    {
                        const char *n = pDbg->GetName();
                        DevMsg( "  rewards kv value: name='%s' raw='%s' parentRaw='%s'\n", n, pDbg->GetString( NULL, "" ), pRewardsKV->GetString( n, "" ) );
                    }
                    FOR_EACH_SUBKEY( pRewardsKV, pDbg2 )
                    {
                        const char *n = pDbg2->GetName();
                        DevMsg( "  rewards kv subkey: name='%s' raw='%s' parentRaw='%s'\n", n, pDbg2->GetString( NULL, "" ), pRewardsKV->GetString( n, "" ) );
                    }
                }
            }
            QuestSystem().RegisterQuestDef(def);
        }
#endif
    }

    static void ParseNodeKV(KeyValues *pNodeKV, DSDialogNode &outNode)
    {
        outNode.speakerLine = pNodeKV->GetString("line", "");
        outNode.choreography = pNodeKV->GetString("choreo", "");
        outNode.soundName = pNodeKV->GetString("sound", "");
        outNode.sequenceName = pNodeKV->GetString("sequence", "");
        outNode.serverSequenceName = pNodeKV->GetString("server_sequence", "");
        outNode.spoils = IsTrueString(pNodeKV->GetString("spoils", "0"));
        outNode.autoNextId = pNodeKV->GetInt("auto_next", -1);
        outNode.autoClose = IsTrueString(pNodeKV->GetString("end", "0")) || IsTrueString(pNodeKV->GetString("close", "0"));

        const char *delayStr = pNodeKV->GetString("option_delay", "");
        if (!delayStr[0] || !Q_stricmp(delayStr, "auto"))
        {
            outNode.optionDelayAuto = true;
            outNode.optionDelaySeconds = 0.0f;
        }
        else
        {
            outNode.optionDelayAuto = false;
            outNode.optionDelaySeconds = (float)atof(delayStr);
        }

        outNode.options.RemoveAll();
        KeyValues *pOptsKV = pNodeKV->FindKey("options");
        if (!pOptsKV)
            return;

        FOR_EACH_SUBKEY(pOptsKV, pOptKV)
        {
            const char *optName = pOptKV->GetName();
            int optIndex = (optName && optName[0]) ? atoi(optName) : -1;
            DSDialogOption opt;
            Q_memset(&opt, 0, sizeof(opt));
            ParseOptionKV(pOptKV, opt);

            if (optIndex >= 0)
            {
                while (outNode.options.Count() <= optIndex)
                {
                    DSDialogOption blank;
                    Q_memset(&blank, 0, sizeof(blank));
                    outNode.options.AddToTail(blank);
                }
                outNode.options[optIndex] = opt;
            }
            else
            {
                outNode.options.AddToTail(opt);
            }
        }
    }

    static DSDialog *ParseDialogFileKV(KeyValues *pRootKV, const char *fallbackEntityName, const char *sourceFile)
    {
        if (!pRootKV)
            return NULL;

        DSDialog *pDialog = new DSDialog();
        pDialog->entityName = pRootKV->GetString("entity", fallbackEntityName ? fallbackEntityName : "");
        pDialog->returningLine = pRootKV->GetString("returning_line", "");
        pDialog->returningChoreography = pRootKV->GetString("returning_choreo", "");
        pDialog->returningAutoNextId = pRootKV->GetInt("returning_auto_next", INT_MIN);
        pDialog->returningNoOptions = IsTrueString(pRootKV->GetString("returning_no_options", "0"));
        pDialog->returningAutoClose = -1;
        if (pRootKV->FindKey("returning_auto_close") != NULL)
        {
            pDialog->returningAutoClose = IsTrueString(pRootKV->GetString("returning_auto_close", "0")) ? 1 : 0;
        }
        pDialog->autoFaceNPC = IsTrueString(pRootKV->GetString("auto_face_npc", "0"));
        pDialog->startNodeId = pRootKV->GetInt("start_node", 0);
        pDialog->nodes.RemoveAll();

        ParseQuestsKV(pRootKV->FindKey("quests"));

        KeyValues *pNodesKV = pRootKV->FindKey("nodes");
        if (!pNodesKV)
        {
            DIALOG_DEVMSG("DialogSystem: '%s' missing 'nodes' block\n", sourceFile ? sourceFile : "(unknown)");
            DeleteDialog(pDialog);
            return NULL;
        }

        FOR_EACH_SUBKEY(pNodesKV, pNodeKV)
        {
            const char *nodeName = pNodeKV->GetName();
            int nodeId = (nodeName && nodeName[0]) ? atoi(nodeName) : -999;
            if (nodeId == -999)
                continue;

            DSDialogNode *pNode = new DSDialogNode();
            pNode->id = nodeId;
            pNode->optionDelaySeconds = 0.0f;
            pNode->optionDelayAuto = true;
            pNode->autoNextId = -1;
            pNode->autoClose = false;
            pNode->spoils = false;
            ParseNodeKV(pNodeKV, *pNode);
            pDialog->nodes.AddToTail(pNode);
        }

        if (pDialog->nodes.Count() <= 0)
        {
            DIALOG_DEVMSG("DialogSystem: '%s' contains no nodes\n", sourceFile ? sourceFile : "(unknown)");
            DeleteDialog(pDialog);
            return NULL;
        }

        return pDialog;
    }

    static void LoadDialogsFromScripts()
    {
        FileFindHandle_t findHandle;
        const char *filename = filesystem->FindFirstEx("scripts/dialogs/*.txt", "MOD", &findHandle);
        while (filename)
        {
            char relPath[MAX_PATH];
            Q_snprintf(relPath, sizeof(relPath), "scripts/dialogs/%s", filename);

            char fileBase[MAX_PATH];
            Q_FileBase(filename, fileBase, sizeof(fileBase));

            KeyValues *pKV = new KeyValues("Dialog");
            if (!pKV->LoadFromFile(filesystem, relPath, "MOD"))
            {
                DevMsg("DialogSystem: failed to load %s\n", relPath);
                pKV->deleteThis();
                filename = filesystem->FindNext(findHandle);
                continue;
            }

            if (Q_stricmp(pKV->GetName(), "Dialog") != 0)
            {
                DevMsg("DialogSystem: %s root must be \"Dialog\"\n", relPath);
                pKV->deleteThis();
                filename = filesystem->FindNext(findHandle);
                continue;
            }

            DSDialog *pDialog = ParseDialogFileKV(pKV, fileBase, relPath);
            pKV->deleteThis();

            if (pDialog && pDialog->entityName.Length() > 0)
            {
                CUtlString key = NormalizeEntityKey(pDialog->entityName.Get());
                int idx = g_DialogByEntity.Find(key.Get());
                if (idx != g_DialogByEntity.InvalidIndex())
                {
                    DSDialog *pOld = g_DialogByEntity[idx];
                    g_DialogByEntity.RemoveAt(idx);
                    for (int i = 0; i < g_LoadedDialogs.Count(); ++i)
                    {
                        if (g_LoadedDialogs[i] == pOld)
                        {
                            DeleteDialog(pOld);
                            g_LoadedDialogs.FastRemove(i);
                            break;
                        }
                    }
                }

                g_LoadedDialogs.AddToTail(pDialog);
                g_DialogByEntity.Insert(key.Get(), pDialog);
                DIALOG_DEVMSG("DialogSystem: loaded dialog '%s' from %s\n", pDialog->entityName.Get(), relPath);
            }
            else
            {
                DeleteDialog(pDialog);
            }

            filename = filesystem->FindNext(findHandle);
        }
        filesystem->FindClose(findHandle);
    }

    void ClearDialogProgress()
    {
        g_SpoiledEntities.RemoveAll();
        g_SpokenEntities.RemoveAll();
    }

    void ReloadDialogs()
    {
        ClearLoadedDialogs();
        QuestSystem().ClearQuestDefs();
        LoadDialogsFromScripts();
        g_DialogsLoaded = true;
    }

    void MarkEntitySpoiled(const char *entityName)
    {
        if (!entityName || !entityName[0]) return;
        for (int i = 0; i < g_SpoiledEntities.Count(); ++i)
        {
            if (!Q_stricmp(g_SpoiledEntities[i].Get(), entityName))
                return;
        }
        g_SpoiledEntities.AddToTail(CUtlString(entityName));
    }

    void ClearEntitySpoiled(const char *entityName)
    {
        if (!entityName || !entityName[0]) return;
        for (int i = g_SpoiledEntities.Count() - 1; i >= 0; --i)
        {
            if (!Q_stricmp(g_SpoiledEntities[i].Get(), entityName))
            {
                g_SpoiledEntities.FastRemove(i);
            }
        }
    }

    bool IsEntitySpoiled(const char *entityName)
    {
        if (!entityName || !entityName[0]) return false;
        for (int i = 0; i < g_SpoiledEntities.Count(); ++i)
        {
            if (!Q_stricmp(g_SpoiledEntities[i].Get(), entityName))
                return true;
        }
        return false;
    }

    DSDialog Example_TestNpc_Dialog;

    const DSDialog *FindDialogForEntity(const char *entityName)
    {
        if (!g_DialogsLoaded)
            ReloadDialogs();
        if (!entityName || !entityName[0])
            return NULL;
        CUtlString key = NormalizeEntityKey(entityName);
        int idx = g_DialogByEntity.Find(key.Get());
        if (idx == g_DialogByEntity.InvalidIndex())
            return NULL;
        return g_DialogByEntity[idx];
    }

    static void SendClientCommandSafe(CBasePlayer *pPlayer, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        char buffer[1024];
        Q_vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        DIALOG_DEVMSG("DialogSystem: sending to player %s -> %s\n", pPlayer ? pPlayer->GetPlayerName() : "(null)", buffer);
        engine->ClientCommand(pPlayer->edict(), buffer);
    }

    void SendDialogToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int entIndex)
    {
        // send the first node (keeps existing behavior)
        if (!pPlayer || !dialog) return;
        // don't open dialog if entity was spoiled
        if (IsEntitySpoiled(dialog->entityName.Get()))
            return;
        int firstNodeId = dialog->startNodeId;
        // If this entity was already spoken to, allow the dialog to override the first-line
        if (IsEntitySpoken(dialog->entityName.Get()) && dialog->returningLine.Length() > 0)
        {
            int targetNodeId = firstNodeId;
            if (!dialog->returningNoOptions)
            {
                const DSDialogNode *node = NULL;
                int hop = 0;
                while (hop < 16)
                {
                    node = NULL;
                    for (int i = 0; i < dialog->nodes.Count(); ++i)
                    {
                        if (dialog->nodes[i] && dialog->nodes[i]->id == targetNodeId)
                        {
                            node = dialog->nodes[i];
                            break;
                        }
                    }
                    if (!node)
                        break;
                    if (node->options.Count() > 0)
                        break;
                    if (node->autoNextId == -1)
                        break;
                    targetNodeId = node->autoNextId;
                    ++hop;
                }
            }
            SendDialogNodeToPlayerEx(
                pPlayer,
                dialog,
                targetNodeId,
                entIndex,
                dialog->returningLine.Get(),
                dialog->returningNoOptions,
                false,
                dialog->returningNoOptions ? dialog->returningAutoNextId : -1,
                dialog->returningNoOptions ? dialog->returningAutoClose : 0);
        }
        else
        {
            SendDialogNodeToPlayer(pPlayer, dialog, firstNodeId, entIndex);
        }
    }

    void MarkEntitySpoken(const char *entityName)
    {
        if (!entityName || !entityName[0]) return;
        for (int i = 0; i < g_SpokenEntities.Count(); ++i)
        {
            if (!Q_stricmp(g_SpokenEntities[i].Get(), entityName))
                return;
        }
        g_SpokenEntities.AddToTail(CUtlString(entityName));
    }

    void ClearEntitySpoken(const char *entityName)
    {
        if (!entityName || !entityName[0]) return;
        for (int i = g_SpokenEntities.Count() - 1; i >= 0; --i)
        {
            if (!Q_stricmp(g_SpokenEntities[i].Get(), entityName))
            {
                g_SpokenEntities.FastRemove(i);
            }
        }
    }

    bool IsEntitySpoken(const char *entityName)
    {
        if (!entityName || !entityName[0]) return false;
        for (int i = 0; i < g_SpokenEntities.Count(); ++i)
        {
            if (!Q_stricmp(g_SpokenEntities[i].Get(), entityName))
                return true;
        }
        return false;
    }

    void SendDialogNodeToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine)
    {
        SendDialogNodeToPlayerEx(pPlayer, dialog, nodeId, entIndex, overrideLine, false, false, INT_MIN, -1);
    }

    void SendDialogNodeToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, const char *overrideChoreo, const char *overrideSound, const char *overrideSequence)
    {
        SendDialogNodeToPlayerEx(pPlayer, dialog, nodeId, entIndex, overrideLine, overrideChoreo, overrideSound, overrideSequence, false, false, INT_MIN, -1);
    }

    void SendDialogNodeToPlayerEx(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, bool forceNoOptions, bool forcePendingSpoil, int forcedAutoNextId, int forcedAutoClose)
    {
        SendDialogNodeToPlayerEx(pPlayer, dialog, nodeId, entIndex, overrideLine, NULL, NULL, NULL, forceNoOptions, forcePendingSpoil, forcedAutoNextId, forcedAutoClose);
    }

    void SendDialogNodeToPlayerEx(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, const char *overrideChoreo, const char *overrideSound, const char *overrideSequence, bool forceNoOptions, bool forcePendingSpoil, int forcedAutoNextId, int forcedAutoClose)
    {
        if (!pPlayer || !dialog) return;

        const DSDialogNode *node = NULL;
        for (int i = 0; i < dialog->nodes.Count(); ++i)
        {
            if (dialog->nodes[i] && dialog->nodes[i]->id == nodeId)
            {
                node = dialog->nodes[i];
                break;
            }
        }
        if (!node)
        {
            for (int i = 0; i < dialog->nodes.Count(); ++i)
            {
                if (dialog->nodes[i] && dialog->nodes[i]->id == dialog->startNodeId)
                {
                    node = dialog->nodes[i];
                    break;
                }
            }
        }
        if (!node)
            node = dialog->nodes.Count() > 0 ? dialog->nodes[0] : NULL;
        if (!node)
            return;

        bool pendingSpoil = forcePendingSpoil || node->spoils;
        int effectiveAutoNextId = (forcedAutoNextId != INT_MIN) ? forcedAutoNextId : node->autoNextId;
        bool effectiveAutoClose = (forcedAutoClose >= 0) ? (forcedAutoClose != 0) : node->autoClose;
        bool suppressOptions = forceNoOptions || pendingSpoil;
        if (pendingSpoil)
        {
            effectiveAutoNextId = -1;
            effectiveAutoClose = true;
        }

        // Pack entire dialog node into a single payload argument to avoid command ordering/timing issues.
        // Format: entityName|entIndex|displayName|nodeId|line|choreo|sound|sequenceName|delaySecs|opt;index~text~next~flags;...
        CUtlString payload;
        payload.Append(dialog->entityName.Get());
        payload.Append("|");
        char entbuf[32];
        Q_snprintf(entbuf, sizeof(entbuf), "%d", entIndex);
        payload.Append(entbuf);
        payload.Append("|");

        const char *displayName = "";
#if defined( GAME_DLL )
        CBaseEntity *pOwner = NULL;
        if (entIndex > 0)
            pOwner = UTIL_EntityByIndex(entIndex);
        if (!pOwner && dialog->entityName.Length() > 0)
            pOwner = gEntList.FindEntityByName(NULL, dialog->entityName.Get());
        CAI_BaseNPC *pNPC = pOwner ? dynamic_cast<CAI_BaseNPC*>(pOwner) : NULL;
        if (pNPC)
            displayName = pNPC->GetLootDisplayName();
#endif
        if (displayName) payload.Append(displayName);
        payload.Append("|");
        char nodebuf[32];
        Q_snprintf(nodebuf, sizeof(nodebuf), "%d", node->id);
        payload.Append(nodebuf);
        payload.Append("|");

        // Use an override line if provided (for return-visit greetings), otherwise use the node's speaker line
        if (overrideLine && overrideLine[0])
            payload.Append(overrideLine);
        else if (node->speakerLine.Length() > 0) payload.Append(node->speakerLine.Get());
        payload.Append("|");
        // Decide which choreography to send:
        // 1) explicit override
        // 2) returning choreography when an override line is used
        // 3) node choreography
        const char *choreoToUse = NULL;
        if (overrideChoreo && overrideChoreo[0])
        {
            choreoToUse = overrideChoreo;
        }
        else if (overrideLine && overrideLine[0] && dialog->returningChoreography.Length() > 0)
        {
            choreoToUse = dialog->returningChoreography.Get();
        }
        else if (node->choreography.Length() > 0)
        {
            choreoToUse = node->choreography.Get();
        }
        if (choreoToUse) payload.Append(choreoToUse);
        payload.Append("|");
        if (overrideSound && overrideSound[0]) payload.Append(overrideSound);
        else if (node->soundName.Length() > 0) payload.Append(node->soundName.Get());
        payload.Append("|");
        // optional sequence name
        if (overrideSequence && overrideSequence[0]) payload.Append(overrideSequence);
        else if (node->sequenceName.Length() > 0) payload.Append(node->sequenceName.Get());
        payload.Append("|");
        // optional option delay in seconds
        char delaybuf[64];
        float delay = node->optionDelaySeconds;
        // If no explicit per-node delay was configured, try to auto-compute from the .vcd choreography duration
        if ((node->optionDelayAuto || delay <= 0.0f) && choreoToUse && choreoToUse[0])
        {
            char scenefile[MAX_PATH];
            Q_strncpy(scenefile, choreoToUse, sizeof(scenefile));
            Q_SetExtension(scenefile, ".vcd", sizeof(scenefile));
            Q_FixSlashes(scenefile);
            CChoreoScene *scene = BlockingLoadScene(scenefile);
            if (scene)
            {
                // Prefer the end time of the last SPEAK event (so buttons appear when NPC finishes speaking)
                float speakEnd = 0.0f;
                int nevents = scene->GetNumEvents();
                for (int ei = 0; ei < nevents; ++ei)
                {
                    CChoreoEvent *e = scene->GetEvent(ei);
                    if (!e) continue;
                    if (e->GetType() == CChoreoEvent::SPEAK)
                    {
                        float endtime = 0.0f;
                        // prefer last slave end time if available (accounts for phoneme timing)
                        endtime = e->GetLastSlaveEndTime();
                        if (endtime <= 0.0f)
                            endtime = e->GetEndTime();
                        if (endtime > speakEnd) speakEnd = endtime;
                    }
                }
                if (speakEnd > 0.0f)
                {
                    delay = speakEnd + 0.05f;
                }
                else
                {
                    // fallback to full scene duration
                    float dur = scene->GetDuration();
                    delay = dur > 0.0f ? dur + 0.05f : 0.0f;
                }
                delete scene;
                DIALOG_DEVMSG("DialogSystem: auto-computed option delay %f from scene %s\n", delay, scenefile);
            }
            else
            {
                DIALOG_DEVMSG("DialogSystem: failed to load scene for delay computation: %s\n", scenefile);
            }
        }
        if ((node->optionDelayAuto || delay <= 0.0f) && delay <= 0.0f && node->soundName.Length() > 0)
        {
#if defined( GAME_DLL )
            const char *actorModel = NULL;
            if (pOwner)
                actorModel = STRING(pOwner->GetModelName());
            float soundDur = CBaseEntity::GetSoundDuration(node->soundName.Get(), actorModel);
            if (soundDur > 0.0f)
                delay = soundDur + 0.05f;
#endif
        }
        if ((node->optionDelayAuto || delay <= 0.0f) && delay <= 0.0f)
        {
            const char *lineForEstimate = (overrideLine && overrideLine[0]) ? overrideLine : (node->speakerLine.Length() > 0 ? node->speakerLine.Get() : "");
            int len = lineForEstimate ? Q_strlen(lineForEstimate) : 0;
            float est = (float)len / 18.0f;
            if (est < 1.8f) est = 1.8f;
            if (est > 6.0f) est = 6.0f;
            delay = est;
        }
        Q_snprintf(delaybuf, sizeof(delaybuf), "%f", delay);
        payload.Append(delaybuf);
        payload.Append("|");
        char autonextbuf[32];
        Q_snprintf(autonextbuf, sizeof(autonextbuf), "%d", effectiveAutoNextId);
        payload.Append(autonextbuf);
        payload.Append("|");
        payload.Append(effectiveAutoClose ? "1" : "0");
        payload.Append("|");
        payload.Append(dialog->autoFaceNPC ? "1" : "0");
        payload.Append("|");
        payload.Append(pendingSpoil ? "1" : "0");
        payload.Append("|");
        // options
        if (!suppressOptions && node->options.Count() > 0)
        {
            bool firstOpt = true;
            for (int optIndex = 0; optIndex < node->options.Count(); ++optIndex)
            {
                const DSDialogOption &opt = node->options[optIndex];
                if (opt.requireReputation != INT_MIN)
                {
                    if (QuestSystem().GetReputation(pPlayer) < opt.requireReputation)
                        continue;
                }
                if (opt.requireQuestId[0] && opt.requireQuestStage != INT_MIN)
                {
                    const PlayerQuestState *st = QuestSystem().FindPlayerQuest(pPlayer, opt.requireQuestId);
                    if (!st || st->state != kQuestState_Active || st->stage < opt.requireQuestStage)
                        continue;
                }
                if (opt.requireQuestStateId[0] && opt.requireQuestState != INT_MIN)
                {
                    const PlayerQuestState *st = QuestSystem().FindPlayerQuest(pPlayer, opt.requireQuestStateId);
                    const int state = st ? (int)st->state : (int)kQuestState_Inactive;
                    if (state != opt.requireQuestState)
                        continue;
                }
                if (opt.grantQuestId[0])
                {
                    const PlayerQuestState *st = QuestSystem().FindPlayerQuest(pPlayer, opt.grantQuestId);
                    if (st && (st->state == kQuestState_Active || st->state == kQuestState_Completed))
                        continue;
                }
                const char *optText = opt.text[0] ? opt.text : "";
                if (!firstOpt) payload.Append(";");
                firstOpt = false;
                char tmp[512];
                int sendFlags = opt.flags;
                if (opt.completeQuestId[0])
                    sendFlags &= ~1;
                Q_snprintf(tmp, sizeof(tmp), "%d~%s~%d~%d", optIndex, optText, opt.nextId, sendFlags);
                payload.Append(tmp);
            }
        }

        CUtlString safePayload = EscapeForQuotedArg(payload.Get());
        char testCmd[1024];
        Q_snprintf(testCmd, sizeof(testCmd), "dialog_open_payload \"%s\"", safePayload.Get());

        if ((int)Q_strlen(testCmd) < CCommand::MaxCommandLength())
        {
            SendClientCommandSafe(pPlayer, "%s", testCmd);
        }
        else
        {
            CUtlString safeEntity = EscapeForQuotedArg(dialog->entityName.Get());
            char beginCmd[1024];
            Q_snprintf(beginCmd, sizeof(beginCmd), "dialog_open_payload_begin \"%s\"", safeEntity.Get());
            SendClientCommandSafe(pPlayer, "%s", beginCmd);

            const char *raw = payload.Get();
            int rawLen = Q_strlen(raw);
            int pos = 0;
            while (pos < rawLen)
            {
                int remaining = rawLen - pos;
                int chunkLen = remaining;
                if (chunkLen > 384) chunkLen = 384;
                while (chunkLen > 1)
                {
                    unsigned char c = (unsigned char)raw[pos + chunkLen - 1];
                    if ((c & 0xC0) != 0x80) break;
                    --chunkLen;
                }
                if (chunkLen < 1) chunkLen = 1;

                char chunkBuf[512];
                if (chunkLen >= (int)sizeof(chunkBuf))
                    chunkLen = sizeof(chunkBuf) - 1;
                Q_memcpy(chunkBuf, raw + pos, chunkLen);
                chunkBuf[chunkLen] = '\0';

                CUtlString safeChunk = EscapeForQuotedArg(chunkBuf);
                char chunkCmd[1024];
                Q_snprintf(chunkCmd, sizeof(chunkCmd), "dialog_open_payload_chunk \"%s\"", safeChunk.Get());
                while ((int)Q_strlen(chunkCmd) >= CCommand::MaxCommandLength() && chunkLen > 8)
                {
                    chunkLen -= 8;
                    Q_memcpy(chunkBuf, raw + pos, chunkLen);
                    chunkBuf[chunkLen] = '\0';
                    safeChunk = EscapeForQuotedArg(chunkBuf);
                    Q_snprintf(chunkCmd, sizeof(chunkCmd), "dialog_open_payload_chunk \"%s\"", safeChunk.Get());
                }
                SendClientCommandSafe(pPlayer, "%s", chunkCmd);
                pos += chunkLen;
            }

            SendClientCommandSafe(pPlayer, "dialog_open_payload_end");
        }

#if defined( GAME_DLL )
        // Also try to load and start the scene on the server so server-side VCD events run
        if (choreoToUse && choreoToUse[0])
        {
            char scenefile[MAX_PATH];
            Q_strncpy(scenefile, choreoToUse, sizeof(scenefile));
            Q_SetExtension(scenefile, ".vcd", sizeof(scenefile));
            Q_FixSlashes(scenefile);
            CChoreoScene *scene = BlockingLoadScene(scenefile);
            if (scene)
            {
                DevMsg("DialogSystem: server loaded scene %s for dialog entity %s\n", scenefile, dialog->entityName.Length() > 0 ? dialog->entityName.Get() : "(null)");
                // Reset simulation and attach to owner actor if possible
                scene->ResetSimulation();

                CBaseEntity *pOwner = NULL;
                if (entIndex > 0)
                    pOwner = UTIL_EntityByIndex(entIndex);
                if (!pOwner && dialog && dialog->entityName.Length() > 0)
                    pOwner = gEntList.FindEntityByName(NULL, dialog->entityName.Get());

                if (pOwner)
                {
                    // Use the instanced scene helper so the scene entity is created and
                    // registered with the scene manager (so server-side events run).
                    CBaseFlex *pFlexOwner = dynamic_cast<CBaseFlex*>(pOwner);
                    if (pFlexOwner)
                    {
                        EHANDLE hSceneEnt;
                        InstancedScriptedScene( pFlexOwner, scenefile, &hSceneEnt, 0.0f, false, NULL, false, NULL );
                        DevMsg("DialogSystem: instanced server choreo scene '%s' started on owner %p\n", scenefile, pOwner);
                        // original scene pointer is not needed; ensure we don't leak
                        delete scene;
                    }
                    else
                    {
                        // If owner is not CBaseFlex, try AI owner path
                        CAI_BaseNPC *pNPC = dynamic_cast<CAI_BaseNPC*>(pOwner);
                        if (pNPC)
                        {
                            EHANDLE hSceneEnt;
                            InstancedScriptedScene( dynamic_cast<CBaseFlex*>(pNPC), scenefile, &hSceneEnt, 0.0f, false, NULL, false, NULL );
                            DevMsg("DialogSystem: instanced server choreo scene '%s' started on NPC owner %p\n", scenefile, pOwner);
                            delete scene;
                        }
                        else
                        {
                            DevMsg("DialogSystem: owner %p not suitable for server choreo; freeing scene %s\n", pOwner, scenefile);
                            delete scene;
                        }
                    }
                }
                else
                {
                    DevMsg("DialogSystem: no server owner found for scene %s; freeing\n", scenefile);
                    delete scene;
                }
            }
            else
            {
                DevMsg("DialogSystem: BlockingLoadScene failed for %s\n", scenefile);
            }
        }
#endif

        // Optionally start a server-side scripted_sequence entity when this node is sent
        if ( node && node->serverSequenceName.Length() > 0 )
        {
                DevMsg("DialogSystem: node requests server sequence '%s'\n", node->serverSequenceName.Get());
            CBaseEntity *pSeq = gEntList.FindEntityByName(NULL, node->serverSequenceName.Get());
            if ( pSeq )
            {
                // Try to find the dialog owner entity by name (dialog->entityName)
                CBaseEntity *pOwner = NULL;
                if ( dialog && dialog->entityName.Length() > 0 )
                    pOwner = gEntList.FindEntityByName(NULL, dialog->entityName.Get());
                // If entIndex was supplied, prefer that entity as the activator
                if ( !pOwner && entIndex > 0 )
                {
                    // Prefer direct lookup by entindex when available.
                    pOwner = UTIL_EntityByIndex(entIndex);
                }
                DevMsg("DialogSystem: starting server sequence entity=%p owner=%p\n", pSeq, pOwner);
                pSeq->AcceptInput("BeginSequence", pOwner, pOwner, variant_t(), 0);

            }
            else
            {
                DevMsg("DialogSystem: server sequence '%s' not found\n", node->serverSequenceName.Get());
            }
        }

        if (node && node->spoils)
        {
            DIALOG_DEVMSG("DialogSystem: node %d has spoils=1; waiting for dialog_finish_spoil\n", node->id);
        }

        // Optionally start a server-side choreo scene on the NPC so server-side events execute
#if defined( GAME_DLL )
        if ( node && node->choreography.Length() > 0 )
        {
            char scenefile[MAX_PATH];
            Q_strncpy(scenefile, node->choreography.Get(), sizeof(scenefile));
            Q_SetExtension(scenefile, ".vcd", sizeof(scenefile));
            Q_FixSlashes(scenefile);

            CChoreoScene *pScene = BlockingLoadScene(scenefile);
                if ( pScene )
                {
                    CBaseEntity *pOwner = NULL;
                    if ( entIndex > 0 )
                        pOwner = UTIL_EntityByIndex(entIndex);
                    if ( !pOwner && dialog && dialog->entityName.Length() > 0 )
                        pOwner = gEntList.FindEntityByName(NULL, dialog->entityName.Get());

                    if ( pOwner )
                    {
                        CBaseFlex *pFlexOwner = dynamic_cast<CBaseFlex*>(pOwner);
                        if ( pFlexOwner )
                        {
                            EHANDLE hSceneEnt;
                            InstancedScriptedScene( pFlexOwner, scenefile, &hSceneEnt, 0.0f, false, NULL, false, NULL );
                            DevMsg("DialogSystem: instanced server choreo scene '%s' started on owner %s\n", scenefile, pOwner->GetEntityName().ToCStr());
                            delete pScene;
                        }
                        else
                        {
                            DevMsg("DialogSystem: owner not a CBaseFlex, cannot start server choreo for %s\n", pOwner->GetClassname());
                            delete pScene;
                        }
                    }
                    else
                    {
                        DevMsg("DialogSystem: no owner found to start server choreo %s\n", scenefile);
                        delete pScene;
                    }
                }
            else
            {
                DevMsg("DialogSystem: BlockingLoadScene failed for %s\n", scenefile);
            }
        }
#endif
    }
}

// Ensure spoiled dialog state does not persist across level loads / save-loads.
// Clear the in-memory spoiled-entity list when a new level is initialized.
class CDialogDefinitionsGameSystem : public CAutoGameSystem
{
public:
    CDialogDefinitionsGameSystem() : CAutoGameSystem("CDialogDefinitionsGameSystem") {}
    virtual void LevelInitPreEntity()
    {
        DialogDefinitions::ReloadDialogs();
        // Clear the per-level spoiled list so earlier gameplay choices do not
        // incorrectly persist across level reloads or loading older saves.
        DialogDefinitions::g_SpoiledEntities.RemoveAll();
        DialogDefinitions::g_SpokenEntities.RemoveAll();
    }
};

static CDialogDefinitionsGameSystem g_DialogDefinitionsGameSystem;

CON_COMMAND_F( dialog_reload, "Reload dialogs from scripts/dialogs/*.txt", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
    DialogDefinitions::ReloadDialogs();
    DevMsg("DialogSystem: reloaded dialogs\n");
}

#if defined( GAME_DLL )
#include "isaverestore.h"
#include "saverestore_utlvector.h"

static short DIALOG_SAVE_RESTORE_VERSION = 1;

class CDialogSaveRestoreBlockHandler : public CDefSaveRestoreBlockHandler
{
public:
    const char *GetBlockName()
    {
        return "Dialog";
    }

    void Save( ISave *pSave )
    {
        pSave->StartBlock( "Spoiled" );
        short n = (short)DialogDefinitions::g_SpoiledEntities.Count();
        pSave->WriteShort( &n );
        for ( int i = 0; i < n; ++i )
        {
            string_t s = MAKE_STRING( DialogDefinitions::g_SpoiledEntities[i].Get() );
            pSave->WriteString( "", &s );
        }
        pSave->EndBlock();
        // Save spoken list as well so return-visit greetings persist across saves
        pSave->StartBlock( "Spoken" );
        short ns = (short)DialogDefinitions::g_SpokenEntities.Count();
        pSave->WriteShort( &ns );
        for ( int i = 0; i < ns; ++i )
        {
            string_t s = MAKE_STRING( DialogDefinitions::g_SpokenEntities[i].Get() );
            pSave->WriteString( "", &s );
        }
        pSave->EndBlock();
    }

    void WriteSaveHeaders( ISave *pSave )
    {
        pSave->WriteShort( &DIALOG_SAVE_RESTORE_VERSION );
    }

    void ReadRestoreHeaders( IRestore *pRestore )
    {
        short version;
        pRestore->ReadShort( &version );
        m_fDoLoad = ( version == DIALOG_SAVE_RESTORE_VERSION ) &&
            ( ( MapLoad_LoadGame == gpGlobals->eLoadType ) || ( MapLoad_NewGame == gpGlobals->eLoadType ) );
    }

    void Restore( IRestore *pRestore, bool createPlayers )
    {
        if ( m_fDoLoad )
        {
            DialogDefinitions::g_SpoiledEntities.RemoveAll();

            pRestore->StartBlock();
            int nSaved = pRestore->ReadShort();
            while ( nSaved-- )
            {
                int sizeData = pRestore->SkipHeader();
                string_t s;
                pRestore->ReadString( &s, 1, sizeData );
                if ( s != NULL_STRING )
                {
                    DialogDefinitions::g_SpoiledEntities.AddToTail( CUtlString( STRING( s ) ) );
                }
            }
            pRestore->EndBlock();
            // Try to read an optional Spoken block
            DialogDefinitions::g_SpokenEntities.RemoveAll();
            // Check if there's more data (another block). Use StartBlock() which will return false
            // if no more blocks are present for this handler; however StartBlock has no return here,
            // so we attempt to read the next short and catch if format differs.
            // Start a new block; if it's not present the read will likely return zero and leave list empty.
            pRestore->StartBlock();
            int nSavedSpoken = pRestore->ReadShort();
            while ( nSavedSpoken-- )
            {
                int sizeData = pRestore->SkipHeader();
                string_t s;
                pRestore->ReadString( &s, 1, sizeData );
                if ( s != NULL_STRING )
                {
                    DialogDefinitions::g_SpokenEntities.AddToTail( CUtlString( STRING( s ) ) );
                }
            }
            pRestore->EndBlock();
        }
    }

private:
    bool m_fDoLoad;
};

CDialogSaveRestoreBlockHandler g_DialogSaveRestoreBlockHandler;

ISaveRestoreBlockHandler *GetDialogSaveRestoreBlockHandler()
{
    return &g_DialogSaveRestoreBlockHandler;
}

#endif // GAME_DLL

#ifndef DIALOG_DEFINITIONS_H
#define DIALOG_DEFINITIONS_H

#include "cbase.h"

class CBasePlayer;

namespace DialogDefinitions
{
    struct DSDialogOption
    {
        char text[256];
        int nextId;
        int flags;
        char serverSequenceName[128];
        bool spoils;
        char grantQuestId[64];
        char completeQuestId[64];
        char failQuestId[64];
        char setQuestStageId[64];
        int setQuestStage;
        int requireReputation;
        char requireQuestId[64];
        int requireQuestStage;
        char requireQuestStateId[64];
        int requireQuestState;
        char afterLine[256];
        char afterChoreography[256];
        char afterSoundName[256];
        char afterSequenceName[256];
        int afterNextId;
    };

    struct DSDialogNode
    {
        int id;
        CUtlString speakerLine;
        CUtlString choreography;
        CUtlString soundName;
        CUtlString sequenceName;
        float optionDelaySeconds;
        bool optionDelayAuto;
        CUtlVector<DSDialogOption> options;
        int autoNextId;
        bool autoClose;
        CUtlString serverSequenceName;
        bool spoils;
    };

    struct DSDialog
    {
        CUtlString entityName;
        int startNodeId;
        CUtlVector<DSDialogNode*> nodes;
        CUtlString returningLine;
        CUtlString returningChoreography;
        int returningAutoNextId;
        int returningAutoClose;
        bool returningNoOptions;
        bool autoFaceNPC;
    };

    void ReloadDialogs();
    void SendDialogToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int entIndex = -1);
    // Send a specific node (by id) from the dialog to the player
    // entIndex: optional server entity index for the NPC (client-side lookup)
    void SendDialogNodeToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex = -1, const char *overrideLine = NULL);
    void SendDialogNodeToPlayer(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, const char *overrideChoreo, const char *overrideSound, const char *overrideSequence);
    void SendDialogNodeToPlayerEx(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, bool forceNoOptions, bool forcePendingSpoil, int forcedAutoNextId, int forcedAutoClose);
    void SendDialogNodeToPlayerEx(CBasePlayer *pPlayer, const DSDialog *dialog, int nodeId, int entIndex, const char *overrideLine, const char *overrideChoreo, const char *overrideSound, const char *overrideSequence, bool forceNoOptions, bool forcePendingSpoil, int forcedAutoNextId, int forcedAutoClose);
    const DSDialog *FindDialogForEntity(const char *entityName);

    // Mark an entity as having its dialog "spoiled" (unopenable) and query that state
    void MarkEntitySpoiled(const char *entityName);
    void ClearEntitySpoiled(const char *entityName);
    bool IsEntitySpoiled(const char *entityName);
    // Track whether the player has already completed a first conversation with this entity
    void MarkEntitySpoken(const char *entityName);
    void ClearEntitySpoken(const char *entityName);
    bool IsEntitySpoken(const char *entityName);

    extern DSDialog Example_TestNpc_Dialog;
}

#if defined( GAME_DLL )
#include "isaverestore.h"
// Save/restore helper for dialog spoiled list
ISaveRestoreBlockHandler *GetDialogSaveRestoreBlockHandler();
#endif

#endif // DIALOG_DEFINITIONS_H

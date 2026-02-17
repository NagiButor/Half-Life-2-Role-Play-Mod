#ifndef DIALOGSYSTEM_SERVER_H
#define DIALOGSYSTEM_SERVER_H

#include "cbase.h"

class CBasePlayer;
class CBaseEntity;

namespace DialogSystemServer
{
    // Start dialog for a particular NPC entity. Finds dialog by entity_name.
    void StartDialogForEntity(CBasePlayer *pPlayer, CBaseEntity *pNPC);
    // Handle a dialog choice sent from client: player who chose, node id, option index, entity name
    void HandleDialogChoice(CBasePlayer *pPlayer, int nodeId, int optionIndex, const char *entityName);
    void ContinueDialog(CBasePlayer *pPlayer, int nodeId, const char *entityName);
}

#endif // DIALOGSYSTEM_SERVER_H

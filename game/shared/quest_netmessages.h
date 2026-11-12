#pragma once

enum QuestUpdateType
{
	kQuestsUpdate_QuestsBegin = 1,
	kQuestsUpdate_QuestAdd = 2,
	kQuestsUpdate_QuestsEnd = 3,
	kQuestsUpdate_QuestDescMore = 4,	// HL2RPM: the rest of the last quest's stage description (a user message holds 255 bytes)
};

enum QuestState
{
	kQuestState_Inactive = 0,
	kQuestState_Active = 1,
	kQuestState_Completed = 2,
	kQuestState_Failed = 3,
};


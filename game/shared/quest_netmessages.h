#pragma once

enum QuestUpdateType
{
	kQuestsUpdate_QuestsBegin = 1,
	kQuestsUpdate_QuestAdd = 2,
	kQuestsUpdate_QuestsEnd = 3,
};

enum QuestState
{
	kQuestState_Inactive = 0,
	kQuestState_Active = 1,
	kQuestState_Completed = 2,
	kQuestState_Failed = 3,
};


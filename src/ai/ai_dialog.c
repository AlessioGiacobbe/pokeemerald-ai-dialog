// Game-side glue for the AI dialog mod. Captures the context of the NPC
// being talked to, hands the scripted line to the backend, and swaps in the
// model's reply once it arrives. Falls back to the scripted line on any
// error or timeout, so the game is always playable without a backend.
#ifdef PORTABLE

#include "global.h"
#include <stdio.h>
#include <string.h>
#include "event_data.h"
#include "event_object_movement.h"
#include "field_message_box.h"
#include "region_map.h"
#include "task.h"
#include "constants/event_objects.h"
#include "ai/ai_dialog.h"
#include "ai/ai_backend.h"
#include "ai/ai_text.h"

static void Task_AiDialogWait(u8 taskId);

static const u8 *sOriginalText;      // scripted line (ROM/static data)
static u8 sEncodedReply[1000];       // game-encoded model reply

struct PersonaEntry
{
    u16 graphicsId;
    const char *persona;
};

static const struct PersonaEntry sPersonas[] =
{
    { OBJ_EVENT_GFX_LITTLE_BOY,   "a little boy" },
    { OBJ_EVENT_GFX_LITTLE_GIRL,  "a little girl" },
    { OBJ_EVENT_GFX_BOY_1,        "a young boy" },
    { OBJ_EVENT_GFX_BOY_2,        "a boy" },
    { OBJ_EVENT_GFX_BOY_3,        "a teenage boy" },
    { OBJ_EVENT_GFX_GIRL_1,       "a young girl" },
    { OBJ_EVENT_GFX_GIRL_2,       "a girl" },
    { OBJ_EVENT_GFX_GIRL_3,       "a teenage girl" },
    { OBJ_EVENT_GFX_RICH_BOY,     "a rich boy" },
    { OBJ_EVENT_GFX_MAN_1,        "a man" },
    { OBJ_EVENT_GFX_MAN_2,        "a man" },
    { OBJ_EVENT_GFX_MAN_3,        "a man" },
    { OBJ_EVENT_GFX_FAT_MAN,      "a jolly heavyset man" },
    { OBJ_EVENT_GFX_WOMAN_1,      "a woman" },
    { OBJ_EVENT_GFX_WOMAN_2,      "a woman" },
    { OBJ_EVENT_GFX_WOMAN_3,      "a woman" },
    { OBJ_EVENT_GFX_WOMAN_4,      "a woman" },
    { OBJ_EVENT_GFX_WOMAN_5,      "a woman" },
    { OBJ_EVENT_GFX_OLD_MAN,      "an old man" },
    { OBJ_EVENT_GFX_OLD_WOMAN,    "an old woman" },
    { OBJ_EVENT_GFX_YOUNGSTER,    "an energetic youngster" },
    { OBJ_EVENT_GFX_BUG_CATCHER,  "a bug catcher" },
    { OBJ_EVENT_GFX_CAMPER,       "a camper" },
    { OBJ_EVENT_GFX_PICNICKER,    "a picnicker" },
    { OBJ_EVENT_GFX_HIKER,        "a rugged hiker" },
    { OBJ_EVENT_GFX_SAILOR,       "a hearty sailor" },
    { OBJ_EVENT_GFX_FISHERMAN,    "a fisherman" },
    { OBJ_EVENT_GFX_SCIENTIST_1,  "a scientist" },
    { OBJ_EVENT_GFX_BEAUTY,       "a fashionable beauty" },
    { OBJ_EVENT_GFX_LASS,         "a cheerful lass" },
    { OBJ_EVENT_GFX_GENTLEMAN,    "a refined gentleman" },
    { OBJ_EVENT_GFX_BLACK_BELT,   "a black belt martial artist" },
    { OBJ_EVENT_GFX_PSYCHIC_M,    "a mysterious psychic" },
    { OBJ_EVENT_GFX_SCHOOL_KID_M, "a school kid" },
    { OBJ_EVENT_GFX_MANIAC,       "an obsessive collector" },
    { OBJ_EVENT_GFX_HEX_MANIAC,   "a spooky hex maniac" },
    { OBJ_EVENT_GFX_SWIMMER_M,    "a swimmer" },
    { OBJ_EVENT_GFX_SWIMMER_F,    "a swimmer" },
    { OBJ_EVENT_GFX_POKEFAN_M,    "an enthusiastic Pokemon fan" },
    { OBJ_EVENT_GFX_POKEFAN_F,    "an enthusiastic Pokemon fan" },
    { OBJ_EVENT_GFX_EXPERT_M,     "a wise expert" },
    { OBJ_EVENT_GFX_EXPERT_F,     "a wise expert" },
    { OBJ_EVENT_GFX_NINJA_BOY,    "a sneaky ninja boy" },
    { OBJ_EVENT_GFX_TWIN,         "one of the twins" },
    { OBJ_EVENT_GFX_TUBER_M,      "a kid floating on an inner tube" },
    { OBJ_EVENT_GFX_TUBER_F,      "a kid floating on an inner tube" },
    { OBJ_EVENT_GFX_COOK,         "a cook" },
    { OBJ_EVENT_GFX_NURSE,        "a kind nurse" },
};

static const char *GetPersonaForLastTalked(void)
{
    u8 objEventId;
    u16 gfxId;
    u32 i;

    if (gSpecialVar_LastTalked == 0)
        return NULL;
    objEventId = GetObjectEventIdByLocalIdAndMap(gSpecialVar_LastTalked,
                                                 gSaveBlock1Ptr->location.mapNum,
                                                 gSaveBlock1Ptr->location.mapGroup);
    if (objEventId >= OBJECT_EVENTS_COUNT || !gObjectEvents[objEventId].active)
        return "a villager";

    gfxId = gObjectEvents[objEventId].graphicsId;
    for (i = 0; i < ARRAY_COUNT(sPersonas); i++)
    {
        if (sPersonas[i].graphicsId == gfxId)
            return sPersonas[i].persona;
    }
    return "a villager";
}

static void GetContext(AiDialogRequest *req, const u8 *msg)
{
    u8 gameBuf[64];
    const char *persona = GetPersonaForLastTalked();

    memset(req, 0, sizeof(*req));
    snprintf(req->persona, sizeof(req->persona), "%s", persona != NULL ? persona : "a villager");

    GetMapNameGeneric(gameBuf, gMapHeader.regionMapSectionId);
    AiText_DecodeGameToAscii(req->mapName, sizeof(req->mapName), gameBuf);

    AiText_DecodeGameToAscii(req->playerName, sizeof(req->playerName), gSaveBlock2Ptr->playerName);
    AiText_DecodeGameToAscii(req->originalText, sizeof(req->originalText), msg);

    snprintf(req->npcKey, sizeof(req->npcKey), "%u.%u.%u",
             gSaveBlock1Ptr->location.mapGroup,
             gSaveBlock1Ptr->location.mapNum,
             gSpecialVar_LastTalked);
}

bool8 AiDialog_TryStart(const u8 *msg)
{
    AiDialogRequest req;

    if (!AiBackend_IsEnabled())
        return FALSE;
    if (msg == NULL || gSpecialVar_LastTalked == 0)
        return FALSE; // signs, scripted cutscene text, etc. stay verbatim
    if (AiText_HasDynamicPlaceholders(msg))
        return FALSE; // lines carrying runtime data stay verbatim
    if (FuncIsActiveTask(Task_AiDialogWait))
        return FALSE;

    GetContext(&req, msg);
    if (req.originalText[0] == '\0')
        return FALSE;
    if (!AiBackend_Submit(&req))
        return FALSE;

    sOriginalText = msg;
    {
        u8 taskId = CreateTask(Task_AiDialogWait, 1);
        // frames to wait before falling back (~60 fps)
        gTasks[taskId].data[0] = AiBackend_GetTimeoutMs() / 16;
    }
    return TRUE;
}

static void Task_AiDialogWait(u8 taskId)
{
    char reply[512];
    int state;

    // The message box was closed from elsewhere (map change, script abort).
    if (GetFieldMessageBoxMode() == FIELD_MESSAGE_BOX_HIDDEN)
    {
        DestroyTask(taskId);
        return;
    }

    state = AiBackend_Poll(reply, sizeof(reply));
    if (state == AI_REPLY_DONE)
    {
        AiText_EncodeAsciiToGame(sEncodedReply, sizeof(sEncodedReply), reply);
        FieldMessage_ShowFromAi(sEncodedReply);
        DestroyTask(taskId);
        return;
    }

    if (state == AI_REPLY_ERROR || --gTasks[taskId].data[0] <= 0)
    {
        FieldMessage_ShowFromAi(sOriginalText);
        DestroyTask(taskId);
    }
}

#endif // PORTABLE

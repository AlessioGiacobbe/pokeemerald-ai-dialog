#ifndef GUARD_AI_BACKEND_H
#define GUARD_AI_BACKEND_H

#include "global.h"

typedef struct
{
    char persona[64];       // "an old woman", "a hiker", ...
    char mapName[64];       // "Littleroot Town"
    char playerName[16];
    char originalText[512]; // the scripted line, decoded to ASCII
    char npcKey[32];        // stable id for short-term conversation memory
} AiDialogRequest;

enum
{
    AI_REPLY_PENDING = 0,
    AI_REPLY_DONE,
    AI_REPLY_ERROR,
};

// TRUE when a backend is configured (ai_dialog.cfg or ANTHROPIC_API_KEY).
bool32 AiBackend_IsEnabled(void);

// Kick off a request on a worker thread. FALSE if disabled or busy.
bool32 AiBackend_Submit(const AiDialogRequest *req);

// Poll from the game loop. On AI_REPLY_DONE copies the sanitized ASCII
// reply into replyOut.
int AiBackend_Poll(char *replyOut, int replySize);

// Timeout the game should wait before falling back to the scripted line.
int AiBackend_GetTimeoutMs(void);

#endif // GUARD_AI_BACKEND_H

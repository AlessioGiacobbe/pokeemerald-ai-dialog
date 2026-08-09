#ifndef GUARD_AI_DIALOG_H
#define GUARD_AI_DIALOG_H

#include "global.h"

#ifdef PORTABLE
// Called from ShowFieldMessage. If an AI backend is configured and this
// message qualifies (a real NPC line without dynamic placeholders), kicks
// off an async request and returns TRUE — the reply (or, on error/timeout,
// the original text) is displayed by a polling task when ready. Returns
// FALSE to display the scripted text as usual.
bool8 AiDialog_TryStart(const u8 *msg);
#else
// GBA build: the mod compiles out entirely.
#define AiDialog_TryStart(msg) FALSE
#endif

#endif // GUARD_AI_DIALOG_H

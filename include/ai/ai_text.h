#ifndef GUARD_AI_TEXT_H
#define GUARD_AI_TEXT_H

#include "global.h"

// Decodes game-encoded text (Emerald charmap) into plain ASCII.
// Placeholders like {PLAYER} are rendered as readable tokens.
void AiText_DecodeGameToAscii(char *dest, int destSize, const u8 *src);

// Encodes an ASCII/UTF-8 reply into game text, word-wrapping into
// message-box lines (\n for the second line, \l to scroll afterwards).
// Returns the number of bytes written including the EOS terminator.
int AiText_EncodeAsciiToGame(u8 *dest, int destSize, const char *src);

// TRUE if the game-encoded string contains placeholders other than
// {PLAYER} (string vars, dynamic items, ...). Such messages carry runtime
// data and should not be paraphrased by the model.
bool32 AiText_HasDynamicPlaceholders(const u8 *src);

#endif // GUARD_AI_TEXT_H

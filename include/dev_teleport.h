#ifndef GUARD_DEV_TELEPORT_H
#define GUARD_DEV_TELEPORT_H

#include "global.h"

#ifdef PORTABLE
// Dev/testing warp: jump straight to a populated city to test NPC dialog.
// Request() is safe to call from the platform input thread (just sets a flag);
// PollAndWarp() runs on the game thread from the overworld field-input path,
// so the warp always happens in a valid, walkable context.
void DevTeleport_Request(int slot);   // 0..N-1, see sCities in dev_teleport.c
bool8 DevTeleport_PollAndWarp(void);  // TRUE if a warp was started this frame
#else
#define DevTeleport_Request(slot) ((void)0)
#define DevTeleport_PollAndWarp() FALSE
#endif

#endif // GUARD_DEV_TELEPORT_H

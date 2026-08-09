// Dev/testing teleport: warp to a city full of NPCs to exercise the AI dialog
// mod. Uses the game's own warp system (SetWarpDestinationToMapWarp + DoWarp),
// so state stays consistent — no save editing. Bound to Ctrl+1..Ctrl+5 by the
// platform input layer.
#ifdef PORTABLE

#include "global.h"
#include "overworld.h"
#include "field_screen_effect.h"
#include "dev_teleport.h"
#include "constants/map_groups.h"

struct DevCity { s8 group; s8 num; const char *name; };

// warpId 0 exists on every outdoor city map and lands you on a walkable tile.
static const struct DevCity sCities[] = {
    { MAP_PETALBURG_CITY >> 8, MAP_PETALBURG_CITY & 0xFF, "Petalburg City" },
    { MAP_RUSTBORO_CITY  >> 8, MAP_RUSTBORO_CITY  & 0xFF, "Rustboro City"  },
    { MAP_SLATEPORT_CITY >> 8, MAP_SLATEPORT_CITY & 0xFF, "Slateport City" },
    { MAP_MAUVILLE_CITY  >> 8, MAP_MAUVILLE_CITY  & 0xFF, "Mauville City"   },
    { MAP_LILYCOVE_CITY  >> 8, MAP_LILYCOVE_CITY  & 0xFF, "Lilycove City"   },
};

// Written by the input thread, read by the game thread. A single aligned int
// is atomic enough for a one-shot request flag.
static volatile int sPendingSlot = -1;

void DevTeleport_Request(int slot)
{
    if (slot >= 0 && slot < (int)ARRAY_COUNT(sCities))
        sPendingSlot = slot;
}

bool8 DevTeleport_PollAndWarp(void)
{
    int slot = sPendingSlot;

    if (slot < 0)
        return FALSE;
    sPendingSlot = -1;

    SetWarpDestinationToMapWarp(sCities[slot].group, sCities[slot].num, 0);
    DoWarp();
    ResetInitialPlayerAvatarState();
    return TRUE;
}

#endif // PORTABLE

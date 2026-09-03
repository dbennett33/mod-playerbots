/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNSTATE_H
#define PLAYERBOTS_RAIDRUNSTATE_H

#include "ObjectGuid.h"
#include <ctime>

enum RaidRunPhase : uint8
{
    RAID_RUN_IDLE = 0,
    RAID_RUN_RUNNING = 1,
    RAID_RUN_PAUSED = 2,
    RAID_RUN_REGEN = 3,
    RAID_RUN_WING_COMPLETE = 4
};

enum RaidRunWing : uint8
{
    RAID_RUN_WING_NONE = 0,
    RAID_RUN_WING_NAXX_ARACHNID = 1,
    RAID_RUN_WING_NAXX_CONSTRUCT = 2
};

struct RaidRunState
{
    RaidRunPhase phase = RAID_RUN_IDLE;
    RaidRunWing wing = RAID_RUN_WING_NONE;
    uint8 routeStep = 0;
    ObjectGuid leaderTankGuid;
    time_t regenBreakStarted = 0;
    bool announcedRegen = false;
    bool announcedBossWait = false;
};

#endif

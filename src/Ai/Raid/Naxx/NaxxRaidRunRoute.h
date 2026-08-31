/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAXXRAIDRUNROUTE_H
#define PLAYERBOTS_NAXXRAIDRUNROUTE_H

#include "RaidRunState.h"
#include <cstdint>
#include <string>
#include <vector>

class Player;

struct RaidRunRouteStep
{
    std::string name;
    float x;
    float y;
    float z;
    uint32 mapId;
    uint32 bossEntry;
    float arriveDistance;
    float pullRadius;
};

class NaxxRaidRunRoute
{
public:
    static std::vector<RaidRunRouteStep> const& GetArachnidSteps();
    static uint8 GetStepCount();
    static RaidRunRouteStep const* GetStep(uint8 index);
    static bool IsBossAlive(Player* bot, uint32 bossEntry, float range = 250.0f);
    static bool IsStepComplete(Player* bot, RaidRunRouteStep const& step);
};

#endif

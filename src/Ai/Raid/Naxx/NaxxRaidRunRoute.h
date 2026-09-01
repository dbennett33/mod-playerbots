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

class Creature;
class GameObject;
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
    float clearRadius = 0.0f;
    uint32 portalGoEntry = 0;
};

class NaxxRaidRunRoute
{
public:
    static std::vector<RaidRunRouteStep> const& GetSteps(RaidRunWing wing);
    static std::vector<RaidRunRouteStep> const& GetArachnidSteps();
    static uint8 GetStepCount(RaidRunWing wing);
    static RaidRunRouteStep const* GetStep(RaidRunWing wing, uint8 index);
    static char const* GetWingName(RaidRunWing wing);
    static RaidRunWing SuggestWing(Player* bot);
    static bool IsAtNaxxHub(Player* bot);
    static bool NeedsHubPortal(Player* bot);
    static GameObject* FindWingReturnPortal(Player* bot, float range = 120.0f);
    static bool IsWingComplete(Player* bot, RaidRunWing wing);
    static bool IsBossAlive(Player* bot, uint32 bossEntry, float range = 250.0f);
    static bool IsBossEncounterDone(Player* bot, uint32 bossEntry);
    static bool IsStepComplete(Player* bot, RaidRunWing wing, uint8 index);
    static uint8 FindFirstIncompleteStep(Player* bot, RaidRunWing wing);
    static Creature* FindClearableTrash(Player* bot, RaidRunRouteStep const& step);
};

#endif

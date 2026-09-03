/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNROUTE_H
#define PLAYERBOTS_RAIDRUNROUTE_H

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

class RaidRunRouteProvider
{
public:
    virtual ~RaidRunRouteProvider() = default;

    virtual std::vector<RaidRunRouteStep> const& GetSteps(uint8 wing) const = 0;
    virtual uint8 GetStepCount(uint8 wing) const = 0;
    virtual RaidRunRouteStep const* GetStep(uint8 wing, uint8 index) const = 0;
    virtual char const* GetWingName(uint8 wing) const = 0;
    virtual uint8 SuggestWing(Player* bot) const = 0;
    virtual bool IsWingComplete(Player* bot, uint8 wing) const = 0;
    virtual bool IsStepComplete(Player* bot, uint8 wing, uint8 index) const = 0;
    virtual uint8 FindFirstIncompleteStep(Player* bot, uint8 wing) const = 0;
    virtual Creature* FindClearableTrash(Player* bot, RaidRunRouteStep const& step) const = 0;
    virtual bool IsBossEncounterDone(Player* bot, uint32 bossEntry) const = 0;
    virtual bool NeedsHubPortal(Player* bot) const = 0;
    virtual uint8 GetHubReturnWing(Player* bot) const { (void)bot; return 0; }
    virtual bool IsAtHub(Player* bot) const = 0;
    virtual GameObject* FindWingReturnPortal(Player* bot, float range = 120.0f) const = 0;
};

#endif

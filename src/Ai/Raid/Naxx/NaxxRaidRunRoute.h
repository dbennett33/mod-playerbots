/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAXXRAIDRUNROUTE_H
#define PLAYERBOTS_NAXXRAIDRUNROUTE_H

#include "RaidRunRoute.h"

class NaxxRaidRunRouteProvider : public RaidRunRouteProvider
{
public:
    std::vector<RaidRunRouteStep> const& GetSteps(uint8 wing) const override;
    std::vector<RaidRunRouteStep> const& GetArachnidSteps() const;
    uint8 GetStepCount(uint8 wing) const override;
    RaidRunRouteStep const* GetStep(uint8 wing, uint8 index) const override;
    char const* GetWingName(uint8 wing) const override;
    uint8 SuggestWing(Player* bot) const override;
    bool IsAtHub(Player* bot) const override;
    bool NeedsHubPortal(Player* bot) const override;
    uint8 GetHubReturnWing(Player* bot) const override;
    GameObject* FindWingReturnPortal(Player* bot, float range = 120.0f) const override;
    bool IsWingComplete(Player* bot, uint8 wing) const override;
    bool IsBossAlive(Player* bot, uint32 bossEntry, float range = 250.0f) const;
    bool IsBossEncounterDone(Player* bot, uint32 bossEntry) const override;
    bool IsStepComplete(Player* bot, uint8 wing, uint8 index) const override;
    uint8 FindFirstIncompleteStep(Player* bot, uint8 wing) const override;
    Creature* FindClearableTrash(Player* bot, RaidRunRouteStep const& step) const override;
};

#endif

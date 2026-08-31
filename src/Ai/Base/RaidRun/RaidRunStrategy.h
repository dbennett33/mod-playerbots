/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNSTRATEGY_H
#define PLAYERBOTS_RAIDRUNSTRATEGY_H

#include "Strategy.h"

class RaidRunLeaderStrategy : public Strategy
{
public:
    RaidRunLeaderStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "raid run leader"; }
    std::vector<NextAction> getDefaultActions() override;
};

class RaidRunFollowStrategy : public Strategy
{
public:
    RaidRunFollowStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "raid run follow"; }
    std::vector<NextAction> getDefaultActions() override;
};

class RaidRunRegenStrategy : public Strategy
{
public:
    RaidRunRegenStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "raid run regen"; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif

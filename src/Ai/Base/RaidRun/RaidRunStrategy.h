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
    NextAction const getDefaultAction() override { return NextAction("raid run leader", 20.0f); }
};

class RaidRunFollowStrategy : public Strategy
{
public:
    RaidRunFollowStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "raid run follow"; }
    NextAction const getDefaultAction() override { return NextAction("raid run follow tank", 5.0f); }
};

class RaidRunRegenStrategy : public Strategy
{
public:
    RaidRunRegenStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "raid run regen"; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif

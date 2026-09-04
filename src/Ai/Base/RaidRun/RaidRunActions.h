/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNACTIONS_H
#define PLAYERBOTS_RAIDRUNACTIONS_H

#include "MovementActions.h"

struct RaidRunRouteStep;
class Creature;

class RaidRunGoChatAction : public Action
{
public:
    RaidRunGoChatAction(PlayerbotAI* botAI) : Action(botAI, "raid run go chat") {}

    bool Execute(Event event) override;
};

class RaidRunPauseChatAction : public Action
{
public:
    RaidRunPauseChatAction(PlayerbotAI* botAI) : Action(botAI, "raid run pause chat") {}

    bool Execute(Event event) override;
};

class RaidRunStopChatAction : public Action
{
public:
    RaidRunStopChatAction(PlayerbotAI* botAI) : Action(botAI, "raid run stop chat") {}

    bool Execute(Event event) override;
};

class RaidRunStatusChatAction : public Action
{
public:
    RaidRunStatusChatAction(PlayerbotAI* botAI) : Action(botAI, "raid run status chat") {}

    bool Execute(Event event) override;
};

class RaidRunRecordChatAction : public Action
{
public:
    RaidRunRecordChatAction(PlayerbotAI* botAI) : Action(botAI, "raid run record chat") {}

    bool Execute(Event event) override;
};

class RaidRunLeaderAction : public MovementAction
{
public:
    RaidRunLeaderAction(PlayerbotAI* botAI) : MovementAction(botAI, "raid run leader") {}

    bool Execute(Event event) override;
    bool isUseful() override;

private:
    Creature* FindPullTarget(RaidRunRouteStep const& step);
    bool PullTarget(Creature* target, Event event);
    bool UseNaxxPortal(uint32 goEntry, float x, float y, float z);
    bool MoveToStepStrict(RaidRunRouteStep const& step);
    bool ReportNoPath(RaidRunRouteStep const& step);
};

class RaidRunFollowTankAction : public MovementAction
{
public:
    RaidRunFollowTankAction(PlayerbotAI* botAI) : MovementAction(botAI, "raid run follow tank") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class RaidRunRegenAction : public Action
{
public:
    RaidRunRegenAction(PlayerbotAI* botAI) : Action(botAI, "raid run regen") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class RaidRunResurrectAction : public MovementAction
{
public:
    RaidRunResurrectAction(PlayerbotAI* botAI) : MovementAction(botAI, "raid run resurrect") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif

/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunActions.h"
#include "Playerbots.h"
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "Event.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "NaxxRaidRunRoute.h"
#include "NonCombatActions.h"
#include "ObjectAccessor.h"
#include "PullStrategy.h"
#include "RaidRunMgr.h"
#include "RaidRunState.h"
#include "ServerFacade.h"
#include <ctime>

namespace
{
bool IsMasterCommand(PlayerbotAI* botAI, Event event)
{
    Player* master = botAI->GetMaster();
    if (!master)
        return false;

    Player* owner = event.getOwner();
    return owner && owner->GetGUID() == master->GetGUID();
}

Player* GetLeaderTank(Player* master, RaidRunState const* state)
{
    if (!master || !state || state->leaderTankGuid.IsEmpty())
        return nullptr;

    if (Player* tank = ObjectAccessor::FindPlayer(state->leaderTankGuid))
        return tank;

    return nullptr;
}

uint32 RegenManaThreshold(RaidRunState const* state)
{
    if (state && state->speedrunMode)
        return sPlayerbotAIConfig.mediumMana;

    return sPlayerbotAIConfig.raidRunManaThreshold;
}
}

bool RaidRunGoChatAction::Execute(Event event)
{
    if (!IsMasterCommand(botAI, event))
        return false;

    Player* master = GetMaster();
    if (!master)
        return false;

    bool speedrun = event.getParam().find("speedrun") != std::string::npos ||
                    event.GetSource().find("speedrun") != std::string::npos;
    RaidRunState const* existing = sRaidRunMgr.GetState(master);
    std::string message;
    if (existing && existing->phase == RAID_RUN_PAUSED)
        message = sRaidRunMgr.ResumeRun(master);
    else
        message = sRaidRunMgr.StartRun(master, speedrun);
    botAI->TellMaster(message);
    return true;
}

bool RaidRunPauseChatAction::Execute(Event event)
{
    if (!IsMasterCommand(botAI, event))
        return false;

    Player* master = GetMaster();
    if (!master)
        return false;

    botAI->TellMaster(sRaidRunMgr.PauseRun(master));
    return true;
}

bool RaidRunStopChatAction::Execute(Event event)
{
    if (!IsMasterCommand(botAI, event))
        return false;

    Player* master = GetMaster();
    if (!master)
        return false;

    botAI->TellMaster(sRaidRunMgr.StopRun(master));
    return true;
}

bool RaidRunStatusChatAction::Execute(Event event)
{
    if (!IsMasterCommand(botAI, event))
        return false;

    Player* master = GetMaster();
    if (!master)
        return false;

    botAI->TellMaster(sRaidRunMgr.GetStatusText(master));
    return true;
}

bool RaidRunLeaderAction::isUseful()
{
    Player* master = GetMaster();
    if (!master)
        return false;

    RaidRunState const* state = sRaidRunMgr.GetState(master);
    if (!state || state->leaderTankGuid != bot->GetGUID())
        return false;

    if (state->phase != RAID_RUN_RUNNING && state->phase != RAID_RUN_REGEN)
        return false;

    if (bot->IsInCombat())
        return false;

    return true;
}

Creature* RaidRunLeaderAction::FindPullTarget(RaidRunRouteStep const& step)
{
    if (!step.bossEntry)
        return nullptr;

    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(bot, step.pullRadius);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, step.pullRadius);

    Creature* best = nullptr;
    float bestDistance = step.pullRadius;

    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!creature || !creature->IsAlive() || creature->GetEntry() != step.bossEntry)
            continue;

        if (!AttackersValue::IsPossibleTarget(creature, bot))
            continue;

        float dist = bot->GetDistance(creature);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            best = creature;
        }
    }

    return best;
}

bool RaidRunLeaderAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    RaidRunState* state = sRaidRunMgr.GetState(master);
    if (!state)
        return false;

    if (state->phase == RAID_RUN_PAUSED)
        return false;

    sRaidRunMgr.SyncRouteStep(master, bot);

    RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(state->routeStep);
    if (!step)
    {
        sRaidRunMgr.StopRun(master);
        botAI->TellMaster("Raid run complete — Arachnid wing cleared");
        return true;
    }

    if (NaxxRaidRunRoute::IsStepComplete(bot, state->routeStep))
    {
        sRaidRunMgr.AdvanceStep(master);
        if (RaidRunState const* updated = sRaidRunMgr.GetState(master))
        {
            if (updated->phase == RAID_RUN_WING_COMPLETE)
                botAI->TellMaster("Raid run complete — Arachnid wing cleared");
        }
        return true;
    }

    if (!AI_VALUE(bool, "raid group ready"))
    {
        if (state->phase == RAID_RUN_REGEN && sPlayerbotAIConfig.raidRunRegenTimeout > 0 &&
            state->regenBreakStarted > 0 &&
            time(nullptr) - state->regenBreakStarted >= static_cast<time_t>(sPlayerbotAIConfig.raidRunRegenTimeout))
        {
            state->phase = RAID_RUN_RUNNING;
            state->announcedRegen = false;
            botAI->TellMaster("Regen timeout — resuming");
        }
        else
        {
            if (state->phase != RAID_RUN_REGEN)
            {
                state->phase = RAID_RUN_REGEN;
                state->regenBreakStarted = time(nullptr);
                if (!state->announcedRegen)
                {
                    botAI->TellMaster("Regen break — waiting for group mana and health");
                    state->announcedRegen = true;
                }
            }
            return false;
        }
    }

    if (state->phase == RAID_RUN_REGEN)
    {
        state->phase = RAID_RUN_RUNNING;
        state->announcedRegen = false;
        botAI->TellMaster("Group ready — resuming");
    }

    float distance = ServerFacade::instance().GetDistance2d(bot, step->x, step->y);
    if (distance > step->arriveDistance)
        return MoveTo(step->mapId, step->x, step->y, step->z, false, false);

    if (step->bossEntry)
    {
        if (Creature* target = FindPullTarget(*step))
        {
            PullStrategy* strategy = PullStrategy::Get(botAI);
            if (!strategy)
                return false;

            if (!botAI->IsTank(bot))
                return false;

            strategy->RequestPull(target);
            context->GetValue<Unit*>("current target")->Set(target);
            botAI->ChangeEngine(BOT_STATE_COMBAT);
            return botAI->DoSpecificAction("pull start", event, true);
        }

        return false;
    }

    sRaidRunMgr.AdvanceStep(master);
    return true;
}

bool RaidRunFollowTankAction::isUseful()
{
    Player* master = GetMaster();
    if (!master)
        return false;

    RaidRunState const* state = sRaidRunMgr.GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE || state->phase == RAID_RUN_WING_COMPLETE)
        return false;

    if (state->leaderTankGuid == bot->GetGUID())
        return false;

    if (bot->IsInCombat())
        return false;

    return true;
}

bool RaidRunFollowTankAction::Execute(Event /*event*/)
{
    Player* master = GetMaster();
    RaidRunState const* state = master ? sRaidRunMgr.GetState(master) : nullptr;
    if (!state)
        return false;

    Player* tank = GetLeaderTank(master, state);
    if (!tank || tank == bot)
        return false;

    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        return false;

    float const maxDistance = std::max(sPlayerbotAIConfig.followDistance, 8.0f);
    return Follow(tank, maxDistance);
}

bool RaidRunRegenAction::isUseful()
{
    Player* master = GetMaster();
    if (!master)
        return false;

    RaidRunState const* state = sRaidRunMgr.GetState(master);
    if (!state || state->phase != RAID_RUN_REGEN)
        return false;

    if (bot->IsInCombat())
        return false;

    uint32 const healthThreshold = sPlayerbotAIConfig.raidRunHealthThreshold;
    uint32 const manaThreshold = RegenManaThreshold(state);

    if (bot->getStandState() == UNIT_STAND_STATE_SIT)
        return AI_VALUE2(uint8, "mana", "self target") < manaThreshold || bot->GetHealthPct() < healthThreshold;

    if (AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < manaThreshold)
        return true;

    return bot->GetHealthPct() < healthThreshold;
}

bool RaidRunRegenAction::Execute(Event event)
{
    Player* master = GetMaster();
    RaidRunState const* state = master ? sRaidRunMgr.GetState(master) : nullptr;
    uint32 const healthThreshold = sPlayerbotAIConfig.raidRunHealthThreshold;
    uint32 const manaThreshold = RegenManaThreshold(state);

    if (AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < manaThreshold)
    {
        DrinkAction drink(botAI);
        if (drink.Execute(event))
            return true;
    }

    if (bot->GetHealthPct() < healthThreshold)
    {
        EatAction food(botAI);
        return food.Execute(event);
    }

    return false;
}

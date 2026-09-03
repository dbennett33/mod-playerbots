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
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "NaxxRaidRunRoute.h"
#include "NaxxSpellIds.h"
#include "NonCombatActions.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "PullStrategy.h"
#include "RaidRunMgr.h"
#include "RaidRunState.h"
#include "ServerFacade.h"
#include "WorldPacket.h"
#include <cmath>
#include <ctime>
#include <sstream>
#include <string>

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

void UseGameObjectPacket(Player* bot, GameObject* go)
{
    if (!bot || !go || !bot->GetSession())
        return;

    WorldPacket data(CMSG_GAMEOBJ_USE);
    data << go->GetGUID();
    bot->GetSession()->HandleGameObjectUseOpcode(data);
}
}

bool RaidRunGoChatAction::Execute(Event event)
{
    if (!IsMasterCommand(botAI, event))
        return false;

    Player* master = GetMaster();
    if (!master)
        return false;

    RaidRunWing wing = RAID_RUN_WING_NONE;
    if (event.getParam().find("construct") != std::string::npos)
        wing = RAID_RUN_WING_NAXX_CONSTRUCT;
    RaidRunState const* existing = sRaidRunMgr.GetState(master);
    std::string message;
    if (existing && existing->phase == RAID_RUN_PAUSED)
        message = sRaidRunMgr.ResumeRun(master);
    else
        message = sRaidRunMgr.StartRun(master, wing);
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

bool RaidRunLeaderAction::PullTarget(Creature* target, Event event)
{
    if (!target)
        return false;

    if (!botAI->IsTank(bot))
        return false;

    // Embalming Slime pulses a poison cloud; do not charge into the pit.
    float engageDistance = sPlayerbotAIConfig.contactDistance > 8.0f ? sPlayerbotAIConfig.contactDistance : 8.0f;
    if (target->GetEntry() == NaxxSpellIds::NpcEmbalmingSlime)
        engageDistance = 18.0f;

    if (bot->GetDistance(target) > engageDistance)
        return MoveTo(target);

    PullStrategy* strategy = PullStrategy::Get(botAI);
    if (!strategy)
        return false;

    strategy->RequestPull(target);
    context->GetValue<Unit*>("current target")->Set(target);
    botAI->ChangeEngine(BOT_STATE_COMBAT);
    return botAI->DoSpecificAction("pull start", event, true);
}

bool RaidRunLeaderAction::UseNaxxPortal(uint32 goEntry, float x, float y, float z)
{
    GameObject* go = bot->FindNearestGameObject(goEntry, 120.0f);
    if (!go)
        return MoveTo(533, x, y, z, false, false);

    if (!go->IsAtInteractDistance(bot))
        return MoveTo(go, std::max(go->GetInteractionDistance() - 1.0f, 0.0f));

    UseGameObjectPacket(bot, go);
    return true;
}

bool RaidRunLeaderAction::ReportNoPath(RaidRunRouteStep const& step)
{
    Player* master = GetMaster();
    RaidRunState* state = master ? sRaidRunMgr.GetState(master) : nullptr;
    uint8 const index = state ? state->routeStep : 0;
    uint32 const total = state ? NaxxRaidRunRoute::GetStepCount(state->wing) : 0;

    LOG_WARN("playerbots", "RaidRun: no path to {} ({:.1f}, {:.1f}, {:.1f} map {})",
        step.name, step.x, step.y, step.z, step.mapId);

    if (state && state->noPathAnnouncedStep != index)
    {
        std::ostringstream out;
        out << "no path to " << step.name << " (step " << static_cast<uint32>(index + 1) << "/" << total << ")";
        botAI->TellMaster(out.str());
        state->noPathAnnouncedStep = index;
    }

    return false;
}

bool RaidRunLeaderAction::MoveToStepStrict(RaidRunRouteStep const& step)
{
    PathGenerator gen(bot);
    gen.CalculatePath(step.x, step.y, step.z, false);
    if (gen.GetPathType() & PATHFIND_NOPATH)
        return ReportNoPath(step);

    if (std::fabs(gen.GetActualEndPosition().z - step.z) > 8.0f)
        return ReportNoPath(step);

    return MoveTo(step.mapId, step.x, step.y, step.z, false, false, false, true);
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

    RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(state->wing, state->routeStep);
    if (!step)
    {
        sRaidRunMgr.StopRun(master);
        botAI->TellMaster(std::string("Raid run complete — ") + NaxxRaidRunRoute::GetWingName(state->wing)
            + " wing cleared");
        return true;
    }

    if (NaxxRaidRunRoute::IsStepComplete(bot, state->wing, state->routeStep))
    {
        RaidRunWing const prevWing = state->wing;
        sRaidRunMgr.AdvanceStep(master);
        if (RaidRunState const* updated = sRaidRunMgr.GetState(master))
        {
            if (updated->phase == RAID_RUN_WING_COMPLETE)
            {
                botAI->TellMaster(std::string("Raid run complete — ") + NaxxRaidRunRoute::GetWingName(prevWing)
                    + " wing cleared");
            }
            else if (updated->wing != prevWing)
            {
                botAI->TellMaster(std::string("Arachnid cleared — starting ")
                    + NaxxRaidRunRoute::GetWingName(updated->wing) + " wing");
            }
            else if (RaidRunRouteStep const* next = NaxxRaidRunRoute::GetStep(updated->wing, updated->routeStep))
            {
                if (next->portalGoEntry)
                    botAI->TellMaster("Heading to the wing portal");
            }
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
            state->ClearStuckTracking();
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
        state->ClearStuckTracking();
        botAI->TellMaster("Group ready — resuming");
    }

    if (Creature* trash = NaxxRaidRunRoute::FindClearableTrash(bot, *step))
        return PullTarget(trash, event);

    if (step->portalGoEntry)
        return UseNaxxPortal(step->portalGoEntry, step->x, step->y, step->z);

    // Keep the step's Z. SearchForBestPath otherwise snaps onto Grobbulus's lab (Z 311).
    float distance = ServerFacade::instance().GetDistance2d(bot, step->x, step->y);
    if (distance > step->arriveDistance)
    {
        if (state->phase == RAID_RUN_RUNNING)
        {
            time_t const now = time(nullptr);
            float const progress = bot->GetExactDist2d(state->lastProgressX, state->lastProgressY);
            if (state->lastProgressAt == 0 || progress >= 5.0f)
            {
                state->lastProgressX = bot->GetPositionX();
                state->lastProgressY = bot->GetPositionY();
                state->lastProgressAt = now;
                if (progress >= 5.0f)
                    state->stuckRetries = 0;
            }
            else if (now - state->lastProgressAt >= 15)
            {
                bot->StopMoving();
                ++state->stuckRetries;
                std::ostringstream retryMsg;
                retryMsg << "stuck at " << step->name << " — retry "
                    << static_cast<uint32>(state->stuckRetries) << "/3";
                botAI->TellMaster(retryMsg.str());
                state->lastProgressX = bot->GetPositionX();
                state->lastProgressY = bot->GetPositionY();
                state->lastProgressAt = now;
                if (state->stuckRetries >= 3)
                {
                    state->phase = RAID_RUN_PAUSED;
                    botAI->TellMaster(std::string("stuck at ") + step->name + " — run paused");
                    return false;
                }
                return MoveToStepStrict(*step);
            }
        }
        return MoveToStepStrict(*step);
    }

    if (step->bossEntry)
    {
        if (Creature* target = FindPullTarget(*step))
        {
            float const readyDist = sPlayerbotAIConfig.raidRunBossReadyDistance;
            if (readyDist > 0.0f)
            {
                uint32 const missing = RaidRunMgr::CountMembersNotReadyForBoss(bot, readyDist);
                if (missing > 0)
                {
                    if (!state->announcedBossWait)
                    {
                        std::ostringstream out;
                        out << "Waiting for " << missing << " raid member";
                        if (missing != 1)
                            out << "s";
                        out << " before pulling " << step->name;
                        botAI->TellMaster(out.str());
                        state->announcedBossWait = true;
                    }
                    return false;
                }
            }

            state->announcedBossWait = false;
            return PullTarget(target, event);
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

    if (!NaxxRaidRunRoute::IsAtNaxxHub(bot) && bot->GetExactDist2d(tank) > 120.0f)
    {
        GameObject* portal = NaxxRaidRunRoute::FindWingReturnPortal(bot, 150.0f);
        if (portal)
        {
            if (!portal->IsAtInteractDistance(bot))
                return MoveTo(portal, std::max(portal->GetInteractionDistance() - 1.0f, 0.0f));

            UseGameObjectPacket(bot, portal);
            return true;
        }
    }

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
    uint32 const manaThreshold = sPlayerbotAIConfig.raidRunManaThreshold;

    if (bot->getStandState() == UNIT_STAND_STATE_SIT)
        return AI_VALUE2(uint8, "mana", "self target") < manaThreshold || bot->GetHealthPct() < healthThreshold;

    if (AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < manaThreshold)
        return true;

    return bot->GetHealthPct() < healthThreshold;
}

bool RaidRunRegenAction::Execute(Event event)
{
    uint32 const healthThreshold = sPlayerbotAIConfig.raidRunHealthThreshold;
    uint32 const manaThreshold = sPlayerbotAIConfig.raidRunManaThreshold;

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

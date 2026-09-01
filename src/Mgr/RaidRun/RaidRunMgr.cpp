/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunMgr.h"
#include "Group.h"
#include "NaxxRaidRunRoute.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include <functional>
#include <sstream>

namespace
{
void ForEachMasterBot(Player* master, std::function<void(Player*, PlayerbotAI*)> const& fn)
{
    if (!master)
        return;

    if (PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(master))
    {
        for (PlayerBotMap::const_iterator it = mgr->GetPlayerBotsBegin(); it != mgr->GetPlayerBotsEnd(); ++it)
        {
            if (Player* bot = it->second)
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                    fn(bot, botAI);
        }
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        if (!bot)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == master)
            fn(bot, botAI);
    }
}
}

RaidRunState* RaidRunMgr::GetState(Player* master)
{
    if (!master)
        return nullptr;

    auto itr = _states.find(master->GetGUID());
    if (itr == _states.end())
        return nullptr;

    return &itr->second;
}

RaidRunState const* RaidRunMgr::GetState(Player const* master) const
{
    if (!master)
        return nullptr;

    auto itr = _states.find(master->GetGUID());
    if (itr == _states.end())
        return nullptr;

    return &itr->second;
}

void RaidRunMgr::ClearState(Player* master)
{
    if (!master)
        return;

    _states.erase(master->GetGUID());
}

Player* RaidRunMgr::FindLeaderTank(Player* master)
{
    if (!master)
        return nullptr;

    Group* group = master->GetGroup();
    if (!group)
        return nullptr;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member->GetMapId() != master->GetMapId())
            continue;

        if (!GET_PLAYERBOT_AI(member))
            continue;

        if (PlayerbotAI::IsTank(member, true))
            return member;
    }

    return nullptr;
}

void RaidRunMgr::AssignMainTank(Group* group, Player* tank)
{
    if (!group || !tank)
        return;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member)
            continue;

        if (PlayerbotAI::IsMainTank(member))
            group->SetGroupMemberFlag(member->GetGUID(), false, MEMBER_FLAG_MAINTANK);
    }

    group->SetGroupMemberFlag(tank->GetGUID(), true, MEMBER_FLAG_MAINTANK);
}

void RaidRunMgr::ApplyRunStrategies(Player* master)
{
    RaidRunState const* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return;

    ForEachMasterBot(master,
        [state](Player* bot, PlayerbotAI* botAI)
        {
            botAI->ChangeStrategy("-follow,-wait for attack", BOT_STATE_NON_COMBAT);
            botAI->ChangeStrategy("-wait for attack", BOT_STATE_COMBAT);

            if (bot->GetGUID() == state->leaderTankGuid)
            {
                botAI->ChangeStrategy("+raid run leader,-passive,-stay", BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy("-passive,-stay", BOT_STATE_COMBAT);
            }
            else
            {
                botAI->ChangeStrategy("+raid run follow,+raid run regen,-stay", BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy("-stay", BOT_STATE_COMBAT);

                if (botAI->ContainsStrategy(STRATEGY_TYPE_DPS))
                    botAI->ChangeStrategy("-threat,-conserve mana,+dps debuff", BOT_STATE_COMBAT);
            }
        });
}

void RaidRunMgr::RemoveRunStrategies(Player* master)
{
    ForEachMasterBot(master,
        [](Player* /*bot*/, PlayerbotAI* botAI)
        {
            botAI->ChangeStrategy("-raid run leader,-raid run follow,-raid run regen", BOT_STATE_NON_COMBAT);
            botAI->ChangeStrategy("-raid run leader,-raid run follow,-raid run regen", BOT_STATE_COMBAT);
            botAI->ChangeStrategy("+follow", BOT_STATE_NON_COMBAT);
        });
}

std::string RaidRunMgr::StartRun(Player* master, bool speedrunMode)
{
    if (!sPlayerbotAIConfig.enableRaidRun)
        return "Raid run is disabled in playerbots.conf";

    if (!master || !master->IsInWorld())
        return "Master not available";

    if (master->GetMapId() != 533)
        return "Raid run requires Naxxramas (map 533)";

    if (!master->GetGroup())
        return "You must be in a group to start a raid run";

    RaidRunState* existing = GetState(master);
    if (existing && existing->phase != RAID_RUN_IDLE)
    {
        if (existing->phase == RAID_RUN_PAUSED)
            return ResumeRun(master);

        if (existing->phase == RAID_RUN_REGEN || existing->phase == RAID_RUN_RUNNING)
        {
            Player* tank = FindLeaderTank(master);
            if (!tank)
                return "No bot tank found in the group";

            existing->routeStep = NaxxRaidRunRoute::FindFirstIncompleteStep(tank);
            existing->phase = RAID_RUN_RUNNING;
            existing->announcedRegen = false;
            existing->leaderTankGuid = tank->GetGUID();
            AssignMainTank(master->GetGroup(), tank);
            ApplyRunStrategies(master);

            std::ostringstream out;
            out << "Raid run resynced to instance — step "
                << static_cast<uint32>(existing->routeStep + 1) << "/"
                << static_cast<uint32>(NaxxRaidRunRoute::GetStepCount());
            if (RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(existing->routeStep))
                out << " (" << step->name << ")";
            return out.str();
        }
    }

    Player* tank = FindLeaderTank(master);
    if (!tank)
        return "No bot tank found in the group";

    RaidRunState& state = _states[master->GetGUID()];
    state.phase = RAID_RUN_RUNNING;
    state.wing = RAID_RUN_WING_NAXX_ARACHNID;
    state.routeStep = NaxxRaidRunRoute::FindFirstIncompleteStep(tank);
    state.leaderTankGuid = tank->GetGUID();
    state.regenBreakStarted = 0;
    state.speedrunMode = speedrunMode;
    state.announcedRegen = false;

    AssignMainTank(master->GetGroup(), tank);
    ApplyRunStrategies(master);

    std::ostringstream out;
    out << "Raid run started — Arachnid wing — " << tank->GetName() << " is leading";
    if (speedrunMode)
        out << " (speedrun mode)";
    return out.str();
}

std::string RaidRunMgr::PauseRun(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return "No raid run is active";

    state->phase = RAID_RUN_PAUSED;
    return "Raid run paused";
}

std::string RaidRunMgr::ResumeRun(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return "No raid run is active";

    if (state->phase == RAID_RUN_WING_COMPLETE)
        return "Wing already complete — whisper raid go to resync or raid stop to clear";

    if (Player* tank = FindLeaderTank(master))
        SyncRouteStep(master, tank);

    state->phase = RAID_RUN_RUNNING;
    state->announcedRegen = false;
    ApplyRunStrategies(master);
    return "Raid run resumed";
}

std::string RaidRunMgr::StopRun(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return "No raid run is active";

    if (Group* group = master->GetGroup())
        if (Player* tank = ObjectAccessor::FindPlayer(state->leaderTankGuid))
            group->SetGroupMemberFlag(tank->GetGUID(), false, MEMBER_FLAG_MAINTANK);

    RemoveRunStrategies(master);
    ClearState(master);
    return "Raid run stopped";
}

std::string RaidRunMgr::GetStatusText(Player* master) const
{
    RaidRunState const* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return "Raid run: idle";

    std::ostringstream out;
    out << "Raid run: ";
    switch (state->phase)
    {
        case RAID_RUN_RUNNING: out << "running"; break;
        case RAID_RUN_PAUSED: out << "paused"; break;
        case RAID_RUN_REGEN: out << "regen break"; break;
        case RAID_RUN_WING_COMPLETE: out << "wing complete"; break;
        default: out << "unknown"; break;
    }

    if (state->wing == RAID_RUN_WING_NAXX_ARACHNID)
        out << " — Arachnid wing";

    out << " — step " << static_cast<uint32>(state->routeStep + 1) << "/" << static_cast<uint32>(NaxxRaidRunRoute::GetStepCount());

    if (RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(state->routeStep))
        out << " (" << step->name << ")";

    return out.str();
}

void RaidRunMgr::SetPhase(Player* master, RaidRunPhase phase)
{
    if (RaidRunState* state = GetState(master))
        state->phase = phase;
}

void RaidRunMgr::AdvanceStep(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state)
        return;

    ++state->routeStep;
    state->announcedRegen = false;

    if (state->routeStep >= NaxxRaidRunRoute::GetStepCount())
    {
        state->phase = RAID_RUN_WING_COMPLETE;
        RemoveRunStrategies(master);
    }
}

void RaidRunMgr::SyncRouteStep(Player* master, Player* bot)
{
    RaidRunState* state = GetState(master);
    if (!state || !bot)
        return;

    uint8 const first = NaxxRaidRunRoute::FindFirstIncompleteStep(bot);
    if (first >= state->routeStep)
        return;

    // Rewind only if an earlier boss (or trash pinned before that boss) is still up.
    // The Faerlina->Maexxna U-hallway walks away from Maexxna, so Euclidean "passed"
    // flips travel steps incomplete and would bounce the tank between boss and door.
    for (uint8 i = first; i < state->routeStep; ++i)
    {
        RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(i);
        if (step && (step->bossEntry || step->clearRadius > 0.0f))
        {
            state->routeStep = first;
            return;
        }
    }
}

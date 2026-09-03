/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunMgr.h"
#include "Group.h"
#include "NaxxRaidRunRoute.h"
#include "ObjectAccessor.h"
#include "Player.h"
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

uint32 RaidRunMgr::CountMembersNotReadyForBoss(Player* tank, float range)
{
    if (!tank || range <= 0.0f)
        return 0;

    Group* group = tank->GetGroup();
    if (!group)
        return 0;

    uint32 missing = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == tank || !member->IsInWorld())
            continue;

        if (member->GetMap() != tank->GetMap())
            continue;

        if (tank->GetExactDist2d(member) > range || member->isMoving())
            ++missing;
    }

    return missing;
}

bool RaidRunMgr::IsInActiveRaidRun(Player* bot)
{
    if (!bot)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    Player* master = botAI->GetMaster();
    if (!master)
        return false;

    RaidRunState const* state = instance().GetState(master);
    if (!state)
        return false;

    return state->phase != RAID_RUN_IDLE && state->phase != RAID_RUN_WING_COMPLETE;
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

std::string RaidRunMgr::StartRun(Player* master, RaidRunWing requestedWing)
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
    if (existing && existing->phase != RAID_RUN_IDLE && existing->phase != RAID_RUN_WING_COMPLETE)
    {
        if (existing->phase == RAID_RUN_PAUSED)
            return ResumeRun(master);

        if (existing->phase == RAID_RUN_REGEN || existing->phase == RAID_RUN_RUNNING)
        {
            Player* tank = FindLeaderTank(master);
            if (!tank)
                return "No bot tank found in the group";

            existing->routeStep = NaxxRaidRunRoute::FindFirstIncompleteStep(tank, existing->wing);
            existing->phase = RAID_RUN_RUNNING;
            existing->announcedRegen = false;
            existing->announcedBossWait = false;
            existing->leaderTankGuid = tank->GetGUID();
            AssignMainTank(master->GetGroup(), tank);
            ApplyRunStrategies(master);

            std::ostringstream out;
            out << "Raid run resynced to instance — step "
                << static_cast<uint32>(existing->routeStep + 1) << "/"
                << static_cast<uint32>(NaxxRaidRunRoute::GetStepCount(existing->wing));
            if (RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(existing->wing, existing->routeStep))
                out << " (" << step->name << ")";
            return out.str();
        }
    }

    Player* tank = FindLeaderTank(master);
    if (!tank)
        return "No bot tank found in the group";

    RaidRunWing wing = requestedWing;
    uint8 routeStep = 0;
    if (wing == RAID_RUN_WING_NONE)
        wing = NaxxRaidRunRoute::SuggestWing(tank);

    if (NaxxRaidRunRoute::NeedsHubPortal(tank))
    {
        wing = RAID_RUN_WING_NAXX_ARACHNID;
        uint8 const count = NaxxRaidRunRoute::GetStepCount(wing);
        routeStep = count ? static_cast<uint8>(count - 1) : 0;
    }
    else
        routeStep = NaxxRaidRunRoute::FindFirstIncompleteStep(tank, wing);

    RaidRunState& state = _states[master->GetGUID()];
    state.phase = RAID_RUN_RUNNING;
    state.wing = wing;
    state.routeStep = routeStep;
    state.leaderTankGuid = tank->GetGUID();
    state.regenBreakStarted = 0;
    state.announcedRegen = false;
    state.announcedBossWait = false;
    state.noPathAnnouncedStep = 255;

    AssignMainTank(master->GetGroup(), tank);
    ApplyRunStrategies(master);

    std::ostringstream out;
    out << "Raid run started — " << NaxxRaidRunRoute::GetWingName(state.wing) << " wing — " << tank->GetName()
        << " is leading";
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

    if (state->wing != RAID_RUN_WING_NONE)
        out << " — " << NaxxRaidRunRoute::GetWingName(state->wing) << " wing";

    out << " — step " << static_cast<uint32>(state->routeStep + 1) << "/"
        << static_cast<uint32>(NaxxRaidRunRoute::GetStepCount(state->wing));

    if (RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(state->wing, state->routeStep))
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

    if (state->routeStep >= NaxxRaidRunRoute::GetStepCount(state->wing))
    {
        Player* tank = FindLeaderTank(master);
        RaidRunWing const next = tank ? NaxxRaidRunRoute::SuggestWing(tank) : RAID_RUN_WING_NONE;
        if (tank && next != state->wing && next != RAID_RUN_WING_NONE &&
            !NaxxRaidRunRoute::IsWingComplete(tank, next))
        {
            state->wing = next;
            state->routeStep = NaxxRaidRunRoute::FindFirstIncompleteStep(tank, next);
            state->phase = RAID_RUN_RUNNING;
            state->announcedRegen = false;
            return;
        }

        state->phase = RAID_RUN_WING_COMPLETE;
        RemoveRunStrategies(master);
    }
}

void RaidRunMgr::SyncRouteStep(Player* master, Player* bot)
{
    RaidRunState* state = GetState(master);
    if (!state || !bot)
        return;

    uint8 const first = NaxxRaidRunRoute::FindFirstIncompleteStep(bot, state->wing);
    if (first >= state->routeStep)
        return;

    // Rewind only if an earlier boss (or trash pinned before that boss) is still up.
    // The Faerlina->Maexxna U-hallway walks away from Maexxna, so Euclidean "passed"
    // flips travel steps incomplete and would bounce the tank between boss and door.
    for (uint8 i = first; i < state->routeStep; ++i)
    {
        RaidRunRouteStep const* step = NaxxRaidRunRoute::GetStep(state->wing, i);
        if (step && (step->bossEntry || step->clearRadius > 0.0f))
        {
            state->routeStep = first;
            return;
        }
    }
}

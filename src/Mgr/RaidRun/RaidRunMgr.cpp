/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunMgr.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RaidRunRoute.h"
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

void TellMasterFromRaidBot(Player* master, std::string const& text)
{
    if (!master || text.empty())
        return;

    Player* speaker = nullptr;
    if (RaidRunState const* state = RaidRunMgr::instance().GetState(master))
        if (!state->leaderTankGuid.IsEmpty())
            if (Player* tank = ObjectAccessor::FindPlayer(state->leaderTankGuid))
                if (GET_PLAYERBOT_AI(tank))
                    speaker = tank;

    if (!speaker)
        ForEachMasterBot(master,
            [&](Player* bot, PlayerbotAI* /*botAI*/)
            {
                if (!speaker)
                    speaker = bot;
            });

    if (PlayerbotAI* botAI = speaker ? GET_PLAYERBOT_AI(speaker) : nullptr)
        botAI->TellMaster(text);
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

void RaidRunMgr::RegisterProvider(uint32 mapId, RaidRunRouteProvider* provider)
{
    if (provider)
        _providers[mapId] = provider;
}

RaidRunRouteProvider* RaidRunMgr::GetProviderForMap(uint32 mapId) const
{
    auto const itr = _providers.find(mapId);
    if (itr == _providers.end())
        return nullptr;

    return itr->second;
}

RaidRunRouteProvider* RaidRunMgr::GetProvider(Player const* player) const
{
    return player ? GetProviderForMap(player->GetMapId()) : nullptr;
}

void RaidRunMgr::OnMasterLogout(Player* master)
{
    if (!master || !GetState(master))
        return;

    RemoveRunStrategies(master);
    ClearState(master);
}

void RaidRunMgr::CheckLeader(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE || state->phase == RAID_RUN_WING_COMPLETE)
        return;

    Player* tank = ObjectAccessor::FindPlayer(state->leaderTankGuid);
    bool const present = tank && tank->IsInWorld() && tank->GetMap() == master->GetMap()
        && tank->GetGroup() && master->GetGroup() && tank->GetGroup() == master->GetGroup();
    if (present)
    {
        state->leaderMissingSince = 0;
        return;
    }

    time_t const now = time(nullptr);
    if (state->leaderMissingSince == 0)
    {
        state->leaderMissingSince = now;
        return;
    }

    if (now - state->leaderMissingSince < 30)
        return;

    Player* next = FindLeaderTank(master);
    if (next)
    {
        state->leaderTankGuid = next->GetGUID();
        state->leaderMissingSince = 0;
        AssignMainTank(master->GetGroup(), next);
        ApplyRunStrategies(master);
        TellMasterFromRaidBot(master, std::string(next->GetName()) + " is the new lead tank");
        BroadcastStatus(master);
        return;
    }

    state->leaderMissingSince = 0;
    if (state->phase != RAID_RUN_PAUSED)
        SetPhase(master, RAID_RUN_PAUSED);
    TellMasterFromRaidBot(master, "no bot tank remaining — run paused");
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

        if (!member->IsAlive() || tank->GetExactDist2d(member) > range || member->isMoving())
            ++missing;
    }

    return missing;
}

uint32 RaidRunMgr::CountDeadMembers(Player* tank)
{
    if (!tank)
        return 0;

    Group* group = tank->GetGroup();
    if (!group)
        return 0;

    uint32 dead = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld())
            continue;

        if (member->GetMap() != tank->GetMap())
            continue;

        if (!member->IsAlive())
            ++dead;
    }

    return dead;
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

bool RaidRunMgr::HasLivingResurrector(Player* bot)
{
    if (!bot)
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || !member->IsInWorld())
            continue;

        if (member->GetMap() != bot->GetMap())
            continue;

        uint8 const cls = member->getClass();
        if (cls == CLASS_PRIEST || cls == CLASS_PALADIN || cls == CLASS_SHAMAN || cls == CLASS_DRUID)
            return true;
    }

    return false;
}

bool RaidRunMgr::ShouldSuppressSpiritRelease(Player* bot)
{
    if (!bot)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    Player* master = botAI->GetMaster();
    RaidRunState const* state = master ? instance().GetState(master) : nullptr;
    if (!state || state->phase == RAID_RUN_IDLE || state->phase == RAID_RUN_WING_COMPLETE)
        return false;

    if (state->wipeRecovery)
        return false;

    return HasLivingResurrector(bot);
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

    RaidRunRouteProvider* provider = GetProviderForMap(master->GetMapId());
    if (!provider)
        return "Raid run is not available on this map";

    if (!master->GetGroup())
        return "You must be in a group to start a raid run";

    Group* group = master->GetGroup();
    for (auto const& pair : _states)
    {
        if (pair.first == master->GetGUID())
            continue;

        RaidRunState const& other = pair.second;
        if (other.phase == RAID_RUN_IDLE || other.phase == RAID_RUN_WING_COMPLETE)
            continue;

        Player* otherMaster = ObjectAccessor::FindPlayer(pair.first);
        if (otherMaster && otherMaster->GetGroup() == group)
            return "A raid run is already active in this group";

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->GetGUID() == other.leaderTankGuid)
                return "A raid run is already active in this group";
        }
    }

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

            existing->routeStep = provider->FindFirstIncompleteStep(tank, existing->wing);
            existing->phase = RAID_RUN_RUNNING;
            existing->announcedRegen = false;
            existing->announcedBossWait = false;
            existing->wipeRecovery = false;
            existing->leaderMissingSince = 0;
            existing->leaderTankGuid = tank->GetGUID();
            AssignMainTank(master->GetGroup(), tank);
            ApplyRunStrategies(master);
            BroadcastStatus(master);

            std::ostringstream out;
            out << "Raid run resynced to instance — step "
                << static_cast<uint32>(existing->routeStep + 1) << "/"
                << static_cast<uint32>(provider->GetStepCount(existing->wing));
            if (RaidRunRouteStep const* step = provider->GetStep(existing->wing, existing->routeStep))
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
        wing = static_cast<RaidRunWing>(provider->SuggestWing(tank));

    if (provider->NeedsHubPortal(tank))
    {
        wing = RAID_RUN_WING_NAXX_ARACHNID;
        uint8 const count = provider->GetStepCount(wing);
        routeStep = count ? static_cast<uint8>(count - 1) : 0;
    }
    else
        routeStep = provider->FindFirstIncompleteStep(tank, wing);

    RaidRunState& state = _states[master->GetGUID()];
    state.phase = RAID_RUN_RUNNING;
    state.wing = wing;
    state.routeStep = routeStep;
    state.leaderTankGuid = tank->GetGUID();
    state.regenBreakStarted = 0;
    state.announcedRegen = false;
    state.announcedBossWait = false;
    state.announcedRecovery = false;
    state.noPathAnnouncedStep = 255;
    state.ClearStuckTracking();
    state.lastWipeCheck = 0;
    state.wipeRecovery = false;
    state.leaderMissingSince = 0;

    AssignMainTank(master->GetGroup(), tank);
    ApplyRunStrategies(master);
    BroadcastStatus(master);

    std::ostringstream out;
    out << "Raid run started — " << provider->GetWingName(state.wing) << " wing — " << tank->GetName()
        << " is leading";
    return out.str();
}

std::string RaidRunMgr::PauseRun(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == RAID_RUN_IDLE)
        return "No raid run is active";

    SetPhase(master, RAID_RUN_PAUSED);
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

    state->announcedRegen = false;
    state->wipeRecovery = false;
    ApplyRunStrategies(master);
    SetPhase(master, RAID_RUN_RUNNING);
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

    SetPhase(master, RAID_RUN_IDLE);
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
        case RAID_RUN_RECOVERY: out << "recovering"; break;
        case RAID_RUN_WING_COMPLETE: out << "wing complete"; break;
        default: out << "unknown"; break;
    }

    if (state->wing != RAID_RUN_WING_NONE)
        if (RaidRunRouteProvider* provider = GetProvider(master))
            out << " — " << provider->GetWingName(state->wing) << " wing";

    if (RaidRunRouteProvider* provider = GetProvider(master))
    {
        out << " — step " << static_cast<uint32>(state->routeStep + 1) << "/"
            << static_cast<uint32>(provider->GetStepCount(state->wing));
        if (RaidRunRouteStep const* step = provider->GetStep(state->wing, state->routeStep))
            out << " (" << step->name << ")";
    }

    return out.str();
}

void RaidRunMgr::BroadcastStatus(Player* master)
{
    RaidRunState const* state = GetState(master);
    if (!master)
        return;

    char const* phase = "idle";
    char const* wing = "";
    uint32 step = 0;
    uint32 steps = 0;
    std::string name;
    uint32 dead = 0;
    uint32 regen = 0;
    uint32 waiting = 0;

    if (state && state->phase != RAID_RUN_IDLE)
    {
        switch (state->phase)
        {
            case RAID_RUN_RUNNING: phase = "running"; break;
            case RAID_RUN_PAUSED: phase = "paused"; break;
            case RAID_RUN_REGEN: phase = "regen"; regen = 1; break;
            case RAID_RUN_RECOVERY: phase = "recovering"; break;
            case RAID_RUN_WING_COMPLETE: phase = "complete"; break;
            default: break;
        }
        if (RaidRunRouteProvider* provider = GetProvider(master))
        {
            wing = provider->GetWingName(state->wing);
            step = static_cast<uint32>(state->routeStep) + 1;
            steps = provider->GetStepCount(state->wing);
            if (RaidRunRouteStep const* routeStep = provider->GetStep(state->wing, state->routeStep))
            {
                name = routeStep->name;
                for (char& ch : name)
                    if (ch == ';')
                        ch = ' ';
                if (routeStep->bossEntry && state->announcedBossWait &&
                    sPlayerbotAIConfig.raidRunBossReadyDistance > 0.0f)
                    if (Player* tank = FindLeaderTank(master))
                        if (!tank->IsInCombat())
                            waiting = CountMembersNotReadyForBoss(tank, sPlayerbotAIConfig.raidRunBossReadyDistance);
            }
        }
        if (Player* tank = FindLeaderTank(master))
            dead = CountDeadMembers(tank);
        else
            dead = CountDeadMembers(master);
    }

    auto buildLine = [&](std::string const& stepName)
    {
        std::ostringstream line;
        line << "[RR] phase=" << phase << ";wing=" << wing << ";step=" << step << ";steps=" << steps
            << ";name=" << stepName << ";dead=" << dead << ";regen=" << regen << ";waiting=" << waiting;
        return line.str();
    };

    std::string text = buildLine(name);
    while (text.size() > 255 && !name.empty())
    {
        name.pop_back();
        text = buildLine(name);
    }
    if (text.size() > 255)
        return;

    Player* speaker = nullptr;
    if (state && !state->leaderTankGuid.IsEmpty())
        if (Player* tank = ObjectAccessor::FindPlayer(state->leaderTankGuid))
            if (tank->IsAlive() && GET_PLAYERBOT_AI(tank))
                speaker = tank;

    if (!speaker && master->GetGroup())
    {
        for (GroupReference* ref = master->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member->IsAlive() && GET_PLAYERBOT_AI(member))
            {
                speaker = member;
                break;
            }
        }
        if (!speaker)
        {
            for (GroupReference* ref = master->GetGroup()->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && GET_PLAYERBOT_AI(member))
                {
                    speaker = member;
                    break;
                }
            }
        }
    }

    if (PlayerbotAI* botAI = speaker ? GET_PLAYERBOT_AI(speaker) : nullptr)
        botAI->TellMaster(text);
}

void RaidRunMgr::SetPhase(Player* master, RaidRunPhase phase)
{
    RaidRunState* state = GetState(master);
    if (!state || state->phase == phase)
        return;

    state->phase = phase;
    BroadcastStatus(master);
}

bool RaidRunMgr::NeedsWipeRecovery(Player* ref) const
{
    if (!ref || !ref->GetGroup())
        return false;

    uint32 dead = 0;
    uint32 alive = 0;
    for (GroupReference* it = ref->GetGroup()->GetFirstMember(); it; it = it->next())
    {
        Player* member = it->GetSource();
        if (!member || !member->IsInWorld())
            continue;

        if (member->GetMap() != ref->GetMap())
            continue;

        if (member->IsAlive())
            ++alive;
        else
            ++dead;
    }

    if (dead == 0)
        return false;

    if (alive == 0)
        return true;

    return !HasLivingResurrector(ref);
}

void RaidRunMgr::ReviveBotsAtWingStart(Player* master)
{
    RaidRunState const* state = GetState(master);
    RaidRunRouteProvider* provider = GetProvider(master);
    if (!state || !provider)
        return;

    RaidRunRouteStep const* start = provider->GetStep(state->wing, 0);
    ForEachMasterBot(master,
        [start](Player* bot, PlayerbotAI* /*botAI*/)
        {
            if (bot->IsAlive())
                return;

            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
            if (start)
                bot->TeleportTo(start->mapId, start->x, start->y, start->z, bot->GetOrientation());
        });
}

void RaidRunMgr::HandleWipe(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->wipeRecovery)
        return;

    state->wipeRecovery = true;

    uint32 const mode = sPlayerbotAIConfig.raidRunWipeMode;
    if (mode == 1)
        ReviveBotsAtWingStart(master);

    if (state->phase != RAID_RUN_PAUSED)
        SetPhase(master, RAID_RUN_PAUSED);
    else
        BroadcastStatus(master);

    if (mode == 1)
        TellMasterFromRaidBot(master, "wipe — bots revived at wing entrance (whisper raid go when ready)");
    else if (mode == 2)
        TellMasterFromRaidBot(master, "wipe — ghost run (whisper raid go when back)");
    else
        TellMasterFromRaidBot(master, "wipe — run paused (revive and whisper raid go)");
}

void RaidRunMgr::CheckWipe(Player* master)
{
    RaidRunState* state = GetState(master);
    if (!state || state->wipeRecovery)
        return;

    if (state->phase == RAID_RUN_IDLE || state->phase == RAID_RUN_WING_COMPLETE)
        return;

    time_t const now = time(nullptr);
    if (state->lastWipeCheck > 0 && now - state->lastWipeCheck < 5)
        return;

    state->lastWipeCheck = now;

    Player* ref = FindLeaderTank(master);
    if (!ref)
        ref = master;

    if (!NeedsWipeRecovery(ref))
        return;

    HandleWipe(master);
}

void RaidRunMgr::AdvanceStep(Player* master)
{
    RaidRunState* state = GetState(master);
    RaidRunRouteProvider* provider = GetProvider(master);
    if (!state || !provider)
        return;

    ++state->routeStep;
    state->announcedRegen = false;
    state->ClearStuckTracking();

    if (state->routeStep >= provider->GetStepCount(state->wing))
    {
        Player* tank = FindLeaderTank(master);
        RaidRunWing const next = tank ? static_cast<RaidRunWing>(provider->SuggestWing(tank)) : RAID_RUN_WING_NONE;
        if (tank && next != state->wing && next != RAID_RUN_WING_NONE &&
            !provider->IsWingComplete(tank, next))
        {
            state->wing = next;
            state->routeStep = provider->FindFirstIncompleteStep(tank, next);
            state->phase = RAID_RUN_RUNNING;
            state->announcedRegen = false;
            BroadcastStatus(master);
            return;
        }

        state->phase = RAID_RUN_WING_COMPLETE;
        RemoveRunStrategies(master);
        BroadcastStatus(master);
        return;
    }

    BroadcastStatus(master);
}

void RaidRunMgr::SyncRouteStep(Player* master, Player* bot)
{
    RaidRunState* state = GetState(master);
    RaidRunRouteProvider* provider = GetProvider(master);
    if (!state || !bot || !provider)
        return;

    uint8 const first = provider->FindFirstIncompleteStep(bot, state->wing);
    if (first >= state->routeStep)
        return;

    // Rewind only when an earlier boss is still incomplete. Trash/travel Euclidean
    // "passed" checks used to bounce the tank in the Faerlina U-hallway.
    for (uint8 i = first; i < state->routeStep; ++i)
    {
        RaidRunRouteStep const* step = provider->GetStep(state->wing, i);
        if (step && step->bossEntry && !provider->IsBossEncounterDone(bot, step->bossEntry))
        {
            state->routeStep = first;
            return;
        }
    }
}

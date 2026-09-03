/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNMGR_H
#define PLAYERBOTS_RAIDRUNMGR_H

#include "ObjectGuid.h"
#include "RaidRunState.h"
#include <string>
#include <unordered_map>

class Group;
class Player;
class PlayerbotMgr;
class RaidRunRouteProvider;

class RaidRunMgr
{
public:
    static RaidRunMgr& instance()
    {
        static RaidRunMgr inst;
        return inst;
    }

    RaidRunState* GetState(Player* master);
    RaidRunState const* GetState(Player const* master) const;

    std::string StartRun(Player* master, RaidRunWing wing = RAID_RUN_WING_NONE);
    std::string PauseRun(Player* master);
    std::string ResumeRun(Player* master);
    std::string StopRun(Player* master);
    std::string GetStatusText(Player* master) const;
    void BroadcastStatus(Player* master);

    void SetPhase(Player* master, RaidRunPhase phase);
    void CheckWipe(Player* master);
    void AdvanceStep(Player* master);
    void SyncRouteStep(Player* master, Player* bot);
    void ApplyRunStrategies(Player* master);
    void RemoveRunStrategies(Player* master);

    static Player* FindLeaderTank(Player* master);
    static void AssignMainTank(Group* group, Player* tank);
    static uint32 CountMembersNotReadyForBoss(Player* tank, float range);
    static uint32 CountDeadMembers(Player* tank);
    static bool IsInActiveRaidRun(Player* bot);
    static bool HasLivingResurrector(Player* bot);
    static bool ShouldSuppressSpiritRelease(Player* bot);

    void RegisterProvider(uint32 mapId, RaidRunRouteProvider* provider);
    RaidRunRouteProvider* GetProviderForMap(uint32 mapId) const;
    RaidRunRouteProvider* GetProvider(Player const* player) const;
    void OnMasterLogout(Player* master);
    void CheckLeader(Player* master);

private:
    RaidRunMgr() = default;

    void ClearState(Player* master);
    void HandleWipe(Player* master);
    bool NeedsWipeRecovery(Player* ref) const;
    void ReviveBotsAtWingStart(Player* master);
    std::unordered_map<ObjectGuid, RaidRunState> _states;
    std::unordered_map<uint32, RaidRunRouteProvider*> _providers;
};

#define sRaidRunMgr RaidRunMgr::instance()

#endif

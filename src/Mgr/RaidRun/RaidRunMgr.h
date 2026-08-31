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

    std::string StartRun(Player* master, bool speedrunMode = false);
    std::string PauseRun(Player* master);
    std::string ResumeRun(Player* master);
    std::string StopRun(Player* master);
    std::string GetStatusText(Player* master) const;

    void SetPhase(Player* master, RaidRunPhase phase);
    void AdvanceStep(Player* master);
    void ApplyRunStrategies(Player* master);
    void RemoveRunStrategies(Player* master);

    static Player* FindLeaderTank(Player* master);
    static void AssignMainTank(Group* group, Player* tank);

private:
    RaidRunMgr() = default;

    void ClearState(Player* master);
    std::unordered_map<ObjectGuid, RaidRunState> _states;
};

#define sRaidRunMgr RaidRunMgr::instance()

#endif

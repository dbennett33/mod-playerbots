/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RAIDRUNRECORDER_H
#define PLAYERBOTS_RAIDRUNRECORDER_H

#include "NaxxRaidRunRoute.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

class RaidRunRecorder
{
public:
    static RaidRunRecorder& instance()
    {
        static RaidRunRecorder inst;
        return inst;
    }

    std::string HandleCommand(Player* master, std::string const& param);
    void Update(Player* master);

private:
    RaidRunRecorder() = default;

    std::string Start(Player* master);
    std::string Stop(Player* master);
    std::string AddNamedPin(Player* master, std::string const& name, uint32 bossEntry, float arriveDistance,
                            float pullRadius, float clearRadius);
    void AutoPin(Player* master);
    std::string FormatTable(Player* owner) const;
    void WriteOutput(Player* owner) const;

    bool _active = false;
    ObjectGuid _masterGuid;
    uint32 _mapId = 0;
    uint32 _autoIndex = 0;
    std::vector<RaidRunRouteStep> _pins;
};

#define sRaidRunRecorder RaidRunRecorder::instance()

#endif

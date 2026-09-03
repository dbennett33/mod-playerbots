/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidRunRecorder.h"
#include "Log.h"
#include "PathGenerator.h"
#include "Player.h"
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::string FormatStep(RaidRunRouteStep const& step, bool warnNoPath)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    out << "    { \"" << step.name << "\", " << step.x << "f, " << step.y << "f, " << step.z << "f, "
        << step.mapId << ", " << step.bossEntry << ", " << step.arriveDistance << "f, " << step.pullRadius << "f";
    if (step.clearRadius > 0.0f || step.portalGoEntry)
    {
        out << ", " << step.clearRadius << "f";
        if (step.portalGoEntry)
            out << ", " << step.portalGoEntry;
    }
    out << " }";
    if (warnNoPath)
        out << "  // WARNING: no path";
    return out.str();
}

bool PairHasNoPath(Player* owner, RaidRunRouteStep const& from, RaidRunRouteStep const& to)
{
    if (!owner)
        return false;

    PathGenerator gen(owner);
    gen.CalculatePath(from.x, from.y, from.z, to.x, to.y, to.z, false);
    return (gen.GetPathType() & PATHFIND_NOPATH) != 0;
}
}

std::string RaidRunRecorder::HandleCommand(Player* master, std::string const& param)
{
    if (!master)
        return "Master not available";

    if (param == "start")
        return Start(master);

    if (param == "stop")
        return Stop(master);

    std::istringstream in(param);
    std::string verb;
    in >> verb;

    if (verb == "pin")
    {
        std::string name;
        std::getline(in, name);
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        if (name.empty())
            return "Usage: raid record pin <name>";
        return AddNamedPin(master, name, 0, 12.0f, 0.0f, 0.0f);
    }

    if (verb == "boss")
    {
        uint32 entry = 0;
        in >> entry;
        std::string name;
        std::getline(in, name);
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        if (!entry || name.empty())
            return "Usage: raid record boss <entry> <name>";
        return AddNamedPin(master, name, entry, 10.0f, 40.0f, 0.0f);
    }

    if (verb == "clear")
    {
        float radius = 0.0f;
        in >> radius;
        std::string name;
        std::getline(in, name);
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        if (radius <= 0.0f || name.empty())
            return "Usage: raid record clear <radius> <name>";
        return AddNamedPin(master, name, 0, 10.0f, 0.0f, radius);
    }

    return "Usage: raid record start|stop|pin <name>|boss <entry> <name>|clear <radius> <name>";
}

void RaidRunRecorder::Update(Player* master)
{
    if (!_active || !master || master->GetGUID() != _masterGuid)
        return;

    AutoPin(master);
}

std::string RaidRunRecorder::Start(Player* master)
{
    _active = true;
    _masterGuid = master->GetGUID();
    _mapId = master->GetMapId();
    _autoIndex = 0;
    _pins.clear();
    AutoPin(master);
    return "Route recording started — walk the path, then whisper raid record stop";
}

std::string RaidRunRecorder::Stop(Player* master)
{
    if (!_active || master->GetGUID() != _masterGuid)
        return "No route recording is active";

    AutoPin(master);
    _active = false;
    WriteOutput(master);

    std::ostringstream out;
    out << "Route recording stopped — " << _pins.size() << " pins, map " << _mapId
        << ". Table written to log and raidrun_route_" << _mapId << "_<timestamp>.txt";
    return out.str();
}

std::string RaidRunRecorder::AddNamedPin(Player* master, std::string const& name, uint32 bossEntry,
                                         float arriveDistance, float pullRadius, float clearRadius)
{
    if (!_active || master->GetGUID() != _masterGuid)
        return "Whisper raid record start first";

    RaidRunRouteStep step;
    step.name = name;
    step.x = master->GetPositionX();
    step.y = master->GetPositionY();
    step.z = master->GetPositionZ();
    step.mapId = master->GetMapId();
    step.bossEntry = bossEntry;
    step.arriveDistance = arriveDistance;
    step.pullRadius = pullRadius;
    step.clearRadius = clearRadius;
    _pins.push_back(step);

    std::ostringstream out;
    out << "Pinned \"" << name << "\" (" << _pins.size() << " pins)";
    return out.str();
}

void RaidRunRecorder::AutoPin(Player* master)
{
    if (!master || master->GetMapId() != _mapId)
        return;

    float const x = master->GetPositionX();
    float const y = master->GetPositionY();
    float const z = master->GetPositionZ();
    if (!_pins.empty())
    {
        RaidRunRouteStep const& last = _pins.back();
        float const dist2d = std::hypot(x - last.x, y - last.y);
        float const dz = std::fabs(z - last.z);
        if (dist2d <= 12.0f && dz <= 3.0f)
            return;
    }

    ++_autoIndex;
    std::ostringstream name;
    name << "pin " << _autoIndex;
    RaidRunRouteStep step;
    step.name = name.str();
    step.x = x;
    step.y = y;
    step.z = z;
    step.mapId = _mapId;
    step.bossEntry = 0;
    step.arriveDistance = 12.0f;
    step.pullRadius = 0.0f;
    _pins.push_back(step);
}

std::string RaidRunRecorder::FormatTable(Player* owner) const
{
    std::ostringstream out;
    out << "std::vector<RaidRunRouteStep> const steps =\n{\n";
    for (size_t i = 0; i < _pins.size(); ++i)
    {
        bool warn = false;
        if (i > 0)
            warn = PairHasNoPath(owner, _pins[i - 1], _pins[i]);
        out << FormatStep(_pins[i], warn);
        if (i + 1 < _pins.size())
            out << ",";
        out << "\n";
    }
    out << "};\n";
    return out.str();
}

void RaidRunRecorder::WriteOutput(Player* owner) const
{
    std::string const table = FormatTable(owner);
    LOG_INFO("playerbots", "RaidRun recorder output:\n{}", table);

    std::ostringstream filename;
    filename << "raidrun_route_" << _mapId << "_" << static_cast<uint64>(time(nullptr)) << ".txt";
    std::ofstream file(filename.str().c_str());
    if (!file)
    {
        LOG_WARN("playerbots", "RaidRun recorder could not write {}", filename.str());
        return;
    }

    file << table;
}

/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxRaidRunRoute.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "ServerFacade.h"

namespace
{
// Arachnid wing (map 533). Hub is 3005,-3434,304. Construct/Patchwerk is north (Y ~-3210, Z 293).
// Spider door is east of the hub (Anub room edge 3195,-3476). Do not use 3125,-3310 — that is Patchwerk's hallway.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    { "Naxx hub", 3005.51f, -3434.64f, 304.20f, 533, 0, 18.0f, 0.0f },
    { "Arachnid entrance", 3175.0f, -3476.0f, 287.50f, 533, 0, 12.0f, 0.0f },
    { "Anub'Rekhan", 3273.79f, -3477.21f, 287.62f, 533, 15956, 10.0f, 40.0f },
    { "Faerlina corridor", 3330.0f, -3580.0f, 265.0f, 533, 0, 12.0f, 0.0f },
    { "Grand Widow Faerlina", 3362.70f, -3620.40f, 261.33f, 533, 15953, 10.0f, 40.0f },
    { "Maexxna corridor", 3430.0f, -3800.0f, 290.0f, 533, 0, 12.0f, 0.0f },
    { "Maexxna", 3498.30f, -3893.40f, 296.62f, 533, 15952, 10.0f, 40.0f }
};
}

std::vector<RaidRunRouteStep> const& NaxxRaidRunRoute::GetArachnidSteps()
{
    return arachnidSteps;
}

uint8 NaxxRaidRunRoute::GetStepCount()
{
    return static_cast<uint8>(arachnidSteps.size());
}

RaidRunRouteStep const* NaxxRaidRunRoute::GetStep(uint8 index)
{
    if (index >= arachnidSteps.size())
        return nullptr;

    return &arachnidSteps[index];
}

bool NaxxRaidRunRoute::IsBossAlive(Player* bot, uint32 bossEntry, float range)
{
    if (!bot || !bossEntry)
        return false;

    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, range);

    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!creature || !creature->IsAlive() || creature->GetEntry() != bossEntry)
            continue;

        return true;
    }

    return false;
}

bool NaxxRaidRunRoute::IsStepComplete(Player* bot, RaidRunRouteStep const& step)
{
    if (!bot)
        return false;

    if (step.bossEntry)
        return !IsBossAlive(bot, step.bossEntry, 250.0f);

    return ServerFacade::instance().IsDistanceLessOrEqualThan(
        ServerFacade::instance().GetDistance2d(bot, step.x, step.y), step.arriveDistance);
}

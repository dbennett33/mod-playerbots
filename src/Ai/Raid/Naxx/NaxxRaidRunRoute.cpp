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
//
// Faerlina -> Maexxna is a U-shaped hallway (web south, west, south, then east). A straight line from
// Faerlina toward Maexxna hits the room's SE wall and clips through, skipping the trash packs.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    { "Naxx hub", 3005.51f, -3434.64f, 304.20f, 533, 0, 18.0f, 0.0f },
    { "Arachnid entrance", 3175.0f, -3476.0f, 287.50f, 533, 0, 12.0f, 0.0f },
    { "Anub'Rekhan", 3273.79f, -3477.21f, 287.62f, 533, 15956, 10.0f, 40.0f },
    { "Faerlina corridor", 3330.0f, -3580.0f, 265.0f, 533, 0, 12.0f, 0.0f },
    { "Grand Widow Faerlina", 3362.70f, -3620.40f, 261.33f, 533, 15953, 10.0f, 40.0f },
    { "Faerlina south", 3350.0f, -3660.0f, 261.08f, 533, 0, 12.0f, 0.0f },
    { "Faerlina web", 3318.0f, -3692.0f, 259.10f, 533, 0, 12.0f, 0.0f },
    { "Maexxna ramp", 3298.0f, -3710.0f, 268.00f, 533, 0, 12.0f, 0.0f },
    { "Maexxna landing", 3240.0f, -3690.0f, 287.16f, 533, 0, 12.0f, 0.0f },
    { "Maexxna descent", 3235.0f, -3745.0f, 281.00f, 533, 0, 12.0f, 0.0f },
    { "Maexxna lower hall", 3220.0f, -3795.0f, 274.03f, 533, 0, 12.0f, 0.0f },
    { "Maexxna west hall", 3148.0f, -3782.0f, 274.03f, 533, 0, 12.0f, 0.0f },
    { "Maexxna south corner", 3112.0f, -3880.0f, 267.60f, 533, 0, 12.0f, 0.0f },
    { "Maexxna south ramp", 3224.0f, -3877.0f, 284.56f, 533, 0, 12.0f, 0.0f },
    { "Maexxna south hall", 3310.0f, -3880.0f, 294.66f, 533, 0, 12.0f, 0.0f },
    { "Maexxna gate", 3410.0f, -3824.0f, 294.75f, 533, 0, 12.0f, 0.0f },
    { "Maexxna", 3511.38f, -3921.58f, 299.51f, 533, 15952, 10.0f, 40.0f }
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

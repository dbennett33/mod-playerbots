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
#include "InstanceScript.h"
#include "Map.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include <cmath>

namespace
{
// Arachnid wing (map 533). Hub center is 3005,-3434,304 (DK portal). Do not waypoint that —
// it pulls the raid into the middle of the start room. Spider door is east (Anub room edge
// 3195,-3476, Z 287). Do not use 3125,-3310 — that is Patchwerk's hallway.
//
// Faerlina -> Maexxna is a U-shaped hallway (web south, west, south, then east). A straight line from
// Faerlina toward Maexxna hits the room's SE wall and clips through, skipping the trash packs.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    { "Arachnid entrance", 3175.0f, -3476.0f, 287.50f, 533, 0, 12.0f, 0.0f },
    // Anub spawn (east end). Do not use room center 3273,-3477 — that drags him off the door.
    { "Anub'Rekhan", 3308.59f, -3476.29f, 287.16f, 533, 15956, 10.0f, 40.0f },
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

// Must match NaxxramasEncouter in naxxramas.h.
constexpr uint32 NPC_ANUBREKHAN = 15956;
constexpr uint32 NPC_FAERLINA = 15953;
constexpr uint32 NPC_MAEXXNA = 15952;
constexpr uint32 NAXX_BOSS_ANUB = 6;
constexpr uint32 NAXX_BOSS_FAERLINA = 7;
constexpr uint32 NAXX_BOSS_MAEXXNA = 8;

bool EncounterIdForBoss(uint32 bossEntry, uint32& encounterId)
{
    switch (bossEntry)
    {
        case NPC_ANUBREKHAN:
            encounterId = NAXX_BOSS_ANUB;
            return true;
        case NPC_FAERLINA:
            encounterId = NAXX_BOSS_FAERLINA;
            return true;
        case NPC_MAEXXNA:
            encounterId = NAXX_BOSS_MAEXXNA;
            return true;
        default:
            return false;
    }
}

RaidRunRouteStep const* NextBossStep(uint8 index)
{
    for (uint8 i = index + 1; i < arachnidSteps.size(); ++i)
    {
        if (arachnidSteps[i].bossEntry)
            return &arachnidSteps[i];
    }

    return nullptr;
}

bool IsTravelStepPassed(Player* bot, uint8 index)
{
    RaidRunRouteStep const* step = &arachnidSteps[index];
    RaidRunRouteStep const* nextBoss = NextBossStep(index);
    if (!nextBoss)
        return false;

    if (NaxxRaidRunRoute::IsBossEncounterDone(bot, nextBoss->bossEntry))
        return true;

    float const botToBoss = bot->GetExactDist2d(nextBoss->x, nextBoss->y);
    float const stepToBoss = std::hypot(step->x - nextBoss->x, step->y - nextBoss->y);
    return botToBoss + step->arriveDistance < stepToBoss;
}
}  // namespace

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

bool NaxxRaidRunRoute::IsBossEncounterDone(Player* bot, uint32 bossEntry)
{
    if (!bot || !bossEntry)
        return false;

    uint32 encounterId = 0;
    if (!EncounterIdForBoss(bossEntry, encounterId))
        return false;

    Map* map = bot->GetMap();
    if (!map || !map->IsDungeon())
        return false;

    InstanceMap* imap = map->ToInstanceMap();
    if (!imap)
        return false;

    InstanceScript* instance = imap->GetInstanceScript();
    if (!instance)
        return false;

    return instance->IsBossDone(encounterId);
}

bool NaxxRaidRunRoute::IsStepComplete(Player* bot, uint8 index)
{
    if (!bot || index >= arachnidSteps.size())
        return true;

    RaidRunRouteStep const& step = arachnidSteps[index];
    if (step.bossEntry)
        return IsBossEncounterDone(bot, step.bossEntry);

    if (ServerFacade::instance().IsDistanceLessOrEqualThan(
            ServerFacade::instance().GetDistance2d(bot, step.x, step.y), step.arriveDistance))
        return true;

    return IsTravelStepPassed(bot, index);
}

uint8 NaxxRaidRunRoute::FindFirstIncompleteStep(Player* bot)
{
    uint8 const count = GetStepCount();
    if (!bot)
        return 0;

    for (uint8 i = 0; i < count; ++i)
    {
        if (!IsStepComplete(bot, i))
            return i;
    }

    return count;
}

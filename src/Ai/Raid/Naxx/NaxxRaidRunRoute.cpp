/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxRaidRunRoute.h"
#include "AttackersValue.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObject.h"
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
//
// Faerlina room cultists (15980) / acolytes (15981) are in formation with her (groupAI member-assist).
// Pulling her with packs up brings the rest of the room. Path east wall, then south, then west.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    { "Arachnid entrance", 3175.0f, -3476.0f, 287.50f, 533, 0, 12.0f, 0.0f },
    // Anub spawn (east end). Do not use room center 3273,-3477 — that drags him off the door.
    { "Anub'Rekhan", 3308.59f, -3476.29f, 287.16f, 533, 15956, 10.0f, 40.0f },
    { "Faerlina corridor", 3330.0f, -3580.0f, 265.0f, 533, 0, 12.0f, 0.0f },
    // Stay east of her 20yd detection while walking south to the packs behind the platform.
    { "Faerlina east wall", 3385.0f, -3588.0f, 261.08f, 533, 0, 12.0f, 0.0f },
    { "Faerlina left front", 3372.0f, -3648.0f, 259.17f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina left rear", 3375.0f, -3669.0f, 259.17f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina right rear", 3327.0f, -3669.0f, 259.17f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina right front", 3334.0f, -3648.0f, 259.17f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Grand Widow Faerlina", 3353.25f, -3620.10f, 261.08f, 533, 15953, 10.0f, 40.0f, 70.0f },
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
    { "Maexxna", 3511.38f, -3921.58f, 299.51f, 533, 15952, 10.0f, 40.0f },
    // GO 181575 casts 28444 to hub 3005.51,-3434.64,304. Do not walk the wing backwards.
    { "Maexxna portal", 3465.16f, -3940.45f, 308.79f, 533, 0, 8.0f, 0.0f, 0.0f, 181575 }
};

// Construct quarter is north of the hub. Do not waypoint hub center 3005,-3434,304.
// Order is Patchwerk (hallway) -> Grobbulus (upper lab, gate 3318,-3254) -> Gluth -> Thaddius.
// Grobbulus sits at Z 311; the Patchwerk floor is Z 294 — mmap the ramp, do not skip Z.
std::vector<RaidRunRouteStep> const constructSteps =
{
    { "Construct entrance", 3070.0f, -3365.0f, 298.40f, 533, 0, 12.0f, 0.0f },
    { "Construct hall golems", 3137.0f, -3353.0f, 294.05f, 533, 0, 10.0f, 0.0f, 20.0f },
    { "Construct hall giants", 3164.0f, -3276.0f, 294.90f, 533, 0, 10.0f, 0.0f, 22.0f },
    { "Patchwerk slimes", 3140.0f, -3212.0f, 294.15f, 533, 0, 10.0f, 0.0f, 28.0f },
    { "Patchwerk", 3256.36f, -3230.33f, 294.06f, 533, 16028, 12.0f, 45.0f, 45.0f },
    { "Patchwerk gate", 3318.0f, -3254.0f, 293.35f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp", 3295.0f, -3285.0f, 300.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus", 3227.58f, -3378.30f, 311.33f, 533, 15931, 12.0f, 45.0f, 40.0f },
    { "Gluth hall", 3285.0f, -3200.0f, 294.15f, 533, 0, 12.0f, 0.0f },
    { "Gluth", 3283.09f, -3156.96f, 297.79f, 533, 15932, 12.0f, 45.0f, 35.0f },
    { "Gluth gate", 3339.16f, -3100.64f, 296.81f, 533, 0, 12.0f, 0.0f },
    { "Thaddius gate", 3421.86f, -3017.51f, 295.62f, 533, 0, 12.0f, 0.0f },
    { "Thaddius", 3513.84f, -2926.55f, 302.91f, 533, 15928, 15.0f, 80.0f, 0.0f }
};

std::vector<RaidRunRouteStep> const emptySteps;

// Must match NaxxramasEncouter in naxxramas.h.
constexpr uint32 NPC_PATCHWERK = 16028;
constexpr uint32 NPC_GROBBULUS = 15931;
constexpr uint32 NPC_GLUTH = 15932;
constexpr uint32 NPC_THADDIUS = 15928;
constexpr uint32 NPC_STALAGG = 15929;
constexpr uint32 NPC_FEUGEN = 15930;
constexpr uint32 NPC_ANUBREKHAN = 15956;
constexpr uint32 NPC_FAERLINA = 15953;
constexpr uint32 NPC_MAEXXNA = 15952;
constexpr uint32 NPC_NAXXRAMAS_FOLLOWER = 16505;
constexpr uint32 NPC_NAXXRAMAS_WORSHIPPER = 16506;
constexpr uint32 NPC_NAXXRAMAS_CULTIST = 15980;
constexpr uint32 NPC_NAXXRAMAS_ACOLYTE = 15981;
constexpr uint32 NPC_PATCHWORK_GOLEM = 16017;
constexpr uint32 NPC_BILE_RETCHER = 16018;
constexpr uint32 NPC_MAD_SCIENTIST = 16020;
constexpr uint32 NPC_LIVING_MONSTROSITY = 16021;
constexpr uint32 NPC_SURGICAL_ASSIST = 16022;
constexpr uint32 NPC_EMBALMING_SLIME = 16024;
constexpr uint32 NPC_STITCHED_GIANT = 16025;
constexpr uint32 NPC_SLUDGE_BELCHER = 16029;
constexpr uint32 NAXX_BOSS_PATCHWERK = 0;
constexpr uint32 NAXX_BOSS_GROBBULUS = 1;
constexpr uint32 NAXX_BOSS_GLUTH = 2;
constexpr uint32 NAXX_BOSS_ANUB = 6;
constexpr uint32 NAXX_BOSS_FAERLINA = 7;
constexpr uint32 NAXX_BOSS_MAEXXNA = 8;
constexpr uint32 NAXX_BOSS_THADDIUS = 9;
constexpr uint32 GO_MAEXXNA_PORTAL = 181575;
constexpr uint32 GO_THADDIUS_PORTAL = 181576;
constexpr uint32 GO_LOATHEB_PORTAL = 181577;
constexpr uint32 GO_HORSEMAN_PORTAL = 181578;
constexpr float NAXX_HUB_X = 3005.51f;
constexpr float NAXX_HUB_Y = -3434.64f;
constexpr float NAXX_HUB_RADIUS = 50.0f;

bool EncounterIdForBoss(uint32 bossEntry, uint32& encounterId)
{
    switch (bossEntry)
    {
        case NPC_PATCHWERK:
            encounterId = NAXX_BOSS_PATCHWERK;
            return true;
        case NPC_GROBBULUS:
            encounterId = NAXX_BOSS_GROBBULUS;
            return true;
        case NPC_GLUTH:
            encounterId = NAXX_BOSS_GLUTH;
            return true;
        case NPC_ANUBREKHAN:
            encounterId = NAXX_BOSS_ANUB;
            return true;
        case NPC_FAERLINA:
            encounterId = NAXX_BOSS_FAERLINA;
            return true;
        case NPC_MAEXXNA:
            encounterId = NAXX_BOSS_MAEXXNA;
            return true;
        case NPC_THADDIUS:
            encounterId = NAXX_BOSS_THADDIUS;
            return true;
        default:
            return false;
    }
}

std::vector<RaidRunRouteStep> const& StepsFor(RaidRunWing wing)
{
    switch (wing)
    {
        case RAID_RUN_WING_NAXX_ARACHNID:
            return arachnidSteps;
        case RAID_RUN_WING_NAXX_CONSTRUCT:
            return constructSteps;
        default:
            return emptySteps;
    }
}

RaidRunRouteStep const* NextBossStep(std::vector<RaidRunRouteStep> const& steps, uint8 index)
{
    for (uint8 i = index + 1; i < steps.size(); ++i)
    {
        if (steps[i].bossEntry)
            return &steps[i];
    }

    return nullptr;
}

bool IsTravelStepPassed(Player* bot, std::vector<RaidRunRouteStep> const& steps, uint8 index)
{
    if (index >= steps.size())
        return true;

    RaidRunRouteStep const* step = &steps[index];
    RaidRunRouteStep const* nextBoss = NextBossStep(steps, index);
    if (!nextBoss)
        return false;

    if (NaxxRaidRunRoute::IsBossEncounterDone(bot, nextBoss->bossEntry))
        return true;

    // Trash-clear pins must be walked. "Closer to next boss" would skip packs.
    if (step->clearRadius > 0.0f)
        return false;

    for (uint8 i = index + 1; i < steps.size(); ++i)
    {
        RaidRunRouteStep const& later = steps[i];
        if (ServerFacade::instance().IsDistanceLessOrEqualThan(
                ServerFacade::instance().GetDistance2d(bot, later.x, later.y), later.arriveDistance))
            return true;
    }

    float const botToBoss = bot->GetExactDist2d(nextBoss->x, nextBoss->y);
    float const stepToBoss = std::hypot(step->x - nextBoss->x, step->y - nextBoss->y);
    return botToBoss + step->arriveDistance < stepToBoss;
}

bool IsRouteBossEntry(uint32 entry)
{
    return entry == NPC_PATCHWERK || entry == NPC_GROBBULUS || entry == NPC_GLUTH || entry == NPC_THADDIUS
        || entry == NPC_ANUBREKHAN || entry == NPC_FAERLINA || entry == NPC_MAEXXNA
        || entry == NPC_STALAGG || entry == NPC_FEUGEN;
}

bool IsEncounterAdd(uint32 entry)
{
    return entry == NPC_NAXXRAMAS_WORSHIPPER || entry == NPC_NAXXRAMAS_FOLLOWER;
}

bool IsArachnidTrash(uint32 entry)
{
    return entry == NPC_NAXXRAMAS_CULTIST || entry == NPC_NAXXRAMAS_ACOLYTE;
}

bool IsConstructTrash(uint32 entry)
{
    return entry == NPC_PATCHWORK_GOLEM || entry == NPC_BILE_RETCHER || entry == NPC_MAD_SCIENTIST
        || entry == NPC_LIVING_MONSTROSITY || entry == NPC_SURGICAL_ASSIST || entry == NPC_EMBALMING_SLIME
        || entry == NPC_STITCHED_GIANT || entry == NPC_SLUDGE_BELCHER;
}

bool IsClearableTrash(Creature* creature, Player* bot, uint32 skipBossEntry)
{
    if (!creature || !creature->IsAlive())
        return false;

    uint32 const entry = creature->GetEntry();
    if (!IsArachnidTrash(entry) && !IsConstructTrash(entry))
        return false;

    if (IsRouteBossEntry(entry) || entry == skipBossEntry || IsEncounterAdd(entry))
        return false;

    if (creature->IsCritter() || creature->IsTotem() || creature->IsPet() || creature->IsSummon())
        return false;

    if (creature->IsTrigger() || creature->IsCivilian())
        return false;

    return AttackersValue::IsPossibleTarget(creature, bot);
}
}  // namespace

std::vector<RaidRunRouteStep> const& NaxxRaidRunRoute::GetSteps(RaidRunWing wing)
{
    return StepsFor(wing == RAID_RUN_WING_NONE ? RAID_RUN_WING_NAXX_ARACHNID : wing);
}

std::vector<RaidRunRouteStep> const& NaxxRaidRunRoute::GetArachnidSteps()
{
    return arachnidSteps;
}

uint8 NaxxRaidRunRoute::GetStepCount(RaidRunWing wing)
{
    return static_cast<uint8>(GetSteps(wing).size());
}

RaidRunRouteStep const* NaxxRaidRunRoute::GetStep(RaidRunWing wing, uint8 index)
{
    std::vector<RaidRunRouteStep> const& steps = GetSteps(wing);
    if (index >= steps.size())
        return nullptr;

    return &steps[index];
}

char const* NaxxRaidRunRoute::GetWingName(RaidRunWing wing)
{
    switch (wing)
    {
        case RAID_RUN_WING_NAXX_ARACHNID:
            return "Arachnid";
        case RAID_RUN_WING_NAXX_CONSTRUCT:
            return "Construct";
        default:
            return "Unknown";
    }
}

RaidRunWing NaxxRaidRunRoute::SuggestWing(Player* bot)
{
    if (!IsWingComplete(bot, RAID_RUN_WING_NAXX_ARACHNID))
        return RAID_RUN_WING_NAXX_ARACHNID;

    return RAID_RUN_WING_NAXX_CONSTRUCT;
}

bool NaxxRaidRunRoute::IsAtNaxxHub(Player* bot)
{
    if (!bot || bot->GetMapId() != 533)
        return false;

    return bot->GetExactDist2d(NAXX_HUB_X, NAXX_HUB_Y) <= NAXX_HUB_RADIUS;
}

bool NaxxRaidRunRoute::NeedsHubPortal(Player* bot)
{
    return bot && IsBossEncounterDone(bot, NPC_MAEXXNA) && !IsAtNaxxHub(bot);
}

GameObject* NaxxRaidRunRoute::FindWingReturnPortal(Player* bot, float range)
{
    if (!bot)
        return nullptr;

    GameObject* best = nullptr;
    float bestDist = range;
    uint32 const portals[] = {GO_MAEXXNA_PORTAL, GO_THADDIUS_PORTAL, GO_LOATHEB_PORTAL, GO_HORSEMAN_PORTAL};
    for (uint32 entry : portals)
    {
        GameObject* go = bot->FindNearestGameObject(entry, range);
        if (!go)
            continue;

        float const dist = bot->GetDistance(go);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = go;
        }
    }

    return best;
}

bool NaxxRaidRunRoute::IsWingComplete(Player* bot, RaidRunWing wing)
{
    return FindFirstIncompleteStep(bot, wing) >= GetStepCount(wing);
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

Creature* NaxxRaidRunRoute::FindClearableTrash(Player* bot, RaidRunRouteStep const& step)
{
    if (!bot || step.clearRadius <= 0.0f)
        return nullptr;

    float const toStep = bot->GetExactDist2d(step.x, step.y);
    float const range = toStep + step.clearRadius;
    if (range <= 0.0f)
        return nullptr;

    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(bot, range);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, range);

    Creature* best = nullptr;
    float bestDistance = range;

    for (Unit* unit : units)
    {
        Creature* creature = unit->ToCreature();
        if (!IsClearableTrash(creature, bot, step.bossEntry))
            continue;

        if (creature->GetExactDist2d(step.x, step.y) > step.clearRadius)
            continue;

        float const dist = bot->GetDistance(creature);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            best = creature;
        }
    }

    return best;
}

bool NaxxRaidRunRoute::IsStepComplete(Player* bot, RaidRunWing wing, uint8 index)
{
    std::vector<RaidRunRouteStep> const& steps = GetSteps(wing);
    if (!bot || index >= steps.size())
        return true;

    RaidRunRouteStep const& step = steps[index];
    if (step.portalGoEntry)
        return IsAtNaxxHub(bot);

    if (step.bossEntry)
    {
        if (!IsBossEncounterDone(bot, step.bossEntry))
            return false;

        return FindClearableTrash(bot, step) == nullptr;
    }

    if (step.clearRadius > 0.0f)
    {
        // Pack is dead: leave the pin. Requiring arriveDistance rewinds to the last
        // pack and loops east-west along Faerlina's south wall.
        if (IsTravelStepPassed(bot, steps, index))
            return true;

        return FindClearableTrash(bot, step) == nullptr;
    }

    if (ServerFacade::instance().IsDistanceLessOrEqualThan(
            ServerFacade::instance().GetDistance2d(bot, step.x, step.y), step.arriveDistance))
        return true;

    return IsTravelStepPassed(bot, steps, index);
}

uint8 NaxxRaidRunRoute::FindFirstIncompleteStep(Player* bot, RaidRunWing wing)
{
    uint8 const count = GetStepCount(wing);
    if (!bot)
        return 0;

    for (uint8 i = 0; i < count; ++i)
    {
        if (!IsStepComplete(bot, wing, i))
            return i;
    }

    return count;
}

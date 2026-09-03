/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxRaidRunRoute.h"
#include "RaidRunMgr.h"
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
// Hallway pins use clearRadius so the tank stops on each spider pack instead of running through.
//
// Faerlina room cultists (15980) / acolytes (15981) are in formation with her (groupAI member-assist).
// Pulling her with packs up brings the rest of the room. Path east wall, then south, then west.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    { "Arachnid entrance", 3175.0f, -3476.0f, 287.50f, 533, 0, 12.0f, 0.0f },
    // Anub spawn (east end). Do not use room center 3273,-3477 — that drags him off the door.
    { "Anub'Rekhan", 3308.59f, -3476.29f, 287.16f, 533, 15956, 10.0f, 40.0f },
    // Stay on the platform. 3210 is past the west slime; mmap walks the outer ring to get there.
    // South ramp starts at the platform edge (~Y -3510), not out in the moat at -3530.
    { "Anub exit", 3245.0f, -3476.0f, 287.16f, 533, 0, 12.0f, 0.0f },
    { "Anub-Faerlina ramp top", 3273.0f, -3510.0f, 287.16f, 533, 0, 12.0f, 0.0f },
    { "Anub-Faerlina ramp mid", 3300.0f, -3555.0f, 276.0f, 533, 0, 12.0f, 0.0f },
    { "Anub-Faerlina ramp bot", 3320.0f, -3570.0f, 265.0f, 533, 0, 12.0f, 0.0f },
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
    { "Maexxna ramp", 3298.0f, -3710.0f, 268.00f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna landing", 3240.0f, -3690.0f, 287.16f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna descent", 3235.0f, -3745.0f, 281.00f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna descent floor", 3244.5f, -3768.7f, 276.50f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna lower hall", 3220.0f, -3795.0f, 274.03f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna west skitters", 3171.0f, -3802.8f, 273.95f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna west hall", 3148.0f, -3782.0f, 274.03f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna south corner", 3112.0f, -3880.0f, 267.60f, 533, 0, 10.0f, 0.0f, 20.0f },
    { "Maexxna south ramp", 3224.0f, -3877.0f, 284.56f, 533, 0, 10.0f, 0.0f, 12.0f },
    { "Maexxna south venoms", 3284.7f, -3898.3f, 294.66f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna south hall", 3310.0f, -3880.0f, 294.66f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna gate", 3410.0f, -3824.0f, 294.75f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna", 3511.38f, -3921.58f, 299.51f, 533, 15952, 10.0f, 40.0f },
    // GO 181575 casts 28444 to hub 3005.51,-3434.64,304. Do not walk the wing backwards.
    { "Maexxna portal", 3465.16f, -3940.45f, 308.79f, 533, 0, 8.0f, 0.0f, 0.0f, 181575 }
};

// Construct quarter is north of the hub. Do not waypoint hub center 3005,-3434,304.
// Sparse pins: walkway GPS (not the vat wall), then bosses. Extra Z pins on the Grobbulus
// ramp only — that lab stacks over the first rooms and mmap will clip without them.
std::vector<RaidRunRouteStep> const constructSteps =
{
    { "Construct entrance", 3045.63f, -3395.20f, 299.39f, 533, 0, 12.0f, 0.0f },
    { "Construct first", 3088.10f, -3352.53f, 299.39f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Construct second", 3092.59f, -3313.92f, 293.63f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Construct hall", 3127.32f, -3266.78f, 294.17f, 533, 0, 10.0f, 0.0f, 20.0f },
    { "Construct slime", 3111.25f, -3236.74f, 294.06f, 533, 0, 10.0f, 0.0f, 22.0f },
    { "Patchwerk", 3256.36f, -3230.33f, 294.06f, 533, 16028, 12.0f, 45.0f, 45.0f },
    { "Patchwerk gate", 3317.40f, -3238.70f, 294.06f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp", 3295.0f, -3285.0f, 300.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp mid", 3285.0f, -3315.0f, 305.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp top", 3265.0f, -3345.0f, 309.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus", 3227.58f, -3378.30f, 311.33f, 533, 15931, 12.0f, 45.0f, 40.0f },
    { "Gluth hall", 3285.0f, -3200.0f, 294.15f, 533, 0, 12.0f, 0.0f },
    { "Gluth", 3283.09f, -3156.96f, 297.79f, 533, 15932, 12.0f, 45.0f, 35.0f },
    { "Gluth gate", 3339.16f, -3100.64f, 296.81f, 533, 0, 12.0f, 0.0f },
    { "Thaddius gate", 3421.86f, -3017.51f, 295.62f, 533, 0, 12.0f, 0.0f },
    // Walk to the low platform before pulling; bots on Feugen/Stalagg high bridges (Z 312)
    // cannot see Thaddius below (Z 302) so the phase-transition trigger never fires.
    // This pin forces mmap to descend to the correct floor first.
    { "Thaddius platform", 3480.0f, -2960.0f, 302.91f, 533, 0, 15.0f, 0.0f },
    { "Thaddius", 3513.84f, -2926.55f, 302.91f, 533, 15928, 15.0f, 80.0f, 0.0f }
};

// Plague quarter is west of the hub. Pins from instance doors/GOs (181198–181241, 181231)
// and script positions (Noth home, Loatheb tank). Ramp 297→262 needs Z pins. Optional
// recorder walk can still replace these if a pin clips.
std::vector<RaidRunRouteStep> const plagueSteps =
{
    { "Plague entrance", 2965.0f, -3476.0f, 297.60f, 533, 0, 12.0f, 0.0f },
    { "Plague upper hall", 2905.0f, -3485.0f, 297.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Plague west hall", 2847.43f, -3489.47f, 297.84f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Noth ramp top", 2820.0f, -3489.0f, 290.00f, 533, 0, 12.0f, 0.0f },
    { "Noth ramp mid", 2785.0f, -3489.0f, 276.00f, 533, 0, 12.0f, 0.0f },
    { "Noth door", 2737.66f, -3489.72f, 262.10f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Noth the Plaguebringer", 2684.94f, -3502.53f, 261.31f, 533, 15954, 12.0f, 40.0f, 30.0f },
    { "Noth exit", 2684.28f, -3559.36f, 261.91f, 533, 0, 12.0f, 0.0f },
    { "Heigan corridor", 2750.0f, -3620.0f, 267.00f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Heigan lower hall", 2800.0f, -3665.0f, 273.00f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Heigan door", 2822.93f, -3685.30f, 273.54f, 533, 0, 12.0f, 0.0f, 14.0f },
    // East of the platform, on the floor — not 2794,-3706 (Heigan's dance platform).
    { "Heigan the Unclean", 2793.80f, -3685.00f, 273.67f, 533, 15936, 12.0f, 35.0f, 0.0f },
    { "Heigan exit", 2771.50f, -3737.34f, 273.60f, 533, 0, 12.0f, 0.0f },
    { "Loatheb west hall", 2840.0f, -3778.0f, 273.55f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Loatheb south hall", 2909.69f, -3818.45f, 273.55f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Loatheb approach", 2909.69f, -3900.00f, 273.55f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Loatheb gate", 2909.69f, -3947.28f, 273.55f, 533, 0, 12.0f, 0.0f, 18.0f },
    { "Loatheb", 2877.57f, -3967.00f, 273.40f, 533, 16011, 12.0f, 40.0f, 0.0f },
    { "Loatheb portal", 2909.00f, -4025.02f, 273.48f, 533, 0, 8.0f, 0.0f, 0.0f, 181577 }
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
constexpr uint32 NPC_NOTH = 15954;
constexpr uint32 NPC_HEIGAN = 15936;
constexpr uint32 NPC_LOATHEB = 16011;
constexpr uint32 NPC_NAXXRAMAS_FOLLOWER = 16505;
constexpr uint32 NPC_NAXXRAMAS_WORSHIPPER = 16506;
constexpr uint32 NPC_DREAD_CREEPER = 15974;
constexpr uint32 NPC_CARRION_SPINNER = 15975;
constexpr uint32 NPC_VENOM_STALKER = 15976;
constexpr uint32 NPC_POISONOUS_SKITTERER = 15977;
constexpr uint32 NPC_CRYPT_REAVER = 15978;
constexpr uint32 NPC_TOMB_HORROR = 15979;
constexpr uint32 NPC_NAXXRAMAS_CULTIST = 15980;
constexpr uint32 NPC_NAXXRAMAS_ACOLYTE = 15981;
constexpr uint32 NPC_NECRO_STALKER = 16453;
constexpr uint32 NPC_PATCHWORK_GOLEM = 16017;
constexpr uint32 NPC_BILE_RETCHER = 16018;
constexpr uint32 NPC_MAD_SCIENTIST = 16020;
constexpr uint32 NPC_LIVING_MONSTROSITY = 16021;
constexpr uint32 NPC_SURGICAL_ASSIST = 16022;
constexpr uint32 NPC_EMBALMING_SLIME = 16024;
constexpr uint32 NPC_STITCHED_GIANT = 16025;
constexpr uint32 NPC_SLUDGE_BELCHER = 16029;
constexpr uint32 NPC_PLAGUE_BEAST = 16034;
constexpr uint32 NPC_FRENZIED_BAT = 16036;
constexpr uint32 NPC_DISEASED_MAGGOT = 16056;
constexpr uint32 NPC_ROTTING_MAGGOT = 16057;
constexpr uint32 NPC_PLAGUE_SLIME = 16243;
constexpr uint32 NPC_INFECTIOUS_GHOUL = 16244;
constexpr uint32 NAXX_BOSS_PATCHWERK = 0;
constexpr uint32 NAXX_BOSS_GROBBULUS = 1;
constexpr uint32 NAXX_BOSS_GLUTH = 2;
constexpr uint32 NAXX_BOSS_ANUB = 6;
constexpr uint32 NAXX_BOSS_FAERLINA = 7;
constexpr uint32 NAXX_BOSS_MAEXXNA = 8;
constexpr uint32 NAXX_BOSS_THADDIUS = 9;
constexpr uint32 NAXX_BOSS_NOTH = 3;
constexpr uint32 NAXX_BOSS_HEIGAN = 4;
constexpr uint32 NAXX_BOSS_LOATHEB = 5;
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
        case NPC_NOTH:
            encounterId = NAXX_BOSS_NOTH;
            return true;
        case NPC_HEIGAN:
            encounterId = NAXX_BOSS_HEIGAN;
            return true;
        case NPC_LOATHEB:
            encounterId = NAXX_BOSS_LOATHEB;
            return true;
        default:
            return false;
    }
}

std::vector<RaidRunRouteStep> const& StepsFor(uint8 wing)
{
    switch (wing)
    {
        case RAID_RUN_WING_NAXX_ARACHNID:
            return arachnidSteps;
        case RAID_RUN_WING_NAXX_CONSTRUCT:
            return constructSteps;
        case RAID_RUN_WING_NAXX_PLAGUE:
            return plagueSteps;
        default:
            return emptySteps;
    }
}

bool IsTravelStepArrived(Player* bot, RaidRunRouteStep const& step)
{
    if (!bot)
        return false;

    if (std::fabs(bot->GetPositionZ() - step.z) > 5.0f)
        return false;

    return ServerFacade::instance().IsDistanceLessOrEqualThan(
        ServerFacade::instance().GetDistance2d(bot, step.x, step.y), step.arriveDistance);
}

bool IsRouteBossEntry(uint32 entry)
{
    return entry == NPC_PATCHWERK || entry == NPC_GROBBULUS || entry == NPC_GLUTH || entry == NPC_THADDIUS
        || entry == NPC_ANUBREKHAN || entry == NPC_FAERLINA || entry == NPC_MAEXXNA
        || entry == NPC_STALAGG || entry == NPC_FEUGEN
        || entry == NPC_NOTH || entry == NPC_HEIGAN || entry == NPC_LOATHEB;
}

bool IsEncounterAdd(uint32 entry)
{
    return entry == NPC_NAXXRAMAS_WORSHIPPER || entry == NPC_NAXXRAMAS_FOLLOWER;
}

bool IsArachnidTrash(uint32 entry)
{
    return entry == NPC_NAXXRAMAS_CULTIST || entry == NPC_NAXXRAMAS_ACOLYTE
        || entry == NPC_DREAD_CREEPER || entry == NPC_CARRION_SPINNER
        || entry == NPC_VENOM_STALKER || entry == NPC_POISONOUS_SKITTERER
        || entry == NPC_CRYPT_REAVER || entry == NPC_TOMB_HORROR
        || entry == NPC_NECRO_STALKER;
}

bool IsConstructTrash(uint32 entry)
{
    return entry == NPC_PATCHWORK_GOLEM || entry == NPC_BILE_RETCHER || entry == NPC_MAD_SCIENTIST
        || entry == NPC_LIVING_MONSTROSITY || entry == NPC_SURGICAL_ASSIST || entry == NPC_EMBALMING_SLIME
        || entry == NPC_STITCHED_GIANT || entry == NPC_SLUDGE_BELCHER;
}

bool IsPlagueTrash(uint32 entry)
{
    return entry == NPC_PLAGUE_BEAST || entry == NPC_FRENZIED_BAT || entry == NPC_DISEASED_MAGGOT
        || entry == NPC_ROTTING_MAGGOT || entry == NPC_PLAGUE_SLIME || entry == NPC_INFECTIOUS_GHOUL;
}

bool IsClearableTrash(Creature* creature, Player* bot, uint32 skipBossEntry)
{
    if (!creature || !creature->IsAlive())
        return false;

    uint32 const entry = creature->GetEntry();
    if (!IsArachnidTrash(entry) && !IsConstructTrash(entry) && !IsPlagueTrash(entry))
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

std::vector<RaidRunRouteStep> const& NaxxRaidRunRouteProvider::GetSteps(uint8 wing) const
{
    return StepsFor(wing == RAID_RUN_WING_NONE ? static_cast<uint8>(RAID_RUN_WING_NAXX_ARACHNID) : wing);
}

std::vector<RaidRunRouteStep> const& NaxxRaidRunRouteProvider::GetArachnidSteps() const
{
    return arachnidSteps;
}

uint8 NaxxRaidRunRouteProvider::GetStepCount(uint8 wing) const
{
    return static_cast<uint8>(GetSteps(wing).size());
}

RaidRunRouteStep const* NaxxRaidRunRouteProvider::GetStep(uint8 wing, uint8 index) const
{
    std::vector<RaidRunRouteStep> const& steps = GetSteps(wing);
    if (index >= steps.size())
        return nullptr;

    return &steps[index];
}

char const* NaxxRaidRunRouteProvider::GetWingName(uint8 wing) const
{
    switch (wing)
    {
        case RAID_RUN_WING_NAXX_ARACHNID:
            return "Arachnid";
        case RAID_RUN_WING_NAXX_CONSTRUCT:
            return "Construct";
        case RAID_RUN_WING_NAXX_PLAGUE:
            return "Plague";
        default:
            return "Unknown";
    }
}

uint8 NaxxRaidRunRouteProvider::SuggestWing(Player* bot) const
{
    if (!IsWingComplete(bot, RAID_RUN_WING_NAXX_ARACHNID))
        return RAID_RUN_WING_NAXX_ARACHNID;

    if (!IsWingComplete(bot, RAID_RUN_WING_NAXX_CONSTRUCT))
        return RAID_RUN_WING_NAXX_CONSTRUCT;

    return RAID_RUN_WING_NAXX_PLAGUE;
}

bool NaxxRaidRunRouteProvider::IsAtHub(Player* bot) const
{
    if (!bot || bot->GetMapId() != 533)
        return false;

    return bot->GetExactDist2d(NAXX_HUB_X, NAXX_HUB_Y) <= NAXX_HUB_RADIUS;
}

bool NaxxRaidRunRouteProvider::NeedsHubPortal(Player* bot) const
{
    return GetHubReturnWing(bot) != RAID_RUN_WING_NONE;
}

uint8 NaxxRaidRunRouteProvider::GetHubReturnWing(Player* bot) const
{
    if (!bot || IsAtHub(bot))
        return RAID_RUN_WING_NONE;

    GameObject* portal = FindWingReturnPortal(bot, 150.0f);
    if (portal)
    {
        switch (portal->GetEntry())
        {
            case GO_MAEXXNA_PORTAL:
                return RAID_RUN_WING_NAXX_ARACHNID;
            case GO_LOATHEB_PORTAL:
                return RAID_RUN_WING_NAXX_PLAGUE;
            default:
                break;
        }
    }

    if (IsBossEncounterDone(bot, NPC_LOATHEB))
        return RAID_RUN_WING_NAXX_PLAGUE;

    if (IsBossEncounterDone(bot, NPC_MAEXXNA))
        return RAID_RUN_WING_NAXX_ARACHNID;

    return RAID_RUN_WING_NONE;
}

GameObject* NaxxRaidRunRouteProvider::FindWingReturnPortal(Player* bot, float range) const
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

bool NaxxRaidRunRouteProvider::IsWingComplete(Player* bot, uint8 wing) const
{
    return FindFirstIncompleteStep(bot, wing) >= GetStepCount(wing);
}

bool NaxxRaidRunRouteProvider::IsBossAlive(Player* bot, uint32 bossEntry, float range) const
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

bool NaxxRaidRunRouteProvider::IsBossEncounterDone(Player* bot, uint32 bossEntry) const
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

Creature* NaxxRaidRunRouteProvider::FindClearableTrash(Player* bot, RaidRunRouteStep const& step) const
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

        // Construct / Grobbulus floors stack in XY. 2D radius must not pull the other lab.
        if (std::fabs(creature->GetPositionZ() - step.z) > 8.0f)
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

bool NaxxRaidRunRouteProvider::IsStepComplete(Player* bot, uint8 wing, uint8 index) const
{
    std::vector<RaidRunRouteStep> const& steps = GetSteps(wing);
    if (!bot || index >= steps.size())
        return true;

    RaidRunRouteStep const& step = steps[index];
    if (step.portalGoEntry)
        return IsAtHub(bot);

    if (step.bossEntry)
    {
        if (!IsBossEncounterDone(bot, step.bossEntry))
            return false;

        return FindClearableTrash(bot, step) == nullptr;
    }

    if (step.clearRadius > 0.0f)
        return FindClearableTrash(bot, step) == nullptr && IsTravelStepArrived(bot, step);

    return IsTravelStepArrived(bot, step);
}

uint8 NaxxRaidRunRouteProvider::FindFirstIncompleteStep(Player* bot, uint8 wing) const
{
    uint8 const count = GetStepCount(wing);
    if (!bot)
        return 0;

    std::vector<RaidRunRouteStep> const& steps = GetSteps(wing);
    uint8 segmentStart = 0;
    for (uint8 i = 0; i < count; ++i)
    {
        RaidRunRouteStep const& step = steps[i];
        if (step.portalGoEntry)
            return IsAtHub(bot) ? count : i;

        if (step.bossEntry)
        {
            if (!IsBossEncounterDone(bot, step.bossEntry))
                return segmentStart;

            segmentStart = static_cast<uint8>(i + 1);
        }
    }

    return count;
}

void RegisterNaxxRaidRunRoute()
{
    static NaxxRaidRunRouteProvider naxxProvider;
    sRaidRunMgr.RegisterProvider(533, &naxxProvider);
}

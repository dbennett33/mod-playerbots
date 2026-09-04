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
// Arachnid wing (map 533). Rebuilt 2026-09-04 from base-DB spawn + gameobject data
// (.agents/plans/raid-run-automation/spawn-data/): every travel pin is a trash-pack
// centroid or an instance door, so X/Y/Z are ground-verified.
//
// Flow per instance_naxxramas.cpp doors: enter through ANUB_GATE 181126 (3202,-3476),
// kill Anub, leave BACK through the room door and head SOUTH to ANUB_NEXT_GATE 181195
// (3144,-3572; opens on Anub kill), flat z287 hall SE, ramp down at (3297,-3707) to
// FAERLINA_WEB 181235 (3318,-3695) — her room door, z259. After Faerlina: back out the
// web, up over the z287 web hump, down to the z274 lower hall, west through
// FAERLINA_GATE 194022 (3122,-3788), south corner, then the east ramp up to z294 and
// MAEXXNA_GATE 181209 (3427,-3846).
//
// The old route left Anub's platform to the SE (3273,-3510 -> 3320,-3570): the game
// data has no door or corridor there — that was the post-Anub wall-run.
//
// Faerlina room cultists (15980) / acolytes (15981) sit in four 8-packs in formation
// with her (groupAI member-assist). Clear all four before pulling her.
std::vector<RaidRunRouteStep> const arachnidSteps =
{
    // Zone-in point (game_tele 5191). Safe wipe-recovery anchor: always on-mesh.
    { "Naxx entrance", 3005.68f, -3447.77f, 293.93f, 533, 0, 12.0f, 0.0f },
    { "Arachnid hall", 3111.80f, -3493.50f, 287.10f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Anub door", 3186.50f, -3477.20f, 287.10f, 533, 0, 10.0f, 0.0f, 16.0f },
    // Anub spawn (east end). Do not use room center 3273,-3477 — that drags him off the door.
    { "Anub'Rekhan", 3308.59f, -3476.29f, 287.16f, 533, 15956, 10.0f, 40.0f },
    // Back out west through the room door (181126, opens after the kill), then south.
    { "Anub room west", 3202.70f, -3475.90f, 287.00f, 533, 0, 12.0f, 0.0f },
    { "Anub exit hall", 3145.90f, -3536.30f, 287.10f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Anub exit gate", 3144.00f, -3572.20f, 287.10f, 533, 0, 12.0f, 0.0f },
    // Wide flat hall (all z287.1) heading SE toward Faerlina's overlook.
    { "Spider hall east", 3205.20f, -3578.40f, 287.10f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Spider hall south", 3148.10f, -3635.10f, 287.10f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Spider hall bend", 3195.30f, -3645.00f, 287.10f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Faerlina hall", 3224.70f, -3653.60f, 287.10f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Faerlina overlook", 3240.80f, -3688.20f, 287.10f, 533, 0, 10.0f, 0.0f, 14.0f },
    // Skitterer pack on the descent into her room.
    { "Faerlina ramp", 3297.30f, -3707.30f, 270.50f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Faerlina web door", 3318.70f, -3695.80f, 259.10f, 533, 0, 12.0f, 0.0f },
    { "Faerlina right front", 3334.40f, -3648.10f, 259.20f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina right rear", 3328.00f, -3669.50f, 259.20f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina left rear", 3375.90f, -3669.90f, 259.20f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Faerlina left front", 3370.40f, -3648.10f, 259.20f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Grand Widow Faerlina", 3353.25f, -3620.10f, 261.08f, 533, 15953, 10.0f, 40.0f, 70.0f },
    // Exit through the web door again, up over the z287 hump, down to the lower hall.
    { "Faerlina web exit", 3318.70f, -3695.80f, 259.10f, 533, 0, 12.0f, 0.0f },
    { "Maexxna ramp", 3297.30f, -3707.30f, 270.50f, 533, 0, 12.0f, 0.0f },
    { "Maexxna landing", 3240.80f, -3688.20f, 287.10f, 533, 0, 12.0f, 0.0f },
    { "Maexxna descent", 3229.40f, -3738.50f, 282.70f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna descent floor", 3244.50f, -3768.70f, 275.90f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna lower hall", 3221.70f, -3798.60f, 274.00f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna west skitters", 3171.10f, -3802.80f, 273.90f, 533, 0, 10.0f, 0.0f, 14.0f },
    // Creeper pack in front of FAERLINA_GATE 194022.
    { "Maexxna west hall", 3144.70f, -3780.00f, 274.00f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna south corner", 3109.00f, -3880.40f, 267.60f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna south ramp", 3224.40f, -3876.80f, 284.60f, 533, 0, 10.0f, 0.0f, 12.0f },
    { "Maexxna south venoms", 3293.30f, -3906.70f, 294.70f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maexxna south hall", 3312.30f, -3879.00f, 294.70f, 533, 0, 10.0f, 0.0f, 14.0f },
    // Pack in front of MAEXXNA_GATE 181209.
    { "Maexxna gate", 3401.20f, -3823.30f, 294.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maexxna", 3511.38f, -3921.58f, 299.51f, 533, 15952, 10.0f, 40.0f },
    // GO 181575 casts 28444 to hub 3005.51,-3434.64,304. Do not walk the wing backwards.
    { "Maexxna portal", 3465.16f, -3940.45f, 308.79f, 533, 0, 8.0f, 0.0f, 0.0f, 181575 }
};

// Construct quarter is north of the hub. Travel pins re-anchored 2026-09-04 to spawn
// pack centroids / door GOs (same data set as Arachnid). Grobbulus ramp pins are the
// in-game-GPS'd originals (no spawns live on the ramp) and now appear on the way DOWN
// too — the old route jumped lab (z311) -> Gluth hall (z294) with no pins, which is a
// stacked-floor clip hazard. Extra Z pins on the ramp only — the lab stacks over the
// first rooms and mmap will clip without them.
std::vector<RaidRunRouteStep> const constructSteps =
{
    // Zone-in point (game_tele 5191). Safe wipe-recovery anchor.
    { "Naxx entrance", 3005.68f, -3447.77f, 293.93f, 533, 0, 12.0f, 0.0f },
    { "Construct entrance", 3046.70f, -3430.00f, 298.20f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Construct first", 3087.40f, -3367.60f, 298.40f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Construct second", 3078.30f, -3313.20f, 294.50f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Construct hall", 3106.20f, -3288.50f, 294.30f, 533, 0, 10.0f, 0.0f, 16.0f },
    // Embalming Slime room (16 spawns) before Patchwerk.
    { "Construct slime", 3135.90f, -3212.80f, 294.10f, 533, 0, 10.0f, 0.0f, 22.0f },
    { "Patchwerk", 3256.36f, -3230.33f, 294.06f, 533, 16028, 12.0f, 45.0f, 45.0f },
    // GO 181123 (Patchwerk gate).
    { "Patchwerk gate", 3318.00f, -3254.30f, 293.30f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp", 3295.0f, -3285.0f, 300.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp mid", 3285.0f, -3315.0f, 305.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus ramp top", 3265.0f, -3345.0f, 309.00f, 533, 0, 12.0f, 0.0f },
    { "Grobbulus", 3227.58f, -3378.30f, 311.33f, 533, 15931, 12.0f, 45.0f, 40.0f },
    // Walk back DOWN the same ramp — never straight-line from the lab to the hall below.
    { "Grobbulus ramp down", 3285.0f, -3315.0f, 305.00f, 533, 0, 12.0f, 0.0f },
    { "Patchwerk gate return", 3318.00f, -3254.30f, 293.30f, 533, 0, 12.0f, 0.0f },
    { "Gluth hall", 3322.60f, -3226.70f, 294.10f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Gluth", 3283.09f, -3156.96f, 297.79f, 533, 15932, 12.0f, 45.0f, 35.0f },
    // GO 181120 (Gluth gate).
    { "Gluth gate", 3339.16f, -3100.64f, 296.81f, 533, 0, 12.0f, 0.0f },
    // WotLK geist/colossus packs between Gluth and Thaddius.
    { "Geist hall", 3411.00f, -3084.50f, 294.70f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Colossus hall", 3405.50f, -3034.00f, 295.20f, 533, 0, 10.0f, 0.0f, 16.0f },
    // GO 181121 (Thaddius gate).
    { "Thaddius gate", 3421.86f, -3017.51f, 295.62f, 533, 0, 12.0f, 0.0f },
    // Walk to the low platform before pulling; bots on Feugen/Stalagg high bridges (Z 312)
    // cannot see Thaddius below (Z 302) so the phase-transition trigger never fires.
    // This pin forces mmap to descend to the correct floor first.
    { "Thaddius platform", 3480.0f, -2960.0f, 302.91f, 533, 0, 15.0f, 0.0f },
    { "Thaddius", 3513.84f, -2926.55f, 302.91f, 533, 15928, 15.0f, 80.0f, 0.0f }
};

// Plague quarter is west of the hub. Re-anchored 2026-09-04: door pins are exact GO
// spawns (181198-181241); the Noth->Heigan "bat tunnel" pins now use GROUND-mob Z
// (larva/grub packs, z 253-266) — the old interpolated pins (z 267/273) floated ~10yd
// above the real tunnel floor, so the leader could never satisfy the |Z|<=5 arrive
// check there. The Loatheb pin now sits 22yd from his spawn (2909,-3997); the old pin
// (2877,-3967) was 44yd out — outside the 40yd pull radius, so the pull never fired.
std::vector<RaidRunRouteStep> const plagueSteps =
{
    // Zone-in point (game_tele 5191). Safe wipe-recovery anchor.
    { "Naxx entrance", 3005.68f, -3447.77f, 293.93f, 533, 0, 12.0f, 0.0f },
    // GO 181198 (Plague quarter entrance).
    { "Plague entrance", 2963.20f, -3476.80f, 297.60f, 533, 0, 12.0f, 0.0f },
    { "Plague upper hall", 2905.0f, -3485.0f, 297.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    // GO 181199.
    { "Plague west hall", 2847.43f, -3489.47f, 297.84f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Noth ramp top", 2820.0f, -3489.0f, 290.00f, 533, 0, 12.0f, 0.0f },
    { "Noth ramp mid", 2785.0f, -3489.0f, 276.00f, 533, 0, 12.0f, 0.0f },
    // GO 181200 (Noth entry).
    { "Noth door", 2737.66f, -3489.72f, 262.10f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Noth the Plaguebringer", 2684.94f, -3502.53f, 261.31f, 533, 15954, 12.0f, 40.0f, 30.0f },
    // GO 181201 (Noth exit), then the bat tunnel: S-curve over the ground packs.
    { "Noth exit", 2684.28f, -3559.36f, 261.91f, 533, 0, 12.0f, 0.0f },
    { "Bat tunnel west", 2711.30f, -3601.40f, 260.60f, 533, 0, 10.0f, 0.0f, 22.0f },
    { "Bat tunnel floor", 2760.00f, -3613.80f, 254.70f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Bat tunnel north", 2788.20f, -3580.00f, 253.70f, 533, 0, 10.0f, 0.0f, 18.0f },
    { "Bat cave east", 2868.40f, -3603.10f, 266.10f, 533, 0, 10.0f, 0.0f, 22.0f },
    { "Bat cave rise", 2866.10f, -3664.80f, 274.60f, 533, 0, 10.0f, 0.0f, 16.0f },
    // GO 181202 (Heigan entry).
    { "Heigan door", 2822.93f, -3685.30f, 273.54f, 533, 0, 12.0f, 0.0f, 14.0f },
    // East of the platform, on the floor — not 2794,-3706 (Heigan's dance platform).
    { "Heigan the Unclean", 2793.80f, -3685.00f, 273.67f, 533, 15936, 12.0f, 35.0f, 0.0f },
    // GO 181203 (Heigan exit), then the maggot field: west edge + south row packs.
    { "Heigan exit", 2771.50f, -3737.34f, 273.60f, 533, 0, 12.0f, 0.0f },
    { "Maggot field west", 2757.40f, -3763.00f, 273.80f, 533, 0, 10.0f, 0.0f, 14.0f },
    { "Maggot field south", 2800.00f, -3783.10f, 273.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maggot field mid", 2847.80f, -3779.50f, 273.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Maggot field east", 2890.00f, -3784.00f, 273.70f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Loatheb south hall", 2909.69f, -3818.45f, 273.55f, 533, 0, 10.0f, 0.0f, 16.0f },
    { "Loatheb approach", 2909.69f, -3900.00f, 273.55f, 533, 0, 10.0f, 0.0f, 16.0f },
    // GO 181241 (Loatheb gate).
    { "Loatheb gate", 2909.69f, -3947.28f, 273.55f, 533, 0, 12.0f, 0.0f, 18.0f },
    { "Loatheb", 2896.00f, -3980.00f, 273.50f, 533, 16011, 12.0f, 45.0f, 0.0f },
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
